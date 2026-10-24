#!/usr/bin/env bash
#
# Scarica, a versioni fissate, i sorgenti delle dipendenze che compila
# direttamente CMake (UADE con libzakalwe e bencode-tools, ancient, miniaudio,
# game-music-emu)
# e applica a UADE la patch per farlo girare nel processo del player.
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

clone libzakalwe   https://gitlab.com/hors/libzakalwe.git              "$LIBZAKALWE_REF"
clone bencodetools https://gitlab.com/heikkiorsila/bencodetools.git    "$BENCODETOOLS_REF"
clone uade         https://gitlab.com/uade-music-player/uade.git       "$UADE_REF"
clone ancient      https://github.com/temisu/ancient.git               "$ANCIENT_REF"
clone miniaudio    https://github.com/mackron/miniaudio.git            "$MINIAUDIO_REF"
clone game-music-emu https://github.com/libgme/game-music-emu.git      "$GME_REF"

# Patch per far girare uadecore in un thread del player (src/uade/InProcess.cpp).
UADE_PATCH="$ROOT/patches/uade-in-process.patch"
if ! git -C "$SRC/uade" apply --reverse --check "$UADE_PATCH" 2>/dev/null; then
	echo "==> applico $(basename "$UADE_PATCH")"
	git -C "$SRC/uade" apply "$UADE_PATCH"
fi

echo
echo "Sorgenti pronti in $SRC"
