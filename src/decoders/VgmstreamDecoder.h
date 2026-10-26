#pragma once

#include "core/Decoder.h"

namespace mp {

// L'audio in streaming dei giochi con vgmstream: centinaia di formati delle
// console (GENH, ADS, VAG, DSP, ADX, BRSTM, HCA...), anche quelli senza
// intestazione che riconosce dall'estensione (i .int della PS2). I brani col
// loop fanno due giri del ritornello e poi sfumano.
class VgmstreamDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "vgmstream"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
