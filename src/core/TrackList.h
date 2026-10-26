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
    std::filesystem::path path; // il file da aprire (per un archivio, quello estratto)
    std::string name;           // come mostrarlo
    bool fromArchive = false;
    // Come ritrovarlo dopo, anche quando i file estratti non ci sono più:
    // il percorso, o "ARCHIVIO/percorso/interno". resolve() lo riapre.
    std::string ref;
};

// Trasforma i percorsi indicati dall'utente in brani. Gli archivi vengono
// estratti e restano su disco finché il collector vive; i loro file che
// nessun backend riconosce (testi, icone .info, ...) vengono scartati, come
// la metà con i campioni di un brano in due file, che da sola non si suona.
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
    // Può essere anche una cartella: allora i suoi file e quelli delle
    // sottocartelle, in ordine e archivi compresi, scartando quelli che
    // nessun backend riconosce.
    std::vector<Track> collect(const std::string& input);

    // File scartati dall'ultima collect().
    int lastSkipped() const { return lastSkipped_; }

    // Il file da aprire per un ref: lui stesso, o per un brano dentro un
    // archivio il file estratto. Se l'archivio non è già estratto ne estrae
    // solo la cartella del brano. Lancia std::runtime_error se il brano non
    // c'è.
    std::filesystem::path resolve(const std::string& ref);

    // Rimuove i file estratti finora.
    void clear()
    {
        collected_.clear();
        resolved_.clear();
    }

private:
    struct Extracted {
        std::filesystem::path archive; // il file su disco
        ExtractedArchive files;
    };

    // Le voci dell'archivio sotto `inner` (tutte, se è vuoto) che un backend
    // riconosce; false se sotto `inner` non c'è niente.
    bool addArchiveEntries(const std::filesystem::path& path, const std::string& inner,
                           const ExtractedArchive& archive, std::vector<Track>& tracks);

    // Il file estratto per una voce dell'archivio, se c'è già.
    const ExtractedArchive::Entry* findExtracted(const std::filesystem::path& archive, const std::string& name) const;

    const DecoderRegistry& registry_;
    // Gli archivi di collect(), i cui file estratti chi chiama usa
    // direttamente: restano finché vive il collector.
    std::vector<Extracted> collected_;
    // Le cartelle estratte da resolve(), dalla più vecchia: si tengono le
    // ultime, le altre si riestraggono se servono.
    std::vector<Extracted> resolved_;
    int lastSkipped_ = 0;
};

// Divide "giochi/Stormlord.lha/Stormlord/jt.title" nel file che esiste su
// disco e nel percorso che segue, da cercare dentro l'archivio.
std::pair<std::filesystem::path, std::string> splitArchivePath(const std::string& input);

} // namespace mp
