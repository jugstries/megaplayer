// Crea la chiave Ed25519 delle release e la mette nel Portachiavi di macOS;
// se c'è già la lascia com'è. Stampa la chiave pubblica, che va in
// lib/update/updater.dart: l'app installa solo quello che è firmato con la
// privata.
//
// Uso (in app/): dart run tool/update_key.dart
//
// Della chiave privata conviene tenere una copia (in un gestore di
// password): senza, le copie già installate non accettano più
// aggiornamenti e vanno reinstallate a mano.

import 'dart:io';
import 'dart:math';

import 'release_key.dart';

Future<void> main() async {
  var seed = await readReleaseSeed();
  if (seed == null) {
    final random = Random.secure();
    seed = List<int>.generate(32, (_) => random.nextInt(256));
    await storeReleaseSeed(seed);
    // Riletta, per essere sicuri che sia davvero lì.
    seed = await readReleaseSeed();
    if (seed == null) throw StateError('la chiave non si rilegge dal Portachiavi');
    stdout.writeln('Chiave nuova nel Portachiavi (megaplayer-update-key).');
  } else {
    stdout.writeln('La chiave c\'è già nel Portachiavi (megaplayer-update-key).');
  }
  stdout.writeln('Chiave pubblica: ${await publicKeyOf(seed)}');
}
