part of 'retro_skin.dart';

// L'esportazione nella skin retrò, come un avviso del Mac: il brano che
// suona in WAV o MP3, com'è adesso, con la MegaSound e i canali zittiti o in
// solo se si vuole.

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
          width: 480,
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
              Text(controller.exportName(), style: _pixel(17, weight: 700), overflow: TextOverflow.ellipsis),
              const SizedBox(height: 12),
              if (job == null) ...[
                Text('Formato:', style: _pixel(16, weight: 600)),
                _RadioRow(label: 'WAV, senza perdita', selected: !_mp3, onTap: () => setState(() => _mp3 = false)),
                if (controller.core.canExportMp3)
                  _RadioRow(label: 'MP3, alta qualità (VBR)', selected: _mp3, onTap: () => setState(() => _mp3 = true)),
                if (controller.megaSoundAudible)
                  _CheckRow(
                    label: 'Con la MegaSound al ${(controller.megaSound * 100).round()}%',
                    checked: _withMegaSound,
                    onTap: () => setState(() => _withMegaSound = !_withMegaSound),
                  ),
                if (controller.channelsChanged)
                  _CheckRow(
                    label: 'Con i canali zittiti e in solo',
                    checked: _withChannels,
                    onTap: () => setState(() => _withChannels = !_withChannels),
                  ),
              ] else if (job.running) ...[
                Text(exportProgress(status!.seconds, status.total), style: _pixel(16)),
                const SizedBox(height: 10),
                _UpdateProgress(
                  value: status.total == null ? null : (status.seconds / status.total!).clamp(0.0, 1.0),
                ),
              ] else
                Text(
                  exportOutcome(done: job.done, cancelled: status!.cancelled, error: status.error, path: job.path),
                  style: _pixel(16, height: 1.4),
                ),
            ],
          ),
        );
      },
    );
  }
}

/// Una casella del Mac: il quadrato, con la croce se è spuntata.
class _CheckRow extends StatelessWidget {
  const _CheckRow({required this.label, required this.checked, required this.onTap});

  final String label;
  final bool checked;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      checked: checked,
      button: true,
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: onTap,
          behavior: HitTestBehavior.opaque,
          child: Padding(
            padding: const EdgeInsets.symmetric(vertical: 4),
            child: Row(
              children: [
                Container(
                  width: 16,
                  height: 16,
                  decoration: const BoxDecoration(
                    color: _paper,
                    border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                  ),
                  child: checked ? CustomPaint(painter: _CrossPainter()) : null,
                ),
                const SizedBox(width: 10),
                Flexible(child: Text(label, style: _pixel(16), overflow: TextOverflow.ellipsis)),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _CrossPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = _ink
      ..strokeWidth = 2
      ..isAntiAlias = false;
    canvas.drawLine(Offset.zero, Offset(size.width, size.height), paint);
    canvas.drawLine(Offset(size.width, 0), Offset(0, size.height), paint);
  }

  @override
  bool shouldRepaint(_CrossPainter old) => false;
}
