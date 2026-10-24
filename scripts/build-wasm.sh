#!/usr/bin/env bash
#
# Compila il core per WebAssembly in build-wasm/: il modulo per il browser
# (megaplayer_web, con la pagina di prova di web/) e CLI e test per Node.
# Serve Emscripten (emcmake nel PATH) e scripts/build-deps.sh già eseguito.
#
# Prova: ctest --test-dir build-wasm, oppure
#        python3 scripts/serve-web.py build-wasm e http://127.0.0.1:8765/

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# I due generatori dell'emulatore 68000 girano sulla macchina che compila.
cmake -S . -B build-host -G Ninja -DMP_WITH_OPENMPT=OFF -DMP_WITH_ARCHIVES=OFF > /dev/null
cmake --build build-host --target uade_build68k uade_gencpu

# libopenmpt e libarchive per WebAssembly non ci sono ancora: per ora suona
# solo UADE.
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DMP_UADE_HOST_TOOLS="$ROOT/build-host" -DMP_WITH_OPENMPT=OFF -DMP_WITH_ARCHIVES=OFF
cmake --build build-wasm
