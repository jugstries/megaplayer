# megaplayer — app desktop

L'app Flutter di megaplayer, per ora solo macOS. Istruzioni, architettura e
limiti attuali sono nel [README principale](../README.md#app-desktop-flutter).

```
lib/core/      binding FFI verso libmegaplayer e modelli dei dati
lib/player/    controller condiviso dalle skin: coda, stato, comandi
lib/skins/     skin moderna e skin retrò
assets/fonts/  font delle skin (licenza OFL)
tool/          make_icons.py: le icone delle due skin per macOS
```
