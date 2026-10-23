#include "DecoderRegistry.h"

#include <algorithm>

namespace mp {

void DecoderRegistry::add(std::unique_ptr<DecoderFactory> factory)
{
    factories_.push_back(std::move(factory));
}

DecoderFactory* DecoderRegistry::find(std::string_view name) const
{
    for (const auto& factory : factories_) {
        if (factory->name() == name)
            return factory.get();
    }
    return nullptr;
}

std::vector<ProbeResult> DecoderRegistry::probe(const SourceFile& file) const
{
    std::vector<ProbeResult> results;
    for (const auto& factory : factories_) {
        const int score = factory->probe(file);
        if (score > 0)
            results.push_back({factory.get(), score});
    }
    std::stable_sort(results.begin(), results.end(),
                     [](const ProbeResult& a, const ProbeResult& b) { return a.score > b.score; });
    return results;
}

std::unique_ptr<Decoder> DecoderRegistry::open(const SourceFile& file, const OutputSpec& spec,
                                               std::string_view forced) const
{
    if (!forced.empty()) {
        DecoderFactory* factory = find(forced);
        if (!factory)
            throw DecoderError("backend sconosciuto: " + std::string(forced));
        return factory->open(file, spec);
    }

    const std::vector<ProbeResult> candidates = probe(file);
    if (candidates.empty())
        throw DecoderError("formato non riconosciuto");

    std::string errors;
    for (const ProbeResult& candidate : candidates) {
        try {
            return candidate.factory->open(file, spec);
        } catch (const DecoderError& e) {
            errors += "\n  " + std::string(candidate.factory->name()) + ": " + e.what();
        }
    }
    throw DecoderError("nessun backend è riuscito ad aprire il file:" + errors);
}

} // namespace mp
