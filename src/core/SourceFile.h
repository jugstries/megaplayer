#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mp {

// Un file da riprodurre, già decompresso se era impacchettato con un packer
// d'epoca (PowerPacker, XPK, Imploder...).
struct SourceFile {
    std::filesystem::path path;
    std::vector<uint8_t> data;
    // Packer rimossi, dal più esterno al più interno.
    std::vector<std::string> packers;

    bool wasUnpacked() const { return !packers.empty(); }

    // Nome del file senza directory, es. "mod.enigma". Alcuni backend
    // riconoscono il formato anche dal prefisso o dall'estensione.
    std::string fileName() const { return path.filename().string(); }
};

// Legge il file e rimuove gli eventuali strati di compressione.
// Lancia std::runtime_error se il file non è leggibile.
SourceFile loadSourceFile(const std::filesystem::path& path);

} // namespace mp
