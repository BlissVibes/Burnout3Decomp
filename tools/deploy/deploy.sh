#!/usr/bin/env bash
# Stages the built page as a static site and deploys it to Vercel (private project).
# Run inside WSL after tools/build-web.sh. Needs `npm i -g vercel` and `vercel login` once.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$HOME/b3/out/web"
STAGE="$HOME/b3/out/deploy"
PROJECT=burnout3-web
TEAM=mark-wilsons-projects-4b36a71c

[ -f "$SRC/ps2web.html" ] || { echo "no build at $SRC; run tools/build-web.sh" >&2; exit 1; }
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp "$SRC"/ps2web.html "$SRC"/ps2web.js "$SRC"/ps2web.wasm "$SRC"/ps2web.data "$SRC"/*.js "$STAGE"/
cp "$STAGE/ps2web.html" "$STAGE/index.html"
cp "$REPO/tools/deploy/vercel.json" "$STAGE/vercel.json"
du -sh "$STAGE"; ls -la "$STAGE"
cd "$STAGE"
vercel link --yes --project "$PROJECT" --scope "$TEAM" > /dev/null
vercel deploy --prod --yes --scope "$TEAM"
