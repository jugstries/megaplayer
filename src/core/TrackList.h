#pragma once

#include "Archive.h"
#include "DecoderRegistry.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mp {

// Un brano da suonare: un file indicato dall'utente o estratto da un archivio.
struct Track {
    std::filesystem::path path;
    std::string name; // come mostrarlo
    bool fromArchive = false;
};

// Trasforma i percorsi indicati dall'utente in brani. Gli archivi vengono
// estratti e restano su disco finché il collector vive; i loro file che
// nessun backend riconosce (testi, icone .info, ...) vengono scartati.
class TrackCollector {
public:
    explicit TrackCollector(const DecoderRegistry& registry)
        : registry_(registry)
    {
    }

    // `input` può essere un file, un archivio o "ARCHIVIO/percorso/interno"
    // per scegliere un file o una cartella dentro l'archivio. Un file
    // normale viene restituito senza controllarne il formato. Lancia
    // std::runtime_error se l'archivio è danneggiato o la selezione vuota.
    std::vector<Track> collect(const std::string& input);

    // File di archivio scartati dall'ultima collect().
    int lastSkipped() const { return lastSkipped_; }

    // Rimuove i file estratti finora.
    void clear() { archives_.clear(); }

private:
    const DecoderRegistry& registry_;
    std::vector<ExtractedArchive> archives_;
    int lastSkipped_ = 0;
};

// Divide "giochi/Stormlord.lha/Stormlord/jt.title" nel file che esiste su
// disco e nel percorso che segue, da cercare dentro l'archivio.
std::pair<std::filesystem::path, std::string> splitArchivePath(const std::string& input);

} // namespace mp
