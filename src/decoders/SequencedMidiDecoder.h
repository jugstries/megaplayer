#pragma once

#include "core/Decoder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mp {

// Un MIDI, o un suo parente dei giochi DOS, letto dal sequencer di libADLMIDI
// e suonato da un sintetizzatore esterno: un modulo Roland emulato, un
// SoundFont. Qui stanno i tempi degli eventi, i salti, la coda dopo l'ultimo
// evento, le porte MIDI e i canali da zittire; le sottoclassi danno il
// sintetizzatore. Si avvia al primo audio, così chi legge solo le
// informazioni del file (la libreria) non lo avvia mai.
class SequencedMidiDecoder : public Decoder {
public:
    // [info] porta backend, formato e replayer. Lancia DecoderError se il
    // sequencer non legge il file.
    SequencedMidiDecoder(const SourceFile& file, const OutputSpec& spec, TrackInfo info);
    ~SequencedMidiDecoder() override;

    SequencedMidiDecoder(const SequencedMidiDecoder&) = delete;
    SequencedMidiDecoder& operator=(const SequencedMidiDecoder&) = delete;

    const TrackInfo& info() const override { return info_; }
    void selectSubsong(int index) override;
    size_t render(float* out, size_t frames) override;
    bool canSeek() const override { return true; }
    void seek(double seconds) override;
    double position() const override;
    std::vector<std::string> channels() const override;
    void setChannelMuted(int channel, bool muted) override;

protected:
    int sampleRate() const { return sampleRate_; }

    // Il sintetizzatore pronto a suonare, la prima volta che serve.
    virtual void start() = 0;
    // Il sintetizzatore come appena acceso, per un altro sottobrano.
    virtual void reset() = 0;
    // Tutte le note e i suoni spenti, prima di un salto.
    virtual void silence() = 0;
    // Un messaggio di canale: stato (con il canale della porta) | primo dato
    // << 8 | secondo dato << 16.
    virtual void shortMessage(unsigned port, uint32_t message) = 0;
    // Un SysEx completo, da F0 a F7.
    virtual void systemExclusive(unsigned port, const uint8_t* data, size_t size) = 0;
    // Il prossimo audio, stereo interlacciato.
    virtual void synthesize(float* out, size_t frames) = 0;
    // Le porte MIDI, da sedici canali ciascuna.
    virtual unsigned ports() const { return 1; }
    // Il tempo per digerire [bytes] di messaggi appena mandati, dopo un
    // salto: un modulo emulato, come uno vero, li riceve alla velocità del
    // cavo MIDI (più [settle] secondi, dopo un reset).
    virtual void pace(size_t /*bytes*/, double /*settle*/) {}

private:
    struct Sequence;
    struct SeekState;

    void load(int song);
    void ensureStarted();
    void send(uint8_t channel, uint32_t message);
    void record(uint8_t channel, uint32_t message);
    void recordSystemExclusive(const std::vector<uint8_t>& sysex);
    void replaySeekState();

    int sampleRate_;
    std::vector<uint8_t> data_;
    TrackInfo info_;
    std::unique_ptr<Sequence> sequence_;
    std::unique_ptr<SeekState> seekState_; // solo durante un salto
    bool started_ = false;
    bool muted_[16] = {};

    double untilEvent_ = 0; // secondi al prossimo evento
    double sinceTick_ = 0;  // secondi suonati dall'ultimo passo del sequencer
    bool ended_ = false;
    size_t tailLeft_ = 0; // frame di coda dopo l'ultimo evento
    size_t rendered_ = 0;
};

} // namespace mp
