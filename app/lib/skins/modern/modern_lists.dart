part of 'modern_skin.dart';

// Coda e playlist della skin moderna, con i menu e le finestre di dialogo
// che servono anche alla libreria.

/// Tema dei widget Material (menu, dialoghi, campi di testo, avvisi) nei
/// colori della skin.
final _theme = ThemeData(
  brightness: Brightness.dark,
  colorScheme: const ColorScheme.dark(primary: _orange, secondary: _pink, surface: _surface, onSurface: _text),
  menuTheme: MenuThemeData(
    style: MenuStyle(
      backgroundColor: const WidgetStatePropertyAll(_surfaceRaised),
      surfaceTintColor: const WidgetStatePropertyAll(Colors.transparent),
      side: const WidgetStatePropertyAll(BorderSide(color: _border)),
      shape: WidgetStatePropertyAll(RoundedRectangleBorder(borderRadius: BorderRadius.circular(14))),
      padding: const WidgetStatePropertyAll(EdgeInsets.symmetric(vertical: 6)),
    ),
  ),
  menuButtonTheme: MenuButtonThemeData(
    style: ButtonStyle(
      foregroundColor: const WidgetStatePropertyAll(_textSoft),
      iconColor: const WidgetStatePropertyAll(_muted),
      textStyle: WidgetStatePropertyAll(_sora(13, weight: 500)),
      minimumSize: const WidgetStatePropertyAll(Size(220, 40)),
      padding: const WidgetStatePropertyAll(EdgeInsets.symmetric(horizontal: 14)),
      overlayColor: const WidgetStatePropertyAll(_selected),
    ),
  ),
  dialogTheme: DialogThemeData(
    backgroundColor: _surface,
    surfaceTintColor: Colors.transparent,
    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(24), side: const BorderSide(color: _border)),
  ),
  snackBarTheme: SnackBarThemeData(
    backgroundColor: _surfaceRaised,
    contentTextStyle: _sora(13, color: _text),
    behavior: SnackBarBehavior.floating,
    width: 420,
    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14), side: const BorderSide(color: _border)),
  ),
  tooltipTheme: TooltipThemeData(
    decoration: BoxDecoration(
      color: _surfaceRaised,
      borderRadius: BorderRadius.circular(8),
      border: Border.all(color: _border),
    ),
    textStyle: _sora(12, color: _text),
  ),
  textSelectionTheme: TextSelectionThemeData(cursorColor: _orange, selectionColor: _violet.withValues(alpha: 0.45)),
  scrollbarTheme: const ScrollbarThemeData(thumbColor: WidgetStatePropertyAll(_track)),
);

/// "1 brano", "3.701 brani".
String _tracksLabel(int count) => '${_number(count)} ${count == 1 ? 'brano' : 'brani'}';

/// Le migliaia col punto, come si scrivono in italiano.
String _number(int value) {
  final digits = value.abs().toString();
  final grouped = StringBuffer();
  for (var i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 == 0) grouped.write('.');
    grouped.write(digits[i]);
  }
  return value < 0 ? '-$grouped' : grouped.toString();
}

void _notify(BuildContext context, String message) {
  ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(message), duration: const Duration(seconds: 2)));
}

/// Mette in coda e lo dice.
void _enqueue(BuildContext context, PlayerController controller, List<Track> entries) {
  final added = controller.enqueue(entries);
  _notify(
    context,
    added == 0
        ? (entries.length == 1 ? 'È già in coda' : 'Sono già tutti in coda')
        : added == 1
            ? 'Aggiunto alla coda'
            : 'Aggiunti alla coda ${_tracksLabel(added)}',
  );
}

/// Aggiunge a una playlist e lo dice.
void _addToPlaylist(BuildContext context, PlayerController controller, PlaylistInfo playlist, List<Track> entries) {
  final added = controller.addToPlaylist(playlist.id, entries);
  _notify(
    context,
    added == 0
        ? (entries.length == 1 ? 'È già in «${playlist.name}»' : 'Sono già tutti in «${playlist.name}»')
        : added == 1
            ? 'Aggiunto a «${playlist.name}»'
            : 'Aggiunti a «${playlist.name}» ${_tracksLabel(added)}',
  );
}

/// Crea una playlist con [entries] dentro, chiedendo il nome.
Future<void> _newPlaylistWith(BuildContext context, PlayerController controller, List<Track> entries) async {
  final name = await _askName(context, title: 'Nuova playlist', action: 'Crea');
  if (name == null || !context.mounted) return;
  if (controller.createPlaylist(name, entries: entries) != null) {
    _notify(
      context,
      entries.length == 1 ? 'Aggiunto a «$name»' : 'Playlist «$name» con ${_tracksLabel(entries.length)}',
    );
  }
}

/// Le voci di menu per aggiungere a una playlist: una per playlist, e una
/// per crearne una nuova. [entries] si legge quando si sceglie.
List<Widget> _playlistMenuItems(BuildContext context, PlayerController controller, List<Track> Function() entries) => [
      for (final playlist in controller.playlists)
        MenuItemButton(
          leadingIcon: const Icon(Icons.queue_music_rounded, size: 18),
          onPressed: () => _addToPlaylist(context, controller, playlist, entries()),
          child: Text(playlist.name, overflow: TextOverflow.ellipsis),
        ),
      MenuItemButton(
        leadingIcon: const Icon(Icons.add_rounded, size: 18),
        onPressed: () => _newPlaylistWith(context, controller, entries()),
        child: const Text('Nuova playlist…'),
      ),
    ];

/// Un'icona che apre un menu.
class _MenuIconButton extends StatelessWidget {
  const _MenuIconButton({required this.icon, required this.tooltip, required this.children, this.size = 36});

  final IconData icon;
  final String tooltip;
  final List<Widget> children;
  final double size;

  @override
  Widget build(BuildContext context) {
    return MenuAnchor(
      menuChildren: children,
      builder: (context, menu, _) => _SmallIconButton(
        icon: icon,
        tooltip: tooltip,
        size: size,
        onPressed: () => menu.isOpen ? menu.close() : menu.open(),
      ),
    );
  }
}

class _SmallIconButton extends StatelessWidget {
  const _SmallIconButton({required this.icon, required this.tooltip, required this.onPressed, this.size = 36});

  final IconData icon;
  final String tooltip;
  final VoidCallback? onPressed;
  final double size;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        onTap: onPressed,
        borderRadius: BorderRadius.circular(10),
        child: SizedBox(
          width: size,
          height: size,
          child: Icon(icon, size: 20, color: onPressed == null ? _track : _muted),
        ),
      ),
    );
  }
}

/// Bottone piccolo con icona e testo, per le intestazioni dei riquadri.
class _SmallButton extends StatelessWidget {
  const _SmallButton({required this.icon, required this.label, required this.onPressed, this.accent = false});

  final IconData icon;
  final String label;
  final VoidCallback? onPressed;
  final bool accent;

  @override
  Widget build(BuildContext context) {
    final color = accent ? _background : _textSoft;
    return Material(
      color: accent ? null : _surfaceRaised,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: accent ? BorderSide.none : const BorderSide(color: _border),
      ),
      child: Ink(
        decoration: accent ? BoxDecoration(gradient: _accentGradient, borderRadius: BorderRadius.circular(12)) : null,
        child: InkWell(
          onTap: onPressed,
          borderRadius: BorderRadius.circular(12),
          child: Container(
            height: 36,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(icon, size: 17, color: color),
                const SizedBox(width: 8),
                Text(label, style: _sora(13, weight: accent ? 600 : 500, color: color)),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

// --- Finestre di dialogo ---

class _DialogFrame extends StatelessWidget {
  const _DialogFrame({required this.title, required this.child, required this.actions});

  final String title;
  final Widget child;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) {
    return Dialog(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 420),
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(title, style: _sora(18, weight: 600)),
              const SizedBox(height: 16),
              Flexible(child: child),
              const SizedBox(height: 20),
              Row(mainAxisAlignment: MainAxisAlignment.end, children: actions),
            ],
          ),
        ),
      ),
    );
  }
}

class _DialogButton extends StatelessWidget {
  const _DialogButton({required this.label, required this.onPressed, this.primary = false, this.danger = false});

  final String label;
  final VoidCallback onPressed;
  final bool primary;
  final bool danger;

  @override
  Widget build(BuildContext context) {
    final background = danger ? const Color(0xFF5A2A3A) : (primary ? _border : Colors.transparent);
    return Padding(
      padding: const EdgeInsets.only(left: 8),
      child: Material(
        color: background,
        borderRadius: BorderRadius.circular(12),
        child: InkWell(
          onTap: onPressed,
          borderRadius: BorderRadius.circular(12),
          child: Container(
            height: 40,
            padding: const EdgeInsets.symmetric(horizontal: 16),
            alignment: Alignment.center,
            child: Text(
              label,
              style: _sora(14, weight: primary || danger ? 600 : 500, color: danger ? const Color(0xFFFFC2D2) : _text),
            ),
          ),
        ),
      ),
    );
  }
}

InputDecoration _fieldDecoration({String? hint, Widget? prefix, Widget? suffix}) => InputDecoration(
      hintText: hint,
      hintStyle: _sora(14, color: _faint),
      prefixIcon: prefix,
      suffixIcon: suffix,
      filled: true,
      fillColor: _inset,
      isDense: true,
      contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(14),
        borderSide: const BorderSide(color: _insetBorder),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(14),
        borderSide: const BorderSide(color: _violet, width: 1.5),
      ),
    );

/// Chiede un nome (di una playlist); null se si annulla o resta vuoto.
Future<String?> _askName(BuildContext context, {required String title, required String action, String initial = ''}) =>
    showDialog<String>(
      context: context,
      builder: (context) => _NameDialog(title: title, action: action, initial: initial),
    );

/// I campi di testo stanno in finestre che sono widget con stato: il
/// controller deve vivere finché la finestra non è sparita del tutto.
class _NameDialog extends StatefulWidget {
  const _NameDialog({required this.title, required this.action, required this.initial});

  final String title;
  final String action;
  final String initial;

  @override
  State<_NameDialog> createState() => _NameDialogState();
}

class _NameDialogState extends State<_NameDialog> {
  late final TextEditingController _text = TextEditingController(text: widget.initial)
    ..selection = TextSelection(baseOffset: 0, extentOffset: widget.initial.length);

  @override
  void dispose() {
    _text.dispose();
    super.dispose();
  }

  void _submit() {
    final name = _text.text.trim();
    Navigator.of(context).pop(name.isEmpty ? null : name);
  }

  @override
  Widget build(BuildContext context) {
    return _DialogFrame(
      title: widget.title,
      actions: [
        _DialogButton(label: 'Annulla', onPressed: () => Navigator.of(context).pop()),
        _DialogButton(label: widget.action, primary: true, onPressed: _submit),
      ],
      child: TextField(
        controller: _text,
        autofocus: true,
        style: _sora(15),
        decoration: _fieldDecoration(hint: 'Nome della playlist'),
        onSubmitted: (_) => _submit(),
      ),
    );
  }
}

Future<bool> _confirm(BuildContext context, {required String title, required String message, required String action}) async {
  final confirmed = await showDialog<bool>(
    context: context,
    builder: (context) => _DialogFrame(
      title: title,
      actions: [
        _DialogButton(label: 'Annulla', onPressed: () => Navigator.of(context).pop(false)),
        _DialogButton(label: action, danger: true, onPressed: () => Navigator.of(context).pop(true)),
      ],
      child: Text(message, style: _sora(14, color: _textSoft, height: 1.5)),
    ),
  );
  return confirmed ?? false;
}

/// Il risultato della scelta del sottobrano: [subsong] null è il file intero.
typedef _SubsongChoice = ({int? subsong});

/// Sceglie il sottobrano di una voce, o il file intero. null se si annulla.
Future<_SubsongChoice?> _pickSubsong(BuildContext context, PlayerController controller, Track entry) =>
    showDialog<_SubsongChoice>(
      context: context,
      builder: (context) => _SubsongDialog(entry: entry, subsongs: controller.subsongsOf(entry)),
    );

class _SubsongDialog extends StatefulWidget {
  const _SubsongDialog({required this.entry, required this.subsongs});

  final Track entry;

  /// Le durate dei sottobrani; null se quanti sono non si sa.
  final List<double?>? subsongs;

  @override
  State<_SubsongDialog> createState() => _SubsongDialogState();
}

class _SubsongDialogState extends State<_SubsongDialog> {
  late final TextEditingController _number =
      TextEditingController(text: widget.entry.subsong == null ? '' : '${widget.entry.subsong! + 1}');

  @override
  void dispose() {
    _number.dispose();
    super.dispose();
  }

  void _submitNumber() {
    final value = int.tryParse(_number.text.trim());
    if (value != null && value >= 1) Navigator.of(context).pop((subsong: value - 1));
  }

  Widget _option(String label, String? detail, int? subsong) {
    final selected = widget.entry.subsong == subsong;
    return InkWell(
      onTap: () => Navigator.of(context).pop((subsong: subsong)),
      borderRadius: BorderRadius.circular(12),
      child: Container(
        height: 44,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        decoration: BoxDecoration(
          color: selected ? _selected : Colors.transparent,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          children: [
            Icon(
              selected ? Icons.radio_button_checked_rounded : Icons.radio_button_off_rounded,
              size: 18,
              color: selected ? _orange : _faint,
            ),
            const SizedBox(width: 12),
            Expanded(child: Text(label, style: _sora(14, weight: selected ? 600 : 400))),
            if (detail != null) Text(detail, style: _mono(12)),
          ],
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final subsongs = widget.subsongs;
    return _DialogFrame(
      title: widget.entry.fileName,
      actions: [
        _DialogButton(label: 'Annulla', onPressed: () => Navigator.of(context).pop()),
        if (subsongs == null) _DialogButton(label: 'Scegli', primary: true, onPressed: _submitNumber),
      ],
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _option('File intero', 'tutti in fila', null),
          const SizedBox(height: 4),
          if (subsongs != null)
            Flexible(
              child: ListView(
                shrinkWrap: true,
                children: [
                  for (var i = 0; i < subsongs.length; i++)
                    _option('Sottobrano ${i + 1}', subsongs[i] == null ? null : formatTime(subsongs[i]), i),
                ],
              ),
            )
          else ...[
            const SizedBox(height: 8),
            Text(
              'Quanti sottobrani ha questo file si sa solo suonandolo. Scrivi il numero:',
              style: _sora(13, color: _muted, height: 1.4),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: _number,
              autofocus: true,
              keyboardType: TextInputType.number,
              style: _sora(15),
              decoration: _fieldDecoration(hint: 'Sottobrano'),
              onSubmitted: (_) => _submitNumber(),
            ),
          ],
        ],
      ),
    );
  }
}

/// Sceglie il sottobrano e lo applica con [apply], che dice false se la
/// stessa voce c'è già.
Future<void> _changeSubsong(
  BuildContext context,
  PlayerController controller,
  Track entry,
  bool Function(int? subsong) apply,
) async {
  final choice = await _pickSubsong(context, controller, entry);
  if (choice == null || choice.subsong == entry.subsong || !context.mounted) return;
  if (!apply(choice.subsong)) _notify(context, 'Questa voce c\'è già');
}

// --- Righe di coda e playlist ---

/// Una voce di coda o playlist: si trascina dal numero, si suona col clic,
/// il menu ha il resto.
class _EntryRow extends StatelessWidget {
  const _EntryRow({
    required this.index,
    required this.entry,
    required this.current,
    required this.onTap,
    required this.menu,
  });

  final int index;
  final Track entry;
  final bool current;
  final VoidCallback onTap;
  final List<Widget> menu;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Material(
        color: current ? _selected : Colors.transparent,
        borderRadius: BorderRadius.circular(14),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(14),
          child: Container(
            constraints: const BoxConstraints(minHeight: 56),
            padding: const EdgeInsets.fromLTRB(4, 6, 4, 6),
            child: Row(
              children: [
                ReorderableDragStartListener(
                  index: index,
                  child: MouseRegion(
                    cursor: SystemMouseCursors.grab,
                    child: Tooltip(
                      message: 'Trascina per spostare',
                      waitDuration: const Duration(milliseconds: 800),
                      child: Container(
                        width: 36,
                        height: 40,
                        alignment: Alignment.center,
                        child: current
                            ? const _PlayingBars()
                            : Text((index + 1).toString().padLeft(2, '0'), style: _mono(12, color: _faint)),
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 4),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(entry.fileName, style: _sora(14, weight: current ? 600 : 500), overflow: TextOverflow.ellipsis),
                      if (entry.location.isNotEmpty)
                        Text(
                          entry.location,
                          style: _sora(12, color: current ? _textSoft : _muted),
                          overflow: TextOverflow.ellipsis,
                        ),
                    ],
                  ),
                ),
                _MenuIconButton(icon: Icons.more_horiz_rounded, tooltip: 'Altro', children: menu),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Le voci in un elenco riordinabile, che scorre da sé se [scrollable].
Widget _reorderableEntries({
  required List<Track> entries,
  required bool scrollable,
  required void Function(int from, int to) onMove,
  required Widget Function(int index) row,
}) {
  return ReorderableListView.builder(
    buildDefaultDragHandles: false,
    shrinkWrap: !scrollable,
    physics: scrollable ? null : const NeverScrollableScrollPhysics(),
    itemCount: entries.length,
    onReorderItem: onMove,
    proxyDecorator: (child, index, animation) => Material(
      color: _surfaceRaised,
      elevation: 8,
      shadowColor: Colors.black,
      borderRadius: BorderRadius.circular(14),
      child: child,
    ),
    itemBuilder: (context, index) => KeyedSubtree(
      key: ValueKey('${entries[index].ref}#${entries[index].subsong}'),
      child: row(index),
    ),
  );
}

class _QueuePanel extends StatelessWidget {
  const _QueuePanel({required this.controller, required this.scrollable});

  final PlayerController controller;
  final bool scrollable;

  @override
  Widget build(BuildContext context) {
    final tracks = controller.tracks;
    final canSave = controller.store != null && tracks.isNotEmpty;

    Future<void> saveAsPlaylist() async {
      final name = await _askName(context, title: 'Salva la coda come playlist', action: 'Salva');
      if (name == null || !context.mounted) return;
      if (controller.saveQueueAsPlaylist(name) != null) _notify(context, 'Playlist «$name» salvata');
    }

    final list = _reorderableEntries(
      entries: tracks,
      scrollable: scrollable,
      onMove: controller.moveTrack,
      row: (i) => _EntryRow(
        index: i,
        entry: tracks[i],
        current: i == controller.currentIndex,
        onTap: () => controller.playIndex(i),
        menu: [
          MenuItemButton(
            leadingIcon: const Icon(Icons.play_arrow_rounded, size: 18),
            onPressed: () => controller.playIndex(i),
            child: const Text('Riproduci'),
          ),
          MenuItemButton(
            leadingIcon: const Icon(Icons.layers_rounded, size: 18),
            onPressed: () => _changeSubsong(context, controller, tracks[i], (s) => controller.setTrackSubsong(i, s)),
            child: const Text('Sottobrano…'),
          ),
          if (controller.store != null)
            SubmenuButton(
              leadingIcon: const Icon(Icons.playlist_add_rounded, size: 18),
              menuChildren: _playlistMenuItems(context, controller, () => [tracks[i]]),
              child: const Text('Aggiungi a playlist'),
            ),
          MenuItemButton(
            leadingIcon: const Icon(Icons.remove_circle_outline_rounded, size: 18),
            onPressed: () => controller.removeTrack(i),
            child: const Text('Togli dalla coda'),
          ),
        ],
      ),
    );

    return _Panel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.only(left: 4),
            child: Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('Coda', style: _sora(18, weight: 600)),
                      const SizedBox(height: 4),
                      Text(tracks.isEmpty ? 'Vuota' : _tracksLabel(tracks.length), style: _sora(13, color: _muted)),
                    ],
                  ),
                ),
                _MenuIconButton(
                  icon: Icons.more_horiz_rounded,
                  tooltip: 'Coda',
                  children: [
                    MenuItemButton(
                      leadingIcon: const Icon(Icons.save_alt_rounded, size: 18),
                      onPressed: canSave ? saveAsPlaylist : null,
                      child: const Text('Salva come playlist…'),
                    ),
                    MenuItemButton(
                      leadingIcon: const Icon(Icons.clear_all_rounded, size: 18),
                      onPressed: tracks.isEmpty ? null : controller.clearQueue,
                      child: const Text('Svuota la coda'),
                    ),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 12),
          if (scrollable) Expanded(child: list) else list,
        ],
      ),
    );
  }
}

/// Una playlist: le sue voci, da riordinare, e i comandi per suonarla.
class _PlaylistPanel extends StatelessWidget {
  const _PlaylistPanel({super.key, required this.controller, required this.playlist});

  final PlayerController controller;
  final PlaylistInfo playlist;

  @override
  Widget build(BuildContext context) {
    final id = playlist.id;
    final entries = controller.playlistTracks(id);

    Future<void> rename() async {
      final name = await _askName(context, title: 'Rinomina la playlist', action: 'Rinomina', initial: playlist.name);
      if (name != null) controller.renamePlaylist(id, name);
    }

    Future<void> delete() async {
      final confirmed = await _confirm(
        context,
        title: 'Eliminare «${playlist.name}»?',
        message: 'La playlist sparisce; i brani restano dove sono.',
        action: 'Elimina',
      );
      if (confirmed) controller.deletePlaylist(id);
    }

    return _Panel(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Container(
                width: 56,
                height: 56,
                decoration: BoxDecoration(gradient: _accentGradient, borderRadius: BorderRadius.circular(16)),
                child: const Icon(Icons.queue_music_rounded, color: _background, size: 30),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('PLAYLIST', style: _label()),
                    const SizedBox(height: 4),
                    Text(playlist.name, style: _sora(26, weight: 700, spacing: -0.6), overflow: TextOverflow.ellipsis),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              _SmallButton(
                icon: Icons.play_arrow_rounded,
                label: 'Riproduci',
                accent: true,
                onPressed: entries.isEmpty ? null : () => controller.loadPlaylist(id),
              ),
              const SizedBox(width: 8),
              _SmallButton(
                icon: Icons.add_rounded,
                label: 'In coda',
                onPressed: entries.isEmpty ? null : () => _enqueue(context, controller, entries),
              ),
              const SizedBox(width: 4),
              _MenuIconButton(
                icon: Icons.more_horiz_rounded,
                tooltip: 'Playlist',
                children: [
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.edit_rounded, size: 18),
                    onPressed: rename,
                    child: const Text('Rinomina…'),
                  ),
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.delete_outline_rounded, size: 18),
                    onPressed: delete,
                    child: const Text('Elimina playlist…'),
                  ),
                ],
              ),
            ],
          ),
          const SizedBox(height: 8),
          Padding(
            padding: const EdgeInsets.only(left: 72),
            child: Text(
              entries.isEmpty
                  ? 'Vuota: aggiungi brani dalla libreria, dalla coda o da quello che suona'
                  : '${_tracksLabel(entries.length)} · «Riproduci» la mette al posto della coda',
              style: _sora(13, color: _muted),
            ),
          ),
          const SizedBox(height: 16),
          Expanded(
            child: _reorderableEntries(
              entries: entries,
              scrollable: true,
              onMove: (from, to) => controller.movePlaylistTrack(id, from, to),
              row: (i) => _EntryRow(
                index: i,
                entry: entries[i],
                current: controller.currentTrack?.sameEntry(entries[i]) ?? false,
                onTap: () => controller.playNow(entries[i]),
                menu: [
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.play_arrow_rounded, size: 18),
                    onPressed: () => controller.playNow(entries[i]),
                    child: const Text('Riproduci subito'),
                  ),
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.playlist_play_rounded, size: 18),
                    onPressed: () => controller.loadPlaylist(id, start: i),
                    child: const Text('Riproduci la playlist da qui'),
                  ),
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.add_rounded, size: 18),
                    onPressed: () => _enqueue(context, controller, [entries[i]]),
                    child: const Text('Aggiungi alla coda'),
                  ),
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.layers_rounded, size: 18),
                    onPressed: () =>
                        _changeSubsong(context, controller, entries[i], (s) => controller.setPlaylistSubsong(id, i, s)),
                    child: const Text('Sottobrano…'),
                  ),
                  MenuItemButton(
                    leadingIcon: const Icon(Icons.remove_circle_outline_rounded, size: 18),
                    onPressed: () => controller.removeFromPlaylist(id, i),
                    child: const Text('Togli dalla playlist'),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
