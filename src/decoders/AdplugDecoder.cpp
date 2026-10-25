#include "AdplugDecoder.h"

#include "AudioTags.h"
#include "core/Platform.h"

#include <adplug.h>
#include <binstr.h>
#include <fprovide.h>
#include <nemuopl.h>
#include <players.h>
#include <silentopl.h>

#include <algorithm>
#include <cmath>
#include <iterator>
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

// Nove canali per banco di registri; l'OPL3 (e il doppio OPL2) ne ha due.
constexpr int kOplChannels = 18;

// I colpi di batteria del modo ritmico (registro BD) e il canale che li
// suona: la cassa il 7, rullante e charleston l'8, tom e piatto il 9.
int drumBits(int channel)
{
    switch (channel % 9) {
    case 6: return 0x10;
    case 7: return 0x08 | 0x01;
    case 8: return 0x04 | 0x02;
    default: return 0;
    }
}

// Il canale di un operatore (l'offset nei registri 20-35, 40-55, ...), o -1
// per gli offset che non esistono: ogni canale ne ha due, a tre di distanza.
int operatorChannel(int offset)
{
    const int slot = offset % 8;
    return offset < 22 && slot < 6 ? offset / 8 * 3 + slot % 3 : -1;
}

// Il chip che vede il lettore. Di solito le scritture vanno al chip vero,
// senza le note dei canali zittiti e con i loro operatori al minimo; mentre
// si misurano i sottobrani vanno perse, e si contano le note accese e i
// canali che le suonano.
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

    // I canali che hanno suonato almeno una nota mentre si misurava.
    bool used(int channel) const { return used_[channel]; }
    bool drums(int channel) const { return drums_[channel]; }

    // Le note del canale non partono più, quelle accese si rilasciano e i
    // suoi operatori vanno al volume minimo: alcuni strumenti non si
    // spengono mai da soli. Riacceso, la nota che il brano tiene premuta
    // riparte.
    void setMuted(int channel, bool muted)
    {
        muted_[channel] = muted;
        const int bank = channel / 9;
        chip_.setchip(bank);
        chip_.write(0xb0 + channel % 9, filtered(bank, 0xb0 + channel % 9, keys_[channel]));
        if (drumBits(channel))
            chip_.write(0xbd, filtered(bank, 0xbd, rhythm_[bank]));
        for (int offset = 0; offset < 22; ++offset) {
            if (operatorChannel(offset) == channel % 9)
                chip_.write(0x40 + offset, filtered(bank, 0x40 + offset, levels_[bank][offset]));
        }
        chip_.setchip(currChip);
    }

    void write(int reg, int value) override
    {
        const bool key = (reg & 0xf0) == 0xb0 && (reg & 0x0f) <= 8;
        const int channel = currChip * 9 + (reg & 0x0f);
        if (!measuring_) {
            if (key)
                keys_[channel] = value;
            else if (reg == 0xbd)
                rhythm_[currChip] = value;
            else if (reg >= 0x40 && reg < 0x56)
                levels_[currChip][reg - 0x40] = value;
            chip_.write(reg, filtered(currChip, reg, value));
            return;
        }
        // Key-on (bit 5 di B0-B8), o un colpo di batteria in modo ritmico.
        if (key && (value & 0x20)) {
            ++notes_;
            used_[channel] = true;
        }
        if (reg == 0xbd && (value & 0x20) && (value & 0x1f)) {
            ++notes_;
            for (int c = currChip * 9 + 6; c < currChip * 9 + 9; ++c) {
                if (value & drumBits(c))
                    used_[c] = drums_[c] = true;
            }
        }
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
        if (!measuring_) {
            chip_.init();
            std::fill(std::begin(keys_), std::end(keys_), 0);
            std::fill(std::begin(rhythm_), std::end(rhythm_), 0);
            for (auto& bank : levels_)
                std::fill(std::begin(bank), std::end(bank), 0);
        }
    }

    void update(short* buffer, int frames) override { chip_.update(buffer, frames); }

private:
    // Il valore da scrivere davvero: per un canale zittito senza key-on,
    // senza i suoi colpi di batteria e con gli operatori al minimo (livello
    // totale 63, -47 dB).
    int filtered(int bank, int reg, int value) const
    {
        if ((reg & 0xf0) == 0xb0 && (reg & 0x0f) <= 8 && muted_[bank * 9 + (reg & 0x0f)])
            return value & ~0x20;
        if (reg >= 0x40 && reg < 0x56) {
            const int channel = operatorChannel(reg - 0x40);
            return channel >= 0 && muted_[bank * 9 + channel] ? value | 0x3f : value;
        }
        if (reg == 0xbd) {
            for (int c = bank * 9 + 6; c < bank * 9 + 9; ++c) {
                if (muted_[c])
                    value &= ~drumBits(c);
            }
        }
        return value;
    }

    Copl& chip_;
    bool measuring_ = false;
    int notes_ = 0;
    bool used_[kOplChannels] = {};
    bool drums_[kOplChannels] = {};
    bool muted_[kOplChannels] = {};
    int keys_[kOplChannels] = {}; // l'ultimo valore scritto in B0-B8
    int rhythm_[2] = {};          // e in BD, per banco
    int levels_[2][22] = {};      // e in 40-55, per banco
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
        info_.platform = platform::kPc;
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

    // I canali del chip che suonano note in qualche sottobrano.
    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int channel : channels_)
            names.push_back("Canale " + std::to_string(channel + 1) + (opl_.drums(channel) ? " · batteria" : ""));
        return names;
    }

    void setChannelMuted(int channel, bool muted) override
    {
        opl_.setMuted(channels_.at(static_cast<size_t>(channel)), muted);
    }

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
        // si tengono tutti, e i canali da zittire non si conoscono.
        if (subsongs_.empty())
            subsongs_ = all;
        for (int channel = 0; channel < kOplChannels; ++channel) {
            if (opl_.used(channel))
                channels_.push_back(channel);
        }
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
    std::vector<int> channels_; // i canali del chip che suonano
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
