#include "Terminal.h"

#include "core/AudioOutput.h"
#include "core/DecoderRegistry.h"
#include "core/Platform.h"
#include "core/SourceFile.h"
#include "core/TrackList.h"
#include "core/WavWriter.h"
#include "dsp/MegaSound.h"
#ifdef MP_WITH_LIBRARY
#include "library/Library.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
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
  megaplayer library scan [CARTELLA]            indicizza la cartella della libreria
  megaplayer library search [TESTO...]          cerca nella libreria
  megaplayer library stats                      brani per piattaforma e formato
  megaplayer library root                       la cartella della libreria

Opzioni:
  -b, --backend NOME   usa solo questo backend
  -s, --subsong N      sottobrano da riprodurre (da 1)
  -t, --seconds N      durata massima in secondi
  -r, --rate HZ        frequenza di campionamento (default 48000)
  -o, --output FILE    file WAV di uscita (render)
  -m, --megasound N    manopola MegaSound da 0 a 100 (play e render)
  -M, --mute LISTA     zittisce questi canali, es. 1,3 o 2-4 (da 1)
  -h, --help           questo messaggio

Opzioni della libreria:
  --db FILE            database (default: nella cartella dei dati dell'utente)
  -p, --platform ID    solo questa piattaforma, es. amiga, pc, c64, nes
                       (si può ripetere)
  -f, --format NOME    solo questo formato (si può ripetere)
  -d, --folder PERCORSO
                       solo in questa cartella della libreria e sotto,
                       es. Amiga/Jesper\ Kyd
  -n, --limit N        quanti risultati mostrare (default 50)

FILE può essere un archivio (LHA, ZIP, 7z, tar, ...): si suonano i brani che
contiene. ARCHIVIO/percorso/interno sceglie un file o una cartella al suo
interno, es. Stormlord.lha/Stormlord/jt.title

Tasti durante play:
  spazio  pausa              ←/→  indietro/avanti di 10 s
  n/p     sottobrano successivo/precedente
  +/-     MegaSound ±10%      m    confronto A/B con l'originale
  1…9, 0  zittisce o riaccende i canali da 1 a 10
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
    int megaSound = 0; // posizione della manopola, 0..100
    std::vector<int> mute; // canali da zittire, da 1 come li scrive l'utente
    // Libreria.
    std::string database;
    std::vector<std::string> platforms;
    std::vector<std::string> formats;
    std::string folder;
    int limit = 50;
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

// "1,3,5-7": i canali da 1, senza ripetizioni, in ordine.
std::vector<int> parseChannels(const std::string& option, const std::string& text)
{
    std::vector<int> channels;
    std::stringstream list(text);
    std::string part;
    while (std::getline(list, part, ',')) {
        const size_t dash = part.find('-');
        const int first = parseNumber(option, part.substr(0, dash), 1, 256);
        const int last = dash == std::string::npos ? first : parseNumber(option, part.substr(dash + 1), first, 256);
        for (int channel = first; channel <= last; ++channel)
            channels.push_back(channel);
    }
    if (channels.empty())
        throw std::invalid_argument("valore non valido per " + option + ": " + text);
    std::sort(channels.begin(), channels.end());
    channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
    return channels;
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
        } else if (arg == "-m" || arg == "--megasound") {
            options.megaSound = parseNumber(arg, value(i), 0, 100);
        } else if (arg == "-M" || arg == "--mute") {
            options.mute = parseChannels(arg, value(i));
        } else if (arg == "--db") {
            options.database = value(i);
        } else if (arg == "-p" || arg == "--platform") {
            options.platforms.push_back(value(i));
        } else if (arg == "-f" || arg == "--format") {
            options.formats.push_back(value(i));
        } else if (arg == "-d" || arg == "--folder") {
            options.folder = value(i);
        } else if (arg == "-n" || arg == "--limit") {
            options.limit = parseNumber(arg, value(i), 1, 100000);
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
        && options.command != "backends" && options.command != "library")
        throw std::invalid_argument("comando sconosciuto: " + options.command);
    if (options.command == "library") {
        const std::string what = options.files.empty() ? "" : options.files.front();
        if (what != "scan" && what != "search" && what != "stats" && what != "root")
            throw std::invalid_argument("library vuole scan, search, stats o root");
        return options;
    }
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

struct Playlist {
    std::vector<Track> tracks;
    int failures = 0;
    int skipped = 0; // file non musicali, o campioni di un brano in due file
};

// `collector` tiene in vita i file estratti dagli archivi: deve vivere quanto
// la playlist.
Playlist buildPlaylist(const std::vector<std::string>& inputs, TrackCollector& collector)
{
    Playlist playlist;
    for (const std::string& input : inputs) {
        try {
            std::vector<Track> tracks = collector.collect(input);
            playlist.tracks.insert(playlist.tracks.end(), tracks.begin(), tracks.end());
            playlist.skipped += collector.lastSkipped();
        } catch (const std::exception& e) {
            std::cerr << input << ": " << e.what() << "\n";
            ++playlist.failures;
        }
    }
    return playlist;
}

void reportSkipped(int skipped)
{
    if (skipped > 0)
        std::cout << "(" << skipped << " file saltati: non musicali, o campioni di un brano)\n";
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
    // I nomi solo se sono pochi e ci sono tutti.
    const std::vector<std::string> channels = decoder.channels();
    if (!channels.empty()) {
        std::string names;
        for (const std::string& channel : channels)
            names += (names.empty() ? "" : ", ") + channel;
        const bool named = channels.size() <= 8
            && std::none_of(channels.begin(), channels.end(), [](const std::string& c) { return c.empty(); });
        std::cout << "  Canali:     " << channels.size() << (named ? " (" + names + ")" : "") << "\n";
    }
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
    const size_t channels = decoder->channels().size();
    for (const int channel : options.mute) {
        if (static_cast<size_t>(channel) > channels)
            throw DecoderError(channels == 0 ? "il brano non ha canali da zittire"
                                             : "il brano ha solo " + std::to_string(channels) + " canali");
        decoder->setChannelMuted(channel - 1, true);
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
    TrackCollector collector(registry);
    const Playlist playlist = buildPlaylist(options.files, collector);
    int failures = playlist.failures;
    for (const Track& track : playlist.tracks) {
        try {
            const SourceFile source = loadSourceFile(track.path);

            std::string candidates;
            for (const ProbeResult& result : registry.probe(source))
                candidates += (candidates.empty() ? "" : ", ") + std::string(result.factory->name()) + " ("
                    + std::to_string(result.score) + ")";

            const auto decoder = openTrack(source, options, registry);
            printTrack(track.name, source, *decoder);
            std::cout << "  Candidati:  " << (candidates.empty() ? "nessuno" : candidates) << "\n";
        } catch (const std::exception& e) {
            std::cerr << track.name << ": " << e.what() << "\n";
            ++failures;
        }
    }
    reportSkipped(playlist.skipped);
    return failures == 0 ? 0 : 1;
}

int runRender(const Options& options, const DecoderRegistry& registry)
{
    TrackCollector collector(registry);
    const Playlist playlist = buildPlaylist(options.files, collector);
    if (playlist.failures > 0)
        return 1;
    if (playlist.tracks.empty())
        throw std::runtime_error("nessun brano riconosciuto in " + options.files.front());
    if (playlist.tracks.size() > 1)
        throw std::runtime_error(options.files.front() + " contiene " + std::to_string(playlist.tracks.size())
                                 + " brani e render ne esporta uno: indica quale, es. ARCHIVIO.lha/percorso/brano");

    const Track& track = playlist.tracks.front();
    const SourceFile source = loadSourceFile(track.path);
    const auto decoder = openTrack(source, options, registry);
    printTrack(track.name, source, *decoder);
    if (options.megaSound > 0)
        std::cout << "  MegaSound:  " << options.megaSound << "%\n";
    if (!options.mute.empty()) {
        std::cout << "  Zittiti:   ";
        for (const int channel : options.mute)
            std::cout << " " << channel;
        std::cout << "\n";
    }

    // Qui non serve un ritardo costante: spenta, la catena si salta e il WAV
    // resta identico al bit.
    MegaSound megaSound(options.sampleRate);
    megaSound.setAmount(options.megaSound / 100.0f);
    megaSound.reset();
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
        if (megaSound.engaged())
            megaSound.process(chunk.data(), got);
        wav.write(chunk.data(), got);
        total += got;
    }

    std::cout << "Scritti " << formatTime(static_cast<double>(total) / options.sampleRate) << " in "
              << options.output << "\n";
    return 0;
}

enum class AfterTrack { NextFile, Quit };

// La manopola resta com'è da un brano all'altro.
struct MegaSoundKnob {
    int amount = 0; // 0..100
    bool compare = false; // confronto A/B: si sente l'originale
};

AfterTrack playTrack(const Track& track, const SourceFile& source, Decoder& decoder, const Options& options,
                     AudioOutput& output, cli::TerminalInput& keys, MegaSoundKnob& knob)
{
    const bool showStatus = stdoutIsTerminal();
    std::vector<float> chunk(kChunkFrames * kChannels);
    bool decoding = true;
    bool paused = false;

    MegaSound megaSound(options.sampleRate);
    auto applyKnob = [&] { megaSound.setAmount(knob.compare ? 0.0f : knob.amount / 100.0f); };
    applyKnob();
    megaSound.reset();

    auto playbackPosition = [&] {
        return decoder.position() - static_cast<double>(output.queuedFrames()) / options.sampleRate;
    };
    // Dopo seek e cambi di sottobrano l'audio già in coda va scartato.
    auto restart = [&] {
        output.flush();
        megaSound.reset();
        decoding = true;
    };
    auto endStatusLine = [&] {
        if (showStatus)
            std::cout << "\n";
    };

    printTrack(track.name, source, decoder);
    output.flush();
    output.setPaused(false);

    std::vector<bool> muted(decoder.channels().size(), false);
    for (const int channel : options.mute)
        muted[static_cast<size_t>(channel - 1)] = true;

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
        case cli::Key::MegaSoundUp:
        case cli::Key::MegaSoundDown:
            knob.amount = std::clamp(knob.amount + (key == cli::Key::MegaSoundUp ? 10 : -10), 0, 100);
            knob.compare = false;
            applyKnob();
            break;
        case cli::Key::MegaSoundCompare:
            knob.compare = !knob.compare;
            applyKnob();
            break;
        case cli::Key::ToggleChannel:
            if (keys.channel() < static_cast<int>(muted.size())) {
                muted[static_cast<size_t>(keys.channel())] = !muted[static_cast<size_t>(keys.channel())];
                decoder.setChannelMuted(keys.channel(), muted[static_cast<size_t>(keys.channel())]);
            }
            break;
        case cli::Key::None:
            break;
        }

        if (decoding) {
            while (output.writableFrames() >= kChunkFrames) {
                const size_t got = decoder.render(chunk.data(), kChunkFrames);
                // Sempre, anche spenta: così accenderla non fa saltare l'audio.
                megaSound.process(chunk.data(), got);
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
            std::cout << "   sottobrano " << info.currentSubsong + 1 << "/" << info.subsongCount;
            if (knob.amount > 0)
                std::cout << "   MegaSound " << knob.amount << "%" << (knob.compare ? " (A/B: originale)" : "");
            if (std::find(muted.begin(), muted.end(), true) != muted.end()) {
                std::cout << "   zittiti";
                for (size_t i = 0; i < muted.size(); ++i) {
                    if (muted[i])
                        std::cout << " " << i + 1;
                }
            }
            std::cout << "\x1b[K" << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    endStatusLine();
    return cli::interrupted() ? AfterTrack::Quit : AfterTrack::NextFile;
}

int runPlay(const Options& options, const DecoderRegistry& registry)
{
    TrackCollector collector(registry);
    const Playlist playlist = buildPlaylist(options.files, collector);
    if (playlist.tracks.empty())
        return 1;

    AudioOutput output(options.sampleRate);
    cli::TerminalInput keys;
    std::cout << "Uscita: " << output.deviceName() << "\n";
    output.start();

    int failures = playlist.failures;
    MegaSoundKnob knob{options.megaSound};
    for (const Track& track : playlist.tracks) {
        try {
            const SourceFile source = loadSourceFile(track.path);
            const auto decoder = openTrack(source, options, registry);
            std::cout << "\n";
            if (playTrack(track, source, *decoder, options, output, keys, knob) == AfterTrack::Quit)
                break;
        } catch (const std::exception& e) {
            // Dopo Ctrl+C si esce senza segnalare l'errore del brano interrotto.
            if (cli::interrupted())
                break;
            std::cerr << track.name << ": " << e.what() << "\n";
            ++failures;
        }
    }
    output.stop();
    reportSkipped(playlist.skipped);
    return failures == 0 ? 0 : 1;
}

#ifdef MP_WITH_LIBRARY
int runLibraryScan(Library& library, const Options& options)
{
    if (options.files.size() > 1)
        library.setRoot(options.files[1]);
    const std::filesystem::path root = library.root();
    if (root.empty())
        throw std::runtime_error("indica la cartella: megaplayer library scan CARTELLA");
    std::cout << "Libreria: " << root.string() << "\n";

    // La scansione in un thread; qui l'avanzamento e Ctrl+C.
    ScanProgress progress;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    ScanResult result;
    std::string error;
    std::optional<cli::QuietStderr> quiet;
    quiet.emplace();
    std::thread worker([&] {
        try {
            result = library.scan(progress, cancel);
        } catch (const std::exception& e) {
            error = e.what();
        }
        done = true;
    });
    const bool live = stdoutIsTerminal();
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (cli::interrupted())
            cancel = true;
        if (live) {
            if (progress.walking)
                std::cout << "\r  conto i file: " << progress.filesFound << "   " << std::flush;
            else
                std::cout << "\r  " << progress.filesChecked << " di " << progress.filesFound << " file, "
                          << progress.filesRead << " letti, " << progress.tracksFound << " brani   " << std::flush;
        }
    }
    worker.join();
    quiet.reset();
    if (live)
        std::cout << "\r" << std::string(70, ' ') << "\r";
    if (!error.empty())
        throw std::runtime_error(error);

    std::cout << "File: " << result.filesFound << " (" << result.filesRead << " letti, " << result.filesRemoved
              << " tolti)\n";
    std::cout << "Brani nei file letti: " << result.tracksFound << "\n";
    std::cout << "Brani nella libreria: " << library.search({}).total << "\n";
    if (result.cancelled)
        std::cout << "Scansione interrotta: il resto alla prossima.\n";
    return 0;
}

int runLibrarySearch(const Library& library, const Options& options)
{
    LibraryQuery query;
    for (size_t i = 1; i < options.files.size(); ++i)
        query.text += (i > 1 ? " " : "") + options.files[i];
    query.platforms = options.platforms;
    query.formats = options.formats;
    query.folder = options.folder;
    query.limit = options.limit;
    const LibraryPage page = library.search(query);
    for (const LibraryTrack& track : page.tracks) {
        const std::string path = track.folder.empty() ? track.name : track.folder + "/" + track.name;
        std::cout << path << "\n    " << platformName(track.platform);
        if (!track.format.empty())
            std::cout << " · " << track.format;
        if (!track.title.empty())
            std::cout << " · " << track.title;
        if (!track.author.empty())
            std::cout << " · " << track.author;
        if (track.subsongCount > 1)
            std::cout << " · " << track.subsongCount << " sottobrani";
        if (track.duration)
            std::cout << " · " << formatTime(*track.duration);
        std::cout << "\n";
    }
    std::cout << page.total << (page.total == 1 ? " brano" : " brani");
    if (page.total > static_cast<int>(page.tracks.size()))
        std::cout << ", mostrati i primi " << page.tracks.size() << " (-n per altri)";
    std::cout << "\n";
    return 0;
}

int runLibraryStats(const Library& library, const Options& options)
{
    LibraryQuery query;
    query.platforms = options.platforms;
    query.formats = options.formats;
    query.folder = options.folder;
    const LibraryFacets facets = library.facets(query);
    std::cout << "Piattaforme:\n";
    for (const LibraryFacet& facet : facets.platforms)
        std::cout << "  " << facet.count << "\t" << platformName(facet.value) << " (" << facet.value << ")\n";
    std::cout << "Formati:\n";
    for (const LibraryFacet& facet : facets.formats)
        std::cout << "  " << facet.count << "\t" << (facet.value.empty() ? "(sconosciuto)" : facet.value) << "\n";
    return 0;
}
#endif

int runLibrary(const Options& options)
{
#ifdef MP_WITH_LIBRARY
    Library library(options.database.empty() ? Library::defaultPath() : std::filesystem::path(options.database));
    const std::string& what = options.files.front();
    if (what == "scan")
        return runLibraryScan(library, options);
    if (what == "search")
        return runLibrarySearch(library, options);
    if (what == "stats")
        return runLibraryStats(library, options);
    const std::filesystem::path root = library.root();
    std::cout << (root.empty() ? "(nessuna cartella)" : root.string()) << "\n";
    std::cout << "Database: " << library.database().string() << "\n";
    return 0;
#else
    (void)options;
    throw std::runtime_error("libreria non compilata in questa build");
#endif
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
        if (options.command == "library")
            return runLibrary(options);
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
