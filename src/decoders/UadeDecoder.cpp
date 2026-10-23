#include "UadeDecoder.h"

#include <uade/uade.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 50;

struct ConfigDeleter {
    void operator()(uade_config* config) const { std::free(config); }
};

using StatePtr = std::unique_ptr<uade_state, UadeStateDeleter>;

StatePtr newState(int sampleRate)
{
    std::unique_ptr<uade_config, ConfigDeleter> config(uade_new_config());
    if (!config)
        throw DecoderError("memoria esaurita");

    uade_config_set_option(config.get(), UC_FREQUENCY, std::to_string(sampleRate).c_str());
    // Un sottobrano per volta: il passaggio al successivo lo decide il player.
    uade_config_set_option(config.get(), UC_ONE_SUBSONG, nullptr);

    // uade_new_state copia la configurazione: si può liberare subito dopo.
    StatePtr state(uade_new_state(config.get()));
    if (!state)
        throw DecoderError("impossibile avviare uadecore (UADE è installato?)");
    return state;
}

// Il nome con cui presentare il file a UADE. UADE riconosce alcuni formati
// dal nome (prefisso "mod.", estensione) e, se il nome esiste su disco, ne
// usa la dimensione al posto di quella dei dati passati. Per i dati
// decompressi serve quindi un nome che non esista: altrimenti varrebbe la
// dimensione del file compresso.
std::string uadeName(const SourceFile& file)
{
    if (!file.wasUnpacked())
        return file.path.string();
    return "/megaplayer-unpacked/" + file.fileName();
}

class UadeDecoder final : public Decoder {
public:
    UadeDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , name_(uadeName(file))
        , state_(newState(spec.sampleRate))
    {
        // Da file su disco UADE carica da solo i file accessori dei formati
        // in più parti (es. TFMX mdat+smpl). I dati decompressi invece
        // esistono solo in memoria.
        if (file.wasUnpacked())
            unpackedData_ = file.data;

        play(-1);

        const struct uade_song_info* song = uade_get_song_info(state_.get());
        subsongMin_ = song->subsongs.min;
        info_.backend = "uade";
        info_.format = song->formatname;
        // Alcuni replayer scrivono il formato come "type: Noisetracker 1.x".
        if (info_.format.rfind("type: ", 0) == 0)
            info_.format.erase(0, 6);
        info_.player = song->playername;
        info_.title = song->modulename;
        info_.subsongCount = song->subsongs.max - song->subsongs.min + 1;
        info_.currentSubsong = song->subsongs.cur - subsongMin_;
        updateDuration();

        if (info_.format.empty()) {
            if (const auto* known = uade_file_ext_to_format_version(&song->detectioninfo))
                info_.format = known->format;
        }
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        play(subsongMin_ + index);
        info_.currentSubsong = index;
        updateDuration();
    }

    size_t render(float* out, size_t frames) override
    {
        if (ended_)
            return 0;

        samples_.resize(frames * kChannels);
        const ssize_t bytes = uade_read(samples_.data(), frames * UADE_BYTES_PER_FRAME, state_.get());
        if (bytes < 0)
            throw DecoderError("uadecore si è interrotto durante la riproduzione");
        if (bytes == 0) {
            ended_ = true;
            return 0;
        }

        const size_t got = static_cast<size_t>(bytes) / UADE_BYTES_PER_FRAME;
        for (size_t i = 0; i < got * kChannels; ++i)
            out[i] = samples_[i] / 32768.0f;
        return got;
    }

    bool canSeek() const override { return true; }

    void seek(double seconds) override
    {
        // Dopo la fine del sottobrano uadecore si è fermato: si riparte.
        if (ended_)
            play(subsongMin_ + info_.currentSubsong);
        // UADE non ha un vero seek: riavvia il sottobrano se serve e lo fa
        // avanzare a vuoto fino al punto richiesto, durante le uade_read().
        uade_seek(UADE_SEEK_SUBSONG_RELATIVE, seconds < 0 ? 0 : seconds, -1, state_.get());
    }

    double position() const override
    {
        const struct uade_song_info* song = uade_get_song_info(state_.get());
        return static_cast<double>(song->subsongbytes) / (sampleRate_ * UADE_BYTES_PER_FRAME);
    }

private:
    void play(int subsong)
    {
        if (playing_) {
            playing_ = false;
            if (uade_stop(state_.get()) != 0)
                state_ = newState(sampleRate_);
        }

        const int result = unpackedData_.empty()
            ? uade_play(name_.c_str(), subsong, state_.get())
            : uade_play_from_buffer(name_.c_str(), unpackedData_.data(), unpackedData_.size(), subsong, state_.get());

        if (result < 0) {
            state_ = newState(sampleRate_);
            throw DecoderError("errore fatale di uadecore");
        }
        if (result == 0)
            throw DecoderError("nessun replayer UADE accetta il file");

        playing_ = true;
        ended_ = false;
    }

    void updateDuration()
    {
        // La durata viene dal database di UADE (~/.uade/contentdb), che ne
        // registra una sola per file: quella dell'ultimo sottobrano arrivato
        // in fondo. Con più sottobrani non vale per quello corrente.
        if (info_.subsongCount > 1) {
            info_.duration.reset();
            return;
        }
        const double duration = uade_get_song_info(state_.get())->duration;
        info_.duration = duration > 0 ? std::optional<double>(duration) : std::nullopt;
    }

    int sampleRate_;
    std::string name_;
    std::vector<uint8_t> unpackedData_;
    StatePtr state_;
    std::vector<int16_t> samples_;
    TrackInfo info_;
    int subsongMin_ = 0;
    bool playing_ = false;
    bool ended_ = false;
};

} // namespace

void UadeStateDeleter::operator()(uade_state* state) const
{
    uade_cleanup_state(state);
}

UadeDecoderFactory::UadeDecoderFactory()
{
    // Se un processo uadecore muore, la scrittura sulla sua pipe genera
    // SIGPIPE: va ignorato, l'errore arriva comunque da libuade.
    std::signal(SIGPIPE, SIG_IGN);
}

std::string_view UadeDecoderFactory::description() const
{
    return "UADE: formati Amiga tramite i replayer originali in emulazione";
}

int UadeDecoderFactory::probe(const SourceFile& file)
{
    if (!probeState_ && !probeStateFailed_) {
        try {
            // Per il riconoscimento la frequenza è irrilevante.
            probeState_ = newState(OutputSpec{}.sampleRate);
        } catch (const DecoderError&) {
            probeStateFailed_ = true;
        }
    }
    if (!probeState_)
        return 0;

    const std::string name = uadeName(file);
    const int ours = uade_is_our_file_from_buffer(name.c_str(), file.data.data(), file.data.size(),
                                                  probeState_.get());
    return ours ? kProbeScore : 0;
}

std::unique_ptr<Decoder> UadeDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<UadeDecoder>(file, spec);
}

} // namespace mp
