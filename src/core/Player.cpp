#include "Player.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>

namespace mp {

namespace {

constexpr size_t kChunkFrames = 1024;
// Abbastanza per non interrompersi se l'interfaccia rallenta, poco per non
// sentire ritardo su pausa, seek e cambi di brano.
constexpr double kBufferSeconds = 0.3;

} // namespace

Player::Player(int sampleRate)
    : sampleRate_(sampleRate)
    , output_(sampleRate, kBufferSeconds)
    , history_(static_cast<size_t>(sampleRate) * kChannels)
{
    output_.start();
    thread_ = std::thread(&Player::run, this);
}

Player::~Player()
{
    quit_ = true;
    thread_.join();
    output_.stop();
}

void Player::play(std::unique_ptr<Decoder> decoder)
{
    std::lock_guard lock(mutex_);
    decoder_ = std::move(decoder);
    decoding_ = decoder_ != nullptr;
    paused_ = false;
    output_.flush();
    output_.setPaused(false);
}

void Player::stop()
{
    std::lock_guard lock(mutex_);
    decoder_.reset();
    decoding_ = false;
    output_.flush();
}

void Player::setPaused(bool paused)
{
    std::lock_guard lock(mutex_);
    paused_ = paused;
    output_.setPaused(paused);
}

void Player::seek(double seconds)
{
    std::lock_guard lock(mutex_);
    if (!decoder_ || !decoder_->canSeek())
        return;
    decoder_->seek(std::max(0.0, seconds));
    decoding_ = true;
    output_.flush();
}

void Player::selectSubsong(int index)
{
    std::lock_guard lock(mutex_);
    if (!decoder_ || index < 0 || index >= decoder_->info().subsongCount)
        return;
    decoder_->selectSubsong(index);
    decoding_ = true;
    output_.flush();
}

void Player::setVolume(float volume)
{
    output_.setVolume(std::clamp(volume, 0.0f, 1.0f));
}

PlayerStatus Player::status() const
{
    std::lock_guard lock(mutex_);
    PlayerStatus status;
    if (!decoder_)
        return status;

    const size_t queued = output_.queuedFrames();
    const TrackInfo& info = decoder_->info();
    status.position = std::max(0.0, decoder_->position() - static_cast<double>(queued) / sampleRate_);
    status.duration = info.duration;
    status.subsong = info.currentSubsong;
    status.subsongCount = info.subsongCount;
    if (!decoding_ && queued == 0)
        status.state = PlaybackState::Ended;
    else
        status.state = paused_ ? PlaybackState::Paused : PlaybackState::Playing;
    return status;
}

TrackInfo Player::info() const
{
    std::lock_guard lock(mutex_);
    return decoder_ ? decoder_->info() : TrackInfo{};
}

size_t Player::scope(float* out, size_t frames) const
{
    std::lock_guard lock(mutex_);
    const size_t capacity = history_.size() / kChannels;
    // L'ultimo frame arrivato alle casse è quello scritto prima della coda.
    const uint64_t heard = written_ - std::min<uint64_t>(written_, output_.queuedFrames());
    frames = static_cast<size_t>(std::min<uint64_t>({frames, capacity, heard}));
    for (size_t i = 0; i < frames; ++i) {
        const size_t slot = static_cast<size_t>((heard - frames + i) % capacity);
        out[i * kChannels] = history_[slot * kChannels];
        out[i * kChannels + 1] = history_[slot * kChannels + 1];
    }
    return frames;
}

void Player::remember(const float* frames, size_t count)
{
    const size_t capacity = history_.size() / kChannels;
    for (size_t i = 0; i < count; ++i) {
        const size_t slot = static_cast<size_t>((written_ + i) % capacity);
        history_[slot * kChannels] = frames[i * kChannels];
        history_[slot * kChannels + 1] = frames[i * kChannels + 1];
    }
    written_ += count;
}

void Player::run()
{
    std::vector<float> chunk(kChunkFrames * kChannels);
    while (!quit_) {
        bool wrote = false;
        {
            std::lock_guard lock(mutex_);
            if (decoder_ && decoding_ && output_.writableFrames() >= kChunkFrames) {
                size_t got = 0;
                try {
                    got = decoder_->render(chunk.data(), kChunkFrames);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "megaplayer: %s\n", e.what());
                }
                if (got == 0) {
                    decoding_ = false;
                } else {
                    output_.write(chunk.data(), got);
                    remember(chunk.data(), got);
                    wrote = true;
                }
            }
        }
        if (!wrote)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace mp
