#include "AudioOutput.h"

#include "Decoder.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace mp {

struct AudioOutput::Impl {
    ma_device device{};
    ma_pcm_rb ring{};
    bool running = false;
    std::atomic<bool> paused{false};

    // Contatori assoluti dei frame scritti e letti. Il ring buffer ha un solo
    // lettore, il thread audio, quindi è lui a scartare la coda: flush()
    // chiede di buttare tutti i frame con indice minore di discardUntil.
    std::atomic<uint64_t> written{0};
    std::atomic<uint64_t> read{0};
    std::atomic<uint64_t> discardUntil{0};

    static void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
    {
        auto* self = static_cast<Impl*>(device->pUserData);
        self->discardStale();

        // Il buffer di uscita arriva già azzerato: in pausa, o se la coda
        // non basta, il resto resta silenzio.
        if (self->paused.load(std::memory_order_relaxed))
            return;

        auto* out = static_cast<float*>(output);
        ma_uint32 done = 0;
        while (done < frameCount) {
            ma_uint32 chunk = frameCount - done;
            void* data = nullptr;
            if (ma_pcm_rb_acquire_read(&self->ring, &chunk, &data) != MA_SUCCESS || chunk == 0)
                break;
            std::memcpy(out + done * kChannels, data, chunk * kChannels * sizeof(float));
            ma_pcm_rb_commit_read(&self->ring, chunk);
            done += chunk;
        }
        self->read.fetch_add(done, std::memory_order_release);
    }

    void discardStale()
    {
        const uint64_t target = discardUntil.load(std::memory_order_acquire);
        uint64_t current = read.load(std::memory_order_relaxed);
        while (current < target) {
            auto chunk = static_cast<ma_uint32>(std::min<uint64_t>(target - current, UINT32_MAX));
            void* data = nullptr;
            if (ma_pcm_rb_acquire_read(&ring, &chunk, &data) != MA_SUCCESS || chunk == 0)
                break;
            ma_pcm_rb_commit_read(&ring, chunk);
            current += chunk;
        }
        read.store(current, std::memory_order_release);
    }
};

AudioOutput::AudioOutput(int sampleRate, double bufferSeconds)
    : impl_(std::make_unique<Impl>())
{
    const auto bufferFrames = static_cast<ma_uint32>(sampleRate * bufferSeconds);
    if (ma_pcm_rb_init(ma_format_f32, kChannels, bufferFrames, nullptr, nullptr, &impl_->ring) != MA_SUCCESS)
        throw std::runtime_error("impossibile allocare il buffer audio");

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = static_cast<ma_uint32>(sampleRate);
    config.dataCallback = Impl::dataCallback;
    config.pUserData = impl_.get();

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&impl_->ring);
        throw std::runtime_error("impossibile aprire il dispositivo audio");
    }
}

AudioOutput::~AudioOutput()
{
    ma_device_uninit(&impl_->device);
    ma_pcm_rb_uninit(&impl_->ring);
}

void AudioOutput::start()
{
    if (!impl_->running && ma_device_start(&impl_->device) == MA_SUCCESS)
        impl_->running = true;
}

void AudioOutput::stop()
{
    if (impl_->running) {
        ma_device_stop(&impl_->device);
        impl_->running = false;
    }
}

void AudioOutput::setPaused(bool paused)
{
    impl_->paused.store(paused, std::memory_order_relaxed);
}

void AudioOutput::flush()
{
    impl_->discardUntil.store(impl_->written.load(std::memory_order_relaxed), std::memory_order_release);
}

size_t AudioOutput::writableFrames() const
{
    return ma_pcm_rb_available_write(&impl_->ring);
}

size_t AudioOutput::queuedFrames() const
{
    const uint64_t written = impl_->written.load(std::memory_order_relaxed);
    const uint64_t consumed = std::max(impl_->read.load(std::memory_order_acquire),
                                       impl_->discardUntil.load(std::memory_order_relaxed));
    return written > consumed ? static_cast<size_t>(written - consumed) : 0;
}

size_t AudioOutput::write(const float* frames, size_t count)
{
    size_t done = 0;
    while (done < count) {
        auto chunk = static_cast<ma_uint32>(count - done);
        void* data = nullptr;
        if (ma_pcm_rb_acquire_write(&impl_->ring, &chunk, &data) != MA_SUCCESS || chunk == 0)
            break;
        std::memcpy(data, frames + done * kChannels, chunk * kChannels * sizeof(float));
        ma_pcm_rb_commit_write(&impl_->ring, chunk);
        done += chunk;
    }
    impl_->written.fetch_add(done, std::memory_order_release);
    return done;
}

std::string AudioOutput::deviceName() const
{
    char name[MA_MAX_DEVICE_NAME_LENGTH + 1] = {};
    size_t length = 0;
    ma_device_get_name(&impl_->device, ma_device_type_playback, name, sizeof name, &length);
    return std::string(name, length);
}

} // namespace mp
