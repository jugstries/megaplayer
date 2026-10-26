# megaplayer

Prototipo di un player multipiattaforma per formati audio "di ogni tipo",
a partire dai formati Amiga. Un core C++ con un'interfaccia comune per i
decoder, usato da un'applicazione da riga di comando e da un'app desktop in
Flutter (per ora macOS) con due skin: moderna e retrò.

## Backend

| Backend   | Libreria              | Cosa suona                                                     |
|-----------|-----------------------|----------------------------------------------------------------|
| `miniaudio` | miniaudio, stb_vorbis | file audio: MP3 (e MP1/MP2), FLAC, WAV, Ogg Vorbis, con titolo e autore dai tag |
| `sid`     | libsidplayfp 3.1.1, libresidfp 1.2.3 | Commodore 64: PSID e RSID, con l'emulazione reSIDfp del chip SID (6581 e 8580) |
| `gme`     | game-music-emu 0.6.5  | console: NES (NSF, NSFE), SNES (SPC), Game Boy (GBS), Master System e Mega Drive (VGM, VGZ, GYM), PC Engine (HES), MSX (KSS), ZX Spectrum (AY), Atari (SAP) |
| `atariaudio` | AtariAudio 1.26 | Atari ST: SNDH, con il replayer originale su un 68000 emulato (YM2149, timer, DAC dell'STE, blitter), e YM (YM2–YM6, Digimix, YM Tracker), anche compressi con ICE! o LHA |
| `psf`     | Highly Experimental (Neill Corlett, commit di gennaio 2022), psflib, OpenBIOS di PCSX-Redux | PlayStation: PSF e MiniPSF con le loro librerie, emulando la CPU R3000 e lo SPU. Al posto della BIOS di Sony gira OpenBIOS: non serve nessuna ROM |
| `psf2`    | Highly Experimental, psflib, i moduli IOP di ps2sdk v2.0.0 | PlayStation 2: PSF2 e MiniPSF2 con le loro librerie, emulando l'IOP e lo SPU2. Anche qui niente BIOS di Sony: l'IOP parte con i moduli di sistema liberi di ps2sdk |
| `ssf`     | Highly Theoretical (Neill Corlett, commit di gennaio 2022), con il 68000 C68K di Yabause | Saturn (SSF, il 68000 e lo SCSP) e Dreamcast (DSF, l'ARM7 e l'AICA), con le loro librerie |
| `qsf`     | Highly Quixotic (Neill Corlett, commit di gennaio 2022), con il QSound di superctr | le schede arcade Capcom con il QSound (CPS-1 e CPS-2): QSF, con lo Z80 e la cifratura Kabuki |
| `2sf`     | vio2sf (DeSmuME, commit del 2019) | Nintendo DS: 2SF, con le sue librerie e lo stato salvato |
| `gsf`     | viogsf (VBA-M e l'APU di Blargg, commit del 2018) | Game Boy Advance: GSF, con le sue librerie, anche multiboot |
| `usf`     | lazyusf2 (Mupen64Plus, commit di marzo 2022) | Nintendo 64: USF, con le sue librerie, con l'RSP che esegue il microcodice audio del gioco |
| `xa`      | (di megaplayer)       | l'audio in streaming dei CD della PlayStation: XA ADPCM, in RIFF/CDXA o in settori grezzi, un sottobrano per canale |
| `vgmstream` | vgmstream r2117     | l'audio in streaming dei giochi: centinaia di formati delle console (GENH, VAG, SVAG, ADS/SS2, NPSF, DSP, ADX, BRSTM, HCA, ...), anche i `.int` della PS2 senza intestazione. Solo i codec che vgmstream ha dentro: niente MP3, Vorbis, ATRAC3/9, XMA e Opus |
| `88emu`   | 88emu di gearmulator (commit del 10 ottobre 2026) | gli stessi MIDI sui moduli Roland: Sound Canvas (SC-55, SC-55mkII, SC-88, SC-88VL, SC-88 Pro, SC-8850, ...) e MT-32/CM-32L, emulati facendo girare il loro firmware. Servono le ROM dell'utente |
| `fluidsynth` | FluidSynth 2.6.1, GeneralUser GS 2.0.3 | gli stessi MIDI su un SoundFont General MIDI e GS a campioni, per chi non ha le ROM: di serie GeneralUser GS, che viaggia con le app |
| `adlmidi` | libADLMIDI 1.6.2.1    | MIDI (MID, RMI) e i suoi parenti dei giochi DOS: XMI, MUS di Doom, HMI/HMP, GMF, su quattro chip FM OPL3 emulati (Nuked OPL3) |
| `adplug`  | AdPlug 2.4, libbinio 1.5 | una cinquantina di formati AdLib dei giochi e dei tracker DOS (RAD, HSC, D00, A2M, CMF, IMF/WLF, DRO, ROL, LDS, SA2, AMD, XAD, Westwood ADL, ...) e i VGM per i chip OPL, su Nuked OPL3 |
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
in più file, che UADE cerca uno accanto all'altro. I nomi in Latin-1 (archivi
Amiga, CD) si convertono in UTF-8, e un file che non si riesce a estrarre si
salta senza perdere il resto dell'archivio. libarchive non legge gli archivi
LZX dell'Amiga. Fanno eccezione i `.ym` dell'Atari ST, quasi tutti compressi
con LHA: un archivio con dentro il solo brano, che AtariAudio suona così
com'è.

I replayer di questi formati in due file ricavano il nome del secondo dal
primo, con la convenzione dell'Amiga: `mdat.Brano` cerca `smpl.Brano`. Le
collezioni moderne conservano i TFMX anche come `Brano.tfx` + `Brano.sam`, o
in un file solo (TFMX-MOD `.tfm`, con titolo, autore e sottobrano, e
TFMXPAK): megaplayer li presenta a UADE come la coppia originale e gli passa
i campioni quando li chiede ([src/decoders/TfmxFiles.cpp](src/decoders/TfmxFiles.cpp)).
La metà con i campioni, scelta da sola, viene saltata.

I brani da console e del C64 spesso girano per sempre. gme li chiude alla
durata indicata nel file, o dopo due giri del ritornello, o dopo due minuti e
mezzo; i SID, che non dicono mai quanto durano, dopo tre minuti. Sempre con
una dissolvenza. I SNDH dell'Atari ST finiscono alla durata del file, o a
quella dell'archivio di durate di AtariAudio; se nessuno dei due la sa, dopo
tre minuti con la dissolvenza, come i SID; così anche i PSF, che di solito
la durata e la dissolvenza la scrivono nei tag. Gli stream dei giochi con un
loop (vgmstream) fanno due giri del ritornello e sfumano in dieci secondi,
come nella CLI di vgmstream. Per i SID non servono le ROM
del C64: libsidplayfp ha un kernal sostitutivo, che basta quasi sempre (le ROM
originali sono protette da copyright).

Nemmeno per la PlayStation serve la BIOS di Sony. Un PSF è il programma del
gioco che suona la musica, e quel programma chiama le funzioni della BIOS:
Highly Experimental, l'emulatore più fedele dello SPU, di solito si porta
dietro un'immagine ricavata da quella della PS2. megaplayer invece avvia
OpenBIOS di PCSX-Redux (MIT, riscritta da zero), che viaggia dentro la
libreria; Highly Experimental la ferma, come fa con quella di Sony, appena il
kernel è pronto, prima della shell, e lì carica il brano. Per farla girare
serve una piccola patch ([patches/highly-experimental-openbios.patch](patches/highly-experimental-openbios.patch)):
il punto dove fermarla si dà da fuori, e le scritture fatte con la cache
isolata dal bit IsC del registro di stato non arrivano alla RAM, come sulla
console (OpenBIOS svuota la cache così, quella di Sony in un altro modo).

Neanche per i PSF2 della PS2 serve la BIOS. Un PSF2 non è un programma ma un
piccolo filesystem: psf2.irx, il driver audio del gioco e i suoi campioni.
Lo legge l'IOP, il processore che sulla PS2 fa l'audio, e per farlo gli serve
il suo sistema operativo, una ventina di moduli che stanno nella BIOS (la
memoria, i thread, gli interrupt, i timer, i file, il caricatore dei
moduli). Highly Experimental li prende da hebios, ricavata dalla BIOS di una
PS2, con in più versioni di quei moduli che non sono in nessuna BIOS in
commercio. megaplayer usa invece quelli di ps2sdk (Academic Free License
2.0, riscritti da zero): li fa partire da una ROM tutta sua, con sei
istruzioni al reset e i moduli in fila, e il lavoro che sulla console fa
IOPBOOT (caricare sysmem e loadcore e passargli l'elenco dei moduli) lo fa il
decoder ([src/decoders/Psf2Decoder.cpp](src/decoders/Psf2Decoder.cpp)). Al
posto del modulo che in hebios collega l'IOP all'emulatore c'è hefile.irx
([src/ps2/hefile](src/ps2/hefile)), che dà all'IOP i file del PSF2 e avvia
psf2.irx. Molti moduli di ps2sdk sono decompilazioni mai provate così:
[patches/ps2sdk-iop.patch](patches/ps2sdk-iop.patch) corregge quello che
impediva l'avvio e la musica (in loadcore, modload, threadman, sysmem,
sysclib, intrman e timrman). I moduli compilati stanno in
[src/ps2/irx](src/ps2/irx), con la licenza di ps2sdk;
[scripts/build-ps2-iop.sh](scripts/build-ps2-iop.sh) li rifà, con la
toolchain della release v2.0.0 di ps2dev, identici. Anche Highly Experimental
ha bisogno di una seconda patch
([patches/highly-experimental-ps2.patch](patches/highly-experimental-ps2.patch)):
ogni stato può avere la sua BIOS, così la ROM della PS2 sta accanto a
OpenBIOS; gli scorrimenti a quantità variabile usano solo i 5 bit bassi, come
l'R3000 (il codice di GCC ci conta); i registri degli indirizzi dello SPU2
tengono solo i 4 bit alti che ha la console, invece di leggere fuori dalla sua
RAM. Saturn, Dreamcast e QSound una BIOS non la vogliono: il programma
del brano basta. Il 68000 del Saturn è C68K di Yabause, perché il Musashi che
Highly Theoretical ha accanto ha una licenza solo non commerciale; i suoi
opcode passano per due `switch` annidati invece che per uno solo
([patches/highly-theoretical.patch](patches/highly-theoretical.patch)), che in
WebAssembly sarebbe una tabella di salto più lunga di quanto i browser
accettino. Nemmeno DS, GBA e N64 vogliono BIOS: vio2sf e viogsf ne emulano le
chiamate, e i brani dell'N64 sono uno stato salvato del gioco già avviato. I
SNSF del Super Nintendo restano fuori: l'emulatore che li suona, Snes9x, ha una
licenza non commerciale.

Molti giochi della PS2 non suonano la musica con lo SPU ma la leggono dal
disco già pronta, in stream: di questi non esistono PSF2, solo gli stream.
I rip li conservano nel formato del gioco o in un GENH, un'intestazione
generica che i ripper mettono davanti ai dati grezzi (canali, frequenza,
interleave, loop, codec); li suona vgmstream. I `.int` invece sono PCM a 16
bit senza nessuna intestazione: vgmstream li riconosce solo dall'estensione e
li suona con la convenzione dei giochi PS2 che li usano (stereo, interleave di
0x200 byte, 48 kHz), scartando quelli che sembrano PS-ADPCM. Un `.int` con
un'altra frequenza suonerebbe stonato.

Con le ROM dei moduli Roland il MIDI suona come lo sentivano i compositori,
su un Sound Canvas o un MT-32 (vedi sotto, «MIDI sui moduli Roland»). Senza,
suona sul SoundFont GeneralUser GS con FluidSynth, o, se l'utente lo
preferisce, come su una Sound Blaster: libADLMIDI usa i banchi di timbri FM
dei giochi, scelti secondo il formato. I giochi con la libreria Miles (XMI)
usavano il banco AIL, Doom (MUS) quello DMX di Bobby Prince, i giochi con la
libreria HMI il suo; per il MIDI generico c'è il banco General MIDI DMXOPL3.
I formati AdLib quasi sempre si riconoscono solo dall'estensione. Alcuni
vogliono un file di timbri accanto al brano (`standard.bnk` per ROL, `.snd`
o `.tim` per MUS di AdLib, `insts.dat` per KSM, `patch.003` per Sierra), che
AdPlug cerca nella stessa cartella; da soli vengono saltati. Gli S3M con
strumenti AdLib restano a libopenmpt, gli MDI (MIDI con dentro i timbri
AdLib) vanno ad AdPlug, anche quando sono stati rinominati `.mid`: si
riconoscono dagli eventi AdLib che contengono, e senza i loro timbri un modulo
General MIDI li suonerebbe tutti col pianoforte.

Quando più backend riconoscono un file vince quello col punteggio più alto
(`openmpt` per i formati che conosce, perché è più veloce, conosce la durata
dei brani e fa seek immediato). Se l'apertura fallisce si prova il successivo.
Con `-b` si sceglie a mano.

## Build

[![build](https://github.com/jugstries/megaplayer/actions/workflows/build.yml/badge.svg)](https://github.com/jugstries/megaplayer/actions/workflows/build.yml)

Serve un compilatore C++20 (Clang o GCC; su Windows MinGW-w64), CMake ≥ 3.21,
Ninja, pkg-config, git e Python 3 per i test. libopenmpt, libarchive, LAME
(per esportare in MP3) e zlib (per i PSF) arrivano dal sistema:

| Sistema | Dipendenze |
|---|---|
| macOS | `brew install cmake ninja libopenmpt libarchive lame` |
| Debian/Ubuntu | `apt install cmake ninja-build pkg-config libopenmpt-dev libarchive-dev libmp3lame-dev zlib1g-dev` |
| Windows (MSYS2, ambiente UCRT64) | `pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,python,libopenmpt,libarchive,lame,zlib}` |

Poi, uguale ovunque (su Windows dalla shell di MSYS2):

```bash
scripts/build-deps.sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

`scripts/build-deps.sh` scarica a versioni fissate i sorgenti di UADE,
libzakalwe, bencode-tools, ancient, miniaudio, game-music-emu, libsidplayfp,
libresidfp, libADLMIDI, AdPlug, libbinio, AtariAudio, vgmstream e SQLite in `third_party/src` (di
libsidplayfp, libresidfp e SQLite gli archivi di rilascio, con la verifica
dello SHA-256), gearmulator, Highly Experimental, Theoretical e Quixotic,
psflib, vio2sf, viogsf e lazyusf2 a un commit fissato
(di gearmulator i soli sottomoduli che servono a 88emu), FluidSynth, il
SoundFont GeneralUser GS e OpenBIOS (dai mirror di Libreboot; entrambi con lo
SHA-256), e applica le patch di `patches/`: due a UADE (l'emulatore nel
processo del player, vedi sotto, e le voci di Paula da zittire), una a
libsidplayfp (il mute di una voce subito), due a gearmulator (MinGW), una
per ciascuno dei nuclei di Neill Corlett (OpenBIOS, il mute dei canali, C68K
fuori da x86), una seconda a Highly Experimental (la PS2) e una per vio2sf,
viogsf e lazyusf2 (MinGW, ARM e i compilatori recenti). Tutto il resto lo fa CMake: compila
UADE senza il suo script di configurazione (che funziona solo su UNIX, vedi
[cmake/UadeInProcess.cmake](cmake/UadeInProcess.cmake)) e copia replayer e
configurazione in `build/uade-data`. La CLI e i test li cercano lì; le app li
indicano a runtime con `mp_set_uade_data_dir()`.

La CI di GitHub Actions ([.github/workflows/build.yml](.github/workflows/build.yml))
compila e prova tutto su Linux, Windows e WebAssembly (con Node); macOS si
prova in locale.

Per controllare la build Windows da macOS senza aspettare la CI c'è la
cross-compilazione con MinGW-w64 (`brew install mingw-w64`), senza libopenmpt
e libarchive; i generatori dell'emulatore 68000 vengono dalla build nativa:

```bash
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DMP_UADE_HOST_TOOLS=$PWD/build -DMP_WITH_OPENMPT=OFF -DMP_WITH_ARCHIVES=OFF
```

### WebAssembly

Con Emscripten (`brew install emscripten`, o emsdk):

```bash
scripts/build-wasm.sh
ctest --test-dir build-wasm        # CLI e test con Node
python3 scripts/serve-web.py       # pagina di prova: http://127.0.0.1:8765/
```

`build-wasm/megaplayer_web.*` è il modulo per il browser: l'API C di
[src/capi/megaplayer.h](src/capi/megaplayer.h) con i replayer di UADE nel
filesystem virtuale. [web/megaplayer-web.js](web/megaplayer-web.js) la avvolge
in JavaScript, e [web/index.html](web/index.html) è una pagina di prova con
coda, comandi e oscilloscopio. UADE e il motore di riproduzione girano in web
worker (pthread di Emscripten); le pthread vogliono `SharedArrayBuffer`, cioè
una pagina isolata cross-origin: `scripts/serve-web.py` manda le intestazioni
COOP/COEP, e un hosting vero dovrà fare lo stesso.

Per ora nella build web suonano UADE e i file audio di miniaudio: libopenmpt e
libarchive vanno ancora compilate per WebAssembly. L'audio coincide con quello
nativo salvo scarti di 1-2 unità su 65536 nel 5% dei campioni: su Apple Silicon
il compilatore fonde moltiplicazioni e somme (FMA), WebAssembly no.

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
In WebAssembly il thread è un web worker (vedi sotto, WebAssembly).

libuade registra in `~/.uade/contentdb` la durata dei brani suonati fino in
fondo e la mostra alle riproduzioni successive. È un solo valore per file:
per i file con più sottobrani megaplayer lo ignora.

## Uso

```bash
build/megaplayer play mod.enigma AHX.Cruisin   # riproduce
build/megaplayer play Stormlord.lha            # tutti i brani dell'archivio
build/megaplayer info -b uade mod.enigma       # formato, backend, metadati
build/megaplayer render -s 2 -o out.wav Stormlord.lha/Stormlord/jt.ingame   # esporta in WAV
build/megaplayer play -m 60 Stormlord.lha      # con MegaSound al 60%
build/megaplayer play --roms ~/Roms/Roland --roland SC-88Pro brano.mid   # MIDI su un Sound Canvas
build/megaplayer render --from 600 -t 30 -o pezzo.wav brano.mid    # 30 secondi dal minuto 10
build/megaplayer render -m 60 -M 2 -o brano.mp3 mod.enigma   # MP3 con MegaSound, senza il canale 2
build/megaplayer render -M 1,3 -o out.wav mod.enigma   # senza i canali 1 e 3
build/megaplayer backends
```

`ARCHIVIO/percorso/interno` sceglie un file o una cartella dentro un
archivio.

Durante `play`: spazio pausa, ←/→ seek di 10 s, `n`/`p` sottobrano
successivo/precedente, invio file successivo, `+`/`-` MegaSound ±10%, `m`
confronto A/B con l'originale, da `1` a `9` e `0` zittisce o riaccende i
canali da 1 a 10, `q` esci.

## Canali

Ogni brano dei formati che lo permettono ha i suoi canali, da zittire uno
per uno o da mettere in solo (`Decoder::channels()` e `setChannelMuted()`):

| Backend  | Canali | Come si zittiscono |
|----------|--------|--------------------|
| openmpt  | i canali del tracker, con il nome dato dall'autore | il mute di libopenmpt |
| uade     | le 4 voci di Paula (1 e 4 a sinistra, 2 e 3 a destra) | una maschera nel mixer di Paula ([patches/uade-voice-mask.patch](patches/uade-voice-mask.patch)): l'emulazione va avanti |
| gme      | le voci del chip («Square 1», «Noise», «DSP 3»…) | il mute di gme, che con canali zittiti non chiude più il brano sul silenzio |
| sid      | 3 voci per chip e i campioni digitalizzati | il mute di libsidplayfp, che filtra i registri; la patch ([patches/libsidplayfp-instant-mute.patch](patches/libsidplayfp-instant-mute.patch)) lo applica subito invece che alla nota dopo |
| atariaudio | le 3 voci del YM2149 e, nei SNDH, il DAC dell'STE (nei YM Digimix e YM Tracker i campioni) | il mute di AtariAudio, rimesso a ogni cambio di sottobrano |
| psf      | le 24 voci dello SPU | il mute di Highly Experimental, rimesso quando il brano riparte |
| psf2     | le 48 voci dello SPU2 (24 per core) | come per il PSF |
| ssf      | i 32 slot dello SCSP, i 64 canali dell'AICA | una maschera nel mixer di Highly Theoretical: il canale si genera ancora, per la modulazione FM degli altri |
| qsf      | le 16 voci del QSound | il mute del QSound di superctr, che con la patch lascia avanzare la voce |
| 2sf      | i 16 canali del DS | il mute di vio2sf |
| gsf      | le due onde quadre, l'onda, il rumore e i due canali PCM | il mute dell'APU di viogsf |
| 88emu, fluidsynth | i 16 canali MIDI (nei file per SC-88 sulle due porte) | le note del canale non partono più e quelle accese si spengono (pedale, All Notes Off, All Sound Off) |
| adlmidi  | i 16 canali MIDI | i canali spenti del sequencer, che rilascia le note |
| adplug   | i canali OPL che suonano note | niente note nuove, quelle accese rilasciate e operatori al minimo (-47 dB) |

I file audio non hanno canali, e nemmeno i brani dell'N64, che mescola le voci
nel microcodice dell'RSP. Il mute vale fino al brano dopo, anche
cambiando sottobrano; nell'app si sente con il ritardo del buffer, circa tre
decimi di secondo. Riaccesi, i canali OPL e SID ripartono dalla nota tenuta,
quelli MIDI dalla nota dopo.

## MIDI sui moduli Roland

Il backend `88emu` suona i MIDI su 88emu di
[The Usual Suspects](https://theusualsuspects.io/downloads/88emuplayer)
([gearmulator](https://github.com/dsp56300/gearmulator)), che non campiona i
moduli Roland ma ne fa girare il firmware originale su CPU e chip sonori
emulati: Sound Canvas dall'SC-55 all'SC-8850, MT-32, CM-32L e parenti.
megaplayer usa la sua libreria, 88lib, attraverso l'interfaccia C, e gli
manda gli eventi con il sequencer di libADLMIDI, che legge anche XMI, MUS,
HMI e HMP ([src/decoders/Emu88Decoder.cpp](src/decoders/Emu88Decoder.cpp)).

Le ROM non sono incluse (sono di Roland) e non si copiano: le fornisce ogni
utente, da una cartella che indica (nell'app in «Impostazioni», da riga di
comando con `--roms CARTELLA`). 88emu le riconosce dal contenuto, non dal
nome, anche dentro le sottocartelle, e guarda già da solo nella cartella di
88emuPlayer (`~/Documents/The Usual Suspects/88emuPlayer` e simili). Senza le
ROM il backend non riconosce niente e i MIDI restano ad `adlmidi`.

Il modulo si sceglie da solo: un brano che manda SysEx all'MT-32 (Roland,
modello 16h, come i giochi Sierra e LucasArts) va a un modulo della sua
famiglia, il resto al Sound Canvas scelto (`--roland SC-88Pro`, o
nell'app), altrimenti al primo con le ROM: SC-55mkII, SC-88 Pro, SC-88VL,
SC-88, SC-8850... `megaplayer backends` dice quali sono pronti. Il modulo si
accende al primo audio (da mezzo secondo a poco più di uno, il tempo del suo
avvio), così la libreria che legge le informazioni dei file non lo avvia mai,
e resta acceso per il brano dopo con un reset. Su un Mac con Apple Silicon
l'SC-55mkII va una quarantina di volte più veloce del tempo reale, l'SC-8850
una quindicina. I sedici canali si zittiscono come quelli di libADLMIDI; le
due porte dei file per SC-88 vanno alle due porte del modulo.

gearmulator si compila a parte ([CMakeLists.txt](CMakeLists.txt), con
`ExternalProject`), con il suo CMake e solo quello che serve a 88lib, in una
libreria statica unica. Su Windows lo si compila con MinGW grazie a due
piccole patch ([patches/gearmulator-mingw.patch](patches/gearmulator-mingw.patch),
[patches/gearmulator-dsp56300-mingw.patch](patches/gearmulator-dsp56300-mingw.patch)).
I test che suonano davvero vogliono le ROM: girano se
`MEGAPLAYER_ROLAND_ROMS` indica la cartella che le contiene quando si
configura CMake, e in CI no.

Senza le ROM i MIDI suonano su un SoundFont con FluidSynth (backend
`fluidsynth`, [src/decoders/FluidSynthDecoder.cpp](src/decoders/FluidSynthDecoder.cpp)):
di serie [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS),
General MIDI e GS, 32 MB, che `build-deps.sh` scarica e le app si portano
dentro (la sua licenza ne permette l'uso nei programmi). Se ne può scegliere
un altro (`.sf2`; i `.sf3` compressi no: FluidSynth è compilato senza
libsndfile), con `--soundfont FILE` o nelle impostazioni dell'app, oppure
tornare all'FM di `adlmidi` (`--soundfont ""`). FluidSynth si compila a
parte, statico e senza driver audio né altro di quello che il suo CMake
cercherebbe sulla macchina. Il SoundFont si carica al primo audio e resta
caricato per il brano dopo; il volume è allineato a quello di 88emu e
dell'FM.

88emu e FluidSynth condividono il sequencer
([src/decoders/SequencedMidiDecoder.cpp](src/decoders/SequencedMidiDecoder.cpp)).
Per un salto il sequencer riparte dall'inizio del brano, ma non manda al
sintetizzatore le migliaia di messaggi incontrati fin lì: rimette il
sintetizzatore come appena acceso e gli manda solo lo stato al punto
d'arrivo, i SysEx dopo l'ultimo reset (dei DT1 Roland l'ultimo per
indirizzo), poi per ogni canale banco, programma, controller, parametri RPN e
NRPN e pitch bend. Ai moduli emulati arriva al ritmo del cavo MIDI, come a
quelli veri, che altrimenti ne perderebbero pezzi.

## Esportazione

Il brano che suona si esporta in WAV (PCM a 16 bit) o in MP3 com'è in quel
momento: il sottobrano, i canali zittiti o in solo, la MegaSound. Da riga di
comando lo fa `render` con `-o brano.wav` o `-o brano.mp3`, `-m`, `-M`,
`--from` e `-t`; nell'app il pulsante con la freccia nel riquadro dei
sottobrani (moderna) o il menu «Esporta…» (retrò), che chiede il formato,
se tenere MegaSound e canali, e dove salvare. Il core fa il render a parte,
più veloce del tempo reale, in un thread suo
([src/core/Exporter.cpp](src/core/Exporter.cpp)); la riproduzione si ferma,
perché UADE suona un brano per volta, e alla fine riprende dallo stesso
punto con gli stessi canali. Senza effetti il WAV è identico al bit a quello
che il decoder produce. Un brano che non dice quanto dura si ferma dopo un
quarto d'ora.

L'MP3 lo scrive LAME ([src/core/Mp3Writer.cpp](src/core/Mp3Writer.cpp)):
VBR alla qualità più alta (V0, intorno ai 245 kbit/s), tag ID3v2 in UTF-16
con titolo (o il nome del file), autore e album (l'archivio e la cartella
del brano), e il tag LAME con la durata esatta, così si sente senza silenzi
in più e i lettori mostrano la durata giusta.

## Libreria

Una cartella radice con tutta la musica, indicizzata in un database SQLite
([src/library/Library.cpp](src/library/Library.cpp)): per ogni brano, anche
dentro gli archivi, formato, piattaforma, titolo, autore e sottobrani con le
durate. Si cerca per parole (in titolo, autore, nome del file e cartella) e si
filtra per piattaforma e formato; i conteggi di ogni filtro tengono conto
dell'altro. Si naviga anche per cartelle, con gli archivi che fanno da
cartella, e la ricerca vale da una cartella in giù.

```bash
build/megaplayer library scan ~/Musica/Moduli    # sceglie la cartella e la indicizza
build/megaplayer library scan                    # aggiorna: legge solo i file nuovi o cambiati
build/megaplayer library search jeroen tel       # cerca
build/megaplayer library search -p amiga -f TFMX-Pro
build/megaplayer library search -d "TFMX Professional" apidya
build/megaplayer library stats                   # brani per piattaforma e formato
```

La scansione apre solo i file nuovi o cambiati (per data e dimensione) e
toglie quelli spariti: la prima su una raccolta grande richiede un po'
(3700 brani, compresa un'immagine ISO da 550 MB, in 12 s), le successive
pochi decimi di secondo. Se cambiano i backend compilati, o come si leggono
i file, la scansione successiva rilegge tutto: così i file che prima non si
riconoscevano entrano quando arriva il formato giusto. Per i formati Amiga
basta il riconoscimento di UADE, senza avviare l'emulatore (che ha una sola
istanza, e serve alla riproduzione): formato e replayer sì, titolo e
sottobrani si sanno solo suonando il brano.

Le piattaforme le dichiarano i backend ([src/core/Platform.h](src/core/Platform.h)):
Amiga, PC / DOS, Commodore 64, NES, SNES, Game Boy, Sega, PC Engine, MSX, ZX
Spectrum, Atari 8 bit, Atari ST, PlayStation, PlayStation 2, Saturn,
Dreamcast, Arcade, Game Boy Advance, Nintendo DS, Nintendo 64 e file audio. Tra i moduli tracker, MOD, MED,
OKT e gli altri nati sull'Amiga vanno all'Amiga, S3M, XM e IT al PC.

Il database sta nella cartella dei dati dell'utente (`~/Library/Application
Support/megaplayer/library.db` su macOS, `%APPDATA%\megaplayer` su Windows,
`~/.local/share/megaplayer` altrove), o dove dice `--db`. Non c'è nella
build WebAssembly.

Nello stesso database stanno le playlist, la coda dell'app e le sue
impostazioni. Una voce di playlist è un brano intero, che suona tutti i
sottobrani in fila, o un suo sottobrano; lo stesso brano entra più volte solo
con sottobrani diversi. I brani si ritrovano per riferimento: il percorso, o
`ARCHIVIO/percorso/interno` per quelli dentro un archivio, così coda e
playlist sopravvivono alla chiusura dell'app. Per suonarne uno il core estrae
solo la sua cartella dell'archivio, dove stanno anche i file che lo
accompagnano, e tiene le ultime otto cartelle estratte. L'app usa tutto
attraverso l'API C (`mp_library_*` in [src/capi/megaplayer.h](src/capi/megaplayer.h)).

Le playlist entrano ed escono in M3U ([src/library/M3u.cpp](src/library/M3u.cpp)).
L'export scrive un M3U8 in UTF-8 con `#EXTINF` (durata, autore e titolo dalla
libreria) e i percorsi relativi per i file che stanno nella cartella della
playlist o sotto, completi per gli altri; i brani dentro un archivio hanno il
loro riferimento `ARCHIVIO/percorso/interno`, che capisce solo megaplayer. Il
sottobrano va in una riga `#MEGAPLAYER-SUBSONG:N` (da 1), che gli altri lettori
ignorano, suonando il file intero. L'import legge anche gli M3U degli altri
lettori: in Latin-1 se non sono UTF-8, con le barre di Windows, con gli
indirizzi `file://`; salta gli indirizzi web e i file che non trova.

La raccolta dei brani da una cartella (quando la si trascina sulla finestra)
prende i suoi file e quelli delle sottocartelle, in ordine, archivi compresi,
senza quelli nascosti e quelli che nessun backend riconosce, come fa la
libreria.

UADE riconosce molti formati anche solo dal nome, col prefisso dell'Amiga
(`mod.`, `cp.`, `hip.`) o con l'estensione: così sui CD di moduli prendeva
per brani anche i programmi e i loro file. Quando il nome è l'unico indizio,
megaplayer scarta i file di testo, le icone del Workbench (`.info`), gli
eseguibili del DOS e di Windows, i WAD, le immagini, gli archivi, i PDF e la
configurazione di ProTracker.

## MegaSound

Una sola manopola, da 0 a 100%, che aggiunge effetti a mano a mano che gira,
come un plugin a manopola singola
([src/dsp/MegaSound.cpp](src/dsp/MegaSound.cpp)). La catena:

1. passa-alto a 25 Hz;
2. compressore stereo con ginocchio morbido: soglia da −6 a −24 dB, rapporto
   fino a 5:1;
3. in sovracampionamento 2× (contro l'aliasing): brillantezza (armoniche
   sopra i 3 kHz), saturazione asimmetrica (piena dal 50%), corpo (dal 50%:
   una saturazione in più solo sotto i 300 Hz, riportata poi al suo volume
   medio, che rende il basso più denso e caldo senza sporcare gli alti) e
   clipper morbido (dal 70%), tarato sul tetto finale: il margine di volume
   lo spinge in saturazione invece di lasciare tutto al limiter;
4. riverbero avvolgente senza bassi (la mandata parte da 350 Hz): entra dal
   10%, sale in fretta nella prima metà e poi più piano fino in fondo, con
   stanza sempre più grande (coda da 1.2 a 2.1 s) e 20 ms di pre-delay che lo
   staccano dal suono diretto. Fino al 60% è scuro (mandata tagliata sopra i
   2.7 kHz, alti della coda smorzati in fretta), poi si schiarisce fino a
   5.5 kHz: a fine corsa la coda è più lunga e più forte, e scura
   suonerebbe cupa. Rispetto al suono che lo alimenta sta tra −15 e −18 dB
   al 50% e tra −10 e −12 dB al 100%;
5. volume: una compensazione restituisce subito quello che toglie il
   compressore (calcolata, alle impostazioni di quel momento, sull'energia
   del segnale nell'ultimo secondo e mezzo), il guadagno automatico corregge
   il resto su una media di ~1,5 s, e poi si aggiungono fino a +3.5 dB alla
   fine della corsa (`kLoudnessBonusDb`), perché un po' più forte suona
   meglio. Così girando la manopola il volume sale e basta, senza buchi
   mentre il guadagno automatico si adegua. Il volume dell'originale si
   misura dopo il passa-alto: certi brani (Turrican 2, AHX.Cruisin) hanno
   molta corrente continua, che non si sente;
6. miscela con il segnale originale, ritardato quanto il sovracampionamento;
7. lo Spazio (sotto), fin dal primo tratto di manopola;
8. limiter con 3 ms di lookahead, tetto a −0.5 dBFS. Latenza totale: 31 + 144
   frame a 48 kHz.

Lo **Spazio** entra con la manopola e a circa il 12% è già pieno: un ramo
parallelo che parte dall'originale, tiene solo le frequenze sopra gli 800 Hz
(24 dB/ottava), le comprime molto (soglia −30 dBFS, rapporto 10:1: tira su
dettagli e code) e le allarga in mid/side: il lato stereo esistente si
rinforza e un lato nuovo nasce dal centro passato in quattro allpass, così si
aprono anche i brani mono. Il ramo si riporta da solo al volume
dell'originale ed entra nella somma 9 dB sotto (`kSpaceLevelDb`): l'effetto è
simile sui brani piani e su quelli già molto forti. In mono la parte larga si
annulla e resta solo un po' di centro compresso.

Le soglie e le quantità sono costanti all'inizio del file. Spenta (manopola a
zero) la catena passa solo dai ritardi, senza effetti: l'uscita
è l'originale ritardato, identico al bit, e accendere non fa saltare l'audio.
L'export in WAV, dove il ritardo non serve, la salta del tutto.

Misure su 40 s di brani Amiga (larghezza: energia del lato stereo rispetto al
centro):

| | originale | 25% | 50% | 100% |
|---|---|---|---|---|
| Stormlord `jt.title` | −10.2 dB | −7.6 dB | −7.6 dB | −6.9 dB |
| Unreal `jt.title_and_game_end` | −10.7 dB | −7.6 dB | −7.4 dB | −6.6 dB |
| Turrican 2 Load | −14.4 dB | −7.5 dB | −7.4 dB | −6.9 dB |
| AHX.Cruisin | −12.4 dB | −7.1 dB | −6.9 dB | −6.4 dB |

Loudness percepita (LUFS, BS.1770) rispetto all'originale:

| | 25% | 50% | 75% | 100% |
|---|---|---|---|---|
| Stormlord `jt.title` | +1.5 dB | +2.5 dB | +3.7 dB | +4.8 dB |
| Unreal `jt.title_and_game_end` | +0.9 dB | +1.8 dB | +2.7 dB | +3.6 dB |
| Turrican 2 Load | +1.6 dB | +2.4 dB | +3.2 dB | +4.0 dB |
| AHX.Cruisin (già molto forte) | +1.3 dB | +2.1 dB | +2.5 dB | +2.9 dB |

Tutta la catena costa circa l'1.5% di un core.

La manopola c'è nella CLI (`-m`, tasti `+`/`-` e `m`), nelle due skin
dell'app (si gira trascinando, con la rotella o con le frecce; doppio clic
per azzerarla; A/B per sentire l'originale; le spie mostrano quali effetti
stanno lavorando) e nella pagina WebAssembly.

## App desktop (Flutter)

In `app/` c'è l'app Flutter, per macOS e Windows, con la skin moderna (scura,
gradienti arancio e viola) e quella retrò (Mac a 1 bit); si passa dall'una
all'altra dall'interfaccia. Parla con il core attraverso l'API C di
[src/capi/megaplayer.h](src/capi/megaplayer.h), compilata in
`libmegaplayer.dylib`: un passo di build di Xcode
([scripts/embed-core-macos.sh](scripts/embed-core-macos.sh)) la compila con
CMake e la copia, firmata, dentro l'app. Su Windows `libmegaplayer.dll`, le
DLL di MSYS2 da cui dipende e i replayer di UADE (`uade-data`) stanno accanto
a `megaplayer.exe`; l'app Windows si compila solo su Windows, e lo fa la CI
delle release (vedi sotto). Il runner in `app/windows` ha gli stessi canali
di quello di macOS (finestre per aprire file e cartelle, icona che segue la
skin) e chiede a Windows le API a 8 bit in UTF-8, come i percorsi che l'app
passa al core. Il core decodifica in un thread suo
([src/core/Player.cpp](src/core/Player.cpp)); l'app legge lo stato circa 30
volte al secondo.

Ogni skin ha la sua icona: la manopola MegaSound per la moderna, la mascotte
floppy per la retrò. L'icona dell'app è la moderna; con la skin retrò l'app
cambia l'icona nel Dock (o nella barra delle applicazioni) finché resta
aperta. Le disegna [app/tool/make_icons.py](app/tool/make_icons.py) (serve
`resvg`), che le scrive in `app/macos/Runner/Assets.xcassets` e, come `.ico`,
in `app/windows/runner/resources`.

Serve Flutter (≥ 3.47) con Xcode, dopo `scripts/build-deps.sh`:

```bash
cd app
flutter run -d macos
```

Per aprire subito dei file, da un'app già compilata:

```bash
open app/build/macos/Build/Products/Debug/megaplayer.app --args /percorso/Stormlord.lha
```

Nella skin moderna la barra laterale porta a quello che suona, alla libreria
e alle playlist. La libreria chiede la cartella della musica la prima volta,
la legge in background e a ogni avvio guarda solo i file nuovi o cambiati
(«Aggiorna» lo fa a richiesta). Si sfoglia per cartelle, archivi compresi, o
si cerca e si filtra per piattaforma e formato da una cartella in giù. Un
clic su un brano lo suona; il + lo mette in coda, l'altra icona in una
playlist, e dei brani con più sottobrani si apre l'elenco per sceglierne uno.
Coda e playlist si riordinano trascinando le voci dal loro numero; dal menu
di ogni voce si sceglie il sottobrano o la si toglie. «Riproduci» mette una
playlist al posto della coda; dal lettore si aggiunge a una playlist il
sottobrano che suona. L'icona accanto al + delle playlist importa un M3U, e
il menu di una playlist o della coda la esporta.

In tutte e due le skin i file trascinati dal Finder o da Esplora risorse
sulla finestra vanno in coda (delle cartelle, i brani che hanno dentro),
mentre gli M3U diventano playlist, come quelli scelti con «Apri file…» o
passati all'avvio. Il trascinamento arriva dai runner nativi sul canale
`megaplayer/drop` ([app/lib/player/file_drop.dart](app/lib/player/file_drop.dart)).

Nella skin retrò le stesse cose stanno nella finestra sotto «In
riproduzione», con le linguette Coda, Libreria e Playlist (un menu con le
playlist, «Nuova playlist…» e «Importa M3U…»; «ESPORTA M3U…» sta sotto la
coda e sopra le voci di una playlist). La libreria è un elenco alla Finder con
nome, formato e durata: il triangolino apre i sottobrani, piattaforma e
formato si scelgono da menu a comparsa con la spunta. Gli avvisi («Aggiunto
alla coda») li dà la mascotte nella finestra «Ciao!».

Nelle impostazioni (l'icona accanto ad «Apri file…» nella moderna, il menu
«Impostazioni…» nella retrò) si sceglie la cartella con le ROM dei moduli
Roland e il Sound Canvas per i MIDI, e con che cosa suonano i MIDI senza le
ROM: GeneralUser GS, un altro SoundFont o i chip FM. L'app se li ricorda.

In tutte e due le skin, sotto i comandi di riproduzione c'è la fila dei
canali del brano: un clic sul numero zittisce il canale, la S lo mette in
solo (con quelli in solo si sentono solo loro), «Tutti» li riaccende.

Test: `flutter test` in `app/`. `test/skin_screenshots_test.dart` disegna le
skin con un core e un database finti e salva le immagini in
`app/build/screenshots`; `test/modern_skin_test.dart` e
`test/retro_skin_test.dart` usano libreria, coda, playlist, canali e
aggiornamenti con clic e menu; `test/updater_test.dart` prova firma,
download e controlli degli aggiornamenti contro un sito finto in locale;
`test/ffi_core_test.dart` prova i binding con la
libreria vera se `MEGAPLAYER_CORE` indica `libmegaplayer.dylib` (la parte
che suona vuole anche `STORMLORD_LHA`, un archivio di prova).

Limiti attuali:

- su macOS la sandbox è disattivata. Le build di sviluppo puntano a
  libopenmpt e libarchive di Homebrew e ai replayer in `build/uade-data`;
  quella delle release se li porta dentro
  ([scripts/package-macos.sh](scripts/package-macos.sh)), ma con le librerie
  di Homebrew del runner gira solo da macOS 15 in poi e su Apple Silicon. Ed è
  firmata solo "ad hoc": la prima volta macOS la apre con «Apri comunque» in
  Impostazioni di Sistema → Privacy e sicurezza;
- l'installer di Windows non è firmato con un certificato: la prima volta
  SmartScreen avvisa che l'autore è sconosciuto («Ulteriori informazioni»,
  poi «Esegui comunque»). Gli aggiornamenti li scarica l'app, e non passano
  da lì;
- gli oscilloscopi mostrano i canali sinistro e destro dell'uscita: per le
  voci separate (quelle di Paula ora si zittiscono una per una) il core
  dovrebbe darne l'audio a parte;
- dalla libreria alle playlist non si trascina: si aggiunge con l'icona o
  dal menu. Una cartella grande trascinata sulla finestra si legge tutta
  prima di andare in coda, e intanto l'app aspetta.

## Release e aggiornamenti

L'app si aggiorna da sola dal sito: qualche secondo dopo l'avvio legge
`updates/windows.json` o `updates/macos.json` da
`https://www.claudiotassitano.it/projects/megaplayer/` e, se c'è una build
più nuova, la annuncia con le novità (finestra della skin moderna, o avviso
del Mac con la mascotte nella retrò). Si installa solo quando l'utente lo
chiede: l'app scarica l'installer (Windows) o lo zip dell'app (macOS), ne
controlla dimensione, SHA-256 e firma e si chiude. Su Windows lancia
l'installer con `/SILENT`, che alla fine la riapre; su macOS estrae l'app e
lascia a uno script il cambio del bundle (rimettendo la vecchia se qualcosa
va storto) e la riapertura, per cui l'app deve stare in una cartella in cui
può scrivere, come Applicazioni, e non girare dalla copia temporanea che
macOS usa per le app aperte da Download. Il manifesto è
firmato con Ed25519 ([app/lib/update](app/lib/update)): l'app accetta solo
quello che è firmato con la chiave privata, che sta nel Portachiavi del Mac
di chi pubblica (servizio `megaplayer-update-key`) e non va mai in CI né nel
repo. Dalla versione accanto al nome (skin moderna) o da «Aggiornamenti…»
(retrò) si cerca a mano. Le build di sviluppo non cercano niente.

Per pubblicare una versione:

1. alzare `version` in [app/pubspec.yaml](app/pubspec.yaml), per esempio
   `0.2.0+2` (il numero dopo il `+` è la build, e deve crescere);
2. fare il commit e un tag annotato con le novità, che l'app mostra:
   `git tag -a v0.2.0 -m "• Prima novità"`, poi il push del tag;
3. la CI ([.github/workflows/release.yml](.github/workflows/release.yml))
   compila core e app per Windows, con l'installer di Inno Setup
   ([app/windows/installer/megaplayer.iss](app/windows/installer/megaplayer.iss)),
   e per macOS, con lo zip dell'app;
4. `scripts/publish-update.sh v0.2.0` scarica dalla CI le build del commit
   del tag (anche quelle di un branch), le firma e prepara `dist/site`:
   `index.html` (la pagina per scaricarle), i manifesti `updates/*.json`,
   l'installer e lo zip;
5. caricare `dist/site` in `projects/megaplayer/` sul sito, i manifesti per
   ultimi.

La chiave si crea una volta con `dart run tool/update_key.dart` in `app/`,
che stampa la chiave pubblica da scrivere in
[app/lib/update/updater.dart](app/lib/update/updater.dart). Della privata
conviene tenere una copia: senza, le copie già installate non accettano più
aggiornamenti. Per provare l'app contro un altro sito:
`--dart-define=MEGAPLAYER_UPDATES=http://.../updates/` e
`--dart-define=MEGAPLAYER_BUILD=1`.

## Struttura

```
src/core/       interfaccia Decoder, registro dei backend, caricamento e
                decompressione dei file, archivi, motore di riproduzione,
                uscita audio, export WAV
src/decoders/   un file per backend, più tag dei file audio e coppie TFMX
src/dsp/        effetti sull'audio (MegaSound)
src/library/    la libreria: indice SQLite di una cartella, con ricerca
src/uade/       UADE nel processo del player
src/capi/       API C per le interfacce grafiche
src/cli/        la riga di comando
app/            app desktop in Flutter (runner macOS e Windows, installer
                Windows, aggiornamenti in app/lib/update, strumenti di firma
                in app/tool)
web/            pagina di prova della build WebAssembly
tests/          test automatici del core; in tests/data file audio di prova
                (toni generati, rigenerabili con make_audio_fixtures.sh)
cmake/          build di UADE, libsidplayfp e AdPlug dai sorgenti, toolchain
                di cross-compilazione
patches/        modifiche alle dipendenze
scripts/        download delle dipendenze, build WebAssembly, server di prova,
                passo di build dell'app macOS, pacchetto dell'app macOS,
                pubblicazione delle release
.github/        CI per Linux, Windows e WebAssembly; build delle release
```

Per aggiungere un backend: implementare `Decoder` e `DecoderFactory`
(`src/core/Decoder.h`), registrarlo in `src/decoders/BuiltinDecoders.cpp` e
aggiungere un'opzione `MP_WITH_*` in `CMakeLists.txt`.

## Licenze

UADE è GPL-2.0 (il motore UAE) con parti LGPL; i replayer in
`share/uade/players` hanno licenze e autori diversi. libopenmpt è BSD-3,
ancient e libarchive BSD-2, miniaudio e stb_vorbis public domain / MIT-0,
game-music-emu LGPL-2.1 (con l'emulatore Nuked del YM2612, anche lui LGPL),
libsidplayfp e libresidfp GPL-2.0, AdPlug e libbinio LGPL-2.1, AtariAudio
MIT (con il 68000 di Musashi, MIT, e i decompressori ICE!, di pubblico
dominio, e LHA di Haruhiko Okumura); Highly Experimental, Theoretical e
Quixotic GPL-3.0 (con C68K di Yabause, GPL-2.0 o successiva, e il QSound di
superctr e Valley Bell, che in MAME è BSD-3-Clause), vio2sf, viogsf e
lazyusf2 GPL-2.0 o successiva (con l'APU di Blargg, LGPL-2.1 o successiva,
l'RSP di Iconoclast, CC0, e il CIC di X-Scale, BSD-2-Clause), psflib e
OpenBIOS MIT, i moduli IOP di ps2sdk Academic Free License 2.0 (dati che
girano nell'IOP emulato, non codice del player), zlib con la sua licenza;
vgmstream ha una licenza permissiva in stile ISC (con parti di altri autori
sotto licenze simili); gearmulator
(88emu) GPL-3.0, con parti derivate da Nuked-SC55 e Nuked-MT32 sotto GPL-2.0
o successiva; FluidSynth LGPL-2.1 o successiva; LAME LGPL-2.0 o successiva; il SoundFont GeneralUser GS
(S. Christian Collins) ha la sua licenza, che ne permette l'uso nei
programmi, e viaggia accanto a lui nelle app; libADLMIDI ha
parti LGPL, MIT e BSD, ma nel complesso è GPL-3.0 o successiva, e i suoi
banchi di timbri hanno ciascuno le sue note. SQLite è di pubblico dominio.
Finché il backend UADE è incluso, il binario risultante va distribuito sotto
GPL (con 88emu e libADLMIDI, la GPL-3.0). Le ROM dei moduli Roland non sono
incluse.
