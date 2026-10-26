import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../core/library_store.dart';
import '../core/models.dart';
import '../core/player_core.dart';
import '../update/updates.dart';
import 'library_browser.dart';

enum Skin { modern, retro }

/// L'esportazione del brano: in corso, o l'ultima finita, finché la finestra
/// non la toglie.
class ExportJob {
  ExportJob(this.path);

  /// Il file, .wav o .mp3.
  final String path;
  ExportStatus status = const ExportStatus(running: true);

  bool get running => status.running;
  bool get done => !status.running && !status.failed && !status.cancelled;
}

/// Con che cosa suonano i MIDI quando non ci sono le ROM dei moduli Roland:
/// il SoundFont incluso nell'app, uno scelto dall'utente, o i chip FM.
enum SoundFontChoice { included, custom, fm }

/// Stato e comandi del player, condivisi dalle due skin: la coda, il brano
/// corrente, le playlist, la libreria e quello che il core riporta a ogni
/// aggiornamento.
///
/// Con uno [store], coda, playlist, skin, volume e manopola restano da un
/// avvio all'altro e c'è la libreria; senza, l'app funziona lo stesso e non
/// ricorda niente. Con [updates] l'app si aggiorna da sola.
class PlayerController extends ChangeNotifier {
  PlayerController(this.core, {this.store, this.updates})
      : backends = core.backends(),
        rolandDevices = core.rolandDevices(),
        includedSoundFont = core.soundFont(),
        library = LibraryBrowser(store) {
    library.addListener(notifyListeners);
    updates?.addListener(notifyListeners);
  }

  final PlayerCore core;
  final LibraryStore? store;
  final List<Backend> backends;

  /// Null nelle build di sviluppo e dove l'app non sa ancora aggiornarsi.
  final Updates? updates;

  /// La cartella della musica, con ricerca e filtri.
  final LibraryBrowser library;

  /// La coda: quello che suona, in ordine.
  final List<Track> tracks = [];
  int? currentIndex;
  CoreStatus status = CoreStatus.idle;
  TrackDetails? details;
  Float32List scope = Float32List(0);
  double volume = 0.8;

  /// La manopola MegaSound, 0..1. Resta dov'è da un brano all'altro.
  double megaSound = 0;

  /// Confronto A/B: la manopola resta dov'è ma si sente l'originale.
  bool megaSoundBypassed = false;
  Skin skin = Skin.modern;
  String? error;

  /// Le playlist salvate (la coda non c'è).
  List<PlaylistInfo> playlists = const [];

  /// I canali del brano zittiti, e quelli in solo: se ce n'è almeno uno in
  /// solo si sentono solo loro. Valgono fino al brano dopo.
  Set<int> mutedChannels = const {};
  Set<int> soloChannels = const {};

  /// Quello che il core sta zittendo, per mandargli solo i cambi.
  List<bool> _silenced = const [];

  /// I moduli Roland di 88emu per i MIDI, con le ROM trovate; vuoto se il
  /// core non ha 88emu.
  List<RolandDevice> rolandDevices;

  /// La cartella con le ROM scelta dall'utente ('' se non l'ha scelta) e il
  /// Sound Canvas per i MIDI GS e GM ('' per lasciarlo scegliere al core).
  String romFolder = '';
  String rolandDevice = '';

  /// Il SoundFont che viaggia con l'app ('' se il core non ne ha uno), la
  /// scelta per i MIDI senza ROM e il SoundFont scelto dall'utente.
  final String includedSoundFont;
  SoundFontChoice soundFontChoice = SoundFontChoice.included;
  String customSoundFont = '';

  static const _files = MethodChannel('megaplayer/files');
  static const scopeFrames = 1024;

  static const _skinKey = 'app.skin';
  static const _volumeKey = 'app.volume';
  static const _megaSoundKey = 'app.megasound';
  static const _currentKey = 'app.queue.current';
  static const _romFolderKey = 'midi.roms';
  static const _rolandKey = 'midi.roland';
  static const _soundFontChoiceKey = 'midi.soundfont';
  static const _customSoundFontKey = 'midi.soundfont.path';

  Timer? _timer;
  int? _queueId;
  final Map<int, List<Track>> _playlistEntries = {};

  Track? get currentTrack => currentIndex == null ? null : tracks[currentIndex!];

  /// Quello che suona come voce di playlist: il sottobrano che si sente, se
  /// il file ne ha più d'uno, altrimenti il file intero.
  Track? get playingEntry => currentTrack?.withSubsong(status.subsongCount > 1 ? status.subsong : null);

  /// Riprende coda, brano corrente (senza farlo partire), playlist, skin,
  /// volume e manopola dall'ultima volta, e aggiorna la libreria. Prima di
  /// [start].
  void restore() {
    _withStore((store) {
      _queueId = store.queueId();
      tracks
        ..clear()
        ..addAll(store.entries(_queueId!));
      final current = int.tryParse(store.setting(_currentKey));
      currentIndex = current != null && current >= 0 && current < tracks.length ? current : null;
      skin = Skin.values.asNameMap()[store.setting(_skinKey)] ?? skin;
      volume = double.tryParse(store.setting(_volumeKey))?.clamp(0.0, 1.0) ?? volume;
      megaSound = double.tryParse(store.setting(_megaSoundKey))?.clamp(0.0, 1.0) ?? megaSound;
      playlists = store.playlists();
      romFolder = store.setting(_romFolderKey);
      rolandDevice = store.setting(_rolandKey);
      soundFontChoice = SoundFontChoice.values.asNameMap()[store.setting(_soundFontChoiceKey)] ?? soundFontChoice;
      customSoundFont = store.setting(_customSoundFontKey);
    });
    // Prima della libreria, che riconosce i MIDI con i moduli che ci sono.
    if (romFolder.isNotEmpty) core.addRolandRomDirectory(romFolder);
    core.setRolandDevice(rolandDevice);
    _applySoundFont();
    rolandDevices = core.rolandDevices();
    library.restore();
  }

  /// Avvia gli aggiornamenti periodici dello stato (circa 30 al secondo).
  void start() {
    core.setVolume(volume);
    _applyMegaSound();
    _timer ??= Timer.periodic(const Duration(milliseconds: 33), (_) => refresh());
  }

  /// Legge lo stato dal core. A fine sottobrano, per una voce col file
  /// intero si passa al sottobrano successivo; altrimenti alla voce dopo.
  void refresh() {
    library.poll();
    _pollExport();
    status = core.status();
    scope = status.isPlaying ? core.scope(scopeFrames) : _flatten(scope);
    if (status.state == PlaybackState.ended) {
      final track = currentTrack;
      if (track != null && track.subsong == null && status.subsong + 1 < status.subsongCount) {
        core.selectSubsong(status.subsong + 1);
        status = core.status();
      } else {
        _advance();
      }
    }
    notifyListeners();
  }

  /// In pausa o da fermo l'oscilloscopio non resta congelato: si appiattisce
  /// in circa un terzo di secondo, a ogni aggiornamento un po' di più.
  static Float32List _flatten(Float32List samples) {
    var peak = 0.0;
    for (final value in samples) {
      peak = math.max(peak, value.abs());
    }
    if (peak == 0) return samples; // già piatto: niente da ridisegnare
    if (peak < 0.003) return Float32List(samples.length);
    return Float32List.fromList([for (final value in samples) value * 0.72]);
  }

  void _advance() {
    final index = currentIndex;
    if (index != null && index + 1 < tracks.length) {
      playIndex(index + 1);
    } else {
      core.stop();
      status = CoreStatus.idle;
    }
  }

  // --- L'esportazione ---

  ExportJob? exportJob;

  /// Il brano da riprendere dopo l'esportazione, che ferma la riproduzione.
  ({int index, int subsong, double position, bool playing})? _resume;

  bool get exporting => exportJob?.running ?? false;

  /// Se la MegaSound e i canali del brano cambierebbero il file esportato.
  bool get megaSoundAudible => megaSound > 0 && !megaSoundBypassed;
  bool get channelsChanged => [for (var i = 0; i < channels.length; i++) !channelAudible(i)].contains(true);

  /// Il nome proposto per il file: il titolo o il nome del brano, con il
  /// sottobrano se ce n'è più d'uno, senza i caratteri che i file non
  /// ammettono.
  String exportName() {
    final track = currentTrack;
    if (track == null) return 'megaplayer';
    final title = details?.title;
    var name = title != null && title.isNotEmpty ? title : track.fileName;
    if (status.subsongCount > 1) name = '$name (sottobrano ${status.subsong + 1})';
    return name.replaceAll(RegExp(r'[/\\:*?"<>|]'), '-').trim();
  }

  /// La finestra del sistema per scegliere dove salvare, poi l'esportazione.
  Future<void> chooseExportFile({required bool mp3, bool withMegaSound = true, bool withChannels = true}) async {
    final extension = mp3 ? 'mp3' : 'wav';
    final path = await _files.invokeMethod<String>('saveFile', {
      'name': '${exportName()}.$extension',
      'message': 'Esporta il brano che suona',
      'extensions': [extension],
    });
    if (path != null) startExport(path, withMegaSound: withMegaSound, withChannels: withChannels);
  }

  /// Esporta il brano che suona com'è adesso: il sottobrano, i canali che si
  /// sentono, la MegaSound. La riproduzione si ferma e alla fine riprende da
  /// dov'era.
  void startExport(String path, {bool withMegaSound = true, bool withChannels = true}) {
    final track = currentTrack;
    if (track == null || exporting) return;
    final playing = status.isPlaying;
    final active = status.state != PlaybackState.idle && status.state != PlaybackState.ended;
    _resume = active
        ? (index: currentIndex!, subsong: status.subsong, position: status.position, playing: playing)
        : null;
    try {
      core.startExport(
        track.ref,
        subsong: active ? status.subsong : (track.subsong ?? 0),
        path: path,
        megaSound: withMegaSound && megaSoundAudible ? megaSound : 0,
        muted: withChannels ? [for (var i = 0; i < channels.length; i++) if (!channelAudible(i)) i] : const [],
        title: details?.title.isNotEmpty == true ? details!.title : track.fileName,
        artist: details?.author ?? '',
        album: track.location,
      );
      exportJob = ExportJob(path)..status = core.exportStatus();
      error = null;
    } on CoreException catch (e) {
      _resume = null;
      error = 'esportazione: $e';
    }
    status = core.status();
    notifyListeners();
  }

  void cancelExport() => core.cancelExport();

  /// La finestra dell'esportazione si chiude.
  void dismissExport() {
    if (exporting) return;
    exportJob = null;
    notifyListeners();
  }

  void _pollExport() {
    final job = exportJob;
    if (job == null || !job.running) return;
    job.status = core.exportStatus();
    if (!job.running) _resumeAfterExport();
  }

  // Lo stesso brano, sottobrano e punto, con i canali e la MegaSound di
  // prima (playIndex li azzererebbe).
  void _resumeAfterExport() {
    final resume = _resume;
    _resume = null;
    if (resume == null || resume.index != currentIndex) return;
    try {
      core.play(tracks[resume.index].ref);
      if (resume.subsong > 0 && resume.subsong < core.status().subsongCount) core.selectSubsong(resume.subsong);
      details = core.details();
      _silenced = List.filled(channels.length, false);
      for (var i = 0; i < channels.length; i++) {
        if (!channelAudible(i)) core.setChannelMuted(i, true);
      }
      _silenced = [for (var i = 0; i < channels.length; i++) !channelAudible(i)];
      if (resume.position > 1) core.seek(resume.position);
      if (!resume.playing) core.setPaused(true);
    } on CoreException catch (e) {
      error = '${tracks[resume.index].fileName}: $e';
    }
  }

  /// La finestra di apertura file del sistema, poi [addPaths].
  Future<AddedFiles> openFiles() async {
    final paths = await _files.invokeListMethod<String>('openFiles') ?? const [];
    return addPaths(paths);
  }

  /// La finestra del sistema per scegliere la cartella della musica; null
  /// se si annulla.
  Future<String?> chooseFolder() => _files.invokeMethod<String>(
        'openFolder',
        {'start': library.root, 'message': 'Scegli la cartella con la tua musica'},
      );

  /// I file passati da riga di comando all'avvio.
  Future<void> openInitialFiles() async {
    final paths = await _files.invokeListMethod<String>('initialFiles') ?? const [];
    addPaths(paths);
  }

  /// I file M3U, che diventano playlist invece di andare in coda.
  static bool isPlaylistFile(String path) => RegExp(r'\.m3u8?$', caseSensitive: false).hasMatch(path);

  /// Aggiunge file e archivi alla coda, come [enqueue]; i file M3U
  /// diventano playlist. Vale per quelli scelti, passati all'avvio o
  /// trascinati sulla finestra.
  AddedFiles addPaths(List<String> paths) {
    final entries = <Track>[];
    final imported = <PlaylistImport>[];
    final problems = <String>[];
    for (final path in paths) {
      final name = path.split(pathSeparators).last;
      try {
        if (isPlaylistFile(path)) {
          final store = this.store;
          if (store == null) throw const CoreException('le playlist non si possono salvare');
          imported.add(store.importPlaylist(path));
        } else {
          entries.addAll(core.collect(path));
        }
      } on CoreException catch (e) {
        problems.add(isPlaylistFile(path) ? '$e' : '$name: $e');
      }
    }
    if (imported.isNotEmpty) refreshPlaylists();
    error = null;
    final added = enqueue(entries);
    // Dopo enqueue, che facendo partire un brano toglierebbe il messaggio.
    if (problems.isNotEmpty) {
      error = [...problems, ?error].join('\n');
      notifyListeners();
    }
    return AddedFiles(added: added, requested: entries.length, playlists: imported);
  }

  /// Mette in fondo alla coda le voci che non ci sono già, e quante sono; se
  /// non sta suonando nulla parte la prima.
  int enqueue(List<Track> entries) {
    final firstNew = tracks.length;
    _addEntries(tracks, entries);
    final added = tracks.length - firstNew;
    if (added > 0) _saveQueue();
    if (currentIndex == null && added > 0) {
      playIndex(firstNew);
    } else {
      notifyListeners();
    }
    return added;
  }

  /// Suona subito una voce: dov'è, se è già in coda, altrimenti dopo quella
  /// corrente.
  void playNow(Track entry) {
    var index = tracks.indexWhere(entry.sameEntry);
    if (index < 0) {
      index = currentIndex == null ? tracks.length : currentIndex! + 1;
      tracks.insert(index, entry);
      _saveQueue();
    }
    playIndex(index);
  }

  void playIndex(int index) {
    if (index < 0 || index >= tracks.length) return;
    currentIndex = index;
    final track = tracks[index];
    // Un brano nuovo parte con tutti i canali.
    mutedChannels = const {};
    soloChannels = const {};
    _silenced = const [];
    try {
      core.play(track.ref);
      final subsong = track.subsong;
      if (subsong != null && subsong < core.status().subsongCount) core.selectSubsong(subsong);
      details = core.details();
      _silenced = List.filled(channels.length, false);
      error = null;
    } on CoreException catch (e) {
      details = null;
      error = '${track.fileName}: $e';
    }
    status = core.status();
    _saveCurrent();
    notifyListeners();
  }

  void togglePause() {
    if (status.state == PlaybackState.idle || status.state == PlaybackState.ended) {
      if (tracks.isNotEmpty) playIndex(currentIndex ?? 0);
      return;
    }
    core.setPaused(status.isPlaying);
    refresh();
  }

  void next() {
    if (currentIndex != null) playIndex(currentIndex! + 1);
  }

  void previous() {
    final index = currentIndex;
    if (index == null) return;
    // Come nei lettori classici: oltre i primi secondi si riparte dall'inizio.
    if (status.position > 3 || index == 0) {
      seekTo(0);
    } else {
      playIndex(index - 1);
    }
  }

  void seekTo(double seconds) {
    core.seek(seconds < 0 ? 0 : seconds);
    refresh();
  }

  void seekBy(double delta) => seekTo(status.position + delta);

  void selectSubsong(int index) {
    if (index < 0 || index >= status.subsongCount) return;
    core.selectSubsong(index);
    refresh();
  }

  // --- I canali ---

  /// I canali del brano corrente che si possono zittire, con il loro nome.
  List<String> get channels => details?.channels ?? const [];

  /// Se il canale si sente, con mute e solo.
  bool channelAudible(int channel) =>
      soloChannels.isNotEmpty ? soloChannels.contains(channel) : !mutedChannels.contains(channel);

  void toggleChannelMute(int channel) {
    if (channel < 0 || channel >= channels.length) return;
    mutedChannels = _toggle(mutedChannels, channel);
    _applyChannels();
  }

  void toggleChannelSolo(int channel) {
    if (channel < 0 || channel >= channels.length) return;
    soloChannels = _toggle(soloChannels, channel);
    _applyChannels();
  }

  /// Tutti i canali di nuovo, senza mute né solo.
  void resetChannels() {
    mutedChannels = const {};
    soloChannels = const {};
    _applyChannels();
  }

  void _applyChannels() {
    for (var i = 0; i < _silenced.length; i++) {
      final silenced = !channelAudible(i);
      if (silenced != _silenced[i]) core.setChannelMuted(i, silenced);
    }
    _silenced = [for (var i = 0; i < _silenced.length; i++) !channelAudible(i)];
    notifyListeners();
  }

  static Set<int> _toggle(Set<int> values, int value) =>
      values.contains(value) ? ({...values}..remove(value)) : {...values, value};

  // --- La coda ---

  /// Toglie una voce. Se è quella che suona, la riproduzione si ferma.
  void removeTrack(int index) {
    if (index < 0 || index >= tracks.length) return;
    tracks.removeAt(index);
    final current = currentIndex;
    if (current == index) {
      currentIndex = null;
      core.stop();
      status = CoreStatus.idle;
      details = null;
    } else if (current != null && current > index) {
      currentIndex = current - 1;
    }
    _saveQueue();
    notifyListeners();
  }

  /// Sposta una voce: [to] è la sua posizione nella coda dopo lo spostamento.
  void moveTrack(int from, int to) {
    if (!_move(tracks, from, to)) return;
    final current = currentIndex;
    if (current == from) {
      currentIndex = to;
    } else if (current != null && from < current && to >= current) {
      currentIndex = current - 1;
    } else if (current != null && from > current && to <= current) {
      currentIndex = current + 1;
    }
    _saveQueue();
    notifyListeners();
  }

  void clearQueue() {
    tracks.clear();
    currentIndex = null;
    core.stop();
    status = CoreStatus.idle;
    details = null;
    _saveQueue();
    notifyListeners();
  }

  /// I sottobrani di una voce, con la durata dove si sa; null se quanti sono
  /// si scopre solo suonandola.
  List<double?>? subsongsOf(Track entry) {
    final item = library.lookup(entry.ref);
    if (item != null && item.subsongs > 0) {
      return [for (var i = 0; i < item.subsongs; i++) i < item.durations.length ? item.durations[i] : null];
    }
    if (currentTrack?.ref == entry.ref && status.subsongCount > 0) return List.filled(status.subsongCount, null);
    return null;
  }

  /// Sceglie il sottobrano di una voce (null per il file intero). false se
  /// la stessa voce c'è già.
  bool setTrackSubsong(int index, int? subsong) {
    if (!_setSubsong(tracks, index, subsong)) return false;
    _saveQueue();
    notifyListeners();
    return true;
  }

  // --- Le playlist ---

  void refreshPlaylists() {
    playlists = _withStore((store) => store.playlists()) ?? playlists;
    _playlistEntries.clear();
    notifyListeners();
  }

  /// Crea una playlist, con dentro [entries] se ci sono. null se non c'è
  /// dove salvarla.
  int? createPlaylist(String name, {List<Track> entries = const []}) {
    final id = _withStore((store) {
      final id = store.createPlaylist(name);
      if (entries.isNotEmpty) store.setEntries(id, _addEntries([], entries));
      return id;
    });
    refreshPlaylists();
    return id;
  }

  void renamePlaylist(int id, String name) {
    _withStore((store) => store.renamePlaylist(id, name));
    refreshPlaylists();
  }

  void deletePlaylist(int id) {
    _withStore((store) => store.deletePlaylist(id));
    refreshPlaylists();
  }

  /// Le voci di una playlist, lette una volta e poi tenute finché la si
  /// cambia: le skin le chiedono a ogni disegno.
  List<Track> playlistTracks(int id) =>
      _playlistEntries[id] ??= List.unmodifiable(_withStore((store) => store.entries(id)) ?? const <Track>[]);

  /// Aggiunge in fondo, senza le voci che ci sono già: quante ne entrano.
  int addToPlaylist(int id, List<Track> entries) {
    final added = _editPlaylist(id, (list) {
      final before = list.length;
      _addEntries(list, entries);
      return list.length - before;
    });
    return added ?? 0;
  }

  void removeFromPlaylist(int id, int index) {
    _editPlaylist(id, (list) {
      if (index >= 0 && index < list.length) list.removeAt(index);
    });
  }

  void movePlaylistTrack(int id, int from, int to) {
    _editPlaylist(id, (list) => _move(list, from, to));
  }

  /// Come [setTrackSubsong], in una playlist.
  bool setPlaylistSubsong(int id, int index, int? subsong) =>
      _editPlaylist(id, (list) => _setSubsong(list, index, subsong)) ?? false;

  /// Salva la coda così com'è in una nuova playlist.
  int? saveQueueAsPlaylist(String name) => createPlaylist(name, entries: tracks);

  /// La finestra del sistema per scegliere un file M3U o M3U8, che diventa
  /// una playlist; null se si annulla o non si legge (allora c'è [error]).
  Future<PlaylistImport?> importPlaylist() async {
    final paths = await _files.invokeListMethod<String>('openFiles', {
      'message': 'Scegli una playlist M3U',
      'single': true,
      'extensions': ['m3u', 'm3u8'],
    });
    if (paths == null || paths.isEmpty) return null;
    return addPaths(paths).playlists.firstOrNull;
  }

  /// La finestra del sistema per scegliere dove salvare, poi la playlist
  /// [id] (la coda, se null) in un file M3U8 che altri lettori aprono. Il
  /// file scritto, o null.
  Future<String?> exportPlaylist(int? id, {required String name}) async {
    if (store == null) return null;
    final fileName = name.replaceAll(RegExp(r'[/\\:*?"<>|]'), '-').trim();
    final path = await _files.invokeMethod<String>('saveFile', {
      'name': '${fileName.isEmpty ? 'playlist' : fileName}.m3u8',
      'message': 'Esporta in M3U',
      'extensions': ['m3u8'],
    });
    if (path == null) return null;
    final written = _withStore((store) {
      store.exportPlaylist(id ?? store.queueId(), path);
      return path;
    });
    notifyListeners();
    return written;
  }

  /// Mette la playlist al posto della coda e fa partire la voce [start].
  void loadPlaylist(int id, {int start = 0}) {
    final entries = _withStore((store) => store.entries(id));
    if (entries == null) return;
    core.stop();
    tracks
      ..clear()
      ..addAll(entries);
    currentIndex = null;
    details = null;
    status = CoreStatus.idle;
    _saveQueue();
    if (tracks.isNotEmpty) {
      playIndex(start.clamp(0, tracks.length - 1));
    } else {
      notifyListeners();
    }
  }

  // --- Impostazioni ---

  void setVolume(double value) {
    volume = value.clamp(0.0, 1.0);
    core.setVolume(volume);
    _withStore((store) => store.setSetting(_volumeKey, '$volume'));
    notifyListeners();
  }

  void setMegaSound(double value) {
    megaSound = value.clamp(0.0, 1.0);
    // Chi gira la manopola vuole sentire l'effetto, non l'originale.
    megaSoundBypassed = false;
    _applyMegaSound();
    _withStore((store) => store.setSetting(_megaSoundKey, '$megaSound'));
    notifyListeners();
  }

  void toggleMegaSoundCompare() {
    megaSoundBypassed = !megaSoundBypassed;
    _applyMegaSound();
    notifyListeners();
  }

  void _applyMegaSound() => core.setMegaSound(megaSoundBypassed ? 0 : megaSound);

  void setSkin(Skin value) {
    skin = value;
    _withStore((store) => store.setSetting(_skinKey, value.name));
    notifyListeners();
  }

  // --- I moduli Roland ---

  /// Se il core sa suonare i MIDI sui moduli Roland (88emu).
  bool get rolandSupported => rolandDevices.isNotEmpty;

  /// I Sound Canvas con le ROM, tra cui scegliere quello dei MIDI GS e GM.
  List<RolandDevice> get soundCanvases => [
        for (final device in rolandDevices)
          if (device.available && !device.la) device,
      ];

  /// La finestra del sistema per scegliere la cartella delle ROM.
  Future<void> chooseRomFolder() async {
    final path = await _files.invokeMethod<String>(
      'openFolder',
      {'start': romFolder, 'message': 'Scegli la cartella con le ROM dei moduli Roland'},
    );
    if (path != null) setRomFolder(path);
  }

  /// Le ROM non si copiano: il core le cerca lì, sottocartelle comprese.
  void setRomFolder(String path) {
    romFolder = path;
    core.addRolandRomDirectory(path);
    rolandDevices = core.rolandDevices();
    _withStore((store) => store.setSetting(_romFolderKey, path));
    notifyListeners();
  }

  /// Vale dal prossimo MIDI che parte.
  void setRolandDevice(String name) {
    rolandDevice = name;
    core.setRolandDevice(name);
    _withStore((store) => store.setSetting(_rolandKey, name));
    notifyListeners();
  }

  // --- Il SoundFont ---

  /// Se il core sa suonare i MIDI su un SoundFont (FluidSynth).
  bool get soundFontSupported => backends.any((backend) => backend.name == 'fluidsynth');

  /// Il SoundFont con cui suonano adesso i MIDI senza ROM; '' per l'FM.
  String get activeSoundFont => switch (soundFontChoice) {
        SoundFontChoice.included => includedSoundFont,
        SoundFontChoice.custom => customSoundFont.isEmpty ? includedSoundFont : customSoundFont,
        SoundFontChoice.fm => '',
      };

  /// Vale dal prossimo MIDI che parte.
  void setSoundFontChoice(SoundFontChoice choice) {
    soundFontChoice = choice;
    _applySoundFont();
    _withStore((store) => store.setSetting(_soundFontChoiceKey, choice.name));
    notifyListeners();
  }

  /// La finestra del sistema per scegliere un SoundFont (.sf2), che diventa
  /// quello dei MIDI senza ROM.
  Future<void> chooseSoundFont() async {
    final paths = await _files.invokeListMethod<String>('openFiles', {
      'message': 'Scegli un SoundFont per i MIDI',
      'single': true,
      'extensions': ['sf2'],
    });
    if (paths == null || paths.isEmpty) return;
    customSoundFont = paths.first;
    _withStore((store) => store.setSetting(_customSoundFontKey, customSoundFont));
    setSoundFontChoice(SoundFontChoice.custom);
  }

  void _applySoundFont() => core.setSoundFont(activeSoundFont);

  // --- Salvataggio ---

  /// Gli errori del database diventano un messaggio, mai un crash.
  T? _withStore<T>(T Function(LibraryStore store) body) {
    final store = this.store;
    if (store == null) return null;
    try {
      return body(store);
    } on CoreException catch (e) {
      error = 'libreria: $e';
      return null;
    }
  }

  void _saveQueue() {
    _withStore((store) => store.setEntries(_queueId ??= store.queueId(), tracks));
    _saveCurrent();
  }

  void _saveCurrent() => _withStore((store) => store.setSetting(_currentKey, '${currentIndex ?? -1}'));

  /// Modifica le voci di una playlist e le salva; il risultato è quello di
  /// [edit], null se non c'è dove salvare.
  R? _editPlaylist<R>(int id, R Function(List<Track> entries) edit) {
    final result = _withStore((store) {
      final entries = store.entries(id);
      final result = edit(entries);
      store.setEntries(id, entries);
      return result;
    });
    refreshPlaylists();
    return result;
  }

  /// Aggiunge a [list] le voci che non ci sono già; restituisce [list].
  static List<Track> _addEntries(List<Track> list, Iterable<Track> entries) {
    // Con una cartella intera sono migliaia: niente confronti uno a uno.
    String key(Track track) => '${track.subsong}\t${track.ref}';
    final present = {for (final track in list) key(track)};
    for (final entry in entries) {
      if (present.add(key(entry))) list.add(entry);
    }
    return list;
  }

  static bool _move(List<Track> list, int from, int to) {
    if (from < 0 || from >= list.length || to < 0 || to >= list.length || from == to) return false;
    list.insert(to, list.removeAt(from));
    return true;
  }

  static bool _setSubsong(List<Track> list, int index, int? subsong) {
    if (index < 0 || index >= list.length) return false;
    final changed = list[index].withSubsong(subsong);
    for (var i = 0; i < list.length; i++) {
      if (i != index && list[i].sameEntry(changed)) return false;
    }
    list[index] = changed;
    return true;
  }

  @override
  void dispose() {
    _timer?.cancel();
    updates?.removeListener(notifyListeners);
    library.dispose();
    core.dispose();
    store?.dispose();
    super.dispose();
  }
}

/// "1:12", oppure "--:--" se il tempo non è noto.
String formatTime(double? seconds, {bool padMinutes = false}) {
  if (seconds == null) return '--:--';
  final total = seconds.floor();
  final minutes = (total ~/ 60).toString();
  final rest = (total % 60).toString().padLeft(2, '0');
  return '${padMinutes ? minutes.padLeft(2, '0') : minutes}:$rest';
}

/// Quello che è entrato con [PlayerController.addPaths]: le voci nuove in
/// coda su quelle chieste, e le playlist create dai file M3U.
class AddedFiles {
  const AddedFiles({this.added = 0, this.requested = 0, this.playlists = const []});

  final int added;
  final int requested;
  final List<PlaylistImport> playlists;
}
