// Testi e piccole regole che le due skin condividono, perché dicano le
// stesse cose con le stesse parole.

import '../core/models.dart';

/// Le migliaia col punto, come si scrivono in italiano: "3.701".
String formatCount(int value) {
  final digits = value.abs().toString();
  final grouped = StringBuffer();
  for (var i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 == 0) grouped.write('.');
    grouped.write(digits[i]);
  }
  return value < 0 ? '-$grouped' : grouped.toString();
}

/// "1 brano", "3.701 brani".
String tracksLabel(int count) => '${formatCount(count)} ${count == 1 ? 'brano' : 'brani'}';

/// Dopo aver messo in coda [requested] voci, di cui [added] nuove.
String enqueuedMessage(int added, int requested) {
  if (added == 0) return requested == 1 ? 'È già in coda' : 'Sono già tutti in coda';
  return added == 1 ? 'Aggiunto alla coda' : 'Aggiunti alla coda ${tracksLabel(added)}';
}

/// Dopo aver aggiunto [requested] voci alla playlist [name], di cui [added]
/// nuove.
String addedToPlaylistMessage(String name, int added, int requested) {
  if (added == 0) return requested == 1 ? 'È già in «$name»' : 'Sono già tutti in «$name»';
  return added == 1 ? 'Aggiunto a «$name»' : 'Aggiunti a «$name» ${tracksLabel(added)}';
}

/// Dopo aver creato la playlist [name] con dentro [count] voci.
String createdPlaylistMessage(String name, int count) => switch (count) {
      0 => 'Playlist «$name» creata',
      1 => 'Aggiunto a «$name»',
      _ => 'Playlist «$name» con ${tracksLabel(count)}',
    };

const _archiveExtensions = {'lha', 'lzh', 'lzx', 'zip', '7z', 'rar', 'tar', 'gz', 'tgz', 'bz2', 'xz', 'iso'};

/// Una cartella della libreria che in realtà è un archivio.
bool looksLikeArchive(String name) {
  final dot = name.lastIndexOf('.');
  return dot > 0 && _archiveExtensions.contains(name.substring(dot + 1).toLowerCase());
}

/// L'ultimo pezzo di un percorso.
String baseName(String path) {
  final parts = path.split(pathSeparators).where((part) => part.isNotEmpty);
  return parts.isEmpty ? path : parts.last;
}
