#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace mp {

// Uscita sul dispositivo audio predefinito, tramite miniaudio. Il chiamante
// scrive frame stereo float in un ring buffer; il thread audio li consuma e,
// se il buffer si svuota, suona silenzio.
//
// Pausa e flush non fermano il dispositivo: con alcuni driver avviarlo e
// fermarlo costa centinaia di millisecondi.
class AudioOutput {
public:
    explicit AudioOutput(int sampleRate, double bufferSeconds = 0.5);
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void start();
    void stop();

    // In pausa il dispositivo suona silenzio e la coda resta intatta.
    void setPaused(bool paused);

    // Applicato all'uscita, quindi subito udibile anche con la coda piena.
    void setVolume(float volume);

    // Scarta l'audio in coda, per seek e cambi di brano. Si può continuare a
    // scrivere subito dopo.
    void flush();

    size_t writableFrames() const;
    size_t queuedFrames() const;
    size_t write(const float* frames, size_t count);

    std::string deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mp
