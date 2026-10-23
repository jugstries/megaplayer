#pragma once

#include "Decoder.h"

#include <memory>
#include <string_view>
#include <vector>

namespace mp {

struct ProbeResult {
    DecoderFactory* factory;
    int score;
};

class DecoderRegistry {
public:
    void add(std::unique_ptr<DecoderFactory> factory);

    const std::vector<std::unique_ptr<DecoderFactory>>& factories() const { return factories_; }
    DecoderFactory* find(std::string_view name) const;

    // Backend che riconoscono il file, dal più adatto al meno adatto.
    std::vector<ProbeResult> probe(const SourceFile& file) const;

    // Apre il file con il primo backend che ci riesce, in ordine di
    // punteggio. Se `forced` non è vuoto usa solo quel backend.
    // Lancia DecoderError se nessuno riesce.
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec,
                                  std::string_view forced = {}) const;

private:
    std::vector<std::unique_ptr<DecoderFactory>> factories_;
};

// Registra i backend inclusi nella build (vedi le opzioni MP_WITH_* in CMake).
void registerBuiltinDecoders(DecoderRegistry& registry);

} // namespace mp
