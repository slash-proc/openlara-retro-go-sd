#!/usr/bin/env bash
# Build phd_pkd.wasm.
#
# The toolchain is wasi-sdk in a container, so nothing has to be installed on
# the machine doing the build and everyone gets the same clang. The packer
# sources are fetched by fetch.sh from upstream OpenLara and patched by
# patch.py; the five files in src/ are the module itself.
set -euo pipefail
cd "$(dirname "$0")"

# Pinned by digest, not by tag. A published module is hashed in the manifest,
# so the build has to be reproducible; `:latest` would silently change the
# compiler under a release and there would be no way to tell from the output
# which one built it. Bump this deliberately, and re-run check.sh when you do.
WASI_SDK_DIGEST="sha256:52595085a0ffbaf574d8d678a9aa81305f0b3af81c639291f15bc7713e42848e"
IMAGE="${WASI_SDK_IMAGE:-ghcr.io/webassembly/wasi-sdk@${WASI_SDK_DIGEST}}"
OUT="phd_pkd.wasm"

bash fetch.sh

# 512 pages = 32 MiB, and the number is measured, not copied.
#
# The module peaks at 255 pages (15.9 MiB) converting LEVEL10C, and every one
# of the 21 TR1 PC levels was measured, not sampled -- a TR1 PC release ships
# exactly these files, so there is no larger unseen case to leave headroom
# for. 512 is double the worst one observed. The manifest publishes whatever
# the binary declares as limits.maxMemoryPages, so the two cannot disagree.
MAX_MEMORY=33554432

# The exact ABI surface. Anything missing is a broken module and anything
# extra is unreviewed surface area, so the verifier rejects both -- which is
# why this list is written out rather than using --export-all.
EXPORTS=(
  abi_version alloc input_clear input_add run run_begin run_step
  stage_count stage_index stage_name_ptr stage_name_len
  output_count output_name_ptr output_name_len output_ptr output_len
  error_ptr error_len warnings_ptr warnings_len
)

# FileStream reaches the "filesystem" through the first six. --wrap rather
# than redefinition, so libc's own stdio keeps working for the packer's
# printf. fputc and fputs are wrapped because clang rewrites single-byte
# fwrite into fputc at -O1 and above -- see the note in memfs.c.
WRAPS=(fopen fread fwrite fclose fseek ftell fputc fputs)

INC="-Ibuild/packer -Ibuild/packer/libimagequant -Isrc"

# -fno-exceptions and -fno-rtti are forced, not preferred. wasm cannot unwind
# without the exception-handling proposal, and neither can setjmp; the packer
# does not throw, so there is nothing to lose, and building without it keeps
# the module free of a tag section so the conformance verifier stays exactly
# as it is for every project that vendors it.
CXXFLAGS="-O2 -w -std=gnu++17 -fno-exceptions -fno-rtti $INC"

LDFLAGS="-nostartfiles -Wl,--strip-all -Wl,--no-entry -Wl,--max-memory=$MAX_MEMORY -Wl,-z,stack-size=1048576"
for e in "${EXPORTS[@]}"; do LDFLAGS="$LDFLAGS -Wl,--export=$e"; done
for w in "${WRAPS[@]}"; do LDFLAGS="$LDFLAGS -Wl,--wrap=$w"; done

exec docker run --rm -v "$PWD:/w" -w /w --entrypoint sh "$IMAGE" -c "
set -e
CC=/opt/wasi-sdk/bin/clang
CXX=/opt/wasi-sdk/bin/clang++
T=--target=wasm32-wasip1
mkdir -p /tmp/o

# C sources: the WASI stubs that empty the import section, the memory
# 'files', and the hash that identifies which level this is.
\$CC \$T -O2 -c src/stubs.c  -o /tmp/o/stubs.o
\$CC \$T -O2 -c src/memfs.c  -o /tmp/o/memfs.o
\$CC \$T -O2 -c src/sha256.c -o /tmp/o/sha256.o

# C++: one translation unit. The packer is all headers -- common.h defines
# functions at namespace scope, so a second unit including it would not link.
\$CXX \$T $CXXFLAGS src/abi.cpp /tmp/o/*.o $LDFLAGS -o $OUT
"
