#pragma once

#include "core/Decoder.h"
#include "core/Resampler.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mp {

// I formati della famiglia PSF (PSF, SSF, DSF, QSF, ...): il programma
// originale della console, compresso, con i tag e le librerie (_lib) che
// psflib ricompone. Qui c'è quello che hanno in comune; ogni decoder ci mette
// il suo emulatore.

// I tag del file e delle sue librerie: quando un tag c'è in più file vince
// il primo, cioè il brano.
struct XsfTags {
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> length;
    std::optional<std::string> fade;
    std::optional<std::string> volume;
    std::vector<std::pair<std::string, std::string>> others; // nome in minuscolo
    bool hasLibrary = false;

    const std::string* find(std::string_view name) const;
};

// Riceve i pezzi del programma: prima le librerie, poi il brano. false se un
// pezzo non va.
using XsfSection = std::function<bool(const uint8_t* exe, size_t size, const uint8_t* reserved, size_t reservedSize)>;

// Carica con psflib il brano e le sue librerie, cercate accanto al brano
// anche con maiuscole e minuscole diverse. Lancia DecoderError.
void loadXsf(const SourceFile& file, uint8_t version, const XsfSection& section, XsfTags& tags);

// Un file di questa versione, e non una libreria che da sola non suona
// (.psflib, .ssflib, ...).
bool isXsf(const SourceFile& file, uint8_t version);

// La base dei decoder: l'emulatore produce stereo a 16 bit alla sua
// frequenza, a blocchi sempre uguali (la temporizzazione di questi emulatori
// dipende da quanto gli si chiede per volta: così dopo un seek il brano è
// quello che si sentirebbe arrivandoci suonando); qui si convertono alla
// frequenza d'uscita, con il volume e la dissolvenza dei tag.
class XsfDecoder : public Decoder {
public:
    const TrackInfo& info() const override { return info_; }
    void selectSubsong(int index) override;
    size_t render(float* out, size_t frames) override;
    bool canSeek() const override { return true; }
    void seek(double seconds) override;
    double position() const override;
    std::vector<std::string> channels() const override { return channelNames_; }
    void setChannelMuted(int channel, bool muted) override;

protected:
    XsfDecoder(const OutputSpec& spec, int nativeRate, const XsfTags& tags);

    // Da chiamare alla fine del costruttore della sottoclasse.
    void start();

    // L'emulatore da capo, con il brano caricato e i mute di mutedMask().
    virtual void reset() = 0;
    // Scrive in buffer frames frame; meno solo se l'emulatore si è fermato.
    virtual size_t emulate(int16_t* buffer, size_t frames) = 0;
    // Applica all'emulatore i mute di mutedMask().
    virtual void applyMutes() {}

    uint64_t mutedMask() const { return mutedMask_; }

    TrackInfo info_;
    std::vector<std::string> channelNames_;

private:
    void restart();
    void emulateBlock();
    size_t pull(float* out, size_t frames);

    int sampleRate_;
    int nativeRate_;
    Resampler resampler_;
    float gain_ = 1;       // il tag volume
    size_t fadeStart_ = 0; // frame d'uscita
    size_t total_ = 0;     // frame d'uscita, dissolvenza compresa
    size_t rendered_ = 0;  // frame d'uscita dall'inizio
    // L'ultimo blocco emulato, che finisce a produced_ frame dall'inizio.
    std::vector<int16_t> block_;
    size_t blockLength_ = 0;
    size_t blockPosition_ = 0; // i frame del blocco già usati
    uint64_t produced_ = 0;
    bool failed_ = false;      // l'emulatore si è fermato
    uint64_t mutedMask_ = 0;
};

} // namespace mp
