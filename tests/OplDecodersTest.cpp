// Verifica i backend del chip FM OPL3 con due brani costruiti qui (quelli
// veri sono protetti da diritti), entrambi un La a 440 Hz per due secondi:
// - per adlmidi un MIDI di tipo 0 con il flauto del General MIDI;
// - per adplug un IMF che programma direttamente i registri del chip, con
//   una sinusoide quasi pura sul primo canale.
// Controlla backend, titolo, autore, durata, la frequenza che esce dal chip
// emulato, la fine del brano e il seek. Un MDI dell'AdLib rinominato .mid
// (un MIDI con dentro uno strumento AdLib) deve andare ad AdPlug, che lo
// suona con quello strumento, e non agli altri MIDI. Scrive i file in DIR/test.mid e
// DIR/test.imf, per i test della CLI. Con ADL, un Westwood ADL dei sorgenti
// di AdPlug (DUNE19.ADL), controlla che i sottobrani vuoti restino fuori.
// Uso: opl_decoders_test DIR [ADL]

#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

void append(std::vector<uint8_t>& out, std::initializer_list<int> bytes)
{
    for (int b : bytes)
        out.push_back(static_cast<uint8_t>(b));
}

void appendText(std::vector<uint8_t>& out, const std::string& text, bool terminated)
{
    out.insert(out.end(), text.begin(), text.end());
    if (terminated)
        out.push_back(0);
}

// MIDI di tipo 0, 96 tick per semiminima a 120 bpm: due secondi sono 384
// tick, la lunghezza in variable-length è 0x83 0x00.
std::vector<uint8_t> makeMidi()
{
    std::vector<uint8_t> track;
    const std::string title = "Prova MIDI";
    append(track, {0x00, 0xff, 0x03, static_cast<int>(title.size())});
    appendText(track, title, false);
    append(track, {0x00, 0xc0, 73});         // flauto
    append(track, {0x00, 0x90, 69, 100});    // La4 (440 Hz)
    append(track, {0x83, 0x00, 0x80, 69, 0}); // due secondi dopo
    append(track, {0x00, 0xff, 0x2f, 0x00});

    std::vector<uint8_t> file;
    appendText(file, "MThd", false);
    append(file, {0, 0, 0, 6, 0, 0, 0, 1, 0, 96});
    appendText(file, "MTrk", false);
    const auto size = static_cast<uint32_t>(track.size());
    append(file, {static_cast<int>(size >> 24), static_cast<int>((size >> 16) & 0xff),
                  static_cast<int>((size >> 8) & 0xff), static_cast<int>(size & 0xff)});
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

// Lo stesso La, ma come lo scrive l'AdLib MIDIPlay: lo strumento della voce
// 0 è dentro il file, un evento del sequencer per il costruttore 00 00 3F
// (codice 1, la voce, i 28 byte del formato BNK), e nessun cambio di
// programma. Modulatore muto, portante sinusoidale con sostegno.
std::vector<uint8_t> makeAdlibMidi()
{
    const int modulator[] = {0, 1, 0, 15, 0, 1, 0, 5, 63, 0, 0, 0, 0};
    const int carrier[] = {0, 1, 0, 15, 0, 1, 0, 5, 0, 0, 0, 0, 0};
    std::vector<uint8_t> track;
    append(track, {0x00, 0xff, 0x7f, 34, 0x00, 0x00, 0x3f, 0x00, 0x01, 0x00});
    for (const int value : modulator)
        track.push_back(static_cast<uint8_t>(value));
    for (const int value : carrier)
        track.push_back(static_cast<uint8_t>(value));
    append(track, {0, 0});                    // forme d'onda
    append(track, {0x00, 0x90, 69, 100});     // La4
    append(track, {0x83, 0x00, 0x80, 69, 0}); // due secondi dopo
    append(track, {0x00, 0xff, 0x2f, 0x00});

    std::vector<uint8_t> file;
    appendText(file, "MThd", false);
    append(file, {0, 0, 0, 6, 0, 0, 0, 1, 0, 96});
    appendText(file, "MTrk", false);
    const auto size = static_cast<uint32_t>(track.size());
    append(file, {static_cast<int>(size >> 24), static_cast<int>((size >> 16) & 0xff),
                  static_cast<int>((size >> 8) & 0xff), static_cast<int>(size & 0xff)});
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

// IMF con la lunghezza in testa (560 tick al secondo) e la coda di Adam
// Nielsen per titolo e autore. Ogni voce è registro, valore, attesa dopo.
std::vector<uint8_t> makeImf()
{
    struct Write {
        int reg, value, wait;
    };
    const Write writes[] = {
        {0x01, 0x20, 0},                                                    // forme d'onda abilitate
        // Modulatore muto; portante sinusoidale, che resta al massimo finché
        // la nota è accesa (inviluppo con sostegno, bit 5 di 0x20+).
        {0x20, 0x21, 0}, {0x40, 0x3f, 0}, {0x60, 0xf0, 0}, {0x80, 0x0f, 0},
        {0x23, 0x21, 0}, {0x43, 0x00, 0}, {0x63, 0xf0, 0}, {0x83, 0x05, 0},
        {0xc0, 0x30, 0},
        // F-number 580 al blocco 4: 580 * 49716 / 2^16 = 440 Hz.
        {0xa0, 0x44, 0}, {0xb0, 0x32, 1120}, // nota accesa per due secondi
        {0xb0, 0x12, 56}, {0x00, 0x00, 0},
    };
    std::vector<uint8_t> data;
    for (const Write& w : writes)
        append(data, {w.reg, w.value, w.wait & 0xff, w.wait >> 8});

    std::vector<uint8_t> file;
    append(file, {static_cast<int>(data.size() & 0xff), static_cast<int>(data.size() >> 8)});
    file.insert(file.end(), data.begin(), data.end());
    file.push_back(0x1a);
    appendText(file, "Prova IMF", true);
    appendText(file, "Megaplayer", true);
    appendText(file, "", true);
    return file;
}

void write(const std::filesystem::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                 static_cast<std::streamsize>(data.size()));
}

std::vector<float> renderSeconds(mp::Decoder& decoder, double seconds)
{
    std::vector<float> all(static_cast<size_t>(seconds * kRate) * 2);
    size_t done = 0;
    while (done < all.size() / 2) {
        const size_t got = decoder.render(&all[done * 2], std::min<size_t>(1024, all.size() / 2 - done));
        if (got == 0)
            break;
        done += got;
    }
    all.resize(done * 2);
    return all;
}

// Frequenza fondamentale del canale sinistro tra `from` e `to` secondi:
// il massimo dell'autocorrelazione tra 200 e 1000 Hz, affinato con una
// parabola. Regge anche i timbri FM, ricchi di armoniche.
double frequency(const std::vector<float>& frames, double from, double to)
{
    const size_t first = static_cast<size_t>(from * kRate);
    const size_t count = static_cast<size_t>((to - from) * kRate);
    auto correlation = [&](size_t lag) {
        double sum = 0;
        for (size_t i = first; i < first + count; ++i)
            sum += static_cast<double>(frames[i * 2]) * frames[(i + lag) * 2];
        return sum;
    };
    const size_t minLag = kRate / 1000;
    const size_t maxLag = kRate / 200;
    size_t best = minLag;
    double bestValue = -1e30;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        const double value = correlation(lag);
        if (value > bestValue) {
            bestValue = value;
            best = lag;
        }
    }
    const double before = correlation(best - 1);
    const double after = correlation(best + 1);
    const double shift = 0.5 * (before - after) / (before - 2 * bestValue + after);
    return kRate / (static_cast<double>(best) + shift);
}

double rms(const std::vector<float>& frames, double from, double to)
{
    double sum = 0;
    const size_t first = static_cast<size_t>(from * kRate) * 2;
    const size_t last = std::min(frames.size(), static_cast<size_t>(to * kRate) * 2);
    for (size_t i = first; i < last; ++i)
        sum += static_cast<double>(frames[i]) * frames[i];
    return std::sqrt(sum / static_cast<double>(last - first));
}

void testFile(mp::DecoderRegistry& registry, const std::filesystem::path& path, const std::string& backend,
              const std::string& title, const std::string& author, double duration)
{
    const mp::SourceFile file = mp::loadSourceFile(path);
    const auto candidates = registry.probe(file);
    check(!candidates.empty() && candidates.front().factory->name() == backend,
          path.filename().string() + " non riconosciuto da " + backend);

    auto decoder = registry.open(file, mp::OutputSpec{kRate});
    const mp::TrackInfo& info = decoder->info();
    check(info.backend == backend, "backend " + info.backend);
    check(info.title == title, "titolo \"" + info.title + "\"");
    check(info.author == author, "autore \"" + info.author + "\"");
    check(info.duration && std::fabs(*info.duration - duration) < 0.15,
          "durata " + (info.duration ? std::to_string(*info.duration) : std::string("sconosciuta")));

    const std::vector<float> frames = renderSeconds(*decoder, 1.0);
    const double hz = frequency(frames, 0.25, 0.75);
    const double level = rms(frames, 0.25, 0.75);
    std::printf("%s: %.1f Hz, rms %.3f\n", backend.c_str(), hz, level);
    check(frames.size() == kRate * 2, "un secondo di audio");
    check(level > 0.01, "audio quasi muto");
    check(std::fabs(hz - 440) < 4, "frequenza sbagliata");

    // Il decoder arriva fino alla durata annunciata, poi si ferma.
    const std::vector<float> rest = renderSeconds(*decoder, duration + 1);
    const double total = 1 + static_cast<double>(rest.size() / 2) / kRate;
    check(std::fabs(total - duration) < 0.05, "si ferma a " + std::to_string(total) + " s");

    decoder->seek(1.5);
    check(std::fabs(decoder->position() - 1.5) < 0.01, "posizione dopo il seek in avanti");
    decoder->seek(0.5);
    check(std::fabs(decoder->position() - 0.5) < 0.01, "posizione dopo il seek indietro");
    // Il MIDI dopo un salto non riaccende le note già partite: si riparte
    // dall'inizio.
    decoder->seek(0);
    const std::vector<float> after = renderSeconds(*decoder, 0.5);
    check(rms(after, 0.1, 0.5) > 0.01, "muto dopo il seek indietro");
}

void testAdlibMidi(mp::DecoderRegistry& registry, const std::filesystem::path& path)
{
    const mp::SourceFile file = mp::loadSourceFile(path);
    const auto candidates = registry.probe(file);
    check(!candidates.empty() && candidates.front().factory->name() == "adplug",
          "un MDI rinominato .mid non va ad adplug");
    for (const mp::ProbeResult& candidate : candidates)
        check(candidate.factory->name() != "88emu", "un MDI dell'AdLib offerto a 88emu");
    auto decoder = registry.open(file, mp::OutputSpec{kRate});
    check(decoder->info().format.find("MIDIPlay") != std::string::npos, "formato " + decoder->info().format);
    const std::vector<float> frames = renderSeconds(*decoder, 1.0);
    std::printf("adplug (MDI): rms %.3f\n", rms(frames, 0.25, 0.75));
    check(rms(frames, 0.25, 0.75) > 0.01, "MDI quasi muto");
}

// In DUNE19.ADL 30 sottobrani su 74 sono vuoti e uno non accende note: chi
// passava da un sottobrano all'altro finiva su uno vuoto e la riproduzione
// si fermava. Ogni sottobrano offerto deve suonare, e si parte dalla prima
// musica (il terzo sottobrano del file, 28 secondi).
void testWestwoodSubsongs(mp::DecoderRegistry& registry, const std::filesystem::path& path)
{
    auto decoder = registry.open(mp::loadSourceFile(path), mp::OutputSpec{kRate});
    const mp::TrackInfo& info = decoder->info();
    std::printf("%s: %d sottobrani, si parte dal %d\n", path.filename().string().c_str(), info.subsongCount,
                info.currentSubsong + 1);
    check(info.subsongCount == 43, "sottobrani di DUNE19.ADL: " + std::to_string(info.subsongCount));
    check(info.currentSubsong == 0 && info.duration && std::fabs(*info.duration - 28.4) < 0.5,
          "DUNE19.ADL non parte dalla prima musica");
    for (int i = 0; i < info.subsongCount; ++i) {
        decoder->selectSubsong(i);
        const std::vector<float> frames = renderSeconds(*decoder, 0.05);
        check(!frames.empty(), "il sottobrano " + std::to_string(i + 1) + " finisce subito");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s DIR\n", argv[0]);
        return 2;
    }
    const std::filesystem::path directory = argv[1];
    write(directory / "test.mid", makeMidi());
    write(directory / "test.imf", makeImf());
    write(directory / "test-adlib.mid", makeAdlibMidi());

    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);
    // Qui si provano i chip FM: niente SoundFont, che per i MIDI verrebbe prima.
    mp::setSoundFont("");
#ifdef MP_TEST_ADLMIDI
    // La durata comprende il secondo che libADLMIDI aspetta a fine brano.
    testFile(registry, directory / "test.mid", "adlmidi", "Prova MIDI", "", 3.0);
#endif
#ifdef MP_TEST_ADPLUG
    // Due secondi di nota e un decimo dopo lo spegnimento.
    testFile(registry, directory / "test.imf", "adplug", "Prova IMF", "Megaplayer", 2.1);
    testAdlibMidi(registry, directory / "test-adlib.mid");
    if (argc > 2)
        testWestwoodSubsongs(registry, argv[2]);
#endif

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
