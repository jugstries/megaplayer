import 'dart:typed_data';

import 'models.dart';

/// Il core di riproduzione visto dall'app. L'implementazione vera è
/// [FfiPlayerCore]; i test ne usano una finta.
abstract interface class PlayerCore {
  List<Backend> backends();

  /// Brani contenuti in un file o in un archivio. Lancia [CoreException].
  List<Track> collect(String path);

  /// Apre e avvia un brano. Lancia [CoreException].
  void play(String path);
  void stop();
  void setPaused(bool paused);
  void seek(double seconds);
  void selectSubsong(int index);
  void setVolume(double volume);

  /// La manopola MegaSound, da 0 (spenta) a 1 (tutti gli effetti).
  void setMegaSound(double amount);


  CoreStatus status();
  TrackDetails details();

  /// Gli ultimi [frames] frame stereo arrivati alle casse (L, R, L, R...).
  Float32List scope(int frames);

  void dispose();
}
