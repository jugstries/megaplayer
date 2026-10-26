#include "PsfDecoder.h"

#include "AudioTags.h"
#include "HighlyExperimental.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <iop.h>
#include <psx.h>
#include <r3000.h>
#include <spu.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// Lo SPU della PlayStation suona a 44100 Hz.
constexpr int kNativeRate = 44100;
constexpr int kVoices = 24;

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// Un pezzo del programma: un PS-X EXE, con 2 KB di intestazione.
struct Section {
    uint32_t address;
    std::vector<uint8_t> data;
};

class PsfDecoder final : public XsfDecoder {
public:
    PsfDecoder(const SourceFile& file, const OutputSpec& spec)
        : PsfDecoder(file, spec, load(file))
    {
    }

private:
    struct Loaded {
        XsfTags tags;
        std::vector<Section> sections;
        uint32_t pc = 0;
        uint32_t sp = 0;
        uint32_t refresh = 0; // 50 o 60 Hz; 0 se non si sa
    };

    static Loaded load(const SourceFile& file)
    {
        initHighlyExperimental();
        Loaded loaded;
        loadXsf(
            file, 1,
            [&](const uint8_t* exe, size_t size, const uint8_t*, size_t) {
                if (size < 0x800 || std::memcmp(exe, "PS-X EXE", 8) != 0)
                    return false;
                const uint32_t address = le32(exe + 0x18) & 0x1FFFFF;
                const size_t length = size - 0x800;
                if (address < 0x10000 || length > 0x1F0000 || address + length > 0x200000)
                    return false;
                // PC e stack del primo pezzo, cioè della libreria se c'è.
                if (loaded.sections.empty()) {
                    loaded.pc = le32(exe + 0x10);
                    loaded.sp = le32(exe + 0x30);
                }
                // La regione, scritta nell'intestazione come sulle console.
                const std::string_view region(reinterpret_cast<const char*>(exe) + 0x4C, 0x800 - 0x4C);
                if (loaded.refresh == 0) {
                    if (region.find("Europe") != std::string_view::npos)
                        loaded.refresh = 50;
                    else if (region.find("North America") != std::string_view::npos
                             || region.find("Japan") != std::string_view::npos)
                        loaded.refresh = 60;
                }
                loaded.sections.push_back({address, std::vector<uint8_t>(exe + 0x800, exe + size)});
                return true;
            },
            loaded.tags);
        if (const std::string* refresh = loaded.tags.find("_refresh")) {
            const int value = std::atoi(refresh->c_str());
            if (value == 50 || value == 60)
                loaded.refresh = static_cast<uint32_t>(value);
        }
        return loaded;
    }

    PsfDecoder(const SourceFile&, const OutputSpec& spec, Loaded loaded)
        : XsfDecoder(spec, kNativeRate, loaded.tags)
        , loaded_(std::move(loaded))
        , state_(new uint8_t[psx_get_state_size(1)])
    {
        info_.backend = "psf";
        info_.platform = platform::kPlayStation;
        info_.format = loaded_.tags.hasLibrary ? "MiniPSF" : "PSF";
        info_.player = "Highly Experimental";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        for (int voice = 1; voice <= kVoices; ++voice)
            channelNames_.push_back("Voce " + std::to_string(voice));
        start();
    }

    // Lo stato appena avviata la BIOS, con il programma del brano in RAM.
    void reset() override
    {
        psx_clear_state(state_.get(), 1);
        void* iop = psx_get_iop_state(state_.get());
        for (const Section& section : loaded_.sections)
            iop_upload_to_ram(iop, section.address, section.data.data(), static_cast<uint32>(section.data.size()));
        r3000_setreg(iop_get_r3000_state(iop), R3000_REG_PC, loaded_.pc);
        r3000_setreg(iop_get_r3000_state(iop), R3000_REG_GEN + 29, loaded_.sp);
        if (loaded_.refresh)
            psx_set_refresh(state_.get(), loaded_.refresh);
        // Come foo_psf: la temporizzazione più fedele.
        iop_set_compat(iop, IOP_COMPAT_HARSH);
        applyMutes();
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        size_t done = 0;
        while (done < frames) {
            uint32 got = static_cast<uint32>(frames - done);
            const sint32 result = psx_execute(state_.get(), 0x7FFFFFFF, buffer + done * kChannels, &got, 0);
            done += got;
            if (result < 0)
                break;
        }
        return done;
    }

    void applyMutes() override
    {
        void* spu = iop_get_spu_state(psx_get_iop_state(state_.get()));
        for (int voice = 0; voice < kVoices; ++voice)
            spu_enable_mute(spu, static_cast<uint8>(voice), (mutedMask() >> voice) & 1);
    }

    Loaded loaded_;
    std::unique_ptr<uint8_t[]> state_;
};

} // namespace

std::string_view PsfDecoderFactory::description() const
{
    return "Highly Experimental: PlayStation (PSF, MiniPSF) con OpenBIOS";
}

int PsfDecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, 1) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> PsfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<PsfDecoder>(file, spec);
}

} // namespace mp
