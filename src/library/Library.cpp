#include "Library.h"

#include "core/Archive.h"
#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"
#include "decoders/TfmxFiles.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace mp {

namespace {

namespace fs = std::filesystem;

constexpr int kSchemaVersion = 1;
// Quando cambia come si leggono i file (un campo nuovo, una piattaforma
// assegnata diversamente) si incrementa: la scansione successiva rilegge
// tutto. Lo stesso succede quando cambiano i backend compilati, perché file
// che prima non si riconoscevano ora magari si suonano.
constexpr int kInspectorVersion = 1;
// Oltre questo numero di sottobrani si indicizzano solo i primi: ognuno costa
// un avvio per conoscerne la durata.
constexpr int kMaxSubsongs = 256;
// Ogni quanto la scansione rende visibile alle ricerche quello che ha letto.
constexpr auto kCommitInterval = std::chrono::seconds(1);

const char* const kSchema = R"(
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS files (
    path TEXT PRIMARY KEY,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    scan INTEGER NOT NULL
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS tracks (
    ref TEXT PRIMARY KEY,
    file TEXT NOT NULL REFERENCES files(path) ON DELETE CASCADE,
    entry TEXT NOT NULL,
    name TEXT NOT NULL,
    folder TEXT NOT NULL,
    backend TEXT NOT NULL,
    format TEXT NOT NULL,
    player TEXT NOT NULL,
    platform TEXT NOT NULL,
    title TEXT NOT NULL,
    author TEXT NOT NULL,
    subsongs INTEGER NOT NULL,
    start_subsong INTEGER NOT NULL,
    duration REAL
);
CREATE INDEX IF NOT EXISTS tracks_file ON tracks(file);
CREATE INDEX IF NOT EXISTS tracks_platform ON tracks(platform);
CREATE INDEX IF NOT EXISTS tracks_format ON tracks(format);
CREATE INDEX IF NOT EXISTS tracks_order ON tracks(folder COLLATE NOCASE, name COLLATE NOCASE);
CREATE TABLE IF NOT EXISTS subsongs (
    track TEXT NOT NULL REFERENCES tracks(ref) ON DELETE CASCADE,
    number INTEGER NOT NULL,
    duration REAL,
    PRIMARY KEY (track, number)
) WITHOUT ROWID;
)";

const char* const kTrackColumns =
    "ref, file, entry, name, folder, backend, format, player, platform, title, author, subsongs, "
    "start_subsong, duration";

std::runtime_error databaseError(sqlite3* db)
{
    return std::runtime_error(std::string("database della libreria: ") + sqlite3_errmsg(db));
}

void exec(sqlite3* db, const char* sql)
{
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw databaseError(db);
}

struct DatabaseCloser {
    void operator()(sqlite3* db) const { sqlite3_close(db); }
};
using Database = std::unique_ptr<sqlite3, DatabaseCloser>;

Database openDatabase(const fs::path& path)
{
    sqlite3* raw = nullptr;
    // SQLite vuole il nome in UTF-8, anche su Windows.
    const std::u8string name = path.u8string();
    const int status = sqlite3_open_v2(reinterpret_cast<const char*>(name.c_str()), &raw,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    Database db(raw);
    if (status != SQLITE_OK)
        throw raw ? databaseError(raw) : std::runtime_error("database della libreria: memoria esaurita");
    // La scansione scrive mentre l'interfaccia legge: in WAL i lettori non
    // aspettano, e chi scrive aspetta l'altro scrittore invece di fallire.
    sqlite3_busy_timeout(db.get(), 10000);
    exec(db.get(), "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL; PRAGMA foreign_keys = ON;");
    return db;
}

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql)
    {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement_, nullptr) != SQLITE_OK)
            throw databaseError(db);
    }
    ~Statement() { sqlite3_finalize(statement_); }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // I parametri si contano da 1, come in SQLite.
    Statement& bind(int index, const std::string& value)
    {
        sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Statement& bind(int index, int64_t value)
    {
        sqlite3_bind_int64(statement_, index, value);
        return *this;
    }
    Statement& bind(int index, std::optional<double> value)
    {
        if (value)
            sqlite3_bind_double(statement_, index, *value);
        else
            sqlite3_bind_null(statement_, index);
        return *this;
    }

    // true se c'è una riga da leggere.
    bool step()
    {
        const int status = sqlite3_step(statement_);
        if (status == SQLITE_ROW)
            return true;
        if (status == SQLITE_DONE)
            return false;
        throw databaseError(sqlite3_db_handle(statement_));
    }

    // Esegue un comando e prepara il successivo.
    void run()
    {
        step();
        reset();
    }

    void reset()
    {
        sqlite3_reset(statement_);
        sqlite3_clear_bindings(statement_);
    }

    std::string text(int column) const
    {
        const auto* value = sqlite3_column_text(statement_, column);
        return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(statement_, column))
                     : std::string();
    }
    int64_t integer(int column) const { return sqlite3_column_int64(statement_, column); }
    std::optional<double> real(int column) const
    {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL)
            return std::nullopt;
        return sqlite3_column_double(statement_, column);
    }

private:
    sqlite3_stmt* statement_ = nullptr;
};

LibraryTrack readTrack(const Statement& row)
{
    LibraryTrack track;
    track.ref = row.text(0);
    track.file = row.text(1);
    track.entry = row.text(2);
    track.name = row.text(3);
    track.folder = row.text(4);
    track.backend = row.text(5);
    track.format = row.text(6);
    track.player = row.text(7);
    track.platform = row.text(8);
    track.title = row.text(9);
    track.author = row.text(10);
    track.subsongCount = static_cast<int>(row.integer(11));
    track.startSubsong = static_cast<int>(row.integer(12));
    track.duration = row.real(13);
    return track;
}

std::string readSetting(sqlite3* db, const std::string& key)
{
    Statement select(db, "SELECT value FROM settings WHERE key = ?");
    select.bind(1, key);
    return select.step() ? select.text(0) : std::string();
}

void writeSetting(sqlite3* db, const std::string& key, const std::string& value)
{
    Statement upsert(db, "INSERT INTO settings (key, value) VALUES (?, ?) "
                         "ON CONFLICT (key) DO UPDATE SET value = excluded.value");
    upsert.bind(1, key).bind(2, value).run();
}

// Le condizioni di una ricerca, da aggiungere a una SELECT su tracks.
struct Filter {
    std::string where = " WHERE 1";
    std::vector<std::string> parameters;

    void text(const std::string& text)
    {
        std::istringstream words(text);
        std::string word;
        while (words >> word) {
            // % e _ nel testo cercato valgono per sé stessi.
            std::string pattern = "%";
            for (const char c : word) {
                if (c == '%' || c == '_' || c == '\\')
                    pattern += '\\';
                pattern += c;
            }
            pattern += '%';
            where += " AND (title LIKE ? ESCAPE '\\' OR author LIKE ? ESCAPE '\\' OR name LIKE ? ESCAPE '\\'"
                     " OR folder LIKE ? ESCAPE '\\')";
            parameters.insert(parameters.end(), 4, pattern);
        }
    }

    void oneOf(const char* column, const std::vector<std::string>& values)
    {
        if (values.empty())
            return;
        where += std::string(" AND ") + column + " IN (";
        for (size_t i = 0; i < values.size(); ++i) {
            where += i == 0 ? "?" : ", ?";
            parameters.push_back(values[i]);
        }
        where += ")";
    }

    void bindTo(Statement& statement) const
    {
        for (size_t i = 0; i < parameters.size(); ++i)
            statement.bind(static_cast<int>(i + 1), parameters[i]);
    }
};

std::vector<LibraryFacet> countBy(sqlite3* db, const char* column, const Filter& filter, const char* order)
{
    Statement select(db, std::string("SELECT ") + column + ", COUNT(*) FROM tracks" + filter.where + " GROUP BY "
                             + column + " ORDER BY " + order);
    filter.bindTo(select);
    std::vector<LibraryFacet> facets;
    while (select.step())
        facets.push_back({select.text(0), static_cast<int>(select.integer(1))});
    return facets;
}

// Un brano letto da un file, con la durata di ogni sottobrano.
struct Inspected {
    LibraryTrack track;
    std::vector<std::optional<double>> durations;
};

void copyInfo(LibraryTrack& track, const TrackInfo& info)
{
    track.backend = info.backend;
    track.format = info.format;
    track.player = info.player;
    track.platform = info.platform;
    track.title = info.title;
    track.author = info.author;
    track.subsongCount = info.subsongCount;
}

// Come la riproduzione: il backend col punteggio più alto, il successivo se
// il file non si apre.
std::optional<Inspected> inspectSource(const SourceFile& source, const DecoderRegistry& registry)
{
    for (const ProbeResult& candidate : registry.probe(source)) {
        try {
            Inspected out;
            if (const std::optional<TrackInfo> info = candidate.factory->describe(source)) {
                copyInfo(out.track, *info);
                return out;
            }
            const auto decoder = candidate.factory->open(source, OutputSpec{});
            const TrackInfo first = decoder->info();
            copyInfo(out.track, first);
            out.track.startSubsong = first.currentSubsong;
            out.track.duration = first.duration;
            out.durations.resize(static_cast<size_t>(std::clamp(first.subsongCount, 1, kMaxSubsongs)));
            for (int i = 0; i < static_cast<int>(out.durations.size()); ++i) {
                if (i == first.currentSubsong) {
                    out.durations[static_cast<size_t>(i)] = first.duration;
                    continue;
                }
                try {
                    decoder->selectSubsong(i);
                    out.durations[static_cast<size_t>(i)] = decoder->info().duration;
                } catch (const std::exception&) {
                }
            }
            return out;
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

std::string relativeFolder(const fs::path& folder, const fs::path& root)
{
    std::error_code error;
    const std::string relative = fs::relative(folder, root, error).generic_string();
    return error || relative == "." ? std::string() : relative;
}

// I brani di un file: uno per un file musicale, quelli che contiene per un
// archivio, nessuno per il resto (testi, immagini, la metà con i campioni di
// un brano in due file).
std::vector<Inspected> inspectFile(const fs::path& path, const fs::path& root, const DecoderRegistry& registry)
{
    std::vector<Inspected> found;
    const std::string folder = relativeFolder(path.parent_path(), root);
    std::optional<ExtractedArchive> archive;
    try {
        archive = openArchive(path);
    } catch (const std::exception&) {
        return found; // archivio danneggiato
    }

    if (!archive) {
        if (isSampleHalf(path))
            return found;
        try {
            if (std::optional<Inspected> track = inspectSource(loadSourceFile(path), registry)) {
                track->track.ref = path.string();
                track->track.file = path.string();
                track->track.name = path.filename().string();
                track->track.folder = folder;
                found.push_back(std::move(*track));
            }
        } catch (const std::exception&) {
        }
        return found;
    }

    const std::string archivePath = relativeFolder(path, root);
    for (const ExtractedArchive::Entry& entry : archive->entries()) {
        try {
            std::optional<Inspected> track = inspectSource(loadSourceFile(entry.path), registry);
            if (!track)
                continue;
            const fs::path inner = fs::path(entry.name);
            const std::string innerFolder = inner.parent_path().generic_string();
            track->track.ref = path.string() + "/" + entry.name;
            track->track.file = path.string();
            track->track.entry = entry.name;
            track->track.name = inner.filename().string();
            track->track.folder = innerFolder.empty() ? archivePath : archivePath + "/" + innerFolder;
            found.push_back(std::move(*track));
        } catch (const std::exception&) {
        }
    }
    return found;
}

struct FoundFile {
    fs::path path;
    int64_t size;
    int64_t mtime;
};

} // namespace

Library::Library(const fs::path& database)
    : database_(database)
{
    std::error_code error;
    fs::create_directories(database.parent_path(), error);
    Database db = openDatabase(database);
    Statement version(db.get(), "PRAGMA user_version");
    version.step();
    const int64_t current = version.integer(0);
    version.reset();
    if (current > kSchemaVersion)
        throw std::runtime_error("database della libreria di una versione più recente di megaplayer");
    exec(db.get(), kSchema);
    exec(db.get(), ("PRAGMA user_version = " + std::to_string(kSchemaVersion)).c_str());
    db_ = db.release();
}

Library::~Library()
{
    sqlite3_close(db_);
}

fs::path Library::defaultPath()
{
    fs::path base;
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"))
        base = appData;
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        base = fs::path(home) / "Library" / "Application Support";
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
        base = data;
    else if (const char* home = std::getenv("HOME"))
        base = fs::path(home) / ".local" / "share";
#endif
    if (base.empty())
        base = fs::current_path();
    return base / "megaplayer" / "library.db";
}

std::string Library::setting(const std::string& key) const
{
    const std::lock_guard lock(mutex_);
    return readSetting(db_, key);
}

fs::path Library::root() const
{
    return setting("root");
}

void Library::setRoot(const fs::path& root)
{
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(fs::absolute(root, error), error);
    if (error)
        normalized = root;
    if (normalized == this->root())
        return;
    const std::lock_guard lock(mutex_);
    exec(db_, "BEGIN IMMEDIATE");
    try {
        exec(db_, "DELETE FROM files");
        writeSetting(db_, "root", normalized.string());
        exec(db_, "COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

ScanResult Library::scan(ScanProgress& progress, const std::atomic<bool>& cancel)
{
    ScanResult result;
    const fs::path rootPath = root();
    if (rootPath.empty())
        throw std::runtime_error("cartella della libreria non scelta");
    std::error_code error;
    if (!fs::is_directory(rootPath, error))
        throw std::runtime_error("la cartella " + rootPath.string() + " non esiste");

    // I file della cartella, senza quelli nascosti. Se la visita non arriva
    // in fondo, i file mancanti non si tolgono dall'indice.
    std::vector<FoundFile> found;
    bool complete = true;
    {
        fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        while (!error && it != end) {
            if (cancel) {
                complete = false;
                break;
            }
            const std::string name = it->path().filename().string();
            std::error_code entryError;
            if (!name.empty() && name[0] == '.') {
                if (it->is_directory(entryError))
                    it.disable_recursion_pending();
            } else if (it->is_regular_file(entryError)) {
                const auto size = static_cast<int64_t>(it->file_size(entryError));
                const auto mtime = static_cast<int64_t>(it->last_write_time(entryError).time_since_epoch().count());
                if (!entryError) {
                    found.push_back({it->path(), size, mtime});
                    progress.filesFound = static_cast<int>(found.size());
                }
            }
            it.increment(error);
        }
        if (error)
            complete = false;
    }
    progress.walking = false;
    result.filesFound = static_cast<int>(found.size());

    Database db = openDatabase(database_);
    std::unordered_map<std::string, std::pair<int64_t, int64_t>> known;
    {
        Statement select(db.get(), "SELECT path, size, mtime FROM files");
        while (select.step())
            known.emplace(select.text(0), std::make_pair(select.integer(1), select.integer(2)));
    }
    const std::string previous = readSetting(db.get(), "scan");
    const int64_t scanId = (previous.empty() ? 0 : std::stoll(previous)) + 1;

    // Una scansione ha i suoi backend: il riconoscimento tiene uno stato, e
    // la riproduzione usa i propri nello stesso momento.
    DecoderRegistry registry;
    registerBuiltinDecoders(registry);
    std::string inspector = std::to_string(kInspectorVersion);
    for (const auto& factory : registry.factories())
        inspector += " " + std::string(factory->name());
    if (inspector != readSetting(db.get(), "inspector"))
        known.clear();

    Statement markSeen(db.get(), "UPDATE files SET scan = ? WHERE path = ?");
    Statement deleteTracks(db.get(), "DELETE FROM tracks WHERE file = ?");
    Statement upsertFile(db.get(), "INSERT INTO files (path, size, mtime, scan) VALUES (?, ?, ?, ?) "
                                   "ON CONFLICT (path) DO UPDATE SET size = excluded.size, "
                                   "mtime = excluded.mtime, scan = excluded.scan");
    Statement insertTrack(db.get(), std::string("INSERT OR REPLACE INTO tracks (") + kTrackColumns
                                        + ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    Statement insertSubsong(db.get(), "INSERT OR REPLACE INTO subsongs (track, number, duration) VALUES (?, ?, ?)");

    exec(db.get(), "BEGIN IMMEDIATE");
    try {
        auto lastCommit = std::chrono::steady_clock::now();
        for (const FoundFile& file : found) {
            if (cancel) {
                result.cancelled = true;
                break;
            }
            const std::string path = file.path.string();
            const auto entry = known.find(path);
            if (entry != known.end() && entry->second == std::make_pair(file.size, file.mtime)) {
                markSeen.bind(1, scanId).bind(2, path).run();
            } else {
                const std::vector<Inspected> tracks = inspectFile(file.path, rootPath, registry);
                deleteTracks.bind(1, path).run();
                upsertFile.bind(1, path).bind(2, file.size).bind(3, file.mtime).bind(4, scanId).run();
                for (const Inspected& inspected : tracks) {
                    const LibraryTrack& t = inspected.track;
                    insertTrack.bind(1, t.ref).bind(2, t.file).bind(3, t.entry).bind(4, t.name).bind(5, t.folder);
                    insertTrack.bind(6, t.backend).bind(7, t.format).bind(8, t.player).bind(9, t.platform);
                    insertTrack.bind(10, t.title).bind(11, t.author).bind(12, int64_t{t.subsongCount});
                    insertTrack.bind(13, int64_t{t.startSubsong}).bind(14, t.duration).run();
                    for (size_t i = 0; i < inspected.durations.size(); ++i)
                        insertSubsong.bind(1, t.ref).bind(2, static_cast<int64_t>(i)).bind(3, inspected.durations[i]).run();
                }
                ++result.filesRead;
                result.tracksFound += static_cast<int>(tracks.size());
                progress.filesRead = result.filesRead;
                progress.tracksFound = result.tracksFound;
            }
            ++progress.filesChecked;
            if (std::chrono::steady_clock::now() - lastCommit > kCommitInterval) {
                exec(db.get(), "COMMIT");
                exec(db.get(), "BEGIN IMMEDIATE");
                lastCommit = std::chrono::steady_clock::now();
            }
        }
        if (!result.cancelled && complete) {
            Statement remove(db.get(), "DELETE FROM files WHERE scan <> ?");
            remove.bind(1, scanId).run();
            result.filesRemoved = sqlite3_changes(db.get());
            writeSetting(db.get(), "inspector", inspector);
        }
        writeSetting(db.get(), "scan", std::to_string(scanId));
        exec(db.get(), "COMMIT");
    } catch (...) {
        sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    if (!complete)
        result.cancelled = true;
    return result;
}

LibraryPage Library::search(const LibraryQuery& query) const
{
    Filter filter;
    filter.text(query.text);
    filter.oneOf("platform", query.platforms);
    filter.oneOf("format", query.formats);

    const std::lock_guard lock(mutex_);
    LibraryPage page;
    {
        Statement count(db_, "SELECT COUNT(*) FROM tracks" + filter.where);
        filter.bindTo(count);
        count.step();
        page.total = static_cast<int>(count.integer(0));
    }
    Statement select(db_, std::string("SELECT ") + kTrackColumns + " FROM tracks" + filter.where
                              + " ORDER BY folder COLLATE NOCASE, name COLLATE NOCASE, ref LIMIT ? OFFSET ?");
    filter.bindTo(select);
    const int next = static_cast<int>(filter.parameters.size()) + 1;
    select.bind(next, int64_t{std::max(0, query.limit)}).bind(next + 1, int64_t{std::max(0, query.offset)});
    while (select.step())
        page.tracks.push_back(readTrack(select));
    return page;
}

LibraryFacets Library::facets(const LibraryQuery& query) const
{
    // Ogni gruppo si conta con il testo e con il filtro dell'altro gruppo:
    // scegliere un formato dice quanti brani di quel formato ha ogni
    // piattaforma, e viceversa.
    Filter byPlatform;
    byPlatform.text(query.text);
    byPlatform.oneOf("format", query.formats);
    Filter byFormat;
    byFormat.text(query.text);
    byFormat.oneOf("platform", query.platforms);

    const std::lock_guard lock(mutex_);
    LibraryFacets facets;
    facets.platforms = countBy(db_, "platform", byPlatform, "COUNT(*) DESC, platform");
    facets.formats = countBy(db_, "format", byFormat, "format COLLATE NOCASE");
    return facets;
}

std::optional<LibraryTrack> Library::track(const std::string& ref) const
{
    const std::lock_guard lock(mutex_);
    Statement select(db_, std::string("SELECT ") + kTrackColumns + " FROM tracks WHERE ref = ?");
    select.bind(1, ref);
    if (!select.step())
        return std::nullopt;
    return readTrack(select);
}

std::vector<std::optional<double>> Library::subsongDurations(const std::string& ref) const
{
    const std::lock_guard lock(mutex_);
    Statement select(db_, "SELECT number, duration FROM subsongs WHERE track = ? ORDER BY number");
    select.bind(1, ref);
    std::vector<std::optional<double>> durations;
    while (select.step()) {
        const auto number = static_cast<size_t>(select.integer(0));
        if (durations.size() <= number)
            durations.resize(number + 1);
        durations[number] = select.real(1);
    }
    return durations;
}

} // namespace mp
