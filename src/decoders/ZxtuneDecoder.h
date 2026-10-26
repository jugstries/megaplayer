#pragma once

#include "core/Decoder.h"

namespace mp {

// I tracker dei chip AY dello ZX Spectrum e dell'Amstrad CPC con ZXTune: Pro
// Tracker 1-3 e Vortex, Sound Tracker (STC, ST1, ST3, STP), ASC Sound
// Master, SQ-Tracker, Global e Fast Tracker, Pro Sound Creator e Maker, i
// moduli TurboSound a due chip, e i dump PSG e VTX. I file AY e YM restano a
// game-music-emu e AtariAudio. I moduli fanno due giri e sfumano.
class ZxtuneDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "zxtune"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
