#pragma once

#include "core/Decoder.h"

namespace mp {

// L'audio in streaming dei CD della PlayStation (e del CD-i e del Saturn):
// settori XA ADPCM, in un file RIFF/CDXA come li salva Windows o grezzi da
// 2352 o 2336 byte. Ogni canale del flusso è un sottobrano.
class XaDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "xa"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
