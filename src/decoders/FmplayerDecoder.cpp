#include "FmplayerDecoder.h"

#include "FmplayerGlue.h"
#include "core/Platform.h"
#include "core/Resampler.h"
#include "core/ShiftJis.h"
#include "core/SourceFile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mp {

namespace {

namespace fs = std::filesystem;

constexpr int kProbeScore = 80;
constexpr size_t kMaxSize = 0xFFFF; // i driver indirizzano il brano a 16 bit
constexpr size_t kDrumRomSize = 0x2000;
// Come per gli stream dei giochi: due giri, poi dieci secondi di dissolvenza.
constexpr int kLoops = 2;
constexpr double kFadeSeconds = 10;
constexpr double kMaxSeconds = 30 * 60;

std::optional<int> driverOf(const SourceFile& file)
{
    std::string extension = file.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (extension == ".m" || extension == ".m2" || extension == ".mz" || extension == ".m86")
        return MP_FM_PMD;
    if (extension == ".opi" || extension == ".ovi" || extension == ".ozi")
        return MP_FM_FMP;
    return std::nullopt;
}

struct SongDeleter {
    void operator()(mp_fm_song* song) const { mp_fm_free(song); }
};
using Song = std::unique_ptr<mp_fm_song, SongDeleter>;

Song createSong(int driver, const std::vector<uint8_t>& data)
{
    if (data.empty() || data.size() > kMaxSize)
        return nullptr;
    return Song(mp_fm_create(driver, data.data(), data.size()));
}

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// La ROM della batteria dello YM2608, cercata una volta nelle cartelle delle
// ROM dell'utente e nelle loro sottocartelle.
std::mutex gRomMutex;
std::vector<fs::path> gRomDirectories;
std::optional<std::vector<uint8_t>> gDrumRom;
bool gDrumRomSearched = false;

} // namespace

const std::vector<uint8_t>* fmplayer::drumRom()
{
    const std::lock_guard lock(gRomMutex);
    if (!gDrumRomSearched) {
        gDrumRomSearched = true;
        for (const fs::path& directory : gRomDirectories) {
            std::error_code error;
            for (auto it = fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, error);
                 !gDrumRom && !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
                if (it.depth() >= 3)
                    it.disable_recursion_pending();
                std::string name = it->path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                if (name == "ym2608_adpcm_rom.bin" && it->is_regular_file(error) && it->file_size(error) == kDrumRomSize)
                    gDrumRom = readFile(it->path());
            }
            if (gDrumRom)
                break;
        }
    }
    return gDrumRom ? &*gDrumRom : nullptr;
}

namespace {

using fmplayer::drumRom;

// I campioni accanto al brano: "NOME" più l'estensione, o il nome com'è se
// ne ha già una, con maiuscole e minuscole come capita.
std::vector<uint8_t> companion(const fs::path& directory, const std::string& name, const char* extension)
{
    if (name.empty())
        return {};
    for (const std::string& candidate : {name + extension, name}) {
        const fs::path path = findIgnoringCase(directory, candidate);
        if (!path.empty())
            return readFile(path);
    }
    return {};
}

// I PCM del brano; false se ne manca qualcuno.
bool loadSamples(mp_fm_song* song, const fs::path& directory, int driver)
{
    bool complete = true;
    if (const std::string adpcm = mp_fm_pcm_name(song, 0); !adpcm.empty()) {
        const std::vector<uint8_t> data = companion(directory, adpcm, driver == MP_FM_PMD ? ".PPC" : ".PVI");
        complete = !data.empty() && mp_fm_load_adpcm(song, data.data(), data.size());
    }
    for (int bank = 0; bank < 2; ++bank) {
        const std::string name = mp_fm_pcm_name(song, bank + 1);
        if (name.empty())
            continue;
        // Il PMD accetta PVI e PZI, l'FMP solo PVI.
        std::vector<uint8_t> data = companion(directory, name, ".PVI");
        bool loaded = !data.empty() && mp_fm_load_ppz(song, bank, data.data(), data.size(), false);
        if (!loaded && driver == MP_FM_PMD) {
            data = companion(directory, name, ".PZI");
            loaded = !data.empty() && mp_fm_load_ppz(song, bank, data.data(), data.size(), true);
        }
        complete = complete && loaded;
    }
    return complete;
}

// Un commento del brano senza le sequenze ANSI dei colori, in UTF-8.
std::string comment(mp_fm_song* song, int line)
{
    const char* text = mp_fm_comment(song, line);
    if (!text)
        return {};
    std::string plain;
    for (const char* c = text; *c; ++c) {
        if (*c == 0x1B && c[1] == '[') {
            c += 2;
            while (*c && !std::isalpha(static_cast<unsigned char>(*c)))
                ++c;
            if (!*c)
                break;
            continue;
        }
        plain += *c;
    }
    std::string utf8 = shiftJisToUtf8(plain);
    while (!utf8.empty() && std::isspace(static_cast<unsigned char>(utf8.back())))
        utf8.pop_back();
    return utf8;
}

class FmplayerDecoder final : public Decoder {
public:
    FmplayerDecoder(const SourceFile& file, int driver, const OutputSpec& spec)
        : data_(file.data)
        , directory_(file.path.parent_path())
        , driver_(driver)
        , sampleRate_(spec.sampleRate)
    {
        const Song song = createSong(driver_, data_);
        if (!song)
            throw DecoderError("98fmplayer non riconosce il brano");
        fadeStart_ = mp_fm_measure(song.get(), kLoops, kMaxSeconds, &looped_);
        info_.duration = looped_ ? fadeStart_ + kFadeSeconds : fadeStart_;

        restart();
        const bool ppz = mp_fm_pcm_name(song_.get(), 1)[0] || mp_fm_pcm_name(song_.get(), 2)[0];
        for (int i = 1; i <= 6; ++i)
            channels_.push_back("FM " + std::to_string(i));
        for (int i = 1; i <= 3; ++i)
            channels_.push_back("SSG " + std::to_string(i));
        channels_.push_back("Ritmo");
        channels_.push_back("ADPCM");
        if (ppz) {
            for (int i = 1; i <= 8; ++i)
                channels_.push_back("PPZ8 " + std::to_string(i));
        }

        info_.backend = "fmplayer";
        info_.platform = platform::kPc98;
        info_.format = driver_ == MP_FM_PMD ? "PMD" : "FMP";
        info_.player = std::string("98fmplayer") + (samplesComplete_ ? "" : ", senza i campioni PCM")
            + (drumRom() ? "" : ", senza la ROM della batteria");
        info_.title = comment(song_.get(), 0);
        if (mp_fm_comment_is_pmd(song_.get()))
            info_.author = comment(song_.get(), 1);
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
        const size_t got = resampler_->render(out, frames, [this](float* buffer, size_t count) {
            return decode(buffer, count);
        });
        rendered_ += got;
        return got;
    }

    bool canSeek() const override { return true; }

    // Da capo, poi i tick del driver senza sintetizzare fino al punto, e
    // l'ultimo pezzo sintetizzato e buttato.
    void seek(double seconds) override
    {
        const double target = std::clamp(seconds, 0.0, info_.duration.value_or(seconds));
        restart();
        mp_fm_song* song = song_.get();
        double time = 0;
        while (mp_fm_playing(song) && time + mp_fm_tick_seconds(song) <= target) {
            time += mp_fm_tick_seconds(song);
            mp_fm_tick(song);
        }
        mp_fm_restart_timer(song);
        nativeDone_ = static_cast<size_t>(time * MP_FM_RATE);
        const auto targetFrames = static_cast<size_t>(target * MP_FM_RATE);
        while (nativeDone_ < targetFrames) {
            const size_t count = std::min<size_t>(targetFrames - nativeDone_, 4096);
            block_.resize(count * kChannels);
            mp_fm_mix(song, block_.data(), static_cast<unsigned>(count));
            nativeDone_ += count;
        }
        rendered_ = static_cast<size_t>(target * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    std::vector<std::string> channels() const override { return channels_; }

    void setChannelMuted(int channel, bool muted) override
    {
        if (channel < 0 || channel >= static_cast<int>(channels_.size()))
            return;
        if (muted)
            muted_ |= 1u << channel;
        else
            muted_ &= ~(1u << channel);
        mp_fm_set_mute(song_.get(), muted_);
    }

private:
    void restart()
    {
        song_ = createSong(driver_, data_);
        if (!song_)
            throw DecoderError("98fmplayer non riconosce il brano");
        const std::vector<uint8_t>* rom = drumRom();
        mp_fm_start(song_.get(), rom ? rom->data() : nullptr);
        samplesComplete_ = loadSamples(song_.get(), directory_, driver_);
        mp_fm_set_mute(song_.get(), muted_);
        resampler_ = std::make_unique<Resampler>(MP_FM_RATE, sampleRate_);
        nativeDone_ = 0;
        rendered_ = 0;
    }

    // Frame stereo a 55,467 kHz, fino alla durata, con la dissolvenza dopo il
    // secondo giro.
    size_t decode(float* out, size_t frames)
    {
        const auto total = static_cast<size_t>(info_.duration.value_or(0) * MP_FM_RATE);
        frames = std::min(frames, total > nativeDone_ ? total - nativeDone_ : 0);
        if (frames == 0)
            return 0;
        block_.resize(frames * kChannels);
        mp_fm_mix(song_.get(), block_.data(), static_cast<unsigned>(frames));
        for (size_t i = 0; i < frames; ++i) {
            float gain = 1.0f / 32768;
            if (looped_) {
                const double time = static_cast<double>(nativeDone_ + i) / MP_FM_RATE;
                if (time > fadeStart_)
                    gain *= static_cast<float>(std::max(0.0, 1 - (time - fadeStart_) / kFadeSeconds));
            }
            out[i * kChannels] = block_[i * kChannels] * gain;
            out[i * kChannels + 1] = block_[i * kChannels + 1] * gain;
        }
        nativeDone_ += frames;
        return frames;
    }

    std::vector<uint8_t> data_;
    fs::path directory_;
    int driver_;
    int sampleRate_;
    Song song_;
    std::unique_ptr<Resampler> resampler_;
    std::vector<int16_t> block_;
    std::vector<std::string> channels_;
    unsigned muted_ = 0;
    bool looped_ = false;
    bool samplesComplete_ = true;
    double fadeStart_ = 0;
    size_t nativeDone_ = 0; // frame a 55,467 kHz dati dall'inizio
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view FmplayerDecoderFactory::description() const
{
    return "98fmplayer: NEC PC-98 (PMD e FMP, sullo YM2608)";
}

int FmplayerDecoderFactory::probe(const SourceFile& file)
{
    const std::optional<int> driver = driverOf(file);
    return driver && createSong(*driver, file.data) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> FmplayerDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const std::optional<int> driver = driverOf(file);
    if (!driver)
        throw DecoderError("non è un brano PMD o FMP");
    return std::make_unique<FmplayerDecoder>(file, *driver, spec);
}

namespace fmplayer {

void addRomDirectory(const std::string& path)
{
    const std::lock_guard lock(gRomMutex);
    if (path.empty())
        return;
    gRomDirectories.emplace_back(path);
    gDrumRomSearched = false;
}

} // namespace fmplayer

} // namespace mp
