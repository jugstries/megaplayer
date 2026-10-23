#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

namespace mp {

// Scrive frame stereo float in un file WAV PCM a 16 bit.
class WavWriter {
public:
    WavWriter(const std::filesystem::path& path, int sampleRate);
    ~WavWriter();

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    void write(const float* frames, size_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mp
