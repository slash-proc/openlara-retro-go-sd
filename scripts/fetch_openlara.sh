#!/usr/bin/env bash
# Fetch OpenLara sources needed for the G&W homebrew (fixed engine + GBA rasterizer).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/OpenLara"
URL="${OPENLARA_URL:-https://github.com/XProger/OpenLara.git}"
# Pin when known; default = tip of master at fetch time.
REF="${OPENLARA_REF:-master}"

mkdir -p "$ROOT/third_party"
if [[ -d "$DEST/.git" ]]; then
  echo "[fetch] updating existing clone in $DEST"
  git -C "$DEST" fetch --depth 1 origin "$REF"
  git -C "$DEST" checkout --force "FETCH_HEAD"
else
  rm -rf "$DEST"
  echo "[fetch] sparse-cloning $URL ($REF) → $DEST"
  git clone --depth 1 --filter=blob:none --sparse "$URL" "$DEST"
  git -C "$DEST" sparse-checkout set src/fixed src/platform/gba
  git -C "$DEST" checkout "$REF" 2>/dev/null || true
fi

COMMIT="$(git -C "$DEST" rev-parse HEAD)"
printf '%s\n%s\n' \
  "https://github.com/XProger/OpenLara/commit/$COMMIT" \
  "sparse: src/fixed + src/platform/gba; apply patches/0001-openlara-gnw.patch" \
  > "$DEST/UPSTREAM.txt"

PATCH="$ROOT/patches/0001-openlara-gnw.patch"
if [[ -f "$PATCH" ]]; then
  if grep -q '__GNW__' "$DEST/src/fixed/common.h" 2>/dev/null; then
    echo "[fetch] __GNW__ already present in common.h"
  else
    echo "[fetch] applying $PATCH"
    patch -d "$DEST" -p1 < "$PATCH"
  fi
fi

mkdir -p "$ROOT/src/platform/gnw"
ln -sfn ../../../third_party/OpenLara/src/fixed "$ROOT/src/platform/gnw/ol"

echo "[fetch] done — $COMMIT"
echo "[fetch] symlink src/platform/gnw/ol → third_party/OpenLara/src/fixed"
