#pragma once

#include "core/Decoder.h"

namespace mp {

// File audio comuni (MP3, FLAC, WAV, Ogg Vorbis) con i decoder di miniaudio.
class MiniaudioDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "miniaudio"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
