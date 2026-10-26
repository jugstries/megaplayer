// Verifica il backend vgmstream sugli stream di tests/make_test_streams.py:
// il GENH in PS-ADPCM campione per campione, con il loop a metà di un frame,
// la dissolvenza e il seek esatto anche dentro il secondo giro; il GENH in
// PCM mono ricampionato; il .int senza intestazione.
// Uso: vgmstream_decoder_test CARTELLA

#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Come make_test_streams.py.
constexpr int kGenhRate = 44100;
constexpr size_t kLoopStart = 44110;
constexpr size_t kLoopEnd = 37 * 3584;
constexpr size_t kLoop = kLoopEnd - kLoopStart;
// Due giri del ritornello, poi dieci secondi di dissolvenza.
constexpr size_t kFadeStart = kLoopStart + 2 * kLoop;
constexpr size_t kTotal = kFadeStart + 10 * kGenhRate;

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
    std::vector<float> chunk(1000 * 2);
    while (const size_t got = decoder.render(chunk.data(), 1000))
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * 2));
    return all;
}

// Il campione n di un canale del GENH, prima della dissolvenza: l'onda
// quadra dell'introduzione, poi quella del ritornello che riparte dal loop.
// In float come lo dà vgmstream, che divide i 16 bit per 32767.
float expected(size_t n, int channel)
{
    static constexpr size_t intro[] = {100, 200};
    static constexpr size_t chorus[] = {50, 80};
    static constexpr int amplitude[] = {16384, 8192};
    if (n >= kLoopEnd)
        n = kLoopStart + (n - kLoopStart) % kLoop;
    const bool high = n < kLoopStart ? (n / (intro[channel] / 2)) % 2 == 0
                                     : ((n - kLoopStart) / (chorus[channel] / 2)) % 2 == 0;
    return static_cast<float>(high ? amplitude[channel] : -amplitude[channel]) * (1.0f / 32767.0f);
}

// Frequenza dominante di un canale dagli attraversamenti dello zero.
double frequency(const std::vector<float>& frames, int channel, size_t from, size_t count, int rate)
{
    int crossings = 0;
    for (size_t i = from + 1; i < from + count; ++i) {
        if ((frames[(i - 1) * 2 + channel] < 0) != (frames[i * 2 + channel] < 0))
            ++crossings;
    }
    return crossings / 2.0 * rate / static_cast<double>(count);
}

void testGenhAdpcm(const mp::DecoderRegistry& registry, const std::filesystem::path& path)
{
    const mp::SourceFile file = mp::loadSourceFile(path);
    const auto candidates = registry.probe(file);
    check(!candidates.empty() && candidates.front().factory->name() == "vgmstream", "GENH: non riconosciuto da vgmstream");

    auto decoder = registry.open(file, mp::OutputSpec{kGenhRate});
    const mp::TrackInfo& info = decoder->info();
    std::printf("test.genh: %s, %s, %.3f s\n", info.format.c_str(), info.player.c_str(), info.duration.value_or(0));
    check(info.backend == "vgmstream", "GENH: backend " + info.backend);
    check(info.format.find("GENH") != std::string::npos, "GENH: formato " + info.format);
    check(info.platform == "playstation", "GENH: piattaforma " + info.platform);
    check(info.title == "test", "GENH: titolo " + info.title);
    check(info.subsongCount == 1, "GENH: sottobrani");
    check(info.duration && std::fabs(*info.duration - static_cast<double>(kTotal) / kGenhRate) < 1e-6,
          "GENH: durata sbagliata");

    const std::vector<float> all = renderAll(*decoder);
    check(all.size() / 2 == kTotal, "GENH: " + std::to_string(all.size() / 2) + " frame invece di " + std::to_string(kTotal));
    if (all.size() / 2 != kTotal)
        return;

    // Introduzione e due giri del ritornello identici al file.
    size_t wrong = 0;
    for (size_t n = 0; n < kFadeStart; ++n) {
        for (int channel = 0; channel < 2; ++channel) {
            const float value = all[n * 2 + static_cast<size_t>(channel)];
            if (value != expected(n, channel) && wrong++ == 0)
                std::fprintf(stderr, "primo campione sbagliato: frame %zu, canale %d: %.9f invece di %.9f\n", n,
                             channel, value, expected(n, channel));
        }
    }
    check(wrong == 0, "GENH: " + std::to_string(wrong) + " campioni diversi dal file");

    // La dissolvenza: piena all'inizio, a metà meno della metà, poi zero.
    const float fadeMiddle = std::fabs(all[(kFadeStart + 5 * kGenhRate) * 2]);
    check(std::fabs(all[kFadeStart * 2]) > 0.49f && fadeMiddle > 0.01f && fadeMiddle < 0.4f,
          "GENH: dissolvenza (metà " + std::to_string(fadeMiddle) + ")");
    check(std::fabs(all[(kTotal - 1) * 2]) < 0.01f, "GENH: la dissolvenza non arriva a zero");

    // Seek esatti, all'indietro dalla fine: nell'introduzione, a cavallo del
    // loop, nel secondo giro, nella dissolvenza.
    for (const size_t target : {size_t{1000}, kLoopStart - 3, kLoopEnd - 500, kLoopEnd + 12345, kFadeStart + 1000}) {
        const double seconds = static_cast<double>(target) / kGenhRate;
        decoder->seek(seconds);
        check(std::fabs(decoder->position() - seconds) < 1e-6, "GENH: posizione dopo il seek a " + std::to_string(target));
        std::vector<float> chunk(2000 * 2);
        const size_t got = decoder->render(chunk.data(), 2000);
        bool same = got == 2000;
        for (size_t i = 0; same && i < got * 2; ++i)
            same = chunk[i] == all[target * 2 + i];
        check(same, "GENH: seek inesatto a " + std::to_string(target));
    }
}

void testGenhPcm(const mp::DecoderRegistry& registry, const std::filesystem::path& path)
{
    constexpr int kRate = 48000;
    auto decoder = registry.open(mp::loadSourceFile(path), mp::OutputSpec{kRate});
    const mp::TrackInfo& info = decoder->info();
    std::printf("test-pcm.genh: %s, %s, %.3f s\n", info.format.c_str(), info.player.c_str(), info.duration.value_or(0));
    check(info.backend == "vgmstream", "GENH PCM: backend " + info.backend);
    check(info.platform == "audio", "GENH PCM: piattaforma " + info.platform);
    check(info.duration && std::fabs(*info.duration - 1.0) < 1e-6, "GENH PCM: durata sbagliata");

    const std::vector<float> all = renderAll(*decoder);
    const size_t count = all.size() / 2;
    check(count > kRate - 10 && count < kRate + 10, "GENH PCM: " + std::to_string(count) + " frame");
    bool mono = true;
    for (size_t i = 0; mono && i < count; ++i)
        mono = all[i * 2] == all[i * 2 + 1];
    check(mono, "GENH PCM: il mono non è uguale sui due canali");
    const double tone = frequency(all, 0, kRate / 4, kRate / 2, kRate);
    check(std::fabs(tone - 1000) < 5, "GENH PCM: frequenza " + std::to_string(tone));
}

void testInt(const mp::DecoderRegistry& registry, const std::filesystem::path& path)
{
    constexpr int kRate = 48000;
    auto decoder = registry.open(mp::loadSourceFile(path), mp::OutputSpec{kRate});
    const mp::TrackInfo& info = decoder->info();
    std::printf("test.int: %s, %s, %.3f s\n", info.format.c_str(), info.player.c_str(), info.duration.value_or(0));
    check(info.backend == "vgmstream", "INT: backend " + info.backend);
    check(info.platform == "playstation", "INT: piattaforma " + info.platform);
    check(info.duration && std::fabs(*info.duration - 2.0) < 1e-6, "INT: durata sbagliata");

    const std::vector<float> all = renderAll(*decoder);
    check(all.size() / 2 == 2 * kRate, "INT: " + std::to_string(all.size() / 2) + " frame");
    const double left = frequency(all, 0, kRate / 2, kRate, kRate);
    const double right = frequency(all, 1, kRate / 2, kRate, kRate);
    check(std::fabs(left - 440) < 3 && std::fabs(right - 880) < 3,
          "INT: frequenze " + std::to_string(left) + " e " + std::to_string(right));
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s CARTELLA\n", argv[0]);
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    mp::DecoderRegistry registry;
    mp::registerBuiltinDecoders(registry);

    testGenhAdpcm(registry, dir / "test.genh");
    testGenhPcm(registry, dir / "test-pcm.genh");
    testInt(registry, dir / "test.int");

    {
        // Un file con l'estensione giusta ma senza l'intestazione non è suo.
        mp::SourceFile file;
        file.path = "falso.genh";
        file.data.assign(4096, 0x55);
        bool claimed = false;
        for (const auto& candidate : registry.probe(file))
            claimed = claimed || candidate.factory->name() == "vgmstream";
        check(!claimed, "vgmstream prende un .genh senza intestazione");
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
