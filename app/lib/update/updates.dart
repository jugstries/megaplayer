import 'package:flutter/foundation.dart';

import 'update_manifest.dart';
import 'updater.dart';

enum UpdateStage { idle, checking, upToDate, available, downloading, installing, failed }

/// Gli aggiornamenti per le skin: all'avvio si cerca una versione nuova e,
/// se c'è, la si annuncia con le novità; si installa solo quando l'utente
/// lo chiede, poi l'app si chiude e l'installer la riapre.
class Updates extends ChangeNotifier {
  Updates(this._updater, {required this.quit, this.currentVersion = appVersion});

  final Updater _updater;

  /// Chiude l'app per lasciare il posto all'installer.
  final Future<void> Function() quit;

  /// La versione che gira, da mostrare.
  final String currentVersion;

  UpdateStage stage = UpdateStage.idle;

  /// La versione nuova trovata sul sito.
  UpdateManifest? offer;

  /// Quanto è stato scaricato, da 0 a 1.
  double progress = 0;

  /// Perché l'ultima operazione non è riuscita.
  String? problem;

  bool _announced = false;

  bool get busy =>
      stage == UpdateStage.checking || stage == UpdateStage.downloading || stage == UpdateStage.installing;

  /// Trovata da sola all'avvio e non ancora mostrata: la skin apre la
  /// finestra con le novità e poi chiama [markAnnounced].
  bool get shouldAnnounce => offer != null && !_announced && stage == UpdateStage.available;

  void markAnnounced() => _announced = true;

  /// Toglie gli installer scaricati le volte prima e cerca una versione
  /// nuova.
  Future<void> start() async {
    await _updater.cleanUp();
    await check();
  }

  /// Cerca una versione nuova. Con [manual] l'utente vuole sapere anche che
  /// non ce n'è, o che non si è potuto controllare; senza, gli errori
  /// restano zitti (magari non c'è rete).
  Future<void> check({bool manual = false}) async {
    if (busy) return;
    stage = UpdateStage.checking;
    problem = null;
    notifyListeners();
    try {
      offer = await _updater.check();
      stage = offer != null ? UpdateStage.available : (manual ? UpdateStage.upToDate : UpdateStage.idle);
      // Cercata a mano, la finestra la apre chi ha chiesto.
      if (manual) _announced = true;
    } on UpdateException catch (e) {
      stage = manual ? UpdateStage.failed : UpdateStage.idle;
      problem = manual ? e.message : null;
    }
    notifyListeners();
  }

  /// Scarica la versione nuova, lancia l'installer e chiude l'app.
  Future<void> install() async {
    final manifest = offer;
    if (manifest == null || busy) return;
    stage = UpdateStage.downloading;
    progress = 0;
    problem = null;
    notifyListeners();
    try {
      final installer = await _updater.download(manifest, onProgress: (value) {
        // Un aggiornamento per punto percentuale basta.
        if ((value * 100).floor() == (progress * 100).floor()) return;
        progress = value;
        notifyListeners();
      });
      stage = UpdateStage.installing;
      notifyListeners();
      await _updater.launch(installer);
      await quit();
    } on UpdateException catch (e) {
      stage = UpdateStage.failed;
      problem = e.message;
      notifyListeners();
    }
  }
}
