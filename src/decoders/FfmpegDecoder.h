#pragma once

#include "core/Decoder.h"

namespace mp {

// I file audio che miniaudio non apre, con FFmpeg (ridotto ai decoder audio,
// LGPL): M4A e MP4 (AAC, ALAC), Ogg Opus, AIFF e AIFF-C, Matroska e WebM,
// WMA, Monkey's Audio, WavPack, TTA, Musepack, TAK, CAF e AU.
class FfmpegDecoderFactory final : public DecoderFactory {
public:
    // Zittisce i messaggi di FFmpeg sulla console, anche quando lo usa
    // vgmstream.
    FfmpegDecoderFactory();

    std::string_view name() const override { return "ffmpeg"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
