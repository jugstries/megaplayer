// Verifica la catena MegaSound su segnali sintetici:
// - spenta non tocca l'audio, riportata a 0 ridà l'originale (ritardato);
// - a ogni posizione l'uscita non supera mai il soffitto del limiter;
// - più si gira, più il suono è compresso (crest factor più basso) e un po'
//   più forte, ma non troppo, anche con molta corrente continua;
// - girandola a brano in corso il volume sale subito, senza buchi, e
//   riportandola giù non schizza in alto;
// - fin dal primo tratto lo Spazio allarga la scena, anche dei brani mono;
// - la coda del riverbero si spegne, senza valori non validi;
// - alzare la manopola a brano in corso non fa saltare l'audio.
// Stampa anche una tabella di misure, utile per la taratura.

#include "dsp/MegaSound.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;
constexpr float kCeiling = 0.944f;

using Signal = std::vector<float>; // stereo interleaved

// Qualcosa che somigli a un modulo: basso e accordo a onda quadra, melodia
// sinusoidale, raffiche di rumore come batteria.
Signal music(double seconds)
{
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1, 1);
    const size_t frames = static_cast<size_t>(seconds * kRate);
    Signal s(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        auto square = [&](double hz) { return std::sin(2 * kPi * hz * t) >= 0 ? 1.0 : -1.0; };
        const double beat = std::fmod(t, 0.25);
        const double drum = noise(random) * 0.3 * std::exp(-beat * 30);
        const double bass = 0.22 * square(55 * (1 + (static_cast<int>(t * 2) % 2)));
        const double chord = 0.08 * (square(220) + square(277.2) + square(329.6));
        const double lead = 0.12 * std::sin(2 * kPi * 880 * t + 2 * std::sin(2 * kPi * 5 * t));
        s[i * 2] = static_cast<float>(bass + chord + drum + lead);
        s[i * 2 + 1] = static_cast<float>(bass + 0.8 * chord + drum + 0.6 * lead);
    }
    return s;
}

Signal loudSquare(double seconds)
{
    const size_t frames = static_cast<size_t>(seconds * kRate);
    Signal s(frames * 2);
    for (size_t i = 0; i < frames; ++i)
        s[i * 2] = s[i * 2 + 1] = (i / 109) % 2 ? 0.99f : -0.99f;
    return s;
}

struct Stats {
    double rmsDb = -200;
    double peak = 0;
    bool valid = true;
    double crestDb() const { return 20 * std::log10(peak + 1e-12) - rmsDb; }
};

// Misure sulla parte finale, dopo che i tempi lenti si sono assestati.
Stats measure(const Signal& s, double fromSeconds)
{
    Stats st;
    double sum = 0;
    size_t n = 0;
    for (size_t i = static_cast<size_t>(fromSeconds * kRate) * 2; i < s.size(); ++i) {
        if (!std::isfinite(s[i]))
            st.valid = false;
        sum += static_cast<double>(s[i]) * s[i];
        st.peak = std::max(st.peak, static_cast<double>(std::fabs(s[i])));
        ++n;
    }
    st.rmsDb = 10 * std::log10(sum / n + 1e-24);
    return st;
}

// Energia del lato stereo rispetto al centro, in dB: più è alta, più il
// suono è largo.
double widthDb(const Signal& s, double fromSeconds)
{
    double mid = 0;
    double side = 0;
    for (size_t i = static_cast<size_t>(fromSeconds * kRate) * 2; i + 1 < s.size(); i += 2) {
        const double m = 0.5 * (s[i] + s[i + 1]);
        const double d = 0.5 * (s[i] - s[i + 1]);
        mid += m * m;
        side += d * d;
    }
    return 10 * std::log10((side + 1e-24) / (mid + 1e-24));
}

Signal mono(Signal s)
{
    for (size_t i = 0; i < s.size(); i += 2)
        s[i] = s[i + 1] = 0.5f * (s[i] + s[i + 1]);
    return s;
}

Signal sine(double seconds, double hz)
{
    const size_t frames = static_cast<size_t>(seconds * kRate);
    Signal s(frames * 2);
    for (size_t i = 0; i < frames; ++i)
        s[i * 2] = s[i * 2 + 1] = static_cast<float>(0.5 * std::sin(2 * kPi * hz * i / kRate));
    return s;
}

// Il salto più grande tra due campioni consecutivi: un buco o uno scatto
// nell'audio lo fa schizzare.
double largestStep(const Signal& s, size_t fromFrame)
{
    double worst = 0;
    for (size_t i = (fromFrame + 1) * 2; i < s.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(s[i] - s[i - 2])));
    return worst;
}

Signal processed(Signal s, float amount)
{
    mp::MegaSound megaSound(kRate);
    megaSound.setAmount(amount);
    megaSound.reset(); // parte già alla posizione, senza rampa
    for (size_t i = 0; i < s.size(); i += 512 * 2)
        megaSound.process(&s[i], std::min<size_t>(512, (s.size() - i) / 2));
    return s;
}

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FALLITO: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    {
        mp::MegaSound megaSound(kRate);
        check(!megaSound.engaged(), "spenta all'inizio non deve essere attiva");
        megaSound.setAmount(0.5f);
        check(megaSound.engaged(), "alzata deve essere attiva");
    }

    const Signal input = music(8);
    const Stats dry = measure(input, 4);
    const double dryWidth = widthDb(input, 4);
    std::printf("posizione  loudness  picco   crest     larghezza\n");
    std::printf("originale  %+6.1f dB  %.3f  %5.1f dB  %6.1f dB\n", 0.0, dry.peak, dry.crestDb(), dryWidth);

    // Lo Spazio, presente da subito, aggiunge qualche picco: la compressione
    // si confronta da una posizione all'altra.
    double previousCrest = 100;
    for (float amount : {0.15f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const Signal out = processed(input, amount);
        const Stats st = measure(out, 4);
        const double gain = st.rmsDb - dry.rmsDb;
        const double width = widthDb(out, 4);
        std::printf("%5.0f%%     %+6.1f dB  %.3f  %5.1f dB  %6.1f dB\n", amount * 100, gain, st.peak, st.crestDb(),
                    width);
        check(st.valid, "valori non validi in uscita");
        check(st.peak <= kCeiling + 1e-6, "il picco supera il soffitto del limiter");
        check(st.crestDb() < previousCrest + 0.3, "il suono deve comprimersi di più girando");
        check(width > dryWidth + 1, "lo Spazio deve allargare la scena");
        previousCrest = st.crestDb();
        if (amount == 0.15f)
            check(gain > 0 && gain < 3, "al primo tratto si aggiunge poco volume");
        if (amount == 1.0f) {
            check(gain > 0.5 && gain < 6, "a fondo corsa un po' più forte, ma non troppo");
            check(st.crestDb() < dry.crestDb(), "a fondo corsa più compresso dell'originale");
        }
    }

    {
        // La corrente continua non si sente: con o senza, la catena deve
        // dare lo stesso volume.
        Signal offset = input;
        for (float& x : offset)
            x = 0.3f * x - 0.15f;
        Signal clean = input;
        for (float& x : clean)
            x *= 0.3f;
        const double withDc = measure(processed(offset, 1), 4).rmsDb;
        const double withoutDc = measure(processed(clean, 1), 4).rmsDb;
        std::printf("con corrente continua %+.1f dB rispetto a senza\n", withDc - withoutDc);
        check(std::fabs(withDc - withoutDc) < 1, "la corrente continua cambia il volume della catena");
    }

    {
        // Dal vivo: manopola da 0 a 1 di colpo, poi giù a 0,5. Volume su
        // finestre di 0,25 s rispetto all'originale.
        const Signal original = music(14);
        Signal s = original;
        mp::MegaSound megaSound(kRate);
        auto run = [&](double from, double to) {
            const size_t a = static_cast<size_t>(from * kRate), b = static_cast<size_t>(to * kRate);
            for (size_t i = a; i < b; i += 512)
                megaSound.process(&s[i * 2], std::min<size_t>(512, b - i));
        };
        auto gainDb = [&](double from) {
            double in = 0, out = 0;
            for (size_t i = static_cast<size_t>(from * kRate) * 2; i < static_cast<size_t>((from + 0.25) * kRate) * 2; ++i) {
                in += static_cast<double>(original[i]) * original[i];
                out += static_cast<double>(s[i]) * s[i];
            }
            return 10 * std::log10(out / in);
        };
        run(0, 3);
        megaSound.setAmount(1);
        run(3, 9);
        megaSound.setAmount(0.5f);
        run(9, 14);
        double lowestUp = 100, steadyFull = gainDb(8.5), highestDown = -100;
        for (double t = 3.25; t < 8.75; t += 0.25)
            lowestUp = std::min(lowestUp, gainDb(t));
        for (double t = 9.25; t < 13.75; t += 0.25)
            highestDown = std::max(highestDown, gainDb(t));
        std::printf("dal vivo: salendo almeno %+.1f dB, a fondo %+.1f dB, scendendo al più %+.1f dB\n", lowestUp,
                    steadyFull, highestDown);
        check(lowestUp > 0, "alzando la manopola il volume scende");
        check(highestDown < steadyFull + 0.5, "riportando giù la manopola il volume schizza in alto");
    }

    {
        // Anche un brano mono si apre un po', già al primo tratto.
        const double monoWidth = widthDb(processed(mono(input), 0.15f), 4);
        std::printf("da mono    larghezza %.1f dB\n", monoWidth);
        check(monoWidth > -30 && monoWidth < -8, "lo Spazio deve aprire un po' anche il mono");
    }

    for (float amount : {0.3f, 1.0f}) {
        const Stats st = measure(processed(loudSquare(3), amount), 0.5);
        check(st.valid && st.peak <= kCeiling + 1e-6, "onda quadra a fondo scala oltre il soffitto");
    }

    {
        // Musica e poi silenzio: la coda del riverbero deve spegnersi.
        Signal s = music(2);
        s.resize(s.size() + 6 * kRate * 2, 0.0f);
        const Stats tail = measure(processed(s, 1.0f), 7.5);
        check(tail.valid && tail.rmsDb < -80, "la coda del riverbero non si spegne");
    }

    {
        // Alzata e poi riportata a 0: torna l'originale, ritardato della
        // latenza della catena.
        mp::MegaSound megaSound(kRate);
        Signal s = music(6);
        const Signal original = s;
        megaSound.setAmount(0.6f);
        const size_t half = 2 * kRate;
        megaSound.process(s.data(), half);
        megaSound.setAmount(0);
        megaSound.process(s.data() + half * 2, s.size() / 2 - half);
        const size_t latency = megaSound.latency();
        double worst = 0;
        for (size_t i = 5 * kRate; i < s.size() / 2; ++i) {
            for (int c = 0; c < 2; ++c)
                worst = std::max(worst, static_cast<double>(std::fabs(s[i * 2 + c] - original[(i - latency) * 2 + c])));
        }
        std::printf("riportata a 0: scarto massimo dall'originale %.2g (latenza %zu frame)\n", worst, latency);
        check(worst == 0, "riportata a 0 non ridà l'originale");
    }

    {
        // Chi suona dal vivo chiama sempre process: alzare la manopola a
        // brano in corso non deve lasciare buchi o scatti.
        const double natural = largestStep(sine(1, 1000), 0);
        mp::MegaSound megaSound(kRate);
        Signal s = sine(3, 1000);
        const size_t first = kRate + 123; // a metà di un blocco
        megaSound.process(s.data(), first);
        megaSound.setAmount(0.3f);
        megaSound.process(s.data() + first * 2, s.size() / 2 - first);
        const double step = largestStep(s, megaSound.latency());
        std::printf("alzata a brano in corso: salto massimo %.3f (sinusoide %.3f)\n", step, natural);
        check(step < natural * 2.5, "alzare la manopola a brano in corso fa saltare l'audio");
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
