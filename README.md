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
LZX dell'Amiga.

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
una dissolvenza. Per i SID non servono le ROM del C64: libsidplayfp ha un
kernal sostitutivo, che basta quasi sempre (le ROM originali sono protette da
copyright).

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
libzakalwe, bencode-tools, ancient, miniaudio, game-music-emu, libsidplayfp,
libresidfp, libADLMIDI, AdPlug, libbinio e SQLite in `third_party/src` (di
libsidplayfp, libresidfp e SQLite gli archivi di rilascio, con la verifica
dello SHA-256), gearmulator a un commit fissato con i soli sottomoduli che
servono a 88emu, FluidSynth e il SoundFont GeneralUser GS (con lo SHA-256), e
applica le patch di `patches/`: due a UADE (l'emulatore nel
processo del player, vedi sotto, e le voci di Paula da zittire), una a
libsidplayfp (il mute di una voce subito) e due a gearmulator (MinGW). Tutto il resto lo fa CMake: compila
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
| 88emu, fluidsynth | i 16 canali MIDI (nei file per SC-88 sulle due porte) | le note del canale non partono più e quelle accese si spengono (pedale, All Notes Off, All Sound Off) |
| adlmidi  | i 16 canali MIDI | i canali spenti del sequencer, che rilascia le note |
| adplug   | i canali OPL che suonano note | niente note nuove, quelle accese rilasciate e operatori al minimo (-47 dB) |

I file audio non hanno canali. Il mute vale fino al brano dopo, anche
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
Spectrum, Atari 8 bit, Atari ST e file audio. Tra i moduli tracker, MOD, MED,
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
sottobrano che suona.

Nella skin retrò le stesse cose stanno nella finestra sotto «In
riproduzione», con le linguette Coda, Libreria e Playlist (un menu con le
playlist e «Nuova playlist…»). La libreria è un elenco alla Finder con
nome, formato e durata: il triangolino apre i sottobrani, piattaforma e
formato si scelgono da menu a comparsa con la spunta. Gli avvisi («Aggiunto
alla coda») li dà la mascotte nella finestra «Ciao!».

Nelle impostazioni (l'icona accanto ad «Apri file…» nella moderna, il menu
«Impostazioni…» nella retrò) si sceglie la cartella con le ROM dei moduli
Roland e il Sound Canvas per i MIDI; l'app se li ricorda.

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
- niente trascinamento dei file sulla finestra, né dalla libreria alle
  playlist, e niente import o export M3U.

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
libsidplayfp e libresidfp GPL-2.0, AdPlug e libbinio LGPL-2.1; gearmulator
(88emu) GPL-3.0, con parti derivate da Nuked-SC55 e Nuked-MT32 sotto GPL-2.0
o successiva; FluidSynth LGPL-2.1 o successiva; il SoundFont GeneralUser GS
(S. Christian Collins) ha la sua licenza, che ne permette l'uso nei
programmi, e viaggia accanto a lui nelle app; libADLMIDI ha
parti LGPL, MIT e BSD, ma nel complesso è GPL-3.0 o successiva, e i suoi
banchi di timbri hanno ciascuno le sue note. SQLite è di pubblico dominio.
Finché il backend UADE è incluso, il binario risultante va distribuito sotto
GPL (con 88emu e libADLMIDI, la GPL-3.0). Le ROM dei moduli Roland non sono
incluse.
