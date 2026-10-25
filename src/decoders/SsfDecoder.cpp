#include "SsfDecoder.h"

#include "AudioTags.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <dcsound.h>
#include <satsound.h>
#include <sega.h>
#include <yam.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// SCSP e AICA suonano a 44100 Hz.
constexpr int kNativeRate = 44100;
// Le versioni dei file PSF, e quelle del nucleo.
constexpr uint8_t kSsf = 0x11;
constexpr uint8_t kDsf = 0x12;

void initCore()
{
    static std::once_flag once;
    std::call_once(once, [] {
        if (sega_init() != 0)
            throw DecoderError("Highly Theoretical non parte");
    });
}

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

class SsfDecoder final : public XsfDecoder {
public:
    SsfDecoder(const SourceFile& file, const OutputSpec& spec, uint8_t version)
        : SsfDecoder(spec, version, load(file, version))
    {
    }

private:
    struct Loaded {
        XsfTags tags;
        // Ogni pezzo con i 4 byte del suo indirizzo davanti, come li vuole
        // sega_upload_program.
        std::vector<std::vector<uint8_t>> sections;
    };

    static Loaded load(const SourceFile& file, uint8_t version)
    {
        initCore();
        // 512 KB di RAM sonora sul Saturn, 8 MB sul Dreamcast.
        const uint32_t memory = version == kSsf ? 0x80000 : 0x800000;
        Loaded loaded;
        loadXsf(
            file, version,
            [&](const uint8_t* exe, size_t size, const uint8_t*, size_t) {
                if (size < 5)
                    return false;
                const uint32_t start = le32(exe) & (memory - 1);
                const size_t length = std::min<size_t>(size - 4, memory - start);
                std::vector<uint8_t> section(exe, exe + 4 + length);
                section[0] = static_cast<uint8_t>(start);
                section[1] = static_cast<uint8_t>(start >> 8);
                section[2] = static_cast<uint8_t>(start >> 16);
                section[3] = static_cast<uint8_t>(start >> 24);
                loaded.sections.push_back(std::move(section));
                return true;
            },
            loaded.tags);
        return loaded;
    }

    SsfDecoder(const OutputSpec& spec, uint8_t version, Loaded loaded)
        : XsfDecoder(spec, kNativeRate, loaded.tags)
        , version_(version == kSsf ? 1 : 2)
        , loaded_(std::move(loaded))
        , state_(new uint8_t[sega_get_state_size(version_)])
    {
        const bool saturn = version_ == 1;
        const std::string format = saturn ? "SSF" : "DSF";
        info_.backend = "ssf";
        info_.platform = saturn ? platform::kSaturn : platform::kDreamcast;
        info_.format = loaded_.tags.hasLibrary ? "Mini" + format : format;
        info_.player = "Highly Theoretical";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        // Gli slot dello SCSP, i canali dell'AICA.
        const int channels = saturn ? 32 : 64;
        for (int channel = 1; channel <= channels; ++channel)
            channelNames_.push_back("Canale " + std::to_string(channel));
        start();
    }

    void reset() override
    {
        sega_clear_state(state_.get(), version_);
        // Come foo_ssf: il suono diretto e gli effetti del DSP, senza il
        // ricompilatore (solo x86 a 32 bit).
        sega_enable_dry(state_.get(), 1);
        sega_enable_dsp(state_.get(), 1);
        sega_enable_dsp_dynarec(state_.get(), 0);
        for (std::vector<uint8_t>& section : loaded_.sections)
            sega_upload_program(state_.get(), section.data(), static_cast<uint32>(section.size()));
        applyMutes();
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        size_t done = 0;
        while (done < frames) {
            uint32 got = static_cast<uint32>(frames - done);
            const sint32 result = sega_execute(state_.get(), 0x7FFFFFFF, buffer + done * kChannels, &got);
            done += got;
            if (result < 0)
                break;
        }
        return done;
    }

    void applyMutes() override
    {
        void* yam = version_ == 1 ? satsound_get_yam_state(sega_get_satsound_state(state_.get()))
                                  : dcsound_get_yam_state(sega_get_dcsound_state(state_.get()));
        yam_set_mute_mask(yam, mutedMask());
    }

    uint8_t version_; // 1 Saturn, 2 Dreamcast
    Loaded loaded_;
    std::unique_ptr<uint8_t[]> state_;
};

} // namespace

std::string_view SsfDecoderFactory::description() const
{
    return "Highly Theoretical: Saturn (SSF) e Dreamcast (DSF)";
}

int SsfDecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, kSsf) || isXsf(file, kDsf) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> SsfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<SsfDecoder>(file, spec, file.data.size() > 3 ? file.data[3] : 0);
}

} // namespace mp
