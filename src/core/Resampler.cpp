#include "Resampler.h"

#include "Decoder.h"

#include <miniaudio.h>

#include <stdexcept>
#include <vector>

namespace mp {

namespace {

// Quanti frame chiedere alla sorgente per volta.
constexpr size_t kChunkFrames = 1024;

} // namespace

struct Resampler::State {
    bool passthrough = false;
    ma_resampler resampler{};
    std::vector<float> input = std::vector<float>(kChunkFrames * kChannels);
    size_t inputStart = 0; // frame già consumati di input
    size_t inputEnd = 0;   // frame validi in input
};

Resampler::Resampler(int inputRate, int outputRate)
    : state_(std::make_unique<State>())
{
    state_->passthrough = inputRate == outputRate;
    if (state_->passthrough)
        return;
    const ma_resampler_config config =
        ma_resampler_config_init(ma_format_f32, kChannels, static_cast<ma_uint32>(inputRate),
                                 static_cast<ma_uint32>(outputRate), ma_resample_algorithm_linear);
    if (ma_resampler_init(&config, nullptr, &state_->resampler) != MA_SUCCESS)
        throw std::runtime_error("resampler non disponibile");
}

Resampler::~Resampler()
{
    if (!state_->passthrough)
        ma_resampler_uninit(&state_->resampler, nullptr);
}

size_t Resampler::render(float* out, size_t frames, const Source& source)
{
    if (state_->passthrough)
        return source(out, frames);

    State& s = *state_;
    size_t written = 0;
    while (written < frames) {
        if (s.inputStart == s.inputEnd) {
            s.inputStart = 0;
            s.inputEnd = source(s.input.data(), kChunkFrames);
            if (s.inputEnd == 0)
                break;
        }
        ma_uint64 in = s.inputEnd - s.inputStart;
        ma_uint64 produced = frames - written;
        ma_resampler_process_pcm_frames(&s.resampler, s.input.data() + s.inputStart * kChannels, &in,
                                        out + written * kChannels, &produced);
        s.inputStart += static_cast<size_t>(in);
        written += static_cast<size_t>(produced);
    }
    return written;
}

void Resampler::reset()
{
    state_->inputStart = state_->inputEnd = 0;
    if (!state_->passthrough)
        ma_resampler_reset(&state_->resampler);
}

} // namespace mp
