// I testi degli aggiornamenti, uguali nelle due skin.

import '../update/updates.dart';
import 'skin_text.dart';

/// Il titolo della finestra degli aggiornamenti.
String updateTitle(Updates updates) {
  final offer = updates.offer;
  return switch (updates.stage) {
    UpdateStage.checking => 'Cerco aggiornamenti…',
    UpdateStage.upToDate => 'Nessun aggiornamento',
    _ when offer != null => 'È arrivato megaplayer ${offer.version}',
    _ => 'Aggiornamenti',
  };
}

/// Cosa sta succedendo, sotto il titolo.
String updateMessage(Updates updates) {
  final offer = updates.offer;
  return switch (updates.stage) {
    UpdateStage.checking => 'Un attimo: guardo sul sito se c\'è una versione nuova.',
    UpdateStage.upToDate => 'Hai già l\'ultima versione, la ${updates.currentVersion}.',
    UpdateStage.downloading => 'Scarico l\'aggiornamento… ${(updates.progress * 100).floor()}%',
    UpdateStage.installing => 'Parte l\'installer: megaplayer si chiude e si riapre da solo.',
    UpdateStage.failed => 'Non è andata: ${updates.problem}.',
    _ when offer != null => 'Hai la ${updates.currentVersion}. La nuova è del ${formatDate(offer.date)} '
        'e pesa ${formatMegabytes(offer.size)}.',
    _ => '',
  };
}

/// Il bottone che resta in vista finché c'è una versione nuova.
String updateBadge(Updates updates) => updates.stage == UpdateStage.downloading
    ? 'Scarico… ${(updates.progress * 100).floor()}%'
    : 'Aggiorna a ${updates.offer?.version}';

/// Il bottone principale della finestra, o null se non c'è niente da fare.
String? updateAction(Updates updates) => switch (updates.stage) {
      UpdateStage.available => 'Installa e riavvia',
      UpdateStage.failed => 'Riprova',
      _ => null,
    };

/// Quello che fa il bottone principale.
void runUpdateAction(Updates updates) {
  if (updates.offer != null) {
    updates.install();
  } else {
    updates.check(manual: true);
  }
}

/// Il bottone per chiudere la finestra (lo scaricamento va avanti), o null
/// mentre parte l'installer.
String? updateDismiss(Updates updates) => switch (updates.stage) {
      UpdateStage.available => 'Più tardi',
      UpdateStage.downloading => 'Nascondi',
      UpdateStage.upToDate => 'OK',
      UpdateStage.installing => null,
      _ => 'Chiudi',
    };

/// Quanto è piena la barra di avanzamento mentre si lavora: quello che è
/// stato scaricato, o null (barra in movimento) se non si sa.
double? updateProgress(Updates updates) => updates.stage == UpdateStage.downloading ? updates.progress : null;
