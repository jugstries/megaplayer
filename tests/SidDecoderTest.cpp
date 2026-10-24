// Verifica il backend sid con un PSID costruito qui (i brani veri sono
// protetti da diritti): un programmino 6502 che accorda la prima voce del
// SID a 440 Hz nel sottobrano 1 e a 660 Hz nel 2, onda triangolare.
// Controlla riconoscimento, titolo, autore, sottobrani, la frequenza che
// esce dal SID emulato e il seek. Scrive anche il file in OUT.sid, per il
// test della CLI.
// Uso: sid_decoder_test OUT.sid

#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"

#include <cmath>
#include <cstdio>
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

// Valore del registro di frequenza per `hz` con il clock PAL (985248 Hz).
uint16_t sidFrequency(double hz)
{
    return static_cast<uint16_t>(std::lround(hz * 16777216.0 / 985248.0));
}

std::vector<uint8_t> makePsid()
{
    std::vector<uint8_t> file(0x7c, 0);
    auto be16 = [&](size_t offset, uint16_t value) {
        file[offset] = static_cast<uint8_t>(value >> 8);
        file[offset + 1] = static_cast<uint8_t>(value);
    };
    auto text = [&](size_t offset, const std::string& value) {
        for (size_t i = 0; i < value.size() && i < 32; ++i)
            file[offset + i] = static_cast<uint8_t>(value[i]);
    };
    file[0] = 'P';
    file[1] = 'S';
    file[2] = 'I';
    file[3] = 'D';
    be16(0x04, 2);      // versione
    be16(0x06, 0x7c);   // inizio dei dati
    be16(0x0a, 0x1000); // init
    be16(0x0c, 0x1022); // play
    be16(0x0e, 2);      // sottobrani
    be16(0x10, 1);      // il primo
    text(0x16, "Prova \xe0\xe8\xec"); // Latin-1
    text(0x36, "Megaplayer");
    text(0x56, "2026 Megaplayer");
    be16(0x76, 0x0024); // PAL, MOS 8580

    const uint16_t low = sidFrequency(440);
    const uint16_t high = sidFrequency(660);
    const std::vector<uint8_t> program = {
        0x00, 0x10,             // indirizzo di caricamento $1000
        0xaa,                   // init: TAX (A = sottobrano da 0)
        0xa9, 0x0f,             //   LDA #$0F
        0x8d, 0x18, 0xd4,       //   STA $D418   volume al massimo
        0xa9, 0x00,             //   LDA #$00
        0x8d, 0x05, 0xd4,       //   STA $D405   attacco e decadimento rapidi
        0xa9, 0xf0,             //   LDA #$F0
        0x8d, 0x06, 0xd4,       //   STA $D406   sostegno pieno
        0xbd, 0x23, 0x10,       //   LDA $1023,X frequenza, byte basso
        0x8d, 0x00, 0xd4,       //   STA $D400
        0xbd, 0x25, 0x10,       //   LDA $1025,X frequenza, byte alto
        0x8d, 0x01, 0xd4,       //   STA $D401
        0xa9, 0x11,             //   LDA #$11    triangolo, gate aperto
        0x8d, 0x04, 0xd4,       //   STA $D404
        0x60,                   //   RTS
        0x60,                   // play ($1022): RTS
        static_cast<uint8_t>(low), static_cast<uint8_t>(high),           // $1023
        static_cast<uint8_t>(low >> 8), static_cast<uint8_t>(high >> 8), // $1025
    };
    file.insert(file.end(), program.begin(), program.end());
    return file;
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

// Frequenza dominante di un canale contando gli attraversamenti della media
// (l'uscita del SID ha una componente continua).
double frequency(const std::vector<float>& frames, int channel, size_t from)
{
    const size_t count = frames.size() / 2 - from;
    double mean = 0;
    for (size_t i = from; i < from + count; ++i)
        mean += frames[i * 2 + channel];
    mean /= count;
    int crossings = 0;
    for (size_t i = from + 1; i < from + count; ++i) {
        if ((frames[(i - 1) * 2 + channel] < mean) != (frames[i * 2 + channel] < mean))
            ++crossings;
    }
    return crossings / 2.0 * kRate / static_cast<double>(count);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s OUT.sid\n", argv[0]);
        return 2;
    }
    const std::vector<uint8_t> psid = makePsid();
    std::ofstream(argv[1], std::ios::binary).write(reinterpret_cast<const char*>(psid.data()),
                                                    static_cast<std::streamsize>(psid.size()));

    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);
    const mp::SourceFile file = mp::loadSourceFile(argv[1]);
    const auto candidates = registry.probe(file);
    check(!candidates.empty() && candidates.front().factory->name() == "sid", "PSID non riconosciuto da sid");

    auto decoder = registry.open(file, mp::OutputSpec{kRate});
    const mp::TrackInfo& info = decoder->info();
    check(info.format == "PSID", "formato " + info.format);
    check(info.player == "MOS 8580", "modello del SID " + info.player);
    check(info.title == "Prova \xc3\xa0\xc3\xa8\xc3\xac", "titolo \"" + info.title + "\"");
    check(info.author == "Megaplayer", "autore \"" + info.author + "\"");
    check(info.subsongCount == 2 && info.currentSubsong == 0, "sottobrani");
    check(info.duration && *info.duration > 60, "durata");

    const double expected[] = {440, 660};
    for (int song = 0; song < 2; ++song) {
        decoder->selectSubsong(song);
        const std::vector<float> frames = renderSeconds(*decoder, 1.0);
        const double left = frequency(frames, 0, kRate / 4);
        const double right = frequency(frames, 1, kRate / 4);
        std::printf("sottobrano %d: sinistra %.1f Hz, destra %.1f Hz\n", song + 1, left, right);
        check(frames.size() == kRate * 2, "un secondo di audio");
        check(std::fabs(left - expected[song]) < 3 && std::fabs(right - expected[song]) < 3,
              "frequenza sbagliata nel sottobrano " + std::to_string(song + 1));
    }

    decoder->seek(2.5);
    check(std::fabs(decoder->position() - 2.5) < 0.01, "posizione dopo il seek in avanti");
    decoder->seek(0.5);
    check(std::fabs(decoder->position() - 0.5) < 0.01, "posizione dopo il seek indietro");

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
