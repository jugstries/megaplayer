#include "AtariAudioDecoder.h"

#include "AudioTags.h"
#include "core/Platform.h"

#include <AtariAudio.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// Un SNDH compresso con ICE! di solito arriva già decompresso da ancient:
// se no lo apre AtariAudio, ma ICE! comprime anche altro.
constexpr int kPackedScore = 50;
// Molti SNDH non dicono quanto dura un sottobrano, e AtariAudio non lo trova
// nel suo archivio di durate: come per i SID, si chiude dopo tre minuti, con
// una dissolvenza negli ultimi cinque secondi.
constexpr double kLengthSeconds = 180;
constexpr double kFadeSeconds = 5;

// L'intestazione LHA di livello 0 con cui si comprimono i .ym.
bool isLzhPacked(const std::vector<uint8_t>& data)
{
    return data.size() > 0x15 && data[0] != 0 && std::memcmp(data.data() + 2, "-lh5-", 5) == 0 && data[0x14] == 0;
}

bool isIcePacked(const std::vector<uint8_t>& data)
{
    return data.size() > 16 && (std::memcmp(data.data(), "ICE!", 4) == 0 || std::memcmp(data.data(), "Ice!", 4) == 0);
}

// Un SNDH comincia con tre salti (BRA) del 68000, poi la firma.
bool isSndh(const std::vector<uint8_t>& data)
{
    return data.size() > 16 && data[0] == 0x60 && std::memcmp(data.data() + 12, "SNDH", 4) == 0;
}

// YM2 e YM3 hanno la firma in testa, gli altri (YM4-6, Digimix, YM Tracker)
// anche quella di Leonard.
bool isYm(const std::vector<uint8_t>& data)
{
    if (data.size() <= 16)
        return false;
    const char* bytes = reinterpret_cast<const char*>(data.data());
    return std::memcmp(bytes + 4, "LeOnArD!", 8) == 0 || std::memcmp(bytes, "YM2!", 4) == 0
           || std::memcmp(bytes, "YM3!", 4) == 0 || std::memcmp(bytes, "YM3b", 4) == 0;
}

bool hasYmExtension(const SourceFile& file)
{
    std::string extension = file.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".ym";
}

struct RendererDeleter {
    void operator()(AtariAudioRenderer* renderer) const { AtariAudioRenderer::Destroy(renderer); }
};

class AtariAudioDecoder final : public Decoder {
public:
    AtariAudioDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
    {
        if (file.data.size() > std::numeric_limits<uint32_t>::max())
            throw DecoderError("file troppo grande");
        // Create copia i dati.
        renderer_.reset(AtariAudioRenderer::Create(file.data.data(), static_cast<uint32_t>(file.data.size()),
                                                   static_cast<uint32_t>(spec.sampleRate)));
        if (!renderer_)
            throw DecoderError("non è un file SNDH o YM valido");

        const AtariAudioRenderer::SongInfo& song = renderer_->GetSongInfo();
        info_.backend = "atariaudio";
        info_.platform = platform::kAtariSt;
        info_.format = song.fileFormat;
        info_.title = textToUtf8(song.musicName);
        info_.author = textToUtf8(song.musicAuthor);
        info_.subsongCount = std::max(1, song.subsongCount);
        // Il DAC dell'STE lo usano i SNDH e, per i campioni, i YM Digimix e
        // YM Tracker; negli altri YM i campioni passano dal YM2149.
        const std::string_view format = song.fileFormat;
        if (song.fileType == AtariAudioRenderer::eFileType::eSndh)
            sampleChannel_ = "DAC STE";
        else if (format == "YM Digimix" || format.rfind("YM Tracker", 0) == 0)
            sampleChannel_ = "Campioni";
        selectSubsong(std::clamp(song.defaultSubsong, 1, info_.subsongCount) - 1);
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        // I sottobrani di AtariAudio contano da 1.
        if (!renderer_->InitSubSong(index + 1))
            throw DecoderError("il replayer non parte");
        renderer_->MuteVoices(mutedMask_);
        info_.currentSubsong = index;
        const uint32_t samples = renderer_->GetSubsongDurationSample(index + 1);
        fade_ = samples == 0;
        total_ = fade_ ? static_cast<size_t>(kLengthSeconds * sampleRate_) : samples;
        info_.duration = static_cast<double>(total_) / sampleRate_;
        rendered_ = 0;
    }

    size_t render(float* out, size_t frames) override
    {
        frames = std::min(frames, total_ - std::min(total_, rendered_));
        if (frames == 0)
            return 0;
        // Se il replayer si pianta AtariAudio smette di scrivere: silenzio.
        samples_.assign(frames, 0);
        renderer_->AudioRender(samples_.data(), static_cast<uint32_t>(frames));
        const size_t fadeStart = fade_ ? total_ - static_cast<size_t>(kFadeSeconds * sampleRate_) : total_;
        for (size_t i = 0; i < frames; ++i) {
            const size_t frame = rendered_ + i;
            const float gain =
                frame < fadeStart ? 1.0f : static_cast<float>(total_ - frame) / static_cast<float>(total_ - fadeStart);
            // AtariAudio rende in mono, come l'uscita dell'ST.
            const float sample = samples_[i] / 32768.0f * gain;
            out[i * kChannels] = sample;
            out[i * kChannels + 1] = sample;
        }
        rendered_ += frames;
        return frames;
    }

    bool canSeek() const override { return true; }

    // Il 68000 e il YM2149 non tornano indietro: si riparte dall'inizio del
    // sottobrano e si emula senza produrre campioni fino al punto richiesto.
    void seek(double seconds) override
    {
        const auto target = static_cast<size_t>(std::clamp(seconds, 0.0, static_cast<double>(total_) / sampleRate_)
                                                * sampleRate_);
        if (target < rendered_)
            selectSubsong(info_.currentSubsong);
        if (target > rendered_)
            renderer_->FastForward(static_cast<uint32_t>(target - rendered_));
        rendered_ = target;
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    // Le tre voci del YM2149 e, dove serve, i campioni del DAC.
    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names = {"Voce A", "Voce B", "Voce C"};
        if (!sampleChannel_.empty())
            names.push_back(sampleChannel_);
        return names;
    }

    // Le maschere di AtariAudio hanno lo stesso ordine dei canali: A, B, C e
    // il DAC. InitSubSong le azzera, selectSubsong le rimette.
    void setChannelMuted(int channel, bool muted) override
    {
        const uint32_t bit = 1u << channel;
        mutedMask_ = muted ? mutedMask_ | bit : mutedMask_ & ~bit;
        renderer_->MuteVoices(mutedMask_);
    }

private:
    int sampleRate_;
    std::unique_ptr<AtariAudioRenderer, RendererDeleter> renderer_;
    std::string sampleChannel_; // vuoto se il brano non usa il DAC
    uint32_t mutedMask_ = 0;
    size_t total_ = 0;    // frame del sottobrano
    bool fade_ = false;   // durata ignota: kLengthSeconds e dissolvenza
    size_t rendered_ = 0; // dall'inizio del sottobrano
    std::vector<int16_t> samples_;
    TrackInfo info_;
};

} // namespace

std::string_view AtariAudioDecoderFactory::description() const
{
    return "AtariAudio: Atari ST (SNDH, YM) con 68000 e YM2149 emulati";
}

int AtariAudioDecoderFactory::probe(const SourceFile& file)
{
    if (isSndh(file.data) || isYm(file.data))
        return kProbeScore;
    // Un .ym compresso arriva com'è (vedi isLhaPackedYm in core/Archive.cpp).
    if (isLzhPacked(file.data) && hasYmExtension(file))
        return kProbeScore;
    return isIcePacked(file.data) ? kPackedScore : 0;
}

std::unique_ptr<Decoder> AtariAudioDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<AtariAudioDecoder>(file, spec);
}

} // namespace mp
