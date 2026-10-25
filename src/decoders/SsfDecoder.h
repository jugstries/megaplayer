#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica del Saturn (SSF) e del Dreamcast (DSF), con le loro librerie, con
// Highly Theoretical: il 68000 e lo SCSP del Saturn, l'ARM7 e l'AICA del
// Dreamcast. Nessuna BIOS da cercare: il programma del brano basta.
class SsfDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "ssf"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
