// Verifica la libreria con i toni di prova di tests/data, copiati in una
// cartella con sottocartelle, un file nascosto, un file di testo e (se c'è
// libarchive) uno zip: prima scansione, scansione senza novità, file cambiati
// e spariti, ricerca, filtri, conteggi, cartelle, cambio di radice, interruzione,
// playlist, coda e impostazioni, l'API C con la scansione in background, e
// come TrackCollector riapre per ref i brani negli archivi.
// Uso: library_test DIR DATI [CMAKE]   (CMAKE serve a creare lo zip)

#include "capi/megaplayer.h"
#include "core/DecoderRegistry.h"
#include "core/TrackList.h"
#include "library/Library.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::fprintf(stderr, "FALLITO: %s\n", what.c_str());
        ++failures;
    }
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary) << text;
}

std::optional<mp::LibraryTrack> find(const mp::Library& library, const std::string& name)
{
    for (const mp::LibraryTrack& track : library.search({}).tracks) {
        if (track.name == name)
            return track;
    }
    return std::nullopt;
}

mp::ScanResult scan(mp::Library& library)
{
    mp::ScanProgress progress;
    const std::atomic<bool> cancel{false};
    const mp::ScanResult result = library.scan(progress, cancel);
    check(!progress.walking && progress.filesChecked == result.filesFound, "avanzamento della scansione");
    return result;
}

void testLibrary(const fs::path& root, const fs::path& database, bool withArchive)
{
    mp::Library library(database);
    check(library.root().empty(), "radice già scelta in un database nuovo");
    library.setRoot(root);
    check(fs::equivalent(library.root(), root), "radice " + library.root().string());

    // tono.{wav,mp3,flac,ogg}, testo, file nascosto, zip con un tono dentro.
    const int expectedFiles = withArchive ? 6 : 5;
    const int expectedTracks = withArchive ? 5 : 4;
    mp::ScanResult result = scan(library);
    check(result.filesFound == expectedFiles, "file trovati: " + std::to_string(result.filesFound));
    check(result.filesRead == expectedFiles && result.tracksFound == expectedTracks,
          "prima scansione: " + std::to_string(result.filesRead) + " letti, " + std::to_string(result.tracksFound)
              + " brani");
    check(!result.cancelled && result.filesRemoved == 0, "prima scansione completa");

    const std::optional<mp::LibraryTrack> flac = find(library, "tono.flac");
    check(flac.has_value(), "tono.flac nella libreria");
    if (flac) {
        check(flac->folder == "Archivio/Toni", "cartella di tono.flac: " + flac->folder);
        check(flac->backend == "miniaudio" && flac->format == "FLAC" && flac->platform == "audio",
              "formato di tono.flac: " + flac->backend + " " + flac->format + " " + flac->platform);
        check(flac->title == "Prova \xc3\xa0\xc3\xa8\xc3\xac" && flac->author == "Megaplayer",
              "tag di tono.flac: " + flac->title + " / " + flac->author);
        check(flac->subsongCount == 1 && flac->duration && std::fabs(*flac->duration - 1.0) < 0.01,
              "sottobrani e durata di tono.flac");
        check(flac->ref == (root / "Archivio" / "Toni" / "tono.flac").string() && flac->entry.empty(),
              "riferimento di tono.flac: " + flac->ref);
        const auto durations = library.subsongDurations(flac->ref);
        check(durations.size() == 1 && durations[0] && std::fabs(*durations[0] - 1.0) < 0.01,
              "durate dei sottobrani di tono.flac");
        check(library.track(flac->ref).has_value(), "track() per riferimento");
    }
    check(!find(library, ".nascosto.wav") && !find(library, "leggimi.txt"), "file nascosti o non musicali");

    if (withArchive) {
        const std::optional<mp::LibraryTrack> zipped = find(library, "zippato.wav");
        check(zipped.has_value(), "il tono dentro lo zip");
        if (zipped) {
            const std::string zip = (root / "toni.zip").string();
            check(zipped->file == zip && zipped->entry == "dentro/zippato.wav", "file e voce nello zip");
            check(zipped->ref == zip + "/dentro/zippato.wav", "riferimento nello zip: " + zipped->ref);
            check(zipped->folder == "toni.zip/dentro", "cartella nello zip: " + zipped->folder);
        }
    }

    // Senza novità non si apre niente.
    result = scan(library);
    check(result.filesRead == 0 && result.filesRemoved == 0, "seconda scansione: " + std::to_string(result.filesRead) + " letti");

    // Un file cambiato si rilegge, uno sparito si toglie.
    writeText(root / "leggimi.txt", "non è musica, ma è cambiato");
    fs::remove(root / "tono.ogg");
    result = scan(library);
    check(result.filesRead == 1 && result.filesRemoved == 1,
          "dopo le modifiche: " + std::to_string(result.filesRead) + " letti, " + std::to_string(result.filesRemoved)
              + " tolti");
    check(!find(library, "tono.ogg"), "tono.ogg ancora nella libreria");

    // Ricerca e filtri.
    mp::LibraryQuery query;
    check(library.search(query).total == expectedTracks - 1, "totale dei brani");
    query.text = "toni";
    check(library.search(query).total == (withArchive ? 2 : 1), "ricerca nella cartella");
    query.text = "prova MEGAPLAYER";
    check(library.search(query).total == expectedTracks - 1, "ricerca su titolo e autore, più parole");
    query.text = "tono_";
    check(library.search(query).total == 0, "_ cercato come carattere");
    query.text.clear();
    query.formats = {"FLAC", "MP3"};
    check(library.search(query).total == 2, "filtro per formato");
    query.platforms = {"amiga"};
    check(library.search(query).total == 0, "filtro per piattaforma");
    query = {};
    query.limit = 2;
    query.offset = 1;
    const mp::LibraryPage page = library.search(query);
    check(page.total == expectedTracks - 1 && page.tracks.size() == 2, "pagina della ricerca");

    query = {};
    query.formats = {"WAV"};
    const mp::LibraryFacets facets = library.facets(query);
    check(facets.platforms.size() == 1 && facets.platforms[0].value == "audio"
              && facets.platforms[0].count == (withArchive ? 2 : 1),
          "piattaforme contate con il filtro sul formato");
    check(facets.formats.size() == 3, "formati contati senza il proprio filtro: " + std::to_string(facets.formats.size()));

    // Cartelle: tono.wav e tono.mp3 nella radice, tono.flac in
    // Archivio/Toni, zippato.wav in toni.zip/dentro.
    query = {};
    const std::vector<mp::LibraryFacet> top = library.folders(query);
    check(top.size() == (withArchive ? 2 : 1) && top[0].value == "Archivio" && top[0].count == 1
              && (!withArchive || (top[1].value == "toni.zip" && top[1].count == 1)),
          "cartelle della radice: " + std::to_string(top.size()));
    query.folder = "Archivio";
    const std::vector<mp::LibraryFacet> inner = library.folders(query);
    check(inner.size() == 1 && inner[0].value == "Toni" && inner[0].count == 1, "sottocartelle");
    check(library.search(query).total == 1, "ricerca in una cartella e sotto");
    check(library.facets(query).formats.size() == 1, "conteggi in una cartella");
    query.subfolders = false;
    check(library.search(query).total == 0, "solo i brani della cartella");
    query.folder.clear();
    check(library.search(query).total == 2, "i brani della radice");
    query = {};
    query.folder = "Archiv";
    check(library.search(query).total == 0, "una cartella non vale come prefisso");
    query = {};
    query.formats = {"MP3"};
    check(library.folders(query).empty(), "cartelle senza brani della ricerca");

    // Interrotta prima di cominciare: niente si toglie.
    fs::remove(root / "tono.mp3");
    mp::ScanProgress progress;
    const std::atomic<bool> stop{true};
    result = library.scan(progress, stop);
    check(result.cancelled && result.filesRemoved == 0 && find(library, "tono.mp3"), "scansione interrotta");

    // Playlist, coda e impostazioni.
    const std::string flacRef = (root / "Archivio" / "Toni" / "tono.flac").string();
    check(library.playlists().empty(), "playlist in un database nuovo");
    const int64_t favourites = library.createPlaylist("Preferiti");
    const int64_t later = library.createPlaylist("Da sentire");
    library.renamePlaylist(later, "Da ascoltare");
    const int64_t queue = library.queue();
    check(queue == library.queue() && queue != favourites && queue != later, "id della coda");
    std::vector<mp::PlaylistInfo> playlists = library.playlists();
    check(playlists.size() == 2 && playlists[0].name == "Preferiti" && playlists[1].name == "Da ascoltare",
          "elenco delle playlist, senza la coda");
    library.setPlaylistEntries(favourites, {
                                               {flacRef, std::nullopt, "tono.flac", std::nullopt},
                                               {flacRef, 0, "tono.flac", std::nullopt},
                                               {flacRef, std::nullopt, "doppione", std::nullopt},
                                               {"/fuori/brano.mod", 2, "brano.mod", std::nullopt},
                                           });
    std::vector<mp::PlaylistEntry> entries = library.playlistEntries(favourites);
    check(entries.size() == 3, "voci della playlist, doppione scartato: " + std::to_string(entries.size()));
    if (entries.size() == 3) {
        check(entries[0].ref == flacRef && !entries[0].subsong && entries[0].track
                  && entries[0].track->format == "FLAC",
              "prima voce: file intero, con i dati della libreria");
        check(entries[1].subsong == 0, "seconda voce: lo stesso file, un sottobrano");
        check(entries[2].subsong == 2 && entries[2].name == "brano.mod" && !entries[2].track,
              "voce di un brano fuori dalla libreria");
    }
    check(library.playlists()[0].count == 3, "conteggio delle voci");
    library.setPlaylistEntries(queue, {{flacRef, std::nullopt, "tono.flac", std::nullopt}});
    check(library.playlistEntries(queue).size() == 1 && library.playlists().size() == 2, "coda");
    library.deletePlaylist(later);
    check(library.playlists().size() == 1, "playlist cancellata");
    bool threw = false;
    try {
        library.playlistEntries(later);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "voci di una playlist cancellata");
    threw = false;
    try {
        library.deletePlaylist(queue);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "la coda non si cancella");
    check(library.setting("app.skin").empty(), "impostazione mai scritta");
    library.setSetting("app.skin", "retro");
    check(library.setting("app.skin") == "retro", "impostazione");

    // Un'altra radice svuota l'indice, ma le playlist restano.
    const fs::path other = root.parent_path() / "altra";
    fs::create_directories(other);
    library.setRoot(other);
    check(library.search({}).total == 0, "indice svuotato cambiando radice");
    entries = library.playlistEntries(favourites);
    check(entries.size() == 3 && !entries[0].track, "playlist dopo il cambio di radice");
}

void testResolve(const fs::path& root, bool withArchive)
{
    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);
    mp::TrackCollector collector(registry);
    const std::string flac = (root / "Archivio" / "Toni" / "tono.flac").string();
    check(collector.resolve(flac) == flac, "resolve di un file normale");
    if (!withArchive)
        return;
    const std::string zip = (root / "toni.zip").string();
    const fs::path extracted = collector.resolve(zip + "/dentro/zippato.wav");
    check(fs::is_regular_file(extracted) && extracted.filename() == "zippato.wav", "resolve dentro lo zip");
    check(collector.resolve(zip + "/dentro/zippato.wav") == extracted, "la seconda volta senza riestrarre");
    bool threw = false;
    try {
        collector.resolve(zip + "/dentro/manca.wav");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "resolve di un file che nello zip non c'è");
    const std::vector<mp::Track> tracks = collector.collect(zip);
    check(tracks.size() == 1 && tracks[0].ref == zip + "/dentro/zippato.wav", "ref da collect()");
}

void testCApi(const fs::path& root, const fs::path& database)
{
    mp_library* library = mp_library_open(database.string().c_str());
    check(library != nullptr, "mp_library_open");
    if (!library)
        return;
    check(mp_library_scan(library) == -1, "scansione senza radice");
    check(mp_library_set_root(library, root.string().c_str()) == 0, "mp_library_set_root");
    check(mp_library_scan(library) == 0, "mp_library_scan");
    mp_scan_status status{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mp_library_scan_status(library, &status);
    } while (status.running && std::chrono::steady_clock::now() < deadline);
    check(!status.running && !status.failed && status.files_read == status.files_found && status.tracks_found > 0,
          "scansione in background");

    const std::string json = mp_library_search(library, "prova", "audio", "WAV\nFLAC", "", 1, 0, 10);
    check(json.find("\"total\":") != std::string::npos && json.find("\"format\":\"FLAC\"") != std::string::npos
              && json.find("\"platformName\":\"File audio\"") != std::string::npos,
          "mp_library_search: " + json.substr(0, 120));
    const std::string facets = mp_library_facets(library, "", "", "", "");
    check(facets.find("{\"id\":\"audio\",\"name\":\"File audio\",\"count\":") != std::string::npos,
          "mp_library_facets: " + facets);
    const std::string folders = mp_library_folders(library, "", "", "", "Archivio");
    check(folders == "[{\"name\":\"Toni\",\"count\":1}]", "mp_library_folders: " + folders);
    const std::string direct = mp_library_search(library, "", "", "", "Archivio/Toni", 0, 0, 10);
    check(direct.find("\"total\":1,") != std::string::npos, "mp_library_search in una cartella: " + direct.substr(0, 60));
    const std::string ref = (root / "Archivio" / "Toni" / "tono.flac").string();
    const std::string track = mp_library_track(library, ref.c_str());
    check(track.find("\"durations\":[1.000]") != std::string::npos, "mp_library_track: " + track);
    check(std::string(mp_library_track(library, "/non/esiste")).empty(), "mp_library_track di un brano assente");

    const int64_t playlist = mp_library_playlist_create(library, "Prova");
    check(playlist > 0, "mp_library_playlist_create");
    const std::string lines = "\t" + ref + "\ttono.flac\n1\t" + ref + "\ttono.flac\n";
    check(mp_library_playlist_set_entries(library, playlist, lines.c_str()) == 0, "mp_library_playlist_set_entries");
    const std::string entries = mp_library_playlist_entries(library, playlist);
    check(entries.find("\"subsong\":null") != std::string::npos && entries.find("\"subsong\":1") != std::string::npos
              && entries.find("\"track\":{") != std::string::npos,
          "mp_library_playlist_entries: " + entries.substr(0, 160));
    check(std::string(mp_library_playlists(library)).find("\"name\":\"Prova\",\"count\":2") != std::string::npos,
          "mp_library_playlists");
    check(mp_library_playlist_set_entries(library, playlist, "riga senza tabulazioni") == -1,
          "voci non valide");
    check(mp_library_playlist_rename(library, playlist + 100, "x") == -1, "rinomina di una playlist che non c'è");
    check(mp_library_queue(library) > 0, "mp_library_queue");
    check(mp_library_set_setting(library, "app.volume", "0.5") == 0
              && std::string(mp_library_setting(library, "app.volume")) == "0.5",
          "impostazioni dall'API C");
    mp_library_close(library);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s DIR DATI [CMAKE]\n", argv[0]);
        return 2;
    }
    const fs::path base = fs::absolute(fs::path(argv[1]) / "library-test");
    const fs::path data = argv[2];
    fs::remove_all(base);
    fs::create_directories(base / "Musica" / "Archivio" / "Toni");
    // Come la normalizza la libreria, per confrontare i riferimenti.
    const fs::path root = fs::weakly_canonical(base / "Musica");
    fs::copy_file(data / "tono.wav", root / "tono.wav");
    fs::copy_file(data / "tono.mp3", root / "tono.mp3");
    fs::copy_file(data / "tono.ogg", root / "tono.ogg");
    fs::copy_file(data / "tono.flac", root / "Archivio" / "Toni" / "tono.flac");
    fs::copy_file(data / "tono.wav", root / ".nascosto.wav");
    writeText(root / "leggimi.txt", "non è musica");

    bool withArchive = false;
#ifdef MP_TEST_ARCHIVES
    if (argc > 3) {
        const fs::path staging = base / "zip";
        fs::create_directories(staging / "dentro");
        fs::copy_file(data / "tono.wav", staging / "dentro" / "zippato.wav");
        const std::string cmake = argv[3];
        std::string command = "\"" + cmake + "\" -E chdir \"" + staging.string() + "\" \"" + cmake
            + "\" -E tar cf \"" + (root / "toni.zip").string() + "\" --format=zip dentro";
#ifdef _WIN32
        // cmd.exe toglie la prima e l'ultima virgoletta della riga.
        command = "\"" + command + "\"";
#endif
        withArchive = std::system(command.c_str()) == 0 && fs::exists(root / "toni.zip");
        check(withArchive, "zip di prova non creato");
    }
#endif

    testLibrary(root, base / "db" / "library.db", withArchive);
    testCApi(root, base / "capi.db");
    testResolve(root, withArchive);

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
