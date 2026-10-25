#include "PsfDecoder.h"

#include "AudioTags.h"
#include "core/Platform.h"
#include "core/Resampler.h"
#include "core/SourceFile.h"

#include <bios.h>
#include <iop.h>
#include <psflib.h>
#include <psx.h>
#include <r3000.h>
#include <spu.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// OpenBIOS, incorporata da CMake (cmake/EmbedBinary.cmake).
extern "C" const unsigned char mp_openbios[];
extern "C" const unsigned long mp_openbios_size;

namespace mp {

namespace {

namespace fs = std::filesystem;

constexpr int kProbeScore = 80;
// Lo SPU della PlayStation suona a 44100 Hz.
constexpr int kNativeRate = 44100;
constexpr int kVoices = 24;
// Highly Experimental emula a blocchi, e la temporizzazione dipende da
// quanti campioni gli si chiedono per volta: sempre lo stesso numero, così
// dopo un seek il brano è quello che si sentirebbe arrivandoci suonando.
constexpr uint32_t kBlock = 1024;
// Senza i tag length e fade: tre minuti, poi cinque secondi di dissolvenza.
constexpr double kDefaultLength = 180;
constexpr double kDefaultFade = 5;

// Dove fermare l'avvio di OpenBIOS: la chiamata a startShell(7), a kernel
// pronto, la stessa dove Highly Experimental ferma la BIOS di Sony. Lì si
// scrive un salto su sé stesso, e dopo si carica il programma del brano.
constexpr uint32_t kShellCall = 0x6FAC;
constexpr uint32_t kJalStartShell = 0x0FF01D57;
constexpr uint32_t kBranchSelf = 0x1000FFFF; // b .

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

void putLe32(uint8_t* p, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(value >> (8 * i));
}

// Il nucleo di Highly Experimental ha un'immagine della BIOS e qualche
// tabella globali: si prepara una volta sola, poi ogni brano ha il suo stato.
void initCore()
{
    static std::once_flag once;
    std::call_once(once, [] {
        static std::vector<uint8_t> bios(mp_openbios, mp_openbios + mp_openbios_size);
        if (bios.size() != 512 * 1024 || le32(&bios[kShellCall]) != kJalStartShell)
            throw DecoderError("OpenBIOS non è quella attesa");
        putLe32(&bios[kShellCall], kBranchSelf);
        putLe32(&bios[kShellCall + 4], 0); // lo slot del salto: nop
        bios_set_image(bios.data(), static_cast<uint32>(bios.size()));
        psx_set_preboot(kShellCall, 0);
        if (psx_init() != 0)
            throw DecoderError("Highly Experimental non parte");
    });
}

std::string toUtf8(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path fromUtf8(const char* text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

// I file per psflib: il brano dalla memoria, le librerie (_lib) dal disco,
// accanto al brano anche con maiuscole e minuscole diverse.
struct OpenFile {
    std::vector<uint8_t> owned;
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t position = 0;
};

struct Files {
    const SourceFile* song;
    std::string songUri;
};

void* openFile(void* context, const char* uri)
{
    const auto* files = static_cast<const Files*>(context);
    auto file = std::make_unique<OpenFile>();
    if (files->songUri == uri) {
        file->data = files->song->data.data();
        file->size = files->song->data.size();
        return file.release();
    }
    fs::path path = fromUtf8(uri);
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        path = findIgnoringCase(path.parent_path(), path.filename().string());
    std::ifstream in(path, std::ios::binary);
    if (path.empty() || !in)
        return nullptr;
    file->owned.assign(std::istreambuf_iterator<char>(in), {});
    file->data = file->owned.data();
    file->size = file->owned.size();
    return file.release();
}

size_t readFile(void* buffer, size_t size, size_t count, void* handle)
{
    auto* file = static_cast<OpenFile*>(handle);
    if (size == 0)
        return 0;
    const size_t items = std::min(count, (file->size - file->position) / size);
    std::memcpy(buffer, file->data + file->position, items * size);
    file->position += items * size;
    return items;
}

int seekFile(void* handle, int64_t offset, int whence)
{
    auto* file = static_cast<OpenFile*>(handle);
    const int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<int64_t>(file->position)
                                                                    : static_cast<int64_t>(file->size);
    const int64_t target = base + offset;
    if (target < 0 || target > static_cast<int64_t>(file->size))
        return -1;
    file->position = static_cast<size_t>(target);
    return 0;
}

int closeFile(void* handle)
{
    delete static_cast<OpenFile*>(handle);
    return 0;
}

long tellFile(void* handle)
{
    return static_cast<long>(static_cast<OpenFile*>(handle)->position);
}

// "3:25.5", "1:02:03" o "95" in secondi.
std::optional<double> parseTime(std::string_view text)
{
    text = text.substr(0, text.find_first_of("\r\n"));
    double seconds = 0;
    bool any = false;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find(':', start), text.size());
        const std::string part(text.substr(start, end - start));
        char* rest = nullptr;
        const double value = std::strtod(part.c_str(), &rest);
        if (rest == part.c_str())
            return std::nullopt;
        seconds = seconds * 60 + value;
        any = true;
        start = end + 1;
    }
    if (!any)
        return std::nullopt;
    return seconds;
}

// Quello che psflib carica: i pezzi del programma (prima le librerie, poi il
// brano) e i tag. Si tiene tutto, per ripartire da capo a ogni seek
// all'indietro senza rileggere i file.
struct Loaded {
    struct Section {
        uint32_t address;
        std::vector<uint8_t> data;
    };
    std::vector<Section> sections;
    uint32_t pc = 0;
    uint32_t sp = 0;
    uint32_t refresh = 0; // 50 o 60 Hz; 0 se non si sa
    bool hasLibrary = false;
    std::optional<std::string> title, artist, length, fade, volume, refreshTag;
    std::string status;
};

int loadSection(void* context, const uint8_t* exe, size_t size, const uint8_t*, size_t)
{
    auto* loaded = static_cast<Loaded*>(context);
    // Un PS-X EXE: intestazione di 2 KB, poi il codice da caricare in RAM.
    if (size < 0x800 || std::memcmp(exe, "PS-X EXE", 8) != 0)
        return -1;
    const uint32_t address = le32(exe + 0x18) & 0x1FFFFF;
    const size_t length = size - 0x800;
    if (address < 0x10000 || length > 0x1F0000 || address + length > 0x200000)
        return -1;
    if (loaded->sections.empty()) {
        loaded->pc = le32(exe + 0x10);
        loaded->sp = le32(exe + 0x30);
    }
    // La regione, scritta nell'intestazione come sulle console.
    const std::string_view region(reinterpret_cast<const char*>(exe) + 0x4C, 0x800 - 0x4C);
    if (loaded->refresh == 0) {
        if (region.find("Europe") != std::string_view::npos)
            loaded->refresh = 50;
        else if (region.find("North America") != std::string_view::npos || region.find("Japan") != std::string_view::npos)
            loaded->refresh = 60;
    }
    loaded->sections.push_back({address, std::vector<uint8_t>(exe + 0x800, exe + size)});
    return 0;
}

// I tag del brano arrivano prima di quelli delle librerie: vince il primo.
int readTag(void* context, const char* name, const char* value)
{
    auto* loaded = static_cast<Loaded*>(context);
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto keep = [&](std::optional<std::string>& field) {
        if (!field)
            field = value;
    };
    if (key == "title")
        keep(loaded->title);
    else if (key == "artist")
        keep(loaded->artist);
    else if (key == "length")
        keep(loaded->length);
    else if (key == "fade")
        keep(loaded->fade);
    else if (key == "volume")
        keep(loaded->volume);
    else if (key == "_refresh")
        keep(loaded->refreshTag);
    else if (key.rfind("_lib", 0) == 0)
        loaded->hasLibrary = true;
    return 0;
}

void addStatus(void* context, const char* message)
{
    static_cast<Loaded*>(context)->status += message;
}

class PsfDecoder final : public Decoder {
public:
    PsfDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , resampler_(kNativeRate, spec.sampleRate)
    {
        initCore();
        Files files{&file, toUtf8(file.path)};
        const psf_file_callbacks callbacks = {"\\/", &files, openFile, readFile, seekFile, closeFile, tellFile};
        if (psf_load(files.songUri.c_str(), &callbacks, 1, loadSection, &loaded_, readTag, &loaded_, 1, addStatus,
                     &loaded_)
            <= 0) {
            std::string reason = loaded_.status.substr(0, loaded_.status.find('\n'));
            throw DecoderError(reason.empty() ? "PSF non valido" : reason);
        }
        if (loaded_.refreshTag) {
            const int refresh = std::atoi(loaded_.refreshTag->c_str());
            if (refresh == 50 || refresh == 60)
                loaded_.refresh = static_cast<uint32_t>(refresh);
        }
        if (loaded_.volume) {
            const double volume = std::atof(loaded_.volume->c_str());
            if (volume > 0)
                gain_ = static_cast<float>(volume);
        }

        info_.backend = "psf";
        info_.platform = platform::kPlayStation;
        info_.format = loaded_.hasLibrary ? "MiniPSF" : "PSF";
        info_.player = "Highly Experimental";
        info_.title = textToUtf8(loaded_.title.value_or(""));
        info_.author = textToUtf8(loaded_.artist.value_or(""));
        const std::optional<double> length = loaded_.length ? parseTime(*loaded_.length) : std::nullopt;
        const std::optional<double> fade = loaded_.fade ? parseTime(*loaded_.fade) : std::nullopt;
        const double lengthSeconds = length.value_or(kDefaultLength);
        const double fadeSeconds = std::max(0.0, fade.value_or(kDefaultFade));
        fadeStart_ = static_cast<size_t>(lengthSeconds * sampleRate_);
        total_ = fadeStart_ + static_cast<size_t>(fadeSeconds * sampleRate_);
        info_.duration = static_cast<double>(total_) / sampleRate_;
        state_.reset(new uint8_t[psx_get_state_size(1)]);
        restart();
    }

    const TrackInfo& info() const override { return info_; }

    // Un PSF ha un brano solo.
    void selectSubsong(int /*index*/) override { restart(); }

    size_t render(float* out, size_t frames) override
    {
        frames = std::min(frames, total_ - std::min(total_, rendered_));
        const size_t got = resampler_.render(out, frames, [this](float* buffer, size_t count) {
            return emulate(buffer, count);
        });
        for (size_t i = 0; i < got; ++i) {
            const size_t frame = rendered_ + i;
            float gain = gain_;
            if (frame >= fadeStart_)
                gain *= static_cast<float>(total_ - frame) / static_cast<float>(total_ - fadeStart_);
            out[i * kChannels] *= gain;
            out[i * kChannels + 1] *= gain;
        }
        rendered_ += got;
        return got;
    }

    bool canSeek() const override { return true; }

    // L'emulatore non torna indietro: per un punto già passato si riparte da
    // capo; poi si emula senza produrre campioni fino al punto richiesto.
    void seek(double seconds) override
    {
        const auto target =
            static_cast<size_t>(std::clamp(seconds, 0.0, static_cast<double>(total_) / sampleRate_) * sampleRate_);
        const auto nativeTarget = static_cast<uint64_t>(static_cast<double>(target) * kNativeRate / sampleRate_);
        // L'ultimo blocco è ancora in memoria: si torna indietro solo da lì.
        if (nativeTarget < produced_ - blockLength_)
            restart();
        if (nativeTarget >= produced_) {
            // Con i campioni, scartati: senza, lo SPU salta il riverbero, che
            // poi per un paio di secondi suonerebbe diverso.
            while (produced_ + kBlock <= nativeTarget && !failed_)
                emulateBlock();
            emulateBlock();
        }
        const uint64_t blockStart = produced_ - blockLength_;
        blockPosition_ = static_cast<size_t>(std::min<uint64_t>(nativeTarget - blockStart, blockLength_));
        resampler_.reset();
        rendered_ = target;
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    // Le 24 voci dello SPU.
    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int voice = 1; voice <= kVoices; ++voice)
            names.push_back("Voce " + std::to_string(voice));
        return names;
    }

    void setChannelMuted(int channel, bool muted) override
    {
        const uint32_t bit = 1u << channel;
        mutedMask_ = muted ? mutedMask_ | bit : mutedMask_ & ~bit;
        applyMutes();
    }

private:
    // Lo stato appena avviata la BIOS, con il programma del brano in RAM.
    void restart()
    {
        psx_clear_state(state_.get(), 1);
        void* iop = psx_get_iop_state(state_.get());
        for (const Loaded::Section& section : loaded_.sections)
            iop_upload_to_ram(iop, section.address, section.data.data(), static_cast<uint32>(section.data.size()));
        r3000_setreg(iop_get_r3000_state(iop), R3000_REG_PC, loaded_.pc);
        r3000_setreg(iop_get_r3000_state(iop), R3000_REG_GEN + 29, loaded_.sp);
        if (loaded_.refresh)
            psx_set_refresh(state_.get(), loaded_.refresh);
        // Come foo_psf: la temporizzazione più fedele.
        iop_set_compat(iop, IOP_COMPAT_HARSH);
        applyMutes();
        resampler_.reset();
        rendered_ = 0;
        produced_ = 0;
        blockLength_ = blockPosition_ = 0;
        failed_ = false;
        info_.currentSubsong = 0;
    }

    void applyMutes()
    {
        void* spu = iop_get_spu_state(psx_get_iop_state(state_.get()));
        for (int voice = 0; voice < kVoices; ++voice)
            spu_enable_mute(spu, static_cast<uint8>(voice), (mutedMask_ >> voice) & 1);
    }

    // Un blocco di kBlock campioni a 44100 Hz in block_; meno solo se
    // l'emulatore si è fermato.
    void emulateBlock()
    {
        uint32_t done = 0;
        while (done < kBlock && !failed_) {
            uint32 got = kBlock - done;
            if (psx_execute(state_.get(), 0x7FFFFFFF, block_.data() + done * kChannels, &got, 0) < 0)
                failed_ = true;
            done += got;
        }
        produced_ += done;
        blockLength_ = done;
        blockPosition_ = 0;
    }

    // Campioni a 44100 Hz dall'emulatore; meno di count solo se si è fermato.
    size_t emulate(float* out, size_t count)
    {
        size_t done = 0;
        while (done < count) {
            if (blockPosition_ == blockLength_) {
                emulateBlock();
                if (blockLength_ == 0)
                    break;
            }
            const size_t take = std::min(count - done, blockLength_ - blockPosition_);
            for (size_t i = 0; i < take * kChannels; ++i)
                out[done * kChannels + i] = block_[blockPosition_ * kChannels + i] / 32768.0f;
            blockPosition_ += take;
            done += take;
        }
        return done;
    }

    int sampleRate_;
    Loaded loaded_;
    std::unique_ptr<uint8_t[]> state_;
    Resampler resampler_;
    float gain_ = 1;          // il tag volume
    size_t fadeStart_ = 0;    // frame d'uscita
    size_t total_ = 0;        // frame d'uscita, dissolvenza compresa
    size_t rendered_ = 0;     // frame d'uscita dall'inizio
    // L'ultimo blocco emulato, fino a produced_ frame a 44100 Hz dall'inizio.
    std::vector<int16_t> block_ = std::vector<int16_t>(kBlock * kChannels);
    size_t blockLength_ = 0;
    size_t blockPosition_ = 0; // i frame del blocco già usati
    uint64_t produced_ = 0;
    bool failed_ = false;     // l'emulatore si è fermato su un errore
    uint32_t mutedMask_ = 0;
    TrackInfo info_;
};

bool hasExtension(const SourceFile& file, std::string_view wanted)
{
    std::string extension = file.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == wanted;
}

} // namespace

std::string_view PsfDecoderFactory::description() const
{
    return "Highly Experimental: PlayStation (PSF, MiniPSF) con OpenBIOS";
}

int PsfDecoderFactory::probe(const SourceFile& file)
{
    // Le librerie dei MiniPSF da sole non suonano.
    if (file.data.size() < 16 || std::memcmp(file.data.data(), "PSF\x01", 4) != 0 || hasExtension(file, ".psflib"))
        return 0;
    return kProbeScore;
}

std::unique_ptr<Decoder> PsfDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<PsfDecoder>(file, spec);
}

} // namespace mp
