#include "GmeDecoder.h"

#include "core/Platform.h"

#include <gme/gme.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

namespace mp {

namespace {

// I formati di gme hanno un'intestazione inconfondibile; solo i GYM più
// vecchi si riconoscono dall'estensione, e valgono meno.
constexpr int kProbeScore = 80;
constexpr int kExtensionScore = 30;
// Dissolvenza in chiusura quando il file non ne indica una.
constexpr int kFadeMs = 8000;

// La piattaforma dal tipo di file. I KSS sono quasi tutti MSX: quelli del
// Master System lo scrivono nelle informazioni del brano (vedi
// selectSubsong()).
const char* platformOf(std::string_view extension)
{
    if (extension == "NSF" || extension == "NSFE")
        return platform::kNes;
    if (extension == "SPC")
        return platform::kSnes;
    if (extension == "GBS")
        return platform::kGameBoy;
    if (extension == "VGM" || extension == "VGZ" || extension == "GYM")
        return platform::kSega;
    if (extension == "HES")
        return platform::kPcEngine;
    if (extension == "AY")
        return platform::kZxSpectrum;
    if (extension == "SAP")
        return platform::kAtari8;
    return platform::kMsx;
}

struct InfoDeleter {
    void operator()(gme_info_t* info) const { gme_free_info(info); }
};

class GmeDecoder final : public Decoder {
public:
    GmeDecoder(const SourceFile& file, const OutputSpec& spec)
    {
        // gme_open_data copia i dati.
        if (const char* error = gme_open_data(file.data.data(), static_cast<long>(file.data.size()), &emu_,
                                              spec.sampleRate))
            throw DecoderError(error);
        // Più fedele dove l'emulatore lo permette (SPC), a un costo minimo.
        gme_enable_accuracy(emu_, 1);

        const gme_type_t type = gme_type(emu_);
        info_.backend = "gme";
        info_.format = gme_type_extension(type);
        info_.player = gme_type_system(type);
        info_.platform = platformOf(info_.format);
        info_.subsongCount = std::max(1, gme_track_count(emu_));
        selectSubsong(0);
    }

    ~GmeDecoder() override { gme_delete(emu_); }

    GmeDecoder(const GmeDecoder&) = delete;
    GmeDecoder& operator=(const GmeDecoder&) = delete;

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        if (const char* error = gme_start_track(emu_, index))
            throw DecoderError(error);
        info_.currentSubsong = index;

        gme_info_t* raw = nullptr;
        if (gme_track_info(emu_, &raw, index) == nullptr && raw != nullptr) {
            const std::unique_ptr<gme_info_t, InfoDeleter> track(raw);
            info_.title = track->song[0] ? track->song : track->game;
            info_.author = track->author;
            if (track->system[0] && std::strstr(track->system, "Sega") != nullptr) {
                info_.player = track->system;
                info_.platform = platform::kSega;
            }
            // La durata del file, oppure intro più due giri del ritornello,
            // oppure due minuti e mezzo; poi la dissolvenza. Così ogni brano
            // finisce, anche quelli che girerebbero per sempre.
            const int fade = track->fade_length > 0 ? track->fade_length : kFadeMs;
            gme_set_fade_msecs(emu_, track->play_length, fade);
            info_.duration = (track->play_length + fade) / 1000.0;
        }
    }

    size_t render(float* out, size_t frames) override
    {
        if (gme_track_ended(emu_))
            return 0;
        samples_.resize(frames * kChannels);
        if (gme_play(emu_, static_cast<int>(samples_.size()), samples_.data()) != nullptr)
            return 0;
        for (size_t i = 0; i < samples_.size(); ++i)
            out[i] = samples_[i] / 32768.0f;
        return frames;
    }

    bool canSeek() const override { return true; }

    // gme emula fino al punto richiesto: indietro riparte dall'inizio.
    void seek(double seconds) override { gme_seek(emu_, static_cast<int>(std::lround(std::max(0.0, seconds) * 1000))); }

    double position() const override { return gme_tell(emu_) / 1000.0; }

private:
    Music_Emu* emu_ = nullptr;
    std::vector<short> samples_;
    TrackInfo info_;
};

} // namespace

std::string_view GmeDecoderFactory::description() const
{
    return "game-music-emu: console (NSF, SPC, GBS, VGM/VGZ, HES, KSS, AY, SAP, GYM)";
}

int GmeDecoderFactory::probe(const SourceFile& file)
{
    if (file.data.size() >= 4 && *gme_identify_header(file.data.data()) != '\0')
        return kProbeScore;
    // gme_identify_extension accetta il nome intero e ne guarda l'estensione.
    const std::string name = file.fileName();
    return gme_identify_extension(name.c_str()) != nullptr ? kExtensionScore : 0;
}

std::unique_ptr<Decoder> GmeDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<GmeDecoder>(file, spec);
}

} // namespace mp
