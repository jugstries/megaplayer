import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Il comportamento di una manopola, uguale nelle due skin: si gira
/// trascinando in verticale (o in orizzontale), con la rotella o con le
/// frecce; il doppio clic la azzera. Ogni skin la disegna a modo suo.
class KnobControl extends StatefulWidget {
  const KnobControl({
    super.key,
    required this.value,
    required this.onChanged,
    required this.label,
    required this.builder,
    this.step = 0.05,
  });

  final double value;
  final ValueChanged<double> onChanged;
  final String label;
  final double step;

  /// Disegna la manopola; `focused` serve a mostrare il fuoco da tastiera.
  final Widget Function(BuildContext context, bool focused) builder;

  @override
  State<KnobControl> createState() => _KnobControlState();
}

class _KnobControlState extends State<KnobControl> {
  // Pixel di trascinamento per andare da 0 a 1.
  static const _dragRange = 200.0;
  static const _scrollRange = 600.0;

  final _focusNode = FocusNode(debugLabel: 'KnobControl');
  bool _focused = false;

  // L'ultimo valore chiesto: più eventi possono arrivare prima che il
  // genitore ridisegni la manopola con il valore nuovo.
  double? _requested;

  double get _value => _requested ?? widget.value;

  @override
  void didUpdateWidget(KnobControl old) {
    super.didUpdateWidget(old);
    _requested = null;
  }

  @override
  void dispose() {
    _focusNode.dispose();
    super.dispose();
  }

  String _percent(double value) => '${(value.clamp(0.0, 1.0) * 100).round()}%';

  void _set(double value) {
    final clamped = value.clamp(0.0, 1.0);
    if (clamped == _value) return;
    _requested = clamped;
    widget.onChanged(clamped);
  }

  KeyEventResult _onKey(FocusNode node, KeyEvent event) {
    if (event is KeyUpEvent) return KeyEventResult.ignored;
    final key = event.logicalKey;
    if (key == LogicalKeyboardKey.arrowUp || key == LogicalKeyboardKey.arrowRight) {
      _set(_value + widget.step);
    } else if (key == LogicalKeyboardKey.arrowDown || key == LogicalKeyboardKey.arrowLeft) {
      _set(_value - widget.step);
    } else if (key == LogicalKeyboardKey.home) {
      _set(0);
    } else if (key == LogicalKeyboardKey.end) {
      _set(1);
    } else {
      return KeyEventResult.ignored;
    }
    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    return Semantics(
      slider: true,
      label: widget.label,
      value: _percent(widget.value),
      increasedValue: _percent(widget.value + widget.step),
      decreasedValue: _percent(widget.value - widget.step),
      onIncrease: () => _set(_value + widget.step),
      onDecrease: () => _set(_value - widget.step),
      child: Focus(
        focusNode: _focusNode,
        onKeyEvent: _onKey,
        onFocusChange: (focused) => setState(() => _focused = focused),
        child: Listener(
          // Qui e non in onTapDown: se il gesto diventa un trascinamento il
          // tap non arriva, ma il fuoco serve lo stesso per le frecce.
          onPointerDown: (_) => _focusNode.requestFocus(),
          onPointerSignal: (event) {
            if (event is PointerScrollEvent) {
              GestureBinding.instance.pointerSignalResolver.register(event, (event) {
                final scroll = event as PointerScrollEvent;
                _set(_value - scroll.scrollDelta.dy / _scrollRange);
              });
            }
          },
          child: MouseRegion(
            cursor: SystemMouseCursors.resizeUpDown,
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onDoubleTap: () => _set(0),
              dragStartBehavior: DragStartBehavior.down,
              onPanUpdate: (d) => _set(_value + (d.delta.dx - d.delta.dy) / _dragRange),
              child: widget.builder(context, _focused),
            ),
          ),
        ),
      ),
    );
  }
}
