#!/usr/bin/env bash
#
# Scarica, a versioni fissate, i sorgenti delle dipendenze che compila
# direttamente CMake (UADE con libzakalwe e bencode-tools, ancient, miniaudio,
# game-music-emu, libsidplayfp con libresidfp, libADLMIDI, AdPlug con libbinio,
# SQLite, gearmulator per 88emu, i moduli Roland emulati, FluidSynth con il
# SoundFont GeneralUser GS, per i MIDI senza le ROM, e AtariAudio per l'Atari ST)
# e applica a UADE le patch per farlo girare nel processo del player e per
# zittirne le voci, a libsidplayfp quella per zittirle subito e a gearmulator
# quelle per compilarlo con MinGW.
# libopenmpt e libarchive invece arrivano dal sistema (brew, apt, MSYS2...),
# vedi README.md.
#
# Funziona su macOS, Linux e Windows (bash di MSYS2 o Git for Windows).
#
# Uso: scripts/build-deps.sh [--clean]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/third_party/src"

UADE_REF=uade-3.05
LIBZAKALWE_REF=v1.0.0
BENCODETOOLS_REF=v1.0.1
ANCIENT_REF=v2.3.0
MINIAUDIO_REF=0.11.25
GME_REF=0.6.5
ADLMIDI_REF=v1.6.2.1
LIBBINIO_REF=libbinio-1.5
ADPLUG_REF=adplug-2.4
LIBRESIDFP_VERSION=1.2.3
LIBRESIDFP_SHA256=e9b53a984ca708c3e8d9139eb6aad6141f73ffc22d70a020f7966803f4e2f76d
LIBSIDPLAYFP_VERSION=3.1.1
LIBSIDPLAYFP_SHA256=12b79190593bf480b2d11481b5c2de62bac07f344437a66cd8d887329875c626
# gearmulator (The Usual Suspects), per 88lib: un commit del ramo principale,
# perché l'interfaccia C di 88lib non è ancora in una release abbastanza vecchia.
GEARMULATOR_COMMIT=a8a9ae99a8f7fc05152fa641a5ec1fed0fd88164
FLUIDSYNTH_REF=v2.6.1
ATARIAUDIO_REF=1.26
# GeneralUser GS 2.0.3 (S. Christian Collins), il SoundFont General MIDI e GS
# che viaggia con le app: non ha release, si prende da un commit preciso.
GENERALUSER_COMMIT=684543d5e5efaef08d02be50dcda8d552478fa60
GENERALUSER_SF2_SHA256=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
GENERALUSER_LICENSE_SHA256=7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be
# SQLite 3.53.4: l'amalgamation (un solo sqlite3.c) dall'archivio autoconf.
SQLITE_VERSION=3530400
SQLITE_YEAR=2026
SQLITE_SHA256=0e9483900e92cd5de8fd48d16bf9200145a61f7fd5be542a5ac81d8a9516eb9c

if [[ "${1:-}" == "--clean" ]]; then
	# third_party/install è il prefisso delle versioni precedenti dello script.
	rm -rf "$SRC" "$ROOT/third_party/install"
fi

mkdir -p "$SRC"

# clone <nome> <url> <ref>: clona una sola volta, alla versione richiesta.
clone() {
	local name="$1" url="$2" ref="$3"
	if [[ ! -d "$SRC/$name" ]]; then
		echo "==> clono $name ($ref)"
		git -c advice.detachedHead=false clone -q --depth 1 --branch "$ref" "$url" "$SRC/$name"
	fi
}

# fetch <nome> <url> <sha256>: scarica ed estrae un archivio di rilascio, una
# volta sola. Per libsidplayfp e libresidfp servono gli archivi e non i tag di
# git: contengono già i file che altrimenti genererebbero autotools e xa65;
# quello di SQLite contiene l'amalgamation.
fetch() {
	local name="$1" url="$2" sha256="$3"
	if [[ ! -d "$SRC/$name" ]]; then
		echo "==> scarico $name"
		local archive="$SRC/$name.tar.gz"
		curl -fsSL -o "$archive" "$url"
		local actual
		if command -v sha256sum >/dev/null; then
			actual=$(sha256sum "$archive" | cut -d' ' -f1)
		else
			actual=$(shasum -a 256 "$archive" | cut -d' ' -f1)
		fi
		if [[ "$actual" != "$sha256" ]]; then
			rm -f "$archive"
			echo "checksum sbagliato per $name: $actual" >&2
			exit 1
		fi
		mkdir -p "$SRC/$name"
		tar -xzf "$archive" -C "$SRC/$name" --strip-components=1
		rm -f "$archive"
	fi
}

clone libzakalwe   https://gitlab.com/hors/libzakalwe.git              "$LIBZAKALWE_REF"
clone bencodetools https://gitlab.com/heikkiorsila/bencodetools.git    "$BENCODETOOLS_REF"
clone uade         https://gitlab.com/uade-music-player/uade.git       "$UADE_REF"
clone ancient      https://github.com/temisu/ancient.git               "$ANCIENT_REF"
clone miniaudio    https://github.com/mackron/miniaudio.git            "$MINIAUDIO_REF"
clone game-music-emu https://github.com/libgme/game-music-emu.git      "$GME_REF"
clone libadlmidi   https://github.com/Wohlstand/libADLMIDI.git        "$ADLMIDI_REF"
clone libbinio     https://github.com/adplug/libbinio.git             "$LIBBINIO_REF"
clone fluidsynth   https://github.com/FluidSynth/fluidsynth.git        "$FLUIDSYNTH_REF"
# Delle sue dipendenze incluse a FluidSynth serve solo gcem.
if [[ ! -f "$SRC/fluidsynth/gcem/CMakeLists.txt" ]]; then
	git -C "$SRC/fluidsynth" submodule update -q --init --depth 1 gcem
fi
clone adplug       https://github.com/adplug/adplug.git               "$ADPLUG_REF"
clone atariaudio   https://github.com/arnaud-carre/AtariAudio.git     "$ATARIAUDIO_REF"
fetch libresidfp   "https://github.com/libsidplayfp/libresidfp/releases/download/v$LIBRESIDFP_VERSION/libresidfp-$LIBRESIDFP_VERSION.tar.gz" "$LIBRESIDFP_SHA256"
fetch libsidplayfp "https://github.com/libsidplayfp/libsidplayfp/releases/download/v$LIBSIDPLAYFP_VERSION/libsidplayfp-$LIBSIDPLAYFP_VERSION.tar.gz" "$LIBSIDPLAYFP_SHA256"
fetch sqlite       "https://sqlite.org/$SQLITE_YEAR/sqlite-autoconf-$SQLITE_VERSION.tar.gz" "$SQLITE_SHA256"

# download <file> <url> <sha256>: scarica un file singolo, una volta sola.
download() {
	local target="$1" url="$2" sha256="$3"
	if [[ ! -f "$target" ]]; then
		echo "==> scarico $(basename "$target")"
		mkdir -p "$(dirname "$target")"
		curl -fsSL -o "$target.part" "$url"
		local actual
		if command -v sha256sum >/dev/null; then
			actual=$(sha256sum "$target.part" | cut -d' ' -f1)
		else
			actual=$(shasum -a 256 "$target.part" | cut -d' ' -f1)
		fi
		if [[ "$actual" != "$sha256" ]]; then
			rm -f "$target.part"
			echo "checksum sbagliato per $(basename "$target"): $actual" >&2
			exit 1
		fi
		mv "$target.part" "$target"
	fi
}

GENERALUSER_URL="https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/$GENERALUSER_COMMIT"
download "$SRC/generaluser-gs/GeneralUser-GS.sf2" "$GENERALUSER_URL/GeneralUser-GS.sf2" "$GENERALUSER_SF2_SHA256"
download "$SRC/generaluser-gs/LICENSE.txt" "$GENERALUSER_URL/documentation/LICENSE.txt" "$GENERALUSER_LICENSE_SHA256"

# gearmulator a un commit preciso, con i soli sottomoduli che il suo CMake
# vuole per costruire 88lib: niente JUCE né le estensioni CLAP.
if [[ ! -d "$SRC/gearmulator/source" ]]; then
	echo "==> clono gearmulator (${GEARMULATOR_COMMIT:0:10})"
	rm -rf "$SRC/gearmulator"
	git init -q "$SRC/gearmulator"
	git -C "$SRC/gearmulator" fetch -q --depth 1 https://github.com/dsp56300/gearmulator.git "$GEARMULATOR_COMMIT"
	git -C "$SRC/gearmulator" -c advice.detachedHead=false checkout -q FETCH_HEAD
	git -C "$SRC/gearmulator" submodule update -q --init --depth 1 --recursive -- \
		source/cpu/dsp56300 source/cpu/mc68k source/3rdparty/cpp-terminal source/3rdparty/RmlUi \
		source/3rdparty/freetype source/3rdparty/lunasvg source/3rdparty/portmidi-latest
fi

# Le patch di UADE: uadecore in un thread del player (src/uade/InProcess.cpp)
# e le voci di Paula da zittire una per una. Ognuna si applica una volta sola.
for UADE_PATCH in "$ROOT/patches/uade-in-process.patch" "$ROOT/patches/uade-voice-mask.patch"; do
	if ! git -C "$SRC/uade" apply --reverse --check "$UADE_PATCH" 2>/dev/null; then
		echo "==> applico $(basename "$UADE_PATCH")"
		git -C "$SRC/uade" apply "$UADE_PATCH"
	fi
done

# La patch di libsidplayfp: il mute di una voce agisce subito, e non alla
# prossima scrittura del brano nel suo registro.
SID_PATCH="$ROOT/patches/libsidplayfp-instant-mute.patch"
if ! git -C "$SRC/libsidplayfp" apply --reverse --check "$SID_PATCH" 2>/dev/null; then
	echo "==> applico $(basename "$SID_PATCH")"
	git -C "$SRC/libsidplayfp" apply "$SID_PATCH"
fi

# Le patch di gearmulator per compilarlo con MinGW (Windows): include e
# eccezioni strutturate, che conosce solo il compilatore di Microsoft. Una
# è per il sottomodulo dsp56300.
for GEAR_PATCH in "gearmulator:$ROOT/patches/gearmulator-mingw.patch" \
	"gearmulator/source/cpu/dsp56300:$ROOT/patches/gearmulator-dsp56300-mingw.patch"; do
	dir="$SRC/${GEAR_PATCH%%:*}" patch="${GEAR_PATCH#*:}"
	if ! git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
		echo "==> applico $(basename "$patch")"
		git -C "$dir" apply "$patch"
	fi
done

echo
echo "Sorgenti pronti in $SRC"
