#pragma once

#include "AudioFileWriter.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace mp {

// Scrive frame stereo float in un file WAV PCM a 16 bit.
class WavWriter final : public AudioFileWriter {
public:
    WavWriter(const std::filesystem::path& path, int sampleRate);
    ~WavWriter() override;

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    void write(const float* frames, size_t count) override;
    // Scrive le dimensioni nell'intestazione; lo fa anche il distruttore.
    void finish() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mp
