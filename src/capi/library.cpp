#include "megaplayer.h"

#include "Json.h"
#include "core/Platform.h"
#include "library/Library.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct mp_library {
    explicit mp_library(const std::filesystem::path& path)
        : library(path)
    {
    }

    ~mp_library()
    {
        cancel = true;
        if (worker.joinable())
            worker.join();
    }

    mp::Library library;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};
    mp::ScanProgress progress;

    std::mutex resultMutex; // per result e scanError, scritti dal thread
    mp::ScanResult result;
    std::string scanError;

    std::string error;
    std::string databaseText;
    std::string rootText;
    std::string searchJson;
    std::string facetsJson;
    std::string trackJson;
};

namespace {

std::vector<std::string> lines(const char* text)
{
    std::vector<std::string> values;
    std::string current;
    for (const char* c = text ? text : ""; *c; ++c) {
        if (*c == '\n') {
            if (!current.empty())
                values.push_back(current);
            current.clear();
        } else {
            current += *c;
        }
    }
    if (!current.empty())
        values.push_back(current);
    return values;
}

mp::LibraryQuery makeQuery(const char* text, const char* platforms, const char* formats)
{
    mp::LibraryQuery query;
    query.text = text ? text : "";
    query.platforms = lines(platforms);
    query.formats = lines(formats);
    return query;
}

// I campi del brano, senza le graffe.
std::string trackFields(const mp::LibraryTrack& track)
{
    using mp::json::string;
    return "\"ref\":" + string(track.ref) + ",\"file\":" + string(track.file) + ",\"entry\":" + string(track.entry)
        + ",\"name\":" + string(track.name) + ",\"folder\":" + string(track.folder)
        + ",\"backend\":" + string(track.backend) + ",\"format\":" + string(track.format)
        + ",\"player\":" + string(track.player) + ",\"platform\":" + string(track.platform)
        + ",\"platformName\":" + string(std::string(mp::platformName(track.platform)))
        + ",\"title\":" + string(track.title) + ",\"author\":" + string(track.author)
        + ",\"subsongs\":" + std::to_string(track.subsongCount)
        + ",\"startSubsong\":" + std::to_string(track.startSubsong)
        + ",\"duration\":" + mp::json::number(track.duration);
}

} // namespace

extern "C" {

mp_library* mp_library_open(const char* path)
{
    try {
        const std::filesystem::path database =
            path && *path ? std::filesystem::path(path) : mp::Library::defaultPath();
        return new mp_library(database);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "megaplayer: %s\n", e.what());
        return nullptr;
    }
}

void mp_library_close(mp_library* library)
{
    delete library;
}

const char* mp_library_last_error(mp_library* library)
{
    return library->error.c_str();
}

const char* mp_library_database(mp_library* library)
{
    library->databaseText = library->library.database().string();
    return library->databaseText.c_str();
}

const char* mp_library_root(mp_library* library)
{
    try {
        library->rootText = library->library.root().string();
    } catch (const std::exception& e) {
        library->error = e.what();
        library->rootText.clear();
    }
    return library->rootText.c_str();
}

int32_t mp_library_set_root(mp_library* library, const char* path)
{
    if (library->running) {
        library->error = "scansione in corso";
        return -1;
    }
    try {
        library->library.setRoot(path ? path : "");
        return 0;
    } catch (const std::exception& e) {
        library->error = e.what();
        return -1;
    }
}

int32_t mp_library_scan(mp_library* library)
{
    if (library->running) {
        library->error = "scansione già in corso";
        return -1;
    }
    if (library->library.root().empty()) {
        library->error = "cartella della libreria non scelta";
        return -1;
    }
    if (library->worker.joinable())
        library->worker.join();

    library->progress.walking = true;
    library->progress.filesFound = 0;
    library->progress.filesChecked = 0;
    library->progress.filesRead = 0;
    library->progress.tracksFound = 0;
    {
        const std::lock_guard lock(library->resultMutex);
        library->result = {};
        library->scanError.clear();
    }
    library->cancel = false;
    library->running = true;
    library->worker = std::thread([library] {
        mp::ScanResult result;
        std::string error;
        try {
            result = library->library.scan(library->progress, library->cancel);
        } catch (const std::exception& e) {
            error = e.what();
        }
        {
            const std::lock_guard lock(library->resultMutex);
            library->result = result;
            library->scanError = error;
        }
        library->running = false;
    });
    return 0;
}

void mp_library_cancel_scan(mp_library* library)
{
    library->cancel = true;
}

void mp_library_scan_status(mp_library* library, mp_scan_status* out)
{
    out->running = library->running ? 1 : 0;
    out->walking = library->running && library->progress.walking ? 1 : 0;
    out->files_found = library->progress.filesFound;
    out->files_checked = library->progress.filesChecked;
    out->files_read = library->progress.filesRead;
    out->tracks_found = library->progress.tracksFound;
    const std::lock_guard lock(library->resultMutex);
    out->files_removed = library->result.filesRemoved;
    out->failed = library->scanError.empty() ? 0 : 1;
    if (!library->scanError.empty())
        library->error = library->scanError;
}

const char* mp_library_search(mp_library* library, const char* text, const char* platforms, const char* formats,
                              int32_t offset, int32_t limit)
{
    try {
        mp::LibraryQuery query = makeQuery(text, platforms, formats);
        query.offset = offset;
        query.limit = limit;
        const mp::LibraryPage page = library->library.search(query);
        std::string json = "{\"total\":" + std::to_string(page.total) + ",\"tracks\":[";
        for (size_t i = 0; i < page.tracks.size(); ++i)
            json += (i == 0 ? "{" : ",{") + trackFields(page.tracks[i]) + "}";
        library->searchJson = json + "]}";
    } catch (const std::exception& e) {
        library->error = e.what();
        library->searchJson = "{\"total\":0,\"tracks\":[]}";
    }
    return library->searchJson.c_str();
}

const char* mp_library_facets(mp_library* library, const char* text, const char* platforms, const char* formats)
{
    try {
        const mp::LibraryFacets facets = library->library.facets(makeQuery(text, platforms, formats));
        std::string json = "{\"platforms\":[";
        for (size_t i = 0; i < facets.platforms.size(); ++i) {
            const mp::LibraryFacet& facet = facets.platforms[i];
            json += (i == 0 ? "{" : ",{") + std::string("\"id\":") + mp::json::string(facet.value)
                + ",\"name\":" + mp::json::string(std::string(mp::platformName(facet.value)))
                + ",\"count\":" + std::to_string(facet.count) + "}";
        }
        json += "],\"formats\":[";
        for (size_t i = 0; i < facets.formats.size(); ++i) {
            const mp::LibraryFacet& facet = facets.formats[i];
            json += (i == 0 ? "{" : ",{") + std::string("\"name\":") + mp::json::string(facet.value)
                + ",\"count\":" + std::to_string(facet.count) + "}";
        }
        library->facetsJson = json + "]}";
    } catch (const std::exception& e) {
        library->error = e.what();
        library->facetsJson = "{\"platforms\":[],\"formats\":[]}";
    }
    return library->facetsJson.c_str();
}

const char* mp_library_track(mp_library* library, const char* ref)
{
    try {
        const std::optional<mp::LibraryTrack> track = library->library.track(ref ? ref : "");
        if (!track) {
            library->trackJson.clear();
            return library->trackJson.c_str();
        }
        std::string durations = "[";
        for (const std::optional<double>& duration : library->library.subsongDurations(track->ref))
            durations += (durations.size() > 1 ? "," : "") + mp::json::number(duration);
        library->trackJson = "{" + trackFields(*track) + ",\"durations\":" + durations + "]}";
    } catch (const std::exception& e) {
        library->error = e.what();
        library->trackJson.clear();
    }
    return library->trackJson.c_str();
}

} // extern "C"
