#include "FfmpegDecoder.h"

#include "core/Platform.h"
#include "core/Resampler.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

namespace {

// Dopo miniaudio (MP3, FLAC, WAV e Ogg Vorbis li suona lui) e prima di
// vgmstream, che con FFmpeg dentro aprirebbe gli stessi file come stream,
// senza i tag.
constexpr int kProbeScore = 45;
// Quanto dell'inizio del file guarda il riconoscimento: basta anche con un
// tag ID3 davanti, che FFmpeg salta.
constexpr size_t kProbeBytes = 256 * 1024;
constexpr int kIoBuffer = 64 * 1024;
// Da quanto prima del punto di un seek si comincia a decodificare.
constexpr double kSeekPreroll = 0.2;
// Dopo tanti pacchetti rovinati di fila il file finisce lì.
constexpr int kMaxErrors = 100;

// I contenitori dei file audio che apre questo backend, come li chiama
// FFmpeg. Gli altri che FFmpeg ha (wav, mp3, xwma) servono a vgmstream.
constexpr std::string_view kContainers[] = {
    "mov,mp4,m4a,3gp,3g2,mj2", "ogg", "aiff", "matroska,webm", "asf", "ape", "wv", "tta",
    "mpc", "mpc8", "tak", "caf", "au", "aac",
};

// Il file letto dalla memoria (può essere stato estratto o decompresso).
struct MemoryInput {
    const std::vector<uint8_t>* data;
    size_t position = 0;
};

int readInput(void* opaque, uint8_t* buffer, int size)
{
    auto* input = static_cast<MemoryInput*>(opaque);
    const size_t left = input->data->size() - input->position;
    if (left == 0)
        return AVERROR_EOF;
    const size_t count = std::min(left, static_cast<size_t>(size));
    std::memcpy(buffer, input->data->data() + input->position, count);
    input->position += count;
    return static_cast<int>(count);
}

int64_t seekInput(void* opaque, int64_t offset, int whence)
{
    auto* input = static_cast<MemoryInput*>(opaque);
    const auto size = static_cast<int64_t>(input->data->size());
    if (whence & AVSEEK_SIZE)
        return size;
    whence &= ~AVSEEK_FORCE;
    const int64_t base = whence == SEEK_CUR ? static_cast<int64_t>(input->position) : whence == SEEK_END ? size : 0;
    if (base + offset < 0 || base + offset > size)
        return -1;
    input->position = static_cast<size_t>(base + offset);
    return base + offset;
}

// Il contenitore secondo FFmpeg, se è di quelli di kContainers e ne è sicuro
// almeno quanto di un'estensione giusta; nullptr altrimenti.
const AVInputFormat* detect(const SourceFile& file)
{
    const size_t size = std::min(file.data.size(), kProbeBytes);
    std::vector<uint8_t> buffer(size + AVPROBE_PADDING_SIZE, 0); // FFmpeg legge oltre la fine
    std::memcpy(buffer.data(), file.data.data(), size);
    const std::string name = file.fileName();
    AVProbeData probe{};
    probe.filename = name.c_str();
    probe.buf = buffer.data();
    probe.buf_size = static_cast<int>(size);
    int score = 0;
    const AVInputFormat* format = av_probe_input_format3(&probe, 1, &score);
    if (!format || score < AVPROBE_SCORE_EXTENSION)
        return nullptr;
    const bool ours = std::find(std::begin(kContainers), std::end(kContainers), format->name) != std::end(kContainers);
    return ours ? format : nullptr;
}

std::string metadata(AVDictionary* dictionary, const char* key)
{
    const AVDictionaryEntry* entry = av_dict_get(dictionary, key, nullptr, 0);
    return entry && entry->value ? entry->value : "";
}

// "AAC (M4A)", "Opus (Ogg)", "AIFF", "WavPack": il codec, e il contenitore
// quando non è il suo.
std::string formatName(const AVInputFormat& container, const AVCodecContext& codec, const SourceFile& file)
{
    const std::string_view name = container.name;
    std::string extension = file.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::toupper(c); });
    std::string box;
    if (name == "mov,mp4,m4a,3gp,3g2,mj2")
        box = extension == ".M4A" || extension == ".M4B" ? "M4A" : "MP4";
    else if (name == "ogg")
        box = "Ogg";
    else if (name == "matroska,webm")
        box = extension == ".WEBM" ? "WebM" : "Matroska";
    else if (name == "aiff")
        box = codec.codec_id == AV_CODEC_ID_PCM_S16BE || codec.codec_id == AV_CODEC_ID_PCM_S24BE
                || codec.codec_id == AV_CODEC_ID_PCM_S32BE || codec.codec_id == AV_CODEC_ID_PCM_S8
            ? "AIFF"
            : "AIFF-C";
    else if (name == "caf")
        box = "CAF";
    else if (name == "au")
        box = "AU";

    std::string label;
    switch (codec.codec_id) {
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_AAC_LATM: label = "AAC"; break;
    case AV_CODEC_ID_ALAC: label = "ALAC"; break;
    case AV_CODEC_ID_OPUS: label = "Opus"; break;
    case AV_CODEC_ID_VORBIS: label = "Vorbis"; break;
    case AV_CODEC_ID_FLAC: label = "FLAC"; break;
    case AV_CODEC_ID_MP3: label = "MP3"; break;
    case AV_CODEC_ID_WMAV1:
    case AV_CODEC_ID_WMAV2: return "WMA";
    case AV_CODEC_ID_WMAPRO: return "WMA Pro";
    case AV_CODEC_ID_WMALOSSLESS: return "WMA Lossless";
    case AV_CODEC_ID_APE: return "Monkey's Audio";
    case AV_CODEC_ID_WAVPACK: return "WavPack";
    case AV_CODEC_ID_TTA: return "TTA";
    case AV_CODEC_ID_MUSEPACK7:
    case AV_CODEC_ID_MUSEPACK8: return "Musepack";
    case AV_CODEC_ID_TAK: return "TAK";
    default:
        // PCM e gli ADPCM e i MACE dell'AIFF-C: conta il contenitore.
        if (!box.empty())
            return box;
        label = avcodec_get_name(codec.codec_id);
    }
    if (name == "aac")
        return label + " (ADTS)";
    return box.empty() || box == "AIFF" || box == "AIFF-C" ? label : label + " (" + box + ")";
}

struct FormatCloser {
    void operator()(AVFormatContext* format) const { avformat_close_input(&format); }
};
struct IoCloser {
    void operator()(AVIOContext* io) const
    {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
};
struct CodecCloser {
    void operator()(AVCodecContext* codec) const { avcodec_free_context(&codec); }
};
struct ResamplerCloser {
    void operator()(SwrContext* swr) const { swr_free(&swr); }
};
struct PacketCloser {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};
struct FrameCloser {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

class FfmpegDecoder final : public Decoder {
public:
    FfmpegDecoder(const SourceFile& file, const AVInputFormat* container, const OutputSpec& spec)
        : data_(file.data)
        , input_{&data_}
        , sampleRate_(spec.sampleRate)
        , packet_(av_packet_alloc())
        , frame_(av_frame_alloc())
    {
        auto* buffer = static_cast<uint8_t*>(av_malloc(kIoBuffer));
        AVIOContext* io = buffer ? avio_alloc_context(buffer, kIoBuffer, 0, &input_, readInput, nullptr, seekInput) : nullptr;
        if (!io)
            av_free(buffer);
        io_.reset(io);
        AVFormatContext* format = avformat_alloc_context();
        if (!io_ || !format || !packet_ || !frame_) {
            avformat_free_context(format);
            throw DecoderError("memoria esaurita");
        }
        format->pb = io_.get();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        const std::string name = file.fileName();
        // In caso di errore avformat_open_input libera il contesto da sé.
        if (avformat_open_input(&format, name.c_str(), container, nullptr) < 0)
            throw DecoderError("FFmpeg non apre il file");
        format_.reset(format);
        if (avformat_find_stream_info(format_.get(), nullptr) < 0)
            throw DecoderError("FFmpeg non legge le tracce del file");

        const AVCodec* decoder = nullptr;
        stream_ = av_find_best_stream(format_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (stream_ < 0 || !decoder)
            throw DecoderError(stream_ == AVERROR_DECODER_NOT_FOUND ? "codec audio non incluso" : "nessuna traccia audio");
        for (unsigned i = 0; i < format_->nb_streams; ++i)
            format_->streams[i]->discard = static_cast<int>(i) == stream_ ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

        const AVStream& stream = *format_->streams[stream_];
        codec_.reset(avcodec_alloc_context3(decoder));
        if (!codec_ || avcodec_parameters_to_context(codec_.get(), stream.codecpar) < 0)
            throw DecoderError("memoria esaurita");
        codec_->pkt_timebase = stream.time_base;
        if (avcodec_open2(codec_.get(), decoder, nullptr) < 0)
            throw DecoderError(std::string("il decoder ") + decoder->name + " non parte");
        nativeRate_ = codec_->sample_rate;
        if (nativeRate_ <= 0)
            throw DecoderError("traccia audio senza frequenza");
        timeBase_ = av_q2d(stream.time_base);
        // I tempi dei pacchetti partono da qui, non sempre da zero.
        if (stream.start_time != AV_NOPTS_VALUE)
            startTime_ = static_cast<double>(stream.start_time) * timeBase_;
        resampler_ = std::make_unique<Resampler>(nativeRate_, sampleRate_);
        openConverter();

        info_.backend = "ffmpeg";
        info_.platform = platform::kAudio;
        info_.format = formatName(*format_->iformat, *codec_, file);
        info_.player = std::string(decoder->long_name ? decoder->long_name : decoder->name) + ", "
            + std::to_string(nativeRate_) + " Hz";
        // Nell'Ogg i tag stanno nella traccia, nell'MP4 nel contenitore.
        for (AVDictionary* tags : {format_->metadata, stream.metadata}) {
            if (info_.title.empty())
                info_.title = metadata(tags, "title");
            if (info_.author.empty())
                info_.author = metadata(tags, "artist");
            if (info_.author.empty())
                info_.author = metadata(tags, "album_artist");
        }
        if (format_->duration != AV_NOPTS_VALUE && format_->duration > 0)
            info_.duration = static_cast<double>(format_->duration) / AV_TIME_BASE;
        else if (stream.duration != AV_NOPTS_VALUE && stream.duration > 0)
            info_.duration = static_cast<double>(stream.duration) * timeBase_;

        // Il primo pezzo subito: un file che non si decodifica non si apre, e
        // dal primo frame si sa di quanto i tempi dei frame sono avanti
        // (vedi convert).
        if (!refill())
            throw DecoderError("FFmpeg non decodifica il file");
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

    // Al punto chiave un po' prima, poi si decodifica fino al campione giusto:
    // il seek è esatto anche dove i pacchetti sono lunghi, e AAC e Opus, che
    // sovrappongono ogni frame al precedente, arrivano al punto già avviati.
    void seek(double seconds) override
    {
        const double target = std::clamp(seconds, 0.0, info_.duration.value_or(seconds));
        const double from = std::max(0.0, target - kSeekPreroll);
        av_seek_frame(format_.get(), stream_, std::llround((startTime_ + from) / timeBase_), AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec_.get());
        openConverter();
        pending_.clear();
        pendingStart_ = 0;
        draining_ = finished_ = false;
        seekTarget_ = target;
        resampler_->reset();
        rendered_ = static_cast<size_t>(std::llround(target * sampleRate_));
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

private:
    // Da qualunque formato e disposizione dei canali allo stereo float, alla
    // frequenza del brano. Un mono va uguale nei due canali.
    void openConverter()
    {
        AVChannelLayout layout{};
        if (codec_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&layout, codec_->ch_layout.nb_channels);
        else
            av_channel_layout_copy(&layout, &codec_->ch_layout);
        const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        SwrContext* swr = nullptr;
        const int status = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, nativeRate_, &layout,
                                               codec_->sample_fmt, nativeRate_, 0, nullptr);
        converter_.reset(swr);
        if (status >= 0 && layout.nb_channels == 1)
            av_opt_set_double(swr, "center_mix_level", 1.0, 0);
        av_channel_layout_uninit(&layout);
        if (status < 0 || swr_init(swr) < 0)
            throw DecoderError("conversione dei campioni impossibile");
    }

    // Frame stereo alla frequenza del brano; meno di frames solo alla fine.
    size_t decode(float* out, size_t frames)
    {
        size_t done = 0;
        while (done < frames) {
            const size_t available = pending_.size() / kChannels - pendingStart_;
            if (available > 0) {
                const size_t count = std::min(available, frames - done);
                std::memcpy(out + done * kChannels, pending_.data() + pendingStart_ * kChannels,
                            count * kChannels * sizeof(float));
                pendingStart_ += count;
                done += count;
                continue;
            }
            if (finished_ || !refill())
                break;
        }
        return done;
    }

    // Decodifica il prossimo pezzo in pending_; false alla fine del brano.
    bool refill()
    {
        pending_.clear();
        pendingStart_ = 0;
        while (pending_.empty()) {
            const int received = avcodec_receive_frame(codec_.get(), frame_.get());
            if (received == 0) {
                errors_ = 0;
                convert(*frame_);
                av_frame_unref(frame_.get());
            } else if (received == AVERROR(EAGAIN) || (received != AVERROR_EOF && ++errors_ < kMaxErrors)) {
                // Serve un altro pacchetto, o questo era rovinato: si va avanti.
                feed();
            } else {
                // La fine: quello che resta nel convertitore, e basta.
                finished_ = true;
                convert(nullptr, 0, 0);
                return !pending_.empty();
            }
        }
        return true;
    }

    // Un pacchetto della traccia al decoder, o la fine del file.
    void feed()
    {
        if (draining_) {
            avcodec_send_packet(codec_.get(), nullptr);
            return;
        }
        while (true) {
            if (av_read_frame(format_.get(), packet_.get()) < 0) {
                draining_ = true;
                avcodec_send_packet(codec_.get(), nullptr);
                return;
            }
            const bool ours = packet_->stream_index == stream_;
            // Un pacchetto rovinato si salta: il decoder riprende dal dopo.
            if (ours)
                avcodec_send_packet(codec_.get(), packet_.get());
            av_packet_unref(packet_.get());
            if (ours)
                return;
        }
    }

    void convert(const AVFrame& frame)
    {
        // Alcuni decoder danno a un frame il tempo del pacchetto che lo
        // completa, non del primo: nel WMA due frame dopo. Lo scarto si misura
        // sul primo frame del brano, che comincia al tempo d'inizio.
        const bool timed = frame.best_effort_timestamp != AV_NOPTS_VALUE;
        if (!ptsOffset_ && timed)
            ptsOffset_ = static_cast<double>(frame.best_effort_timestamp) * timeBase_ - startTime_;
        size_t skip = 0;
        if (seekTarget_) {
            // Dopo un seek si buttano i campioni prima del punto chiesto.
            if (timed) {
                const double start = static_cast<double>(frame.best_effort_timestamp) * timeBase_ - *ptsOffset_;
                const double late = (startTime_ + *seekTarget_ - start) * nativeRate_;
                skip = late > 0 ? static_cast<size_t>(std::llround(late)) : 0;
            }
            if (skip < static_cast<size_t>(frame.nb_samples))
                seekTarget_.reset();
        }
        convert(const_cast<const uint8_t**>(frame.extended_data), frame.nb_samples, skip);
    }

    // input nullptr svuota il convertitore.
    void convert(const uint8_t** input, int samples, size_t skip)
    {
        const int capacity = swr_get_out_samples(converter_.get(), samples);
        if (capacity <= 0)
            return;
        const size_t start = pending_.size();
        pending_.resize(start + static_cast<size_t>(capacity) * kChannels);
        auto* output = reinterpret_cast<uint8_t*>(pending_.data() + start);
        const int got = swr_convert(converter_.get(), &output, capacity, input, samples);
        const size_t kept = got > 0 ? static_cast<size_t>(got) : 0;
        pending_.resize(start + kept * kChannels);
        const size_t dropped = std::min(skip, kept);
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(start),
                       pending_.begin() + static_cast<std::ptrdiff_t>(start + dropped * kChannels));
    }

    std::vector<uint8_t> data_;
    MemoryInput input_;
    int sampleRate_;
    std::unique_ptr<AVIOContext, IoCloser> io_;
    std::unique_ptr<AVFormatContext, FormatCloser> format_;
    std::unique_ptr<AVCodecContext, CodecCloser> codec_;
    std::unique_ptr<SwrContext, ResamplerCloser> converter_;
    std::unique_ptr<AVPacket, PacketCloser> packet_;
    std::unique_ptr<AVFrame, FrameCloser> frame_;
    int stream_ = -1;
    int nativeRate_ = 0;
    double timeBase_ = 0;
    double startTime_ = 0;
    std::optional<double> ptsOffset_; // di quanto i tempi dei frame sono avanti
    int errors_ = 0; // errori del decoder di fila
    std::vector<float> pending_; // stereo alla frequenza del brano
    size_t pendingStart_ = 0;    // frame di pending_ già dati
    bool draining_ = false;
    bool finished_ = false;
    std::optional<double> seekTarget_;
    std::unique_ptr<Resampler> resampler_;
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

// Una funzione che non scrive niente: vgmstream, la prima volta che usa
// FFmpeg, rimette il livello dei messaggi agli errori.
FfmpegDecoderFactory::FfmpegDecoderFactory()
{
    av_log_set_callback([](void*, int, const char*, va_list) {});
}

std::string_view FfmpegDecoderFactory::description() const
{
    return "FFmpeg: file audio (M4A, ALAC, Opus, AIFF, WMA, Monkey's Audio, WavPack, Musepack, ...)";
}

int FfmpegDecoderFactory::probe(const SourceFile& file)
{
    return detect(file) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> FfmpegDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const AVInputFormat* container = detect(file);
    if (!container)
        throw DecoderError("non è un file audio di FFmpeg");
    return std::make_unique<FfmpegDecoder>(file, container, spec);
}

} // namespace mp
