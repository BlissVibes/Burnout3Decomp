#!/usr/bin/env bash
# Stages the built page as a static site and deploys it to Vercel (private project).
# Run inside WSL after tools/build-web.sh. Needs `npm i -g vercel` and `vercel login` once.
#
# usage: deploy.sh [--yes]
# Without --yes this is a dry run: it stages the files, lists them and prints the
# vercel commands it would run. Only --yes uploads anything.
# The staged ps2web.data holds the game's executable and ps2web.wasm its translated
# code. Never deploy to a project that is not behind Vercel Authentication.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$HOME/b3/out/web"
STAGE="$HOME/b3/out/deploy"
PROJECT=burnout3-web
TEAM=mark-wilsons-projects-4b36a71c

confirmed=0
for arg in "$@"; do
  case "$arg" in
    --yes) confirmed=1 ;;
    *) echo "usage: deploy.sh [--yes]" >&2; exit 2 ;;
  esac
done

[ -f "$SRC/ps2web.html" ] || { echo "no build at $SRC; run tools/build-web.sh" >&2; exit 1; }
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp "$SRC"/ps2web.html "$SRC"/ps2web.wasm "$SRC"/ps2web.data "$SRC"/*.js "$STAGE"/
cp "$STAGE/ps2web.html" "$STAGE/index.html"
cp "$REPO/tools/deploy/vercel.json" "$STAGE/vercel.json"
du -sh "$STAGE"; ls -la "$STAGE"
link=(vercel link --yes --project "$PROJECT" --scope "$TEAM")
deploy=(vercel deploy --prod --yes --scope "$TEAM")
if [ "$confirmed" != 1 ]; then
  echo "dry run: staged $STAGE, nothing uploaded. With --yes this would run, in that directory:"
  echo "  ${link[*]}"
  echo "  ${deploy[*]}"
  echo "Check that Vercel Authentication is on for $PROJECT, then run: bash tools/deploy/deploy.sh --yes"
  exit 0
fi
cd "$STAGE"
"${link[@]}" > /dev/null
"${deploy[@]}"
