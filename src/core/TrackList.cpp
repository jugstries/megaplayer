#include "TrackList.h"

#include "SourceFile.h"
#include "decoders/TfmxFiles.h"

#include <stdexcept>

namespace mp {

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
    const auto [path, inner] = splitArchivePath(input);
    std::optional<ExtractedArchive> archive = openArchive(path);
    if (!archive) {
        if (isSampleHalf(path)) {
            lastSkipped_ = 1;
            return {};
        }
        return {{input, input, false}};
    }

    std::vector<Track> tracks;
    bool selected = false;
    for (const ExtractedArchive::Entry& entry : archive->entries()) {
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
            tracks.push_back({entry.path, path.string() + " > " + entry.name, true});
        else
            ++lastSkipped_;
    }
    if (!selected)
        throw std::runtime_error(inner.empty() ? "archivio vuoto" : "nessun file " + inner + " nell'archivio");

    archives_.push_back(std::move(*archive));
    return tracks;
}

} // namespace mp
