#include "S98Decoder.h"

#include "FmplayerDecoder.h"
#include "FmplayerGlue.h"
#include "core/Platform.h"
#include "core/Resampler.h"
#include "core/ShiftJis.h"

#ifdef MP_WITH_MDX
#include "MdxDecoder.h"
extern "C" {
#include <ym2151.h>
}
#endif
#ifdef MP_WITH_GME
#include "Ym2612_Nuked.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// Come per gli stream dei giochi: due giri, poi dieci secondi di dissolvenza.
constexpr int kLoops = 2;
constexpr double kFadeSeconds = 10;
constexpr double kMaxSeconds = 30 * 60;
constexpr size_t kMaxDevices = 64;

// I chip del formato (il numero è quello del file).
enum DeviceType : uint32_t {
    kPsgYm = 1, // YM2149
    kOpn = 2,   // YM2203
    kOpn2 = 3,  // YM2612
    kOpna = 4,  // YM2608
    kOpm = 5,   // YM2151
    kOpll = 6,  // YM2413
    kOpl = 7,   // YM3526
    kOpl2 = 8,  // YM3812
    kOpl3 = 9,  // YMF262
    kPsgAy = 15, // AY-3-8910
    kDcsg = 16, // SN76489
};

const char* chipName(uint32_t type)
{
    switch (type) {
    case kPsgYm: return "YM2149";
    case kOpn: return "YM2203";
    case kOpn2: return "YM2612";
    case kOpna: return "YM2608";
    case kOpm: return "YM2151";
    case kOpll: return "YM2413";
    case kOpl: return "YM3526";
    case kOpl2: return "YM3812";
    case kOpl3: return "YMF262";
    case kPsgAy: return "AY-3-8910";
    case kDcsg: return "SN76489";
    default: return "sconosciuto";
    }
}

uint32_t le32(const std::vector<uint8_t>& data, size_t at)
{
    return data[at] | data[at + 1] << 8 | data[at + 2] << 16 | static_cast<uint32_t>(data[at + 3]) << 24;
}

struct Device {
    uint32_t type;
    uint32_t clock;
};

struct Event {
    uint64_t tick;
    uint8_t device;
    uint8_t port;
    uint8_t reg;
    uint8_t data;
};

// Il file letto: chip, tick, tag e le scritture in ordine, coi giri del loop
// già srotolati fino alla fine della dissolvenza.
struct Log {
    int version = 0;
    double tickSeconds = 0.01;
    std::vector<Device> devices;
    std::vector<Event> events;
    bool looped = false;
    double fadeStart = 0;
    double duration = 0;
    std::string title;
    std::string author;
};

bool validHeader(const std::vector<uint8_t>& data)
{
    return data.size() >= 0x20 && std::memcmp(data.data(), "S98", 3) == 0 && data[3] >= '0' && data[3] <= '3'
        && le32(data, 0x14) < data.size() && le32(data, 0x10) < data.size();
}

// I tag della versione 3: "[S98]", forse il BOM dell'UTF-8, poi righe
// "chiave=valore" come nei PSF; prima, al loro posto, solo il titolo.
void readTags(const std::vector<uint8_t>& data, Log& log)
{
    const size_t start = le32(data, 0x10);
    if (start == 0 || start >= data.size())
        return;
    const auto* begin = reinterpret_cast<const char*>(data.data()) + start;
    const char* end = static_cast<const char*>(std::memchr(begin, 0, data.size() - start));
    std::string text(begin, end ? end : reinterpret_cast<const char*>(data.data()) + data.size());
    if (log.version < 3) {
        log.title = shiftJisToUtf8(text);
        return;
    }
    if (text.rfind("[S98]", 0) != 0)
        return;
    text.erase(0, 5);
    const bool utf8 = text.rfind("\xEF\xBB\xBF", 0) == 0;
    if (utf8)
        text.erase(0, 3);
    size_t at = 0;
    while (at < text.size()) {
        size_t line = text.find('\n', at);
        if (line == std::string::npos)
            line = text.size();
        const std::string entry = text.substr(at, line - at);
        at = line + 1;
        const size_t equal = entry.find('=');
        if (equal == std::string::npos)
            continue;
        std::string key = entry.substr(0, equal);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        std::string value = entry.substr(equal + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
            value.pop_back();
        value = utf8 ? value : shiftJisToUtf8(value);
        if (key == "title")
            log.title = value;
        else if (key == "artist")
            log.author = value;
    }
}

std::optional<Log> readLog(const std::vector<uint8_t>& data)
{
    if (!validHeader(data))
        return std::nullopt;
    Log log;
    log.version = data[3] - '0';
    uint32_t multiplier = le32(data, 0x04);
    uint32_t divisor = le32(data, 0x08);
    if (log.version < 2) {
        divisor = 0;
        if (log.version == 0)
            multiplier = 0;
    }
    log.tickSeconds = static_cast<double>(multiplier ? multiplier : 10) / (divisor ? divisor : 1000);

    if (log.version == 2) {
        for (size_t at = 0x20; at + 16 <= data.size() && log.devices.size() < kMaxDevices; at += 16) {
            if (le32(data, at) == 0)
                break;
            log.devices.push_back({le32(data, at), le32(data, at + 4)});
        }
    } else if (log.version == 3) {
        const size_t count = std::min<size_t>(le32(data, 0x1C), kMaxDevices);
        for (size_t i = 0; i < count && 0x20 + i * 16 + 16 <= data.size(); ++i)
            log.devices.push_back({le32(data, 0x20 + i * 16), le32(data, 0x24 + i * 16)});
    }
    if (log.devices.empty())
        log.devices.push_back({kOpna, 7987200});
    readTags(data, log);

    const size_t dataStart = le32(data, 0x14);
    size_t loop = le32(data, 0x18);
    if (loop != 0 && (loop < dataStart || loop >= data.size()))
        loop = 0;
    size_t at = dataStart;
    uint64_t tick = 0;
    uint64_t loopStartTick = 0;
    int passes = 0;
    while (at < data.size() && tick * log.tickSeconds < kMaxSeconds) {
        if (at == loop)
            loopStartTick = tick;
        const uint8_t command = data[at++];
        if (command == 0xFF) {
            ++tick;
        } else if (command == 0xFE) {
            uint64_t value = 0;
            int shift = 0;
            uint8_t byte = 0x80;
            while (at < data.size() && (byte & 0x80) && shift < 32) {
                byte = data[at++];
                value |= static_cast<uint64_t>(byte & 0x7F) << shift;
                shift += 7;
            }
            tick += 2 + value;
        } else if (command == 0xFD) {
            // Un loop che non fa passare tempo non si ripete.
            if (loop == 0 || tick == loopStartTick)
                break;
            if (++passes == kLoops) {
                log.looped = true;
                log.fadeStart = tick * log.tickSeconds;
            }
            if (log.looped && tick * log.tickSeconds >= log.fadeStart + kFadeSeconds)
                break;
            at = loop;
        } else if (at + 2 <= data.size()) {
            if ((command >> 1) < log.devices.size())
                log.events.push_back({tick, static_cast<uint8_t>(command >> 1), static_cast<uint8_t>(command & 1),
                                      data[at], data[at + 1]});
            at += 2;
        } else {
            break;
        }
    }
    log.duration = log.looped ? log.fadeStart + kFadeSeconds : tick * log.tickSeconds;
    return log;
}

// Un chip emulato: le scritture ai registri e il suono stereo alla sua
// frequenza.
class Chip {
public:
    virtual ~Chip() = default;
    virtual double rate() const = 0;
    virtual void write(int port, int reg, int data) = 0;
    virtual void render(float* out, size_t frames) = 0;
    virtual std::vector<std::string> channels() const = 0;
    virtual void setMuted(unsigned mask) = 0;
};

// YM2608, YM2203 e i PSG con lo YM2608 di 98fmplayer, che genera un
// campione ogni 144 cicli di 7,9872 MHz: un YM2203 è come uno YM2608 a
// frequenza doppia, un PSG come uno a frequenza quadrupla (MP_FM_RATE
// cresce nella stessa proporzione).
class OpnaChip final : public Chip {
public:
    OpnaChip(uint32_t type, uint32_t clock)
        : type_(type)
    {
        const std::vector<uint8_t>* rom = fmplayer::drumRom();
        chip_ = mp_opna_create(rom ? rom->data() : nullptr);
        if (!chip_)
            throw DecoderError("memoria esaurita");
        const double master = type == kOpna ? clock : type == kOpn ? clock * 2.0 : clock * 4.0;
        rate_ = master / 144;
        // Senza i canali FM 4-6 dello YM2608 il registro 0x29 non serve.
        if (type == kOpna)
            mp_opna_write(chip_, 0x29, 0x80);
    }

    ~OpnaChip() override { mp_opna_free(chip_); }

    double rate() const override { return rate_; }

    void write(int port, int reg, int data) override
    {
        if (type_ != kOpna && port != 0)
            return;
        if ((type_ == kPsgYm || type_ == kPsgAy) && reg > 0x0F)
            return;
        mp_opna_write(chip_, static_cast<unsigned>(port << 8 | reg), static_cast<unsigned>(data));
    }

    void render(float* out, size_t frames) override
    {
        block_.resize(frames * kChannels);
        mp_opna_mix(chip_, block_.data(), static_cast<unsigned>(frames));
        for (size_t i = 0; i < frames * kChannels; ++i)
            out[i] = block_[i] / 32768.0f;
    }

    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        if (type_ == kOpna || type_ == kOpn) {
            for (int i = 1; i <= (type_ == kOpna ? 6 : 3); ++i)
                names.push_back("FM " + std::to_string(i));
        }
        for (int i = 1; i <= 3; ++i)
            names.push_back("SSG " + std::to_string(i));
        if (type_ == kOpna) {
            names.push_back("Ritmo");
            names.push_back("ADPCM");
        }
        return names;
    }

    // Dai canali di channels() ai bit di mp_opna_set_mute (FM 1-6, SSG 1-3,
    // batteria, ADPCM).
    void setMuted(unsigned mask) override
    {
        unsigned bits = 0;
        const std::vector<int> map = type_ == kOpna ? std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
                                     : type_ == kOpn ? std::vector<int>{0, 1, 2, 6, 7, 8}
                                                     : std::vector<int>{6, 7, 8};
        for (size_t i = 0; i < map.size(); ++i) {
            if (mask & (1u << i))
                bits |= 1u << map[i];
        }
        mp_opna_set_mute(chip_, bits);
    }

private:
    uint32_t type_;
    mp_opna_chip* chip_ = nullptr;
    double rate_ = 0;
    std::vector<int16_t> block_;
};

#ifdef MP_WITH_MDX
// YM2151 con l'emulatore di Jarek Burczynski in mdxmini, a un campione ogni
// 64 cicli come il chip. Zittire un canale vuol dire non lasciarlo più
// partire: i key-on (registro 0x08) diventano key-off.
class OpmChip final : public Chip {
public:
    explicit OpmChip(uint32_t clock)
        : rate_(clock / 64.0)
    {
        const std::lock_guard lock(mdx::chipMutex());
        chip_ = YM2151Init(0, static_cast<int>(clock), static_cast<int>(std::lround(rate_)));
        if (!chip_)
            throw DecoderError("memoria esaurita");
        YM2151ResetChip(chip_);
    }

    ~OpmChip() override
    {
        const std::lock_guard lock(mdx::chipMutex());
        YM2151Shutdown(chip_);
    }

    double rate() const override { return std::lround(rate_); }

    void write(int, int reg, int data) override
    {
        if (reg == 0x08 && (muted_ & (1u << (data & 7))))
            data &= 7;
        YM2151WriteReg(chip_, reg, data);
    }

    void render(float* out, size_t frames) override
    {
        left_.resize(frames);
        right_.resize(frames);
        SAMP* buffers[] = {left_.data(), right_.data()};
        YM2151UpdateOne(chip_, buffers, static_cast<int>(frames));
        for (size_t i = 0; i < frames; ++i) {
            out[i * kChannels] = left_[i] / 32768.0f;
            out[i * kChannels + 1] = right_[i] / 32768.0f;
        }
    }

    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int i = 1; i <= 8; ++i)
            names.push_back("FM " + std::to_string(i));
        return names;
    }

    void setMuted(unsigned mask) override
    {
        for (int i = 0; i < 8; ++i) {
            if ((mask & ~muted_) & (1u << i))
                YM2151WriteReg(chip_, 0x08, i); // tace subito
        }
        muted_ = mask;
    }

private:
    double rate_;
    void* chip_ = nullptr;
    unsigned muted_ = 0;
    std::vector<SAMP> left_;
    std::vector<SAMP> right_;
};
#endif

#ifdef MP_WITH_GME
// YM2612 col Nuked OPN2 di game-music-emu, alla sua frequenza (144 cicli).
class Opn2Chip final : public Chip {
public:
    explicit Opn2Chip(uint32_t clock)
        : rate_(clock / 144.0)
    {
        if (emu_.set_rate(rate_, clock))
            throw DecoderError("YM2612 non avviato");
        emu_.reset();
    }

    double rate() const override { return rate_; }

    void write(int port, int reg, int data) override
    {
        if (port == 0)
            emu_.write0(reg, data);
        else
            emu_.write1(reg, data);
    }

    void render(float* out, size_t frames) override
    {
        block_.assign(frames * kChannels, 0);
        emu_.run(static_cast<int>(frames), block_.data());
        for (size_t i = 0; i < frames * kChannels; ++i)
            out[i] = block_[i] / 32768.0f;
    }

    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int i = 1; i <= 6; ++i)
            names.push_back("FM " + std::to_string(i));
        return names;
    }

    void setMuted(unsigned mask) override { emu_.mute_voices(static_cast<int>(mask)); }

private:
    double rate_;
    Ym2612_Nuked_Emu emu_;
    std::vector<short> block_;
};
#endif

std::unique_ptr<Chip> makeChip(const Device& device)
{
    const uint32_t clock = device.clock;
    switch (device.type) {
    case kOpna:
    case kOpn:
    case kPsgYm:
    case kPsgAy:
        return clock ? std::make_unique<OpnaChip>(device.type, clock) : nullptr;
#ifdef MP_WITH_MDX
    case kOpm:
        return clock ? std::make_unique<OpmChip>(clock) : nullptr;
#endif
#ifdef MP_WITH_GME
    case kOpn2:
        return clock ? std::make_unique<Opn2Chip>(clock) : nullptr;
#endif
    default:
        return nullptr;
    }
}

// Un chip con le sue scritture, che suona per conto suo: applica quelle che
// arrivano mentre genera, alla sua frequenza, e passa dal ricampionatore.
struct Voice {
    size_t device = 0;
    std::unique_ptr<Chip> chip;
    std::vector<Event> events;
    std::vector<uint64_t> samples; // il campione di ogni scrittura
    size_t next = 0;
    uint64_t position = 0;
    std::unique_ptr<Resampler> resampler;
    unsigned muted = 0;

    size_t generate(float* out, size_t frames)
    {
        size_t done = 0;
        while (done < frames) {
            while (next < events.size() && samples[next] <= position) {
                chip->write(events[next].port, events[next].reg, events[next].data);
                ++next;
            }
            const uint64_t until = next < events.size() ? samples[next] : position + (frames - done);
            const size_t count = static_cast<size_t>(std::min<uint64_t>(frames - done, until - position));
            chip->render(out + done * kChannels, count);
            position += count;
            done += count;
        }
        return frames;
    }
};

class S98Decoder final : public Decoder {
public:
    S98Decoder(Log log, const OutputSpec& spec)
        : log_(std::move(log))
        , sampleRate_(spec.sampleRate)
    {
        start();
        std::vector<std::string> missing;
        bool opm = false;
        for (size_t i = 0; i < log_.devices.size(); ++i) {
            const char* name = chipName(log_.devices[i].type);
            opm = opm || log_.devices[i].type == kOpm;
            const auto voice = std::find_if(voices_.begin(), voices_.end(), [i](const Voice& v) { return v.device == i; });
            if (voice == voices_.end()) {
                missing.push_back(name);
                continue;
            }
            for (const std::string& channel : voice->chip->channels())
                channels_.push_back(voices_.size() > 1 ? std::string(name) + " " + channel : channel);
        }
        info_.backend = "s98";
        info_.platform = opm ? platform::kX68000 : platform::kPc98;
        info_.format = "S98 v" + std::to_string(log_.version);
        std::string chips;
        for (const Voice& voice : voices_)
            chips += (chips.empty() ? "" : ", ") + std::string(chipName(log_.devices[voice.device].type));
        info_.player = "chip emulati: " + (chips.empty() ? std::string("nessuno") : chips);
        for (size_t i = 0; i < missing.size(); ++i)
            info_.player += (i == 0 ? "; non supportati: " : ", ") + missing[i];
        info_.title = log_.title;
        info_.author = log_.author;
        info_.duration = log_.duration;
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        if (index != 0)
            throw DecoderError("sottobrano " + std::to_string(index + 1) + " non valido");
        seek(0);
    }

    size_t render(float* out, size_t frames) override
    {
        const auto total = static_cast<size_t>(log_.duration * sampleRate_);
        frames = std::min(frames, total > rendered_ ? total - rendered_ : 0);
        std::fill(out, out + frames * kChannels, 0.0f);
        mix_.resize(frames * kChannels);
        for (Voice& voice : voices_) {
            voice.resampler->render(mix_.data(), frames, [&voice](float* buffer, size_t count) {
                return voice.generate(buffer, count);
            });
            for (size_t i = 0; i < frames * kChannels; ++i)
                out[i] += mix_[i];
        }
        if (log_.looped) {
            for (size_t i = 0; i < frames; ++i) {
                const double time = static_cast<double>(rendered_ + i) / sampleRate_;
                if (time > log_.fadeStart) {
                    const auto gain = static_cast<float>(std::max(0.0, 1 - (time - log_.fadeStart) / kFadeSeconds));
                    out[i * kChannels] *= gain;
                    out[i * kChannels + 1] *= gain;
                }
            }
        }
        rendered_ += frames;
        return frames;
    }

    bool canSeek() const override { return true; }

    // Chip nuovi, le scritture fino al punto senza sintetizzare, e da lì si
    // suona: le note che suonano al punto ripartono dalle loro scritture.
    void seek(double seconds) override
    {
        const double target = std::clamp(seconds, 0.0, log_.duration);
        start();
        for (Voice& voice : voices_) {
            const auto position = static_cast<uint64_t>(target * voice.chip->rate());
            while (voice.next < voice.events.size() && voice.samples[voice.next] <= position) {
                const Event& event = voice.events[voice.next++];
                voice.chip->write(event.port, event.reg, event.data);
            }
            voice.position = position;
        }
        rendered_ = static_cast<size_t>(target * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    std::vector<std::string> channels() const override { return channels_; }

    void setChannelMuted(int channel, bool muted) override
    {
        for (Voice& voice : voices_) {
            const int count = static_cast<int>(voice.chip->channels().size());
            if (channel < count) {
                if (muted)
                    voice.muted |= 1u << channel;
                else
                    voice.muted &= ~(1u << channel);
                voice.chip->setMuted(voice.muted);
                return;
            }
            channel -= count;
        }
    }

private:
    // I chip da capo, con le scritture di ciascuno.
    void start()
    {
        std::vector<unsigned> muted;
        for (const Voice& voice : voices_)
            muted.push_back(voice.muted);
        voices_.clear();
        for (size_t i = 0; i < log_.devices.size(); ++i) {
            std::unique_ptr<Chip> chip = makeChip(log_.devices[i]);
            if (!chip)
                continue;
            Voice voice;
            voice.device = i;
            voice.chip = std::move(chip);
            for (const Event& event : log_.events) {
                if (event.device == i) {
                    voice.events.push_back(event);
                    voice.samples.push_back(
                        static_cast<uint64_t>(std::llround(event.tick * log_.tickSeconds * voice.chip->rate())));
                }
            }
            voice.resampler = std::make_unique<Resampler>(static_cast<int>(std::lround(voice.chip->rate())), sampleRate_);
            if (voices_.size() < muted.size()) {
                voice.muted = muted[voices_.size()];
                voice.chip->setMuted(voice.muted);
            }
            voices_.push_back(std::move(voice));
        }
        rendered_ = 0;
    }

    Log log_;
    int sampleRate_;
    std::vector<Voice> voices_;
    std::vector<std::string> channels_;
    std::vector<float> mix_;
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view S98DecoderFactory::description() const
{
    return "S98: log dei chip dei computer giapponesi (YM2608, YM2203, YM2151, YM2612, PSG)";
}

int S98DecoderFactory::probe(const SourceFile& file)
{
    return validHeader(file.data) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> S98DecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    std::optional<Log> log = readLog(file.data);
    if (!log)
        throw DecoderError("non è un S98");
    return std::make_unique<S98Decoder>(std::move(*log), spec);
}

} // namespace mp
