#include "TrackList.h"

#include "SourceFile.h"
#include "decoders/TfmxFiles.h"

#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace mp {

namespace {

// Quante cartelle estratte da resolve() tenere su disco: si riascoltano
// spesso i brani vicini, ma un'immagine ISO intera non serve.
constexpr size_t kMaxResolved = 8;

// I file di una cartella e delle sue sottocartelle, in ordine, senza quelli
// nascosti, come li vede la libreria.
std::vector<std::filesystem::path> folderFiles(const std::filesystem::path& folder)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') {
            if (it->is_directory(error))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error))
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

std::pair<std::filesystem::path, std::string> splitArchivePath(const std::string& input)
{
    std::filesystem::path candidate = input;
    while (!std::filesystem::exists(candidate) && candidate.has_relative_path())
        candidate = candidate.parent_path();
    if (candidate.empty() || !std::filesystem::is_regular_file(candidate) || candidate == input)
        return {input, {}};
    return {candidate, input.substr(candidate.string().size() + 1)};
}

std::vector<Track> TrackCollector::collect(const std::string& input)
{
    lastSkipped_ = 0;
    std::vector<Track> tracks;
    if (std::error_code error; std::filesystem::is_directory(input, error)) {
        for (const std::filesystem::path& file : folderFiles(input)) {
            try {
                if (std::optional<ExtractedArchive> archive = openArchive(file)) {
                    addArchiveEntries(file, {}, *archive, tracks);
                    collected_.push_back({file, std::move(*archive)});
                } else if (!isSampleHalf(file) && !registry_.probe(loadSourceFile(file)).empty()) {
                    tracks.push_back({file, file.string(), false, file.string()});
                } else {
                    ++lastSkipped_;
                }
            } catch (const std::exception&) {
                ++lastSkipped_; // un archivio rovinato non ferma il resto della cartella
            }
        }
        return tracks;
    }

    const auto [path, inner] = splitArchivePath(input);
    std::optional<ExtractedArchive> archive = openArchive(path);
    if (!archive) {
        if (isSampleHalf(path)) {
            lastSkipped_ = 1;
            return {};
        }
        return {{input, input, false, input}};
    }
    if (!addArchiveEntries(path, inner, *archive, tracks))
        throw std::runtime_error(inner.empty() ? "archivio vuoto" : "nessun file " + inner + " nell'archivio");
    collected_.push_back({path, std::move(*archive)});
    return tracks;
}

bool TrackCollector::addArchiveEntries(const std::filesystem::path& path, const std::string& inner,
                                       const ExtractedArchive& archive, std::vector<Track>& tracks)
{
    bool selected = false;
    for (const ExtractedArchive::Entry& entry : archive.entries()) {
        if (!inner.empty() && entry.name != inner && entry.name.rfind(inner + "/", 0) != 0)
            continue;
        selected = true;

        // Un file illeggibile qui darà errore più avanti, alla riproduzione.
        bool playable = true;
        try {
            playable = !registry_.probe(loadSourceFile(entry.path)).empty();
        } catch (const std::exception&) {
        }
        if (playable)
            tracks.push_back({entry.path, path.string() + " > " + entry.name, true, path.string() + "/" + entry.name});
        else
            ++lastSkipped_;
    }
    return selected;
}

const ExtractedArchive::Entry* TrackCollector::findExtracted(const std::filesystem::path& archive,
                                                             const std::string& name) const
{
    for (const std::vector<Extracted>* list : {&collected_, &resolved_}) {
        for (const Extracted& extracted : *list) {
            if (extracted.archive != archive)
                continue;
            for (const ExtractedArchive::Entry& entry : extracted.files.entries()) {
                if (entry.name == name)
                    return &entry;
            }
        }
    }
    return nullptr;
}

std::filesystem::path TrackCollector::resolve(const std::string& ref)
{
    const auto [path, inner] = splitArchivePath(ref);
    if (inner.empty())
        return path;
    if (const ExtractedArchive::Entry* entry = findExtracted(path, inner))
        return entry->path;

    const std::string folder = std::filesystem::path(inner).parent_path().generic_string();
    std::optional<ExtractedArchive> archive = openArchive(path, folder);
    if (!archive)
        throw std::runtime_error(path.string() + " non è un archivio");
    const auto entry = std::find_if(archive->entries().begin(), archive->entries().end(),
                                    [&](const ExtractedArchive::Entry& e) { return e.name == inner; });
    if (entry == archive->entries().end())
        throw std::runtime_error("nessun file " + inner + " nell'archivio");
    const std::filesystem::path file = entry->path;
    resolved_.push_back({path, std::move(*archive)});
    if (resolved_.size() > kMaxResolved)
        resolved_.erase(resolved_.begin());
    return file;
}

} // namespace mp
