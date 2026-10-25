part of 'retro_skin.dart';

// La libreria della skin retrò: un elenco alla Finder, con il percorso, la
// ricerca e i filtri nei menu a comparsa.

Color _platformColor(String id) => switch (id) {
      'amiga' => _teal,
      'c64' => _lilac,
      'pc' => _pink,
      'audio' => _paper,
      _ => _lightGray,
    };

class _LibraryView extends StatefulWidget {
  const _LibraryView({required this.controller});

  final PlayerController controller;

  @override
  State<_LibraryView> createState() => _LibraryViewState();
}

class _LibraryViewState extends State<_LibraryView> {
  late final TextEditingController _search = TextEditingController(text: widget.controller.library.query.text);

  // Senza conservare la posizione: cambiando cartella o ricerca si torna in
  // cima (vedi build).
  final _scroll = ScrollController(keepScrollOffset: false);
  String? _shownQuery;
  bool _loadingMore = false;

  /// I brani aperti per vederne i sottobrani, con le durate.
  final Map<String, LibraryItem?> _expanded = {};

  PlayerController get controller => widget.controller;
  LibraryBrowser get library => widget.controller.library;

  @override
  void dispose() {
    _search.dispose();
    _scroll.dispose();
    super.dispose();
  }

  Future<void> _chooseRoot() async {
    final path = await controller.chooseFolder();
    if (path == null || !mounted) return;
    final root = library.root;
    if (root.isNotEmpty && path != root) {
      final confirmed = await _confirm(
        context,
        title: 'Cambiare cartella?',
        message: 'L\'indice di «${baseName(root)}» si svuota e megaplayer legge «${baseName(path)}». '
            'Playlist e coda restano come sono.',
        action: 'Cambia',
      );
      if (!confirmed || !mounted) return;
    }
    _search.clear();
    _expanded.clear();
    library.clearFilters();
    library.setRoot(path);
  }

  void _clearFilters() {
    _search.clear();
    library.clearFilters();
  }

  /// Arrivati quasi in fondo, le righe dopo: dopo il frame, perché le
  /// notifiche di scorrimento arrivano anche durante il layout.
  bool _onScroll(ScrollNotification notification) {
    if (notification.metrics.extentAfter < 400 && library.items.length < library.total && !_loadingMore) {
      _loadingMore = true;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        _loadingMore = false;
        if (mounted) library.loadMore();
      });
    }
    return false;
  }

  void _toggle(LibraryItem item) {
    setState(() {
      if (_expanded.containsKey(item.ref)) {
        _expanded.remove(item.ref);
      } else {
        _expanded[item.ref] = library.lookup(item.ref);
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    if (!library.available) {
      return Center(child: Text('Il database della libreria non si è aperto', style: _pixel(16)));
    }
    if (library.root.isEmpty) return _emptyLibrary();

    final query = library.query;
    final shown = '${query.folder}|${query.text}|${query.platforms}|${query.formats}';
    if (_shownQuery != null && shown != _shownQuery) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scroll.hasClients) _scroll.jumpTo(0);
      });
    }
    _shownQuery = shown;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _toolbar(),
        _filters(),
        if (library.scanning) _scanProgress(),
        if (library.error != null)
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
            decoration: const BoxDecoration(
              color: _pink,
              border: Border(bottom: BorderSide(color: _ink, width: 2)),
            ),
            child: Text(library.error!, style: _pixel(15)),
          ),
        const _ColumnHeader(columns: [('NOME', null), ('FORMATO', 170), ('DURATA', 64), ('', 64)]),
        Expanded(child: _list()),
        _footer(),
      ],
    );
  }

  Widget _emptyLibrary() {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const _PixelIcon(_PixelIcon.folder, size: 48),
            const SizedBox(height: 14),
            Text('Scegli la cartella della tua musica', style: _pixel(22, weight: 700)),
            const SizedBox(height: 8),
            ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 440),
              child: Text(
                'megaplayer la legge tutta, archivi compresi, e poi a ogni avvio guarda solo i file nuovi o '
                'cambiati. Da lì cerchi e filtri per piattaforma e formato.',
                textAlign: TextAlign.center,
                style: _pixel(16, height: 1.35),
              ),
            ),
            const SizedBox(height: 18),
            _TextButton(label: 'SCEGLI CARTELLA…', onPressed: _chooseRoot),
          ],
        ),
      ),
    );
  }

  /// Il percorso, con la radice sempre in vista, e la ricerca.
  Widget _toolbar() {
    final path = library.path;

    Widget crumb(String label, String? folder) {
      final current = folder == null;
      final text = Text(label, style: _pixel(15, weight: current ? 700 : 400));
      if (current) return text;
      return MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: () => library.openFolder(folder),
          child: Text(label, style: _pixel(15).copyWith(decoration: TextDecoration.underline)),
        ),
      );
    }

    return Container(
      padding: const EdgeInsets.fromLTRB(6, 6, 12, 6),
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
      child: Row(
        children: [
          _IconButton(
            icon: _PixelIcon.up,
            grid: 8,
            tooltip: 'Cartella superiore',
            onPressed: path.isEmpty ? null : () => library.openFolder(path.take(path.length - 1).join('/')),
          ),
          const SizedBox(width: 2),
          const _PixelIcon(_PixelIcon.home, size: 16),
          const SizedBox(width: 6),
          crumb(baseName(library.root), path.isEmpty ? null : ''),
          Expanded(
            child: ClipRect(
              child: LayoutBuilder(
                builder: (context, constraints) => SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  reverse: true,
                  child: ConstrainedBox(
                    constraints: BoxConstraints(minWidth: constraints.maxWidth),
                    child: Row(
                      children: [
                        for (var i = 0; i < path.length; i++) ...[
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 4),
                            child: _PixelIcon(_PixelIcon.triangleRight, size: 12, grid: 8),
                          ),
                          crumb(path[i], i == path.length - 1 ? null : path.take(i + 1).join('/')),
                        ],
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(width: 12),
          SizedBox(
            width: 230,
            child: TextField(
              controller: _search,
              style: _pixel(15),
              decoration: _fieldDecoration(
                hint: 'Cerca…',
                suffix: library.query.text.isEmpty
                    ? null
                    : _IconButton(
                        icon: _PixelIcon.close,
                        grid: 8,
                        size: 26,
                        tooltip: 'Cancella',
                        onPressed: () {
                          _search.clear();
                          library.setText('');
                        },
                      ),
              ),
              onChanged: library.setText,
            ),
          ),
        ],
      ),
    );
  }

  /// I filtri nei menu a comparsa, e i comandi della cartella.
  Widget _filters() {
    final query = library.query;
    final facets = library.facets;

    String chosen(Set<String> values, String none, String Function(String id) name) => switch (values.length) {
          0 => none,
          1 => name(values.first),
          _ => '${name(values.first)} +${values.length - 1}',
        };
    String platformName(String id) => facets.platforms.where((p) => p.id == id).firstOrNull?.name ?? id;
    String formatName(String id) => id.isEmpty ? 'sconosciuto' : id;
    final scanning = library.scanning;

    return Container(
      padding: const EdgeInsets.fromLTRB(12, 6, 12, 8),
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
      child: Row(
        children: [
          Text('PIATTAFORMA', style: _silk(11)),
          const SizedBox(width: 8),
          _PopUpMenu(
            label: chosen(query.platforms, 'Tutte', platformName),
            children: [
              _checkItem(
                label: 'Tutte',
                checked: query.platforms.isEmpty,
                onPressed: () => library.setFilters(platforms: const {}),
              ),
              const _MenuSeparator(),
              for (final platform in facets.platforms)
                _checkItem(
                  label: platform.name,
                  detail: formatCount(platform.count),
                  checked: query.platforms.contains(platform.id),
                  onPressed: () => library.togglePlatform(platform.id),
                ),
            ],
          ),
          const SizedBox(width: 16),
          Text('FORMATO', style: _silk(11)),
          const SizedBox(width: 8),
          Flexible(
            child: _PopUpMenu(
              label: chosen(query.formats, 'Tutti', formatName),
              children: [
                _checkItem(
                  label: 'Tutti',
                  checked: query.formats.isEmpty,
                  onPressed: () => library.setFilters(formats: const {}),
                ),
                const _MenuSeparator(),
                for (final format in [...facets.formats]..sort((a, b) => b.count.compareTo(a.count)))
                  _checkItem(
                    label: formatName(format.id),
                    detail: formatCount(format.count),
                    checked: query.formats.contains(format.id),
                    onPressed: () => library.toggleFormat(format.id),
                  ),
              ],
            ),
          ),
          const Spacer(),
          _TextButton(label: 'CARTELLA…', tooltip: 'Scegli la cartella della musica', onPressed: _chooseRoot),
          const SizedBox(width: 8),
          _TextButton(
            label: scanning ? 'FERMA' : 'AGGIORNA',
            tooltip: scanning ? 'Ferma la lettura della cartella' : 'Rilegge i file nuovi o cambiati e toglie quelli spariti',
            onPressed: scanning ? library.cancelScan : library.rescan,
          ),
        ],
      ),
    );
  }

  Widget _scanProgress() {
    final scan = library.scan;
    final total = scan.filesFound;
    final text = !scan.running
        ? 'Fermo la lettura…'
        : scan.walking
            ? 'Conto i file della cartella… ${formatCount(total)}'
            : 'File controllati ${formatCount(scan.filesChecked)} di ${formatCount(total)} · '
                'nuovi ${formatCount(scan.filesRead)} · brani ${formatCount(scan.tracksFound)}';
    return Container(
      padding: const EdgeInsets.fromLTRB(12, 6, 12, 6),
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
      child: Row(
        children: [
          SizedBox(
            width: 160,
            child: CustomPaint(
              size: const Size(160, 18),
              painter: _ProgressStripPainter(
                fraction: scan.walking || total == 0 ? null : (scan.filesChecked / total).clamp(0.0, 1.0),
                position: total / 40,
              ),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(child: Text(text.toUpperCase(), style: _silk(11), overflow: TextOverflow.ellipsis)),
        ],
      ),
    );
  }

  Widget _footer() {
    final browsing = library.browsing;
    final folders = library.folders.length;
    final summary = browsing
        ? [
            if (folders > 0) '${formatCount(folders)} ${folders == 1 ? 'cartella' : 'cartelle'}',
            if (library.total > 0 || folders == 0) tracksLabel(library.total),
          ].join(' · ')
        : '${tracksLabel(library.total)} trovati';
    final empty = library.total == 0 && folders == 0;

    return _FooterBar(
      text: summary,
      actions: [
        _TextButton(
          label: 'TUTTO IN CODA',
          tooltip: browsing
              ? 'Mette in coda i brani di questa cartella e delle sottocartelle'
              : 'Mette in coda tutti i risultati',
          onPressed: empty ? null : () => _enqueue(context, controller, library.allTracks()),
        ),
        if (controller.store != null)
          _IconMenu(
            icon: _PixelIcon.listAdd,
            tooltip: 'Aggiungi tutto a una playlist',
            children: _playlistMenuItems(context, controller, library.allTracks),
          ),
      ],
    );
  }

  Widget _list() {
    final folders = library.folders;
    final items = library.items;
    if (folders.isEmpty && items.isEmpty) {
      final String message;
      if (library.scanning) {
        message = 'Sto leggendo la cartella: i brani compaiono man mano';
      } else if (!library.browsing) {
        message = 'Nessun brano trovato';
      } else {
        message = 'Nessun brano in questa cartella';
      }
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(message, style: _pixel(16)),
            if (!library.browsing) ...[
              const SizedBox(height: 12),
              _TextButton(label: 'TOGLI RICERCA E FILTRI', onPressed: _clearFilters),
            ],
          ],
        ),
      );
    }

    // Le righe: cartelle, brani, e sotto i brani aperti i loro sottobrani.
    final rows = <(Key, Widget Function())>[
      for (final folder in folders) (ValueKey('d:${folder.name}'), () => _folderRow(folder)),
      for (final item in items) ...[
        (ValueKey('t:${item.ref}'), () => _trackRow(item)),
        if (_expanded.containsKey(item.ref))
          for (var i = 0; i < item.subsongs; i++)
            (ValueKey('s:${item.ref}#$i'), () => _subsongRow(item, i, _expanded[item.ref])),
      ],
      if (items.length < library.total) (const ValueKey('more'), _moreRow),
    ];

    return NotificationListener<ScrollNotification>(
      onNotification: _onScroll,
      child: _scrollbar(
        controller: _scroll,
        child: ListView.builder(
          controller: _scroll,
          itemCount: rows.length,
          itemExtent: 36,
          findChildIndexCallback: (key) {
            final index = rows.indexWhere((row) => row.$1 == key);
            return index < 0 ? null : index;
          },
          itemBuilder: (context, index) => KeyedSubtree(key: rows[index].$1, child: rows[index].$2()),
        ),
      ),
    );
  }

  Widget _folderRow(LibraryFacet folder) {
    final path = library.subfolderPath(folder.name);
    return _LibraryRow(
      onTap: () => library.openSubfolder(folder.name),
      leading: _PixelIcon(looksLikeArchive(folder.name) ? _PixelIcon.archive : _PixelIcon.folder, size: 16),
      name: folder.name,
      format: tracksLabel(folder.count),
      actions: (inverted) => [
        _IconButton(
          icon: _PixelIcon.plus,
          tooltip: 'Metti in coda tutta la cartella',
          inverted: inverted,
          onPressed: () => _enqueue(context, controller, library.allTracks(folder: path)),
        ),
        if (controller.store != null)
          _IconMenu(
            icon: _PixelIcon.listAdd,
            tooltip: 'Aggiungi la cartella a una playlist',
            inverted: inverted,
            children: _playlistMenuItems(context, controller, () => library.allTracks(folder: path)),
          ),
      ],
    );
  }

  Widget _trackRow(LibraryItem item) {
    final current = controller.currentTrack;
    final playing = current != null && current.ref == item.ref;
    final expanded = _expanded.containsKey(item.ref);
    final details = [
      if (item.author.isNotEmpty) item.author,
      if (item.title.isNotEmpty) item.name,
      if (item.subsongs > 1) '${item.subsongs} sottobrani',
      if (!library.browsing && item.folder.isNotEmpty) item.folder,
    ].join(' · ');

    return _LibraryRow(
      selected: playing,
      onTap: () => controller.playNow(item.toTrack()),
      leading: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            width: 18,
            child: item.subsongs > 1
                ? Tooltip(
                    message: expanded ? 'Chiudi i sottobrani' : 'Mostra i ${item.subsongs} sottobrani',
                    child: GestureDetector(
                      onTap: () => _toggle(item),
                      child: _PixelIcon(
                        expanded ? _PixelIcon.triangleDown : _PixelIcon.triangleRight,
                        size: 16,
                        grid: 8,
                        color: playing ? _paper : _ink,
                      ),
                    ),
                  )
                : null,
          ),
          Container(
            width: 12,
            height: 12,
            decoration: BoxDecoration(
              color: _platformColor(item.platform),
              border: Border.all(color: playing ? _paper : _ink, width: 2),
            ),
          ),
        ],
      ),
      name: item.displayTitle,
      detail: details,
      format: item.formatLabel,
      duration: item.duration,
      actions: (inverted) => [
        _IconButton(
          icon: _PixelIcon.plus,
          tooltip: item.subsongs > 1 ? 'Aggiungi alla coda il file intero' : 'Aggiungi alla coda',
          inverted: inverted,
          onPressed: () => _enqueue(context, controller, [item.toTrack()]),
        ),
        if (controller.store != null)
          _IconMenu(
            icon: _PixelIcon.listAdd,
            tooltip: 'Aggiungi a una playlist',
            inverted: inverted,
            children: _playlistMenuItems(context, controller, () => [item.toTrack()]),
          ),
      ],
    );
  }

  Widget _subsongRow(LibraryItem item, int index, LibraryItem? details) {
    final entry = item.toTrack(subsong: index);
    final current = controller.currentTrack;
    final playing = current != null &&
        current.ref == item.ref &&
        (current.subsong == index || (current.subsong == null && controller.status.subsong == index));
    final duration = details != null && index < details.durations.length ? details.durations[index] : null;

    return _LibraryRow(
      selected: playing,
      indent: 34,
      onTap: () => controller.playNow(entry),
      leading: const SizedBox(width: 0),
      name: 'Sottobrano ${index + 1}${index == item.startSubsong && item.startSubsong > 0 ? ' (iniziale)' : ''}',
      duration: duration,
      actions: (inverted) => [
        _IconButton(
          icon: _PixelIcon.plus,
          tooltip: 'Aggiungi alla coda questo sottobrano',
          inverted: inverted,
          onPressed: () => _enqueue(context, controller, [entry]),
        ),
        if (controller.store != null)
          _IconMenu(
            icon: _PixelIcon.listAdd,
            tooltip: 'Aggiungi a una playlist questo sottobrano',
            inverted: inverted,
            children: _playlistMenuItems(context, controller, () => [entry]),
          ),
      ],
    );
  }

  Widget _moreRow() => Center(
        child: Text('Altri ${formatCount(library.total - library.items.length)} brani…', style: _pixel(15)),
      );
}

/// Una riga della libreria nelle colonne nome, formato, durata; quella che
/// suona è in negativo.
class _LibraryRow extends StatelessWidget {
  const _LibraryRow({
    required this.onTap,
    required this.leading,
    required this.name,
    required this.actions,
    this.detail = '',
    this.format = '',
    this.duration,
    this.selected = false,
    this.indent = 0,
  });

  final VoidCallback onTap;
  final Widget leading;
  final String name;
  final String detail;
  final String format;
  final double? duration;
  final bool selected;
  final double indent;
  final List<Widget> Function(bool inverted) actions;

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
            color: selected ? _ink : _paper,
            padding: EdgeInsets.only(left: 8 + indent, right: 18),
            child: Row(
              children: [
                leading,
                const SizedBox(width: 8),
                Expanded(
                  child: Text.rich(
                    TextSpan(
                      children: [
                        TextSpan(text: name, style: _pixel(16, color: color)),
                        if (detail.isNotEmpty) TextSpan(text: '  $detail', style: _pixel(13, color: selected ? _lightGray : _midGray)),
                      ],
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                SizedBox(
                  width: 170,
                  child: Text(format, style: _pixel(14, color: color), overflow: TextOverflow.ellipsis),
                ),
                SizedBox(
                  width: 64,
                  child: Text(duration == null ? '' : formatTime(duration), style: _vt(22, color: color)),
                ),
                SizedBox(width: 64, child: Row(children: actions(selected))),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
