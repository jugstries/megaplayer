#pragma once

#include "AudioOutput.h"
#include "Decoder.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace mp {

enum class PlaybackState { Idle, Playing, Paused, Ended };

struct PlayerStatus {
    PlaybackState state = PlaybackState::Idle;
    double position = 0; // secondi, di ciò che si sta ascoltando
    std::optional<double> duration;
    int subsong = 0; // base 0
    int subsongCount = 0;
};

// Riproduzione per le interfacce grafiche: un thread decodifica il brano
// corrente nell'uscita audio, mentre l'interfaccia dà i comandi e legge lo
// stato. I metodi pubblici vanno chiamati da un solo thread.
class Player {
public:
    explicit Player(int sampleRate);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    int sampleRate() const { return sampleRate_; }

    // Sostituisce il brano corrente e lo avvia dall'inizio. Il decoder
    // precedente va chiuso prima di aprire quello nuovo (stop()): UADE
    // suona un solo brano per volta.
    void play(std::unique_ptr<Decoder> decoder);
    void stop();

    void setPaused(bool paused);
    void seek(double seconds);
    void selectSubsong(int index);
    void setVolume(float volume); // 0..1

    PlayerStatus status() const;
    TrackInfo info() const;

    // Copia in `out` gli ultimi `frames` frame stereo arrivati alle casse,
    // per gli oscilloscopi, e restituisce quanti ne ha copiati.
    size_t scope(float* out, size_t frames) const;

private:
    void run();
    void remember(const float* frames, size_t count);

    const int sampleRate_;
    AudioOutput output_;

    // Protegge il decoder e lo stato che segue, condivisi con il thread di
    // decodifica.
    mutable std::mutex mutex_;
    std::unique_ptr<Decoder> decoder_;
    bool decoding_ = false;
    bool paused_ = false;
    // L'ultimo secondo di audio scritto nell'uscita, per frame assoluto.
    std::vector<float> history_;
    uint64_t written_ = 0;

    std::atomic<bool> quit_{false};
    std::thread thread_;
};

} // namespace mp
