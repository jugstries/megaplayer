#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica della PlayStation (PSF e MiniPSF con le loro librerie) con Highly
// Experimental, che emula la CPU R3000 e lo SPU. Al posto della BIOS di
// Sony, che non si può distribuire, gira OpenBIOS di PCSX-Redux.
class PsfDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "psf"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
