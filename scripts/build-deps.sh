#!/usr/bin/env bash
#
# Scarica (a versioni fissate) e compila le dipendenze che non usiamo come
# pacchetti di sistema. Tutto finisce sotto third_party/:
#
#   third_party/src/      sorgenti clonati
#   third_party/install/  prefisso di installazione di UADE e delle sue librerie
#
# ancient e miniaudio vengono solo clonati: li compila direttamente CMake.
# libopenmpt invece arriva dal sistema (brew, apt, vcpkg...), vedi README.md.
#
# Uso: scripts/build-deps.sh [--clean]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
SRC="$TP/src"
PREFIX="$TP/install"

UADE_REF=uade-3.05
LIBZAKALWE_REF=v1.0.0
BENCODETOOLS_REF=v1.0.1
ANCIENT_REF=v2.3.0
MINIAUDIO_REF=0.11.25

if [[ "${1:-}" == "--clean" ]]; then
	rm -rf "$SRC" "$PREFIX"
fi

case "$(uname -s)" in
	Darwin) JOBS="$(sysctl -n hw.ncpu)"; SHLIB_EXT=dylib ;;
	Linux)  JOBS="$(nproc)";             SHLIB_EXT=so ;;
	*) echo "UADE supporta solo sistemi UNIX (macOS, Linux, BSD)." >&2; exit 1 ;;
esac

mkdir -p "$SRC" "$PREFIX/lib"

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

# libzakalwe si chiama libzakalwe.so anche su macOS: UADE la cerca con quel nome.
if [[ ! -f "$PREFIX/lib/libzakalwe.so" ]]; then
	echo "==> compilo libzakalwe"
	(
		cd "$SRC/libzakalwe"
		if [[ "$SHLIB_EXT" == dylib ]]; then
			# Il linker di macOS non conosce -soname: al suo posto un install
			# name assoluto, così libuade e uadecore la trovano a runtime.
			# Clang recente rifiuta anche le dichiarazioni implicite di un
			# paio di file.
			sed -i '' -e 's|-Wl,-soname,\$@|-Wl,-install_name,$(PREFIX)/lib/$@|' \
				-e 's/^CFLAGS = -W -Wall/CFLAGS = -Wno-implicit-function-declaration -W -Wall/' Makefile
		fi
		./configure --prefix="$PREFIX"
		make -j"$JOBS" install PREFIX="$PREFIX" CC="${CC:-cc}"
	)
fi

if [[ ! -f "$PREFIX/lib/libbencodetools.$SHLIB_EXT" ]]; then
	echo "==> compilo bencode-tools"
	(
		cd "$SRC/bencodetools"
		./configure --prefix="$PREFIX" --without-python
		make -j"$JOBS"
		make install
	)
fi

if [[ ! -f "$PREFIX/lib/libuade.$SHLIB_EXT" ]]; then
	echo "==> compilo UADE"
	(
		cd "$SRC/uade"
		./configure --prefix="$PREFIX" \
			--bencode-tools-prefix="$PREFIX" \
			--libzakalwe-prefix="$PREFIX" \
			--without-uade123 --without-uadesimple --without-uadefs --without-write-audio
		make -j"$JOBS"
		make install
	)
fi

echo
echo "Dipendenze pronte in $PREFIX"
