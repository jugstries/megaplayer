#pragma once

#include "AudioFileWriter.h"
#include "Decoder.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace mp {

struct ExportOptions {
    std::filesystem::path output; // .wav o .mp3
    int sampleRate = 48000;       // quella del decoder
    float megaSound = 0;          // la manopola, 0..1 (0: spenta)
    // Il limite per i brani che non dicono quando finiscono; senza, quello
    // di kExportLimit.
    std::optional<double> maxSeconds;
    FileTags tags;
};

// Il limite dei brani senza durata che non finiscono da soli.
constexpr double kExportLimit = 15 * 60;

// Esporta in options.output il brano di [decoder], che ha già il sottobrano e
// i canali zittiti che servono, con la MegaSound, fino alla fine del brano o
// a options.maxSeconds. [progress] riceve i secondi scritti; quando
// [cancelled] dice di sì si ferma e cancella il file. Lancia
// std::runtime_error, e allora il file non resta.
void exportTrack(Decoder& decoder, const ExportOptions& options, const std::function<void(double)>& progress = {},
                 const std::function<bool()>& cancelled = {});

// exportTrack in un thread suo, per le interfacce grafiche: si chiede lo
// stato quando si vuole, e si può annullare.
class Exporter {
public:
    struct Status {
        bool running = false;
        bool failed = false;
        bool cancelled = false;
        double seconds = 0; // scritti finora
        std::optional<double> total;
        std::string error;
    };

    Exporter(std::unique_ptr<Decoder> decoder, ExportOptions options);
    ~Exporter(); // annulla e aspetta

    Exporter(const Exporter&) = delete;
    Exporter& operator=(const Exporter&) = delete;

    Status status() const;
    void cancel() { cancel_ = true; }

private:
    std::unique_ptr<Decoder> decoder_;
    ExportOptions options_;
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
    Status status_;
    std::thread thread_;
};

} // namespace mp
