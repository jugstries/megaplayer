#include "XaDecoder.h"

#include "core/Platform.h"
#include "core/Resampler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// I settori grezzi si riconoscono solo dal contenuto, che hanno anche le
// immagini dei CD: si prendono solo col nome giusto.
constexpr int kRawScore = 60;

// Un settore da 2352 byte: sincronismo (12), intestazione (4), sottointestazione
// (8, ripetuta), 18 gruppi audio da 128 byte. Quelli da 2336 cominciano dalla
// sottointestazione.
constexpr size_t kRawSector = 2352;
constexpr size_t kModeSector = 2336;
constexpr size_t kGroups = 18;
constexpr size_t kGroupSize = 128;
constexpr uint8_t kSubmodeAudio = 0x04;

constexpr std::array<uint8_t, 12> kSync = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

// Dove stanno i settori nel file.
struct Layout {
    size_t start = 0;      // primo settore
    size_t count = 0;      // settori
    size_t size = 0;       // 2352 o 2336
    size_t subheader = 0;  // posizione della sottointestazione nel settore
};

// RIFF/CDXA: i settori interi nel blocco "data".
std::optional<Layout> riffLayout(const std::vector<uint8_t>& data)
{
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "CDXA", 4) != 0)
        return std::nullopt;
    size_t offset = 12;
    while (offset + 8 <= data.size()) {
        const uint32_t size = data[offset + 4] | data[offset + 5] << 8 | data[offset + 6] << 16
                              | static_cast<uint32_t>(data[offset + 7]) << 24;
        if (std::memcmp(data.data() + offset, "data", 4) == 0) {
            const size_t available = std::min<size_t>(size, data.size() - offset - 8);
            return Layout{offset + 8, available / kRawSector, kRawSector, 16};
        }
        offset += 8 + size + (size & 1);
    }
    return std::nullopt;
}

bool isAudioSubheader(const uint8_t* subheader)
{
    // La sottointestazione è ripetuta due volte, e l'audio ha il suo bit.
    return std::memcmp(subheader, subheader + 4, 4) == 0 && (subheader[2] & kSubmodeAudio) != 0;
}

// Settori grezzi: col sincronismo da 2352 byte, senza da 2336. Si guarda che
// i primi settori siano audio.
std::optional<Layout> rawLayout(const std::vector<uint8_t>& data)
{
    const bool synced = data.size() >= kRawSector && std::equal(kSync.begin(), kSync.end(), data.begin());
    const Layout layout = synced ? Layout{0, data.size() / kRawSector, kRawSector, 16}
                                 : Layout{0, data.size() / kModeSector, kModeSector, 0};
    if (layout.count == 0 || data.size() % layout.size != 0)
        return std::nullopt;
    const size_t checked = std::min<size_t>(layout.count, 8);
    for (size_t i = 0; i < checked; ++i) {
        if (!isAudioSubheader(data.data() + i * layout.size + layout.subheader))
            return std::nullopt;
    }
    return layout;
}

bool hasXaExtension(const SourceFile& file)
{
    std::string extension = file.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".xa";
}

// Il decodificatore ADPCM di un canale (sinistro o destro), con i due
// campioni precedenti.
struct Predictor {
    int previous = 0;
    int older = 0;

    // scaled: il campione del blocco già portato a 16 bit e spostato.
    int16_t next(int scaled, int filter)
    {
        static constexpr int kPositive[] = {0, 60, 115, 98};
        static constexpr int kNegative[] = {0, 0, -52, -55};
        int value = scaled + ((previous * kPositive[filter] + older * kNegative[filter] + 32) >> 6);
        value = std::clamp(value, -32768, 32767);
        older = previous;
        previous = value;
        return static_cast<int16_t>(value);
    }
};

class XaDecoder final : public Decoder {
public:
    XaDecoder(const SourceFile& file, const OutputSpec& spec, const Layout& layout)
        : data_(file.data)
        , layout_(layout)
        , sampleRate_(spec.sampleRate)
    {
        // I canali del flusso (numero di file e di canale), nell'ordine in cui
        // compaiono, con i loro settori audio.
        for (size_t i = 0; i < layout_.count; ++i) {
            const uint8_t* subheader = sectorAt(i) + layout_.subheader;
            if (!isAudioSubheader(subheader))
                continue;
            const int key = subheader[0] << 8 | subheader[1];
            auto stream = std::find_if(streams_.begin(), streams_.end(), [&](const Stream& s) { return s.key == key; });
            if (stream == streams_.end()) {
                streams_.push_back({key, subheader[3], {}});
                stream = streams_.end() - 1;
            }
            stream->sectors.push_back(i);
        }
        if (streams_.empty())
            throw DecoderError("nessun settore audio XA");

        info_.backend = "xa";
        info_.platform = platform::kPlayStation;
        info_.format = "XA ADPCM";
        info_.title = file.path.stem().string();
        info_.subsongCount = static_cast<int>(streams_.size());
        selectSubsong(0);
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        stream_ = &streams_.at(static_cast<size_t>(index));
        const uint8_t coding = stream_->coding;
        stereo_ = (coding & 0x03) == 1;
        eightBit_ = ((coding >> 4) & 0x03) == 1;
        nativeRate_ = ((coding >> 2) & 0x03) == 1 ? 18900 : 37800;
        // Ogni gruppo ha 8 blocchi da 28 campioni a 4 bit, o 4 a 8 bit.
        framesPerSector_ = kGroups * (eightBit_ ? 4 : 8) * 28 / (stereo_ ? 2 : 1);
        resampler_ = std::make_unique<Resampler>(nativeRate_, sampleRate_);
        info_.currentSubsong = index;
        info_.player = std::string(nativeRate_ == 37800 ? "37,8" : "18,9") + " kHz " + (stereo_ ? "stereo" : "mono")
                       + (eightBit_ ? ", 8 bit" : "");
        info_.duration = static_cast<double>(stream_->sectors.size() * framesPerSector_) / nativeRate_;
        startAt(0);
    }

    size_t render(float* out, size_t frames) override
    {
        const size_t got = resampler_->render(out, frames, [this](float* buffer, size_t count) {
            return decode(buffer, count);
        });
        rendered_ += got;
        return got;
    }

    bool canSeek() const override { return true; }

    // Si riparte dal settore che contiene il punto, con i predittori azzerati.
    void seek(double seconds) override
    {
        const double clamped = std::clamp(seconds, 0.0, info_.duration.value_or(0));
        startAt(static_cast<size_t>(clamped * nativeRate_));
        rendered_ = static_cast<size_t>(clamped * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

private:
    struct Stream {
        int key;
        uint8_t coding;
        std::vector<size_t> sectors;
    };

    const uint8_t* sectorAt(size_t index) const { return data_.data() + layout_.start + index * layout_.size; }

    void startAt(size_t nativeFrame)
    {
        sector_ = nativeFrame / framesPerSector_;
        pending_.clear();
        pendingStart_ = 0;
        left_ = right_ = Predictor{};
        resampler_->reset();
        rendered_ = 0;
        if (sector_ < stream_->sectors.size()) {
            decodeSector();
            pendingStart_ = (nativeFrame % framesPerSector_) * kChannels;
        }
    }

    // I campioni di un settore in pending_, stereo float.
    void decodeSector()
    {
        pending_.clear();
        pendingStart_ = 0;
        const uint8_t* groups = sectorAt(stream_->sectors[sector_++]) + layout_.subheader + 8;
        const int blocks = eightBit_ ? 4 : 8;
        std::vector<int16_t> mono;
        for (size_t g = 0; g < kGroups; ++g) {
            const uint8_t* group = groups + g * kGroupSize;
            for (int block = 0; block < blocks; ++block) {
                const uint8_t header = group[4 + block];
                int shift = header & 0x0F;
                if (shift > 12)
                    shift = 9; // come fa la console
                const int filter = (header >> 4) & 0x03;
                Predictor& predictor = stereo_ && (block & 1) ? right_ : left_;
                int16_t samples[28];
                for (int i = 0; i < 28; ++i) {
                    int scaled;
                    if (eightBit_) {
                        scaled = (static_cast<int8_t>(group[16 + i * 4 + block]) * 256) >> shift;
                    } else {
                        // Il nibble basso per i blocchi pari, l'alto per i dispari.
                        const uint8_t byte = group[16 + i * 4 + block / 2];
                        const int nibble = static_cast<int8_t>(((block & 1) ? byte : byte << 4) & 0xF0) >> 4;
                        scaled = (nibble * 4096) >> shift;
                    }
                    samples[i] = predictor.next(scaled, filter);
                }
                if (!stereo_) {
                    for (const int16_t sample : samples) {
                        pending_.push_back(sample / 32768.0f);
                        pending_.push_back(sample / 32768.0f);
                    }
                } else if (block & 1) {
                    // Il blocco destro dopo il suo sinistro: si intrecciano.
                    const size_t base = pending_.size() - 28 * kChannels;
                    for (int i = 0; i < 28; ++i)
                        pending_[base + static_cast<size_t>(i) * kChannels + 1] = samples[i] / 32768.0f;
                } else {
                    for (const int16_t sample : samples) {
                        pending_.push_back(sample / 32768.0f);
                        pending_.push_back(0);
                    }
                }
            }
        }
    }

    // Frame alla frequenza dell'XA; 0 a fine flusso.
    size_t decode(float* out, size_t frames)
    {
        size_t done = 0;
        while (done < frames) {
            if (pendingStart_ == pending_.size()) {
                if (sector_ >= stream_->sectors.size())
                    break;
                decodeSector();
            }
            const size_t take = std::min((pending_.size() - pendingStart_) / kChannels, frames - done);
            std::copy_n(pending_.begin() + static_cast<std::ptrdiff_t>(pendingStart_), take * kChannels,
                        out + done * kChannels);
            pendingStart_ += take * kChannels;
            done += take;
        }
        return done;
    }

    std::vector<uint8_t> data_;
    Layout layout_;
    int sampleRate_;
    std::vector<Stream> streams_;
    const Stream* stream_ = nullptr;
    bool stereo_ = false;
    bool eightBit_ = false;
    int nativeRate_ = 37800;
    size_t framesPerSector_ = 0;
    size_t sector_ = 0; // il prossimo da decodificare, fra quelli del canale
    Predictor left_, right_;
    std::vector<float> pending_;
    size_t pendingStart_ = 0;
    std::unique_ptr<Resampler> resampler_;
    size_t rendered_ = 0;
    TrackInfo info_;
};

std::optional<Layout> layoutOf(const SourceFile& file)
{
    if (std::optional<Layout> layout = riffLayout(file.data))
        return layout;
    return rawLayout(file.data);
}

} // namespace

std::string_view XaDecoderFactory::description() const
{
    return "XA ADPCM: l'audio in streaming dei CD della PlayStation";
}

int XaDecoderFactory::probe(const SourceFile& file)
{
    if (riffLayout(file.data))
        return kProbeScore;
    return hasXaExtension(file) && rawLayout(file.data) ? kRawScore : 0;
}

std::unique_ptr<Decoder> XaDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const std::optional<Layout> layout = layoutOf(file);
    if (!layout)
        throw DecoderError("non è un flusso XA");
    return std::make_unique<XaDecoder>(file, spec, *layout);
}

} // namespace mp
