# megaplayer

Prototipo di un player multipiattaforma per formati audio "di ogni tipo",
a partire dai formati Amiga. Per ora è un'applicazione da riga di comando
costruita su un core con un'interfaccia comune per i decoder.

## Backend

| Backend   | Libreria              | Cosa suona                                                     |
|-----------|-----------------------|----------------------------------------------------------------|
| `openmpt` | libopenmpt            | moduli tracker: MOD, XM, S3M, IT, MED, OKT, ...                |
| `uade`    | UADE 3.05 (libuade)   | 200+ formati Amiga, con i replayer originali Eagleplayer/DeliTracker in emulazione 68000 + Paula |

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

Serve un compilatore C++20, CMake ≥ 3.21, pkg-config e git.

```bash
brew install libopenmpt libarchive   # Debian/Ubuntu: apt install libopenmpt-dev libarchive-dev
scripts/build-deps.sh                # UADE (+ libzakalwe, bencode-tools), ancient, miniaudio
cmake -S . -B build -G Ninja
cmake --build build
```

`scripts/build-deps.sh` scarica le dipendenze a versioni fissate in
`third_party/src` e installa UADE in `third_party/install`. I percorsi di
UADE (replayer, `uadecore`) sono compilati in libuade: se sposti la cartella
del progetto rilancia `scripts/build-deps.sh --clean`.

UADE gira solo su sistemi UNIX (macOS, Linux, BSD). Su Windows il backend
viene disattivato e resta `openmpt`.

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

## Struttura

```
src/core/       interfaccia Decoder, registro dei backend, caricamento e
                decompressione dei file, archivi, uscita audio, export WAV
src/decoders/   un file per backend
src/cli/        la riga di comando
scripts/        build delle dipendenze
```

Per aggiungere un backend: implementare `Decoder` e `DecoderFactory`
(`src/core/Decoder.h`), registrarlo in `src/decoders/BuiltinDecoders.cpp` e
aggiungere un'opzione `MP_WITH_*` in `CMakeLists.txt`.

## Licenze

UADE è GPL-2.0 (il motore UAE) con parti LGPL; i replayer in
`share/uade/players` hanno licenze e autori diversi. libopenmpt è BSD-3,
ancient e libarchive BSD-2, miniaudio public domain / MIT-0. Finché il backend UADE è
incluso, il binario risultante va distribuito sotto GPL.
