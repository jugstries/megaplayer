#!/bin/sh
#
# Prepara una release per il sito: scarica dalla CI (workflow "release") le
# build del commit del tag, l'installer di Windows e lo zip per Mac, le firma
# con la chiave del Portachiavi (app/tool/update_key.dart) e scrive in
# dist/site quello da caricare in www.claudiotassitano.it/projects/megaplayer/:
#
#   index.html                                     la pagina per scaricarle
#   updates/windows.json, updates/macos.json       i manifesti che l'app legge
#   updates/megaplayer-X.Y.Z-windows-setup.exe     l'installer di Windows
#   updates/megaplayer-X.Y.Z-macos.zip             l'app per Mac
#
# Va bene qualunque build riuscita di quel commit, anche di un branch. Le
# novità sono il messaggio del tag, che va creato annotato:
#   git tag -a v0.2.0 -m "• Prima novità
#   • Seconda novità"
#
# Uso: scripts/publish-update.sh v0.2.0   (serve gh autenticato)

set -eu

TAG=${1:?"uso: scripts/publish-update.sh vX.Y.Z"}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO=jugstries/megaplayer
OUT="$ROOT/dist/site"
WORK="$ROOT/dist/ci-$TAG"

fail() {
	echo "publish-update: $*" >&2
	exit 1
}

[ "$(git -C "$ROOT" cat-file -t "$TAG" 2>/dev/null)" = tag ] ||
	fail "$TAG non è un tag annotato: le novità vengono dal suo messaggio"

line=$(git -C "$ROOT" show "$TAG:app/pubspec.yaml" | grep '^version:')
version=${line#version: }
name=${version%+*}
build=${version#*+}
[ "v$name" = "$TAG" ] || fail "in $TAG app/pubspec.yaml dice $name"

commit=$(git -C "$ROOT" rev-parse "$TAG^{commit}")
run=$(gh run list --repo "$REPO" --workflow release.yml --commit "$commit" --status success \
	--limit 1 --json databaseId --jq '.[0].databaseId')
[ -n "$run" ] || fail "nessuna build riuscita per $TAG: la CI ha finito?"

rm -rf "$WORK" "$OUT"
mkdir -p "$WORK" "$OUT/updates"
git -C "$ROOT" tag -l --format='%(contents)' "$TAG" >"$WORK/novita.txt"

cd "$ROOT/app"
page=""
for platform in windows macos; do
	case $platform in
	windows) artifact=megaplayer-windows-setup file=megaplayer-$name-windows-setup.exe ;;
	macos) artifact=megaplayer-macos file=megaplayer-$name-macos.zip ;;
	esac
	if ! gh run download "$run" --repo "$REPO" --name "$artifact" --dir "$WORK/$platform" 2>/dev/null; then
		echo "publish-update: la build di $TAG non ha $artifact, salto $platform" >&2
		continue
	fi
	[ -f "$WORK/$platform/$file" ] || fail "la CI non ha prodotto $file"
	dart run tool/sign_update.dart --installer "$WORK/$platform/$file" --platform "$platform" \
		--version "$name" --build "$build" --notes "$WORK/novita.txt" --out "$OUT/updates"
	page="$page --$platform $OUT/updates/$platform.json"
done
[ -n "$page" ] || fail "la build di $TAG non ha niente da pubblicare"
# shellcheck disable=SC2086 # le opzioni vanno spezzate
dart run tool/download_page.dart $page --out "$OUT/index.html"

echo
echo "Da caricare in projects/megaplayer/ sul sito: il contenuto di dist/site"
echo "(prima installer e zip, per ultimi i manifesti in updates/*.json)."
