#!/usr/bin/env python3
"""
Apply the portability patch to the fetched OpenLara GBA packer sources.

The packer is Windows C++: it includes <windows.h>, asserts with
DebugBreak(), shells out with CreateProcess() and walks directories with
FindFirstFile().  None of that exists on Unix and none of it exists on
wasm.  The patch is deliberately tiny -- three edits in one file -- because
every byte of the conversion logic has to stay exactly as upstream wrote it
for the parity check in check.sh to mean anything.

The same patched tree builds both the wasm module and the native oracle, so
a difference between them can only come from the port layer in src/, never
from a source edit that landed on one side and not the other.

This mirrors what scripts/phd_to_pkd.py does for its native build.  It is a
separate copy on purpose: scripts/ is shared byte-identical across many
repositories and is not ours to change.
"""
import re
import sys
from pathlib import Path

DIR = Path(sys.argv[1])
common = (DIR / "common.h").read_text(encoding="utf-8", errors="replace")

# 1. <windows.h> is where DebugBreak, WIN32_FIND_DATA and friends came from.
if "#include <windows.h>" not in common and "packer_compat.h" not in common:
    sys.exit("common.h: no <windows.h> include -- upstream changed shape")
common = common.replace('#include <windows.h>', '#include "packer_compat.h"')

# 2. ASSERT should say what failed.  Upstream breaks into a debugger, which
#    is useless in a browser worker; the message is the only thing the user
#    will ever see if the packer hits an impossible level.
common = common.replace(
    "#define ASSERT(x) { if (!(x)) { DebugBreak(); } }",
    '#define ASSERT(x) { if (!(x)) { fprintf(stderr, "ASSERT %s:%d: %s\\n",'
    ' __FILE__, __LINE__, #x); DebugBreak(); } }',
)

# 3. launchApp() runs an external encoder for audio tracks.  Nothing on the
#    PHD -> PKD path calls it, but it still has to compile, and a module that
#    imports nothing cannot start a process by definition.
common, n = re.subn(
    r"void launchApp\(const char\* cmdline\)\s*\{.*?\n\}",
    "void launchApp(const char* cmdline)\n"
    "{\n"
    "    (void)cmdline; // no process control off the Windows build\n"
    "}",
    common,
    count=1,
    flags=re.S,
)
if n != 1 and "no process control" not in common:
    sys.exit("common.h: launchApp not found -- upstream changed shape")

(DIR / "common.h").write_text(common)
print("patched common.h")
