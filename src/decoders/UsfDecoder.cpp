#include "UsfDecoder.h"

#include "AudioTags.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <usf/usf.h>

#include <memory>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
constexpr uint8_t kUsf = 0x21;

class UsfDecoder final : public XsfDecoder {
public:
    UsfDecoder(const SourceFile& file, const OutputSpec& spec)
        : UsfDecoder(spec, load(file))
    {
    }

    ~UsfDecoder() override
    {
        if (rendered_)
            usf_shutdown(state_.get());
    }

    UsfDecoder(const UsfDecoder&) = delete;
    UsfDecoder& operator=(const UsfDecoder&) = delete;

private:
    // La ROM e lo stato salvato stanno nell'area riservata di ogni file, da
    // dare a lazyusf2 nell'ordine di psflib.
    struct Loaded {
        XsfTags tags;
        std::vector<std::vector<uint8_t>> sections;
    };

    static Loaded load(const SourceFile& file)
    {
        Loaded loaded;
        loadXsf(
            file, kUsf,
            [&](const uint8_t*, size_t size, const uint8_t* reserved, size_t reservedSize) {
                if (size > 0)
                    return false;
                loaded.sections.emplace_back(reserved, reserved + reservedSize);
                return true;
            },
            loaded.tags);
        return loaded;
    }

    UsfDecoder(const OutputSpec& spec, Loaded loaded)
        // lazyusf2 ricampiona da sé: la frequenza del gioco cambia da brano a
        // brano, a volte anche durante.
        : XsfDecoder(spec, spec.sampleRate, loaded.tags)
        , loaded_(std::move(loaded))
        , sampleRate_(spec.sampleRate)
        , state_(new uint8_t[usf_get_state_size()])
    {
        usf_clear(state_.get());
        // I tag dicono se il brano vuole queste due stranezze dell'hardware.
        usf_set_compare(state_.get(), loaded_.tags.find("_enablecompare") != nullptr);
        usf_set_fifo_full(state_.get(), loaded_.tags.find("_enablefifofull") != nullptr);
        for (const std::vector<uint8_t>& section : loaded_.sections) {
            if (usf_upload_section(state_.get(), section.data(), section.size()) < 0)
                throw DecoderError("USF non valido");
        }
        // Un primo blocco subito: un brano che non parte si scarta qui.
        int32_t rate = 0;
        rendered_ = true;
        if (const char* error = usf_render(state_.get(), nullptr, 0, &rate)) {
            usf_shutdown(state_.get());
            rendered_ = false;
            throw DecoderError(std::string("USF non valido: ") + error);
        }
        info_.backend = "usf";
        info_.platform = platform::kNintendo64;
        info_.format = loaded_.tags.hasLibrary ? "MiniUSF" : "USF";
        info_.player = "lazyusf2";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        start();
    }

    // Da capo: ROM e stato salvato ricaricati.
    void reset() override
    {
        if (rendered_)
            usf_restart(state_.get());
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        rendered_ = true;
        if (usf_render_resampled(state_.get(), buffer, frames, sampleRate_) != nullptr)
            return 0;
        return frames;
    }

    Loaded loaded_;
    int sampleRate_;
    std::unique_ptr<uint8_t[]> state_;
    bool rendered_ = false; // usf_restart e usf_shutdown solo dopo un usf_render
};

} // namespace

std::string_view UsfDecoderFactory::description() const
{
    return "lazyusf2: Nintendo 64 (USF)";
}

int UsfDecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, kUsf) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> UsfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<UsfDecoder>(file, spec);
}

} // namespace mp
