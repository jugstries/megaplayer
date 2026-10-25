#include "GsfDecoder.h"

#include "AudioTags.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <gba/GBA.h>
#include <gba/Sound.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
constexpr uint8_t kGsf = 0x22;
// Quanti cicli emulare per volta: circa 15 ms del GBA.
constexpr int kCycles = 250000;
// Un brano che smette di produrre campioni si è piantato.
constexpr int kMaxIdleLoops = 1000;

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// I canali dell'APU e i bit della maschera di viogsf (che dice quelli accesi).
struct Channel {
    const char* name;
    int bit;
};
constexpr Channel kChannelBits[] = {{"Onda quadra 1", 0x001}, {"Onda quadra 2", 0x002}, {"Onda", 0x004},
                                    {"Rumore", 0x008},        {"PCM A", 0x100},         {"PCM B", 0x200}};

// Raccoglie i campioni che viogsf scrive quando ha riempito il suo buffer.
struct Output final : GBASoundOut {
    std::vector<int16_t> samples; // stereo
    size_t start = 0;             // i campioni già consegnati

    void write(const void* data, unsigned long bytes) override
    {
        if (start > 0 && start == samples.size()) {
            samples.clear();
            start = 0;
        }
        const auto* first = static_cast<const int16_t*>(data);
        samples.insert(samples.end(), first, first + bytes / sizeof(int16_t));
    }

    size_t frames() const { return (samples.size() - start) / 2; }
};

class GsfDecoder final : public XsfDecoder {
public:
    GsfDecoder(const SourceFile& file, const OutputSpec& spec)
        : GsfDecoder(spec, load(file))
    {
    }

    ~GsfDecoder() override { shutdown(); }

    GsfDecoder(const GsfDecoder&) = delete;
    GsfDecoder& operator=(const GsfDecoder&) = delete;

private:
    struct Loaded {
        XsfTags tags;
        std::vector<uint8_t> rom;
        uint32_t entry = 0;
        bool entrySet = false;
    };

    // Ogni pezzo: il punto d'ingresso, l'indirizzo (in ROM, o in RAM per il
    // multiboot) e la lunghezza, poi i dati.
    static Loaded load(const SourceFile& file)
    {
        Loaded loaded;
        loadXsf(
            file, kGsf,
            [&](const uint8_t* exe, size_t size, const uint8_t*, size_t) {
                if (size < 12)
                    return false;
                const uint32_t entry = le32(exe);
                const uint32_t offset = le32(exe + 4) & 0x1FFFFFF;
                const uint32_t length = le32(exe + 8);
                if (length > size - 12)
                    return false;
                if (!loaded.entrySet) {
                    loaded.entry = entry;
                    loaded.entrySet = true;
                }
                if (loaded.rom.size() < size_t{offset} + length)
                    loaded.rom.resize(size_t{offset} + length);
                std::memcpy(loaded.rom.data() + offset, exe + 12, length);
                return true;
            },
            loaded.tags);
        if (loaded.rom.empty())
            throw DecoderError("GSF senza programma");
        return loaded;
    }

    GsfDecoder(const OutputSpec& spec, Loaded loaded)
        // Blip_Buffer porta già il suono alla frequenza d'uscita.
        : XsfDecoder(spec, spec.sampleRate, loaded.tags)
        , loaded_(std::move(loaded))
        , sampleRate_(spec.sampleRate)
    {
        info_.backend = "gsf";
        info_.platform = platform::kGameBoyAdvance;
        info_.format = loaded_.tags.hasLibrary ? "MiniGSF" : "GSF";
        info_.player = "viogsf";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        for (const Channel& channel : kChannelBits)
            channelNames_.emplace_back(channel.name);
        start();
    }

    void shutdown()
    {
        if (!system_)
            return;
        CPUCleanUp(system_.get());
        soundShutdown(system_.get());
        system_.reset();
    }

    // Il GBA da capo, come lo prepara foo_gsf.
    void reset() override
    {
        shutdown();
        output_ = Output{};
        system_ = std::make_unique<GBASystem>();
        // Un punto d'ingresso in RAM (0x02...) vuol dire multiboot.
        system_->cpuIsMultiBoot = (loaded_.entry >> 24) == 2;
        system_->soundSampleRate = sampleRate_;
        CPULoadRom(system_.get(), loaded_.rom.data(), static_cast<u32>(loaded_.rom.size()));
        soundInit(system_.get(), &output_);
        soundReset(system_.get());
        CPUInit(system_.get());
        CPUReset(system_.get());
        applyMutes();
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        for (int idle = 0; output_.frames() < frames && idle < kMaxIdleLoops; ++idle) {
            const size_t before = output_.frames();
            CPULoop(system_.get(), kCycles);
            if (output_.frames() > before)
                idle = 0;
        }
        const size_t done = std::min(frames, output_.frames());
        std::copy_n(output_.samples.begin() + static_cast<std::ptrdiff_t>(output_.start), done * 2, buffer);
        output_.start += done * 2;
        return done;
    }

    void applyMutes() override
    {
        int enabled = 0;
        for (size_t i = 0; i < std::size(kChannelBits); ++i) {
            if (!((mutedMask() >> i) & 1))
                enabled |= kChannelBits[i].bit;
        }
        soundSetEnable(system_.get(), enabled);
    }

    Loaded loaded_;
    int sampleRate_;
    Output output_;
    std::unique_ptr<GBASystem> system_;
};

} // namespace

std::string_view GsfDecoderFactory::description() const
{
    return "viogsf: Game Boy Advance (GSF)";
}

int GsfDecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, kGsf) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> GsfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<GsfDecoder>(file, spec);
}

} // namespace mp
