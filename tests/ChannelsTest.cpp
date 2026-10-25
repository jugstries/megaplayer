// Verifica il mute dei canali in ogni backend che lo permette, su un brano
// di prova per ciascuno: senza mute si sente, con tutti i canali zittiti è
// silenzio (dall'inizio, a metà brano e dopo un cambio di sottobrano), e
// riaccesi si sente di nuovo. Un file audio non ha canali.
// Uso: channels_test BACKEND=FILE...

#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kRate = 48000;
// Sotto questo picco è silenzio: un canale OPL zittito resta a -47 dB
// finché la nota si rilascia, e qualche chip ha un filo di continua.
constexpr float kSilence = 0.01f;
constexpr float kAudible = 0.02f;

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::fprintf(stderr, "FALLITO: %s\n", what.c_str());
        ++failures;
    }
}

// Il picco di `seconds` di audio, senza la componente continua.
float peak(mp::Decoder& decoder, double seconds)
{
    std::vector<float> samples;
    std::vector<float> chunk(1024 * mp::kChannels);
    const size_t wanted = static_cast<size_t>(seconds * kRate);
    while (samples.size() < wanted * mp::kChannels) {
        const size_t got = decoder.render(chunk.data(), 1024);
        if (got == 0)
            break;
        samples.insert(samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * mp::kChannels));
    }
    if (samples.empty())
        return 0;
    double mean = 0;
    for (float sample : samples)
        mean += sample;
    mean /= static_cast<double>(samples.size());
    float top = 0;
    for (float sample : samples)
        top = std::max(top, static_cast<float>(std::fabs(sample - mean)));
    return top;
}

void muteAll(mp::Decoder& decoder, bool muted)
{
    for (size_t i = 0; i < decoder.channels().size(); ++i)
        decoder.setChannelMuted(static_cast<int>(i), muted);
}

void testBackend(const mp::DecoderRegistry& registry, const std::string& backend, const std::string& path)
{
    const mp::SourceFile source = mp::loadSourceFile(path);
    auto open = [&] { return registry.open(source, {kRate}, backend); };
    const std::string name = backend + " (" + path + ")";

    std::unique_ptr<mp::Decoder> decoder = open();
    const size_t channels = decoder->channels().size();
    check(channels > 0, name + ": nessun canale");
    if (channels == 0)
        return;
    const float loud = peak(*decoder, 1.5);
    check(loud > kAudible, name + ": senza mute non si sente (" + std::to_string(loud) + ")");
    decoder.reset();

    // Il primo mezzo secondo resta fuori: il SID all'accensione ha un
    // transitorio, anche senza suonare.
    decoder = open();
    muteAll(*decoder, true);
    peak(*decoder, 0.5);
    float quiet = peak(*decoder, 1);
    check(quiet < kSilence, name + ": zittito dall'inizio si sente (" + std::to_string(quiet) + ")");

    // Il mute resta cambiando sottobrano.
    decoder->selectSubsong(decoder->info().currentSubsong);
    peak(*decoder, 0.5);
    quiet = peak(*decoder, 1);
    check(quiet < kSilence, name + ": dopo il cambio di sottobrano si sente (" + std::to_string(quiet) + ")");

    // Riacceso e riavvolto si sente di nuovo (un MIDI suona dalla nota dopo).
    muteAll(*decoder, false);
    decoder->seek(0);
    const float again = peak(*decoder, 1.5);
    check(again > kAudible, name + ": riacceso non si sente (" + std::to_string(again) + ")");
    decoder.reset();

    // Zittito a metà: dopo mezzo secondo per le note in rilascio, silenzio.
    decoder = open();
    peak(*decoder, 0.5);
    muteAll(*decoder, true);
    peak(*decoder, 0.5);
    quiet = peak(*decoder, 0.8);
    check(quiet < kSilence, name + ": zittito a metà si sente (" + std::to_string(quiet) + ")");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s BACKEND=FILE...\n", argv[0]);
        return 2;
    }
    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t equals = arg.find('=');
        if (equals == std::string::npos) {
            std::fprintf(stderr, "argomento non valido: %s\n", arg.c_str());
            return 2;
        }
        const std::string backend = arg.substr(0, equals);
        const std::string path = arg.substr(equals + 1);
        try {
            if (backend == "nessuno") {
                const mp::SourceFile source = mp::loadSourceFile(path);
                check(registry.open(source, {kRate})->channels().empty(), path + ": un file audio ha canali");
            } else {
                testBackend(registry, backend, path);
            }
        } catch (const std::exception& e) {
            check(false, backend + " (" + path + "): " + e.what());
        }
    }
    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
