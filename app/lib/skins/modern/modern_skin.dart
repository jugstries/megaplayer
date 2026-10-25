import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../../core/models.dart';
import '../../player/library_browser.dart';
import '../../player/megasound.dart';
import '../../player/player_controller.dart';
import '../knob_control.dart';

part 'modern_library.dart';
part 'modern_lists.dart';

// Skin moderna: scura, minimale, angoli arrotondati, accenti a gradiente.

const _background = Color(0xFF0C0B10);
const _surface = Color(0xFF15131B);
const _surfaceRaised = Color(0xFF1C1924);
const _selected = Color(0xFF221F2B);
const _border = Color(0xFF2A2633);
const _inset = Color(0xFF0F0E14);
const _insetBorder = Color(0xFF24212D);
const _text = Color(0xFFF2EFF7);
const _textSoft = Color(0xFFCFC9DA);
const _muted = Color(0xFFA39DB0);
const _faint = Color(0xFF8C8699);
const _track = Color(0xFF34303F);

const _orange = Color(0xFFFF8A3D);
const _pink = Color(0xFFEC4F8F);
const _violet = Color(0xFF8B5CF6);
const _accentGradient = LinearGradient(
  colors: [_orange, _pink, _violet],
  begin: Alignment.topLeft,
  end: Alignment.bottomRight,
);

TextStyle _sora(double size, {double weight = 400, Color color = _text, double? height, double? spacing}) =>
    TextStyle(
      fontFamily: 'Sora',
      fontSize: size,
      color: color,
      height: height,
      letterSpacing: spacing,
      fontVariations: [FontVariation('wght', weight)],
    );

TextStyle _mono(double size, {Color color = _muted, double weight = 400}) => TextStyle(
      fontFamily: 'JetBrainsMono',
      fontSize: size,
      color: color,
      fontVariations: [FontVariation('wght', weight)],
    );

TextStyle _label() => _sora(11, weight: 600, color: _faint, spacing: 0.9);

/// Le sezioni che si scelgono dalla barra laterale.
enum _Section { nowPlaying, library, playlist }

class ModernSkin extends StatefulWidget {
  const ModernSkin({super.key, required this.controller});

  final PlayerController controller;

  @override
  State<ModernSkin> createState() => _ModernSkinState();
}

class _ModernSkinState extends State<ModernSkin> {
  _Section _section = _Section.nowPlaying;
  int? _playlist;

  void _show(_Section section, {int? playlist}) => setState(() {
        _section = section;
        _playlist = playlist;
      });

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    // Una playlist appena cancellata non c'è più: si torna a quello che suona.
    final playlist = controller.playlists.where((p) => p.id == _playlist).firstOrNull;
    final section = _section == _Section.playlist && playlist == null ? _Section.nowPlaying : _section;

    return Theme(
      data: _theme,
      child: Scaffold(
        backgroundColor: _background,
        body: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _Header(controller: controller),
              if (controller.error != null) ...[
                const SizedBox(height: 12),
                _ErrorBanner(message: controller.error!),
              ],
              const SizedBox(height: 20),
              Expanded(
                child: LayoutBuilder(
                  builder: (context, constraints) {
                    final wide = constraints.maxWidth >= 1180;
                    return Row(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        SizedBox(
                          width: wide ? 232 : 216,
                          child: _Sidebar(
                            controller: controller,
                            section: section,
                            playlist: playlist?.id,
                            onShow: _show,
                          ),
                        ),
                        const SizedBox(width: 20),
                        Expanded(child: _content(section, playlist, wide)),
                        if (wide) ...[
                          const SizedBox(width: 20),
                          SizedBox(
                            width: 320,
                            child: _Aside(controller: controller, fill: true, details: section == _Section.nowPlaying),
                          ),
                        ],
                      ],
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  /// Quello che sta tra la barra laterale e la coda. Stretta, la finestra
  /// mette la coda sotto a quello che suona, e nella libreria non c'è.
  Widget _content(_Section section, PlaylistInfo? playlist, bool wide) {
    final controller = widget.controller;
    void showLibrary() => _show(_Section.library);
    if (section == _Section.nowPlaying) {
      if (wide) return _MainColumn(controller: controller, fill: true, onShowLibrary: showLibrary);
      return ListView(
        children: [
          _MainColumn(controller: controller, fill: false, onShowLibrary: showLibrary),
          const SizedBox(height: 20),
          _Aside(controller: controller, fill: false, details: true),
        ],
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _MiniPlayer(controller: controller, onOpen: () => _show(_Section.nowPlaying)),
        const SizedBox(height: 20),
        Expanded(
          child: section == _Section.library
              ? _LibraryPanel(controller: controller)
              : _PlaylistPanel(key: ValueKey(playlist!.id), controller: controller, playlist: playlist),
        ),
      ],
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Container(
          width: 44,
          height: 44,
          decoration: BoxDecoration(gradient: _accentGradient, borderRadius: BorderRadius.circular(14)),
          child: const Icon(Icons.graphic_eq_rounded, color: _background, size: 26),
        ),
        const SizedBox(width: 12),
        Text('megaplayer', style: _sora(20, weight: 700, spacing: -0.4)),
        const Spacer(),
        _PillButton(
          icon: Icons.folder_open_rounded,
          label: 'Apri file…',
          onPressed: controller.openFiles,
        ),
        const SizedBox(width: 12),
        _SkinSwitch(controller: controller),
      ],
    );
  }
}

class _ErrorBanner extends StatelessWidget {
  const _ErrorBanner({required this.message});

  final String message;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: const Color(0xFF2A1720),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: const Color(0xFF5A2A3A)),
      ),
      child: Text(message, style: _sora(13, color: const Color(0xFFFFC2D2))),
    );
  }
}

class _SkinSwitch extends StatelessWidget {
  const _SkinSwitch({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    Widget option(String label, Skin skin) {
      final active = controller.skin == skin;
      return Semantics(
        selected: active,
        button: true,
        child: InkWell(
          onTap: () => controller.setSkin(skin),
          borderRadius: BorderRadius.circular(12),
          child: Container(
            height: 44,
            padding: const EdgeInsets.symmetric(horizontal: 18),
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: active ? _border : Colors.transparent,
              borderRadius: BorderRadius.circular(12),
            ),
            child: Text(label, style: _sora(14, weight: active ? 600 : 500, color: active ? _text : _muted)),
          ),
        ),
      );
    }

    return Container(
      padding: const EdgeInsets.all(3),
      decoration: BoxDecoration(
        color: _surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: _border),
      ),
      child: Row(children: [option('Moderna', Skin.modern), const SizedBox(width: 4), option('Retrò', Skin.retro)]),
    );
  }
}

class _PillButton extends StatelessWidget {
  const _PillButton({required this.icon, required this.label, required this.onPressed});

  final IconData icon;
  final String label;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: _surface,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14), side: const BorderSide(color: _border)),
      child: InkWell(
        onTap: onPressed,
        borderRadius: BorderRadius.circular(14),
        child: Container(
          height: 44,
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 18, color: _textSoft),
              const SizedBox(width: 10),
              Text(label, style: _sora(14, weight: 500, color: _textSoft)),
            ],
          ),
        ),
      ),
    );
  }
}

/// Riquadro con fondo, bordo e angoli arrotondati.
class _Panel extends StatelessWidget {
  const _Panel({required this.child, this.padding = const EdgeInsets.all(20)});

  final Widget child;
  final EdgeInsets padding;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: padding,
      decoration: BoxDecoration(
        color: _surface,
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: _border),
      ),
      child: child,
    );
  }
}

class _Sidebar extends StatelessWidget {
  const _Sidebar({required this.controller, required this.section, required this.playlist, required this.onShow});

  final PlayerController controller;
  final _Section section;
  final int? playlist;
  final void Function(_Section section, {int? playlist}) onShow;

  @override
  Widget build(BuildContext context) {
    final library = controller.library;

    Future<void> newPlaylist() async {
      final name = await _askName(context, title: 'Nuova playlist', action: 'Crea');
      if (name == null) return;
      final id = controller.createPlaylist(name);
      if (id != null) onShow(_Section.playlist, playlist: id);
    }

    return _Panel(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(padding: const EdgeInsets.only(left: 12, bottom: 8), child: Text('ASCOLTA', style: _label())),
          _NavItem(
            icon: Icons.album_rounded,
            label: 'In riproduzione',
            active: section == _Section.nowPlaying,
            onTap: () => onShow(_Section.nowPlaying),
          ),
          if (library.available)
            _NavItem(
              icon: Icons.library_music_rounded,
              label: 'Libreria',
              active: section == _Section.library,
              onTap: () => onShow(_Section.library),
              trailing: library.scanning
                  ? const SizedBox(
                      width: 12,
                      height: 12,
                      child: CircularProgressIndicator(strokeWidth: 2, color: _orange),
                    )
                  : null,
            ),
          _NavItem(icon: Icons.folder_open_rounded, label: 'Apri file…', onTap: controller.openFiles),
          if (controller.store != null) ...[
            const SizedBox(height: 24),
            Row(
              children: [
                Expanded(
                  child: Padding(padding: const EdgeInsets.only(left: 12), child: Text('PLAYLIST', style: _label())),
                ),
                _SmallIconButton(icon: Icons.add_rounded, tooltip: 'Nuova playlist', size: 32, onPressed: newPlaylist),
              ],
            ),
            const SizedBox(height: 4),
            Expanded(
              child: controller.playlists.isEmpty
                  ? Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      child: Text(
                        'Nessuna playlist. Creane una col +, o salva la coda.',
                        style: _sora(13, color: _faint, height: 1.4),
                      ),
                    )
                  : ListView(
                      children: [
                        for (final item in controller.playlists)
                          _NavItem(
                            icon: Icons.queue_music_rounded,
                            label: item.name,
                            active: section == _Section.playlist && playlist == item.id,
                            onTap: () => onShow(_Section.playlist, playlist: item.id),
                            trailing: Text(_number(item.count), style: _mono(12, color: _faint)),
                          ),
                      ],
                    ),
            ),
          ] else
            const Spacer(),
          const SizedBox(height: 12),
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: _inset,
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: _insetBorder),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('BACKEND', style: _label()),
                const SizedBox(height: 10),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    for (final backend in controller.backends)
                      Tooltip(
                        message: backend.description,
                        child: Container(
                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                          decoration: BoxDecoration(color: _surfaceRaised, borderRadius: BorderRadius.circular(999)),
                          child: Text(backend.name, style: _mono(12, color: _textSoft)),
                        ),
                      ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _NavItem extends StatelessWidget {
  const _NavItem({required this.icon, required this.label, this.active = false, this.onTap, this.trailing});

  final IconData icon;
  final String label;
  final bool active;
  final VoidCallback? onTap;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      selected: active,
      button: true,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Container(
          height: 44,
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: BoxDecoration(
            color: active ? _selected : Colors.transparent,
            borderRadius: BorderRadius.circular(12),
          ),
          child: Row(
            children: [
              Icon(icon, size: 18, color: active ? _orange : _textSoft),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  label,
                  style: _sora(14, weight: active ? 600 : 400, color: active ? _text : _textSoft),
                  overflow: TextOverflow.ellipsis,
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

class _MainColumn extends StatelessWidget {
  const _MainColumn({required this.controller, required this.fill, required this.onShowLibrary});

  final PlayerController controller;
  final bool fill;
  final VoidCallback onShowLibrary;

  @override
  Widget build(BuildContext context) {
    final bottom = Row(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Expanded(child: _ScopePanel(controller: controller)),
        const SizedBox(width: 20),
        SizedBox(width: 340, child: _MegaSoundPanel(controller: controller)),
      ],
    );
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _NowPlaying(controller: controller, onShowLibrary: onShowLibrary),
        const SizedBox(height: 20),
        if (fill) Expanded(child: bottom) else SizedBox(height: 300, child: bottom),
      ],
    );
  }
}

class _NowPlaying extends StatelessWidget {
  const _NowPlaying({required this.controller, required this.onShowLibrary});

  final PlayerController controller;
  final VoidCallback onShowLibrary;

  @override
  Widget build(BuildContext context) {
    final track = controller.currentTrack;
    final details = controller.details;
    final status = controller.status;

    return ClipRRect(
      borderRadius: BorderRadius.circular(24),
      child: Container(
        decoration: BoxDecoration(
          color: _surface,
          borderRadius: BorderRadius.circular(24),
          border: Border.all(color: _border),
        ),
        child: Stack(
          children: [
            // Aloni di colore dietro al contenuto.
            Positioned(top: -200, right: -140, child: _Glow(color: _violet, size: 480, opacity: 0.3)),
            Positioned(bottom: -240, left: -160, child: _Glow(color: _orange, size: 460, opacity: 0.16)),
            Padding(
              padding: const EdgeInsets.all(28),
              child: track == null
                  ? _EmptyState(controller: controller, onShowLibrary: onShowLibrary)
                  : Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        _TrackHeading(track: track, details: details),
                        const SizedBox(height: 24),
                        _ProgressBar(status: status, onSeek: controller.seekTo),
                        const SizedBox(height: 20),
                        _Transport(controller: controller),
                      ],
                    ),
            ),
          ],
        ),
      ),
    );
  }
}

class _Glow extends StatelessWidget {
  const _Glow({required this.color, required this.size, required this.opacity});

  final Color color;
  final double size;
  final double opacity;

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
      child: Opacity(
        opacity: opacity,
        child: Container(
          width: size,
          height: size,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            gradient: RadialGradient(colors: [color, color.withValues(alpha: 0)], stops: const [0, 0.65]),
          ),
        ),
      ),
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({required this.controller, required this.onShowLibrary});

  final PlayerController controller;
  final VoidCallback onShowLibrary;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 260,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text('Nessun brano in riproduzione', style: _sora(28, weight: 700, spacing: -0.6)),
          const SizedBox(height: 10),
          Text(
            'Apri un modulo, un brano o un archivio (LHA, ZIP, 7z…)',
            style: _sora(15, color: _textSoft),
          ),
          const SizedBox(height: 24),
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              _PillButton(icon: Icons.folder_open_rounded, label: 'Apri file…', onPressed: controller.openFiles),
              if (controller.library.available) ...[
                const SizedBox(width: 12),
                _PillButton(icon: Icons.library_music_rounded, label: 'Sfoglia la libreria', onPressed: onShowLibrary),
              ],
            ],
          ),
        ],
      ),
    );
  }
}

class _TrackHeading extends StatelessWidget {
  const _TrackHeading({required this.track, required this.details});

  final Track track;
  final TrackDetails? details;

  @override
  Widget build(BuildContext context) {
    final info = details;
    final title = (info?.title.isNotEmpty ?? false) ? info!.title : track.fileName;
    final subtitle = [
      if (title != track.fileName) track.fileName,
      if (info != null && info.formatLabel.isNotEmpty) info.formatLabel,
      if (info != null && info.author.isNotEmpty) info.author,
    ].join(' · ');
    final monogram = (info?.formatLabel.isNotEmpty ?? false) ? info!.formatLabel : track.fileName;

    return Row(
      children: [
        Container(
          width: 128,
          height: 128,
          padding: const EdgeInsets.all(14),
          alignment: Alignment.bottomLeft,
          decoration: BoxDecoration(
            gradient: _accentGradient,
            borderRadius: BorderRadius.circular(22),
            boxShadow: [BoxShadow(color: _violet.withValues(alpha: 0.45), blurRadius: 40, offset: const Offset(0, 20), spreadRadius: -20)],
          ),
          child: Text(
            _initials(monogram),
            style: _sora(34, weight: 700, color: _background, spacing: -1.4),
          ),
        ),
        const SizedBox(width: 24),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'IN RIPRODUZIONE${track.location.isEmpty ? '' : ' · ${track.location}'}',
                style: _sora(12, weight: 600, color: _muted, spacing: 1),
                overflow: TextOverflow.ellipsis,
              ),
              const SizedBox(height: 8),
              Text(
                title,
                style: _sora(44, weight: 700, height: 1.05, spacing: -1.3),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
              if (subtitle.isNotEmpty) ...[
                const SizedBox(height: 8),
                Text(subtitle, style: _sora(16, color: _textSoft), overflow: TextOverflow.ellipsis),
              ],
              const SizedBox(height: 14),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  if (info != null) _Chip(info.backend.toUpperCase()),
                  if (info != null && info.player.isNotEmpty) _Chip('Replayer ${info.player}'),
                  if (info != null && info.subsongCount > 1) _Chip('${info.subsongCount} sottobrani'),
                  if (info != null && info.packers.isNotEmpty) _Chip(info.packers.join(' › ')),
                  const _Chip('48 kHz'),
                ],
              ),
            ],
          ),
        ),
      ],
    );
  }

  static String _initials(String text) {
    final letters = text.replaceAll(RegExp(r'[^A-Za-z0-9 ]'), ' ').trim().split(RegExp(r'\s+'));
    if (letters.length >= 2) return (letters[0][0] + letters[1][0]).toUpperCase();
    final word = letters.first;
    return word.substring(0, math.min(2, word.length)).toUpperCase();
  }
}

class _Chip extends StatelessWidget {
  const _Chip(this.label);

  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      decoration: BoxDecoration(color: _selected, borderRadius: BorderRadius.circular(999)),
      child: Text(label, style: _sora(12, color: const Color(0xFFE4DFEC))),
    );
  }
}

/// Barra di avanzamento. Con durata sconosciuta (succede spesso con UADE)
/// mostra solo che il brano sta andando, e il seek passa dai pulsanti ±10 s.
class _ProgressBar extends StatelessWidget {
  const _ProgressBar({required this.status, required this.onSeek});

  final CoreStatus status;
  final ValueChanged<double> onSeek;

  @override
  Widget build(BuildContext context) {
    final duration = status.duration;
    final fraction = duration == null || duration <= 0 ? null : (status.position / duration).clamp(0.0, 1.0);

    void seekAt(double dx, double width) {
      if (duration != null) onSeek((dx / width).clamp(0.0, 1.0) * duration);
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        LayoutBuilder(
          builder: (context, constraints) => GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTapDown: (d) => seekAt(d.localPosition.dx, constraints.maxWidth),
            onHorizontalDragUpdate: (d) => seekAt(d.localPosition.dx, constraints.maxWidth),
            child: SizedBox(
              height: 28,
              child: CustomPaint(
                painter: _ProgressPainter(fraction: fraction, position: status.position),
              ),
            ),
          ),
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Text(formatTime(status.position), style: _mono(13, color: _text)),
            const Spacer(),
            Text(formatTime(duration), style: _mono(13)),
          ],
        ),
      ],
    );
  }
}

class _ProgressPainter extends CustomPainter {
  _ProgressPainter({required this.fraction, required this.position});

  final double? fraction;
  final double position;

  @override
  void paint(Canvas canvas, Size size) {
    final y = size.height / 2;
    final track = RRect.fromLTRBR(0, y - 3, size.width, y + 3, const Radius.circular(3));
    canvas.drawRRect(track, Paint()..color = _track);

    final gradient = Paint()
      ..shader = _accentGradient.createShader(Rect.fromLTWH(0, 0, size.width, size.height));
    final value = fraction;
    if (value == null) {
      // Durata sconosciuta: un tratto colorato che scorre.
      const segment = 0.18;
      final start = (position / 8) % (1 + segment) - segment;
      final left = (start.clamp(0.0, 1.0)) * size.width;
      final right = ((start + segment).clamp(0.0, 1.0)) * size.width;
      if (right > left) {
        canvas.drawRRect(RRect.fromLTRBR(left, y - 3, right, y + 3, const Radius.circular(3)), gradient);
      }
      return;
    }
    final x = value * size.width;
    canvas.drawRRect(RRect.fromLTRBR(0, y - 3, x, y + 3, const Radius.circular(3)), gradient);
    canvas.drawCircle(Offset(x, y), 8, Paint()..color = _text);
  }

  @override
  bool shouldRepaint(_ProgressPainter old) => old.fraction != fraction || old.position != position;
}

class _Transport extends StatelessWidget {
  const _Transport({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final status = controller.status;
    final playing = status.isPlaying;

    return Wrap(
      alignment: WrapAlignment.spaceBetween,
      crossAxisAlignment: WrapCrossAlignment.center,
      spacing: 16,
      runSpacing: 16,
      children: [
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _RoundIconButton(icon: Icons.skip_previous_rounded, tooltip: 'Brano precedente', onPressed: controller.previous),
            const SizedBox(width: 10),
            _RoundIconButton(icon: Icons.replay_10_rounded, tooltip: 'Indietro di 10 secondi', onPressed: () => controller.seekBy(-10)),
            const SizedBox(width: 10),
            Tooltip(
              message: playing ? 'Pausa' : 'Riproduci',
              child: InkWell(
                onTap: controller.togglePause,
                customBorder: const CircleBorder(),
                child: Container(
                  width: 68,
                  height: 68,
                  decoration: BoxDecoration(
                    gradient: _accentGradient,
                    shape: BoxShape.circle,
                    boxShadow: [BoxShadow(color: _pink.withValues(alpha: 0.5), blurRadius: 34, offset: const Offset(0, 14), spreadRadius: -12)],
                  ),
                  child: Icon(playing ? Icons.pause_rounded : Icons.play_arrow_rounded, size: 34, color: _background),
                ),
              ),
            ),
            const SizedBox(width: 10),
            _RoundIconButton(icon: Icons.forward_10_rounded, tooltip: 'Avanti di 10 secondi', onPressed: () => controller.seekBy(10)),
            const SizedBox(width: 10),
            _RoundIconButton(icon: Icons.skip_next_rounded, tooltip: 'Brano successivo', onPressed: controller.next),
          ],
        ),
        Container(
          padding: const EdgeInsets.all(3),
          decoration: BoxDecoration(
            color: _surfaceRaised,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: _border),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              _FlatIconButton(
                icon: Icons.chevron_left_rounded,
                tooltip: 'Sottobrano precedente',
                onPressed: () => controller.selectSubsong(status.subsong - 1),
              ),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 6),
                child: Text.rich(
                  TextSpan(
                    style: _sora(13, color: _textSoft),
                    children: [
                      const TextSpan(text: 'Sottobrano '),
                      TextSpan(text: '${status.subsong + 1}', style: _sora(13, weight: 600)),
                      TextSpan(text: ' di ${math.max(1, status.subsongCount)}'),
                    ],
                  ),
                ),
              ),
              _FlatIconButton(
                icon: Icons.chevron_right_rounded,
                tooltip: 'Sottobrano successivo',
                onPressed: () => controller.selectSubsong(status.subsong + 1),
              ),
              if (controller.store != null)
                _MenuIconButton(
                  icon: Icons.playlist_add_rounded,
                  tooltip: status.subsongCount > 1
                      ? 'Aggiungi a una playlist questo sottobrano'
                      : 'Aggiungi a una playlist',
                  size: 44,
                  children: _playlistMenuItems(context, controller, () => [controller.playingEntry!]),
                ),
            ],
          ),
        ),
        SizedBox(
          width: 220,
          child: Row(
            children: [
              const Icon(Icons.volume_up_rounded, size: 20, color: _muted),
              Expanded(
                child: SliderTheme(
                  data: SliderThemeData(
                    activeTrackColor: _orange,
                    inactiveTrackColor: _track,
                    thumbColor: _text,
                    overlayColor: _orange.withValues(alpha: 0.12),
                    trackHeight: 4,
                  ),
                  child: Slider(value: controller.volume, onChanged: controller.setVolume, semanticFormatterCallback: (v) => 'Volume ${(v * 100).round()}%'),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _RoundIconButton extends StatelessWidget {
  const _RoundIconButton({required this.icon, required this.tooltip, required this.onPressed});

  final IconData icon;
  final String tooltip;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: Material(
        color: _selected,
        borderRadius: BorderRadius.circular(14),
        child: InkWell(
          onTap: onPressed,
          borderRadius: BorderRadius.circular(14),
          child: SizedBox(width: 48, height: 48, child: Icon(icon, size: 24, color: _text)),
        ),
      ),
    );
  }
}

class _FlatIconButton extends StatelessWidget {
  const _FlatIconButton({required this.icon, required this.tooltip, required this.onPressed});

  final IconData icon;
  final String tooltip;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        onTap: onPressed,
        borderRadius: BorderRadius.circular(12),
        child: SizedBox(width: 44, height: 44, child: Icon(icon, size: 22, color: _muted)),
      ),
    );
  }
}

class _ScopePanel extends StatelessWidget {
  const _ScopePanel({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return _Panel(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Text('Oscilloscopio', style: _sora(18, weight: 600)),
              const Spacer(),
              Text('uscita stereo', style: _sora(13, color: _muted)),
            ],
          ),
          const SizedBox(height: 16),
          Expanded(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Expanded(child: _ScopeTile(label: 'Sinistra', samples: controller.scope, channel: 0)),
                const SizedBox(width: 12),
                Expanded(child: _ScopeTile(label: 'Destra', samples: controller.scope, channel: 1)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _ScopeTile extends StatelessWidget {
  const _ScopeTile({required this.label, required this.samples, required this.channel});

  final String label;
  final Float32List samples;
  final int channel;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: _inset,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: _insetBorder),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: _sora(13, weight: 600)),
          const SizedBox(height: 8),
          Expanded(
            child: CustomPaint(
              size: Size.infinite,
              painter: _ScopePainter(samples: samples, channel: channel),
            ),
          ),
        ],
      ),
    );
  }
}

class _ScopePainter extends CustomPainter {
  _ScopePainter({required this.samples, required this.channel});

  final Float32List samples;
  final int channel;

  @override
  void paint(Canvas canvas, Size size) {
    final frames = samples.length ~/ 2;
    final mid = size.height / 2;
    final path = Path();
    if (frames < 2) {
      path
        ..moveTo(0, mid)
        ..lineTo(size.width, mid);
    } else {
      for (var i = 0; i < frames; i++) {
        final x = i / (frames - 1) * size.width;
        final y = mid - samples[i * 2 + channel].clamp(-1.0, 1.0) * (mid - 4);
        i == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
      }
    }
    final bounds = Offset.zero & size;
    canvas.drawPath(
      path,
      Paint()
        ..shader = LinearGradient(colors: [_orange.withValues(alpha: 0.3), _violet.withValues(alpha: 0.3)])
            .createShader(bounds)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 7
        ..strokeJoin = StrokeJoin.round
        ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 4),
    );
    canvas.drawPath(
      path,
      Paint()
        ..shader = const LinearGradient(colors: [_orange, _violet]).createShader(bounds)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(_ScopePainter old) => !identical(old.samples, samples);
}

/// La manopola MegaSound: gli effetti si accendono a mano a mano che gira.
class _MegaSoundPanel extends StatelessWidget {
  const _MegaSoundPanel({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final amount = controller.megaSound;
    final bypassed = controller.megaSoundBypassed;
    final knob = KnobControl(
      value: amount,
      onChanged: controller.setMegaSound,
      label: 'Manopola MegaSound',
      builder: (context, focused) => CustomPaint(
        painter: _KnobPainter(amount: amount, bypassed: bypassed, focused: focused),
        // La percentuale la legge già lo slider.
        child: ExcludeSemantics(
          child: Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  '${(amount * 100).round()}%',
                  style: _sora(28, weight: 700, color: bypassed ? _muted : _text, height: 1.1),
                ),
                Text(
                  bypassed ? 'originale' : megaSoundCharacter(amount),
                  style: _sora(12, weight: 500, color: _muted),
                ),
              ],
            ),
          ),
        ),
      ),
    );

    return _Panel(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Text('MegaSound', style: _sora(18, weight: 600)),
              const Spacer(),
              _CompareSwitch(bypassed: bypassed, onToggle: controller.toggleMegaSoundCompare),
            ],
          ),
          const SizedBox(height: 16),
          Expanded(
            child: Row(
              children: [
                Expanded(child: Center(child: AspectRatio(aspectRatio: 1, child: knob))),
                const SizedBox(width: 16),
                Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    for (var i = 0; i < megaSoundStages.length; i++)
                      _StageRow(
                        name: megaSoundStages[i].name,
                        color: _stageColor(i / (megaSoundStages.length - 1)),
                        level: bypassed ? 0 : megaSoundStages[i].level(amount),
                      ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Il colore del gradiente arancio, rosa, viola alla posizione [t] (0..1).
Color _stageColor(double t) =>
    t < 0.5 ? Color.lerp(_orange, _pink, t * 2)! : Color.lerp(_pink, _violet, (t - 0.5) * 2)!;

/// Confronto A/B: l'originale contro il suono con la manopola.
class _CompareSwitch extends StatelessWidget {
  const _CompareSwitch({required this.bypassed, required this.onToggle});

  final bool bypassed;
  final VoidCallback onToggle;

  @override
  Widget build(BuildContext context) {
    Widget option(String label, String tooltip, bool active) {
      return Semantics(
        selected: active,
        button: true,
        child: Tooltip(
          message: tooltip,
          child: InkWell(
            onTap: active ? null : onToggle,
            borderRadius: BorderRadius.circular(9),
            child: Container(
              height: 28,
              padding: const EdgeInsets.symmetric(horizontal: 12),
              alignment: Alignment.center,
              decoration: BoxDecoration(
                color: active ? _border : Colors.transparent,
                borderRadius: BorderRadius.circular(9),
              ),
              child: Text(label, style: _sora(12, weight: active ? 600 : 500, color: active ? _text : _muted)),
            ),
          ),
        ),
      );
    }

    return Container(
      padding: const EdgeInsets.all(2),
      decoration: BoxDecoration(
        color: _inset,
        borderRadius: BorderRadius.circular(11),
        border: Border.all(color: _insetBorder),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          option('A', 'Ascolta l\'originale', bypassed),
          const SizedBox(width: 2),
          option('B', 'Ascolta con MegaSound', !bypassed),
        ],
      ),
    );
  }
}

class _StageRow extends StatelessWidget {
  const _StageRow({required this.name, required this.color, required this.level});

  final String name;
  final Color color;
  final double level;

  @override
  Widget build(BuildContext context) {
    final lit = level > 0;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Row(
        children: [
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: lit ? Color.lerp(_track, color, 0.35 + 0.65 * level) : _track,
              boxShadow: [if (lit) BoxShadow(color: color.withValues(alpha: 0.6 * level), blurRadius: 8)],
            ),
          ),
          const SizedBox(width: 10),
          Text(name, style: _sora(13, weight: 500, color: lit ? Color.lerp(_muted, _text, level)! : _faint)),
        ],
      ),
    );
  }
}

class _KnobPainter extends CustomPainter {
  _KnobPainter({required this.amount, required this.bypassed, required this.focused});

  final double amount;
  final bool bypassed;
  final bool focused;

  // L'arco va da sette e mezza a quattro e mezza, come sulle manopole vere.
  static const _start = math.pi * 3 / 4;
  static const _sweep = math.pi * 3 / 2;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = size.shortestSide / 2;
    const stroke = 8.0;
    final arcRect = Rect.fromCircle(center: center, radius: radius - stroke);

    canvas.drawArc(
      arcRect,
      _start,
      _sweep,
      false,
      Paint()
        ..color = _track
        ..style = PaintingStyle.stroke
        ..strokeWidth = stroke
        ..strokeCap = StrokeCap.round,
    );

    if (amount > 0) {
      final shader = const SweepGradient(
        colors: [_orange, _pink, _violet],
        endAngle: _sweep,
        transform: GradientRotation(_start),
      ).createShader(arcRect);
      // Con uno shader il colore conta solo per l'opacità.
      final alpha = bypassed ? 0.3 : 1.0;
      canvas.drawArc(
        arcRect,
        _start,
        _sweep * amount,
        false,
        Paint()
          ..shader = shader
          ..color = Colors.black.withValues(alpha: alpha * 0.5)
          ..style = PaintingStyle.stroke
          ..strokeWidth = stroke + 6
          ..strokeCap = StrokeCap.round
          ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 8),
      );
      canvas.drawArc(
        arcRect,
        _start,
        _sweep * amount,
        false,
        Paint()
          ..shader = shader
          ..color = Colors.black.withValues(alpha: alpha)
          ..style = PaintingStyle.stroke
          ..strokeWidth = stroke
          ..strokeCap = StrokeCap.round,
      );
    }

    final bodyRadius = radius - stroke * 2 - 10;
    final body = Rect.fromCircle(center: center, radius: bodyRadius);
    canvas.drawCircle(
      center + const Offset(0, 6),
      bodyRadius,
      Paint()
        ..color = Colors.black.withValues(alpha: 0.45)
        ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 10),
    );
    canvas.drawCircle(
      center,
      bodyRadius,
      Paint()
        ..shader = const LinearGradient(
          colors: [_surfaceRaised, _inset],
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
        ).createShader(body),
    );
    canvas.drawCircle(
      center,
      bodyRadius,
      Paint()
        ..color = focused ? _violet : _border
        ..style = PaintingStyle.stroke
        ..strokeWidth = focused ? 2 : 1,
    );

    final angle = _start + _sweep * amount;
    final pointer = center + Offset(math.cos(angle), math.sin(angle)) * (bodyRadius - 12);
    canvas.drawCircle(pointer, 4, Paint()..color = bypassed ? _faint : _text);
  }

  @override
  bool shouldRepaint(_KnobPainter old) =>
      old.amount != amount || old.bypassed != bypassed || old.focused != focused;
}

class _Aside extends StatelessWidget {
  const _Aside({required this.controller, required this.fill, required this.details});

  final PlayerController controller;
  final bool fill;
  final bool details;

  @override
  Widget build(BuildContext context) {
    final queue = _QueuePanel(controller: controller, scrollable: fill);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (fill) Expanded(child: queue) else queue,
        if (details) ...[
          const SizedBox(height: 20),
          _DetailsPanel(controller: controller),
        ],
      ],
    );
  }
}

class _PlayingBars extends StatelessWidget {
  const _PlayingBars();

  @override
  Widget build(BuildContext context) {
    Widget bar(double height, Color color) => Container(
          width: 4,
          height: height,
          margin: const EdgeInsets.only(right: 3),
          decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(2)),
        );
    return SizedBox(
      height: 16,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [bar(9, _orange), bar(16, _pink), bar(12, _violet)],
      ),
    );
  }
}

class _DetailsPanel extends StatelessWidget {
  const _DetailsPanel({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final info = controller.details;
    final rows = info == null
        ? const <(String, String)>[]
        : [
            ('Formato', info.formatLabel.isEmpty ? '—' : info.formatLabel),
            ('Replayer', info.player.isEmpty ? '—' : info.player),
            ('Backend', info.backend),
            ('File', info.file),
            ('Packer', info.packers.isEmpty ? 'nessuno' : info.packers.join(' › ')),
            ('Sottobrani', '${info.subsongCount}'),
            ('Durata', info.duration == null ? 'sconosciuta' : formatTime(info.duration)),
          ];

    return _Panel(
      padding: const EdgeInsets.fromLTRB(24, 20, 24, 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('Dettagli', style: _sora(18, weight: 600)),
          const SizedBox(height: 14),
          if (rows.isEmpty) Text('Nessun brano aperto', style: _sora(13, color: _muted)),
          for (final (label, value) in rows)
            Padding(
              padding: const EdgeInsets.only(bottom: 10),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  SizedBox(width: 92, child: Text(label, style: _sora(13, color: _muted))),
                  Expanded(
                    child: Text(
                      value,
                      style: label == 'File' ? _mono(12, color: _text) : _sora(13),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}
