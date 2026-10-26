#pragma once

#include "core/Decoder.h"

namespace mp {

// I log S98 dei computer giapponesi (PC-88, PC-98, X68000): le scritture ai
// registri dei chip sonori, rifatte su chip emulati. Suonano lo YM2608
// (OPNA), lo YM2203 (OPN) e i PSG (YM2149, AY-3-8910) con lo YM2608 di
// 98fmplayer, lo YM2151 (OPM) con quello di mdxmini e lo YM2612 (OPN2) col
// Nuked di game-music-emu; gli altri chip tacciono. I log col loop fanno due
// giri e sfumano.
class S98DecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "s98"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
