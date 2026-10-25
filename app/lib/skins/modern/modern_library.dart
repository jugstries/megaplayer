part of 'modern_skin.dart';

// La libreria della skin moderna: la cartella della musica da sfogliare,
// con ricerca e filtri, e il lettore compatto che resta in cima.

Color _platformColor(String id) => switch (id) {
      'amiga' => _orange,
      'pc' => _pink,
      'c64' => _violet,
      'atarist' || 'atari8' => const Color(0xFFFFB27A),
      'nes' || 'snes' || 'gameboy' => const Color(0xFFB58CFF),
      'sega' || 'pcengine' || 'msx' || 'zxspectrum' => const Color(0xFFC9A6FF),
      _ => const Color(0xFFD9C2FF),
    };

const _archiveExtensions = {'lha', 'lzh', 'lzx', 'zip', '7z', 'rar', 'tar', 'gz', 'tgz', 'bz2', 'xz', 'iso'};

bool _looksLikeArchive(String name) {
  final dot = name.lastIndexOf('.');
  return dot > 0 && _archiveExtensions.contains(name.substring(dot + 1).toLowerCase());
}

String _baseName(String path) {
  final parts = path.split('/').where((part) => part.isNotEmpty);
  return parts.isEmpty ? path : parts.last;
}

/// Il lettore compatto in cima alla libreria e alle playlist.
class _MiniPlayer extends StatelessWidget {
  const _MiniPlayer({required this.controller, required this.onOpen});

  final PlayerController controller;
  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    final track = controller.currentTrack;
    final details = controller.details;
    final status = controller.status;
    final title = track == null
        ? 'Nessun brano in riproduzione'
        : ((details?.title.isNotEmpty ?? false) ? details!.title : track.fileName);
    final subtitle = track == null
        ? 'Scegli un brano dalla libreria'
        : [
            if (title != track.fileName) track.fileName,
            if (details != null && details.formatLabel.isNotEmpty) details.formatLabel,
            if (status.subsongCount > 1) 'sottobrano ${status.subsong + 1} di ${status.subsongCount}',
          ].join(' · ');
    final duration = status.duration;
    final fraction = duration == null || duration <= 0 ? null : (status.position / duration).clamp(0.0, 1.0);

    return _Panel(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Expanded(
                child: Tooltip(
                  message: 'Vai a «In riproduzione»',
                  waitDuration: const Duration(milliseconds: 600),
                  child: InkWell(
                    onTap: onOpen,
                    borderRadius: BorderRadius.circular(12),
                    child: Row(
                      children: [
                        Container(
                          width: 44,
                          height: 44,
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            gradient: track == null ? null : _accentGradient,
                            color: track == null ? _surfaceRaised : null,
                            borderRadius: BorderRadius.circular(12),
                          ),
                          child: track == null
                              ? const Icon(Icons.music_note_rounded, size: 20, color: _faint)
                              : Text(
                                  _TrackHeading._initials(
                                      (details?.formatLabel.isNotEmpty ?? false) ? details!.formatLabel : track.fileName),
                                  style: _sora(15, weight: 700, color: _background, spacing: -0.6),
                                ),
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Text(title, style: _sora(15, weight: 600), overflow: TextOverflow.ellipsis),
                              if (subtitle.isNotEmpty)
                                Text(subtitle, style: _sora(12, color: _muted), overflow: TextOverflow.ellipsis),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Text('${formatTime(status.position)} / ${formatTime(duration)}', style: _mono(12)),
              const SizedBox(width: 12),
              _SmallIconButton(
                icon: Icons.skip_previous_rounded,
                tooltip: 'Brano precedente',
                onPressed: controller.previous,
              ),
              const SizedBox(width: 4),
              Tooltip(
                message: status.isPlaying ? 'Pausa' : 'Riproduci',
                child: InkWell(
                  onTap: controller.togglePause,
                  customBorder: const CircleBorder(),
                  child: Container(
                    width: 44,
                    height: 44,
                    decoration: const BoxDecoration(gradient: _accentGradient, shape: BoxShape.circle),
                    child: Icon(
                      status.isPlaying ? Icons.pause_rounded : Icons.play_arrow_rounded,
                      size: 24,
                      color: _background,
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 4),
              _SmallIconButton(icon: Icons.skip_next_rounded, tooltip: 'Brano successivo', onPressed: controller.next),
              if (controller.store != null && track != null)
                _MenuIconButton(
                  icon: Icons.playlist_add_rounded,
                  tooltip: 'Aggiungi a una playlist quello che suona',
                  children: _playlistMenuItems(context, controller, () => [controller.playingEntry!]),
                ),
            ],
          ),
          const SizedBox(height: 10),
          ClipRRect(
            borderRadius: BorderRadius.circular(2),
            child: SizedBox(
              height: 3,
              child: Stack(
                fit: StackFit.expand,
                children: [
                  const ColoredBox(color: _track),
                  if (fraction != null)
                    FractionallySizedBox(
                      alignment: Alignment.centerLeft,
                      widthFactor: fraction,
                      child: const DecoratedBox(decoration: BoxDecoration(gradient: _accentGradient)),
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

class _LibraryPanel extends StatefulWidget {
  const _LibraryPanel({required this.controller});

  final PlayerController controller;

  @override
  State<_LibraryPanel> createState() => _LibraryPanelState();
}

class _LibraryPanelState extends State<_LibraryPanel> {
  static const _visibleFormats = 12;

  late final TextEditingController _search = TextEditingController(text: widget.controller.library.query.text);
  bool _allFormats = false;
  bool _loadingMore = false;

  /// I brani aperti per vederne i sottobrani, con le durate.
  final Map<String, LibraryItem?> _expanded = {};

  PlayerController get controller => widget.controller;
  LibraryBrowser get library => widget.controller.library;

  @override
  void dispose() {
    _search.dispose();
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
        message: 'L\'indice di «${_baseName(root)}» si svuota e megaplayer legge «${_baseName(path)}». '
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
    if (notification.metrics.extentAfter < 600 && library.items.length < library.total && !_loadingMore) {
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
      return _Panel(
        child: Center(
          child: Text('Il database della libreria non si è aperto', style: _sora(15, color: _muted)),
        ),
      );
    }
    if (library.root.isEmpty) return _emptyLibrary();

    return _Panel(
      padding: const EdgeInsets.fromLTRB(24, 20, 24, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _header(),
          if (library.scanning) ...[const SizedBox(height: 12), _ScanProgress(scan: library.scan)],
          if (library.error != null) ...[
            const SizedBox(height: 8),
            Text(library.error!, style: _sora(13, color: const Color(0xFFFFC2D2))),
          ],
          const SizedBox(height: 16),
          TextField(
            controller: _search,
            style: _sora(14),
            decoration: _fieldDecoration(
              hint: 'Cerca per titolo, autore, nome del file o cartella',
              prefix: const Icon(Icons.search_rounded, size: 20, color: _muted),
              suffix: library.query.text.isEmpty
                  ? null
                  : _SmallIconButton(
                      icon: Icons.close_rounded,
                      tooltip: 'Cancella',
                      onPressed: () {
                        _search.clear();
                        library.setText('');
                      },
                    ),
            ),
            onChanged: library.setText,
          ),
          const SizedBox(height: 12),
          _filters(),
          const SizedBox(height: 12),
          _locationBar(),
          const SizedBox(height: 8),
          Expanded(child: _list()),
        ],
      ),
    );
  }

  Widget _emptyLibrary() {
    return _Panel(
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 72,
              height: 72,
              decoration: BoxDecoration(gradient: _accentGradient, borderRadius: BorderRadius.circular(22)),
              child: const Icon(Icons.library_music_rounded, size: 38, color: _background),
            ),
            const SizedBox(height: 20),
            Text('Scegli la cartella della tua musica', style: _sora(24, weight: 700, spacing: -0.5)),
            const SizedBox(height: 10),
            ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 460),
              child: Text(
                'megaplayer la legge tutta, archivi compresi, e poi a ogni avvio guarda solo i file nuovi o '
                'cambiati. Da lì cerchi e filtri per piattaforma e formato.',
                textAlign: TextAlign.center,
                style: _sora(14, color: _textSoft, height: 1.5),
              ),
            ),
            const SizedBox(height: 24),
            _SmallButton(icon: Icons.folder_open_rounded, label: 'Scegli cartella…', accent: true, onPressed: _chooseRoot),
          ],
        ),
      ),
    );
  }

  Widget _header() {
    final scanning = library.scanning;
    return Row(
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Libreria', style: _sora(18, weight: 600)),
              const SizedBox(height: 2),
              Text(library.root, style: _mono(12), overflow: TextOverflow.ellipsis),
            ],
          ),
        ),
        const SizedBox(width: 12),
        _SmallButton(icon: Icons.folder_open_rounded, label: 'Cartella…', onPressed: _chooseRoot),
        const SizedBox(width: 8),
        Tooltip(
          message: scanning ? 'Ferma la lettura della cartella' : 'Rilegge i file nuovi o cambiati e toglie quelli spariti',
          child: _SmallButton(
            icon: scanning ? Icons.stop_rounded : Icons.refresh_rounded,
            label: scanning ? 'Ferma' : 'Aggiorna',
            onPressed: scanning ? library.cancelScan : library.rescan,
          ),
        ),
      ],
    );
  }

  Widget _filters() {
    final query = library.query;
    final facets = library.facets;

    // Un valore scelto resta visibile anche se la ricerca non ne trova.
    List<LibraryFacet> withSelected(List<LibraryFacet> values, Set<String> selected) => [
          ...values,
          for (final id in selected)
            if (!values.any((value) => value.id == id)) LibraryFacet(id: id, name: id, count: 0),
        ];

    final platforms = withSelected(facets.platforms, query.platforms);
    final byCount = withSelected(facets.formats, query.formats)..sort((a, b) => b.count.compareTo(a.count));
    final formats = _allFormats
        ? byCount
        : [
            for (var i = 0; i < byCount.length; i++)
              if (i < _visibleFormats || query.formats.contains(byCount[i].id)) byCount[i],
          ];
    final hidden = byCount.length - formats.length;

    Widget row(String label, List<Widget> chips) => Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            SizedBox(
              width: 116,
              child: Padding(padding: const EdgeInsets.only(top: 7), child: Text(label, style: _label())),
            ),
            Expanded(child: Wrap(spacing: 6, runSpacing: 6, children: chips)),
          ],
        );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        row('PIATTAFORMA', [
          for (final platform in platforms)
            _FilterChip(
              label: platform.name,
              count: platform.count,
              color: _platformColor(platform.id),
              selected: query.platforms.contains(platform.id),
              onTap: () => library.togglePlatform(platform.id),
            ),
        ]),
        const SizedBox(height: 8),
        row('FORMATO', [
          for (final format in formats)
            _FilterChip(
              label: format.name.isEmpty ? 'sconosciuto' : format.name,
              count: format.count,
              selected: query.formats.contains(format.id),
              onTap: () => library.toggleFormat(format.id),
            ),
          if (hidden > 0 || _allFormats)
            _FilterChip(
              label: _allFormats ? 'meno' : 'altri $hidden',
              onTap: () => setState(() => _allFormats = !_allFormats),
            ),
        ]),
      ],
    );
  }

  Widget _locationBar() {
    final path = library.path;
    final browsing = library.browsing;
    final folders = library.folders.length;
    final summary = browsing
        ? [
            if (folders > 0) '${_number(folders)} ${folders == 1 ? 'cartella' : 'cartelle'}',
            if (library.total > 0 || folders == 0) _tracksLabel(library.total),
          ].join(' · ')
        : '${_tracksLabel(library.total)} trovati';
    final empty = library.total == 0 && folders == 0;

    Widget crumb(String label, String? folder, {IconData? icon}) {
      final current = folder == null;
      final text = Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[Icon(icon, size: 16, color: current ? _text : _muted), const SizedBox(width: 6)],
          Text(label, style: _sora(13, weight: current ? 600 : 500, color: current ? _text : _muted)),
        ],
      );
      if (current) return Padding(padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 6), child: text);
      return InkWell(
        onTap: () => library.openFolder(folder),
        borderRadius: BorderRadius.circular(8),
        child: Padding(padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 6), child: text),
      );
    }

    return Row(
      children: [
        _SmallIconButton(
          icon: Icons.arrow_upward_rounded,
          tooltip: 'Cartella superiore',
          size: 32,
          onPressed: path.isEmpty ? null : () => library.openFolder(path.take(path.length - 1).join('/')),
        ),
        const SizedBox(width: 2),
        // La radice resta sempre in vista; il resto del percorso, se è lungo,
        // scorre e mostra la fine.
        crumb(_baseName(library.root), path.isEmpty ? null : '', icon: Icons.home_rounded),
        if (path.isNotEmpty) const Icon(Icons.chevron_right_rounded, size: 16, color: _faint),
        Expanded(
          // A sinistra il percorso tagliato sfuma; il margine tiene fuori
          // dalla sfumatura un percorso che ci sta.
          child: ShaderMask(
            blendMode: BlendMode.dstIn,
            shaderCallback: (bounds) => LinearGradient(
              colors: const [Colors.transparent, Colors.white],
              stops: [0, bounds.width > 0 ? math.min(1, 14 / bounds.width) : 0],
            ).createShader(bounds),
            child: LayoutBuilder(
              builder: (context, constraints) => SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                reverse: true,
                child: ConstrainedBox(
                  constraints: BoxConstraints(minWidth: constraints.maxWidth),
                  child: Row(
                    children: [
                      if (path.isNotEmpty) const SizedBox(width: 8),
                      for (var i = 0; i < path.length; i++) ...[
                        if (i > 0) const Icon(Icons.chevron_right_rounded, size: 16, color: _faint),
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
        Text(summary, style: _sora(13, color: _muted)),
        const SizedBox(width: 12),
        Tooltip(
          message: browsing
              ? 'Mette in coda i brani di questa cartella e delle sottocartelle'
              : 'Mette in coda tutti i risultati',
          child: _SmallButton(
            icon: Icons.add_rounded,
            label: 'Tutto in coda',
            onPressed: empty ? null : () => _enqueue(context, controller, library.allTracks()),
          ),
        ),
        if (controller.store != null)
          _MenuIconButton(
            icon: Icons.playlist_add_rounded,
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
            Text(message, style: _sora(15, color: _muted)),
            if (!library.browsing) ...[
              const SizedBox(height: 12),
              _SmallButton(icon: Icons.filter_alt_off_rounded, label: 'Togli ricerca e filtri', onPressed: _clearFilters),
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
    final query = library.query;

    return NotificationListener<ScrollNotification>(
      onNotification: _onScroll,
      child: ListView.builder(
        // Cambiando cartella o ricerca si riparte dall'inizio.
        key: ValueKey('${query.folder}|${query.text}|${query.platforms}|${query.formats}'),
        itemCount: rows.length,
        findChildIndexCallback: (key) {
          final index = rows.indexWhere((row) => row.$1 == key);
          return index < 0 ? null : index;
        },
        itemBuilder: (context, index) => KeyedSubtree(key: rows[index].$1, child: rows[index].$2()),
      ),
    );
  }

  Widget _folderRow(LibraryFacet folder) {
    final path = library.subfolderPath(folder.name);
    return _LibraryRowFrame(
      onTap: () => library.openSubfolder(folder.name),
      leading: Icon(
        _looksLikeArchive(folder.name) ? Icons.inventory_2_rounded : Icons.folder_rounded,
        size: 20,
        color: _textSoft,
      ),
      title: folder.name,
      subtitle: _tracksLabel(folder.count),
      trailing: [
        _SmallIconButton(
          icon: Icons.add_rounded,
          tooltip: 'Metti in coda tutta la cartella',
          onPressed: () => _enqueue(context, controller, library.allTracks(folder: path)),
        ),
        if (controller.store != null)
          _MenuIconButton(
            icon: Icons.playlist_add_rounded,
            tooltip: 'Aggiungi la cartella a una playlist',
            children: _playlistMenuItems(context, controller, () => library.allTracks(folder: path)),
          ),
        const Icon(Icons.chevron_right_rounded, size: 20, color: _faint),
      ],
    );
  }

  Widget _trackRow(LibraryItem item) {
    final current = controller.currentTrack;
    final playing = current != null && current.ref == item.ref;
    final expanded = _expanded.containsKey(item.ref);
    final details = [
      if (item.author.isNotEmpty) item.author,
      if (item.formatLabel.isNotEmpty) item.formatLabel,
      item.platformName,
      if (item.title.isNotEmpty) item.name,
      if (!library.browsing && item.folder.isNotEmpty) item.folder,
    ].join(' · ');

    return _LibraryRowFrame(
      selected: playing,
      onTap: () => controller.playNow(item.toTrack()),
      leading: playing
          ? const _PlayingBars()
          : Container(
              width: 10,
              height: 10,
              decoration: BoxDecoration(color: _platformColor(item.platform), borderRadius: BorderRadius.circular(3)),
            ),
      title: item.displayTitle,
      subtitle: details,
      trailing: [
        if (item.subsongs > 1)
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: InkWell(
              onTap: () => _toggle(item),
              borderRadius: BorderRadius.circular(999),
              child: Container(
                padding: const EdgeInsets.fromLTRB(10, 4, 6, 4),
                decoration: BoxDecoration(
                  color: expanded ? _border : _selected,
                  borderRadius: BorderRadius.circular(999),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text('${item.subsongs} sottobrani', style: _sora(12, color: _textSoft)),
                    Icon(expanded ? Icons.expand_less_rounded : Icons.expand_more_rounded, size: 16, color: _muted),
                  ],
                ),
              ),
            ),
          ),
        SizedBox(
          width: 44,
          child: Text(
            item.duration == null ? '' : formatTime(item.duration),
            textAlign: TextAlign.right,
            style: _mono(12),
          ),
        ),
        const SizedBox(width: 4),
        _SmallIconButton(
          icon: Icons.add_rounded,
          tooltip: item.subsongs > 1 ? 'Aggiungi alla coda il file intero' : 'Aggiungi alla coda',
          onPressed: () => _enqueue(context, controller, [item.toTrack()]),
        ),
        if (controller.store != null)
          _MenuIconButton(
            icon: Icons.playlist_add_rounded,
            tooltip: 'Aggiungi a una playlist',
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

    return Padding(
      padding: const EdgeInsets.only(left: 28),
      child: _LibraryRowFrame(
        compact: true,
        selected: playing,
        onTap: () => controller.playNow(entry),
        leading: playing
            ? const _PlayingBars()
            : const Icon(Icons.subdirectory_arrow_right_rounded, size: 16, color: _faint),
        title: 'Sottobrano ${index + 1}${index == item.startSubsong && item.startSubsong > 0 ? ' · iniziale' : ''}',
        trailing: [
          SizedBox(
            width: 44,
            child: Text(duration == null ? '' : formatTime(duration), textAlign: TextAlign.right, style: _mono(12)),
          ),
          const SizedBox(width: 4),
          _SmallIconButton(
            icon: Icons.add_rounded,
            tooltip: 'Aggiungi alla coda questo sottobrano',
            onPressed: () => _enqueue(context, controller, [entry]),
          ),
          if (controller.store != null)
            _MenuIconButton(
              icon: Icons.playlist_add_rounded,
              tooltip: 'Aggiungi a una playlist questo sottobrano',
              children: _playlistMenuItems(context, controller, () => [entry]),
            ),
        ],
      ),
    );
  }

  Widget _moreRow() => Padding(
        padding: const EdgeInsets.symmetric(vertical: 16),
        child: Center(
          child: Text(
            'Altri ${_number(library.total - library.items.length)} brani…',
            style: _sora(13, color: _muted),
          ),
        ),
      );
}

/// Una riga della libreria: icona, titolo con dettagli, comandi.
class _LibraryRowFrame extends StatelessWidget {
  const _LibraryRowFrame({
    required this.onTap,
    required this.leading,
    required this.title,
    this.subtitle,
    this.trailing = const [],
    this.selected = false,
    this.compact = false,
  });

  final VoidCallback onTap;
  final Widget leading;
  final String title;
  final String? subtitle;
  final List<Widget> trailing;
  final bool selected;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Material(
        color: selected ? _selected : Colors.transparent,
        borderRadius: BorderRadius.circular(12),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(12),
          child: Container(
            constraints: BoxConstraints(minHeight: compact ? 40 : 52),
            padding: const EdgeInsets.fromLTRB(10, 4, 4, 4),
            child: Row(
              children: [
                SizedBox(width: 24, child: Center(child: leading)),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        title,
                        style: _sora(compact ? 13 : 14, weight: selected ? 600 : 500, color: compact ? _textSoft : _text),
                        overflow: TextOverflow.ellipsis,
                      ),
                      if (subtitle != null && subtitle!.isNotEmpty)
                        Text(subtitle!, style: _sora(12, color: _muted), overflow: TextOverflow.ellipsis),
                    ],
                  ),
                ),
                ...trailing,
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _FilterChip extends StatelessWidget {
  const _FilterChip({required this.label, required this.onTap, this.count, this.color, this.selected = false});

  final String label;
  final int? count;
  final Color? color;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      selected: selected,
      button: true,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(999),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
          decoration: BoxDecoration(
            color: selected ? _border : _inset,
            borderRadius: BorderRadius.circular(999),
            border: Border.all(color: selected ? (color ?? _violet) : _insetBorder),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (color != null) ...[
                Container(
                  width: 8,
                  height: 8,
                  decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(2)),
                ),
                const SizedBox(width: 6),
              ],
              Text(label, style: _sora(12, weight: selected ? 600 : 400, color: selected ? _text : _textSoft)),
              if (count != null) ...[
                const SizedBox(width: 6),
                Text(_number(count!), style: _mono(11, color: selected ? _textSoft : _faint)),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

class _ScanProgress extends StatelessWidget {
  const _ScanProgress({required this.scan});

  final ScanStatus scan;

  @override
  Widget build(BuildContext context) {
    final total = scan.filesFound;
    final fraction = scan.walking || total == 0 ? null : (scan.filesChecked / total).clamp(0.0, 1.0);
    final text = !scan.running
        ? 'Fermo la lettura…'
        : scan.walking
            ? 'Conto i file della cartella… ${_number(total)}'
            : 'File controllati: ${_number(scan.filesChecked)} di ${_number(total)} · '
                'nuovi o cambiati: ${_number(scan.filesRead)} · brani trovati: ${_number(scan.tracksFound)}';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        ClipRRect(
          borderRadius: BorderRadius.circular(2),
          child: LinearProgressIndicator(value: fraction, minHeight: 3, backgroundColor: _track, color: _orange),
        ),
        const SizedBox(height: 6),
        Text(text, style: _sora(12, color: _muted)),
      ],
    );
  }
}
