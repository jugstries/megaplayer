#pragma once

#include "SourceFile.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mp {

// Tutti i decoder producono float stereo interleaved alla frequenza richiesta.
constexpr int kChannels = 2;

struct OutputSpec {
    int sampleRate = 48000;
};

struct TrackInfo {
    std::string backend;            // nome del backend, es. "uade"
    std::string format;             // formato leggibile, es. "ProTracker"
    std::string player;             // replayer usato, se il backend lo dice
    std::string platform;           // identificativo da Platform.h, es. "amiga"
    std::string title;
    std::string author;
    int subsongCount = 1;
    int currentSubsong = 0;         // base 0
    std::optional<double> duration; // secondi, del sottobrano corrente
};

class DecoderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Decoder {
public:
    virtual ~Decoder() = default;

    virtual const TrackInfo& info() const = 0;

    // Seleziona il sottobrano (base 0) e riparte dall'inizio.
    virtual void selectSubsong(int index) = 0;

    // Scrive fino a `frames` frame stereo in `out` e restituisce quanti ne ha
    // scritti; 0 significa fine del sottobrano.
    virtual size_t render(float* out, size_t frames) = 0;

    virtual bool canSeek() const { return false; }
    // Posizione assoluta nel sottobrano corrente, in secondi.
    virtual void seek(double /*seconds*/) {}

    // Posizione di decodifica nel sottobrano corrente, in secondi.
    virtual double position() const = 0;
};

class DecoderFactory {
public:
    virtual ~DecoderFactory() = default;

    virtual std::string_view name() const = 0;
    virtual std::string_view description() const = 0;

    // Quanto il backend è adatto al file: 0 se non lo riconosce, valori più
    // alti per i backend da preferire.
    virtual int probe(const SourceFile& file) = 0;

    // Lancia DecoderError se il file non si apre.
    virtual std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) = 0;

    // Le informazioni sul file senza aprirlo, per la libreria: nullopt se
    // per averle bisogna aprire il decoder (il caso normale). UADE invece
    // riconosce solo il formato, perché l'emulatore ha una sola istanza e
    // servirebbe alla riproduzione; subsongCount 0 vuol dire sconosciuti.
    virtual std::optional<TrackInfo> describe(const SourceFile& /*file*/) { return std::nullopt; }
};

} // namespace mp
