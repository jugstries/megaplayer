part of 'retro_skin.dart';

// I canali del brano nella skin retrò: caselle a 1 bit, retinate quando il
// canale non si sente, con la S del solo in negativo.

class _ChannelStrip extends StatelessWidget {
  const _ChannelStrip({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final channels = controller.channels;
    final changed = controller.mutedChannels.isNotEmpty || controller.soloChannels.isNotEmpty;
    return Row(
      children: [
        Tooltip(
          message: 'Clic su un canale per zittirlo, S per sentire solo lui',
          child: Text('CANALI', style: _silk(12)),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                for (var i = 0; i < channels.length; i++)
                  Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: _ChannelBox(
                      index: i,
                      name: channels[i],
                      audible: controller.channelAudible(i),
                      muted: controller.mutedChannels.contains(i),
                      solo: controller.soloChannels.contains(i),
                      onMute: () => controller.toggleChannelMute(i),
                      onSolo: () => controller.toggleChannelSolo(i),
                    ),
                  ),
              ],
            ),
          ),
        ),
        const SizedBox(width: 10),
        _TextButton(label: 'TUTTI', onPressed: changed ? controller.resetChannels : null),
      ],
    );
  }
}

class _ChannelBox extends StatelessWidget {
  const _ChannelBox({
    required this.index,
    required this.name,
    required this.audible,
    required this.muted,
    required this.solo,
    required this.onMute,
    required this.onSolo,
  });

  final int index;
  final String name;
  final bool audible;
  final bool muted;
  final bool solo;
  final VoidCallback onMute;
  final VoidCallback onSolo;

  @override
  Widget build(BuildContext context) {
    final label = name.isEmpty ? 'Canale ${index + 1}' : name;
    final state = solo ? 'in solo' : (muted ? 'zittito' : (audible ? 'acceso' : 'escluso dal solo'));
    return Semantics(
      label: '$label, $state',
      child: Container(
        width: 38,
        decoration: const BoxDecoration(
          color: _paper,
          border: Border.fromBorderSide(BorderSide(color: _ink, width: 2)),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Tooltip(
              message: '$label · ${muted ? 'clic per riaccenderlo' : 'clic per zittirlo'}',
              waitDuration: const Duration(milliseconds: 500),
              child: MouseRegion(
                cursor: SystemMouseCursors.click,
                child: GestureDetector(
                  onTap: onMute,
                  child: CustomPaint(
                    // Retinata, come i comandi disattivati del Mac.
                    painter: audible ? null : _DitherPainter(),
                    child: SizedBox(
                      height: 28,
                      child: Center(
                        child: Container(
                          color: audible ? Colors.transparent : _paper,
                          padding: const EdgeInsets.symmetric(horizontal: 2),
                          child: Text('${index + 1}', style: _vt(24, color: audible ? _ink : _midGray, height: 1)),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
            Container(height: 2, color: _ink),
            Tooltip(
              message: solo ? 'Togli dal solo' : 'Senti solo questo canale (con gli altri in solo)',
              waitDuration: const Duration(milliseconds: 500),
              child: MouseRegion(
                cursor: SystemMouseCursors.click,
                child: GestureDetector(
                  onTap: onSolo,
                  child: Container(
                    height: 16,
                    color: solo ? _ink : _paper,
                    alignment: Alignment.center,
                    child: Text('S', style: _silk(10, color: solo ? _paper : _ink)),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
