#include "VgmstreamDecoder.h"

#include "core/Platform.h"
#include "core/Resampler.h"

extern "C" {
#include <libvgmstream.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

namespace {

namespace fs = std::filesystem;

// Dopo i backend che riconoscono i loro formati dal contenuto, UADE
// compreso: molti formati vgmstream li riconosce solo dall'estensione.
constexpr int kProbeScore = 40;
// Come la CLI di vgmstream: due giri del ritornello, poi dieci secondi di
// dissolvenza.
constexpr double kLoopCount = 2;
constexpr double kFadeSeconds = 10;
// Frame chiesti a vgmstream per volta.
constexpr size_t kBlock = 1024;

using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

std::string toUtf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path fromUtf8(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

// I file che vgmstream apre per un brano: il brano stesso dalla memoria
// (può essere stato decompresso), quelli che lo accompagnano (le
// intestazioni a parte, il canale destro in un altro file...) dal disco,
// letti una volta sola. vgmstream riapre il brano per ogni canale.
class Files {
public:
    Files(const fs::path& path, Bytes data)
        : name_(toUtf8(path))
        , data_(std::move(data))
    {
    }

    const std::string& name() const { return name_; }

    // nullptr se il file non c'è.
    Bytes find(const std::string& name)
    {
        if (name == name_)
            return data_;
        auto found = others_.find(name);
        if (found == others_.end())
            found = others_.emplace(name, load(fromUtf8(name))).first;
        return found->second;
    }

private:
    static Bytes load(fs::path path)
    {
        std::error_code error;
        if (!fs::is_regular_file(path, error))
            path = findIgnoringCase(path.parent_path(), path.filename().string());
        std::ifstream in(path, std::ios::binary);
        if (path.empty() || !in)
            return nullptr;
        return std::make_shared<const std::vector<uint8_t>>(std::istreambuf_iterator<char>(in),
                                                            std::istreambuf_iterator<char>());
    }

    std::string name_;
    Bytes data_;
    std::map<std::string, Bytes> others_;
};

// Un file aperto da vgmstream (libstreamfile_t::user_data).
struct OpenFile {
    std::shared_ptr<Files> files;
    Bytes data;
    std::string name;
};

libstreamfile_t* openFile(const std::shared_ptr<Files>& files, const std::string& name);

int readFile(void* user, uint8_t* dst, int64_t offset, int length)
{
    const std::vector<uint8_t>& data = *static_cast<OpenFile*>(user)->data;
    if (offset < 0 || length <= 0 || static_cast<uint64_t>(offset) >= data.size())
        return 0;
    const size_t count = std::min(static_cast<size_t>(length), data.size() - static_cast<size_t>(offset));
    std::memcpy(dst, data.data() + offset, count);
    return static_cast<int>(count);
}

int64_t fileSize(void* user)
{
    return static_cast<int64_t>(static_cast<OpenFile*>(user)->data->size());
}

const char* fileName(void* user)
{
    return static_cast<OpenFile*>(user)->name.c_str();
}

libstreamfile_t* reopenFile(void* user, const char* name)
{
    return openFile(static_cast<OpenFile*>(user)->files, name);
}

void closeFile(libstreamfile_t* file)
{
    delete static_cast<OpenFile*>(file->user_data);
    delete file;
}

libstreamfile_t* openFile(const std::shared_ptr<Files>& files, const std::string& name)
{
    Bytes data = files->find(name);
    if (!data)
        return nullptr;
    auto* file = new libstreamfile_t{};
    file->user_data = new OpenFile{files, std::move(data), name};
    file->read = readFile;
    file->get_size = fileSize;
    file->get_name = fileName;
    file->open = reopenFile;
    file->close = closeFile;
    return file;
}

struct LibDeleter {
    void operator()(libvgmstream_t* lib) const { libvgmstream_free(lib); }
};
using Lib = std::unique_ptr<libvgmstream_t, LibDeleter>;

// Apre in lib il sottobrano subsong (1..N, 0 il primo o l'unico) con la
// configurazione del player: loop, dissolvenza, al più due canali, float.
// false se il file non è di vgmstream.
bool openSubsong(libvgmstream_t* lib, const std::shared_ptr<Files>& files, int subsong)
{
    libvgmstream_config_t config{};
    config.loop_count = kLoopCount;
    config.fade_time = kFadeSeconds;
    config.auto_downmix_channels = kChannels;
    config.force_sfmt = LIBVGMSTREAM_SFMT_FLOAT;
    libvgmstream_setup(lib, &config);
    libstreamfile_t* file = openFile(files, files->name());
    if (!file)
        return false;
    // vgmstream riapre da sé i file che gli servono: questo si chiude subito.
    const int result = libvgmstream_open_stream(lib, file, subsong);
    libstreamfile_close(file);
    return result >= 0;
}

// La piattaforma dal codec o dal formato, quando sono di una sola famiglia di
// console: il PS-ADPCM della PlayStation (anche PS2 e PSP), l'AICA del
// Dreamcast. PCM, IMA e gli altri girano dappertutto.
const char* platformOf(std::string_view codec, std::string_view meta)
{
    if (codec.find("PlayStation") != std::string_view::npos || codec.find("HEVAG") != std::string_view::npos
        || meta.find("PS2") != std::string_view::npos || meta.find("PSX") != std::string_view::npos)
        return platform::kPlayStation;
    if (codec.find("AICA") != std::string_view::npos)
        return platform::kDreamcast;
    return platform::kAudio;
}

class VgmstreamDecoder final : public Decoder {
public:
    VgmstreamDecoder(const SourceFile& file, const OutputSpec& spec)
        : files_(std::make_shared<Files>(file.path, std::make_shared<const std::vector<uint8_t>>(file.data)))
        , sampleRate_(spec.sampleRate)
        , lib_(libvgmstream_init())
        , stem_(file.path.stem().string())
    {
        if (!lib_ || !openSubsong(lib_.get(), files_, 0))
            throw DecoderError("vgmstream non riconosce il file");
        // subsong_count 0: il formato non ha sottobrani.
        hasSubsongs_ = lib_->format->subsong_count > 0;
        info_.backend = "vgmstream";
        info_.subsongCount = std::max(1, lib_->format->subsong_count);
        start(std::max(0, lib_->format->subsong_index - 1));
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        if (!openSubsong(lib_.get(), files_, hasSubsongs_ ? index + 1 : 0))
            throw DecoderError("sottobrano " + std::to_string(index + 1) + " non valido");
        start(index);
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

    // vgmstream decodifica fino al punto, dall'inizio o dal loop: il seek è
    // esatto anche negli ADPCM, che dipendono dai campioni precedenti.
    void seek(double seconds) override
    {
        const double clamped = std::clamp(seconds, 0.0, info_.duration.value_or(0));
        libvgmstream_seek(lib_.get(), std::llround(clamped * nativeRate_));
        resampler_->reset();
        rendered_ = static_cast<size_t>(std::llround(clamped * sampleRate_));
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

private:
    void start(int index)
    {
        const libvgmstream_format_t& format = *lib_->format;
        if (format.sample_rate <= 0 || format.channels <= 0)
            throw DecoderError("flusso senza frequenza o canali");
        nativeRate_ = format.sample_rate;
        channels_ = format.channels;
        resampler_ = std::make_unique<Resampler>(nativeRate_, sampleRate_);
        rendered_ = 0;
        info_.currentSubsong = index;
        info_.format = format.meta_name;
        info_.player = std::string(format.codec_name) + ", " + std::to_string(nativeRate_) + " Hz";
        info_.platform = platformOf(format.codec_name, format.meta_name);
        info_.title = hasSubsongs_ && format.stream_name[0] != '\0' ? format.stream_name : stem_;
        info_.duration = static_cast<double>(format.play_samples) / nativeRate_;
    }

    // Frame stereo alla frequenza del brano; meno di frames solo alla fine.
    size_t decode(float* out, size_t frames)
    {
        size_t done = 0;
        while (done < frames) {
            const size_t wanted = std::min(frames - done, kBlock);
            block_.resize(wanted * static_cast<size_t>(channels_));
            if (libvgmstream_fill(lib_.get(), block_.data(), static_cast<int>(wanted)) < 0)
                break;
            const size_t got = static_cast<size_t>(std::max(0, lib_->decoder->buf_samples));
            for (size_t i = 0; i < got; ++i) {
                const float* frame = block_.data() + i * static_cast<size_t>(channels_);
                out[(done + i) * kChannels] = frame[0];
                out[(done + i) * kChannels + 1] = frame[channels_ > 1 ? 1 : 0];
            }
            done += got;
            if (got < wanted)
                break;
        }
        return done;
    }

    std::shared_ptr<Files> files_;
    int sampleRate_;
    Lib lib_;
    std::string stem_;
    bool hasSubsongs_ = false;
    int nativeRate_ = 0;
    int channels_ = 0;
    std::vector<float> block_;
    std::unique_ptr<Resampler> resampler_;
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view VgmstreamDecoderFactory::description() const
{
    return "vgmstream: l'audio in streaming dei giochi (GENH, ADS, VAG, DSP, ADX, BRSTM, HCA, ...)";
}

// L'estensione deve essere fra quelle di vgmstream, poi vgmstream deve
// riuscire ad aprirlo. Anche le estensioni comuni (WAV, Ogg): miniaudio
// viene prima, ma i WAV con i codec delle console (Xbox ADPCM, PS-ADPCM) li
// apre solo vgmstream.
int VgmstreamDecoderFactory::probe(const SourceFile& file)
{
    libvgmstream_valid_t valid{};
    valid.reject_extensionless = true;
    valid.accept_common = true;
    if (!libvgmstream_is_valid(toUtf8(file.path).c_str(), &valid))
        return 0;
    // I dati del brano senza copiarli: servono solo durante il probe.
    const Bytes data(&file.data, [](const std::vector<uint8_t>*) {});
    const Lib lib(libvgmstream_init());
    return lib && openSubsong(lib.get(), std::make_shared<Files>(file.path, data), 0) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> VgmstreamDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<VgmstreamDecoder>(file, spec);
}

} // namespace mp
