part of 'modern_skin.dart';

// Gli aggiornamenti nella skin moderna: la versione accanto al nome (un clic
// cerca sul sito), un bottone a gradiente finché ce n'è una nuova, e la
// finestra con le novità, che si apre da sola quando l'app la trova.

/// Apre la finestra degli aggiornamenti; con [check] cerca prima sul sito.
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

class _VersionLabel extends StatelessWidget {
  const _VersionLabel({required this.updates});

  final Updates updates;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: 'Cerca aggiornamenti',
      child: InkWell(
        onTap: () => _showUpdates(context, updates, check: updates.offer == null),
        borderRadius: BorderRadius.circular(8),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
          child: Text(updates.currentVersion, style: _mono(12, color: _faint)),
        ),
      ),
    );
  }
}

class _UpdateBadge extends StatelessWidget {
  const _UpdateBadge({required this.updates});

  final Updates updates;

  @override
  Widget build(BuildContext context) {
    return Material(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
      clipBehavior: Clip.antiAlias,
      child: Ink(
        decoration: const BoxDecoration(gradient: _accentGradient),
        child: InkWell(
          onTap: () => _showUpdates(context, updates),
          child: Container(
            height: 44,
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Icon(Icons.system_update_alt_rounded, size: 18, color: _background),
                const SizedBox(width: 10),
                Text(updateBadge(updates), style: _sora(14, weight: 600, color: _background)),
              ],
            ),
          ),
        ),
      ),
    );
  }
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
        final failed = updates.stage == UpdateStage.failed;
        final notes = offer != null && offer.notes.isNotEmpty && updates.stage != UpdateStage.upToDate;
        return _DialogFrame(
          title: updateTitle(updates),
          actions: [
            if (dismiss != null) _DialogButton(label: dismiss, onPressed: () => Navigator.of(context).pop()),
            if (action != null) _DialogButton(label: action, primary: true, onPressed: () => runUpdateAction(updates)),
          ],
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                updateMessage(updates),
                style: _sora(14, color: failed ? const Color(0xFFFFC2D2) : _textSoft, height: 1.5),
              ),
              if (updates.busy) ...[
                const SizedBox(height: 14),
                _UpdateProgress(value: updateProgress(updates)),
              ],
              if (notes) ...[
                const SizedBox(height: 18),
                Text('NOVITÀ', style: _label()),
                const SizedBox(height: 8),
                Flexible(
                  child: Container(
                    constraints: const BoxConstraints(maxHeight: 240),
                    padding: const EdgeInsets.all(14),
                    decoration: BoxDecoration(
                      color: _inset,
                      borderRadius: BorderRadius.circular(14),
                      border: Border.all(color: _insetBorder),
                    ),
                    child: SingleChildScrollView(
                      child: Text(offer.notes, style: _sora(13, color: _textSoft, height: 1.55)),
                    ),
                  ),
                ),
              ],
            ],
          ),
        );
      },
    );
  }
}

/// La barra di avanzamento a gradiente; senza [value] scorre.
class _UpdateProgress extends StatelessWidget {
  const _UpdateProgress({required this.value});

  final double? value;

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(3),
      child: SizedBox(
        height: 6,
        child: value == null
            ? const LinearProgressIndicator(backgroundColor: _track, color: _violet)
            : Stack(
                children: [
                  const Positioned.fill(child: ColoredBox(color: _track)),
                  FractionallySizedBox(
                    widthFactor: value!.clamp(0.0, 1.0),
                    heightFactor: 1,
                    child: const DecoratedBox(decoration: BoxDecoration(gradient: _accentGradient)),
                  ),
                ],
              ),
      ),
    );
  }
}
