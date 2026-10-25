#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica del Game Boy Advance (GSF, con le sue librerie) con viogsf: il GBA di
// VBA-M, con le chiamate della BIOS emulate e l'APU di Blargg.
class GsfDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "gsf"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
