#!/bin/sh
#
# Passo di build dell'app Flutter per macOS (app/macos, Runner): compila
# libmegaplayer.dylib con CMake e la copia, firmata, nel bundle dell'app.
# Richiede che scripts/build-deps.sh sia già stato eseguito.

set -e

# Xcode lancia i passi di build con un PATH minimo.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
	cmake -S "$ROOT" -B "$BUILD" -G Ninja
fi
cmake --build "$BUILD" --target megaplayer_capi

DEST="$BUILT_PRODUCTS_DIR/$FRAMEWORKS_FOLDER_PATH"
mkdir -p "$DEST"
cp "$BUILD/libmegaplayer.dylib" "$DEST/"
# Xcode firma il bundle alla fine: il codice annidato va firmato prima.
codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY:--}" "$DEST/libmegaplayer.dylib"
