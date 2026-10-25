#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace mp {

// Porta l'audio stereo float di un emulatore dalla sua frequenza fissa (i
// 44100 Hz dello SPU della PlayStation, i 37800 dell'XA) a quella d'uscita,
// con il resampler di miniaudio. Alla stessa frequenza passa i campioni così
// come sono.
class Resampler {
public:
    // Scrive in out fino a frames frame alla frequenza d'ingresso; 0 alla fine.
    using Source = std::function<size_t(float* out, size_t frames)>;

    Resampler(int inputRate, int outputRate);
    ~Resampler();

    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    // Scrive in out fino a frames frame alla frequenza d'uscita, chiedendo a
    // source quelli che servono; meno solo quando source è finita.
    size_t render(float* out, size_t frames, const Source& source);

    // Dimentica i campioni in sospeso, per ripartire da un altro punto.
    void reset();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace mp
