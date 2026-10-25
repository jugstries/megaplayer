#include "WavWriter.h"

#include "Decoder.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mp {

struct WavWriter::Impl {
    ma_encoder encoder{};
    std::vector<int16_t> buffer;
    bool finished = false;
};

WavWriter::WavWriter(const std::filesystem::path& path, int sampleRate)
    : impl_(std::make_unique<Impl>())
{
    const ma_encoder_config config =
        ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, kChannels, static_cast<ma_uint32>(sampleRate));
#ifdef _WIN32
    // In UTF-16, come per gli archivi: vedi openArchive().
    const ma_result created = ma_encoder_init_file_w(path.c_str(), &config, &impl_->encoder);
#else
    const ma_result created = ma_encoder_init_file(path.c_str(), &config, &impl_->encoder);
#endif
    if (created != MA_SUCCESS)
        throw std::runtime_error("impossibile creare " + path.string());
}

WavWriter::~WavWriter()
{
    finish();
}

void WavWriter::finish()
{
    if (impl_->finished)
        return;
    ma_encoder_uninit(&impl_->encoder);
    impl_->finished = true;
}

void WavWriter::write(const float* frames, size_t count)
{
    // L'encoder non converte: i campioni vanno passati già a 16 bit.
    impl_->buffer.resize(count * kChannels);
    for (size_t i = 0; i < count * kChannels; ++i) {
        const float sample = std::clamp(frames[i], -1.0f, 1.0f);
        impl_->buffer[i] = static_cast<int16_t>(std::lrint(sample * 32767.0f));
    }
    if (ma_encoder_write_pcm_frames(&impl_->encoder, impl_->buffer.data(), count, nullptr) != MA_SUCCESS)
        throw std::runtime_error("errore di scrittura del file WAV");
}

} // namespace mp
