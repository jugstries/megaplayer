// Verifica l'esportazione: un WAV senza effetti è identico al bit al render
// del decoder, con i canali zittiti è silenzio, un MP3 riletto dura quanto
// il brano e ha lo stesso livello, e l'esportazione in un thread finisce o,
// annullata, non lascia il file.
// Uso: export_test DIR FILE [MOD]   (MOD: un modulo con canali, per il mute)

#include "core/AudioFileWriter.h"
#include "core/DecoderRegistry.h"
#include "core/Exporter.h"
#include "core/SourceFile.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kRate = 48000;

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::fprintf(stderr, "FALLITO: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<float> renderAll(mp::Decoder& decoder)
{
    std::vector<float> all;
    std::vector<float> chunk(1024 * mp::kChannels);
    while (const size_t got = decoder.render(chunk.data(), 1024))
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * mp::kChannels));
    return all;
}

double rms(const std::vector<float>& samples)
{
    double sum = 0;
    for (const float sample : samples)
        sum += static_cast<double>(sample) * sample;
    return samples.empty() ? 0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

std::unique_ptr<mp::Decoder> open(const mp::DecoderRegistry& registry, const fs::path& path)
{
    return registry.open(mp::loadSourceFile(path), {kRate});
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s DIR FILE [MOD]\n", argv[0]);
        return 2;
    }
    const fs::path directory = argv[1];
    const fs::path file = argv[2];
    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);

    // Senza MegaSound né mute: gli stessi campioni del decoder, a 16 bit.
    const std::vector<float> direct = renderAll(*open(registry, file));
    mp::ExportOptions options;
    options.output = directory / "export-test.wav";
    options.sampleRate = kRate;
    double written = 0;
    mp::exportTrack(*open(registry, file), options, [&](double seconds) { written = seconds; });
    const std::vector<float> back = renderAll(*open(registry, options.output));
    check(back.size() == direct.size(), "il WAV ha " + std::to_string(back.size()) + " campioni invece di "
                                            + std::to_string(direct.size()));
    double worst = 0;
    for (size_t i = 0; i < std::min(back.size(), direct.size()); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(back[i] - direct[i])));
    check(worst < 1.0 / 16384, "il WAV non è il render del decoder (scarto " + std::to_string(worst) + ")");
    check(std::fabs(written - static_cast<double>(direct.size() / mp::kChannels) / kRate) < 0.01,
          "avanzamento a " + std::to_string(written) + " s");

    // Un MP3 dura quanto il brano (il tag LAME toglie i campioni di troppo)
    // e ha lo stesso livello.
    if (mp::canWriteMp3()) {
        options.output = directory / "export-test.mp3";
        options.tags = {"Prova è giusta", "megaplayer", "Test"};
        mp::exportTrack(*open(registry, file), options);
        const std::vector<float> mp3 = renderAll(*open(registry, options.output));
        const double seconds = static_cast<double>(mp3.size() / mp::kChannels) / kRate;
        const double expected = static_cast<double>(direct.size() / mp::kChannels) / kRate;
        std::printf("MP3: %.3f s (WAV %.3f s), rms %.4f (WAV %.4f)\n", seconds, expected, rms(mp3), rms(direct));
        check(std::fabs(seconds - expected) < 0.06, "l'MP3 dura " + std::to_string(seconds) + " s");
        check(std::fabs(rms(mp3) - rms(direct)) < 0.1 * rms(direct), "l'MP3 ha un altro livello");
    }

    // Con tutti i canali zittiti il file è silenzio.
    if (argc > 3) {
        auto decoder = open(registry, argv[3]);
        for (size_t i = 0; i < decoder->channels().size(); ++i)
            decoder->setChannelMuted(static_cast<int>(i), true);
        options.output = directory / "export-muted.wav";
        options.maxSeconds = 2;
        mp::exportTrack(*decoder, options);
        options.maxSeconds.reset();
        const std::vector<float> muted = renderAll(*open(registry, options.output));
        check(!muted.empty() && rms(muted) < 0.001, "zittito, il file non è silenzio");
    }

    // In un thread: finisce; annullata, il file non resta.
    {
        options.output = directory / "export-thread.wav";
        mp::Exporter exporter(open(registry, file), options);
        for (int i = 0; i < 500 && exporter.status().running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const mp::Exporter::Status status = exporter.status();
        check(!status.running && !status.failed && fs::exists(options.output), "esportazione in un thread: " + status.error);
    }
    {
        options.output = directory / "export-cancelled.wav";
        options.maxSeconds = 3600; // abbastanza lunga da annullarla a metà
        mp::Exporter exporter(open(registry, file), options);
        exporter.cancel();
        for (int i = 0; i < 500 && exporter.status().running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const mp::Exporter::Status status = exporter.status();
        check(!status.running && (status.cancelled || !fs::exists(options.output)), "annullata, è ancora lì");
        check(!fs::exists(options.output) || !status.cancelled, "annullata, il file resta");
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
