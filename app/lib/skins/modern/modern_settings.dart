part of 'modern_skin.dart';

// Le impostazioni nella skin moderna: per ora i moduli Roland per i MIDI,
// con la cartella delle ROM dell'utente e il Sound Canvas da usare.

class _SettingsButton extends StatelessWidget {
  const _SettingsButton({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: 'Impostazioni',
      child: Material(
        color: _surface,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14), side: const BorderSide(color: _border)),
        child: InkWell(
          onTap: () => showDialog<void>(context: context, builder: (_) => _SettingsDialog(controller: controller)),
          borderRadius: BorderRadius.circular(14),
          child: const SizedBox(width: 44, height: 44, child: Icon(Icons.tune_rounded, size: 18, color: _textSoft)),
        ),
      ),
    );
  }
}

class _SettingsDialog extends StatelessWidget {
  const _SettingsDialog({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        final soundCanvases = controller.soundCanvases;
        return _DialogFrame(
          title: 'Impostazioni',
          width: 520,
          actions: [_DialogButton(label: 'Fatto', primary: true, onPressed: () => Navigator.of(context).pop())],
          child: SingleChildScrollView(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text('MIDI SUI MODULI ROLAND', style: _label()),
                const SizedBox(height: 8),
                Text(rolandIntro, style: _sora(13, color: _textSoft, height: 1.5)),
                const SizedBox(height: 16),
                Container(
                  padding: const EdgeInsets.fromLTRB(14, 8, 8, 8),
                  decoration: BoxDecoration(
                    color: _inset,
                    borderRadius: BorderRadius.circular(14),
                    border: Border.all(color: _insetBorder),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.folder_rounded, size: 18, color: _faint),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          controller.romFolder.isEmpty ? 'Nessuna cartella scelta' : controller.romFolder,
                          style: _mono(12, color: controller.romFolder.isEmpty ? _faint : _textSoft),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                      const SizedBox(width: 8),
                      _SmallButton(
                        icon: Icons.folder_open_rounded,
                        label: 'Scegli…',
                        onPressed: controller.chooseRomFolder,
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 10),
                Text(rolandSummary(controller.rolandDevices), style: _sora(13, color: _muted, height: 1.4)),
                if (soundCanvases.isNotEmpty) ...[
                  const SizedBox(height: 20),
                  Text('SOUND CANVAS PER I MIDI GS E GM', style: _label()),
                  const SizedBox(height: 4),
                  Text(
                    'I brani scritti per l\'MT-32 vanno comunque all\'MT-32, se ci sono le sue ROM.',
                    style: _sora(12, color: _faint, height: 1.4),
                  ),
                  const SizedBox(height: 10),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final name in ['', for (final device in soundCanvases) device.name])
                        _ChoicePill(
                          label: rolandChoice(name, soundCanvases),
                          selected: controller.rolandDevice == name,
                          onTap: () => controller.setRolandDevice(name),
                        ),
                    ],
                  ),
                ],
              ],
            ),
          ),
        );
      },
    );
  }
}

class _ChoicePill extends StatelessWidget {
  const _ChoicePill({required this.label, required this.selected, required this.onTap});

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      selected: selected,
      button: true,
      child: Material(
        color: selected ? _selected : Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(999),
          side: BorderSide(color: selected ? _orange : _border),
        ),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(999),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
            child: Text(label, style: _sora(13, weight: selected ? 600 : 400, color: selected ? _text : _textSoft)),
          ),
        ),
      ),
    );
  }
}
