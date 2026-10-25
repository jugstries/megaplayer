part of 'retro_skin.dart';

// Gli aggiornamenti nella skin retrò: una voce nel menu («Aggiornamenti…»,
// «Aggiorna!» quando ce n'è uno), la mascotte che lo dice, e l'avviso del
// Mac con le novità, che si apre da solo quando l'app lo trova.

/// Apre l'avviso degli aggiornamenti; con [check] cerca prima sul sito.
void _showUpdates(BuildContext context, Updates updates, {bool check = false}) {
  if (check) updates.check(manual: true);
  showDialog<void>(context: context, builder: (context) => _UpdateDialog(updates: updates));
}

/// Annuncia la versione nuova trovata all'avvio, una volta sola.
void _announceUpdate(BuildContext context, Updates? updates) {
  if (updates == null || !updates.shouldAnnounce) return;
  updates.markAnnounced();
  WidgetsBinding.instance.addPostFrameCallback((_) {
    if (context.mounted) _showUpdates(context, updates);
  });
}

/// Quello che dice la mascotte finché c'è una versione da installare.
String? _updateHint(Updates? updates) {
  final offer = updates?.offer;
  if (offer == null) return null;
  return updates!.stage == UpdateStage.downloading
      ? updateMessage(updates)
      : 'C\'è megaplayer ${offer.version}! Lo installi da «Aggiorna!» nel menu.';
}

class _UpdateDialog extends StatelessWidget {
  const _UpdateDialog({required this.updates});

  final Updates updates;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: updates,
      builder: (context, _) {
        final offer = updates.offer;
        final dismiss = updateDismiss(updates);
        final action = updateAction(updates);
        final notes = offer != null && offer.notes.isNotEmpty && updates.stage != UpdateStage.upToDate;
        return _DialogFrame(
          title: updateTitle(updates),
          actions: [
            if (dismiss != null)
              _DialogButton(label: dismiss, primary: action == null, onPressed: () => Navigator.of(context).pop()),
            if (action != null) _DialogButton(label: action, primary: true, onPressed: () => runUpdateAction(updates)),
          ],
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Gli avvisi del Mac hanno l'icona a sinistra: qui la mascotte.
              CustomPaint(size: const Size.square(48), painter: _MascotPainter()),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Text(updateMessage(updates), style: _pixel(16, height: 1.4)),
                    if (updates.busy) ...[
                      const SizedBox(height: 12),
                      _UpdateProgress(value: updateProgress(updates)),
                    ],
                    if (notes) ...[
                      const SizedBox(height: 14),
                      Text('NOVITÀ', style: _silk(12)),
                      const SizedBox(height: 6),
                      Flexible(
                        child: Container(
                          constraints: const BoxConstraints(maxHeight: 220),
                          padding: const EdgeInsets.all(10),
                          decoration: const BoxDecoration(
                            color: _paper,
                            border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                          ),
                          child: SingleChildScrollView(child: Text(offer.notes, style: _pixel(15, height: 1.4))),
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// La barra di avanzamento del Mac: una casella che si riempie di nero, o
/// a righe quando non si sa quanto manca.
class _UpdateProgress extends StatelessWidget {
  const _UpdateProgress({required this.value});

  final double? value;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 18,
      padding: const EdgeInsets.all(2),
      decoration: const BoxDecoration(
        color: _paper,
        border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
      ),
      child: value == null
          ? CustomPaint(painter: _StripesPainter(), size: Size.infinite)
          : Align(
              alignment: Alignment.centerLeft,
              child: FractionallySizedBox(
                widthFactor: value!.clamp(0.0, 1.0),
                heightFactor: 1,
                child: const ColoredBox(color: _ink),
              ),
            ),
    );
  }
}
