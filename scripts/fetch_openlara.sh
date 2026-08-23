#!/usr/bin/env bash
# Init / update the OpenLara submodule (sylverb/OpenLara @ gnw).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/OpenLara"

cd "$ROOT"
if [[ ! -f .gitmodules ]] || ! grep -q 'third_party/OpenLara' .gitmodules 2>/dev/null; then
  echo "error: OpenLara submodule not registered — expected third_party/OpenLara in .gitmodules" >&2
  exit 1
fi

echo "[fetch] git submodule update --init --recursive -- third_party/OpenLara"
git submodule update --init --recursive -- third_party/OpenLara

if [[ ! -f "$DEST/src/fixed/common.h" ]]; then
  echo "error: missing $DEST/src/fixed/common.h after submodule update" >&2
  exit 1
fi

COMMIT="$(git -C "$DEST" rev-parse HEAD)"
BRANCH="$(git -C "$DEST" rev-parse --abbrev-ref HEAD 2>/dev/null || echo detached)"
echo "[fetch] OpenLara $BRANCH @ $COMMIT"

mkdir -p "$ROOT/src/platform/gnw"
ln -sfn ../../../third_party/OpenLara/src/fixed "$ROOT/src/platform/gnw/ol"
echo "[fetch] symlink src/platform/gnw/ol → third_party/OpenLara/src/fixed"
