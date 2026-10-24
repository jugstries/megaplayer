#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica del Commodore 64 (file PSID e RSID) con libsidplayfp ed emulazione
// del SID reSIDfp.
class SidDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "sid"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
