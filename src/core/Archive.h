#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mp {

// Un archivio (LHA, ZIP, 7z, tar...) estratto in una directory temporanea,
// cancellata alla distruzione. I file vanno su disco, e non solo in memoria,
// perché alcuni formati Amiga sono divisi in più file (es. TFMX mdat + smpl)
// e UADE cerca i file accessori accanto a quello principale.
class ExtractedArchive {
public:
    struct Entry {
        std::filesystem::path path; // file estratto
        std::string name;           // percorso dentro l'archivio
    };

    ExtractedArchive(std::filesystem::path directory, std::vector<Entry> entries);
    ~ExtractedArchive();

    ExtractedArchive(ExtractedArchive&& other) noexcept;
    ExtractedArchive& operator=(ExtractedArchive&& other) noexcept;
    ExtractedArchive(const ExtractedArchive&) = delete;
    ExtractedArchive& operator=(const ExtractedArchive&) = delete;

    // Nell'ordine in cui compaiono nell'archivio; solo file regolari.
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::filesystem::path directory_;
    std::vector<Entry> entries_;
};

// Estrae `path` se è un archivio; std::nullopt se non lo è (o se il supporto
// agli archivi non è compilato). Con `folder` estrae solo i file di quella
// cartella dell'archivio ("" la radice), senza le sottocartelle: per suonare
// un brano basta la sua cartella, dove stanno anche i file che lo
// accompagnano. Lancia std::runtime_error se l'archivio è riconosciuto ma
// danneggiato.
std::optional<ExtractedArchive> openArchive(const std::filesystem::path& path,
                                            const std::optional<std::string>& folder = std::nullopt);

} // namespace mp
