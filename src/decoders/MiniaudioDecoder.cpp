#include "MiniaudioDecoder.h"

#include "AudioTags.h"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string_view>

namespace mp {

namespace {

// Sopra openmpt e UADE: un file audio non è mai un modulo.
constexpr int kProbeScore = 90;

enum class Encoding { None, Wav, Flac, Mp3, Vorbis };

bool startsWith(const std::vector<uint8_t>& data, size_t offset, std::string_view magic)
{
    return offset + magic.size() <= data.size()
        && std::string_view(reinterpret_cast<const char*>(&data[offset]), magic.size()) == magic;
}

// Lunghezza del frame MPEG audio (layer I, II e III) che inizia a `offset`,
// 0 se lì non c'è un'intestazione valida.
size_t mpegFrameLength(const std::vector<uint8_t>& data, size_t offset)
{
    if (offset + 4 > data.size() || data[offset] != 0xff || (data[offset + 1] & 0xe0) != 0xe0)
        return 0;
    const int version = (data[offset + 1] >> 3) & 3; // 0 = 2.5, 2 = 2, 3 = 1
    const int layer = 4 - ((data[offset + 1] >> 1) & 3); // 1, 2, 3; 4 non valido
    const int bitrateIndex = data[offset + 2] >> 4;
    const int rateIndex = (data[offset + 2] >> 2) & 3;
    const int padding = (data[offset + 2] >> 1) & 1;
    if (version == 1 || layer == 4 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3)
        return 0;
    static constexpr std::array<std::array<int, 15>, 5> kBitrates = {{
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448}, // MPEG-1 layer I
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},    // MPEG-1 layer II
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},     // MPEG-1 layer III
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},    // MPEG-2/2.5 layer I
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},         // MPEG-2/2.5 layer II e III
    }};
    static constexpr std::array<int, 3> kRates = {44100, 48000, 32000};
    const bool mpeg1 = version == 3;
    const int table = mpeg1 ? layer - 1 : (layer == 1 ? 3 : 4);
    const int bitrate = kBitrates[table][bitrateIndex] * 1000;
    const int rate = kRates[rateIndex] >> (mpeg1 ? 0 : version == 2 ? 1 : 2);
    if (layer == 1)
        return static_cast<size_t>((12 * bitrate / rate + padding) * 4);
    return static_cast<size_t>((layer == 3 && !mpeg1 ? 72 : 144) * bitrate / rate + padding);
}

// Vero se da `offset` partono `count` frame MPEG uno dopo l'altro.
bool mpegFrames(const std::vector<uint8_t>& data, size_t offset, int count)
{
    for (int i = 0; i < count; ++i) {
        const size_t length = mpegFrameLength(data, offset);
        if (length == 0)
            return false;
        offset += length;
        if (offset >= data.size())
            return true; // file cortissimo, ma fin qui tutto torna
    }
    return true;
}

// "MP1", "MP2" o "MP3", dal layer del primo frame valido.
std::string mpegFormatName(const std::vector<uint8_t>& data)
{
    const size_t start = id3TagSize(data);
    for (size_t offset = start; offset < std::min(data.size(), start + 65536); ++offset) {
        if (mpegFrameLength(data, offset) > 0)
            return "MP" + std::to_string(4 - ((data[offset + 1] >> 1) & 3));
    }
    return "MP3";
}

bool hasMpegExtension(const SourceFile& file)
{
    std::string extension = file.path.extension().string();
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".mp3" || extension == ".mp2" || extension == ".mpga";
}

Encoding detect(const SourceFile& file)
{
    const std::vector<uint8_t>& data = file.data;
    if ((startsWith(data, 0, "RIFF") || startsWith(data, 0, "RF64")) && startsWith(data, 8, "WAVE"))
        return Encoding::Wav;
    if (startsWith(data, 0, "OggS") && data.size() > 27) {
        // Il primo pacchetto è l'intestazione: "\x01vorbis" (l'Opus non c'è).
        const size_t body = 27 + data[26];
        return startsWith(data, body, "\x01vorbis") ? Encoding::Vorbis : Encoding::None;
    }
    const size_t start = id3TagSize(data);
    if (startsWith(data, start, "fLaC"))
        return Encoding::Flac;
    // Le intestazioni MPEG sono brevi e possono capitare per caso: servono
    // un tag ID3, o più frame di fila, o l'estensione giusta.
    if (mpegFrames(data, start, start > 0 ? 1 : 4))
        return Encoding::Mp3;
    if (hasMpegExtension(file)) {
        for (size_t offset = start; offset < std::min(data.size(), start + 65536); ++offset) {
            if (mpegFrames(data, offset, 3))
                return Encoding::Mp3;
        }
    }
    return Encoding::None;
}

class MiniaudioDecoder final : public Decoder {
public:
    MiniaudioDecoder(const SourceFile& file, Encoding encoding, const OutputSpec& spec)
        : data_(file.data)
        , sampleRate_(spec.sampleRate)
    {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, spec.sampleRate);
        AudioTags tags;
        switch (encoding) {
        case Encoding::Wav:
            config.encodingFormat = ma_encoding_format_wav;
            info_.format = "WAV";
            tags = readWavTags(data_);
            break;
        case Encoding::Flac:
            config.encodingFormat = ma_encoding_format_flac;
            info_.format = "FLAC";
            tags = readFlacTags(data_);
            break;
        case Encoding::Mp3:
            config.encodingFormat = ma_encoding_format_mp3;
            info_.format = mpegFormatName(data_);
            tags = readId3Tags(data_);
            break;
        case Encoding::Vorbis:
            config.encodingFormat = ma_encoding_format_vorbis;
            info_.format = "Ogg Vorbis";
            tags = readOggVorbisTags(data_);
            break;
        case Encoding::None:
            break;
        }
        // Ricampionamento lineare con il filtro anti-aliasing più ripido.
        config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
        if (ma_decoder_init_memory(data_.data(), data_.size(), &config, &decoder_) != MA_SUCCESS)
            throw DecoderError("file " + info_.format + " non valido");

        info_.backend = "miniaudio";
        info_.title = tags.title;
        info_.author = tags.artist;
        ma_uint64 length = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder_, &length) == MA_SUCCESS && length > 0)
            info_.duration = static_cast<double>(length) / sampleRate_;
    }

    ~MiniaudioDecoder() override { ma_decoder_uninit(&decoder_); }

    // Il decoder di miniaudio tiene puntatori a sé stesso e ai dati.
    MiniaudioDecoder(const MiniaudioDecoder&) = delete;
    MiniaudioDecoder& operator=(const MiniaudioDecoder&) = delete;

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int /*index*/) override { seek(0); }

    size_t render(float* out, size_t frames) override
    {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder_, out, frames, &read);
        return static_cast<size_t>(read);
    }

    bool canSeek() const override { return true; }

    void seek(double seconds) override
    {
        ma_decoder_seek_to_pcm_frame(&decoder_, static_cast<ma_uint64>(std::llround(std::max(0.0, seconds) * sampleRate_)));
    }

    double position() const override
    {
        ma_uint64 cursor = 0;
        ma_decoder_get_cursor_in_pcm_frames(&decoder_, &cursor);
        return static_cast<double>(cursor) / sampleRate_;
    }

private:
    std::vector<uint8_t> data_;
    int sampleRate_;
    mutable ma_decoder decoder_{};
    TrackInfo info_;
};

} // namespace

std::string_view MiniaudioDecoderFactory::description() const
{
    return "miniaudio: file audio (MP3, FLAC, WAV, Ogg Vorbis)";
}

int MiniaudioDecoderFactory::probe(const SourceFile& file)
{
    return detect(file) == Encoding::None ? 0 : kProbeScore;
}

std::unique_ptr<Decoder> MiniaudioDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const Encoding encoding = detect(file);
    if (encoding == Encoding::None)
        throw DecoderError("non è un file audio riconosciuto");
    return std::make_unique<MiniaudioDecoder>(file, encoding, spec);
}

} // namespace mp
