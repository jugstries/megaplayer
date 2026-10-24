# megaplayer

Prototipo di un player multipiattaforma per formati audio "di ogni tipo",
a partire dai formati Amiga. Un core C++ con un'interfaccia comune per i
decoder, usato da un'applicazione da riga di comando e da un'app desktop in
Flutter (per ora macOS) con due skin: moderna e retrò.

## Backend

| Backend   | Libreria              | Cosa suona                                                     |
|-----------|-----------------------|----------------------------------------------------------------|
| `openmpt` | libopenmpt            | moduli tracker: MOD, XM, S3M, IT, MED, OKT, ...                |
| `uade`    | UADE 3.05             | 200+ formati Amiga, con i replayer originali Eagleplayer/DeliTracker in emulazione 68000 + Paula |

Prima di passare i dati ai backend, il core rimuove gli strati di compressione
con [ancient](https://github.com/temisu/ancient) (PowerPacker, XPK, Imploder,
StoneCracker, MMCMP, gzip, bzip2, ...).

Gli archivi (LHA, ZIP, 7z, tar, ...) si aprono direttamente, tramite
[libarchive](https://libarchive.org): vengono estratti in una directory
temporanea, cancellata all'uscita, e si suonano i brani che contengono. I file
che nessun backend riconosce (testi, icone `.info`) vengono saltati. I file
restano su disco e non solo in memoria perché alcuni formati Amiga sono divisi
in più file, che UADE cerca uno accanto all'altro. libarchive non legge gli
archivi LZX dell'Amiga.

Quando più backend riconoscono un file vince quello col punteggio più alto
(`openmpt` per i formati che conosce, perché è più veloce, conosce la durata
dei brani e fa seek immediato). Se l'apertura fallisce si prova il successivo.
Con `-b` si sceglie a mano.

## Build

[![build](https://github.com/jugstries/megaplayer/actions/workflows/build.yml/badge.svg)](https://github.com/jugstries/megaplayer/actions/workflows/build.yml)

Serve un compilatore C++20 (Clang o GCC; su Windows MinGW-w64), CMake ≥ 3.21,
Ninja, pkg-config, git e Python 3 per i test. libopenmpt e libarchive arrivano
dal sistema:

| Sistema | Dipendenze |
|---|---|
| macOS | `brew install cmake ninja libopenmpt libarchive` |
| Debian/Ubuntu | `apt install cmake ninja-build pkg-config libopenmpt-dev libarchive-dev` |
| Windows (MSYS2, ambiente UCRT64) | `pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,python,libopenmpt,libarchive}` |

Poi, uguale ovunque (su Windows dalla shell di MSYS2):

```bash
scripts/build-deps.sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

`scripts/build-deps.sh` scarica a versioni fissate i sorgenti di UADE,
libzakalwe, bencode-tools, ancient e miniaudio in `third_party/src` e applica
`patches/uade-in-process.patch` a UADE. Tutto il resto lo fa CMake: compila
UADE senza il suo script di configurazione (che funziona solo su UNIX, vedi
[cmake/UadeInProcess.cmake](cmake/UadeInProcess.cmake)) e copia replayer e
configurazione in `build/uade-data`. La CLI e i test li cercano lì; le app li
indicano a runtime con `mp_set_uade_data_dir()`.

La CI di GitHub Actions ([.github/workflows/build.yml](.github/workflows/build.yml))
compila e prova tutto su Linux e Windows; macOS si prova in locale.

Per controllare la build Windows da macOS senza aspettare la CI c'è la
cross-compilazione con MinGW-w64 (`brew install mingw-w64`), senza libopenmpt
e libarchive; i generatori dell'emulatore 68000 vengono dalla build nativa:

```bash
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DMP_UADE_HOST_TOOLS=$PWD/build -DMP_WITH_OPENMPT=OFF -DMP_WITH_ARCHIVES=OFF
```

### UADE nel processo del player

UADE di suo avvia l'emulatore (`uadecore`) come processo separato e ci parla
tramite pipe: su Windows, iOS e nel browser non si può fare. Qui CMake compila
libuade ed emulatore in un'unica libreria e `uadecore` gira in un thread
([src/uade/InProcess.cpp](src/uade/InProcess.cpp)): al posto delle pipe ci
sono canali in memoria, e gli `exit()` dell'emulatore terminano solo il
thread. La patch a UADE è piccola: esclude le funzioni che avviano il
processo e fa sì che l'emulatore possa ripartire più volte nello stesso
processo senza perdere memoria.

L'audio è identico, bit per bit, a quello della versione con processo
separato. L'emulatore usa molte variabili globali, quindi suona un solo brano
UADE per volta: aprirne un secondo mentre il primo suona dà un errore.

Su Windows UADE si compila con MinGW-w64: il thread di `uadecore` usa le
pthread di MinGW, e le poche funzioni POSIX che mancano (`strsep`, `lockf`,
`realpath`…) stanno in [src/uade/windows](src/uade/windows), incluse solo lì.
Resta da fare la build WebAssembly.

libuade registra in `~/.uade/contentdb` la durata dei brani suonati fino in
fondo e la mostra alle riproduzioni successive. È un solo valore per file:
per i file con più sottobrani megaplayer lo ignora.

## Uso

```bash
build/megaplayer play mod.enigma AHX.Cruisin   # riproduce
build/megaplayer play Stormlord.lha            # tutti i brani dell'archivio
build/megaplayer info -b uade mod.enigma       # formato, backend, metadati
build/megaplayer render -s 2 -o out.wav Stormlord.lha/Stormlord/jt.ingame   # esporta in WAV
build/megaplayer backends
```

`ARCHIVIO/percorso/interno` sceglie un file o una cartella dentro un
archivio.

Durante `play`: spazio pausa, ←/→ seek di 10 s, `n`/`p` sottobrano
successivo/precedente, invio file successivo, `q` esci.

## App desktop (Flutter)

In `app/` c'è l'app Flutter, per ora solo macOS, con la skin moderna (scura,
gradienti arancio e viola) e quella retrò (Mac a 1 bit); si passa dall'una
all'altra dall'interfaccia. Parla con il core attraverso l'API C di
[src/capi/megaplayer.h](src/capi/megaplayer.h), compilata in
`libmegaplayer.dylib`: un passo di build di Xcode
([scripts/embed-core-macos.sh](scripts/embed-core-macos.sh)) la compila con
CMake e la copia, firmata, dentro l'app. Il core decodifica in un thread suo
([src/core/Player.cpp](src/core/Player.cpp)); l'app legge lo stato circa 30
volte al secondo.

Serve Flutter (≥ 3.47) con Xcode, dopo `scripts/build-deps.sh`:

```bash
cd app
flutter run -d macos
```

Per aprire subito dei file, da un'app già compilata:

```bash
open app/build/macos/Build/Products/Debug/megaplayer.app --args /percorso/Stormlord.lha
```

Test: `flutter test` in `app/`. `test/skin_screenshots_test.dart` disegna le
due skin con un core finto e salva le immagini in `app/build/screenshots`;
`test/ffi_core_test.dart` prova i binding con la libreria vera se
`MEGAPLAYER_CORE` e `STORMLORD_LHA` indicano libreria e archivio di prova.

Limiti attuali:

- la sandbox di macOS è disattivata, e la libreria punta a libopenmpt e
  libarchive di Homebrew e ai replayer di UADE in `build/uade-data`: l'app
  gira solo su questa macchina di sviluppo. Per distribuirla vanno messi nel
  bundle (i replayer con `mp_set_uade_data_dir()`);
- gli oscilloscopi mostrano i canali sinistro e destro dell'uscita: i quattro
  canali Paula separati richiedono lavoro nel core;
- niente trascinamento dei file sulla finestra e la skin scelta non viene
  ricordata.

## Struttura

```
src/core/       interfaccia Decoder, registro dei backend, caricamento e
                decompressione dei file, archivi, motore di riproduzione,
                uscita audio, export WAV
src/decoders/   un file per backend
src/uade/       UADE nel processo del player
src/capi/       API C per le interfacce grafiche
src/cli/        la riga di comando
app/            app desktop in Flutter
tests/          test automatici del core
cmake/          build di UADE dai sorgenti, toolchain di cross-compilazione
patches/        modifiche alle dipendenze
scripts/        download delle dipendenze e passo di build dell'app macOS
.github/        CI per Linux e Windows
```

Per aggiungere un backend: implementare `Decoder` e `DecoderFactory`
(`src/core/Decoder.h`), registrarlo in `src/decoders/BuiltinDecoders.cpp` e
aggiungere un'opzione `MP_WITH_*` in `CMakeLists.txt`.

## Licenze

UADE è GPL-2.0 (il motore UAE) con parti LGPL; i replayer in
`share/uade/players` hanno licenze e autori diversi. libopenmpt è BSD-3,
ancient e libarchive BSD-2, miniaudio public domain / MIT-0. Finché il backend UADE è
incluso, il binario risultante va distribuito sotto GPL.
