#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mp {

// I replayer TFMX dell'Amiga vogliono due file, "mdat.Brano" con il brano e
// "smpl.Brano" con i campioni. Le collezioni li conservano anche in altri
// modi, che qui si riconducono a quella coppia.

// Lo "mdat" di un TFMX, riconosciuto come fa UADE.
bool isTfmxSong(const std::vector<uint8_t>& data);

struct TfmxPair {
    std::vector<uint8_t> song;    // il contenuto dello "mdat"
    std::vector<uint8_t> samples; // il contenuto dello "smpl"
    int subsong = -1;             // da suonare, -1 se non indicato
    std::string title;
    std::string author;
};

// I formati in un file solo che contengono la coppia: TFMX-MOD (con titolo,
// autore e sottobrano) e TFMXPAK. Vuoto se `data` non è uno di questi.
std::optional<TfmxPair> unpackTfmxContainer(const std::vector<uint8_t>& data);

// I nomi con cui può trovarsi il secondo file di una coppia che il replayer
// chiama `name`: com'è, con prefisso e suffisso scambiati ("smpl.Brano" e
// "Brano.smpl") e, per i campioni, con l'estensione ".sam".
std::vector<std::string> pairedFileNames(const std::string& name);

// Vero per la metà con i campioni di una coppia ("Brano.sam" o "smpl.Brano")
// se accanto c'è il brano ("Brano.tfx" o "mdat.Brano"): da sola non si
// suona.
bool isSampleHalf(const std::filesystem::path& path);

} // namespace mp
