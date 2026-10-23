#include "Archive.h"

#include <system_error>
#include <utility>

#ifdef MP_WITH_ARCHIVES
#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#endif

namespace mp {

ExtractedArchive::ExtractedArchive(std::filesystem::path directory, std::vector<Entry> entries)
    : directory_(std::move(directory))
    , entries_(std::move(entries))
{
}

ExtractedArchive::~ExtractedArchive()
{
    if (!directory_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }
}

ExtractedArchive::ExtractedArchive(ExtractedArchive&& other) noexcept
    : directory_(std::exchange(other.directory_, {}))
    , entries_(std::move(other.entries_))
{
}

ExtractedArchive& ExtractedArchive::operator=(ExtractedArchive&& other) noexcept
{
    if (this != &other) {
        ExtractedArchive old(std::move(*this));
        directory_ = std::exchange(other.directory_, {});
        entries_ = std::move(other.entries_);
    }
    return *this;
}

#ifdef MP_WITH_ARCHIVES

namespace {

// Difesa contro archivi malevoli o corrotti ("zip bomb").
constexpr std::uintmax_t kMaxEntryBytes = 256u * 1024 * 1024;

struct ArchiveDeleter {
    void operator()(archive* a) const { archive_read_free(a); }
};

std::filesystem::path makeTempDirectory()
{
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto dir = std::filesystem::temp_directory_path() / ("megaplayer-" + std::to_string(random()));
        if (std::filesystem::create_directory(dir))
            return dir;
    }
    throw std::runtime_error("impossibile creare una directory temporanea");
}

// Percorso relativo sicuro dentro la directory di estrazione: niente
// percorsi assoluti né "..". Restituisce un percorso vuoto se l'elemento va
// scartato.
std::filesystem::path safeRelativePath(std::string name)
{
    for (char& c : name) {
        if (c == '\\')
            c = '/'; // archivi creati su DOS/Amiga
        else if (c == ':')
            c = '_'; // volumi Amiga ("DH0:") e lettere di unità
    }

    std::filesystem::path result;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = std::min(name.find('/', start), name.size());
        const std::string part = name.substr(start, end - start);
        if (part == "..")
            return {};
        if (!part.empty() && part != ".")
            result /= part;
        start = end + 1;
    }
    return result;
}

std::string lastError(archive* a)
{
    const char* message = archive_error_string(a);
    return message ? message : "errore sconosciuto";
}

} // namespace

std::optional<ExtractedArchive> openArchive(const std::filesystem::path& path)
{
    std::unique_ptr<archive, ArchiveDeleter> reader(archive_read_new());
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());
    if (archive_read_open_filename(reader.get(), path.string().c_str(), 64 * 1024) != ARCHIVE_OK)
        return std::nullopt;

    // Si crea la directory solo dopo aver letto il primo elemento: un file
    // che non è un archivio fallisce già qui.
    archive_entry* entry = nullptr;
    int status = archive_read_next_header(reader.get(), &entry);
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN)
        return std::nullopt;

    const std::filesystem::path root = makeTempDirectory();
    // Se l'estrazione si interrompe con un'eccezione, la directory va tolta.
    struct DirectoryGuard {
        std::filesystem::path dir;
        ~DirectoryGuard()
        {
            std::error_code ignored;
            if (!dir.empty())
                std::filesystem::remove_all(dir, ignored);
        }
    } guard{root};

    std::vector<ExtractedArchive::Entry> entries;
    std::vector<char> buffer(64 * 1024);
    for (; status == ARCHIVE_OK || status == ARCHIVE_WARN; status = archive_read_next_header(reader.get(), &entry)) {
        if (archive_entry_filetype(entry) != AE_IFREG)
            continue;

        const char* utf8 = archive_entry_pathname_utf8(entry);
        const std::string name = utf8 ? utf8 : archive_entry_pathname(entry);
        const std::filesystem::path relative = safeRelativePath(name);
        if (relative.empty())
            continue;

        const std::filesystem::path target = root / relative;
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary);
        if (!out)
            throw std::runtime_error("impossibile estrarre " + name);

        std::uintmax_t total = 0;
        la_ssize_t got;
        while ((got = archive_read_data(reader.get(), buffer.data(), buffer.size())) > 0) {
            total += static_cast<std::uintmax_t>(got);
            if (total > kMaxEntryBytes)
                throw std::runtime_error(name + ": file troppo grande");
            out.write(buffer.data(), got);
        }
        if (got < 0)
            throw std::runtime_error(name + ": " + lastError(reader.get()));

        entries.push_back({target, relative.generic_string()});
    }
    if (status != ARCHIVE_EOF)
        throw std::runtime_error("archivio danneggiato: " + lastError(reader.get()));

    guard.dir.clear();
    return ExtractedArchive(root, std::move(entries));
}

#else

std::optional<ExtractedArchive> openArchive(const std::filesystem::path&)
{
    return std::nullopt;
}

#endif

} // namespace mp
