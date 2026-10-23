import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../../core/models.dart';
import '../../player/player_controller.dart';

// Skin retrò: il Mac a 1 bit degli anni '80, con pochi accenti pastello.

const _ink = Color(0xFF111111);
const _paper = Color(0xFFFFFFFF);
const _desk = Color(0xFF8C99AA);
const _lightGray = Color(0xFFC7CCD3);
const _midGray = Color(0xFF9AA3AF);
const _teal = Color(0xFF7FD1C1);
const _pink = Color(0xFFF3A6C8);
const _lilac = Color(0xFFB8A9E8);

TextStyle _pixel(double size, {double weight = 400, Color color = _ink, double? height}) => TextStyle(
      fontFamily: 'PixelifySans',
      fontSize: size,
      color: color,
      height: height,
      fontVariations: [FontVariation('wght', weight)],
    );

TextStyle _silk(double size, {Color color = _ink}) =>
    TextStyle(fontFamily: 'Silkscreen', fontSize: size, color: color, letterSpacing: 0.6);

TextStyle _vt(double size, {Color color = _ink, double? height}) =>
    TextStyle(fontFamily: 'VT323', fontSize: size, color: color, height: height);

class RetroSkin extends StatelessWidget {
  const RetroSkin({super.key, required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: _desk,
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _MenuBar(controller: controller),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: LayoutBuilder(
                builder: (context, constraints) {
                  if (constraints.maxWidth >= 1100) {
                    return Row(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Expanded(
                          flex: 5,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [
                              _NowPlayingWindow(controller: controller),
                              const SizedBox(height: 24),
                              Expanded(child: _PlaylistWindow(controller: controller)),
                            ],
                          ),
                        ),
                        const SizedBox(width: 24),
                        Expanded(
                          flex: 3,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [
                              Expanded(child: _ScopeWindow(controller: controller)),
                              const SizedBox(height: 24),
                              _InfoWindow(controller: controller),
                            ],
                          ),
                        ),
                        const SizedBox(width: 24),
                        SizedBox(
                          width: 260,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [
                              _OutputWindow(controller: controller),
                              const SizedBox(height: 24),
                              Expanded(child: _HelloWindow(controller: controller)),
                            ],
                          ),
                        ),
                      ],
                    );
                  }
                  return ListView(
                    children: [
                      _NowPlayingWindow(controller: controller),
                      const SizedBox(height: 24),
                      SizedBox(height: 320, child: _PlaylistWindow(controller: controller)),
                      const SizedBox(height: 24),
                      SizedBox(height: 260, child: _ScopeWindow(controller: controller)),
                      const SizedBox(height: 24),
                      _InfoWindow(controller: controller),
                      const SizedBox(height: 24),
                      _OutputWindow(controller: controller),
                    ],
                  );
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _MenuBar extends StatelessWidget {
  const _MenuBar({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final now = TimeOfDay.now();
    final backends = controller.backends.map((b) => b.name.toUpperCase()).join(' · ');
    return Container(
      height: 46,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: const BoxDecoration(
        color: _paper,
        border: Border(bottom: BorderSide(color: _ink, width: 2)),
      ),
      child: Row(
        children: [
          const _PixelIcon(_PixelIcon.note, size: 20),
          const SizedBox(width: 6),
          Text('megaplayer', style: _pixel(19, weight: 700)),
          const SizedBox(width: 10),
          _MenuItem(label: 'Apri…', onTap: controller.openFiles),
          _MenuItem(label: 'Skin moderna', onTap: () => controller.setSkin(Skin.modern)),
          const Spacer(),
          Text('$backends · 48 KHZ', style: _silk(12)),
          const SizedBox(width: 14),
          Container(width: 2, height: 24, color: _ink),
          const SizedBox(width: 14),
          Text(
            '${now.hour.toString().padLeft(2, '0')}:${now.minute.toString().padLeft(2, '0')}',
            style: _vt(26),
          ),
        ],
      ),
    );
  }
}

class _MenuItem extends StatefulWidget {
  const _MenuItem({required this.label, required this.onTap});

  final String label;
  final VoidCallback onTap;

  @override
  State<_MenuItem> createState() => _MenuItemState();
}

class _MenuItemState extends State<_MenuItem> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        onEnter: (_) => setState(() => _hover = true),
        onExit: (_) => setState(() => _hover = false),
        child: GestureDetector(
          onTap: widget.onTap,
          child: Container(
            height: 44,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            alignment: Alignment.center,
            color: _hover ? _ink : Colors.transparent,
            child: Text(widget.label, style: _pixel(17, color: _hover ? _paper : _ink)),
          ),
        ),
      ),
    );
  }
}

/// Finestra con la barra del titolo a righe e la casella di chiusura.
class _Window extends StatelessWidget {
  const _Window({required this.title, required this.child, this.expand = false});

  final String title;
  final Widget child;
  final bool expand;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: const BoxDecoration(
        color: _paper,
        border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
        boxShadow: [BoxShadow(color: _ink, offset: Offset(4, 4))],
      ),
      child: Column(
        mainAxisSize: expand ? MainAxisSize.max : MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _TitleBar(title: title),
          if (expand) Expanded(child: child) else child,
        ],
      ),
    );
  }
}

class _TitleBar extends StatelessWidget {
  const _TitleBar({required this.title});

  final String title;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 32,
      padding: const EdgeInsets.all(6),
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
      child: CustomPaint(
        painter: _StripesPainter(),
        child: Stack(
          alignment: Alignment.center,
          children: [
            Positioned(
              left: 10,
              top: 0,
              child: Container(
                width: 22,
                height: 18,
                color: _paper,
                padding: const EdgeInsets.symmetric(horizontal: 3),
                child: Container(
                  decoration: const BoxDecoration(border: Border.fromBorderSide(BorderSide(color: _ink, width: 2))),
                ),
              ),
            ),
            Container(
              color: _paper,
              padding: const EdgeInsets.symmetric(horizontal: 12),
              child: Text(title, style: _pixel(17, weight: 600, height: 1.15)),
            ),
          ],
        ),
      ),
    );
  }
}

class _StripesPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = _ink
      ..isAntiAlias = false;
    for (var y = 0.0; y < size.height; y += 4) {
      canvas.drawRect(Rect.fromLTWH(0, y, size.width, 2), paint);
    }
  }

  @override
  bool shouldRepaint(_StripesPainter old) => false;
}

/// Icone disegnate a pixel su una griglia.
class _PixelIcon extends StatelessWidget {
  const _PixelIcon(this.pixels, {this.size = 24, this.color = _ink, this.grid = 16});

  // Rettangoli [x, y, larghezza, altezza] sulla griglia.
  static const note = [
    [9, 2, 2, 9], [11, 2, 3, 2], [13, 4, 1, 2], [6, 10, 4, 4], [5, 11, 1, 2],
  ];
  static const previous = [
    [3, 3, 2, 10], [12, 3, 2, 10], [10, 4, 2, 8], [8, 5, 2, 6], [6, 6, 2, 4],
  ];
  static const play = [
    [4, 2, 2, 12], [6, 3, 2, 10], [8, 4, 2, 8], [10, 5, 2, 6], [12, 6, 2, 4],
  ];
  static const pause = [
    [4, 3, 3, 10], [9, 3, 3, 10],
  ];
  static const next = [
    [11, 3, 2, 10], [2, 3, 2, 10], [4, 4, 2, 8], [6, 5, 2, 6], [8, 6, 2, 4],
  ];
  static const up = [
    [3, 1, 2, 1], [2, 2, 4, 1], [1, 3, 6, 1], [3, 4, 2, 3],
  ];
  static const down = [
    [3, 1, 2, 3], [1, 4, 6, 1], [2, 5, 4, 1], [3, 6, 2, 1],
  ];

  final List<List<int>> pixels;
  final double size;
  final Color color;
  final int grid;

  @override
  Widget build(BuildContext context) {
    return CustomPaint(size: Size.square(size), painter: _PixelIconPainter(pixels, color, grid));
  }
}

class _PixelIconPainter extends CustomPainter {
  _PixelIconPainter(this.pixels, this.color, this.grid);

  final List<List<int>> pixels;
  final Color color;
  final int grid;

  @override
  void paint(Canvas canvas, Size size) {
    final unit = size.width / grid;
    final paint = Paint()
      ..color = color
      ..isAntiAlias = false;
    for (final r in pixels) {
      canvas.drawRect(Rect.fromLTWH(r[0] * unit, r[1] * unit, r[2] * unit, r[3] * unit), paint);
    }
  }

  @override
  bool shouldRepaint(_PixelIconPainter old) => old.color != color || old.pixels != pixels;
}

class _RetroButton extends StatelessWidget {
  const _RetroButton({
    required this.label,
    required this.onPressed,
    required this.child,
    this.inverted = false,
    this.width = 56,
  });

  final String label;
  final VoidCallback onPressed;
  final Widget Function(Color color) child;
  final bool inverted;
  final double width;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      label: label,
      child: Tooltip(
        message: label,
        child: MouseRegion(
          cursor: SystemMouseCursors.click,
          child: GestureDetector(
            onTap: onPressed,
            child: Container(
              width: width,
              height: 44,
              alignment: Alignment.center,
              color: inverted ? _ink : _paper,
              child: child(inverted ? _paper : _ink),
            ),
          ),
        ),
      ),
    );
  }
}

/// Bordo nero di 2 px tra elementi accostati.
class _Divider extends StatelessWidget {
  const _Divider();

  @override
  Widget build(BuildContext context) => Container(width: 2, color: _ink);
}

class _NowPlayingWindow extends StatelessWidget {
  const _NowPlayingWindow({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final status = controller.status;
    final track = controller.currentTrack;
    final details = controller.details;
    final title = track == null
        ? 'MEGAPLAYER'
        : ((details?.title.isNotEmpty ?? false) ? details!.title : track.fileName).toUpperCase();
    final caption = track == null
        ? 'NESSUN BRANO'
        : [
            track.fileName,
            if (details != null && details.formatLabel.isNotEmpty) details.formatLabel,
            if (details != null) details.backend,
          ].join(' · ').toUpperCase();
    final position = controller.currentIndex == null
        ? 'PRONTO'
        : 'IN RIPRODUZIONE · ${controller.currentIndex! + 1}/${controller.tracks.length}';

    return _Window(
      title: 'In riproduzione',
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Container(
              color: _ink,
              padding: const EdgeInsets.fromLTRB(18, 14, 18, 14),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(position, style: _silk(12, color: _teal)),
                        const SizedBox(height: 6),
                        Text(
                          title,
                          style: _pixel(38, weight: 700, color: _paper, height: 1),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                        ),
                        const SizedBox(height: 6),
                        Text(caption, style: _silk(12, color: _paper), overflow: TextOverflow.ellipsis),
                      ],
                    ),
                  ),
                  const SizedBox(width: 16),
                  Text(formatTime(status.position, padMinutes: true), style: _vt(60, color: _paper, height: 0.8)),
                  const SizedBox(width: 8),
                  Text(
                    '/ ${formatTime(status.duration, padMinutes: true)}',
                    style: _vt(26, color: _midGray, height: 1),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 16),
            _ProgressStrip(status: status, onSeek: controller.seekTo),
            const SizedBox(height: 16),
            Wrap(
              spacing: 16,
              runSpacing: 12,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                Container(
                  decoration: const BoxDecoration(
                    border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                    boxShadow: [BoxShadow(color: _ink, offset: Offset(3, 3))],
                  ),
                  child: IntrinsicHeight(
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        _RetroButton(
                          label: 'Brano precedente',
                          onPressed: controller.previous,
                          child: (c) => _PixelIcon(_PixelIcon.previous, color: c),
                        ),
                        const _Divider(),
                        _RetroButton(
                          label: 'Indietro di 10 secondi',
                          onPressed: () => controller.seekBy(-10),
                          child: (c) => Text('-10', style: _vt(24, color: c)),
                        ),
                        const _Divider(),
                        _RetroButton(
                          label: status.isPlaying ? 'Pausa' : 'Riproduci',
                          onPressed: controller.togglePause,
                          inverted: status.isPlaying,
                          child: (c) => _PixelIcon(status.isPlaying ? _PixelIcon.pause : _PixelIcon.play, color: c),
                        ),
                        const _Divider(),
                        _RetroButton(
                          label: 'Avanti di 10 secondi',
                          onPressed: () => controller.seekBy(10),
                          child: (c) => Text('+10', style: _vt(24, color: c)),
                        ),
                        const _Divider(),
                        _RetroButton(
                          label: 'Brano successivo',
                          onPressed: controller.next,
                          child: (c) => _PixelIcon(_PixelIcon.next, color: c),
                        ),
                      ],
                    ),
                  ),
                ),
                Container(
                  decoration: const BoxDecoration(border: Border.fromBorderSide(BorderSide(color: _ink, width: 2))),
                  child: IntrinsicHeight(
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 10),
                          child: Center(child: Text('SOTTOBRANO', style: _silk(12))),
                        ),
                        const _Divider(),
                        Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 14),
                          child: Center(
                            child: Text(
                              '${status.subsong + 1} / ${math.max(1, status.subsongCount)}',
                              style: _vt(26),
                            ),
                          ),
                        ),
                        const _Divider(),
                        _RetroButton(
                          label: 'Sottobrano successivo',
                          width: 44,
                          onPressed: () => controller.selectSubsong(status.subsong + 1),
                          child: (c) => _PixelIcon(_PixelIcon.up, size: 16, grid: 8, color: c),
                        ),
                        const _Divider(),
                        _RetroButton(
                          label: 'Sottobrano precedente',
                          width: 44,
                          onPressed: () => controller.selectSubsong(status.subsong - 1),
                          child: (c) => _PixelIcon(_PixelIcon.down, size: 16, grid: 8, color: c),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

/// Avanzamento a 1 bit: righe verticali per la parte suonata. Con durata
/// sconosciuta un blocco scorre per mostrare che il brano va avanti.
class _ProgressStrip extends StatelessWidget {
  const _ProgressStrip({required this.status, required this.onSeek});

  final CoreStatus status;
  final ValueChanged<double> onSeek;

  @override
  Widget build(BuildContext context) {
    final duration = status.duration;
    return LayoutBuilder(
      builder: (context, constraints) {
        void seekAt(double dx) {
          if (duration != null) onSeek((dx / constraints.maxWidth).clamp(0.0, 1.0) * duration);
        }

        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTapDown: (d) => seekAt(d.localPosition.dx),
          onHorizontalDragUpdate: (d) => seekAt(d.localPosition.dx),
          child: CustomPaint(
            size: Size(constraints.maxWidth, 36),
            painter: _ProgressStripPainter(
              fraction: duration == null || duration <= 0 ? null : (status.position / duration).clamp(0.0, 1.0),
              position: status.position,
            ),
          ),
        );
      },
    );
  }
}

class _ProgressStripPainter extends CustomPainter {
  _ProgressStripPainter({required this.fraction, required this.position});

  final double? fraction;
  final double position;

  @override
  void paint(Canvas canvas, Size size) {
    final ink = Paint()
      ..color = _ink
      ..isAntiAlias = false;
    final inner = Rect.fromLTWH(2, 2, size.width - 4, size.height - 4);
    canvas.drawRect(Offset.zero & size, ink);
    canvas.drawRect(inner, Paint()..color = _paper);

    final value = fraction;
    double from;
    double to;
    if (value == null) {
      final start = (position / 8) % 1.2 - 0.2;
      from = inner.left + start.clamp(0.0, 1.0) * inner.width;
      to = inner.left + (start + 0.2).clamp(0.0, 1.0) * inner.width;
    } else {
      from = inner.left;
      to = inner.left + value * inner.width;
    }
    for (var x = from.roundToDouble(); x < to; x += 3) {
      canvas.drawRect(Rect.fromLTWH(x, inner.top + 4, 2, inner.height - 8), ink);
    }
    if (value != null) {
      canvas.drawRect(
        Rect.fromLTWH(to.roundToDouble() - 1, 0, 3, size.height),
        Paint()
          ..color = _pink
          ..isAntiAlias = false,
      );
    }
  }

  @override
  bool shouldRepaint(_ProgressStripPainter old) => old.fraction != fraction || old.position != position;
}

class _PlaylistWindow extends StatelessWidget {
  const _PlaylistWindow({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final tracks = controller.tracks;
    final archive = tracks.isNotEmpty && tracks.first.fromArchive ? ' — ${tracks.first.location.split(' · ').first}' : '';

    return _Window(
      title: 'Playlist$archive',
      expand: true,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Container(
            height: 30,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
            child: Row(
              children: [
                SizedBox(width: 40, child: Text('#', style: _silk(11))),
                Expanded(child: Text('BRANO', style: _silk(11))),
                SizedBox(width: 220, child: Text('POSIZIONE', style: _silk(11))),
              ],
            ),
          ),
          Expanded(
            child: tracks.isEmpty
                ? Center(child: Text('Nessun brano: usa Apri… nel menu', style: _pixel(16)))
                : RawScrollbar(
                    thumbVisibility: true,
                    trackVisibility: true,
                    thickness: 14,
                    thumbColor: _ink,
                    trackColor: _lightGray,
                    trackBorderColor: _ink,
                    radius: Radius.zero,
                    child: ListView.builder(
                      itemCount: tracks.length,
                      itemExtent: 44,
                      itemBuilder: (context, i) => _PlaylistRow(
                        index: i,
                        track: tracks[i],
                        current: i == controller.currentIndex,
                        onTap: () => controller.playIndex(i),
                      ),
                    ),
                  ),
          ),
        ],
      ),
    );
  }
}

class _PlaylistRow extends StatelessWidget {
  const _PlaylistRow({required this.index, required this.track, required this.current, required this.onTap});

  final int index;
  final Track track;
  final bool current;
  final VoidCallback onTap;

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
            color: current ? _ink : _paper,
            padding: const EdgeInsets.only(left: 12, right: 24),
            child: Row(
              children: [
                SizedBox(width: 40, child: Text((index + 1).toString().padLeft(2, '0'), style: _vt(22, color: color))),
                Expanded(child: Text(track.fileName, style: _pixel(16, color: color), overflow: TextOverflow.ellipsis)),
                SizedBox(
                  width: 220,
                  child: Text(track.location, style: _pixel(14, color: color), overflow: TextOverflow.ellipsis),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _ScopeWindow extends StatelessWidget {
  const _ScopeWindow({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    Widget screen(String label, int channel) => Container(
          decoration: const BoxDecoration(border: Border.fromBorderSide(BorderSide(color: _ink, width: 2))),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Container(
                height: 30,
                padding: const EdgeInsets.symmetric(horizontal: 8),
                alignment: Alignment.centerLeft,
                decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: _ink, width: 2))),
                child: Text(label, style: _silk(11)),
              ),
              Expanded(
                child: Container(
                  color: _ink,
                  child: CustomPaint(
                    size: Size.infinite,
                    painter: _SteppedScopePainter(samples: controller.scope, channel: channel),
                  ),
                ),
              ),
            ],
          ),
        );

    return _Window(
      title: 'Oscilloscopio',
      expand: true,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(child: screen('CANALE SINISTRO', 0)),
            const SizedBox(height: 12),
            Expanded(child: screen('CANALE DESTRO', 1)),
          ],
        ),
      ),
    );
  }
}

/// Oscilloscopio a gradini, come su uno schermo a 1 bit.
class _SteppedScopePainter extends CustomPainter {
  _SteppedScopePainter({required this.samples, required this.channel});

  final Float32List samples;
  final int channel;

  @override
  void paint(Canvas canvas, Size size) {
    const step = 4.0;
    final frames = samples.length ~/ 2;
    final mid = size.height / 2;
    final paint = Paint()
      ..color = _paper
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..isAntiAlias = false;

    double yAt(double x) {
      if (frames < 2) return mid;
      final i = (x / size.width * (frames - 1)).round();
      final y = mid - samples[i * 2 + channel].clamp(-1.0, 1.0) * (mid - 6);
      return (y / 2).roundToDouble() * 2;
    }

    final path = Path()..moveTo(0, yAt(0));
    for (var x = step; x <= size.width; x += step) {
      path
        ..lineTo(x, yAt(x - step))
        ..lineTo(x, yAt(x));
    }
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(_SteppedScopePainter old) => !identical(old.samples, samples);
}

class _InfoWindow extends StatelessWidget {
  const _InfoWindow({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final info = controller.details;
    final rows = info == null
        ? [('STATO', 'nessun brano')]
        : [
            ('FORMATO', info.formatLabel.isEmpty ? '—' : info.formatLabel),
            ('BACKEND', info.backend),
            ('FILE', info.file),
            ('PACKER', info.packers.isEmpty ? 'nessuno' : info.packers.join(' › ')),
            ('SOTTOBRANI', '${info.subsongCount}'),
          ];

    return _Window(
      title: 'Info',
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
        child: Column(
          children: [
            for (final (label, value) in rows)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 5),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    Text(label, style: _silk(11)),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Padding(
                        padding: const EdgeInsets.only(bottom: 5),
                        child: CustomPaint(size: const Size(double.infinity, 2), painter: _DotsPainter()),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Flexible(
                      child: Text(
                        value,
                        // Nel font pixel il 5 sembra una S: i numeri vanno in VT323.
                        style: RegExp(r'^[0-9 /:]+$').hasMatch(value) ? _vt(22) : _pixel(16),
                        overflow: TextOverflow.ellipsis,
                        textAlign: TextAlign.right,
                      ),
                    ),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _DotsPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = _ink
      ..isAntiAlias = false;
    for (var x = 0.0; x + 2 <= size.width; x += 4) {
      canvas.drawRect(Rect.fromLTWH(x, 0, 2, 2), paint);
    }
  }

  @override
  bool shouldRepaint(_DotsPainter old) => false;
}

class _OutputWindow extends StatelessWidget {
  const _OutputWindow({required this.controller});

  final PlayerController controller;

  /// Livello RMS di un canale dell'oscilloscopio, 0..1.
  double _level(int channel) {
    final samples = controller.scope;
    final frames = samples.length ~/ 2;
    if (frames == 0) return 0;
    var sum = 0.0;
    for (var i = 0; i < frames; i++) {
      final v = samples[i * 2 + channel];
      sum += v * v;
    }
    return (math.sqrt(sum / frames) * 3).clamp(0.0, 1.0);
  }

  @override
  Widget build(BuildContext context) {
    return _Window(
      title: 'Uscita',
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _VuMeter(label: 'L', level: _level(0)),
                const SizedBox(width: 18),
                _VuMeter(label: 'R', level: _level(1)),
              ],
            ),
            const SizedBox(height: 18),
            Text('VOLUME', style: _silk(11)),
            const SizedBox(height: 8),
            _PixelSlider(value: controller.volume, onChanged: controller.setVolume),
          ],
        ),
      ),
    );
  }
}

class _VuMeter extends StatelessWidget {
  const _VuMeter({required this.label, required this.level});

  static const _blocks = 11;

  final String label;
  final double level;

  @override
  Widget build(BuildContext context) {
    final lit = (level * _blocks).round();
    Color colorFor(int block) => block >= 10 ? _pink : (block >= 7 ? _lilac : _teal);
    return Column(
      children: [
        for (var block = _blocks - 1; block >= 0; block--)
          Container(
            width: 30,
            height: 10,
            margin: const EdgeInsets.only(bottom: 3),
            decoration: BoxDecoration(
              color: block < lit ? colorFor(block) : _paper,
              border: const Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
            ),
          ),
        const SizedBox(height: 4),
        Text(label, style: _silk(11)),
      ],
    );
  }
}

class _PixelSlider extends StatelessWidget {
  const _PixelSlider({required this.value, required this.onChanged});

  final double value;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      slider: true,
      label: 'Volume',
      value: '${(value * 100).round()}%',
      child: LayoutBuilder(
        builder: (context, constraints) {
          const handle = 22.0;
          final usable = constraints.maxWidth - handle;
          void set(double dx) => onChanged(((dx - handle / 2) / usable).clamp(0.0, 1.0));
          return GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTapDown: (d) => set(d.localPosition.dx),
            onHorizontalDragUpdate: (d) => set(d.localPosition.dx),
            child: SizedBox(
              height: 44,
              child: Stack(
                children: [
                  Positioned(
                    left: handle / 2,
                    right: handle / 2,
                    top: 16,
                    child: Container(
                      height: 12,
                      decoration: const BoxDecoration(
                        color: _paper,
                        border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                      ),
                      child: FractionallySizedBox(
                        alignment: Alignment.centerLeft,
                        widthFactor: value,
                        child: Container(color: _teal),
                      ),
                    ),
                  ),
                  Positioned(
                    left: value * usable,
                    top: 8,
                    child: Container(
                      width: handle,
                      height: 28,
                      decoration: const BoxDecoration(
                        color: _paper,
                        border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                        boxShadow: [BoxShadow(color: _ink, offset: Offset(2, 2))],
                      ),
                    ),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }
}

class _HelloWindow extends StatelessWidget {
  const _HelloWindow({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final message = controller.error ??
        (controller.tracks.isEmpty
            ? 'Benvenuto in megaplayer! Apri un archivio o un modulo dal menu.'
            : controller.status.isPlaying
                ? 'Buon ascolto!'
                : 'Premi play quando vuoi.');

    return _Window(
      title: 'Ciao!',
      expand: true,
      child: CustomPaint(
        painter: _DitherPainter(),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
                decoration: const BoxDecoration(
                  color: _paper,
                  border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
                ),
                child: Text(message, style: _pixel(16, height: 1.35), textAlign: TextAlign.center),
              ),
              Transform.translate(
                offset: const Offset(0, -2),
                child: const _PixelIcon(_bubbleTail, size: 16, grid: 8),
              ),
              const SizedBox(height: 6),
              CustomPaint(size: const Size.square(96), painter: _MascotPainter()),
            ],
          ),
        ),
      ),
    );
  }

  static const _bubbleTail = [
    [1, 0, 1, 1], [6, 0, 1, 1], [2, 1, 1, 1], [5, 1, 1, 1], [3, 2, 2, 1],
  ];
}

class _DitherPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = _paper);
    final paint = Paint()
      ..color = _lightGray
      ..isAntiAlias = false;
    for (var y = 0.0; y < size.height; y += 2) {
      for (var x = (y % 4 == 0) ? 0.0 : 2.0; x < size.width; x += 4) {
        canvas.drawRect(Rect.fromLTWH(x, y, 2, 2), paint);
      }
    }
  }

  @override
  bool shouldRepaint(_DitherPainter old) => false;
}

/// La mascotte: un floppy disk sorridente, 16×16 pixel.
class _MascotPainter extends CustomPainter {
  static const _rows = [
    '##############..',
    '#llll#####llll#.',
    '#llll#w#w#llll##',
    '#llll#w#w#lllll#',
    '#llll#####lllll#',
    '#llllllllllllll#',
    '#l############l#',
    '#l#wwwwwwwwww#l#',
    '#l#wwwwwwwwww#l#',
    '#l#ww#wwww#ww#l#',
    '#l#ww#wwww#ww#l#',
    '#l#wpwwwwwwpw#l#',
    '#l#wwww##wwww#l#',
    '#l#wwwwwwwwww#l#',
    '#l############l#',
    '################',
  ];
  static const _colors = {'#': _ink, 'l': _lilac, 'w': _paper, 'p': _pink};

  @override
  void paint(Canvas canvas, Size size) {
    final unit = size.width / 16;
    for (var y = 0; y < _rows.length; y++) {
      for (var x = 0; x < _rows[y].length; x++) {
        final color = _colors[_rows[y][x]];
        if (color == null) continue;
        canvas.drawRect(
          Rect.fromLTWH(x * unit, y * unit, unit, unit),
          Paint()
            ..color = color
            ..isAntiAlias = false,
        );
      }
    }
  }

  @override
  bool shouldRepaint(_MascotPainter old) => false;
}
