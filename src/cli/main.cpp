#include "Terminal.h"

#include "core/Archive.h"
#include "core/AudioOutput.h"
#include "core/DecoderRegistry.h"
#include "core/SourceFile.h"
#include "core/WavWriter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

using namespace mp;

constexpr size_t kChunkFrames = 1024;
constexpr double kSeekStep = 10.0;

const char* const kUsage = R"(megaplayer - prototipo da riga di comando

Uso:
  megaplayer play   [opzioni] FILE...           riproduce i file
  megaplayer info   [opzioni] FILE...           formato, backend e metadati
  megaplayer render [opzioni] -o OUT.wav FILE   esporta in WAV
  megaplayer backends                           backend disponibili

Opzioni:
  -b, --backend NOME   usa solo questo backend
  -s, --subsong N      sottobrano da riprodurre (da 1)
  -t, --seconds N      durata massima in secondi
  -r, --rate HZ        frequenza di campionamento (default 48000)
  -o, --output FILE    file WAV di uscita (render)
  -h, --help           questo messaggio

FILE può essere un archivio (LHA, ZIP, 7z, tar, ...): si suonano i brani che
contiene. ARCHIVIO/percorso/interno sceglie un file o una cartella al suo
interno, es. Stormlord.lha/Stormlord/jt.title

Tasti durante play:
  spazio  pausa              ←/→  indietro/avanti di 10 s
  n/p     sottobrano successivo/precedente
  invio   file successivo    q    esci
)";

struct Options {
    std::string command;
    std::vector<std::string> files;
    std::string backend;
    std::optional<int> subsong; // da 1, come lo scrive l'utente
    std::optional<double> seconds;
    int sampleRate = 48000;
    std::string output;
};

template <typename T>
T parseNumber(const std::string& option, const std::string& text, T min, T max)
{
    try {
        size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size() && value >= min && value <= max)
            return static_cast<T>(value);
    } catch (const std::exception&) {
    }
    throw std::invalid_argument("valore non valido per " + option + ": " + text);
}

Options parseArgs(int argc, char** argv)
{
    Options options;
    const std::vector<std::string> args(argv + 1, argv + argc);

    auto value = [&](size_t& i) -> const std::string& {
        if (i + 1 >= args.size())
            throw std::invalid_argument("manca il valore di " + args[i]);
        return args[++i];
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "-h" || arg == "--help") {
            options.command = "help";
            return options;
        } else if (arg == "-b" || arg == "--backend") {
            options.backend = value(i);
        } else if (arg == "-s" || arg == "--subsong") {
            options.subsong = parseNumber(arg, value(i), 1, 1000);
        } else if (arg == "-t" || arg == "--seconds") {
            options.seconds = parseNumber(arg, value(i), 0.0, 1e6);
        } else if (arg == "-r" || arg == "--rate") {
            options.sampleRate = parseNumber(arg, value(i), 8000, 192000);
        } else if (arg == "-o" || arg == "--output") {
            options.output = value(i);
        } else if (arg.size() > 1 && arg[0] == '-') {
            throw std::invalid_argument("opzione sconosciuta: " + arg);
        } else if (options.command.empty()) {
            options.command = arg;
        } else {
            options.files.push_back(arg);
        }
    }

    if (options.command.empty())
        throw std::invalid_argument("manca il comando");
    if (options.command != "play" && options.command != "info" && options.command != "render"
        && options.command != "backends")
        throw std::invalid_argument("comando sconosciuto: " + options.command);
    if (options.command != "backends" && options.files.empty())
        throw std::invalid_argument("nessun file indicato");
    if (options.command == "render" && (options.files.size() != 1 || options.output.empty()))
        throw std::invalid_argument("render vuole un solo file e -o OUT.wav");
    return options;
}

std::string formatTime(double seconds)
{
    const auto total = static_cast<long>(std::max(0.0, seconds));
    char text[32];
    std::snprintf(text, sizeof text, "%ld:%02ld", total / 60, total % 60);
    return text;
}

bool stdoutIsTerminal()
{
#ifndef _WIN32
    return isatty(STDOUT_FILENO);
#else
    return false;
#endif
}

// Un brano da suonare: un file indicato dall'utente o estratto da un archivio.
struct Track {
    std::filesystem::path path;
    std::string name; // come mostrarlo
    bool fromArchive = false;
};

struct Playlist {
    std::vector<Track> tracks;
    std::vector<ExtractedArchive> archives; // tengono in vita i file estratti
    int failures = 0;
};

// Divide "giochi/Stormlord.lha/Stormlord/jt.title" nel file che esiste su
// disco e nel percorso che segue, da cercare dentro l'archivio.
std::pair<std::filesystem::path, std::string> splitArchivePath(const std::string& input)
{
    std::filesystem::path candidate = input;
    while (!std::filesystem::exists(candidate) && candidate.has_relative_path())
        candidate = candidate.parent_path();
    if (candidate.empty() || !std::filesystem::is_regular_file(candidate) || candidate == input)
        return {input, {}};
    return {candidate, input.substr(candidate.string().size() + 1)};
}

Playlist buildPlaylist(const std::vector<std::string>& inputs)
{
    Playlist playlist;
    for (const std::string& input : inputs) {
        try {
            const auto [path, inner] = splitArchivePath(input);
            std::optional<ExtractedArchive> archive = openArchive(path);
            if (!archive) {
                playlist.tracks.push_back({input, input});
                continue;
            }

            size_t added = 0;
            for (const ExtractedArchive::Entry& entry : archive->entries()) {
                const bool selected = inner.empty() || entry.name == inner
                    || entry.name.rfind(inner + "/", 0) == 0;
                if (selected) {
                    playlist.tracks.push_back({entry.path, path.string() + " > " + entry.name, true});
                    ++added;
                }
            }
            if (added == 0)
                throw std::runtime_error(inner.empty() ? "archivio vuoto" : "nessun file " + inner + " nell'archivio");
            playlist.archives.push_back(std::move(*archive));
        } catch (const std::exception& e) {
            std::cerr << input << ": " << e.what() << "\n";
            ++playlist.failures;
        }
    }
    return playlist;
}

// Carica il brano; std::nullopt per i file di un archivio che nessun backend
// riconosce (testi, icone .info, ...), da saltare senza segnalarli.
std::optional<SourceFile> loadTrack(const Track& track, const DecoderRegistry& registry)
{
    SourceFile source = loadSourceFile(track.path);
    if (track.fromArchive && registry.probe(source).empty())
        return std::nullopt;
    return source;
}

void reportSkipped(int skipped)
{
    if (skipped > 0)
        std::cout << "(" << skipped << " file non musicali degli archivi saltati)\n";
}

void printTrack(const std::string& name, const SourceFile& source, const Decoder& decoder)
{
    const TrackInfo& info = decoder.info();
    auto field = [](const char* label, const std::string& value) {
        if (!value.empty())
            std::cout << "  " << label << value << "\n";
    };

    std::cout << name << "\n";
    if (source.wasUnpacked()) {
        std::string chain;
        for (const std::string& packer : source.packers)
            chain += (chain.empty() ? "" : " > ") + packer;
        field("Packer:     ", chain);
    }
    field("Backend:    ", info.backend);
    field("Formato:    ", info.format);
    field("Replayer:   ", info.player);
    field("Titolo:     ", info.title);
    field("Autore:     ", info.author);
    std::cout << "  Sottobrano: " << info.currentSubsong + 1 << " di " << info.subsongCount << "\n";
    std::cout << "  Durata:     " << (info.duration ? formatTime(*info.duration) : "sconosciuta") << "\n";
}

std::unique_ptr<Decoder> openTrack(const SourceFile& source, const Options& options,
                                   const DecoderRegistry& registry)
{
    auto decoder = registry.open(source, {options.sampleRate}, options.backend);
    if (options.subsong) {
        if (*options.subsong > decoder->info().subsongCount)
            throw DecoderError("il file ha solo " + std::to_string(decoder->info().subsongCount) + " sottobrani");
        decoder->selectSubsong(*options.subsong - 1);
    }
    return decoder;
}

int runBackends(const DecoderRegistry& registry)
{
    if (registry.factories().empty())
        std::cout << "Nessun backend compilato.\n";
    for (const auto& factory : registry.factories())
        std::cout << "  " << factory->name() << "\t" << factory->description() << "\n";
    return 0;
}

int runInfo(const Options& options, const DecoderRegistry& registry)
{
    const Playlist playlist = buildPlaylist(options.files);
    int failures = playlist.failures;
    int skipped = 0;
    for (const Track& track : playlist.tracks) {
        try {
            const std::optional<SourceFile> source = loadTrack(track, registry);
            if (!source) {
                ++skipped;
                continue;
            }

            std::string candidates;
            for (const ProbeResult& result : registry.probe(*source))
                candidates += (candidates.empty() ? "" : ", ") + std::string(result.factory->name()) + " ("
                    + std::to_string(result.score) + ")";

            const auto decoder = openTrack(*source, options, registry);
            printTrack(track.name, *source, *decoder);
            std::cout << "  Candidati:  " << (candidates.empty() ? "nessuno" : candidates) << "\n";
        } catch (const std::exception& e) {
            std::cerr << track.name << ": " << e.what() << "\n";
            ++failures;
        }
    }
    reportSkipped(skipped);
    return failures == 0 ? 0 : 1;
}

int runRender(const Options& options, const DecoderRegistry& registry)
{
    const Playlist playlist = buildPlaylist(options.files);
    if (playlist.failures > 0)
        return 1;

    std::vector<std::pair<const Track*, SourceFile>> playable;
    for (const Track& track : playlist.tracks) {
        if (std::optional<SourceFile> source = loadTrack(track, registry))
            playable.emplace_back(&track, std::move(*source));
    }
    if (playable.empty())
        throw std::runtime_error("nessun brano riconosciuto in " + options.files.front());
    if (playable.size() > 1)
        throw std::runtime_error(options.files.front() + " contiene " + std::to_string(playable.size())
                                 + " brani e render ne esporta uno: indica quale, es. ARCHIVIO.lha/percorso/brano");

    const auto& [track, source] = playable.front();
    const auto decoder = openTrack(source, options, registry);
    printTrack(track->name, source, *decoder);

    WavWriter wav(options.output, options.sampleRate);
    std::vector<float> chunk(kChunkFrames * kChannels);
    const size_t maxFrames = options.seconds
        ? static_cast<size_t>(std::llround(*options.seconds * options.sampleRate))
        : std::numeric_limits<size_t>::max();

    size_t total = 0;
    while (total < maxFrames && !cli::interrupted()) {
        const size_t got = decoder->render(chunk.data(), std::min(kChunkFrames, maxFrames - total));
        if (got == 0)
            break;
        wav.write(chunk.data(), got);
        total += got;
    }

    std::cout << "Scritti " << formatTime(static_cast<double>(total) / options.sampleRate) << " in "
              << options.output << "\n";
    return 0;
}

enum class AfterTrack { NextFile, Quit };

AfterTrack playTrack(const Track& track, const SourceFile& source, Decoder& decoder, const Options& options,
                     AudioOutput& output, cli::TerminalInput& keys)
{
    const bool showStatus = stdoutIsTerminal();
    std::vector<float> chunk(kChunkFrames * kChannels);
    bool decoding = true;
    bool paused = false;

    auto playbackPosition = [&] {
        return decoder.position() - static_cast<double>(output.queuedFrames()) / options.sampleRate;
    };
    // Dopo seek e cambi di sottobrano l'audio già in coda va scartato.
    auto restart = [&] {
        output.flush();
        decoding = true;
    };
    auto endStatusLine = [&] {
        if (showStatus)
            std::cout << "\n";
    };

    printTrack(track.name, source, decoder);
    output.flush();
    output.setPaused(false);

    while (!cli::interrupted()) {
        const cli::Key key = keys.poll();
        switch (key) {
        case cli::Key::Quit:
            endStatusLine();
            return AfterTrack::Quit;
        case cli::Key::NextFile:
            endStatusLine();
            return AfterTrack::NextFile;
        case cli::Key::Pause:
            paused = !paused;
            output.setPaused(paused);
            break;
        case cli::Key::SeekBack:
        case cli::Key::SeekForward:
            if (decoder.canSeek()) {
                const double step = key == cli::Key::SeekForward ? kSeekStep : -kSeekStep;
                decoder.seek(std::max(0.0, playbackPosition() + step));
                restart();
            }
            break;
        case cli::Key::NextSubsong:
        case cli::Key::PrevSubsong: {
            const TrackInfo& info = decoder.info();
            const int next = info.currentSubsong + (key == cli::Key::NextSubsong ? 1 : -1);
            if (next >= 0 && next < info.subsongCount) {
                decoder.selectSubsong(next);
                restart();
            }
            break;
        }
        case cli::Key::None:
            break;
        }

        if (decoding) {
            while (output.writableFrames() >= kChunkFrames) {
                const size_t got = decoder.render(chunk.data(), kChunkFrames);
                output.write(chunk.data(), got);
                if (got == 0 || (options.seconds && decoder.position() >= *options.seconds)) {
                    decoding = false;
                    break;
                }
            }
        }
        if (!decoding && output.queuedFrames() == 0)
            break;

        if (showStatus) {
            const TrackInfo& info = decoder.info();
            std::cout << "\r  " << (paused ? "pausa " : "      ") << formatTime(playbackPosition());
            if (info.duration)
                std::cout << " / " << formatTime(*info.duration);
            std::cout << "   sottobrano " << info.currentSubsong + 1 << "/" << info.subsongCount << "\x1b[K"
                      << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    endStatusLine();
    return cli::interrupted() ? AfterTrack::Quit : AfterTrack::NextFile;
}

int runPlay(const Options& options, const DecoderRegistry& registry)
{
    const Playlist playlist = buildPlaylist(options.files);
    if (playlist.tracks.empty())
        return 1;

    AudioOutput output(options.sampleRate);
    cli::TerminalInput keys;
    std::cout << "Uscita: " << output.deviceName() << "\n";
    output.start();

    int failures = playlist.failures;
    int skipped = 0;
    for (const Track& track : playlist.tracks) {
        try {
            const std::optional<SourceFile> source = loadTrack(track, registry);
            if (!source) {
                ++skipped;
                continue;
            }
            const auto decoder = openTrack(*source, options, registry);
            std::cout << "\n";
            if (playTrack(track, *source, *decoder, options, output, keys) == AfterTrack::Quit)
                break;
        } catch (const std::exception& e) {
            // Con Ctrl+C anche i processi uadecore ricevono SIGINT e
            // muoiono: non è un errore da segnalare.
            if (cli::interrupted())
                break;
            std::cerr << track.name << ": " << e.what() << "\n";
            ++failures;
        }
    }
    output.stop();
    reportSkipped(skipped);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    try {
        options = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "megaplayer: " << e.what() << "\n\n" << kUsage;
        return 2;
    }
    if (options.command == "help") {
        std::cout << kUsage;
        return 0;
    }

    DecoderRegistry registry;
    registerBuiltinDecoders(registry);
    cli::installInterruptHandler();

    try {
        if (options.command == "backends")
            return runBackends(registry);
        if (options.command == "info")
            return runInfo(options, registry);
        if (options.command == "render")
            return runRender(options, registry);
        return runPlay(options, registry);
    } catch (const std::exception& e) {
        std::cerr << "megaplayer: " << e.what() << "\n";
        return 1;
    }
}
