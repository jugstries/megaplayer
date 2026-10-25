#include "QsfDecoder.h"

#include "AudioTags.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <qsound.h>
// Senza extern "C" suo.
extern "C" {
#include <qsound_ctr.h>
}

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
constexpr uint8_t kQsf = 0x41;
// La frequenza del QSound, quella che Highly Quixotic usa di serie.
constexpr int kNativeRate = 24038;
constexpr int kVoices = 16;

void initCore()
{
    static std::once_flag once;
    std::call_once(once, [] {
        if (qsound_init() != 0)
            throw DecoderError("Highly Quixotic non parte");
    });
}

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint32_t be32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

class QsfDecoder final : public XsfDecoder {
public:
    QsfDecoder(const SourceFile& file, const OutputSpec& spec)
        : QsfDecoder(spec, load(file))
    {
    }

private:
    // Le tre memorie del QSF: la chiave Kabuki (11 byte), la ROM dello Z80 e
    // quella dei campioni.
    struct Loaded {
        XsfTags tags;
        std::vector<uint8_t> key;
        std::vector<uint8_t> z80;
        std::vector<uint8_t> samples;
    };

    // Un pezzo: "KEY", "Z80" o "SMP", la posizione e la lunghezza, i dati.
    static bool addChunks(Loaded& loaded, const uint8_t* data, size_t size)
    {
        while (size >= 11) {
            const std::string name(reinterpret_cast<const char*>(data), 3);
            const uint32_t offset = le32(data + 3);
            const uint32_t length = le32(data + 7);
            data += 11;
            size -= 11;
            if (length > size)
                return false;
            std::vector<uint8_t>* memory = name == "KEY" ? &loaded.key
                                           : name == "Z80" ? &loaded.z80
                                           : name == "SMP" ? &loaded.samples
                                                           : nullptr;
            const uint64_t end = uint64_t{offset} + length;
            if (!memory || (memory == &loaded.key && end > 11) || end > 0x40000000)
                return false;
            if (memory->size() < end)
                memory->resize(static_cast<size_t>(end));
            std::memcpy(memory->data() + offset, data, length);
            data += length;
            size -= length;
        }
        return true;
    }

    static Loaded load(const SourceFile& file)
    {
        initCore();
        Loaded loaded;
        loadXsf(
            file, kQsf,
            [&](const uint8_t* exe, size_t size, const uint8_t*, size_t) { return addChunks(loaded, exe, size); },
            loaded.tags);
        if (loaded.z80.empty())
            throw DecoderError("QSF senza il programma dello Z80");
        return loaded;
    }

    QsfDecoder(const OutputSpec& spec, Loaded loaded)
        : XsfDecoder(spec, kNativeRate, loaded.tags)
        , loaded_(std::move(loaded))
        , state_(new uint8_t[qsound_get_state_size()])
    {
        info_.backend = "qsf";
        info_.platform = platform::kArcade;
        info_.format = loaded_.tags.hasLibrary ? "MiniQSF" : "QSF";
        info_.player = "Highly Quixotic";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        for (int voice = 1; voice <= kVoices; ++voice)
            channelNames_.push_back("Voce " + std::to_string(voice));
        start();
    }

    void reset() override
    {
        qsound_clear_state(state_.get());
        if (loaded_.key.size() == 11) {
            const uint8_t* key = loaded_.key.data();
            qsound_set_kabuki_key(state_.get(), be32(key), be32(key + 4), static_cast<uint16>(key[8] << 8 | key[9]),
                                  key[10]);
        } else {
            qsound_set_kabuki_key(state_.get(), 0, 0, 0, 0);
        }
        qsound_set_z80_rom(state_.get(), loaded_.z80.data(), static_cast<uint32>(loaded_.z80.size()));
        qsound_set_sample_rom(state_.get(), loaded_.samples.data(), static_cast<uint32>(loaded_.samples.size()));
        applyMutes();
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        size_t done = 0;
        while (done < frames) {
            uint32 got = static_cast<uint32>(frames - done);
            const sint32 result = qsound_execute(state_.get(), 0x7FFFFFFF, buffer + done * kChannels, &got);
            done += got;
            if (result < 0)
                break;
        }
        return done;
    }

    // Il QSound è quello di superctr (qsound_ctr.c), che Highly Quixotic
    // chiama ancora qmix.
    void applyMutes() override
    {
        qsoundc_set_mute_mask(qsound_get_qmix_state(state_.get()), static_cast<UINT32>(mutedMask() & 0xFFFF));
    }

    Loaded loaded_;
    std::unique_ptr<uint8_t[]> state_;
};

} // namespace

std::string_view QsfDecoderFactory::description() const
{
    return "Highly Quixotic: QSound delle schede arcade Capcom (QSF)";
}

int QsfDecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, kQsf) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> QsfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<QsfDecoder>(file, spec);
}

} // namespace mp
