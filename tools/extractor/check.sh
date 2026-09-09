#!/usr/bin/env bash
# Build the module, verify it, and prove its output byte-for-byte against the
# upstream packer.
#
#   ./check.sh                    build + verify + malformed-input handling
#   ./check.sh /path/to/TR1/DATA  also byte-for-byte parity for every level
#
# The oracle is build/native/oracle, built from the same patched sources by
# the host compiler, and it is the DIRECTORY packer -- it loads all 21 levels
# in one run and writes all 21 .PKDs, exactly as upstream's main.cpp does.
# That is the point. The module converts one level at a time, which is the
# only real change this port makes to the packer, and the only way to show
# the change is harmless is to compare against the thing it replaced.
set -euo pipefail
cd "$(dirname "$0")"

DATA="${1:-}"
MODULE=phd_pkd.wasm

echo "== build =="
bash build.sh
ls -l "$MODULE" | awk '{print "   " $5 " bytes"}'

echo
echo "== conformance =="
node verify.mjs "$MODULE"

echo
echo "== malformed input =="
# Not an edge case: the input is declared strict:false in the manifest, so a
# file that matches no known level hash reaches the module by design. Each of
# these must come back as an error with a message, never as a trap and never
# as output.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
head -c 200000 /dev/urandom > "$tmp/junk.PHD"
head -c 8 /dev/zero > "$tmp/tiny.PHD"
# A well-formed TR1 header on a file with nothing behind it: version 0x20,
# then a tile count that promises 200 tiles of 64 KB each.
printf '\x20\x00\x00\x00\xc8\x00\x00\x00' > "$tmp/truncated.PHD"
head -c 100000 /dev/zero >> "$tmp/truncated.PHD"
# TR2's level version. Same file layout at the top, different everything else.
printf '\x2d\x00\x00\x00' > "$tmp/tr2.PHD"
head -c 100000 /dev/zero >> "$tmp/tr2.PHD"

for f in junk tiny truncated tr2; do
  if node extract.mjs "$MODULE" "$tmp/out.PKD" "$tmp/$f.PHD" >/dev/null 2>"$tmp/err"; then
    echo "   FAIL - $f.PHD was accepted"; exit 1
  fi
  grep -q "trapped" "$tmp/err" && { echo "   FAIL - $f.PHD trapped instead of returning"; cat "$tmp/err"; exit 1; }
  echo "   ok: $f.PHD -> $(sed -n 's/.*failed ([0-9]*): //p' "$tmp/err" | head -1)"
done

if [[ -z "$DATA" ]]; then
  echo
  echo "No DATA directory given - skipping the parity check."
  echo "Run './check.sh /path/to/TR1_PC/DATA' to compare against the upstream packer."
  exit 0
fi

echo
echo "== native oracle =="
# Always rebuilt from the current sources. A stale binary is worse than no
# binary here: it would compare the module against a packer that no longer
# exists and call the difference a regression.
mkdir -p build/native build/ref
rm -f build/native/oracle
${CXX:-c++} -O2 -std=gnu++17 -fno-exceptions -fno-rtti -w \
  -Ibuild/packer -Ibuild/packer/libimagequant -Isrc \
  -o build/native/oracle src/oracle.cpp -lm
build/native/oracle "$DATA" build/ref | grep -c '^wrote' | xargs echo "  " "levels packed:"

echo
echo "== sanitizers =="
# The same ABI, the same port layer, the same one-level path -- linked
# natively so a memory error shows up as a report with a stack rather than as
# a wasm trap saying "null function or function signature mismatch", which is
# all a browser can tell you. This is how the fputc substitution in memfs.c
# was found, and it is worth keeping for the next one.
#
# alloc_dealloc_mismatch is off because out_GBA::convertGBA does
# `Mesh* m = new Mesh(...); ... delete[] m;`. That is upstream's, Mesh is
# trivially destructible so both spellings are one free() of the same
# pointer, and correcting it would be a source edit this port has no business
# making.
${CC:-cc} -O1 -g -fsanitize=address -std=c11 -w -c src/memfs.c  -o build/native/memfs.o
${CC:-cc} -O1 -g -fsanitize=address -std=c11 -w -c src/sha256.c -o build/native/sha256.o
#
# ASan only, not UBSan: TR1_PC is #pragma pack(1) throughout, so every array
# it hands out is a misaligned pointer to a packed struct by design. UBSan
# reports thousands of those and none of them is a bug on any target the
# packer runs on.
${CXX:-c++} -O1 -g -fsanitize=address \
  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
  -std=gnu++17 -fno-exceptions -fno-rtti -w \
  -Ibuild/packer -Ibuild/packer/libimagequant -Isrc \
  -o build/native/abi_san src/abi.cpp src/native_abi_main.cpp \
  build/native/memfs.o build/native/sha256.o \
  -Wl,--wrap=fopen,--wrap=fread,--wrap=fwrite,--wrap=fclose,--wrap=fseek,--wrap=ftell,--wrap=fputc,--wrap=fputs \
  -lm
san_level="$DATA/LEVEL1.PHD"
[[ -f "$san_level" ]] || san_level="$(ls "$DATA"/*.PHD | head -1)"
ASAN_OPTIONS=alloc_dealloc_mismatch=0 \
  build/native/abi_san "$san_level" "$tmp/san.PKD" 2>&1 | sed 's/^/   /'
cmp -s "$tmp/san.PKD" "build/ref/$(basename "$san_level" .PHD).PKD" \
  && echo "   ok: sanitized build agrees with the oracle" \
  || { echo "   FAIL - sanitized build disagrees with the oracle"; exit 1; }

echo
echo "== parity =="
fail=0
for ref in build/ref/*.PKD; do
  name="$(basename "$ref" .PKD)"
  phd="$DATA/$name.PHD"
  [[ -f "$phd" ]] || continue
  node extract.mjs "$MODULE" "$tmp/$name.PKD" "$phd" 2>"$tmp/log" >/dev/null || {
    echo "   FAIL $name: $(tail -1 "$tmp/log")"; fail=1; continue; }
  if cmp -s "$ref" "$tmp/$name.PKD"; then
    printf "   ok   %-9s %s\n" "$name" "$(grep '^ok:' "$tmp/log" | head -1)"
  else
    printf "   FAIL %-9s differs from the upstream packer\n" "$name"; fail=1
  fi
done

echo
if [[ $fail -eq 0 ]]; then
  echo "PASS - every level is byte-identical to the upstream packer."
else
  echo "FAIL - output differs from the upstream packer."
  exit 1
fi
