import 'dart:convert';

import 'package:cryptography/cryptography.dart';

// Il manifesto di una versione pubblicata sul sito: updates/windows.json
// accanto all'installer. Lo scrive tool/sign_update.dart, lo legge l'app.
// Non usa Flutter, così lo strumento di firma lo condivide.

/// Una versione pubblicata, con la firma Ed25519 di chi l'ha pubblicata.
class UpdateManifest {
  const UpdateManifest({
    required this.platform,
    required this.version,
    required this.build,
    required this.date,
    required this.notes,
    required this.file,
    required this.size,
    required this.sha256,
    this.signature = '',
  });

  /// Lancia [FormatException] se manca qualcosa.
  factory UpdateManifest.fromJson(Map<String, dynamic> json) {
    T field<T>(String name) {
      final value = json[name];
      if (value is! T) throw FormatException('manifesto senza "$name"');
      return value;
    }

    return UpdateManifest(
      platform: field<String>('platform'),
      version: field<String>('version'),
      build: field<int>('build'),
      date: field<String>('date'),
      notes: field<String>('notes'),
      file: field<String>('file'),
      size: field<int>('size'),
      sha256: field<String>('sha256'),
      signature: field<String>('signature'),
    );
  }

  /// "windows" o "macos".
  final String platform;

  /// La versione da mostrare ("0.2.0").
  final String version;

  /// Il numero di build: cresce a ogni versione, ed è quello che si confronta.
  final int build;

  /// AAAA-MM-GG.
  final String date;

  /// Le novità, in italiano, da mostrare prima di installare.
  final String notes;

  /// L'installer, accanto al manifesto.
  final String file;
  final int size;

  /// L'SHA-256 dell'installer, in esadecimale.
  final String sha256;

  /// La firma Ed25519 di [signedBytes], in base64.
  final String signature;

  Map<String, dynamic> toJson() => {
        'platform': platform,
        'version': version,
        'build': build,
        'date': date,
        'notes': notes,
        'file': file,
        'size': size,
        'sha256': sha256,
        'signature': signature,
      };

  /// Quello che la firma copre: tutti i campi, uno per riga, con le novità
  /// in fondo perché possono andare a capo.
  List<int> get signedBytes =>
      utf8.encode(['megaplayer-update-1', platform, '$build', version, date, file, '$size', sha256, notes].join('\n'));

  /// Lo stesso manifesto firmato con la chiave privata [seed] (32 byte).
  Future<UpdateManifest> signedWith(List<int> seed) async {
    final algorithm = Ed25519();
    final keyPair = await algorithm.newKeyPairFromSeed(seed);
    final signed = await algorithm.sign(signedBytes, keyPair: keyPair);
    return UpdateManifest(
      platform: platform,
      version: version,
      build: build,
      date: date,
      notes: notes,
      file: file,
      size: size,
      sha256: sha256,
      signature: base64.encode(signed.bytes),
    );
  }

  /// Se la firma è di chi ha la chiave privata di [publicKey] (base64).
  Future<bool> isSignedBy(String publicKey) async {
    try {
      final key = SimplePublicKey(base64.decode(publicKey), type: KeyPairType.ed25519);
      return await Ed25519().verify(signedBytes, signature: Signature(base64.decode(signature), publicKey: key));
    } on FormatException {
      return false;
    }
  }
}

/// L'SHA-256 di dati che arrivano a pezzi, in esadecimale.
class Sha256Digest {
  final _sink = Sha256().newHashSink();

  void add(List<int> chunk) => _sink.add(chunk);

  Future<String> finish() async {
    _sink.close();
    final hash = await _sink.hash();
    return hash.bytes.map((byte) => byte.toRadixString(16).padLeft(2, '0')).join();
  }
}
