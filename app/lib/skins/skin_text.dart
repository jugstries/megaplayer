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

// --- Date e dimensioni (anche per tool/download_page.dart) ---

/// I megabyte all'italiana: "24,3 MB".
String formatMegabytes(int bytes) => '${(bytes / (1024 * 1024)).toStringAsFixed(1).replaceAll('.', ',')} MB';

const _months = [
  'gennaio', 'febbraio', 'marzo', 'aprile', 'maggio', 'giugno', //
  'luglio', 'agosto', 'settembre', 'ottobre', 'novembre', 'dicembre',
];

/// "25 ottobre 2026" da "2026-10-25"; com'è se non si legge.
String formatDate(String date) {
  final parsed = DateTime.tryParse(date);
  return parsed == null ? date : '${parsed.day} ${_months[parsed.month - 1]} ${parsed.year}';
}

// --- Moduli Roland ---

/// Cosa sono le impostazioni dei moduli Roland, in cima alla loro sezione.
const rolandIntro = 'megaplayer suona i MIDI sui Sound Canvas e sull\'MT-32 emulati da 88emu di The Usual '
    'Suspects. Le ROM non sono incluse: indica la cartella dove tieni le tue, anche divise in sottocartelle.';

/// Il nome di un modulo come lo mostrano le skin.
String rolandName(String name) => name.replaceAll('(experimental)', '(sperimentale)');

/// Quali moduli hanno le ROM, o con che cosa suonano i MIDI senza: il
/// SoundFont [soundFont], o i chip FM se è ''.
String rolandSummary(List<RolandDevice> devices, {String soundFont = ''}) {
  final ready = [
    for (final device in devices)
      if (device.available) rolandName(device.name),
  ];
  if (ready.isNotEmpty) return 'Moduli pronti: ${ready.join(', ')}.';
  return soundFont.isEmpty
      ? 'Nessuna ROM trovata: i MIDI suonano sui chip FM (libADLMIDI).'
      : 'Nessuna ROM trovata: i MIDI suonano con il SoundFont ${soundFontName(soundFont)}.';
}

/// Cosa sono le impostazioni dei MIDI senza le ROM.
const soundFontIntro = 'Senza le ROM dei moduli Roland, i MIDI suonano su un SoundFont a campioni, General MIDI e '
    'GS, oppure sui chip FM, come su una Sound Blaster.';

/// Il nome di un SoundFont dal suo file: "GeneralUser GS".
String soundFontName(String path) {
  final name = baseName(path);
  final dot = name.lastIndexOf('.');
  return (dot > 0 ? name.substring(0, dot) : name).replaceAll('-', ' ').replaceAll('_', ' ');
}

/// Una scelta del Sound Canvas per i MIDI GS e GM; '' è «Automatico», con
/// quello che il core sceglierebbe.
String rolandChoice(String name, List<RolandDevice> soundCanvases) {
  if (name.isNotEmpty) return rolandName(name);
  return soundCanvases.isEmpty ? 'Automatico' : 'Automatico (${rolandName(soundCanvases.first.name)})';
}

// --- Esportazione ---

/// "1:23", per l'avanzamento dell'esportazione.
String formatClock(double seconds) {
  final total = seconds.floor();
  return '${total ~/ 60}:${(total % 60).toString().padLeft(2, '0')}';
}

/// "Esporto… 1:23 di 3:04".
String exportProgress(double seconds, double? total) =>
    'Esporto… ${formatClock(seconds)}${total == null ? '' : ' di ${formatClock(total)}'}';

/// Cosa dice la finestra a esportazione finita, riuscita o no.
String exportOutcome({required bool done, required bool cancelled, required String error, required String path}) {
  if (done) return 'Salvato in $path';
  if (cancelled) return 'Esportazione annullata.';
  return 'Non è andata: $error.';
}
