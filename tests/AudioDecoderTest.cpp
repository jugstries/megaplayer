// Verifica i backend miniaudio e ffmpeg sui file di tests/data (vedi
// make_audio_fixtures.sh): riconoscimento dal contenuto, titolo e autore,
// durata, ricampionamento a 48 kHz con i canali al posto giusto, seek.
// Uso: audio_decoder_test CARTELLA_DATI

#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

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

// Frequenza dominante di un canale contando gli attraversamenti dello zero
// nel secondo frame per frame.
double frequency(const std::vector<float>& frames, int channel, size_t from, size_t count)
{
    int crossings = 0;
    for (size_t i = from + 1; i < from + count; ++i) {
        if ((frames[(i - 1) * 2 + channel] < 0) != (frames[i * 2 + channel] < 0))
            ++crossings;
    }
    return crossings / 2.0 * kRate / static_cast<double>(count);
}

std::vector<float> renderAll(mp::Decoder& decoder)
{
    std::vector<float> all;
    std::vector<float> chunk(1024 * 2);
    while (const size_t got = decoder.render(chunk.data(), 1024))
        all.insert(all.end(), chunk.begin(), chunk.begin() + got * 2);
    return all;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s CARTELLA_DATI\n", argv[0]);
        return 2;
    }
    const std::filesystem::path data = argv[1];
    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);

    bool withFfmpeg = false;
    for (const auto& factory : registry.factories())
        withFfmpeg = withFfmpeg || factory->name() == "ffmpeg";

    // length: i secondi che escono. Il decoder WMA di FFmpeg tiene per sé
    // l'ultimo frame da 2048 campioni, come nella sua riga di comando.
    const struct {
        const char* file;
        const char* format;
        const char* backend;
        double length = 1.0;
    } cases[] = {
        {"tono.wav", "WAV", "miniaudio"},
        {"tono.mp3", "MP3", "miniaudio"},
        {"tono.flac", "FLAC", "miniaudio"},
        {"tono.ogg", "Ogg Vorbis", "miniaudio"},
        {"tono.m4a", "AAC (M4A)", "ffmpeg"},
        {"tono-alac.m4a", "ALAC (M4A)", "ffmpeg"},
        {"tono.opus", "Opus (Ogg)", "ffmpeg"},
        {"tono.aiff", "AIFF", "ffmpeg"},
        {"tono.wma", "WMA", "ffmpeg", 0.96},
        {"tono.wv", "WavPack", "ffmpeg"},
    };

    for (const auto& c : cases) {
        const std::string name = c.file;
        if (std::string(c.backend) == "ffmpeg" && !withFfmpeg)
            continue;
        const mp::SourceFile file = mp::loadSourceFile(data / c.file);
        const auto candidates = registry.probe(file);
        check(!candidates.empty() && candidates.front().factory->name() == c.backend,
              name + ": non riconosciuto da " + c.backend);

        auto decoder = registry.open(file, mp::OutputSpec{kRate});
        const mp::TrackInfo& info = decoder->info();
        check(info.format == c.format, name + ": formato " + info.format);
        check(info.title == "Prova \xc3\xa0\xc3\xa8\xc3\xac", name + ": titolo \"" + info.title + "\"");
        check(info.author == "Megaplayer", name + ": autore \"" + info.author + "\"");
        check(info.duration && std::fabs(*info.duration - 1.0) < 0.06, name + ": durata sbagliata");

        const std::vector<float> frames = renderAll(*decoder);
        const size_t count = frames.size() / 2;
        const double left = frequency(frames, 0, kRate / 4, kRate / 2);
        const double right = frequency(frames, 1, kRate / 4, kRate / 2);
        std::printf("%-14s %-11s %zu frame, sinistra %.0f Hz, destra %.0f Hz\n", c.file, info.format.c_str(), count,
                    left, right);
        // L'MP3 e il WMA hanno il ritardo dell'encoder e il riempimento
        // dell'ultimo frame.
        check(count > kRate * (c.length - 0.03) && count < kRate * (c.length + 0.06), name + ": numero di frame sbagliato");
        check(std::fabs(left - 440) < 5 && std::fabs(right - 660) < 5, name + ": frequenze o canali sbagliati");

        decoder->seek(0.5);
        check(std::fabs(decoder->position() - 0.5) < 0.01, name + ": posizione dopo il seek");
        const size_t rest = renderAll(*decoder).size() / 2;
        check(rest > kRate * (c.length - 0.55) && rest < kRate * (c.length - 0.44),
              name + ": seek a metà, restano " + std::to_string(rest));

        if (std::string(c.backend) == "ffmpeg") {
            // Dopo un seek gli stessi campioni che dall'inizio, a parte il
            // primo assestarsi del ricampionamento. A 0,4 s, un campione
            // intero a tutte le frequenze dei file.
            decoder->seek(0.4);
            const std::vector<float> after = renderAll(*decoder);
            const size_t from = static_cast<size_t>(kRate * 0.4) * 2;
            double worst = 1;
            if (after.size() > 2000 * 2 && frames.size() > from + 2000 * 2) {
                worst = 0;
                for (size_t i = 500 * 2; i < 2000 * 2; ++i)
                    worst = std::max(worst, std::fabs(static_cast<double>(after[i] - frames[from + i])));
            }
            check(worst < 0.002, name + ": dopo il seek campioni diversi, scarto " + std::to_string(worst));
        }
    }

    {
        // Un MP3 senza tag e con un altro nome si riconosce dai frame.
        mp::SourceFile file = mp::loadSourceFile(data / "tono.mp3");
        const size_t tag = 10 + ((file.data[6] & 0x7f) << 21 | (file.data[7] & 0x7f) << 14
                                 | (file.data[8] & 0x7f) << 7 | (file.data[9] & 0x7f));
        file.data.erase(file.data.begin(), file.data.begin() + static_cast<std::ptrdiff_t>(tag));
        file.path = "senza-nome.bin";
        const auto candidates = registry.probe(file);
        check(!candidates.empty() && candidates.front().factory->name() == "miniaudio", "MP3 senza tag non riconosciuto");
    }

    {
        // Un modulo non è un file audio: un MOD a 31 strumenti ("M.K.").
        mp::SourceFile file;
        file.path = "mod.prova";
        file.data.assign(1084 + 1024, 0);
        file.data[1080] = 'M';
        file.data[1081] = '.';
        file.data[1082] = 'K';
        file.data[1083] = '.';
        bool claimed = false;
        for (const auto& candidate : registry.probe(file))
            claimed = claimed || candidate.factory->name() == "miniaudio" || candidate.factory->name() == "ffmpeg";
        check(!claimed, "miniaudio o ffmpeg prendono un modulo per un file audio");
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
