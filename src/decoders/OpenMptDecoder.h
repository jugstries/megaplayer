#pragma once

#include "core/Decoder.h"

namespace mp {

// Moduli tracker (MOD, XM, S3M, IT, MED, OKT...) tramite libopenmpt.
class OpenMptDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "openmpt"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
