part of 'retro_skin.dart';

// Coda e playlist della skin retrò, nella finestra con le linguette, e i
// controlli che servono anche alla libreria: menu a tendina, bottoni,
// finestre di dialogo.

/// Tema dei widget Material (menu, dialoghi, campi di testo, suggerimenti)
/// a 1 bit: carta bianca, bordi neri, la voce scelta in negativo.
final _theme = ThemeData(
  colorScheme: const ColorScheme.light(primary: _ink, onPrimary: _paper, surface: _paper, onSurface: _ink),
  menuTheme: const MenuThemeData(
    style: MenuStyle(
      backgroundColor: WidgetStatePropertyAll(_paper),
      surfaceTintColor: WidgetStatePropertyAll(Colors.transparent),
      shadowColor: WidgetStatePropertyAll(Colors.transparent),
      elevation: WidgetStatePropertyAll(0),
      side: WidgetStatePropertyAll(BorderSide(color: _ink, width: 2)),
      shape: WidgetStatePropertyAll(RoundedRectangleBorder()),
      padding: WidgetStatePropertyAll(EdgeInsets.symmetric(vertical: 4)),
    ),
  ),
  menuButtonTheme: MenuButtonThemeData(
    style: ButtonStyle(
      foregroundColor: WidgetStateProperty.resolveWith(_menuForeground),
      iconColor: WidgetStateProperty.resolveWith(_menuForeground),
      backgroundColor: WidgetStateProperty.resolveWith(
        (states) => _menuHighlighted(states) ? _ink : Colors.transparent,
      ),
      overlayColor: const WidgetStatePropertyAll(Colors.transparent),
      textStyle: WidgetStatePropertyAll(_pixel(16)),
      shape: const WidgetStatePropertyAll(RoundedRectangleBorder()),
      minimumSize: const WidgetStatePropertyAll(Size(220, 32)),
      padding: const WidgetStatePropertyAll(EdgeInsets.symmetric(horizontal: 14)),
      tapTargetSize: MaterialTapTargetSize.shrinkWrap,
    ),
  ),
  dialogTheme: const DialogThemeData(
    backgroundColor: Colors.transparent,
    surfaceTintColor: Colors.transparent,
    elevation: 0,
    shape: RoundedRectangleBorder(),
  ),
  tooltipTheme: TooltipThemeData(
    decoration: BoxDecoration(color: _paper, border: Border.all(color: _ink, width: 2)),
    textStyle: _pixel(14),
  ),
  textSelectionTheme: const TextSelectionThemeData(cursorColor: _ink, selectionColor: _lilac),
);

bool _menuHighlighted(Set<WidgetState> states) =>
    !states.contains(WidgetState.disabled) &&
    (states.contains(WidgetState.hovered) || states.contains(WidgetState.focused));

Color _menuForeground(Set<WidgetState> states) =>
    states.contains(WidgetState.disabled) ? _midGray : (_menuHighlighted(states) ? _paper : _ink);

/// Chi dà gli avvisi ("Aggiunto alla coda"): la mascotte nella finestra
/// «Ciao!».
class _Notices extends InheritedWidget {
  const _Notices({required this.say, required super.child});

  final void Function(String message) say;

  @override
  bool updateShouldNotify(_Notices old) => false;
}

void _say(BuildContext context, String message) =>
    context.getInheritedWidgetOfExactType<_Notices>()?.say(message);

/// «Apri…»: i brani vanno in coda, i file M3U diventano playlist, e lo
/// dice.
Future<void> _openFiles(BuildContext context, PlayerController controller) async {
  final result = await controller.openFiles();
  if (result.playlists.isNotEmpty && context.mounted) {
    _say(context, addedFilesMessage(result.added, result.requested, result.playlists));
  }
}

/// Scrive la playlist [id] (la coda, se null) in un M3U e lo dice.
Future<void> _exportPlaylist(BuildContext context, PlayerController controller, int? id, String name) async {
  final path = await controller.exportPlaylist(id, name: name);
  if (path != null && context.mounted) _say(context, exportedPlaylistMessage(path));
}

void _enqueue(BuildContext context, PlayerController controller, List<Track> entries) =>
    _say(context, enqueuedMessage(controller.enqueue(entries), entries.length));

/// Le voci di menu per aggiungere a una playlist: una per playlist, e una
/// per crearne una nuova. [entries] si legge quando si sceglie.
List<Widget> _playlistMenuItems(BuildContext context, PlayerController controller, List<Track> Function() entries) => [
      for (final playlist in controller.playlists)
        MenuItemButton(
          onPressed: () {
            final list = entries();
            _say(context, addedToPlaylistMessage(playlist.name, controller.addToPlaylist(playlist.id, list), list.length));
          },
          child: Text(playlist.name, overflow: TextOverflow.ellipsis),
        ),
      if (controller.playlists.isNotEmpty) const _MenuSeparator(),
      MenuItemButton(
        onPressed: () async {
          final list = entries();
          final name = await _askName(context, title: 'Nuova playlist', action: 'Crea');
          if (name == null || !context.mounted) return;
          if (controller.createPlaylist(name, entries: list) != null) {
            _say(context, createdPlaylistMessage(name, list.length));
          }
        },
        child: const Text('Nuova playlist…'),
      ),
    ];

/// La riga grigia tra gruppi di voci, come nei menu del Mac.
class _MenuSeparator extends StatelessWidget {
  const _MenuSeparator();

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: CustomPaint(size: const Size(double.infinity, 2), painter: _DotsPainter()),
    );
  }
}

/// Un bottone con il testo in maiuscoletto, bordato, che si inverte
/// premendolo.
class _TextButton extends StatefulWidget {
  const _TextButton({required this.label, required this.onPressed, this.tooltip});

  final String label;
  final VoidCallback? onPressed;
  final String? tooltip;

  @override
  State<_TextButton> createState() => _TextButtonState();
}

class _TextButtonState extends State<_TextButton> {
  bool _pressed = false;

  @override
  Widget build(BuildContext context) {
    final enabled = widget.onPressed != null;
    final inverted = _pressed && enabled;
    final button = Semantics(
      button: true,
      enabled: enabled,
      child: MouseRegion(
        cursor: enabled ? SystemMouseCursors.click : SystemMouseCursors.basic,
        child: GestureDetector(
          onTapDown: (_) => setState(() => _pressed = true),
          onTapUp: (_) => setState(() => _pressed = false),
          onTapCancel: () => setState(() => _pressed = false),
          onTap: widget.onPressed,
          child: Container(
            height: 32,
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: BoxDecoration(
              color: inverted ? _ink : _paper,
              border: Border.all(color: enabled ? _ink : _midGray, width: 2),
            ),
            child: Center(
              widthFactor: 1,
              child: Text(widget.label, style: _silk(12, color: inverted ? _paper : (enabled ? _ink : _midGray))),
            ),
          ),
        ),
      ),
    );
    final tooltip = widget.tooltip;
    return tooltip == null ? button : Tooltip(message: tooltip, child: button);
  }
}

/// Un'icona a pixel su cui si clicca, in negativo al passaggio del mouse.
class _IconButton extends StatefulWidget {
  const _IconButton({
    required this.icon,
    required this.tooltip,
    required this.onPressed,
    this.size = 32,
    this.grid = 16,
    this.inverted = false,
  });

  final List<List<int>> icon;
  final String tooltip;
  final VoidCallback? onPressed;
  final double size;
  final int grid;

  /// Su una riga in negativo (quella che suona).
  final bool inverted;

  @override
  State<_IconButton> createState() => _IconButtonState();
}

class _IconButtonState extends State<_IconButton> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    final enabled = widget.onPressed != null;
    final highlighted = _hover && enabled;
    final background = widget.inverted ? _ink : _paper;
    final foreground = widget.inverted ? _paper : _ink;
    return Tooltip(
      message: widget.tooltip,
      child: Semantics(
        button: true,
        label: widget.tooltip,
        child: MouseRegion(
          cursor: enabled ? SystemMouseCursors.click : SystemMouseCursors.basic,
          onEnter: (_) => setState(() => _hover = true),
          onExit: (_) => setState(() => _hover = false),
          child: GestureDetector(
            onTap: widget.onPressed,
            child: Container(
              width: widget.size,
              height: widget.size,
              alignment: Alignment.center,
              color: highlighted ? foreground : background,
              child: _PixelIcon(
                widget.icon,
                size: 16,
                grid: widget.grid,
                color: !enabled ? _midGray : (highlighted ? background : foreground),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Un menu a tendina che si apre da un'icona a pixel.
class _IconMenu extends StatelessWidget {
  const _IconMenu({required this.icon, required this.tooltip, required this.children, this.inverted = false});

  final List<List<int>> icon;
  final String tooltip;
  final List<Widget> children;
  final bool inverted;

  @override
  Widget build(BuildContext context) {
    return MenuAnchor(
      menuChildren: children,
      builder: (context, menu, _) => _IconButton(
        icon: icon,
        tooltip: tooltip,
        inverted: inverted,
        onPressed: () => menu.isOpen ? menu.close() : menu.open(),
      ),
    );
  }
}

/// Un menu a comparsa del Mac: un riquadro con l'ombra e il triangolo, che
/// mostra la scelta e apre il menu.
class _PopUpMenu extends StatelessWidget {
  const _PopUpMenu({required this.label, required this.children});

  final String label;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return MenuAnchor(
      menuChildren: children,
      builder: (context, menu, _) => MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: () => menu.isOpen ? menu.close() : menu.open(),
          child: Container(
            height: 32,
            padding: const EdgeInsets.only(left: 10, right: 6),
            decoration: const BoxDecoration(
              color: _paper,
              border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
              boxShadow: [BoxShadow(color: _ink, offset: Offset(2, 2))],
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 180),
                  child: Text(label, style: _pixel(15), overflow: TextOverflow.ellipsis),
                ),
                const SizedBox(width: 6),
                const _PixelIcon(_PixelIcon.triangleDown, size: 16, grid: 8),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Una voce di menu con il segno di spunta. Per i filtri il menu resta
/// aperto ([keepOpen]), così se ne sceglie più d'uno.
Widget _checkItem({
  required String label,
  required bool checked,
  required VoidCallback onPressed,
  String? detail,
  bool keepOpen = true,
}) {
  return MenuItemButton(
    closeOnActivate: !keepOpen,
    leadingIcon: SizedBox(
      width: 16,
      child: checked ? const _PixelIcon(_PixelIcon.check, size: 16, grid: 8) : null,
    ),
    trailingIcon: detail == null ? null : Text(detail, style: _vt(20)),
    onPressed: onPressed,
    child: Text(label),
  );
}

/// La barra sotto un elenco: quante voci e i comandi.
class _FooterBar extends StatelessWidget {
  const _FooterBar({required this.text, this.actions = const []});

  final String text;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 46,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: const BoxDecoration(border: Border(top: BorderSide(color: _ink, width: 2))),
      child: Row(
        children: [
          Expanded(child: Text(text.toUpperCase(), style: _silk(11), overflow: TextOverflow.ellipsis)),
          for (final action in actions) ...[const SizedBox(width: 8), action],
        ],
      ),
    );
  }
}

/// I titoli delle colonne sopra un elenco.
class _ColumnHeader extends StatelessWidget {
  const _ColumnHeader({required this.columns});

  /// Titolo e larghezza; null per la colonna che prende il resto.
  final List<(String, double?)> columns;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 28,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
      child: Row(
        children: [
          for (final (title, width) in columns)
            if (width == null)
              Expanded(child: Text(title, style: _silk(11)))
            else
              SizedBox(width: width, child: Text(title, style: _silk(11))),
        ],
      ),
    );
  }
}

/// La barra di scorrimento del Mac: binario grigio, cursore nero.
Widget _scrollbar({required ScrollController controller, required Widget child}) => RawScrollbar(
      controller: controller,
      thumbVisibility: true,
      trackVisibility: true,
      thickness: 14,
      thumbColor: _ink,
      trackColor: _lightGray,
      trackBorderColor: _ink,
      radius: Radius.zero,
      child: child,
    );

// --- Finestre di dialogo ---

/// Il riquadro degli avvisi del Mac: doppia cornice e ombra.
class _DialogFrame extends StatelessWidget {
  const _DialogFrame({required this.title, required this.child, required this.actions, this.width = 440});

  final String title;
  final Widget child;
  final List<Widget> actions;
  final double width;

  @override
  Widget build(BuildContext context) {
    return Dialog(
      child: ConstrainedBox(
        constraints: BoxConstraints(maxWidth: width),
        child: Container(
          padding: const EdgeInsets.all(4),
          decoration: const BoxDecoration(
            color: _paper,
            border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
            boxShadow: [BoxShadow(color: _ink, offset: Offset(4, 4))],
          ),
          child: Container(
            padding: const EdgeInsets.all(18),
            decoration: const BoxDecoration(border: Border.fromBorderSide(BorderSide(color: _ink, width: 2))),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text(title, style: _pixel(20, weight: 700)),
                const SizedBox(height: 14),
                Flexible(child: child),
                const SizedBox(height: 18),
                Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [for (final action in actions) Padding(padding: const EdgeInsets.only(left: 12), child: action)],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// I bottoni arrotondati degli avvisi; quello predefinito ha la cornice
/// spessa, e risponde a Invio.
class _DialogButton extends StatelessWidget {
  const _DialogButton({required this.label, required this.onPressed, this.primary = false});

  final String label;
  final VoidCallback onPressed;
  final bool primary;

  @override
  Widget build(BuildContext context) {
    final button = MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onPressed,
        child: Container(
          height: 32,
          constraints: const BoxConstraints(minWidth: 84),
          padding: const EdgeInsets.symmetric(horizontal: 14),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: _paper,
            borderRadius: BorderRadius.circular(10),
            border: Border.all(color: _ink, width: 2),
          ),
          child: Text(label, style: _pixel(16)),
        ),
      ),
    );
    if (!primary) return button;
    return Container(
      padding: const EdgeInsets.all(2),
      decoration: BoxDecoration(borderRadius: BorderRadius.circular(13), border: Border.all(color: _ink, width: 3)),
      child: button,
    );
  }
}

InputDecoration _fieldDecoration({String? hint, Widget? suffix}) => InputDecoration(
      hintText: hint,
      hintStyle: _pixel(15, color: _midGray),
      suffixIcon: suffix,
      suffixIconConstraints: const BoxConstraints(minWidth: 28, minHeight: 28),
      filled: true,
      fillColor: _paper,
      isDense: true,
      contentPadding: const EdgeInsets.symmetric(horizontal: 8, vertical: 8),
      enabledBorder: const OutlineInputBorder(borderRadius: BorderRadius.zero, borderSide: BorderSide(color: _ink, width: 2)),
      focusedBorder: const OutlineInputBorder(borderRadius: BorderRadius.zero, borderSide: BorderSide(color: _ink, width: 3)),
    );

/// Chiede un nome (di una playlist); null se si annulla o resta vuoto.
Future<String?> _askName(BuildContext context, {required String title, required String action, String initial = ''}) =>
    showDialog<String>(
      context: context,
      builder: (context) => _NameDialog(title: title, action: action, initial: initial),
    );

/// Il campo di testo vive in un widget con stato: il controller deve
/// restare finché la finestra non è sparita del tutto.
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
        style: _pixel(17),
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
        _DialogButton(label: 'Annulla', primary: true, onPressed: () => Navigator.of(context).pop(false)),
        _DialogButton(label: action, onPressed: () => Navigator.of(context).pop(true)),
      ],
      child: Text(message, style: _pixel(16, height: 1.4)),
    ),
  );
  return confirmed ?? false;
}

/// Sceglie il sottobrano di una voce e lo applica con [apply], che dice
/// false se la stessa voce c'è già.
Future<void> _changeSubsong(
  BuildContext context,
  PlayerController controller,
  Track entry,
  bool Function(int? subsong) apply,
) async {
  final choice = await showDialog<({int? subsong})>(
    context: context,
    builder: (context) => _SubsongDialog(entry: entry, subsongs: controller.subsongsOf(entry)),
  );
  if (choice == null || choice.subsong == entry.subsong || !context.mounted) return;
  if (!apply(choice.subsong)) _say(context, 'Questa voce c\'è già!');
}

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

  /// Una scelta, con il pallino dei radio button del Mac.
  Widget _option(String label, String? detail, int? subsong) {
    final selected = widget.entry.subsong == subsong;
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () => Navigator.of(context).pop((subsong: subsong)),
        child: Container(
          height: 32,
          color: _paper,
          child: Row(
            children: [
              Container(
                width: 16,
                height: 16,
                alignment: Alignment.center,
                decoration: const BoxDecoration(
                  shape: BoxShape.circle,
                  border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                ),
                child: selected
                    ? Container(width: 6, height: 6, decoration: const BoxDecoration(color: _ink, shape: BoxShape.circle))
                    : null,
              ),
              const SizedBox(width: 10),
              Expanded(child: Text(label, style: _pixel(16, weight: selected ? 700 : 400))),
              if (detail != null) Text(detail, style: _vt(22)),
            ],
          ),
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
        _DialogButton(label: 'Annulla', primary: subsongs != null, onPressed: () => Navigator.of(context).pop()),
        if (subsongs == null) _DialogButton(label: 'Scegli', primary: true, onPressed: _submitNumber),
      ],
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _option('File intero', 'TUTTI IN FILA', null),
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
            const SizedBox(height: 10),
            Text(
              'Quanti sottobrani ha questo file si sa solo suonandolo. Scrivi il numero:',
              style: _pixel(15, height: 1.35),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: _number,
              autofocus: true,
              keyboardType: TextInputType.number,
              style: _vt(26),
              decoration: _fieldDecoration(hint: 'Sottobrano'),
              onSubmitted: (_) => _submitNumber(),
            ),
          ],
        ],
      ),
    );
  }
}

// --- La finestra con coda, libreria e playlist ---

/// Cosa mostra la finestra sotto «In riproduzione».
enum _View { queue, library, playlist }

class _BrowserWindow extends StatelessWidget {
  const _BrowserWindow({
    required this.controller,
    required this.view,
    required this.playlist,
    required this.onShow,
  });

  final PlayerController controller;
  final _View view;
  final PlaylistInfo? playlist;
  final void Function(_View view, {int? playlist}) onShow;

  @override
  Widget build(BuildContext context) {
    final library = controller.library;
    final title = switch (view) {
      _View.queue => 'Coda',
      _View.library => library.root.isEmpty ? 'Libreria' : 'Libreria — ${baseName(library.root)}',
      _View.playlist => 'Playlist — ${playlist!.name}',
    };

    return _Window(
      title: title,
      expand: true,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _Tabs(controller: controller, view: view, playlist: playlist, onShow: onShow),
          Expanded(
            child: switch (view) {
              _View.queue => _QueueView(controller: controller),
              _View.library => _LibraryView(controller: controller),
              _View.playlist => _PlaylistView(key: ValueKey(playlist!.id), controller: controller, playlist: playlist!),
            },
          ),
        ],
      ),
    );
  }
}

/// Le linguette: la coda, la libreria e un menu con le playlist.
class _Tabs extends StatelessWidget {
  const _Tabs({required this.controller, required this.view, required this.playlist, required this.onShow});

  final PlayerController controller;
  final _View view;
  final PlaylistInfo? playlist;
  final void Function(_View view, {int? playlist}) onShow;

  @override
  Widget build(BuildContext context) {
    Future<void> newPlaylist() async {
      final name = await _askName(context, title: 'Nuova playlist', action: 'Crea');
      if (name == null) return;
      final id = controller.createPlaylist(name);
      if (id != null) onShow(_View.playlist, playlist: id);
    }

    Future<void> importPlaylist() async {
      final playlist = await controller.importPlaylist();
      if (playlist == null || !context.mounted) return;
      onShow(_View.playlist, playlist: playlist.id);
      _say(context, importedPlaylistMessage(playlist));
    }

    return Container(
      height: 36,
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _Tab(
            label: 'CODA',
            detail: controller.tracks.isEmpty ? null : '${controller.tracks.length}',
            selected: view == _View.queue,
            onTap: () => onShow(_View.queue),
          ),
          if (controller.library.available)
            _Tab(
              label: controller.library.scanning ? 'LIBRERIA…' : 'LIBRERIA',
              selected: view == _View.library,
              onTap: () => onShow(_View.library),
            ),
          if (controller.store != null)
            MenuAnchor(
              menuChildren: [
                for (final item in controller.playlists)
                  _checkItem(
                    label: item.name,
                    detail: '${item.count}',
                    checked: view == _View.playlist && playlist?.id == item.id,
                    keepOpen: false,
                    onPressed: () => onShow(_View.playlist, playlist: item.id),
                  ),
                if (controller.playlists.isNotEmpty) const _MenuSeparator(),
                MenuItemButton(onPressed: newPlaylist, child: const Text('Nuova playlist…')),
                MenuItemButton(onPressed: importPlaylist, child: const Text('Importa M3U…')),
              ],
              builder: (context, menu, _) => _Tab(
                label: view == _View.playlist ? playlist!.name.toUpperCase() : 'PLAYLIST',
                selected: view == _View.playlist,
                menu: true,
                onTap: () => menu.isOpen ? menu.close() : menu.open(),
              ),
            ),
        ],
      ),
    );
  }
}

class _Tab extends StatelessWidget {
  const _Tab({required this.label, required this.selected, required this.onTap, this.detail, this.menu = false});

  final String label;
  final String? detail;
  final bool selected;
  final bool menu;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final color = selected ? _paper : _ink;
    return Semantics(
      button: true,
      selected: selected,
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: onTap,
          child: Container(
            constraints: const BoxConstraints(maxWidth: 240),
            padding: const EdgeInsets.symmetric(horizontal: 14),
            decoration: BoxDecoration(
              color: selected ? _ink : _paper,
              border: const Border(right: BorderSide(color: _ink, width: 2)),
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Flexible(child: Text(label, style: _silk(12, color: color), overflow: TextOverflow.ellipsis)),
                if (detail != null) ...[const SizedBox(width: 8), Text(detail!, style: _vt(22, color: color))],
                if (menu) ...[
                  const SizedBox(width: 6),
                  _PixelIcon(_PixelIcon.triangleDown, size: 16, grid: 8, color: color),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}

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
    final color = current ? _paper : _ink;
    return Semantics(
      button: true,
      selected: current,
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: onTap,
          child: Container(
            height: 44,
            color: current ? _ink : _paper,
            padding: const EdgeInsets.only(left: 12, right: 18),
            child: Row(
              children: [
                ReorderableDragStartListener(
                  index: index,
                  child: MouseRegion(
                    cursor: SystemMouseCursors.grab,
                    child: Tooltip(
                      message: 'Trascina per spostare',
                      waitDuration: const Duration(milliseconds: 800),
                      child: SizedBox(
                        width: 40,
                        height: 44,
                        child: Align(
                          alignment: Alignment.centerLeft,
                          child: Text((index + 1).toString().padLeft(2, '0'), style: _vt(22, color: color)),
                        ),
                      ),
                    ),
                  ),
                ),
                Expanded(child: Text(entry.fileName, style: _pixel(16, color: color), overflow: TextOverflow.ellipsis)),
                SizedBox(
                  width: 220,
                  child: Text(entry.location, style: _pixel(14, color: color), overflow: TextOverflow.ellipsis),
                ),
                _IconMenu(icon: _PixelIcon.dots, tooltip: 'Altro', inverted: current, children: menu),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Le voci in un elenco che si riordina trascinando.
class _ReorderableEntries extends StatefulWidget {
  const _ReorderableEntries({required this.entries, required this.onMove, required this.row, required this.empty});

  final List<Track> entries;
  final void Function(int from, int to) onMove;
  final Widget Function(int index) row;
  final String empty;

  @override
  State<_ReorderableEntries> createState() => _ReorderableEntriesState();
}

class _ReorderableEntriesState extends State<_ReorderableEntries> {
  final _scroll = ScrollController();

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final entries = widget.entries;
    if (entries.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Text(widget.empty, style: _pixel(16), textAlign: TextAlign.center),
        ),
      );
    }
    return _scrollbar(
      controller: _scroll,
      child: ReorderableListView.builder(
        scrollController: _scroll,
        buildDefaultDragHandles: false,
        itemCount: entries.length,
        onReorderItem: widget.onMove,
        proxyDecorator: (child, index, animation) => Container(
          decoration: const BoxDecoration(
            border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
            boxShadow: [BoxShadow(color: _ink, offset: Offset(3, 3))],
          ),
          child: Material(color: _paper, child: child),
        ),
        itemBuilder: (context, index) => KeyedSubtree(
          key: ValueKey('${entries[index].ref}#${entries[index].subsong}'),
          child: widget.row(index),
        ),
      ),
    );
  }
}

class _QueueView extends StatelessWidget {
  const _QueueView({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final tracks = controller.tracks;

    Future<void> saveAsPlaylist() async {
      final name = await _askName(context, title: 'Salva la coda come playlist', action: 'Salva');
      if (name == null || !context.mounted) return;
      if (controller.saveQueueAsPlaylist(name) != null) _say(context, 'Playlist «$name» salvata');
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const _ColumnHeader(columns: [('#', 40), ('BRANO', null), ('POSIZIONE', 220), ('', 32)]),
        Expanded(
          child: _ReorderableEntries(
            entries: tracks,
            onMove: controller.moveTrack,
            empty: controller.library.available
                ? 'Coda vuota: apri dei file dal menu, o scegli dei brani dalla libreria'
                : 'Coda vuota: usa Apri… nel menu',
            row: (i) => _EntryRow(
              index: i,
              entry: tracks[i],
              current: i == controller.currentIndex,
              onTap: () => controller.playIndex(i),
              menu: [
                MenuItemButton(onPressed: () => controller.playIndex(i), child: const Text('Riproduci')),
                MenuItemButton(
                  onPressed: () => _changeSubsong(context, controller, tracks[i], (s) => controller.setTrackSubsong(i, s)),
                  child: const Text('Sottobrano…'),
                ),
                if (controller.store != null)
                  SubmenuButton(
                    menuChildren: _playlistMenuItems(context, controller, () => [tracks[i]]),
                    child: const Text('Aggiungi a playlist'),
                  ),
                const _MenuSeparator(),
                MenuItemButton(onPressed: () => controller.removeTrack(i), child: const Text('Togli dalla coda')),
              ],
            ),
          ),
        ),
        _FooterBar(
          text: tracks.isEmpty ? 'vuota' : tracksLabel(tracks.length),
          actions: [
            if (controller.store != null) ...[
              _TextButton(label: 'SALVA COME PLAYLIST…', onPressed: tracks.isEmpty ? null : saveAsPlaylist),
              _TextButton(
                label: 'ESPORTA M3U…',
                onPressed: tracks.isEmpty ? null : () => _exportPlaylist(context, controller, null, 'Coda'),
              ),
            ],
            _TextButton(label: 'SVUOTA', onPressed: tracks.isEmpty ? null : controller.clearQueue),
          ],
        ),
      ],
    );
  }
}

class _PlaylistView extends StatelessWidget {
  const _PlaylistView({super.key, required this.controller, required this.playlist});

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

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
          child: Row(
            children: [
              _TextButton(
                label: 'RIPRODUCI',
                tooltip: 'Mette la playlist al posto della coda',
                onPressed: entries.isEmpty ? null : () => controller.loadPlaylist(id),
              ),
              const SizedBox(width: 8),
              _TextButton(
                label: 'IN CODA',
                tooltip: 'Aggiunge le voci in fondo alla coda',
                onPressed: entries.isEmpty ? null : () => _enqueue(context, controller, entries),
              ),
              const Spacer(),
              _TextButton(
                label: 'ESPORTA M3U…',
                onPressed: entries.isEmpty ? null : () => _exportPlaylist(context, controller, id, playlist.name),
              ),
              const SizedBox(width: 8),
              _TextButton(label: 'RINOMINA…', onPressed: rename),
              const SizedBox(width: 8),
              _TextButton(label: 'ELIMINA…', onPressed: delete),
            ],
          ),
        ),
        const _ColumnHeader(columns: [('#', 40), ('BRANO', null), ('POSIZIONE', 220), ('', 32)]),
        Expanded(
          child: _ReorderableEntries(
            entries: entries,
            onMove: (from, to) => controller.movePlaylistTrack(id, from, to),
            empty: 'Playlist vuota: aggiungi brani dalla libreria, dalla coda o da quello che suona',
            row: (i) => _EntryRow(
              index: i,
              entry: entries[i],
              current: controller.currentTrack?.sameEntry(entries[i]) ?? false,
              onTap: () => controller.playNow(entries[i]),
              menu: [
                MenuItemButton(onPressed: () => controller.playNow(entries[i]), child: const Text('Riproduci subito')),
                MenuItemButton(
                  onPressed: () => controller.loadPlaylist(id, start: i),
                  child: const Text('Riproduci la playlist da qui'),
                ),
                MenuItemButton(
                  onPressed: () => _enqueue(context, controller, [entries[i]]),
                  child: const Text('Aggiungi alla coda'),
                ),
                MenuItemButton(
                  onPressed: () =>
                      _changeSubsong(context, controller, entries[i], (s) => controller.setPlaylistSubsong(id, i, s)),
                  child: const Text('Sottobrano…'),
                ),
                const _MenuSeparator(),
                MenuItemButton(
                  onPressed: () => controller.removeFromPlaylist(id, i),
                  child: const Text('Togli dalla playlist'),
                ),
              ],
            ),
          ),
        ),
        _FooterBar(text: tracksLabel(entries.length)),
      ],
    );
  }
}
