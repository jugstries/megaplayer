#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

// Una voce di un file M3U: un percorso, che può anche essere
// "ARCHIVIO/percorso/interno" come i riferimenti della libreria, e il
// sottobrano scelto.
struct M3uEntry {
    std::string path;
    std::optional<int> subsong;     // base 0; nullopt per il file intero
    std::string title;              // da #EXTINF, vuoto se manca
    std::optional<double> duration; // da #EXTINF, in secondi
};

struct M3uPlaylist {
    std::string name; // da #PLAYLIST, vuoto se manca
    std::vector<M3uEntry> entries;
    int skipped = 0; // voci che non sono file (indirizzi web)
};

// Legge il testo di un M3U o M3U8, in UTF-8 o, se non lo è, in Latin-1. I
// percorsi relativi valgono rispetto a `folder`, la cartella del file, e
// tornano completi; quelli file:// si decodificano. Il sottobrano viene
// dalla riga #MEGAPLAYER-SUBSONG che scrive formatM3u.
M3uPlaylist parseM3u(std::string_view text, const std::filesystem::path& folder);

// Il testo di un M3U8: UTF-8, #EXTM3U, #EXTINF con durata e titolo. I file
// dentro `folder` hanno il percorso relativo, che resta valido spostando la
// cartella intera; gli altri quello completo. Il sottobrano, che gli altri
// lettori non conoscono, va in una riga #MEGAPLAYER-SUBSONG (da 1), che loro
// ignorano suonando il file intero.
std::string formatM3u(const M3uPlaylist& playlist, const std::filesystem::path& folder);

} // namespace mp
