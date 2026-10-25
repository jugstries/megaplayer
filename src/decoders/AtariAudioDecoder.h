#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica dell'Atari ST con AtariAudio: i file SNDH (il replayer originale
// gira su un 68000 emulato, con YM2149, timer, DAC dell'STE e blitter) e i
// file YM (i registri del YM2149 frame per frame), anche compressi con ICE!
// o LHA come si trovano in giro.
class AtariAudioDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "atariaudio"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
