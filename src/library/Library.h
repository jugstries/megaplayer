#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace mp {

// Un brano della libreria: un file musicale, o un file dentro un archivio.
struct LibraryTrack {
    // Come ritrovarlo: il percorso del file, o "ARCHIVIO/percorso/interno"
    // (la forma che TrackCollector e la CLI capiscono).
    std::string ref;
    std::string file;   // il file su disco (l'archivio, se il brano è dentro)
    std::string entry;  // il percorso nell'archivio, vuoto per un file normale
    std::string name;   // il nome del file del brano
    std::string folder; // la cartella rispetto alla radice, archivio compreso
    std::string backend;
    std::string format;
    std::string player;
    std::string platform; // identificativo da core/Platform.h
    std::string title;
    std::string author;
    int subsongCount = 1; // 0 se non si sanno senza suonare il brano (UADE)
    int startSubsong = 0;
    std::optional<double> duration; // del sottobrano iniziale
};

struct LibraryQuery {
    std::string text;                   // parole cercate in titolo, autore, nome e cartella
    std::vector<std::string> platforms; // vuoto: tutte
    std::vector<std::string> formats;   // vuoto: tutti
    int offset = 0;
    int limit = 200;
};

struct LibraryPage {
    int total = 0; // brani che soddisfano la ricerca, non solo quelli della pagina
    std::vector<LibraryTrack> tracks;
};

struct LibraryFacet {
    std::string value;
    int count = 0;
};

struct LibraryFacets {
    std::vector<LibraryFacet> platforms;
    std::vector<LibraryFacet> formats;
};

// L'avanzamento di una scansione, leggibile da un altro thread.
struct ScanProgress {
    std::atomic<bool> walking{true};    // ancora a contare i file della cartella
    std::atomic<int> filesFound{0};     // file nella cartella
    std::atomic<int> filesChecked{0};   // file controllati, già noti o letti
    std::atomic<int> filesRead{0};      // file nuovi o cambiati, aperti
    std::atomic<int> tracksFound{0};    // brani nei file letti
};

struct ScanResult {
    int filesFound = 0;
    int filesRead = 0;
    int filesRemoved = 0;
    int tracksFound = 0;
    bool cancelled = false;
};

// L'indice della libreria, in un database SQLite: la cartella radice, i suoi
// file con data e dimensione, e per ogni brano formato, piattaforma, titolo,
// autore e sottobrani con le durate. Cerca e filtra senza toccare i file.
//
// scan() può girare in un thread mentre gli altri metodi vengono chiamati da
// un altro: usa una connessione sua, e il database è in modalità WAL.
class Library {
public:
    // Apre o crea il database, e la cartella che lo contiene. Lancia
    // std::runtime_error se non ci riesce.
    explicit Library(const std::filesystem::path& database);
    ~Library();

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    // Dove sta il database se non si sceglie altro: la cartella dei dati
    // dell'utente (Application Support su macOS, %APPDATA% su Windows,
    // ~/.local/share altrove).
    static std::filesystem::path defaultPath();

    const std::filesystem::path& database() const { return database_; }

    // La cartella radice, vuota se non è stata scelta. Cambiarla svuota
    // l'indice, che riguardava l'altra.
    std::filesystem::path root() const;
    void setRoot(const std::filesystem::path& root);

    // Allinea l'indice alla cartella radice: apre solo i file nuovi o
    // cambiati (per data o dimensione) e toglie quelli spariti. Con `cancel`
    // si ferma: quello che ha letto resta, i file spariti si tolgono alla
    // scansione completa successiva.
    ScanResult scan(ScanProgress& progress, const std::atomic<bool>& cancel);

    LibraryPage search(const LibraryQuery& query) const;
    // Quanti brani per piattaforma e per formato, con il solo testo della
    // ricerca: i filtri si combinano guardando questi conteggi.
    LibraryFacets facets(const LibraryQuery& query) const;
    std::optional<LibraryTrack> track(const std::string& ref) const;
    // La durata di ogni sottobrano, nullopt dove non si sa.
    std::vector<std::optional<double>> subsongDurations(const std::string& ref) const;

private:
    std::string setting(const std::string& key) const;

    std::filesystem::path database_;
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_; // per db_
};

} // namespace mp
