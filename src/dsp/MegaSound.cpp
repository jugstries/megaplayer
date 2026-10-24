#include "MegaSound.h"

#include <algorithm>
#include <cmath>

namespace mp {

namespace {

// Dove entra ogni effetto lungo la corsa della manopola (0..1) e fino a che
// intensità arriva: da qui si regola il carattere di MegaSound.

// Sotto questa posizione la catena si miscela gradualmente al suono
// originale.
constexpr float kFullMixAt = 0.12f;
// Compressore: soglia e rapporto crescono su tutta la corsa; il make-up
// recupera una parte del volume tolto.
constexpr float kThresholdFromDb = -6.0f;
constexpr float kThresholdToDb = -24.0f;
constexpr float kRatioTo = 5.0f;
constexpr float kMakeupShare = 0.25f;
constexpr float kKneeDb = 6.0f;
// Exciter: armoniche sopra i 3 kHz, per ravvivare i campioni a 8 bit.
constexpr float kExciterFullAt = 0.7f;
constexpr float kExciterMax = 0.25f;
// Saturazione morbida, con un po' di asimmetria per le armoniche pari.
constexpr float kSaturationFullAt = 0.5f;
constexpr float kSaturationDriveMax = 4.0f;
constexpr float kSaturationBiasMax = 0.15f;
// Corpo, nella seconda metà della corsa: una saturazione in più solo su
// bassi e medie basse (sotto kBodyHz), riportati poi al loro volume medio.
// Il basso diventa più denso e prende armoniche calde, senza sporcare gli
// alti e, a differenza di una distorsione, senza alzare i picchi.
constexpr float kBodyFrom = 0.5f;
constexpr double kBodyHz = 300;
constexpr float kBodyDriveMax = 4.0f;
// Riverbero avvolgente, senza bassi (mandata filtrata a kReverbSendHz): entra
// presto, sale in fretta nella prima metà, poi più piano fino a fine corsa, e
// la stanza cresce con lui. Il pre-delay lo stacca dal suono diretto, così si
// sente come spazio attorno e non impasta.
constexpr float kReverbFrom = 0.1f;
constexpr float kReverbMax = 0.39f;
constexpr float kRoomFrom = 0.6f;
constexpr float kRoomTo = 0.79f;
constexpr double kReverbSendHz = 350;
constexpr double kReverbPreDelayMs = 20;
// Il colore: scuro fino a kReverbToneFrom (mandata tagliata sopra
// kReverbDarkHz, alti della coda smorzati in fretta), poi si schiarisce fino
// a fine corsa, dove la coda è più lunga e più forte e altrimenti suonerebbe
// cupa.
constexpr float kReverbToneFrom = 0.6f;
constexpr double kReverbDarkHz = 2700;
constexpr double kReverbBrightHz = 5500;
constexpr float kReverbDarkDamp = 0.6f;
constexpr float kReverbBrightDamp = 0.42f;
// Clipper morbido nell'ultimo tratto, tarato sul soffitto finale: il margine
// di volume lo spinge in saturazione, invece di lasciare tutto al limiter.
constexpr float kClipperFrom = 0.7f;
// Volume: la compensazione restituisce subito quello che toglie il
// compressore, il guadagno automatico il resto (saturazione, riverbero) su una
// media di ~1,5 s; poi si aggiunge un margine che dipende solo dalla manopola.
// Così girandola il volume sale e basta. Un po' più forte suona comunque
// meglio, ma senza esagerare.
constexpr float kLoudnessBonusDb = 3.5f;
// Spazio: il ramo parallelo che allarga la scena, presente fin dal primo
// tratto di manopola. Prende il segnale da kSpaceFromHz in su, lo comprime
// molto (così tira su dettagli e code), lo allarga e lo somma all'uscita. Il
// ramo si riporta da solo al volume dell'originale, poi entra a
// kSpaceLevelDb: così l'effetto è simile sui brani piani e su quelli già
// molto forti.
constexpr double kSpaceFromHz = 800;
constexpr float kSpaceThresholdDb = -30.0f;
constexpr float kSpaceRatio = 10.0f;
// Quanto centro, quanto lato stereo esistente e quanto lato ricavato dal
// centro decorrelato (quello che apre anche i brani mono).
constexpr float kSpaceMid = 0.35f;
constexpr float kSpaceSide = 1.0f;
constexpr float kSpaceDiffuse = 0.8f;
constexpr float kSpaceLevelDb = -9.0f;
// Il decorrelatore: quattro allpass, ritardi in millisecondi.
constexpr std::array<double, 4> kDiffuserMs = {1.7, 3.1, 4.9, 7.3};
constexpr float kDiffuserGain = 0.6f;
// Soffitto del limiter: -0,5 dBFS.
constexpr float kCeiling = 0.944f;

// Ritardo del sovracampionamento, in frame: (taps - 1) / 2 campioni
// sovracampionati per il filtro di salita e altrettanti per la discesa.
constexpr size_t kOversamplingLatency = 31;
constexpr int kSettingsBlock = 32;

constexpr double kPi = 3.14159265358979323846;

float ramp(float value, float from, float to)
{
    return std::clamp((value - from) / (to - from), 0.0f, 1.0f);
}

float smoothstep(float x)
{
    return x * x * (3 - 2 * x);
}

float dbToGain(float db)
{
    return std::exp(db * 0.11512925f);
}

float coefficient(double seconds, double sampleRate)
{
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
}

// I valori piccolissimi (denormali) rallentano molto le CPU: nelle code del
// riverbero si azzerano.
float flushDenormal(float x)
{
    return std::fabs(x) < 1e-15f ? 0.0f : x;
}

// Avvicina value a target; vicinissimo, lo raggiunge: così "spento" arriva a
// zero esatto e la catena può tornare a riposo.
float approach(float value, float target, float coefficient)
{
    value += (target - value) * coefficient;
    return std::fabs(target - value) < 1e-5f ? target : value;
}

float gainToDb(float gain)
{
    return 20 * std::log10(gain + 1e-9f);
}

// Riduzione di guadagno, in dB, di un compressore a ginocchio morbido.
float compressionDb(float envelopeDb, float thresholdDb, float ratio, float kneeDb)
{
    const float over = envelopeDb - thresholdDb;
    if (2 * std::fabs(over) <= kneeDb)
        return (1 / ratio - 1) * (over + kneeDb / 2) * (over + kneeDb / 2) / (2 * kneeDb);
    return over > 0 ? (1 / ratio - 1) * over : 0.0f;
}

} // namespace

// --- Allpass del decorrelatore ---

float MegaSound::Diffuser::process(float x)
{
    const float delayed = buffer[pos];
    const float v = flushDenormal(x + kDiffuserGain * delayed);
    buffer[pos] = v;
    if (++pos == buffer.size())
        pos = 0;
    return delayed - kDiffuserGain * v;
}

void MegaSound::Diffuser::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    pos = 0;
}

// --- Biquad (passa-alto di Robert Bristow-Johnson) ---

void MegaSound::Biquad::highpass(double sampleRate, double frequency)
{
    const double w0 = 2 * kPi * frequency / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * 0.70710678);
    const double a0 = 1 + alpha;
    b0 = static_cast<float>((1 + cosw) / 2 / a0);
    b1 = static_cast<float>(-(1 + cosw) / a0);
    b2 = b0;
    a1 = static_cast<float>(-2 * cosw / a0);
    a2 = static_cast<float>((1 - alpha) / a0);
}

void MegaSound::Biquad::lowpass(double sampleRate, double frequency)
{
    const double w0 = 2 * kPi * frequency / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * 0.70710678);
    const double a0 = 1 + alpha;
    b0 = static_cast<float>((1 - cosw) / 2 / a0);
    b1 = static_cast<float>((1 - cosw) / a0);
    b2 = b0;
    a1 = static_cast<float>(-2 * cosw / a0);
    a2 = static_cast<float>((1 - alpha) / a0);
}

float MegaSound::Biquad::process(float x)
{
    const float y = b0 * x + z1;
    z1 = flushDenormal(b1 * x - a1 * y + z2);
    z2 = flushDenormal(b2 * x - a2 * y);
    return y;
}

// --- Sovracampionamento 2× ---

MegaSound::Oversampler::Oversampler()
{
    // Sinc finestrato (Blackman), taglio appena sotto la Nyquist originale.
    constexpr double cutoff = 0.23; // cicli per campione sovracampionato
    constexpr int middle = (kTaps - 1) / 2;
    double sum = 0;
    for (int n = 0; n < kTaps; ++n) {
        const double t = n - middle;
        const double sinc = t == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * t) / (kPi * t);
        const double window = 0.42 - 0.5 * std::cos(2 * kPi * n / (kTaps - 1)) + 0.08 * std::cos(4 * kPi * n / (kTaps - 1));
        taps_[n] = static_cast<float>(sinc * window);
        sum += taps_[n];
    }
    for (float& tap : taps_)
        tap = static_cast<float>(tap / sum);
}

void MegaSound::Oversampler::up(float x, float& first, float& second)
{
    // Polifase: i taps pari producono il campione allineato all'ingresso,
    // quelli dispari il campione intermedio.
    constexpr int phase = (kTaps + 1) / 2;
    upPos_ = (upPos_ + 1) % phase;
    upHistory_[upPos_] = x;
    float even = 0;
    float odd = 0;
    for (int k = 0; k < phase; ++k) {
        const float sample = upHistory_[(upPos_ - k + phase) % phase];
        even += taps_[2 * k] * sample;
        if (2 * k + 1 < kTaps)
            odd += taps_[2 * k + 1] * sample;
    }
    first = 2 * even;
    second = 2 * odd;
}

float MegaSound::Oversampler::down(float first, float second)
{
    // Si tiene un campione su due: quello allineato, così il ritardo totale
    // è un numero intero di frame.
    downPos_ = (downPos_ + 1) % kTaps;
    downHistory_[downPos_] = first;
    float y = 0;
    for (int j = 0; j < kTaps; ++j)
        y += taps_[j] * downHistory_[(downPos_ - j + kTaps) % kTaps];
    downPos_ = (downPos_ + 1) % kTaps;
    downHistory_[downPos_] = second;
    return y;
}

void MegaSound::Oversampler::reset()
{
    upHistory_.fill(0);
    downHistory_.fill(0);
}

// --- Riverbero ---

void MegaSound::Reverb::init(int sampleRate)
{
    // Lunghezze originali di Freeverb, pensate per 44,1 kHz.
    constexpr std::array<int, 8> combs = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    constexpr std::array<int, 4> allpasses = {556, 441, 341, 225};
    constexpr int stereoSpread = 23;
    const double scale = sampleRate / 44100.0;
    auto length = [&](int samples) { return static_cast<size_t>(std::lround(samples * scale)); };
    for (size_t i = 0; i < combs.size(); ++i) {
        combsLeft_[i].buffer.assign(length(combs[i]), 0);
        combsRight_[i].buffer.assign(length(combs[i] + stereoSpread), 0);
    }
    for (size_t i = 0; i < allpasses.size(); ++i) {
        allpassLeft_[i].buffer.assign(length(allpasses[i]), 0);
        allpassRight_[i].buffer.assign(length(allpasses[i] + stereoSpread), 0);
    }
    preDelay_.assign(static_cast<size_t>(std::lround(kReverbPreDelayMs * sampleRate / 1000)), 0);
}

void MegaSound::Reverb::reset()
{
    for (auto* combs : {&combsLeft_, &combsRight_}) {
        for (Comb& c : *combs) {
            std::fill(c.buffer.begin(), c.buffer.end(), 0.0f);
            c.store = 0;
        }
    }
    for (auto* allpasses : {&allpassLeft_, &allpassRight_}) {
        for (Allpass& a : *allpasses)
            std::fill(a.buffer.begin(), a.buffer.end(), 0.0f);
    }
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
    preDelayPos_ = 0;
}

void MegaSound::Reverb::setRoom(float roomSize, float damp)
{
    feedback_ = roomSize * 0.28f + 0.7f;
    damp_ = damp;
}

float MegaSound::Reverb::comb(Comb& c, float input)
{
    const float output = c.buffer[c.pos];
    c.store = flushDenormal(output * (1 - damp_) + c.store * damp_);
    c.buffer[c.pos] = input + c.store * feedback_;
    if (++c.pos == c.buffer.size())
        c.pos = 0;
    return output;
}

float MegaSound::Reverb::allpass(Allpass& a, float input)
{
    const float buffered = a.buffer[a.pos];
    a.buffer[a.pos] = flushDenormal(input + buffered * 0.5f);
    if (++a.pos == a.buffer.size())
        a.pos = 0;
    return buffered - input;
}

void MegaSound::Reverb::process(float input, float& left, float& right)
{
    const float delayed = preDelay_[preDelayPos_];
    preDelay_[preDelayPos_] = input;
    if (++preDelayPos_ == preDelay_.size())
        preDelayPos_ = 0;
    input = delayed;
    left = 0;
    right = 0;
    for (size_t i = 0; i < combsLeft_.size(); ++i) {
        left += comb(combsLeft_[i], input);
        right += comb(combsRight_[i], input);
    }
    for (size_t i = 0; i < allpassLeft_.size(); ++i) {
        left = allpass(allpassLeft_[i], left);
        right = allpass(allpassRight_[i], right);
    }
}

// --- La catena ---

MegaSound::MegaSound(int sampleRate)
    : sampleRate_(sampleRate)
    , amountCoef_(coefficient(0.06, sampleRate))
    , bodyCoef_(coefficient(0.3, 4.0 * sampleRate)) // 2 canali a frequenza doppia
    , attackCoef_(coefficient(0.005, sampleRate))
    , releaseCoef_(coefficient(0.12, sampleRate))
    , rmsCoef_(coefficient(1.5, sampleRate))
    , autoGainCoef_(coefficient(0.3, sampleRate))
    , spaceAttackCoef_(coefficient(0.002, sampleRate))
    , spaceReleaseCoef_(coefficient(0.08, sampleRate))
    , lookahead_(static_cast<size_t>(std::lround(0.003 * sampleRate)))
{
    for (int c = 0; c < 2; ++c) {
        highpass_[c].highpass(sampleRate, 25);
        exciterBand_[c].highpass(2.0 * sampleRate, 3000);
        exciterPost_[c].highpass(2.0 * sampleRate, 4500);
        bodyBand_[c].lowpass(2.0 * sampleRate, kBodyHz);
        reverbSend_[c].highpass(sampleRate, kReverbSendHz);
    }
    reverb_.init(sampleRate);
    for (Biquad& band : spaceBand_)
        band.highpass(sampleRate, kSpaceFromHz);
    for (size_t i = 0; i < spaceDiffusers_.size(); ++i)
        spaceDiffusers_[i].buffer.assign(static_cast<size_t>(std::lround(kDiffuserMs[i] * sampleRate / 1000)), 0);

    limiterDelay_.assign(lookahead_ * 2, 0);
    limiterTargets_.assign(lookahead_ + 1, 1);
    limiterAttack_ = static_cast<float>(1.0 - std::exp(-4.0 / lookahead_));
    limiterRelease_ = coefficient(0.1, sampleRate);
    levelDecay_ = static_cast<float>(std::pow(1.0 - rmsCoef_, kSettingsBlock));
    compensationCoef_ = coefficient(0.02, sampleRate);

    // Il limiter sta dopo la somma; prima, l'originale aspetta solo il
    // sovracampionamento della catena.
    latency_ = kOversamplingLatency + lookahead_;
    dryDelay_.assign(kOversamplingLatency * 2, 0);
    updateSettings();
}

void MegaSound::setAmount(float amount)
{
    target_ = std::clamp(amount, 0.0f, 1.0f);
    if (target_ > 0)
        engaged_ = true;
}

void MegaSound::resetEffects()
{
    for (int c = 0; c < 2; ++c) {
        highpass_[c].reset();
        exciterBand_[c].reset();
        exciterPost_[c].reset();
        bodyBand_[c].reset();
        reverbSend_[c].reset();
        reverbTone_[c].reset();
        oversampler_[c].reset();
    }
    reverb_.reset();
    compressorEnvelope_ = 0;
    levelEnergy_.fill(0);
    compensation_ = 1;
    rmsIn_ = rmsOut_ = 0;
    autoGain_ = 1;
    bodyLowRms_ = bodyWarmRms_ = 0;
    bodyGain_ = 1;
    for (Biquad& band : spaceBand_)
        band.reset();
    for (Diffuser& diffuser : spaceDiffusers_)
        diffuser.reset();
    spaceEnvelope_ = 0;
    // Il guadagno del ramo resta quello del brano prima, che di solito va già
    // bene: le due medie ripartono insieme, e il rapporto è giusto subito.
    spaceRms_ = 0;
}

void MegaSound::reset()
{
    current_ = target_;
    engaged_ = target_ > 0;
    idle_ = !engaged_;
    resetEffects();
    std::fill(limiterDelay_.begin(), limiterDelay_.end(), 0.0f);
    std::fill(limiterTargets_.begin(), limiterTargets_.end(), 1.0f);
    limiterPos_ = 0;
    limiterGain_ = 1;
    std::fill(dryDelay_.begin(), dryDelay_.end(), 0.0f);
    dryPos_ = 0;
    updateSettings();
}

void MegaSound::updateSettings()
{
    const float a = current_;
    Settings& s = settings_;
    s.mix = smoothstep(ramp(a, 0, kFullMixAt));
    s.bonus = dbToGain(kLoudnessBonusDb * a);

    s.thresholdDb = kThresholdFromDb + (kThresholdToDb - kThresholdFromDb) * a;
    s.ratio = 1 + (kRatioTo - 1) * a;
    s.makeupDb = kMakeupShare * -s.thresholdDb * (1 - 1 / s.ratio);

    s.exciter = kExciterMax * ramp(a, 0, kExciterFullAt);

    s.saturation = ramp(a, 0, kSaturationFullAt);
    s.saturationDrive = 1 + (kSaturationDriveMax - 1) * a;
    s.saturationBias = kSaturationBiasMax * a;
    s.saturationOffset = std::tanh(s.saturationDrive * s.saturationBias);

    s.body = ramp(a, kBodyFrom, 1);
    s.bodyDrive = 1 + (kBodyDriveMax - 1) * s.body;

    s.clipper = ramp(a, kClipperFrom, 1);

    s.reverb = kReverbMax * std::sqrt(ramp(a, kReverbFrom, 1));
    const float tone = ramp(a, kReverbToneFrom, 1);
    reverb_.setRoom(kRoomFrom + (kRoomTo - kRoomFrom) * ramp(a, kReverbFrom, 1),
                    kReverbDarkDamp + (kReverbBrightDamp - kReverbDarkDamp) * tone);
    if (tone != reverbToneAt_) {
        const double hz = kReverbDarkHz * std::pow(kReverbBrightHz / kReverbDarkHz, tone);
        for (Biquad& filter : reverbTone_)
            filter.lowpass(sampleRate_, hz);
        reverbToneAt_ = tone;
    }
}

// Quanto volume toglie il compressore, alle impostazioni di adesso, al
// segnale dell'ultimo secondo e mezzo (l'energia contata per livello
// dell'inviluppo), e quindi quanto restituire. Così girare la manopola non
// abbassa il volume per qualche secondo, aspettando il guadagno automatico.
float MegaSound::compensation()
{
    const Settings& s = settings_;
    double input = 0;
    double output = 0;
    for (int b = 0; b < kLevelBins; ++b) {
        float& energy = levelEnergy_[b];
        energy = energy * levelDecay_ < 1e-20f ? 0.0f : energy * levelDecay_;
        if (energy == 0)
            continue;
        const float gain =
            dbToGain(compressionDb(kLevelFloorDb + b + 0.5f, s.thresholdDb, s.ratio, kKneeDb) + s.makeupDb);
        input += energy;
        output += energy * gain * gain;
    }
    return output > 0 ? static_cast<float>(std::clamp(std::sqrt(input / output), 0.25, 16.0)) : 1.0f;
}

// La parte non lineare, a frequenza doppia.
float MegaSound::shape(int channel, float x)
{
    const Settings& s = settings_;

    const float band = exciterBand_[channel].process(x);
    const float harmonics = exciterPost_[channel].process(std::tanh(3 * band) + 0.6f * std::fabs(band));
    x += s.exciter * harmonics;

    // Il corpo: la banda bassa satura di più e torna al suo volume medio, il
    // resto passa com'è.
    const float low = bodyBand_[channel].process(x);
    if (s.body > 0) {
        const float warm = std::tanh(s.bodyDrive * low) / s.bodyDrive;
        bodyLowRms_ += (low * low - bodyLowRms_) * bodyCoef_;
        bodyWarmRms_ += (warm * warm - bodyWarmRms_) * bodyCoef_;
        if (bodyWarmRms_ > 1e-12f)
            bodyGain_ += (std::clamp(std::sqrt(bodyLowRms_ / bodyWarmRms_), 1.0f, 4.0f) - bodyGain_) * bodyCoef_;
        x += s.body * (warm * bodyGain_ - low);
    }

    const float saturated =
        (std::tanh(s.saturationDrive * (x + s.saturationBias)) - s.saturationOffset) / s.saturationDrive;
    x += s.saturation * (saturated - x);

    if (s.clipper > 0) {
        // Cubica a pendenza unitaria nell'origine, piatta a ±1 da ±1,5; 1 è
        // il soffitto, una volta applicati compensazione e guadagno
        // automatico.
        const float scale = compensation_ * autoGain_ * s.bonus / kCeiling;
        const float v = x * scale;
        const float clipped = std::fabs(v) < 1.5f ? v - (4.0f / 27.0f) * v * v * v : std::copysign(1.0f, v);
        x += s.clipper * (clipped / scale - x);
    }
    return std::clamp(x, -4.0f, 4.0f);
}

// Lo Spazio, sul segnale originale: banda dalle medie in su, compressa molto
// e allargata in M/S. In mono la parte larga si annulla e resta il centro.
void MegaSound::widen(float inLeft, float inRight, float& outLeft, float& outRight)
{
    float left = spaceBand_[1].process(spaceBand_[0].process(inLeft));
    float right = spaceBand_[3].process(spaceBand_[2].process(inRight));

    const float level = std::max(std::fabs(left), std::fabs(right));
    spaceEnvelope_ += (level - spaceEnvelope_) * (level > spaceEnvelope_ ? spaceAttackCoef_ : spaceReleaseCoef_);
    const float gain = dbToGain(compressionDb(gainToDb(spaceEnvelope_), kSpaceThresholdDb, kSpaceRatio, kKneeDb));
    left *= gain;
    right *= gain;

    // Al volume dell'originale, sulla stessa media del guadagno automatico.
    spaceRms_ += (0.5f * (left * left + right * right) - spaceRms_) * rmsCoef_;
    if (rmsIn_ > 1e-12f && spaceRms_ > 1e-12f) {
        const float target = std::clamp(std::sqrt(rmsIn_ / spaceRms_), 0.1f, 30.0f);
        spaceGain_ += (target - spaceGain_) * autoGainCoef_;
    }
    left *= spaceGain_;
    right *= spaceGain_;

    const float mid = 0.5f * (left + right);
    const float side = 0.5f * (left - right);
    float diffused = mid;
    for (Diffuser& diffuser : spaceDiffusers_)
        diffused = diffuser.process(diffused);
    const float width = kSpaceSide * side + kSpaceDiffuse * diffused;
    outLeft = kSpaceMid * mid + width;
    outRight = kSpaceMid * mid - width;
}

void MegaSound::process(float* frames, size_t count)
{
    const float spaceLevel = dbToGain(kSpaceLevelDb);
    float autoGainTarget = 1;
    float compensationTarget = compensation_;
    for (size_t i = 0; i < count; ++i) {
        current_ = approach(current_, target_, amountCoef_);
        const bool idle = current_ == 0 && target_ == 0;
        if (idle_ && !idle) {
            resetEffects();
            updateSettings();
        }
        idle_ = idle;

        const float inLeft = frames[i * 2];
        const float inRight = frames[i * 2 + 1];

        // L'originale, ritardato quanto il sovracampionamento.
        const float dryLeft = dryDelay_[dryPos_ * 2];
        const float dryRight = dryDelay_[dryPos_ * 2 + 1];
        dryDelay_[dryPos_ * 2] = inLeft;
        dryDelay_[dryPos_ * 2 + 1] = inRight;
        dryPos_ = (dryPos_ + 1) % kOversamplingLatency;

        float outLeft = dryLeft;
        float outRight = dryRight;
        // Quanto conta il limiter: niente a riposo, così l'uscita resta
        // l'originale ritardato, identico al bit.
        float engage = 0;

        if (!idle) {
            if (i % kSettingsBlock == 0) {
                updateSettings();
                compensationTarget = compensation();
                if (rmsIn_ > 1e-9f && rmsOut_ > 1e-9f)
                    autoGainTarget = std::clamp(std::sqrt(rmsIn_ / rmsOut_), 0.25f, 4.0f);
            }
            const Settings& s = settings_;

            float left = highpass_[0].process(inLeft);
            float right = highpass_[1].process(inRight);
            // Il volume dell'originale si misura dopo il passa-alto: certi
            // brani hanno molta corrente continua, che non si sente.
            const float energy = 0.5f * (left * left + right * right);
            rmsIn_ += (energy - rmsIn_) * rmsCoef_;

            // Compressore a soglia morbida, canali collegati. L'energia del
            // segnale si conta per livello dell'inviluppo, per la
            // compensazione.
            const float level = std::max(std::fabs(left), std::fabs(right));
            compressorEnvelope_ +=
                (level - compressorEnvelope_) * (level > compressorEnvelope_ ? attackCoef_ : releaseCoef_);
            const float envelopeDb = gainToDb(compressorEnvelope_);
            const int bin = std::clamp(static_cast<int>(envelopeDb - kLevelFloorDb), 0, kLevelBins - 1);
            levelEnergy_[bin] += energy;
            const float gain = dbToGain(compressionDb(envelopeDb, s.thresholdDb, s.ratio, kKneeDb) + s.makeupDb);
            left *= gain;
            right *= gain;

            float up[2][2];
            oversampler_[0].up(left, up[0][0], up[0][1]);
            oversampler_[1].up(right, up[1][0], up[1][1]);
            for (int c = 0; c < 2; ++c) {
                up[c][0] = shape(c, up[c][0]);
                up[c][1] = shape(c, up[c][1]);
            }
            left = oversampler_[0].down(up[0][0], up[0][1]);
            right = oversampler_[1].down(up[1][0], up[1][1]);

            float reverbLeft;
            float reverbRight;
            const float send = (reverbTone_[0].process(reverbSend_[0].process(left))
                                + reverbTone_[1].process(reverbSend_[1].process(right)))
                * 0.015f;
            reverb_.process(send, reverbLeft, reverbRight);
            left += s.reverb * reverbLeft;
            right += s.reverb * reverbRight;

            compensation_ += (compensationTarget - compensation_) * compensationCoef_;
            left *= compensation_;
            right *= compensation_;

            // Guadagno automatico sulla loudness media (finestra di ~1,5 s),
            // per quello che la compensazione non prevede: saturazione e
            // riverbero. Il margine di volume dipende solo dalla manopola e
            // arriva subito.
            rmsOut_ += (0.5f * (left * left + right * right) - rmsOut_) * rmsCoef_;
            autoGain_ += (autoGainTarget - autoGain_) * autoGainCoef_;
            left *= autoGain_ * s.bonus;
            right *= autoGain_ * s.bonus;

            outLeft += s.mix * (left - dryLeft);
            outRight += s.mix * (right - dryRight);

            // Lo Spazio entra insieme alla catena. Il ramo nasce
            // dall'originale e segue il margine di volume della catena, per
            // restare in proporzione.
            const float wide = s.mix * spaceLevel * s.bonus;
            float wideLeft;
            float wideRight;
            widen(dryLeft, dryRight, wideLeft, wideRight);
            outLeft += wide * wideLeft;
            outRight += wide * wideRight;
            engage = s.mix;
        }

        // Limiter con lookahead sulla somma: il guadagno scende prima che il
        // picco esca dal ritardo.
        const float peak = std::max(std::fabs(outLeft), std::fabs(outRight));
        limiterTargets_[limiterPos_ % limiterTargets_.size()] = !idle && peak > kCeiling ? kCeiling / peak : 1.0f;
        if (!idle) {
            const float limit = *std::min_element(limiterTargets_.begin(), limiterTargets_.end());
            limiterGain_ += (limit - limiterGain_) * (limit < limiterGain_ ? limiterAttack_ : limiterRelease_);
        }
        const size_t slot = limiterPos_ % lookahead_;
        float left = limiterDelay_[slot * 2];
        float right = limiterDelay_[slot * 2 + 1];
        limiterDelay_[slot * 2] = outLeft;
        limiterDelay_[slot * 2 + 1] = outRight;
        ++limiterPos_;
        if (engage > 0) {
            const float gain = 1 + engage * (limiterGain_ - 1);
            left *= gain;
            right *= gain;
            if (engage >= 1) {
                left = std::clamp(left, -kCeiling, kCeiling);
                right = std::clamp(right, -kCeiling, kCeiling);
            }
        }
        frames[i * 2] = left;
        frames[i * 2 + 1] = right;
    }
}

} // namespace mp
