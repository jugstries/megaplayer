#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica del Nintendo DS (2SF, con le sue librerie) con vio2sf: il DS di
// DeSmuME ridotto al suono, con le chiamate della BIOS emulate.
class TwosfDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "2sf"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
