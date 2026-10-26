// Verifica UADE nel processo del player (src/uade/InProcess.cpp):
// - riaprendo lo stesso brano molte volte l'emulatore riparte da zero e
//   produce sempre lo stesso audio, senza far crescere la memoria;
// - un secondo brano UADE aperto mentre il primo suona dà un errore pulito,
//   e il primo continua a suonare;
// - un file di testo o un'icona del Workbench col prefisso di un formato
//   ("CP.BAT", "HiP.info") non è un modulo, mentre altri dati binari con lo
//   stesso nome sì.
//
// Uso: uade_inprocess_test BRANO

#include "core/SourceFile.h"
#include "decoders/UadeDecoder.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr size_t kFrames = 48000 * 5;
constexpr int kRestarts = 30;
// Senza la patch a memory.c ogni riavvio perdeva circa 2,5 MB.
constexpr long kMaxGrowthKb = 16 * 1024;

uint64_t fnv1a(const std::vector<float>& samples)
{
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(samples.data());
    for (size_t i = 0; i < samples.size() * sizeof(float); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t renderHash(mp::UadeDecoderFactory& factory, const mp::SourceFile& file)
{
    const auto decoder = factory.open(file, {});
    std::vector<float> samples(kFrames * mp::kChannels);
    size_t done = 0;
    while (done < kFrames) {
        const size_t got = decoder->render(samples.data() + done * mp::kChannels, kFrames - done);
        if (got == 0)
            break;
        done += got;
    }
    samples.resize(done * mp::kChannels);
    return fnv1a(samples);
}

long maxRssKb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters);
    return static_cast<long>(counters.PeakWorkingSetSize / 1024);
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return usage.ru_maxrss / 1024; // su macOS è in byte
#else
    return usage.ru_maxrss;
#endif
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "uso: %s BRANO\n", argv[0]);
        return 2;
    }

    const mp::SourceFile file = mp::loadSourceFile(argv[1]);
    mp::UadeDecoderFactory factory;
    int failures = 0;

    const uint64_t expected = renderHash(factory, file);
    const long rssBefore = maxRssKb();
    for (int i = 0; i < kRestarts; ++i) {
        if (renderHash(factory, file) != expected) {
            std::fprintf(stderr, "riavvio %d: audio diverso dal primo\n", i + 1);
            ++failures;
        }
    }
    const long growth = maxRssKb() - rssBefore;
    std::printf("%d riavvii: audio identico, memoria cresciuta di %ld KB\n", kRestarts, growth);
    if (growth > kMaxGrowthKb) {
        std::fprintf(stderr, "la memoria cresce troppo a ogni riavvio\n");
        ++failures;
    }

    {
        const auto first = factory.open(file, {});
        try {
            const auto second = factory.open(file, {});
            std::fprintf(stderr, "il secondo brano non doveva aprirsi\n");
            ++failures;
        } catch (const mp::DecoderError&) {
            std::printf("secondo brano rifiutato correttamente\n");
        }
        std::vector<float> samples(1024 * mp::kChannels);
        if (first->render(samples.data(), 1024) == 0) {
            std::fprintf(stderr, "il primo brano ha smesso di suonare\n");
            ++failures;
        }
    }

    if (renderHash(factory, file) != expected) {
        std::fprintf(stderr, "dopo il rifiuto l'emulatore non riparte come prima\n");
        ++failures;
    }

    // Il batch trovato su un CD di moduli: UADE lo prendeva per Channel Player.
    const char batch[] = "@Echo Off\r\nCLS\r\nCD\\TOOLS\\PC\\CP17\r\nCP.EXE\r\nCD\\\r\n";
    mp::SourceFile text{"CP.BAT", std::vector<uint8_t>(batch, batch + sizeof batch - 1), {}};
    if (factory.probe(text) != 0) {
        std::fprintf(stderr, "CP.BAT, un file di testo, riconosciuto come modulo\n");
        ++failures;
    }
    std::vector<uint8_t> icon(2048, 0);
    icon[0] = 0xE3;
    icon[1] = 0x10;
    if (factory.probe(mp::SourceFile{"HiP.info", icon, {}}) != 0) {
        std::fprintf(stderr, "HiP.info, un'icona del Workbench, riconosciuta come modulo\n");
        ++failures;
    }
    mp::SourceFile binary{"CP.BAT", std::vector<uint8_t>(1024, 0), {}};
    if (factory.probe(binary) == 0) {
        std::fprintf(stderr, "dati binari col prefisso CP non riconosciuti dal nome\n");
        ++failures;
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
