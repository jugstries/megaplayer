part of 'modern_skin.dart';

// Le impostazioni nella skin moderna: i MIDI, sui moduli Roland con la
// cartella delle ROM dell'utente e il Sound Canvas da usare, e senza le ROM
// su un SoundFont o sui chip FM.

class _SettingsButton extends StatelessWidget {
  const _SettingsButton({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: 'Impostazioni',
      child: Material(
        color: _surface,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(14),
          side: const BorderSide(color: _border),
        ),
        child: InkWell(
          onTap: () => showDialog<void>(
            context: context,
            builder: (_) => _SettingsDialog(controller: controller),
          ),
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
                if (controller.rolandSupported) ..._roland(controller, soundCanvases),
                if (controller.rolandSupported && controller.soundFontSupported) const SizedBox(height: 24),
                if (controller.soundFontSupported) ..._soundFont(controller),
              ],
            ),
          ),
        );
      },
    );
  }

  List<Widget> _roland(PlayerController controller, List<RolandDevice> soundCanvases) => [
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
          _SmallButton(icon: Icons.folder_open_rounded, label: 'Scegli…', onPressed: controller.chooseRomFolder),
        ],
      ),
    ),
    const SizedBox(height: 10),
    Text(
      rolandSummary(controller.rolandDevices, soundFont: controller.activeSoundFont),
      style: _sora(13, color: _muted, height: 1.4),
    ),
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
  ];

  List<Widget> _soundFont(PlayerController controller) {
    final choice = controller.soundFontChoice;
    return [
      Text('MIDI SENZA LE ROM', style: _label()),
      const SizedBox(height: 8),
      Text(soundFontIntro, style: _sora(13, color: _textSoft, height: 1.5)),
      const SizedBox(height: 12),
      if (controller.includedSoundFont.isNotEmpty)
        _RadioOption(
          label: '${soundFontName(controller.includedSoundFont)}, incluso in megaplayer',
          selected: choice == SoundFontChoice.included,
          onTap: () => controller.setSoundFontChoice(SoundFontChoice.included),
        ),
      _RadioOption(
        label: controller.customSoundFont.isEmpty
            ? 'Un altro SoundFont (.sf2)'
            : 'Il SoundFont ${soundFontName(controller.customSoundFont)}',
        detail: controller.customSoundFont.isEmpty ? null : controller.customSoundFont,
        selected: choice == SoundFontChoice.custom && controller.customSoundFont.isNotEmpty,
        onTap: controller.customSoundFont.isEmpty
            ? controller.chooseSoundFont
            : () => controller.setSoundFontChoice(SoundFontChoice.custom),
        trailing: _SmallButton(
          icon: Icons.folder_open_rounded,
          label: 'Scegli…',
          onPressed: controller.chooseSoundFont,
        ),
      ),
      _RadioOption(
        label: 'I chip FM, come una Sound Blaster',
        selected: choice == SoundFontChoice.fm,
        onTap: () => controller.setSoundFontChoice(SoundFontChoice.fm),
      ),
    ];
  }
}

/// Una scelta tra più: il cerchio, pieno al centro se è quella in uso.
class _RadioOption extends StatelessWidget {
  const _RadioOption({required this.label, required this.selected, required this.onTap, this.detail, this.trailing});

  final String label;
  final String? detail;
  final bool selected;
  final VoidCallback onTap;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      selected: selected,
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
                padding: const EdgeInsets.all(4),
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  border: Border.all(color: selected ? _orange : _border, width: 2),
                ),
                child: selected
                    ? const DecoratedBox(
                        decoration: BoxDecoration(color: _orange, shape: BoxShape.circle),
                      )
                    : null,
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      label,
                      style: _sora(14, weight: selected ? 600 : 400, color: selected ? _text : _textSoft),
                    ),
                    if (detail != null)
                      Text(
                        detail!,
                        style: _mono(11, color: _faint),
                        overflow: TextOverflow.ellipsis,
                      ),
                  ],
                ),
              ),
              if (trailing != null) ...[const SizedBox(width: 8), trailing!],
            ],
          ),
        ),
      ),
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
            child: Text(
              label,
              style: _sora(13, weight: selected ? 600 : 400, color: selected ? _text : _textSoft),
            ),
          ),
        ),
      ),
    );
  }
}
