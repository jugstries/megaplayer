import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// I file trascinati sulla finestra dal Finder o da Esplora risorse, che
/// Flutter da solo non riceve: il runner li manda sul canale
/// "megaplayer/drop", con "enter" ed "exit" mentre passano sopra la
/// finestra e "drop" con i percorsi quando si lasciano. Intanto sopra
/// [child] c'è [overlay], la sagoma della skin che invita a lasciarli.
class FileDrop extends StatefulWidget {
  const FileDrop({super.key, required this.onDrop, required this.overlay, required this.child});

  final void Function(List<String> paths) onDrop;
  final Widget overlay;
  final Widget child;

  @override
  State<FileDrop> createState() => _FileDropState();
}

class _FileDropState extends State<FileDrop> {
  static const _channel = MethodChannel('megaplayer/drop');

  // Cambiando skin la nuova parte prima che la vecchia se ne vada: il canale
  // va a chi è arrivato per ultimo.
  static _FileDropState? _current;

  bool _hovering = false;

  @override
  void initState() {
    super.initState();
    _current = this;
    _channel.setMethodCallHandler((call) async => _current?._handle(call));
  }

  @override
  void dispose() {
    if (_current == this) _current = null;
    super.dispose();
  }

  void _handle(MethodCall call) {
    if (!mounted) return;
    switch (call.method) {
      case 'enter':
        setState(() => _hovering = true);
      case 'exit':
        setState(() => _hovering = false);
      case 'drop':
        setState(() => _hovering = false);
        final paths = (call.arguments as List?)?.whereType<String>().toList() ?? const <String>[];
        if (paths.isNotEmpty) widget.onDrop(paths);
    }
  }

  @override
  Widget build(BuildContext context) => Stack(
        fit: StackFit.expand,
        children: [
          widget.child,
          if (_hovering) IgnorePointer(child: widget.overlay),
        ],
      );
}
