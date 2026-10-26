#include "MdxDecoder.h"

#include "core/Platform.h"
#include "core/Resampler.h"
#include "core/ShiftJis.h"
#include "core/SourceFile.h"

extern "C" {
#include <mdxmini.h>
#include <mdx2151.h>
}

#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// mdxmini genera a una frequenza sola, per tutti i brani.
constexpr int kNativeRate = 44100;
// Come per gli stream dei giochi: due giri, poi la dissolvenza di mdxmini.
constexpr int kLoops = 2;
// Un brano che non finisce mai si ferma qui.
constexpr double kMaxSeconds = 30 * 60;
constexpr int kFmTracks = 8;

// mdx_open scrive delle variabili globali di mdxmini (le stesse a ogni
// brano) e prepara le tabelle dell'YM2151: un brano per volta.
std::mutex gOpenMutex;

// Quello che mdxmini legge dall'inizio del file senza controllare i limiti:
// il titolo fino a CR LF EOF, il nome del PDX fino allo zero, poi l'offset
// delle voci e quelli delle tracce (9, o 16 col PCM8), tutti nel file.
struct Header {
    std::string title; // Shift-JIS
    std::string pdx;
};

unsigned be16(const std::vector<uint8_t>& data, size_t at)
{
    return data[at] << 8 | data[at + 1];
}

std::optional<Header> readHeader(const std::vector<uint8_t>& data)
{
    const size_t size = data.size();
    size_t end = 0;
    while (end + 2 < std::min<size_t>(size, 1024)
           && !(data[end] == 0x0D && data[end + 1] == 0x0A && data[end + 2] == 0x1A))
        ++end;
    if (end + 2 >= std::min<size_t>(size, 1024))
        return std::nullopt;
    Header header;
    header.title.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(end));
    size_t at = end + 3;
    while (at < size && at < end + 3 + 256 && data[at] != 0)
        header.pdx += static_cast<char>(data[at++]);
    if (at >= size || data[at] != 0)
        return std::nullopt;
    const size_t base = at + 1;
    if (base + 4 > size || base + be16(data, base) > size)
        return std::nullopt;
    const size_t first = base + be16(data, base + 2);
    if (first >= size)
        return std::nullopt;
    const size_t tracks = data[first] == 0xE8 ? 16 : 9; // MDX_SET_PCM8_MODE
    if (base + 2 + tracks * 2 > size)
        return std::nullopt;
    for (size_t i = 0; i < tracks; ++i) {
        if (base + be16(data, base + 2 + i * 2) >= size)
            return std::nullopt;
    }
    return header;
}

bool hasExtension(const SourceFile& file, const char* extension)
{
    std::string name = file.path.extension().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name == extension;
}

// Il PDX accanto all'MDX, con maiuscole e minuscole come capita; vuoto se
// non c'è (allora il brano suona senza campioni).
std::string findPdx(const SourceFile& file, std::string name)
{
    if (name.empty())
        return {};
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".pdx") != 0)
        name += ".pdx";
    return findIgnoringCase(file.path.parent_path(), name).string();
}

class Song {
public:
    Song(const std::string& path, const std::string& pdx)
    {
        static std::once_flag rate;
        std::call_once(rate, [] { mdx_set_rate(kNativeRate); });
        std::string mdxPath = path;
        std::string pdxPath = pdx;
        const std::lock_guard lock(gOpenMutex);
        if (mdx_open(&mini_, mdxPath.data(), pdxPath.empty() ? nullptr : pdxPath.data()) != 0)
            throw DecoderError("mdxmini non apre il file");
        mdx_set_max_loop(&mini_, kLoops);
    }

    ~Song()
    {
        const std::lock_guard lock(gOpenMutex);
        mdx_close(&mini_);
    }

    Song(const Song&) = delete;
    Song& operator=(const Song&) = delete;

    t_mdxmini* get() { return &mini_; }

private:
    t_mdxmini mini_{};
};

class MdxDecoder final : public Decoder {
public:
    MdxDecoder(const SourceFile& file, const Header& header, const OutputSpec& spec)
        : path_(file.path.string())
        , pdx_(findPdx(file, header.pdx))
        , sampleRate_(spec.sampleRate)
    {
        // La durata: si scorre il brano senza sintetizzarlo.
        {
            Song measure(path_, pdx_);
            double seconds = 0;
            int next = 1;
            while (next && seconds < kMaxSeconds) {
                next = mdx_next_frame(measure.get());
                seconds += mdx_frame_length(measure.get()) / 1e6;
            }
            info_.duration = seconds;
        }
        restart();
        const int tracks = song_->get()->mdx->tracks;
        for (int i = 0; i < tracks; ++i) {
            const char letter = static_cast<char>(i < kFmTracks ? 'A' + i : 'P' + (i - kFmTracks));
            channels_.push_back(std::string(i < kFmTracks ? "FM " : "PCM ") + letter);
        }
        info_.backend = "mdx";
        info_.platform = platform::kX68000;
        info_.format = tracks > 9 ? "MDX (PCM8)" : "MDX";
        info_.player = header.pdx.empty() ? "mdxmini" : pdx_.empty() ? "mdxmini, senza il PDX" : "mdxmini con il PDX";
        info_.title = shiftJisToUtf8(header.title);
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

    // Da capo, poi i frame del driver senza sintetizzarli fino al punto, e
    // l'ultimo pezzo sintetizzato e buttato: le note che suonano al punto
    // ripartono da lì.
    void seek(double seconds) override
    {
        const double target = std::clamp(seconds, 0.0, info_.duration.value_or(seconds));
        restart();
        t_mdxmini* mini = song_->get();
        const auto targetFrames = static_cast<size_t>(target * kNativeRate);
        size_t elapsed = 0;
        while (!finished_) {
            finished_ = !mdx_next_frame(mini);
            const auto frameLength = static_cast<size_t>(
                static_cast<long long>(kNativeRate) * mdx_frame_length(mini) / 1000000);
            if (elapsed + frameLength > targetFrames) {
                mini->samples = static_cast<int>(frameLength);
                skip(targetFrames - elapsed);
                break;
            }
            elapsed += frameLength;
        }
        nativeDone_ = targetFrames;
        rendered_ = static_cast<size_t>(target * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    std::vector<std::string> channels() const override { return channels_; }

    void setChannelMuted(int channel, bool muted) override
    {
        if (channel < 0 || channel >= static_cast<int>(channels_.size()))
            return;
        if (muted)
            mutedTracks_ |= 1 << channel;
        else
            mutedTracks_ &= ~(1 << channel);
        applyMutes();
        // La nota che suona tace subito; le prossime non partono.
        if (muted && channel < kFmTracks)
            ym2151_note_off(channel, song_->get()->songdata);
        else if (muted)
            pcm8_note_off(channel - kFmTracks, song_->get()->songdata);
    }

private:
    void restart()
    {
        song_.reset();
        song_ = std::make_unique<Song>(path_, pdx_);
        applyMutes();
        resampler_ = std::make_unique<Resampler>(kNativeRate, sampleRate_);
        finished_ = false;
        nativeDone_ = 0;
        rendered_ = 0;
    }

    void applyMutes() { song_->get()->mdx->muted_tracks = mutedTracks_; }

    void skip(size_t frames)
    {
        block_.resize(1024 * kChannels);
        while (frames > 0) {
            const size_t count = std::min<size_t>(frames, 1024);
            mdx_calc_sample(song_->get(), block_.data(), static_cast<int>(count));
            frames -= count;
        }
    }

    // Frame stereo a 44,1 kHz, fino alla durata misurata.
    size_t decode(float* out, size_t frames)
    {
        const auto total = static_cast<size_t>(info_.duration.value_or(0) * kNativeRate);
        frames = std::min(frames, total > nativeDone_ ? total - nativeDone_ : 0);
        if (frames == 0)
            return 0;
        block_.resize(frames * kChannels);
        mdx_calc_sample(song_->get(), block_.data(), static_cast<int>(frames));
        for (size_t i = 0; i < frames * kChannels; ++i)
            out[i] = block_[i] / 32768.0f;
        nativeDone_ += frames;
        return frames;
    }

    std::string path_;
    std::string pdx_;
    int sampleRate_;
    std::unique_ptr<Song> song_;
    std::unique_ptr<Resampler> resampler_;
    std::vector<short> block_;
    std::vector<std::string> channels_;
    int mutedTracks_ = 0;
    bool finished_ = false;
    size_t nativeDone_ = 0; // frame a 44,1 kHz dati dall'inizio
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view MdxDecoderFactory::description() const
{
    return "mdxmini: Sharp X68000 (MDX, con i campioni PDX)";
}

int MdxDecoderFactory::probe(const SourceFile& file)
{
    return hasExtension(file, ".mdx") && readHeader(file.data) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> MdxDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const std::optional<Header> header = hasExtension(file, ".mdx") ? readHeader(file.data) : std::nullopt;
    if (!header)
        throw DecoderError("non è un MDX");
    if (file.wasUnpacked())
        throw DecoderError("MDX compresso: mdxmini legge solo dal disco");
    return std::make_unique<MdxDecoder>(file, *header, spec);
}

} // namespace mp
