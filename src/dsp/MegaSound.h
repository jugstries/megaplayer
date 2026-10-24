#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace mp {

// La manopola MegaSound: una catena di effetti che si apre man mano che la
// manopola sale da 0 a 1, come un plugin a manopola singola. In ordine:
// passa-alto, compressore, poi (sovracampionati 2×) exciter sugli alti,
// saturazione con corpo e clipper; infine riverbero solo da medie in su e
// guadagno automatico. In parallelo, fin dal primo tratto, lo Spazio: un
// ramo che allarga la scena. Tutto finisce in un limiter. Quali effetti
// entrano e quanto, a ogni posizione, sta all'inizio di MegaSound.cpp.
class MegaSound {
public:
    explicit MegaSound(int sampleRate);

    // 0 = nessun effetto, 1 = tutto. La posizione viene raggiunta in modo
    // graduale, per non sentire scatti mentre si gira.
    void setAmount(float amount);
    float amount() const { return target_; }

    // Vero da quando la manopola è stata alzata dopo l'ultimo reset.
    // Finché non lo è, chi non ha bisogno di un ritardo costante (l'export in
    // WAV) può saltare la catena: l'audio resta identico al bit. Chi suona dal
    // vivo la chiama sempre: spenta costa poco e ritarda soltanto, così
    // accenderla non fa saltare l'audio di colpo.
    bool engaged() const { return engaged_; }

    // Svuota code e stati (riverbero, compressore...): nuovo brano o seek.
    void reset();

    // Elabora in place frame stereo interleaved.
    void process(float* frames, size_t count);

    // Ritardo della catena, in frame: sovracampionamento e lookahead del
    // limiter.
    size_t latency() const { return latency_; }

private:
    // Allpass di Schroeder: non cambia il timbro, sposta la fase in modo
    // diverso a ogni frequenza. In catena decorrela il centro per lo Spazio.
    struct Diffuser {
        std::vector<float> buffer;
        size_t pos = 0;
        float process(float x);
        void reset();
    };

    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        void highpass(double sampleRate, double frequency);
        void lowpass(double sampleRate, double frequency);
        float process(float x);
        void reset() { z1 = z2 = 0; }
    };

    // Sovracampionamento 2× con un FIR a fase lineare: evita l'aliasing degli
    // effetti non lineari, forte con le onde quadre dei chiptune.
    class Oversampler {
    public:
        static constexpr int kTaps = 63;
        Oversampler();
        void up(float x, float& first, float& second);
        float down(float first, float second);
        void reset();

    private:
        std::array<float, kTaps> taps_{};
        std::array<float, kTaps> upHistory_{};
        std::array<float, kTaps> downHistory_{};
        int upPos_ = 0;
        int downPos_ = 0;
    };

    // Riverbero alla Freeverb (Jezar, pubblico dominio): otto comb e quattro
    // allpass per canale.
    class Reverb {
    public:
        void init(int sampleRate);
        void reset();
        void setRoom(float roomSize);
        void process(float input, float& left, float& right);

    private:
        struct Comb {
            std::vector<float> buffer;
            size_t pos = 0;
            float store = 0;
        };
        struct Allpass {
            std::vector<float> buffer;
            size_t pos = 0;
        };
        float comb(Comb& c, float input);
        static float allpass(Allpass& a, float input);

        std::array<Comb, 8> combsLeft_, combsRight_;
        std::array<Allpass, 4> allpassLeft_, allpassRight_;
        std::vector<float> preDelay_;
        size_t preDelayPos_ = 0;
        float feedback_ = 0.84f;
    };

    // Parametri derivati dalla posizione della manopola.
    struct Settings {
        float mix = 0;
        float bonus = 1;
        float thresholdDb = 0, ratio = 1, makeupDb = 0;
        float exciter = 0;
        float saturation = 0, saturationDrive = 1, saturationBias = 0, saturationOffset = 0;
        float body = 0, bodyDrive = 1;
        float clipper = 0;
        float reverb = 0;
    };

    void updateSettings();
    float compensation();
    float shape(int channel, float x);
    void widen(float inLeft, float inRight, float& outLeft, float& outRight);
    void resetEffects();

    float target_ = 0;
    float current_ = 0;
    bool engaged_ = false;
    float amountCoef_;
    Settings settings_;

    std::array<Biquad, 2> highpass_;
    std::array<Biquad, 2> exciterBand_;
    std::array<Biquad, 2> exciterPost_;
    std::array<Biquad, 2> bodyBand_;
    float bodyLowRms_ = 0, bodyWarmRms_ = 0, bodyGain_ = 1;
    float bodyCoef_;
    std::array<Biquad, 2> reverbSend_;
    std::array<Oversampler, 2> oversampler_;
    Reverb reverb_;

    float compressorEnvelope_ = 0;
    float attackCoef_, releaseCoef_;

    // Energia del segnale per livello dell'inviluppo del compressore, a
    // passi di 1 dB, che sfuma come le medie del guadagno automatico.
    static constexpr int kLevelBins = 72;
    static constexpr float kLevelFloorDb = -72.0f;
    std::array<float, kLevelBins> levelEnergy_{};
    float levelDecay_;
    float compensation_ = 1;
    float compensationCoef_;

    float rmsIn_ = 0, rmsOut_ = 0, autoGain_ = 1;
    float rmsCoef_, autoGainCoef_;

    // Lo Spazio: il segnale dalle medie in su, compresso molto e allargato
    // (lato stereo rinforzato, più un lato nuovo ricavato dal centro), viene
    // aggiunto in piccola parte all'uscita.
    std::array<Biquad, 4> spaceBand_; // due per canale: 24 dB/ottava
    float spaceEnvelope_ = 0;
    float spaceAttackCoef_, spaceReleaseCoef_;
    float spaceRms_ = 0, spaceGain_ = 1;
    std::array<Diffuser, 4> spaceDiffusers_;

    // A riposo (manopola a 0) passano solo i ritardi; quando si riaccende,
    // gli effetti ripartono da zero.
    bool idle_ = true;

    size_t lookahead_;
    std::vector<float> limiterDelay_;
    std::vector<float> limiterTargets_;
    size_t limiterPos_ = 0;
    float limiterGain_ = 1;
    float limiterAttack_, limiterRelease_;

    size_t latency_;
    std::vector<float> dryDelay_;
    size_t dryPos_ = 0;
};

} // namespace mp
