// Verifica il backend miniaudio sui file di tests/data (vedi
// make_audio_fixtures.sh): riconoscimento dal contenuto, titolo e autore,
// durata, ricampionamento a 48 kHz con i canali al posto giusto, seek.
// Uso: audio_decoder_test CARTELLA_DATI

#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"

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

    const struct {
        const char* file;
        const char* format;
    } cases[] = {{"tono.wav", "WAV"}, {"tono.mp3", "MP3"}, {"tono.flac", "FLAC"}, {"tono.ogg", "Ogg Vorbis"}};

    for (const auto& c : cases) {
        const std::string name = c.file;
        const mp::SourceFile file = mp::loadSourceFile(data / c.file);
        const auto candidates = registry.probe(file);
        check(!candidates.empty() && candidates.front().factory->name() == "miniaudio",
              name + ": non riconosciuto da miniaudio");

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
        std::printf("%-10s %-10s %zu frame, sinistra %.0f Hz, destra %.0f Hz\n", c.file, info.format.c_str(), count,
                    left, right);
        // L'MP3 ha il ritardo dell'encoder e il riempimento dell'ultimo frame.
        check(count > kRate * 0.97 && count < kRate * 1.06, name + ": numero di frame sbagliato");
        check(std::fabs(left - 440) < 5 && std::fabs(right - 660) < 5, name + ": frequenze o canali sbagliati");

        decoder->seek(0.5);
        check(std::fabs(decoder->position() - 0.5) < 0.01, name + ": posizione dopo il seek");
        const size_t rest = renderAll(*decoder).size() / 2;
        check(rest > kRate * 0.45 && rest < kRate * 0.56, name + ": seek a metà, restano " + std::to_string(rest));
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
            claimed = claimed || candidate.factory->name() == "miniaudio";
        check(!claimed, "miniaudio prende un modulo per un file audio");
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
