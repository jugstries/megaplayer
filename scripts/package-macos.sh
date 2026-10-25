#!/bin/bash
#
# Rende distribuibile l'app macOS compilata da `flutter build macos --release`:
# mette nel bundle le librerie di Homebrew da cui dipende il core (con i
# riferimenti riscritti verso Contents/Frameworks), i replayer di UADE in
# Contents/Resources/uade-data e il SoundFont GeneralUser GS (con la sua
# licenza) in Contents/Resources/soundfonts, firma e fa lo zip, che serve sia per
# scaricarla sia per gli aggiornamenti.
#
# Per ora la firma è "ad hoc": la prima volta macOS la apre solo con «Apri
# comunque» in Impostazioni di Sistema → Privacy e sicurezza.
#
# Uso: scripts/package-macos.sh build/.../megaplayer.app dist/megaplayer-X.Y.Z-macos.zip

set -eu

APP=${1:?"uso: scripts/package-macos.sh APP.app USCITA.zip"}
OUT=${2:?"uso: scripts/package-macos.sh APP.app USCITA.zip"}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FRAMEWORKS="$APP/Contents/Frameworks"
CORE="$FRAMEWORKS/libmegaplayer.dylib"

fail() {
	echo "package-macos: $*" >&2
	exit 1
}

[ -f "$CORE" ] || fail "nel bundle manca libmegaplayer.dylib"
[ -d "$ROOT/build/uade-data" ] || fail "mancano i replayer di UADE in build/uade-data"
SOUNDFONT="$ROOT/third_party/src/generaluser-gs"
[ -f "$SOUNDFONT/GeneralUser-GS.sf2" ] || fail "manca GeneralUser GS (scripts/build-deps.sh)"

# I riferimenti di una libreria ad altre librerie, tranne quelle di sistema.
references() {
	otool -L "$1" | tail -n +2 | awk '{ print $1 }' | grep -v -e '^/usr/lib/' -e '^/System/' || true
}

# Copia nel bundle, una volta sola, le librerie da cui dipende $1 e ne
# riscrive i riferimenti: tutte stanno accanto, in Contents/Frameworks.
# $2 è la cartella da cui $1 viene, per i riferimenti relativi di Homebrew.
bundle() {
	local file=$1 origin=$2 reference name source
	for reference in $(references "$file"); do
		name=$(basename "$reference")
		[ "$name" = "$(basename "$file")" ] && continue
		case $reference in
		@loader_path/* | @rpath/*) source="$origin/$name" ;;
		*) source=$reference ;;
		esac
		if [ ! -f "$FRAMEWORKS/$name" ]; then
			[ -f "$source" ] || fail "non trovo $reference (per $(basename "$file"))"
			cp "$source" "$FRAMEWORKS/$name"
			chmod u+w "$FRAMEWORKS/$name"
			install_name_tool -id "@loader_path/$name" "$FRAMEWORKS/$name" 2>/dev/null
			bundle "$FRAMEWORKS/$name" "$(dirname "$(realpath "$source")")"
		fi
		install_name_tool -change "$reference" "@loader_path/$name" "$file" 2>/dev/null
	done
}

bundle "$CORE" "$ROOT/build"

# Niente deve più puntare fuori dal bundle o dal sistema.
for library in "$FRAMEWORKS"/*.dylib; do
	outside=$(references "$library" | grep -v '^@loader_path/' | grep -v '^@rpath/libmegaplayer.dylib$' || true)
	[ -z "$outside" ] || fail "$(basename "$library") punta ancora a: $outside"
done

rm -rf "$APP/Contents/Resources/uade-data" "$APP/Contents/Resources/soundfonts"
cp -R "$ROOT/build/uade-data" "$APP/Contents/Resources/uade-data"
mkdir -p "$APP/Contents/Resources/soundfonts"
cp "$SOUNDFONT/GeneralUser-GS.sf2" "$APP/Contents/Resources/soundfonts/"
cp "$SOUNDFONT/LICENSE.txt" "$APP/Contents/Resources/soundfonts/GeneralUser-GS-LICENSE.txt"

# Prima le librerie, poi l'app: la firma del bundle copre quello che c'è dentro.
for library in "$FRAMEWORKS"/*.dylib; do
	codesign --force --sign - "$library"
done
codesign --force --sign - --preserve-metadata=entitlements "$APP"
codesign --verify --deep --strict "$APP"

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT"
echo "$OUT: $(ls "$FRAMEWORKS" | grep -c '\.dylib$') librerie nel bundle"
