#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica della PlayStation 2 (PSF2 e MiniPSF2 con le loro librerie) con
// Highly Experimental, che emula l'IOP e lo SPU2. Senza BIOS: l'IOP parte da
// una ROM con i moduli di sistema liberi di ps2sdk e hefile.irx (src/ps2),
// e il lavoro della IOPBOOT di Sony lo fa il decoder.
class Psf2DecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "psf2"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
