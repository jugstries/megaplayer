#include "UadeDecoder.h"

#include "TfmxFiles.h"
#include "core/Platform.h"

#include <uade/uade.h>
// A differenza di uade.h, non dichiara le sue funzioni extern "C".
extern "C" {
#include <uade/eagleplayer.h>

// Le voci di Paula che si sentono, un bit per voce: in src/audio.c di UADE,
// con patches/uade-voice-mask.patch. uadecore gira nel nostro processo, e ha
// una sola istanza.
extern volatile int uade_audible_voices;
}

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 50;

struct ConfigDeleter {
    void operator()(uade_config* config) const { std::free(config); }
};

using StatePtr = std::unique_ptr<uade_state, UadeStateDeleter>;

// Vuota: la cartella scelta alla compilazione (UADE_CONFIG_BASE_DIR).
std::string gDataDirectory;

StatePtr newState(int sampleRate)
{
    std::unique_ptr<uade_config, ConfigDeleter> config(uade_new_config());
    if (!config)
        throw DecoderError("memoria esaurita");

    if (!gDataDirectory.empty())
        uade_config_set_option(config.get(), UC_BASE_DIR, gDataDirectory.c_str());
    uade_config_set_option(config.get(), UC_FREQUENCY, std::to_string(sampleRate).c_str());
    // Un sottobrano per volta: il passaggio al successivo lo decide il player.
    uade_config_set_option(config.get(), UC_ONE_SUBSONG, nullptr);

    // uade_new_state copia la configurazione: si può liberare subito dopo.
    StatePtr state(uade_new_state(config.get()));
    if (!state)
        throw DecoderError("impossibile inizializzare UADE (mancano i replayer? vedi scripts/build-deps.sh)");
    return state;
}

// Cartella che non esiste: i file passati a UADE dalla memoria hanno un nome
// qui dentro. UADE riconosce alcuni formati dal nome (prefisso "mod.",
// estensione) e, se il nome esiste su disco, ne usa la dimensione al posto di
// quella dei dati passati.
constexpr const char* kMemoryDirectory = "/megaplayer-memory/";

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Cosa passare a UADE per un file. I replayer dei formati in due file
// ricavano il nome del secondo dal primo, secondo la convenzione dell'Amiga:
// "mdat.Brano" vuole "smpl.Brano". Un TFMX salvato come "Brano.tfx" +
// "Brano.sam", o in un file solo (TFMX-MOD, TFMXPAK), si presenta quindi come
// "mdat.Brano", e loadCompanion() dà poi i campioni. Per i dati decompressi
// serve un nome che non esista, altrimenti varrebbe la dimensione del file
// compresso.
struct UadeInput {
    std::string name;
    std::vector<uint8_t> data;    // vuoto: UADE legge `name` dal disco
    std::vector<uint8_t> samples; // i campioni, se erano nello stesso file
    int subsong = -1;
    std::string title;
    std::string author;
};

UadeInput prepareInput(const SourceFile& file)
{
    UadeInput input;
    const std::string mdatName = kMemoryDirectory + std::string("mdat.") + file.path.stem().string();
    if (auto pair = unpackTfmxContainer(file.data)) {
        input.name = mdatName;
        input.data = std::move(pair->song);
        input.samples = std::move(pair->samples);
        input.subsong = pair->subsong;
        input.title = std::move(pair->title);
        input.author = std::move(pair->author);
    } else if (isTfmxSong(file.data) && lowercase(file.fileName()).rfind("mdat.", 0) != 0) {
        input.name = mdatName;
        input.data = file.data;
    } else if (file.wasUnpacked()) {
        input.name = kMemoryDirectory + file.fileName();
        input.data = file.data;
    } else {
        input.name = file.path.string();
    }
    return input;
}

// Nei moduli ci sono sempre caratteri di controllo, a partire dagli zeri
// delle intestazioni; nel testo solo spazi, a capo, il fine file del DOS e
// gli escape dei file ANSI.
bool looksLikeText(const std::vector<uint8_t>& data)
{
    const size_t checked = std::min<size_t>(data.size(), 4096);
    for (size_t i = 0; i < checked; ++i) {
        const uint8_t c = data[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1A && c != 0x1B)
            return false;
    }
    return true;
}

bool startsWith(const std::vector<uint8_t>& data, std::string_view prefix, size_t offset = 0)
{
    return data.size() >= offset + prefix.size()
        && std::equal(prefix.begin(), prefix.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                      [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; });
}

// UADE riconosce molti formati anche solo dal nome, e allora prende per un
// modulo qualunque file col prefisso giusto. Sui CD di moduli, nelle
// cartelle dei programmi, "CP.BAT" (un batch del DOS) e "CP.EXE" hanno il
// prefisso dei moduli Channel Player, l'icona del Workbench "HiP.info"
// quello di Jochen Hippel. Quando il nome è l'unico indizio, non sono moduli
// i file di testo e quelli che dall'intestazione sono altro.
bool clearlyNotAModule(const std::vector<uint8_t>& data)
{
    static constexpr std::string_view kSignatures[] = {
        std::string_view("\xE3\x10", 2), // icona del Workbench (DiskObject)
        "MZ",                            // eseguibile del DOS o di Windows
        "\x7F" "ELF",
        "PWAD",
        "IWAD",
        "\x89PNG",
        "\xFF\xD8\xFF", // JPEG
        "GIF8",
        "%PDF",
        "PK\x03\x04",
        "Rar!",
    };
    for (const std::string_view signature : kSignatures) {
        if (startsWith(data, signature))
            return true;
    }
    // Un'immagine IFF, o la configurazione di ProTracker ("PT2.3 Configuration File").
    if (startsWith(data, "FORM") && startsWith(data, "ILBM", 8))
        return true;
    if (startsWith(data, "PT") && startsWith(data, " Configuration File", 5))
        return true;
    return looksLikeText(data);
}

class UadeDecoder final : public Decoder {
public:
    UadeDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , directory_(file.path.parent_path())
        , input_(prepareInput(file))
    {
        uade_audible_voices = kAllVoices;
        resetState();

        play(input_.subsong);

        const struct uade_song_info* song = uade_get_song_info(state_.get());
        subsongMin_ = song->subsongs.min;
        info_.backend = "uade";
        info_.platform = platform::kAmiga;
        info_.format = song->formatname;
        // Alcuni replayer scrivono il formato come "type: Noisetracker 1.x".
        if (info_.format.rfind("type: ", 0) == 0)
            info_.format.erase(0, 6);
        info_.player = song->playername;
        info_.title = input_.title.empty() ? song->modulename : input_.title;
        info_.author = input_.author;
        info_.subsongCount = song->subsongs.max - song->subsongs.min + 1;
        info_.currentSubsong = song->subsongs.cur - subsongMin_;
        updateDuration();

        if (info_.format.empty()) {
            if (const auto* known = uade_file_ext_to_format_version(&song->detectioninfo))
                info_.format = known->format;
        }
    }

    // Il brano dopo, anche con un altro backend, si sente tutto.
    ~UadeDecoder() override { uade_audible_voices = kAllVoices; }

    // UADE ha l'indirizzo del decoder, per loadAmigaFile().
    UadeDecoder(const UadeDecoder&) = delete;
    UadeDecoder& operator=(const UadeDecoder&) = delete;

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

    // Le quattro voci di Paula: la 1 e la 4 vanno a sinistra, la 2 e la 3 a
    // destra.
    std::vector<std::string> channels() const override
    {
        return {"Voce 1 · sinistra", "Voce 2 · destra", "Voce 3 · destra", "Voce 4 · sinistra"};
    }

    // L'emulazione va avanti: riaccesa, la voce si sente da dove è arrivata.
    void setChannelMuted(int channel, bool muted) override
    {
        const int bit = 1 << channel;
        uade_audible_voices = muted ? (uade_audible_voices & ~bit) : (uade_audible_voices | bit);
    }

private:
    static constexpr int kAllVoices = 0xf;

    void resetState()
    {
        state_ = newState(sampleRate_);
        uade_set_amiga_loader(&UadeDecoder::loadAmigaFile, this, state_.get());
    }

    // Ogni file che il replayer chiede (il secondo file di una coppia, ma
    // anche librerie e configurazione): prima la ricerca di UADE, poi la
    // cartella del brano con i nomi alternativi della coppia.
    static struct uade_file* loadAmigaFile(const char* name, const char* playerdir, void* context, uade_state* state)
    {
        if (struct uade_file* found = uade_load_amiga_file(name, playerdir, state))
            return found;
        return static_cast<UadeDecoder*>(context)->loadCompanion(name);
    }

    struct uade_file* loadCompanion(const std::string& request) const
    {
        const size_t slash = request.find_last_of("/:");
        const std::string name = slash == std::string::npos ? request : request.substr(slash + 1);
        if (!input_.samples.empty() && lowercase(name).rfind("smpl.", 0) == 0)
            return uade_file(request.c_str(), input_.samples.data(), input_.samples.size());
        for (const std::string& candidate : pairedFileNames(name)) {
            const std::filesystem::path path = findIgnoringCase(directory_, candidate);
            if (path.empty())
                continue;
            try {
                // Anche il secondo file può essere impacchettato.
                const SourceFile companion = loadSourceFile(path);
                return uade_file(request.c_str(), companion.data.data(), companion.data.size());
            } catch (const std::exception&) {
            }
        }
        return nullptr;
    }

    void play(int subsong)
    {
        if (playing_) {
            playing_ = false;
            if (uade_stop(state_.get()) != 0)
                resetState();
        }

        const int result = input_.data.empty()
            ? uade_play(input_.name.c_str(), subsong, state_.get())
            : uade_play_from_buffer(input_.name.c_str(), input_.data.data(), input_.data.size(), subsong, state_.get());

        if (result < 0) {
            resetState();
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
    std::filesystem::path directory_;
    UadeInput input_;
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

void UadeDecoderFactory::setDataDirectory(const std::string& path)
{
    gDataDirectory = path;
}

std::string_view UadeDecoderFactory::description() const
{
    return "UADE: formati Amiga tramite i replayer originali in emulazione";
}

uade_state* UadeDecoderFactory::detectionState()
{
    if (!probeState_ && !probeStateFailed_) {
        try {
            // Per il riconoscimento la frequenza è irrilevante.
            probeState_ = newState(OutputSpec{}.sampleRate);
        } catch (const DecoderError&) {
            probeStateFailed_ = true;
        }
    }
    return probeState_.get();
}

int UadeDecoderFactory::probe(const SourceFile& file)
{
    uade_state* state = detectionState();
    if (!state)
        return 0;
    const UadeInput input = prepareInput(file);
    const std::vector<uint8_t>& data = input.data.empty() ? file.data : input.data;
    // Come uade_is_our_file_from_buffer, ma senza i file presi per moduli
    // solo dal nome che si vede che non lo sono.
    if (uade_is_rmc(reinterpret_cast<const char*>(data.data()), data.size()))
        return kProbeScore;
    struct uade_detection_info detection = {};
    uade_analyze_eagleplayer(&detection, data.data(), data.size(), input.name.c_str(), data.size(), state);
    if (!detection.ep || (!detection.content && clearlyNotAModule(data)))
        return 0;
    return kProbeScore;
}

std::optional<TrackInfo> UadeDecoderFactory::describe(const SourceFile& file)
{
    TrackInfo info;
    info.backend = "uade";
    info.platform = platform::kAmiga;
    info.subsongCount = 0;
    const UadeInput input = prepareInput(file);
    info.title = input.title;
    info.author = input.author;
    uade_state* state = detectionState();
    if (!state)
        return info;
    const std::vector<uint8_t>& data = input.data.empty() ? file.data : input.data;
    struct uade_detection_info detection = {};
    uade_analyze_eagleplayer(&detection, data.data(), data.size(), input.name.c_str(), data.size(), state);
    if (detection.ep) {
        info.player = detection.ep->playername;
        info.format = info.player;
        if (const auto* known = uade_file_ext_to_format_version(&detection)) {
            info.format = known->format;
            if (known->version)
                info.format += std::string(" ") + known->version;
        }
    }
    return info;
}

std::unique_ptr<Decoder> UadeDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<UadeDecoder>(file, spec);
}

} // namespace mp
