#include "AdplugDecoder.h"

#include "AudioTags.h"

#include <adplug.h>
#include <binstr.h>
#include <fprovide.h>
#include <nemuopl.h>
#include <players.h>
#include <silentopl.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace mp {

namespace {

// Quasi tutti i formati AdLib si riconoscono solo dall'estensione, poi il
// lettore di AdPlug controlla i dati.
constexpr int kProbeScore = 60;
// Dove AdPlug batte gli altri backend: i VGM che accetta sono quelli per i
// chip OPL, che gme non suona, e gli MDI sono file MIDI con dentro i timbri
// AdLib, che libADLMIDI ignorerebbe.
constexpr int kPreferredScore = 85;
// La durata si misura suonando il brano a vuoto, fino a dieci minuti:
// oltre, il brano gira su sé stesso e si chiude lì.
constexpr unsigned long kLengthLimitMs = 600000;

// Il brano per AdPlug, dalla memoria. I file accessori (i banchi di strumenti,
// come standard.bnk per ROL) si cercano nella cartella del brano.
class MemoryProvider final : public CFileProvider {
public:
    explicit MemoryProvider(const SourceFile& file)
        : file_(file)
    {
    }

    binistream* open(std::string name) const override
    {
        const std::vector<uint8_t>* data = &file_.data;
        if (name != file_.path.string()) {
            const std::filesystem::path path =
                findIgnoringCase(file_.path.parent_path(), std::filesystem::path(name).filename().string());
            if (path.empty())
                return nullptr;
            try {
                companions_.push_back(loadSourceFile(path).data);
            } catch (const std::exception&) {
                return nullptr;
            }
            data = &companions_.back();
        }
        auto* stream = new binisstream(const_cast<uint8_t*>(data->data()), static_cast<unsigned long>(data->size()));
        // Come CProvider_Filesystem: little endian, float IEEE.
        stream->setFlag(binio::BigEndian, false);
        stream->setFlag(binio::FloatIEEE);
        return stream;
    }

    void close(binistream* stream) const override { delete stream; }

private:
    const SourceFile& file_;
    mutable std::list<std::vector<uint8_t>> companions_;
};

// Il lettore di AdPlug per il file, tra quelli dell'estensione. Lo S3M resta
// a libopenmpt, che suona anche gli strumenti AdLib.
std::unique_ptr<CPlayer> loadPlayer(const SourceFile& file, Copl* opl, const CFileProvider& provider,
                                    const CPlayerDesc** found = nullptr)
{
    const std::string name = file.path.string();
    for (const CPlayerDesc* desc : CAdPlug::players) {
        if (desc->filetype == "Scream Tracker 3")
            continue;
        for (unsigned int i = 0; const char* extension = desc->get_extension(i); ++i) {
            if (!CFileProvider::extension(name, extension))
                continue;
            std::unique_ptr<CPlayer> player(desc->factory(opl));
            if (player && player->load(name, provider)) {
                if (found)
                    *found = desc;
                return player;
            }
            break;
        }
    }
    return nullptr;
}

// Il chip che vede il lettore. Di solito le scritture vanno al chip vero;
// mentre si misurano i sottobrani vanno perse, e si contano le note accese.
class RoutedOpl final : public Copl {
public:
    explicit RoutedOpl(Copl& chip)
        : chip_(chip)
    {
        currType = chip.gettype();
    }

    void measure(bool on)
    {
        measuring_ = on;
        notes_ = 0;
    }

    int notes() const { return notes_; }

    void write(int reg, int value) override
    {
        if (!measuring_) {
            chip_.write(reg, value);
            return;
        }
        // Key-on (bit 5 di B0-B8), o un colpo di batteria in modo ritmico.
        const bool keyOn = (reg & 0xf0) == 0xb0 && (reg & 0x0f) <= 8 && (value & 0x20);
        const bool drum = reg == 0xbd && (value & 0x20) && (value & 0x1f);
        if (keyOn || drum)
            ++notes_;
    }

    // Il banco di registri si segue sempre, anche mentre si misura.
    void setchip(int n) override
    {
        Copl::setchip(n);
        chip_.setchip(n);
    }

    void init() override
    {
        currChip = 0;
        if (!measuring_)
            chip_.init();
    }

    void update(short* buffer, int frames) override { chip_.update(buffer, frames); }

private:
    Copl& chip_;
    bool measuring_ = false;
    int notes_ = 0;
};

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return textToUtf8(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

class AdplugDecoder final : public Decoder {
public:
    AdplugDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , file_(file)
        , chip_(spec.sampleRate)
        , opl_(chip_)
    {
        const MemoryProvider provider(file_);
        player_ = loadPlayer(file_, &opl_, provider);
        if (!player_)
            throw DecoderError("formato AdLib non riconosciuto");
        info_.backend = "adplug";
        info_.format = trimmed(player_->gettype());
        info_.title = trimmed(player_->gettitle());
        info_.author = trimmed(player_->getauthor());
        // Alcuni formati non partono dal primo sottobrano: nei Westwood ADL
        // i primi sono effetti sonori.
        const unsigned int start = player_->getsubsong();
        measureSubsongs();
        info_.subsongCount = static_cast<int>(subsongs_.size());
        int first = 0;
        for (size_t i = 0; i < subsongs_.size(); ++i) {
            if (subsongs_[i].index >= start) {
                first = static_cast<int>(i);
                break;
            }
        }
        selectSubsong(first);
    }

    AdplugDecoder(const AdplugDecoder&) = delete;
    AdplugDecoder& operator=(const AdplugDecoder&) = delete;

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        const Subsong& subsong = subsongs_.at(static_cast<size_t>(index));
        player_->rewind(static_cast<int>(subsong.index));
        info_.currentSubsong = index;
        if (subsong.lengthMs > 0)
            info_.duration = subsong.lengthMs / 1000.0;
        else
            info_.duration.reset();
        const unsigned long limit = subsong.lengthMs > 0 ? subsong.lengthMs : kLengthLimitMs;
        limit_ = static_cast<size_t>(limit / 1000.0 * sampleRate_);
        restart(0);
    }

    size_t render(float* out, size_t frames) override
    {
        frames = std::min(frames, limit_ - std::min(limit_, rendered_));
        size_t done = 0;
        while (done < frames) {
            if (untilTick_ <= 0) {
                if (ended_ || !player_->update()) {
                    ended_ = true;
                    break;
                }
                untilTick_ += sampleRate_ / player_->getrefresh();
            }
            const size_t count = std::min(frames - done, static_cast<size_t>(std::ceil(untilTick_)));
            samples_.resize(count * kChannels);
            chip_.update(samples_.data(), static_cast<int>(count));
            for (size_t i = 0; i < count * kChannels; ++i)
                out[done * kChannels + i] = samples_[i] / 32768.0f;
            untilTick_ -= static_cast<double>(count);
            done += count;
        }
        rendered_ += done;
        return done;
    }

    bool canSeek() const override { return true; }

    // Si riavvolge e si esegue il brano senza generare audio fino al punto
    // richiesto, come CPlayer::seek(), ma nel sottobrano corrente.
    void seek(double seconds) override
    {
        const double target = std::max(0.0, seconds) * 1000;
        player_->rewind(static_cast<int>(subsongs_[static_cast<size_t>(info_.currentSubsong)].index));
        double ms = 0;
        while (ms < target && player_->update())
            ms += 1000.0 / player_->getrefresh();
        restart(static_cast<size_t>(target / 1000 * sampleRate_));
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

private:
    struct Subsong {
        unsigned int index; // per AdPlug
        unsigned long lengthMs;
    };

    // Ogni sottobrano suonato a vuoto, sul chip muto che conta le note: la
    // durata e se suona qualcosa. I sottobrani senza note restano fuori:
    // nei Westwood ADL quasi metà sono vuoti, e passarci sopra
    // fermerebbe la riproduzione.
    void measureSubsongs()
    {
        const unsigned int count = std::max(1u, player_->getsubsongs());
        std::vector<Subsong> all;
        for (unsigned int index = 0; index < count; ++index) {
            opl_.measure(true);
            player_->rewind(static_cast<int>(index));
            double ms = 0;
            while (ms < kLengthLimitMs && player_->update())
                ms += 1000.0 / player_->getrefresh();
            const Subsong subsong{index, static_cast<unsigned long>(ms)};
            all.push_back(subsong);
            if (opl_.notes() > 0)
                subsongs_.push_back(subsong);
        }
        opl_.measure(false);
        // Se nessuno accende note (un formato che le suona in altro modo),
        // si tengono tutti.
        if (subsongs_.empty())
            subsongs_ = all;
    }

    void restart(size_t frame)
    {
        rendered_ = frame;
        untilTick_ = 0;
        ended_ = false;
    }

    int sampleRate_;
    SourceFile file_;
    CNemuopl chip_;
    RoutedOpl opl_;
    std::unique_ptr<CPlayer> player_;
    std::vector<Subsong> subsongs_;
    std::vector<short> samples_;
    double untilTick_ = 0; // frame da generare prima del prossimo tick del brano
    bool ended_ = false;
    size_t limit_ = 0;
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view AdplugDecoderFactory::description() const
{
    return "AdPlug: formati AdLib dei giochi DOS (RAD, HSC, D00, A2M, CMF, IMF, DRO, ROL, ...)";
}

int AdplugDecoderFactory::probe(const SourceFile& file)
{
    CSilentopl silent;
    const MemoryProvider provider(file);
    const CPlayerDesc* desc = nullptr;
    if (!loadPlayer(file, &silent, provider, &desc))
        return 0;
    const bool preferred = desc->filetype == "Video Game Music" || desc->filetype == "AdLib MIDIPlay File";
    return preferred ? kPreferredScore : kProbeScore;
}

std::unique_ptr<Decoder> AdplugDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<AdplugDecoder>(file, spec);
}

} // namespace mp
