import 'models.dart';

/// Dove l'app tiene playlist, coda e impostazioni: il database della
/// libreria nel core (src/library). L'implementazione vera è
/// [FfiLibraryStore]; i test ne usano una finta. I metodi lanciano
/// [CoreException].
abstract interface class LibraryStore {
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

  /// "" se l'impostazione non è mai stata scritta.
  String setting(String key);
  void setSetting(String key, String value);

  void dispose();
}
