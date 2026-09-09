#!/usr/bin/env bash
# Fetch the upstream OpenLara GBA packer and apply the portability patch.
#
# The sources are not vendored into git. They are ~400 KB of someone else's
# C++ that this repository does not modify beyond patch.py, and
# scripts/phd_to_pkd.py already fetches the same files the same way for its
# native build -- two copies in the tree would be two things to keep in step.
# build/ is gitignored, so a checkout stays clean.
set -euo pipefail
cd "$(dirname "$0")"

UPSTREAM="${OPENLARA_PACKER_URL:-https://raw.githubusercontent.com/XProger/OpenLara/master/src/platform/gba/packer}"
DIR=build/packer

# TR1_PSX.h is a 71-byte empty stub upstream; PSX levels cannot be packed at
# all. It is fetched because out_GBA.h includes it, not because it does
# anything. PC .PHD in, .PKD out -- that is the whole of what works.
FILES=(main.cpp common.h TR1_PC.h TR1_PSX.h out_GBA.h IMA.h stb_image_resize.h)
LIQ=(libimagequant.h)

mkdir -p "$DIR/libimagequant"
for f in "${FILES[@]}"; do
  [[ -s "$DIR/$f" ]] || { echo "  fetch $f"; curl -fsSL "$UPSTREAM/$f" -o "$DIR/$f"; }
done
# Only the header. common.h includes libimagequant.h, but nothing on the
# PHD -> PKD path calls a liq_ function, so the library itself is never linked.
for f in "${LIQ[@]}"; do
  [[ -s "$DIR/libimagequant/$f" ]] || { echo "  fetch $f"; curl -fsSL "$UPSTREAM/libimagequant/$f" -o "$DIR/libimagequant/$f"; }
done

# Re-fetch common.h every time so the patch is applied to a pristine copy and
# its replacements cannot stack.
curl -fsSL "$UPSTREAM/common.h" -o "$DIR/common.h"
python3 patch.py "$DIR"
