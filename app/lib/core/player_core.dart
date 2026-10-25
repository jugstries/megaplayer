import 'dart:typed_data';

import 'models.dart';

/// Il core di riproduzione visto dall'app. L'implementazione vera è
/// [FfiPlayerCore]; i test ne usano una finta.
abstract interface class PlayerCore {
  List<Backend> backends();

  /// Brani contenuti in un file o in un archivio. Lancia [CoreException].
  List<Track> collect(String path);

  /// Apre e avvia un brano, dal suo [Track.ref]. Lancia [CoreException].
  void play(String ref);
  void stop();
  void setPaused(bool paused);
  void seek(double seconds);
  void selectSubsong(int index);
  void setVolume(double volume);

  /// La manopola MegaSound, da 0 (spenta) a 1 (tutti gli effetti).
  void setMegaSound(double amount);

  /// Zittisce o riaccende un canale del brano ([TrackDetails.channels]).
  /// Vale fino al brano dopo.
  void setChannelMuted(int channel, bool muted);

  /// I moduli Roland di 88emu per i MIDI: una cartella in più in cui cercare
  /// le ROM dell'utente, il Sound Canvas per i MIDI GS e GM ('' per
  /// sceglierlo da soli) e i moduli che il core conosce, con le ROM trovate.
  void addRolandRomDirectory(String path);
  void setRolandDevice(String name);
  List<RolandDevice> rolandDevices();

  /// Il SoundFont per i MIDI senza le ROM ('' per l'FM di adlmidi), e quello
  /// in uso: all'avvio GeneralUser GS, che viaggia con l'app.
  void setSoundFont(String path);
  String soundFont();

  /// Se il core sa esportare in MP3, oltre che in WAV.
  bool get canExportMp3;

  /// Esporta in [path] (.wav o .mp3) un brano come lo suona: il sottobrano,
  /// i canali [muted] zittiti, la MegaSound a [megaSound]. Gira per conto
  /// suo e ferma la riproduzione (UADE suona un brano per volta). Lancia
  /// [CoreException].
  void startExport(
    String ref, {
    required int subsong,
    required String path,
    required double megaSound,
    required List<int> muted,
    String title = '',
    String artist = '',
    String album = '',
  });
  ExportStatus exportStatus();

  /// Si ferma e cancella il file.
  void cancelExport();

  CoreStatus status();
  TrackDetails details();

  /// Gli ultimi [frames] frame stereo arrivati alle casse (L, R, L, R...).
  Float32List scope(int frames);

  void dispose();
}
