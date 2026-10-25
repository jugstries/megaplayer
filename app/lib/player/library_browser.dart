import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';

import '../core/library_store.dart';
import '../core/models.dart';

/// La libreria come la vede l'app: la cartella della musica, la scansione,
/// la ricerca con i filtri e la cartella in cui si naviga.
///
/// Senza ricerca né filtri mostra le sottocartelle e i brani della cartella
/// corrente; con una ricerca, tutti i brani che la soddisfano da lì in giù.
class LibraryBrowser extends ChangeNotifier {
  LibraryBrowser(this.store);

  final LibraryStore? store;

  /// La cartella della musica, "" se non è stata scelta.
  String root = '';
  ScanStatus scan = ScanStatus.idle;
  LibraryQuery query = const LibraryQuery();

  /// Le sottocartelle della cartella corrente; vuote durante una ricerca.
  List<LibraryFacet> folders = const [];
  List<LibraryItem> items = const [];

  /// Tutti i brani da mostrare: [items] ne ha solo le prime pagine.
  int total = 0;
  LibraryFacets facets = LibraryFacets.empty;
  String? error;

  static const pageSize = 200;
  static const _searchDelay = Duration(milliseconds: 150);
  static const _scanRefresh = Duration(seconds: 1);

  Timer? _searchTimer;
  String? _pendingRoot;
  DateTime _lastScanRefresh = DateTime(0);

  bool get available => store != null;
  bool get scanning => scan.running || _pendingRoot != null;

  /// Senza ricerca né filtri: si naviga per cartelle.
  bool get browsing => !query.filtered;

  /// Le cartelle dalla radice a quella corrente.
  List<String> get path => query.folder.isEmpty ? const [] : query.folder.split('/');

  /// All'avvio: legge la cartella scelta l'ultima volta e la aggiorna.
  void restore() {
    _guard((store) {
      root = store.root();
      if (root.isNotEmpty) _startScan(store);
    });
    _search();
  }

  /// Da chiamare spesso (a ogni aggiornamento del player): segue la
  /// scansione e intanto rinfresca i risultati, una volta al secondo.
  void poll() {
    final store = this.store;
    if (store == null || !scan.running) return;
    scan = store.scanStatus();
    if (scan.running) {
      final now = DateTime.now();
      if (now.difference(_lastScanRefresh) >= _scanRefresh) {
        _lastScanRefresh = now;
        _search(keepLoaded: true);
      } else {
        notifyListeners();
      }
      return;
    }
    if (scan.error != null) error = scan.error;
    final pending = _pendingRoot;
    _pendingRoot = null;
    if (pending != null) {
      _applyRoot(pending);
    } else {
      _search(keepLoaded: true);
    }
  }

  /// Sceglie la cartella della musica e la legge. L'indice della cartella
  /// di prima si svuota; le playlist restano. Durante una scansione la si
  /// ferma, e si cambia appena è ferma.
  void setRoot(String path) {
    final store = this.store;
    if (store == null) return;
    if (scan.running) {
      _pendingRoot = path;
      store.cancelScan();
      notifyListeners();
      return;
    }
    _applyRoot(path);
  }

  void _applyRoot(String path) {
    _guard((store) {
      if (path != root) {
        store.setRoot(path);
        root = store.root();
        query = query.copyWith(folder: '');
      }
      _startScan(store);
    });
    _search();
  }

  /// "Aggiorna": rilegge i file nuovi o cambiati e toglie quelli spariti.
  void rescan() {
    _guard(_startScan);
    notifyListeners();
  }

  void cancelScan() {
    _pendingRoot = null;
    _guard((store) => store.cancelScan());
    notifyListeners();
  }

  void _startScan(LibraryStore store) {
    if (root.isEmpty || scan.running) return;
    error = null;
    store.startScan();
    scan = store.scanStatus();
    _lastScanRefresh = DateTime.now();
  }

  // --- Ricerca e navigazione ---

  /// Il testo cercato: la ricerca parte quando si smette di scrivere.
  void setText(String text) {
    if (text == query.text) return;
    query = query.copyWith(text: text);
    _searchTimer?.cancel();
    _searchTimer = Timer(_searchDelay, _search);
  }

  void togglePlatform(String id) {
    query = query.copyWith(platforms: _toggle(query.platforms, id));
    _search();
  }

  void toggleFormat(String id) {
    query = query.copyWith(formats: _toggle(query.formats, id));
    _search();
  }

  /// Sostituisce i valori scelti di un filtro, o di tutti e due.
  void setFilters({Set<String>? platforms, Set<String>? formats}) {
    query = query.copyWith(platforms: platforms, formats: formats);
    _search();
  }

  void clearFilters() {
    query = query.copyWith(text: '', platforms: const {}, formats: const {});
    _search();
  }

  /// [folder] rispetto alla radice, "" per la radice.
  void openFolder(String folder) {
    query = query.copyWith(folder: folder);
    _search();
  }

  void openSubfolder(String name) => openFolder(subfolderPath(name));

  String subfolderPath(String name) => query.folder.isEmpty ? name : '${query.folder}/$name';

  /// Altre righe in fondo all'elenco, quando ci si arriva.
  void loadMore() {
    if (items.length >= total) return;
    _guard((store) {
      final page = store.search(_listQuery, offset: items.length, limit: pageSize);
      items = [...items, ...page.items];
      total = page.total;
    });
    notifyListeners();
  }

  /// Tutti i brani della ricerca da [folder] in giù (dalla cartella
  /// corrente se manca), da mettere in coda o in una playlist.
  List<Track> allTracks({String? folder}) {
    final scope = query.copyWith(folder: folder ?? query.folder, subfolders: true);
    return _guard((store) {
          final total = store.search(scope, limit: 0).total;
          return [for (final item in store.search(scope, limit: total).items) item.toTrack()];
        }) ??
        const [];
  }

  /// Un brano della libreria con le durate dei sottobrani; null se non c'è.
  LibraryItem? lookup(String ref) => _guard<LibraryItem?>((store) => store.track(ref));

  /// Nella cartella i brani che stanno proprio lì; in una ricerca anche
  /// quelli delle sottocartelle.
  LibraryQuery get _listQuery => query.copyWith(subfolders: !browsing);

  void _search({bool keepLoaded = false}) {
    _searchTimer?.cancel();
    _guard((store) {
      final limit = keepLoaded ? math.max(items.length, pageSize) : pageSize;
      folders = browsing ? store.folders(query) : const [];
      final page = store.search(_listQuery, limit: limit);
      items = page.items;
      total = page.total;
      facets = store.facets(query.copyWith(subfolders: true));
    });
    notifyListeners();
  }

  /// Gli errori del database diventano un messaggio, mai un crash.
  T? _guard<T>(T Function(LibraryStore store) body) {
    final store = this.store;
    if (store == null) return null;
    try {
      return body(store);
    } on CoreException catch (e) {
      error = '$e';
      return null;
    }
  }

  static Set<String> _toggle(Set<String> values, String value) =>
      values.contains(value) ? ({...values}..remove(value)) : {...values, value};

  @override
  void dispose() {
    _searchTimer?.cancel();
    super.dispose();
  }
}
