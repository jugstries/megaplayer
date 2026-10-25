#pragma once

#include "core/Decoder.h"

namespace mp {

// Musica delle schede arcade Capcom con il QSound (CPS-1 e CPS-2), file QSF
// con le loro librerie, con Highly Quixotic: lo Z80 del suono, con la
// cifratura Kabuki, e il chip QSound.
class QsfDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "qsf"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
