part of 'modern_skin.dart';

// L'esportazione nella skin moderna: il brano che suona in WAV o MP3, com'è
// adesso, con la MegaSound e i canali zittiti o in solo se si vuole.

void _showExport(BuildContext context, PlayerController controller) {
  showDialog<void>(
    context: context,
    barrierDismissible: false,
    builder: (_) => _ExportDialog(controller: controller),
  );
}

class _ExportDialog extends StatefulWidget {
  const _ExportDialog({required this.controller});

  final PlayerController controller;

  @override
  State<_ExportDialog> createState() => _ExportDialogState();
}

class _ExportDialogState extends State<_ExportDialog> {
  late bool _mp3 = widget.controller.core.canExportMp3;
  bool _withMegaSound = true;
  bool _withChannels = true;

  void _close() {
    widget.controller.dismissExport();
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        final job = controller.exportJob;
        final status = job?.status;
        return _DialogFrame(
          title: 'Esporta il brano',
          width: 460,
          actions: [
            if (job == null) ...[
              _DialogButton(label: 'Annulla', onPressed: _close),
              _DialogButton(
                label: 'Esporta…',
                primary: true,
                onPressed: () => controller.chooseExportFile(
                  mp3: _mp3,
                  withMegaSound: _withMegaSound,
                  withChannels: _withChannels,
                ),
              ),
            ] else if (job.running)
              _DialogButton(label: 'Annulla', onPressed: controller.cancelExport)
            else
              _DialogButton(label: 'Fatto', primary: true, onPressed: _close),
          ],
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(controller.exportName(), style: _sora(15, weight: 600), overflow: TextOverflow.ellipsis),
              const SizedBox(height: 14),
              if (job == null) ...[
                Text('FORMATO', style: _label()),
                const SizedBox(height: 4),
                _RadioOption(label: 'WAV, senza perdita', selected: !_mp3, onTap: () => setState(() => _mp3 = false)),
                if (controller.core.canExportMp3)
                  _RadioOption(
                    label: 'MP3, alta qualità (VBR)',
                    selected: _mp3,
                    onTap: () => setState(() => _mp3 = true),
                  ),
                if (controller.megaSoundAudible || controller.channelsChanged) ...[
                  const SizedBox(height: 12),
                  Text('COME LO SENTI', style: _label()),
                  const SizedBox(height: 4),
                  if (controller.megaSoundAudible)
                    _CheckOption(
                      label: 'Con la MegaSound al ${(controller.megaSound * 100).round()}%',
                      checked: _withMegaSound,
                      onTap: () => setState(() => _withMegaSound = !_withMegaSound),
                    ),
                  if (controller.channelsChanged)
                    _CheckOption(
                      label: 'Con i canali zittiti e in solo',
                      checked: _withChannels,
                      onTap: () => setState(() => _withChannels = !_withChannels),
                    ),
                ],
              ] else if (job.running) ...[
                Text(exportProgress(status!.seconds, status.total), style: _sora(13, color: _textSoft)),
                const SizedBox(height: 10),
                _UpdateProgress(
                  value: status.total == null ? null : (status.seconds / status.total!).clamp(0.0, 1.0),
                ),
              ] else
                Text(
                  exportOutcome(done: job.done, cancelled: status!.cancelled, error: status.error, path: job.path),
                  style: _sora(13, color: job.done ? _textSoft : const Color(0xFFFFC2D2), height: 1.5),
                ),
            ],
          ),
        );
      },
    );
  }
}

/// Una casella da spuntare.
class _CheckOption extends StatelessWidget {
  const _CheckOption({required this.label, required this.checked, required this.onTap});

  final String label;
  final bool checked;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      checked: checked,
      button: true,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 6),
          child: Row(
            children: [
              Container(
                width: 18,
                height: 18,
                decoration: BoxDecoration(
                  color: checked ? _orange : Colors.transparent,
                  borderRadius: BorderRadius.circular(5),
                  border: Border.all(color: checked ? _orange : _border, width: 2),
                ),
                child: checked ? const Icon(Icons.check_rounded, size: 13, color: _background) : null,
              ),
              const SizedBox(width: 12),
              Expanded(child: Text(label, style: _sora(14, color: checked ? _text : _textSoft))),
            ],
          ),
        ),
      ),
    );
  }
}
