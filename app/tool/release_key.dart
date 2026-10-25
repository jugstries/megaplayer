import 'dart:convert';
import 'dart:io';

import 'package:cryptography/cryptography.dart';

// La chiave privata delle release nel Portachiavi di macOS: 32 byte in
// base64, servizio "megaplayer-update-key". La usano tool/update_key.dart e
// tool/sign_update.dart; non lascia mai il Mac.

const _service = 'megaplayer-update-key';
const _account = 'megaplayer';

/// La chiave privata, o null se nel Portachiavi non c'è.
Future<List<int>?> readReleaseSeed() async {
  final result = await Process.run('security', ['find-generic-password', '-s', _service, '-a', _account, '-w']);
  if (result.exitCode != 0) return null;
  return base64.decode((result.stdout as String).trim());
}

/// Mette [seed] nel Portachiavi. Il comando passa da stdin, così la chiave
/// non compare tra gli argomenti dei processi.
Future<void> storeReleaseSeed(List<int> seed) async {
  final security = await Process.start('security', ['-i']);
  security.stdin.writeln('add-generic-password -s $_service -a $_account '
      '-l "megaplayer: chiave di firma delle release" -w "${base64.encode(seed)}"');
  await security.stdin.close();
  final code = await security.exitCode;
  if (code != 0) throw StateError('il Portachiavi non ha accettato la chiave (security: $code)');
}

/// La chiave pubblica di [seed], in base64.
Future<String> publicKeyOf(List<int> seed) async {
  final keyPair = await Ed25519().newKeyPairFromSeed(seed);
  return base64.encode((await keyPair.extractPublicKey()).bytes);
}
