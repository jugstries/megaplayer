part of 'retro_skin.dart';

// Le impostazioni nella skin retrò, come un pannello di controllo del Mac:
// per ora i moduli Roland per i MIDI, con la cartella delle ROM dell'utente
// e il Sound Canvas da usare, scelto con i bottoni radio.

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
          width: 540,
          actions: [_DialogButton(label: 'Fatto', primary: true, onPressed: () => Navigator.of(context).pop())],
          child: SingleChildScrollView(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text('MIDI sui moduli Roland', style: _pixel(17, weight: 700)),
                const SizedBox(height: 6),
                Text(rolandIntro, style: _pixel(15, height: 1.4)),
                const SizedBox(height: 14),
                Row(
                  children: [
                    Expanded(
                      child: Container(
                        height: 32,
                        padding: const EdgeInsets.symmetric(horizontal: 8),
                        alignment: Alignment.centerLeft,
                        decoration: const BoxDecoration(
                          color: _paper,
                          border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                        ),
                        child: Text(
                          controller.romFolder.isEmpty ? 'Nessuna cartella scelta' : controller.romFolder,
                          style: _vt(20, color: controller.romFolder.isEmpty ? _midGray : _ink),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                    ),
                    const SizedBox(width: 12),
                    _DialogButton(label: 'Scegli…', onPressed: controller.chooseRomFolder),
                  ],
                ),
                const SizedBox(height: 10),
                Text(rolandSummary(controller.rolandDevices), style: _pixel(15, height: 1.35)),
                if (soundCanvases.isNotEmpty) ...[
                  const SizedBox(height: 16),
                  Text('Sound Canvas per i MIDI GS e GM:', style: _pixel(16, weight: 600)),
                  const SizedBox(height: 6),
                  for (final name in ['', for (final device in soundCanvases) device.name])
                    _RadioRow(
                      label: rolandChoice(name, soundCanvases),
                      selected: controller.rolandDevice == name,
                      onTap: () => controller.setRolandDevice(name),
                    ),
                  const SizedBox(height: 6),
                  Text(
                    'I brani per MT-32 vanno comunque all\'MT-32, se ci sono le sue ROM.',
                    style: _pixel(14, color: _midGray, height: 1.3),
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

/// Un bottone radio del Mac: il cerchio, pieno al centro se è la scelta.
class _RadioRow extends StatelessWidget {
  const _RadioRow({required this.label, required this.selected, required this.onTap});

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      selected: selected,
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
                  padding: const EdgeInsets.all(3),
                  decoration: BoxDecoration(
                    color: _paper,
                    shape: BoxShape.circle,
                    border: Border.all(color: _ink, width: 2),
                  ),
                  child: selected ? const DecoratedBox(decoration: BoxDecoration(color: _ink, shape: BoxShape.circle)) : null,
                ),
                const SizedBox(width: 10),
                Text(label, style: _pixel(16)),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
