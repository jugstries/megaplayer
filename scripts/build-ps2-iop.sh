#!/bin/sh
# I moduli IOP della PlayStation 2 che il decoder PSF2 mette nella sua ROM, al
# posto della BIOS di Sony (src/ps2/irx): quelli di sistema da ps2sdk v2.0.0
# (Academic Free License 2.0) con le correzioni di patches/ps2sdk-iop.patch, e
# hefile.irx (src/ps2/hefile), il ponte con Highly Experimental. Sono nel
# repository già compilati: lo script serve a rifarli, con la toolchain della
# release v2.0.0 di ps2dev, scaricata e verificata qui oppure quella già in
# $PS2DEV.
#
# Uso: scripts/build-ps2-iop.sh [cartella di lavoro]   (predefinita: build-ps2)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mkdir -p "${1:-$ROOT/build-ps2}" && cd "${1:-$ROOT/build-ps2}" && pwd)
OUT="$ROOT/src/ps2/irx"

PS2DEV_URL=https://github.com/ps2dev/ps2dev/releases/download/v2.0.0
PS2SDK_URL=https://github.com/ps2dev/ps2sdk.git
PS2SDK_COMMIT=b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b # tag v2.0.0

# I moduli, nell'ordine della ROM, e la loro cartella in ps2sdk.
MODULES="sysmem:iop/system/sysmem loadcore:iop/system/loadcore excepman:iop/system/excepman
intrman:iop/system/intrman ssbusc:iop/system/ssbusc dmacman:iop/system/dmacman
timrman:iop/system/timrman sysclib:iop/system/sysclib heaplib:iop/system/heaplib
threadman:iop/system/threadman vblank:iop/system/vblank ioman:iop/system/ioman
modload:iop/system/modload stdio:iop/system/stdio sifman:iop/system/sifman
sifcmd:iop/system/sifcmd loadfile:iop/system/loadfile sifinit:iop/system/sifinit
fileio:iop/fs/fileio"

sha256() {
	if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

# --- La toolchain ---
if [ -z "${PS2DEV:-}" ]; then
	case "$(uname -s)-$(uname -m)" in
	Darwin-arm64) asset=ps2dev-macos-latest.tar.gz sum=6f6df659d2d49738f21f53a53c361a74182d0e4c2495e31e76972ff818bc21cb ;;
	Darwin-x86_64) asset=ps2dev-macos-15-intel.tar.gz sum=833d2dc1e3cbfa2091047e4539e1bd0bd2836668f886a1c8a75e6e09deb56bef ;;
	Linux-x86_64) asset=ps2dev-ubuntu-latest.tar.gz sum=c8e5dedccf62084d476894e88cd0451b57f1ed288e741eb3c1263010aaffc028 ;;
	Linux-aarch64) asset=ps2dev-ubuntu-24.04-arm.tar.gz sum=ed69330f235d421754196acf866f8590e5f92e8b92571f1c7e05e853900e7622 ;;
	*) echo "nessuna toolchain ps2dev per $(uname -s) $(uname -m): indica la tua in PS2DEV" >&2; exit 1 ;;
	esac
	if [ ! -x "$WORK/ps2dev/ps2dev/iop/bin/mipsel-none-elf-gcc" ]; then
		if [ ! -f "$WORK/$asset" ] || [ "$(sha256 "$WORK/$asset")" != "$sum" ]; then
			echo "==> scarico $asset"
			curl -fL --retry 3 -o "$WORK/$asset" "$PS2DEV_URL/$asset"
		fi
		[ "$(sha256 "$WORK/$asset")" = "$sum" ] || { echo "$asset: checksum sbagliato" >&2; exit 1; }
		rm -rf "$WORK/ps2dev" && mkdir -p "$WORK/ps2dev"
		tar xzf "$WORK/$asset" -C "$WORK/ps2dev"
	fi
	PS2DEV="$WORK/ps2dev/ps2dev"
fi
PATH="$PS2DEV/iop/bin:$PS2DEV/ps2sdk/bin:$PATH"
export PS2DEV PATH

# --- ps2sdk con le correzioni ---
PS2SDKSRC="$WORK/ps2sdk"
export PS2SDKSRC
if [ ! -d "$PS2SDKSRC/.git" ]; then
	git clone --quiet --filter=blob:none "$PS2SDK_URL" "$PS2SDKSRC"
fi
git -C "$PS2SDKSRC" checkout --quiet "$PS2SDK_COMMIT"
if ! git -C "$PS2SDKSRC" apply --reverse --check "$ROOT/patches/ps2sdk-iop.patch" 2>/dev/null; then
	echo "==> applico ps2sdk-iop.patch"
	git -C "$PS2SDKSRC" apply "$ROOT/patches/ps2sdk-iop.patch"
fi

# --- I moduli ---
mkdir -p "$OUT"
for entry in $MODULES; do
	name=${entry%%:*} dir=${entry#*:}
	echo "==> $name"
	make -s -C "$PS2SDKSRC/$dir" >/dev/null
	cp "$PS2SDKSRC/$dir/irx/$name.irx" "$OUT/$name.irx"
done
echo "==> hefile"
rm -rf "$WORK/hefile" && cp -R "$ROOT/src/ps2/hefile" "$WORK/hefile"
make -s -C "$WORK/hefile" >/dev/null
cp "$WORK/hefile/irx/hefile.irx" "$OUT/hefile.irx"
cp "$PS2SDKSRC/LICENSE" "$OUT/LICENSE.ps2sdk"

echo
echo "Moduli in $OUT:"
cd "$OUT" && ls -l *.irx
