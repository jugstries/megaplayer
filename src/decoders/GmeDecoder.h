#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica delle console a 8 e 16 bit (NES, SNES, Game Boy, Mega Drive/Master
// System, PC Engine, MSX, ZX Spectrum, Atari) con game-music-emu.
class GmeDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "gme"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
