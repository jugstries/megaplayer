#include "TwosfDecoder.h"

#include "AudioTags.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <state.h>
#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
constexpr uint8_t k2sf = 0x24;
// vio2sf rende a 44100 Hz.
constexpr int kNativeRate = 44100;
constexpr int kVoices = 16;
// SPUInterpolation_Sinc di SPU.h, che è C++ di DeSmuME e qui non serve.
constexpr unsigned long kSincInterpolation = 4;
// "SAVE": lo stato salvato del DS, compresso, nell'area riservata.
constexpr uint32_t kSaveTag = 0x45564153;

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// Un pezzo: posizione e lunghezza, poi i dati, da mettere in image. La ROM
// del DS va tenuta a una potenza di due.
bool mapSection(std::vector<uint8_t>& image, const uint8_t* data, size_t size, bool rom)
{
    if (size < 8)
        return false;
    const uint32_t offset = le32(data);
    const uint32_t length = le32(data + 4);
    if (length > size - 8 || uint64_t{offset} + length > 0x10000000)
        return false;
    size_t end = size_t{offset} + length;
    if (rom) {
        size_t power = 1;
        while (power < end)
            power <<= 1;
        end = power;
    }
    if (image.size() < end)
        image.resize(end);
    std::memcpy(image.data() + offset, data + 8, length);
    return true;
}

std::vector<uint8_t> inflate(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        throw DecoderError("zlib non parte");
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    int result = Z_OK;
    while (result == Z_OK) {
        out.resize(out.size() + 65536);
        stream.next_out = out.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
        result = ::inflate(&stream, Z_NO_FLUSH);
    }
    out.resize(stream.total_out);
    inflateEnd(&stream);
    if (result != Z_STREAM_END)
        throw DecoderError("stato salvato del 2SF danneggiato");
    return out;
}

int intTag(const XsfTags& tags, const char* name, int fallback)
{
    const std::string* value = tags.find(name);
    return value ? std::atoi(value->c_str()) : fallback;
}

class TwosfDecoder final : public XsfDecoder {
public:
    TwosfDecoder(const SourceFile& file, const OutputSpec& spec)
        : TwosfDecoder(spec, load(file))
    {
    }

    ~TwosfDecoder() override
    {
        if (started_)
            state_deinit(&state_);
    }

    TwosfDecoder(const TwosfDecoder&) = delete;
    TwosfDecoder& operator=(const TwosfDecoder&) = delete;

private:
    struct Loaded {
        XsfTags tags;
        std::vector<uint8_t> rom;
        std::vector<uint8_t> save;
    };

    static Loaded load(const SourceFile& file)
    {
        Loaded loaded;
        loadXsf(
            file, k2sf,
            [&](const uint8_t* exe, size_t size, const uint8_t* reserved, size_t reservedSize) {
                if (size >= 8 && !mapSection(loaded.rom, exe, size, true))
                    return false;
                for (size_t position = 0; position + 12 <= reservedSize;) {
                    const uint32_t tag = le32(reserved + position);
                    const uint32_t length = le32(reserved + position + 4);
                    if (length > reservedSize - position - 12)
                        return false;
                    if (tag == kSaveTag) {
                        const std::vector<uint8_t> save = inflate(reserved + position + 12, length);
                        if (!mapSection(loaded.save, save.data(), save.size(), false))
                            return false;
                    }
                    position += 12 + length;
                }
                return true;
            },
            loaded.tags);
        if (loaded.rom.empty())
            throw DecoderError("2SF senza la ROM");
        return loaded;
    }

    TwosfDecoder(const OutputSpec& spec, Loaded loaded)
        : XsfDecoder(spec, kNativeRate, loaded.tags)
        , loaded_(std::move(loaded))
    {
        info_.backend = "2sf";
        info_.platform = platform::kNintendoDs;
        info_.format = loaded_.tags.hasLibrary ? "Mini2SF" : "2SF";
        info_.player = "vio2sf";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        for (int voice = 1; voice <= kVoices; ++voice)
            channelNames_.push_back("Canale " + std::to_string(voice));
        start();
    }

    // Il DS da capo, configurato dai tag come fa foo_2sf.
    void reset() override
    {
        if (started_)
            state_deinit(&state_);
        if (state_init(&state_) != 0)
            throw DecoderError("vio2sf non parte");
        started_ = true;
        state_.dwInterpolation = kSincInterpolation;
        state_.initial_frames = intTag(loaded_.tags, "_frames", -1);
        state_.sync_type = intTag(loaded_.tags, "_vio2sf_sync_type", intTag(loaded_.tags, "_2sf_sync_type", 0));
        state_.arm9_clockdown_level = intTag(loaded_.tags, "_vio2sf_arm9_clockdown_level", 0);
        state_.arm7_clockdown_level = intTag(loaded_.tags, "_vio2sf_arm7_clockdown_level", 0);
        applyMutes();
        state_setrom(&state_, loaded_.rom.data(), static_cast<u32>(loaded_.rom.size()), 0);
        state_loadstate(&state_, loaded_.save.empty() ? nullptr : loaded_.save.data(),
                        static_cast<u32>(loaded_.save.size()));
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        state_render(&state_, buffer, static_cast<unsigned int>(frames));
        return frames;
    }

    void applyMutes() override { state_.dwChannelMute = static_cast<unsigned long>(mutedMask() & 0xFFFF); }

    Loaded loaded_;
    NDS_state state_{};
    bool started_ = false;
};

} // namespace

std::string_view TwosfDecoderFactory::description() const
{
    return "vio2sf: Nintendo DS (2SF)";
}

int TwosfDecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, k2sf) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> TwosfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<TwosfDecoder>(file, spec);
}

} // namespace mp
