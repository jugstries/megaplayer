#!/bin/sh
#
# Prepara una release per il sito: scarica dalla CI l'installer di Windows
# del tag (workflow "release"), lo firma con la chiave del Portachiavi
# (app/tool/update_key.dart) e scrive in dist/site quello da caricare in
# www.claudiotassitano.it/projects/megaplayer/:
#
#   index.html                                     la pagina per scaricarlo
#   updates/windows.json                           il manifesto che l'app legge
#   updates/megaplayer-X.Y.Z-windows-setup.exe     l'installer
#
# Le novità sono il messaggio del tag, che va creato annotato:
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

run=$(gh run list --repo "$REPO" --workflow release.yml --branch "$TAG" --status success \
	--limit 1 --json databaseId --jq '.[0].databaseId')
[ -n "$run" ] || fail "nessuna build riuscita per $TAG: la CI ha finito?"

rm -rf "$WORK"
mkdir -p "$WORK"
gh run download "$run" --repo "$REPO" --name megaplayer-windows-setup --dir "$WORK"
installer="$WORK/megaplayer-$name-windows-setup.exe"
[ -f "$installer" ] || fail "la CI non ha prodotto $(basename "$installer")"

git -C "$ROOT" tag -l --format='%(contents)' "$TAG" >"$WORK/novita.txt"

mkdir -p "$OUT/updates"
cd "$ROOT/app"
dart run tool/sign_update.dart --installer "$installer" --platform windows \
	--version "$name" --build "$build" --notes "$WORK/novita.txt" --out "$OUT/updates"
dart run tool/download_page.dart --manifest "$OUT/updates/windows.json" --out "$OUT/index.html"

echo
echo "Da caricare in projects/megaplayer/ sul sito: il contenuto di dist/site"
echo "(prima l'installer, per ultimo updates/windows.json)."
