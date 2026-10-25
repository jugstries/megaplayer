#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica del Nintendo 64 (USF, con le sue librerie) con lazyusf2:
// Mupen64Plus ridotto al suono, con l'RSP che esegue il microcodice audio del
// gioco.
class UsfDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "usf"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
