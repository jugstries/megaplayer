part of 'modern_skin.dart';

// I canali del brano nella skin moderna: uno per voce del chip o canale del
// tracker, da zittire con un clic o da mettere in solo.

class _ChannelStrip extends StatelessWidget {
  const _ChannelStrip({required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    final channels = controller.channels;
    final changed = controller.mutedChannels.isNotEmpty || controller.soloChannels.isNotEmpty;
    return Row(
      children: [
        SizedBox(
          width: 76,
          child: Tooltip(
            message: 'Clic su un canale per zittirlo, S per sentire solo lui',
            child: Text('CANALI', style: _label()),
          ),
        ),
        Expanded(
          child: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                for (var i = 0; i < channels.length; i++)
                  Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: _ChannelChip(
                      index: i,
                      name: channels[i],
                      audible: controller.channelAudible(i),
                      muted: controller.mutedChannels.contains(i),
                      solo: controller.soloChannels.contains(i),
                      color: _stageColor(channels.length == 1 ? 0 : i / (channels.length - 1)),
                      onMute: () => controller.toggleChannelMute(i),
                      onSolo: () => controller.toggleChannelSolo(i),
                    ),
                  ),
              ],
            ),
          ),
        ),
        const SizedBox(width: 12),
        _SmallButton(icon: Icons.restart_alt_rounded, label: 'Tutti', onPressed: changed ? controller.resetChannels : null),
      ],
    );
  }
}

/// Un canale: il numero si clicca per zittirlo, la S sotto per il solo.
class _ChannelChip extends StatelessWidget {
  const _ChannelChip({
    required this.index,
    required this.name,
    required this.audible,
    required this.muted,
    required this.solo,
    required this.color,
    required this.onMute,
    required this.onSolo,
  });

  final int index;
  final String name;
  final bool audible;
  final bool muted;
  final bool solo;
  final Color color;
  final VoidCallback onMute;
  final VoidCallback onSolo;

  @override
  Widget build(BuildContext context) {
    final label = name.isEmpty ? 'Canale ${index + 1}' : name;
    final state = solo ? 'in solo' : (muted ? 'zittito' : (audible ? 'acceso' : 'escluso dal solo'));
    return Semantics(
      label: '$label, $state',
      child: Container(
        width: 44,
        decoration: BoxDecoration(
          color: audible ? _selected : _inset,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: solo ? color : (audible ? _border : _insetBorder)),
        ),
        child: Column(
          children: [
            Tooltip(
              message: '$label · ${muted ? 'clic per riaccenderlo' : 'clic per zittirlo'}',
              waitDuration: const Duration(milliseconds: 500),
              child: InkWell(
                onTap: onMute,
                borderRadius: const BorderRadius.vertical(top: Radius.circular(12)),
                child: SizedBox(
                  height: 30,
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        '${index + 1}',
                        style: _mono(13, color: audible ? _text : _faint, weight: audible ? 600 : 400).copyWith(
                          decoration: muted ? TextDecoration.lineThrough : null,
                          decorationColor: _faint,
                        ),
                      ),
                      const SizedBox(height: 3),
                      Container(
                        width: 18,
                        height: 3,
                        decoration: BoxDecoration(
                          color: audible ? color : _track,
                          borderRadius: BorderRadius.circular(2),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
            Tooltip(
              message: solo ? 'Togli dal solo' : 'Senti solo questo canale (con gli altri in solo)',
              waitDuration: const Duration(milliseconds: 500),
              child: InkWell(
                onTap: onSolo,
                borderRadius: const BorderRadius.vertical(bottom: Radius.circular(12)),
                child: Container(
                  height: 18,
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: solo ? color : Colors.transparent,
                    borderRadius: const BorderRadius.vertical(bottom: Radius.circular(11)),
                  ),
                  child: Text('S', style: _sora(10, weight: 700, color: solo ? _background : _faint)),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
