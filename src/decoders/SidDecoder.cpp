#include "SidDecoder.h"

#include "AudioTags.h"
#include "core/Platform.h"

#include <builders/residfp-builder/residfp.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/sidplayfp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// I file SID non dicono quanto dura un brano, e quasi tutti girano per
// sempre: si chiude dopo tre minuti, con una dissolvenza negli ultimi cinque
// secondi.
constexpr double kLengthSeconds = 180;
constexpr double kFadeSeconds = 5;
// Quanto far avanzare l'emulazione a ogni passo: ~5 ms a 1 MHz.
constexpr unsigned int kCycles = 5000;

bool hasSidMagic(const std::vector<uint8_t>& data)
{
    return data.size() >= 4 && (std::memcmp(data.data(), "PSID", 4) == 0 || std::memcmp(data.data(), "RSID", 4) == 0);
}

class SidDecoder final : public Decoder {
public:
    SidDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , builder_("megaplayer")
        , tune_(file.data.data(), static_cast<uint_least32_t>(file.data.size()))
    {
        if (!tune_.getStatus())
            throw DecoderError(tune_.statusString());

        SidConfig config = engine_.config();
        config.frequency = static_cast<uint_least32_t>(spec.sampleRate);
        config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
        config.sidEmulation = &builder_;
        if (!engine_.config(config))
            throw DecoderError(engine_.error());

        const SidTuneInfo* tune = tune_.getInfo();
        info_.backend = "sid";
        info_.platform = platform::kC64;
        info_.format = std::string(reinterpret_cast<const char*>(file.data.data()), hasSidMagic(file.data) ? 4 : 0);
        switch (tune->sidModel(0)) {
        case SidTuneInfo::SIDMODEL_6581: info_.player = "MOS 6581"; break;
        case SidTuneInfo::SIDMODEL_8580: info_.player = "MOS 8580"; break;
        default: info_.player = "MOS 6581/8580"; break;
        }
        // Nei PSID: titolo, autore e anno con editore, in Latin-1.
        if (tune->numberOfInfoStrings() > 0)
            info_.title = textToUtf8(tune->infoString(0));
        if (tune->numberOfInfoStrings() > 1)
            info_.author = textToUtf8(tune->infoString(1));
        info_.subsongCount = static_cast<int>(std::max(1u, tune->songs()));
        chips_ = std::max(1, tune->sidChips());
        muted_.assign(static_cast<size_t>(chips_ * kVoices), false);
        info_.duration = kLengthSeconds;
        selectSubsong(static_cast<int>(std::max(1u, tune->startSong())) - 1);
    }

    SidDecoder(const SidDecoder&) = delete;
    SidDecoder& operator=(const SidDecoder&) = delete;

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        tune_.selectSong(static_cast<unsigned int>(index) + 1);
        if (!engine_.load(&tune_))
            throw DecoderError(engine_.error());
        // Mixer stereo e dimensione di un passo si preparano a brano caricato.
        engine_.initMixer(true);
        // Caricare il brano ricrea i chip: i mute vanno rimessi.
        for (size_t i = 0; i < muted_.size(); ++i)
            applyMute(static_cast<int>(i));
        mixed_.resize(static_cast<size_t>(std::max(engine_.getBufSize(kCycles), 0)) * kChannels + 64);
        info_.currentSubsong = index;
        pending_.clear();
        pendingStart_ = 0;
        rendered_ = 0;
    }

    size_t render(float* out, size_t frames) override
    {
        const size_t total = static_cast<size_t>(kLengthSeconds * sampleRate_);
        frames = std::min(frames, total - std::min(total, rendered_));
        const size_t fadeStart = total - static_cast<size_t>(kFadeSeconds * sampleRate_);
        for (size_t i = 0; i < frames; ++i) {
            if (pendingStart_ == pending_.size())
                fill();
            const size_t frame = rendered_ + i;
            const float gain = frame < fadeStart ? 1.0f : static_cast<float>(total - frame) / (total - fadeStart);
            for (int c = 0; c < kChannels; ++c)
                out[i * kChannels + c] = pending_[pendingStart_ + c] / 32768.0f * gain;
            pendingStart_ += kChannels;
        }
        rendered_ += frames;
        return frames;
    }

    bool canSeek() const override { return true; }

    // Il SID non torna indietro: si riparte dall'inizio del sottobrano e si
    // emula a vuoto fino al punto richiesto.
    void seek(double seconds) override
    {
        const size_t target = static_cast<size_t>(std::clamp(seconds, 0.0, kLengthSeconds) * sampleRate_);
        if (target < rendered_)
            selectSubsong(info_.currentSubsong);
        std::vector<float> scratch(4096 * kChannels);
        while (rendered_ < target) {
            if (render(scratch.data(), std::min<size_t>(4096, target - rendered_)) == 0)
                break;
        }
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    // Le tre voci di ogni chip, e i campioni digitalizzati (scritti nel
    // registro del volume) come quarto canale.
    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int chip = 0; chip < chips_; ++chip) {
            const std::string prefix = chips_ > 1 ? "SID " + std::to_string(chip + 1) + " · " : "";
            for (int voice = 0; voice < kVoices; ++voice)
                names.push_back(prefix + (voice < 3 ? "Voce " + std::to_string(voice + 1) : std::string("Digi")));
        }
        return names;
    }

    void setChannelMuted(int channel, bool muted) override
    {
        muted_.at(static_cast<size_t>(channel)) = muted;
        applyMute(channel);
    }

private:
    static constexpr int kVoices = 4; // tre voci e i campioni

    // libsidplayfp filtra le scritture nei registri del chip: una voce si
    // zittisce (o si riaccende) alla prossima scrittura nel suo registro di
    // controllo, che quasi tutti i replayer rifanno a ogni frame. Il terzo
    // argomento vuol dire zittita, al contrario di quanto dice sidplayfp.h.
    void applyMute(int channel)
    {
        engine_.mute(static_cast<unsigned int>(channel / kVoices), static_cast<unsigned int>(channel % kVoices),
                     muted_[static_cast<size_t>(channel)]);
    }

    // Fa avanzare l'emulazione di un passo e raccoglie i campioni prodotti.
    void fill()
    {
        pending_.clear();
        pendingStart_ = 0;
        while (pending_.empty()) {
            const int produced = engine_.play(kCycles);
            if (produced < 0)
                throw DecoderError(engine_.error());
            const unsigned int count = engine_.mix(mixed_.data(), static_cast<unsigned int>(produced));
            if (produced > 0 && count == 0)
                throw DecoderError("il mixer di libsidplayfp non produce campioni");
            pending_.insert(pending_.end(), mixed_.begin(), mixed_.begin() + count);
        }
    }

    int sampleRate_;
    // Il motore usa emulatore e brano: va distrutto prima di loro.
    ReSIDfpBuilder builder_;
    SidTune tune_;
    sidplayfp engine_;
    std::vector<short> mixed_;
    std::vector<short> pending_; // stereo interleaved, da pendingStart_ in poi
    size_t pendingStart_ = 0;
    size_t rendered_ = 0;
    int chips_ = 1;
    std::vector<bool> muted_; // kVoices per chip
    TrackInfo info_;
};

} // namespace

std::string_view SidDecoderFactory::description() const
{
    return "libsidplayfp: Commodore 64 (PSID, RSID) con reSIDfp";
}

int SidDecoderFactory::probe(const SourceFile& file)
{
    return hasSidMagic(file.data) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> SidDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<SidDecoder>(file, spec);
}

} // namespace mp
