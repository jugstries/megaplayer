// Verifica come si riconducono i TFMX alla coppia mdat+smpl che vogliono i
// replayer, con file costruiti qui (i brani veri sono protetti da diritti):
// - TFMX-MOD e TFMXPAK divisi in brano e campioni, con titolo, autore e
//   sottobrano;
// - i nomi alternativi del secondo file di una coppia;
// - "Brano.sam" accanto a "Brano.tfx" saltato dalla lista dei brani.

#include "core/DecoderRegistry.h"
#include "core/TrackList.h"
#include "decoders/TfmxFiles.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FALLITO: %s\n", what);
        ++failures;
    }
}

using Bytes = std::vector<uint8_t>;

void append(Bytes& out, const std::string& text)
{
    out.insert(out.end(), text.begin(), text.end());
}

void appendLe32(Bytes& out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Uno "mdat" finto: basta l'intestazione per riconoscerlo.
Bytes fakeSong()
{
    Bytes song;
    append(song, "TFMX-SONG ");
    song.resize(0x220, 0x11);
    return song;
}

void infoEntry(Bytes& out, uint8_t type, const std::string& value)
{
    out.push_back(type);
    out.push_back(static_cast<uint8_t>(value.size()));
    out.push_back(static_cast<uint8_t>(value.size() >> 8));
    append(out, value);
}

bool contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

int main()
{
    const Bytes song = fakeSong();
    const Bytes samples(300, 0x22);

    check(mp::isTfmxSong(song), "mdat non riconosciuto");
    {
        Bytes pak;
        append(pak, "TFMXPAK 1 1 >>>");
        check(!mp::isTfmxSong(pak), "un TFMXPAK scambiato per un mdat");
    }

    {
        // TFMX-MOD: intestazione, mdat, smpl, informazioni (autore in
        // Latin-1, gioco, sottobrano, titolo).
        Bytes file;
        append(file, "TFMX-MOD");
        const uint32_t samplesStart = 20 + static_cast<uint32_t>(song.size());
        appendLe32(file, samplesStart);
        appendLe32(file, samplesStart + static_cast<uint32_t>(samples.size()));
        appendLe32(file, 0);
        file.insert(file.end(), song.begin(), song.end());
        file.insert(file.end(), samples.begin(), samples.end());
        infoEntry(file, 1, "Chris H\xfclsbeck");
        infoEntry(file, 2, "Turrican III");
        infoEntry(file, 5, std::string(1, '\x03'));
        infoEntry(file, 6, "Level Complete");
        file.push_back(0);

        const auto pair = mp::unpackTfmxContainer(file);
        check(pair.has_value(), "TFMX-MOD non riconosciuto");
        if (pair) {
            check(pair->song == song && pair->samples == samples, "TFMX-MOD diviso male");
            check(pair->subsong == 3, "TFMX-MOD: sottobrano sbagliato");
            check(pair->title == "Turrican III - Level Complete", "TFMX-MOD: titolo sbagliato");
            check(pair->author == "Chris H\xc3\xbclsbeck", "TFMX-MOD: autore sbagliato");
        }

        Bytes broken = file;
        broken[12] = 0xff; // informazioni oltre la fine del file
        check(!mp::unpackTfmxContainer(broken), "TFMX-MOD con offset sbagliati accettato");
    }

    {
        Bytes file;
        append(file, "TFMXPAK " + std::to_string(song.size()) + " " + std::to_string(samples.size()) + " >>>");
        file.insert(file.end(), song.begin(), song.end());
        file.insert(file.end(), samples.begin(), samples.end());
        const auto pair = mp::unpackTfmxContainer(file);
        check(pair && pair->song == song && pair->samples == samples, "TFMXPAK diviso male");
        file.pop_back();
        check(!mp::unpackTfmxContainer(file), "TFMXPAK troncato accettato");
    }

    {
        const auto fromPrefix = mp::pairedFileNames("smpl.Brano 1");
        check(contains(fromPrefix, "smpl.Brano 1") && contains(fromPrefix, "Brano 1.smpl")
                  && contains(fromPrefix, "Brano 1.sam"),
              "nomi alternativi di smpl.Brano");
        const auto fromSuffix = mp::pairedFileNames("Brano.smpl");
        check(contains(fromSuffix, "smpl.Brano") && contains(fromSuffix, "Brano.sam"), "nomi alternativi di Brano.smpl");
    }

    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "megaplayer-tfmx-test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        for (const char* name : {"Brano.tfx", "Brano.sam", "Altro.sam", "mdat.Coppia", "smpl.Coppia"})
            std::ofstream(dir / name, std::ios::binary) << "x";

        mp::DecoderRegistry registry;
        mp::TrackCollector collector(registry);
        check(collector.collect((dir / "Brano.sam").string()).empty() && collector.lastSkipped() == 1,
              "Brano.sam accanto a Brano.tfx non saltato");
        check(collector.collect((dir / "smpl.Coppia").string()).empty(), "smpl.Coppia accanto a mdat.Coppia non saltato");
        check(collector.collect((dir / "Brano.tfx").string()).size() == 1, "Brano.tfx saltato");
        check(collector.collect((dir / "Altro.sam").string()).size() == 1, "un .sam senza brano accanto saltato");
        std::filesystem::remove_all(dir);
    }

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
