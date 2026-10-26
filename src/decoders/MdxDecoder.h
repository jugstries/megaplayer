#pragma once

#include "core/Decoder.h"

#include <mutex>

namespace mp {

// La musica dello Sharp X68000 (MDX, per il driver MXDRV), con mdxmini: otto
// voci FM dello YM2151 e i campioni ADPCM del PDX che accompagna il brano,
// anche nella variante PCM8 a otto voci. Le canzoni col loop fanno due giri e
// poi sfumano.
class MdxDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "mdx"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

namespace mdx {

// L'YM2151 di mdxmini prepara tabelle globali a ogni chip nuovo, e lo usa
// anche il lettore S98: un chip per volta.
std::mutex& chipMutex();

} // namespace mdx

} // namespace mp
