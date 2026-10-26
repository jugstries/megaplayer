import 'models.dart';

/// Il database della libreria nel core (src/library): l'indice della
/// cartella della musica, le playlist, la coda e le impostazioni.
/// L'implementazione vera è [FfiLibraryStore]; i test ne usano una finta. I
/// metodi lanciano [CoreException].
abstract interface class LibraryStore {
  /// La cartella della libreria, "" se non è stata scelta.
  String root();

  /// Cambiarla svuota l'indice (le playlist restano). Non durante una
  /// scansione.
  void setRoot(String path);

  /// Avvia la scansione in background: legge i file nuovi o cambiati e
  /// toglie quelli spariti.
  void startScan();
  void cancelScan();
  ScanStatus scanStatus();

  LibraryPage search(LibraryQuery query, {int offset = 0, int limit = 200});

  /// Quanti brani per piattaforma e per formato, ognuno contato con il
  /// filtro dell'altro.
  LibraryFacets facets(LibraryQuery query);

  /// Le sottocartelle di query.folder con brani della ricerca.
  List<LibraryFacet> folders(LibraryQuery query);

  /// Un brano con la durata di ogni sottobrano; null se non è nella libreria.
  LibraryItem? track(String ref);

  /// Le playlist dell'utente, senza la coda.
  List<PlaylistInfo> playlists();
  int createPlaylist(String name);
  void renamePlaylist(int id, String name);
  void deletePlaylist(int id);

  /// La playlist che fa da coda.
  int queueId();
  List<Track> entries(int id);

  /// Sostituisce le voci; i doppioni (stesso brano e sottobrano) si scartano.
  void setEntries(int id, List<Track> entries);

  /// Una playlist nuova da un file M3U o M3U8, senza le voci che non si
  /// trovano; se non se ne trova nessuna lancia e non crea niente.
  PlaylistImport importPlaylist(String path);

  /// Scrive una playlist, o la coda, in un file M3U8.
  void exportPlaylist(int id, String path);

  /// "" se l'impostazione non è mai stata scritta.
  String setting(String key);
  void setSetting(String key, String value);

  void dispose();
}
