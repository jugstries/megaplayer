#pragma once

#include <cstdint>

namespace mp {

// Highly Experimental ha qualche tabella globale e un'immagine della BIOS
// globale: si preparano una volta sola, per il PSF e per il PSF2. La BIOS
// globale è OpenBIOS, per la PlayStation; il PSF2 dà a ogni stato la sua ROM
// con psx_clear_state_bios. Lancia DecoderError.
void initHighlyExperimental();

// Dove si ferma l'avvio della ROM del PSF2: il suo "b ." (Psf2Decoder.cpp).
constexpr uint32_t kPs2BootStop = 0x10;

} // namespace mp
