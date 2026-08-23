#!/usr/bin/env python3
"""
OpenLara PHD → PKD converter (Python driver).

The official packer is ~5k lines of Windows C++ (not a small script). This tool:
  1. Fetches the upstream GBA packer sources
  2. Applies a small Unix/macOS portability patch
  3. Compiles a native ``phd_packer`` binary
  4. Runs it on your TR1 PC ``.PHD`` tree

IMPORTANT
  - Input must be Tomb Raider 1 **PC** levels (``*.PHD``), NOT ``*.PSX``.
  - PSX→PKD is not implemented upstream (``TR1_PSX.h`` is an empty stub).
  - Pre-built demo PKDs (TITLE/GYM/LEVEL1/LEVEL2) are also downloadable with
    ``scripts/fetch_openlara_pkd.py``.

Usage
  python3 scripts/phd_to_pkd.py /path/to/TR1_PC -o sd_assets/openlara

Expected layout (or pass the DATA folder directly)::

  TR1_PC/
    DATA/
      TITLE.PHD
      GYM.PHD
      LEVEL1.PHD
      ...

Optional: place ``screens/TITLE.bmp`` (240×160, 32-bit) next to the staged
work tree if you want the packer to regenerate ``TITLE.SCR``. Otherwise an
existing ``TITLE.SCR`` in the output dir is left alone.

If ``FMV/*.RPL`` is found next to ``DATA/`` (typical TR1 PC install) and
``ffmpeg`` is installed, cutscenes are converted automatically to
``<output>/fmv/*.AVI`` (320×240 MJPEG + mono MP3).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from rpl_to_avi import convert_clips, ffmpeg_available, resolve_fmv_dirs

PACKER_DIR = ROOT / "build" / "phd_packer"
UPSTREAM = "https://raw.githubusercontent.com/XProger/OpenLara/master/src/platform/gba/packer"

PACKER_FILES = [
    "main.cpp",
    "common.h",
    "TR1_PC.h",
    "TR1_PSX.h",
    "out_GBA.h",
    "IMA.h",
    "stb_image_resize.h",
]

LIQ_FILES = [
    "libimagequant.h",
    "libimagequant.c",
    "blur.c",
    "blur.h",
    "kmeans.c",
    "kmeans.h",
    "mediancut.c",
    "mediancut.h",
    "mempool.c",
    "mempool.h",
    "nearest.c",
    "nearest.h",
    "pam.c",
    "pam.h",
]

LEVEL_NAMES = [
    "TITLE", "GYM", "LEVEL1", "LEVEL2", "LEVEL3A", "LEVEL3B", "CUT1",
    "LEVEL4", "LEVEL5", "LEVEL6", "LEVEL7A", "LEVEL7B", "CUT2",
    "LEVEL8A", "LEVEL8B", "LEVEL8C", "LEVEL10A", "CUT3", "LEVEL10B",
    "CUT4", "LEVEL10C",
    # Unfinished Business (TR1 Gold CD2)
    "EGYPT", "CAT", "END", "END2",
]


def download(url: str, dest: Path, *, force: bool = False) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not force and dest.exists() and dest.stat().st_size > 0:
        return
    print(f"  fetch {dest.name}")
    urllib.request.urlretrieve(url, dest)


def fetch_sources() -> None:
    print(f"Fetching packer sources → {PACKER_DIR}")
    for name in PACKER_FILES:
        download(f"{UPSTREAM}/{name}", PACKER_DIR / name)
    for name in LIQ_FILES:
        download(f"{UPSTREAM}/libimagequant/{name}", PACKER_DIR / "libimagequant" / name)


UNIX_COMPAT_H = r"""
#ifndef H_UNIX_COMPAT
#define H_UNIX_COMPAT
/* Minimal stand-ins so the Windows-oriented OpenLara packer builds on Unix. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

static inline void DebugBreak(void) {
    fprintf(stderr, "ASSERT failed\n");
    abort();
}

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

typedef struct {
    char cFileName[MAX_PATH];
    unsigned dwFileAttributes;
} WIN32_FIND_DATA;

#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define INVALID_HANDLE_VALUE ((void *)0)

typedef struct {
    DIR *dir;
    char pattern[256];
    char dirpath[MAX_PATH];
} FindHandle;

static int match_glob(const char *name, const char *pat)
{
    /* Only supports suffix patterns like "*.ad4". */
    const char *star = strchr(pat, '*');
    if (!star)
        return strcmp(name, pat) == 0;
    if (star != pat)
        return 0;
    const char *suf = star + 1;
    size_t n = strlen(name), s = strlen(suf);
    return n >= s && strcmp(name + n - s, suf) == 0;
}

static void *FindFirstFile(const char *path_pat, WIN32_FIND_DATA *fd)
{
    char dirpath[MAX_PATH];
    const char *slash = strrchr(path_pat, '/');
    const char *pat;
    FindHandle *h;
    struct dirent *ent;

    if (slash) {
        size_t n = (size_t)(slash - path_pat);
        if (n >= sizeof(dirpath))
            return INVALID_HANDLE_VALUE;
        memcpy(dirpath, path_pat, n);
        dirpath[n] = 0;
        pat = slash + 1;
    } else {
        strcpy(dirpath, ".");
        pat = path_pat;
    }

    h = (FindHandle *)calloc(1, sizeof(*h));
    if (!h)
        return INVALID_HANDLE_VALUE;
    h->dir = opendir(dirpath);
    if (!h->dir) {
        free(h);
        return INVALID_HANDLE_VALUE;
    }
    strncpy(h->pattern, pat, sizeof(h->pattern) - 1);
    strncpy(h->dirpath, dirpath, sizeof(h->dirpath) - 1);

    while ((ent = readdir(h->dir)) != NULL) {
        if (!match_glob(ent->d_name, h->pattern))
            continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, ent->d_name, sizeof(fd->cFileName) - 1);
        {
            char full[MAX_PATH];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", h->dirpath, ent->d_name);
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
                fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        }
        return h;
    }
    closedir(h->dir);
    free(h);
    return INVALID_HANDLE_VALUE;
}

static int FindNextFile(void *handle, WIN32_FIND_DATA *fd)
{
    FindHandle *h = (FindHandle *)handle;
    struct dirent *ent;
    if (!h || !h->dir)
        return 0;
    while ((ent = readdir(h->dir)) != NULL) {
        if (!match_glob(ent->d_name, h->pattern))
            continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, ent->d_name, sizeof(fd->cFileName) - 1);
        {
            char full[MAX_PATH];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", h->dirpath, ent->d_name);
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
                fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        }
        return 1;
    }
    return 0;
}

static void FindClose(void *handle)
{
    FindHandle *h = (FindHandle *)handle;
    if (!h)
        return;
    if (h->dir)
        closedir(h->dir);
    free(h);
}

typedef void *HANDLE;

#endif
"""

MAKEFILE = r"""
CXX ?= c++
CC  ?= cc
CFLAGS_C   ?= -O2 -std=c11
CFLAGS_CXX ?= -O2 -std=c++17 -fno-exceptions -fno-rtti -Wno-c++11-narrowing
INCLUDES = -I. -Ilibimagequant

LIQ_SRCS = \
  libimagequant/libimagequant.c \
  libimagequant/blur.c \
  libimagequant/kmeans.c \
  libimagequant/mediancut.c \
  libimagequant/mempool.c \
  libimagequant/nearest.c \
  libimagequant/pam.c

LIQ_OBJS = $(LIQ_SRCS:.c=.o)

all: phd_packer

libimagequant/%.o: libimagequant/%.c
	$(CC) $(CFLAGS_C) $(INCLUDES) -c -o $@ $<

phd_packer: main_unix.cpp $(LIQ_OBJS) common_unix.h out_GBA.h TR1_PC.h
	$(CXX) $(CFLAGS_CXX) $(INCLUDES) -o $@ main_unix.cpp $(LIQ_OBJS) -lm

clean:
	rm -f phd_packer $(LIQ_OBJS)
"""


def write_port_files() -> None:
    import re

    (PACKER_DIR / "unix_compat.h").write_text(UNIX_COMPAT_H)
    (PACKER_DIR / "Makefile").write_text(MAKEFILE)

    common = (PACKER_DIR / "common.h").read_text(encoding="utf-8", errors="replace")
    # Upstream packer stops at LEVEL10C; add Unfinished Business names.
    if "LVL_TR1_EGYPT" not in common:
        common = common.replace(
            "    LVL_TR1_10C,\n    LVL_MAX\n};",
            "    LVL_TR1_10C,\n"
            "    LVL_TR1_EGYPT,\n"
            "    LVL_TR1_CAT,\n"
            "    LVL_TR1_END,\n"
            "    LVL_TR1_END2,\n"
            "    LVL_MAX\n};",
        )
        common = common.replace(
            '    "LEVEL10C"\n};',
            '    "LEVEL10C",\n'
            '    "EGYPT",\n'
            '    "CAT",\n'
            '    "END",\n'
            '    "END2"\n};',
        )
    common = common.replace("#include <windows.h>", '#include "unix_compat.h"')
    common = common.replace(
        "#define ASSERT(x) { if (!(x)) { DebugBreak(); } }",
        '#define ASSERT(x) { if (!(x)) { fprintf(stderr, "ASSERT %s:%d: %s\\n", __FILE__, __LINE__, #x); DebugBreak(); } }',
    )
    common = re.sub(
        r"void launchApp\(const char\* cmdline\)\s*\{.*?\n\}",
        "void launchApp(const char* cmdline)\n"
        "{\n"
        "    (void)cmdline; /* Win32 helper unused on Unix */\n"
        "}",
        common,
        count=1,
        flags=re.S,
    )
    (PACKER_DIR / "common_unix.h").write_text(common)

    (PACKER_DIR / "main_unix.cpp").write_text(
        r'''#include "common_unix.h"
#include "TR1_PC.h"
#include "TR1_PSX.h"
#include "out_GBA.h"

TR1_PC* pc[LVL_MAX];
TR1_PSX* psx[LVL_MAX];

int main(int argc, char** argv)
{
    if (argc < 3 || strcmp(argv[1], "gba") != 0) {
        printf("usage: phd_packer gba <output_directory>\n");
        printf("  expects ./TR1_PC/DATA/*.PHD relative to the working directory\n");
        return 1;
    }

    memset(pc, 0, sizeof(pc));
    memset(psx, 0, sizeof(psx));

    for (int32 i = 0; i < LVL_MAX; i++) {
        char fileName[64];
        snprintf(fileName, sizeof(fileName), "TR1_PC/DATA/%s.PHD", levelNames[i]);
        FileStream f(fileName, false);
        if (f.isValid()) {
            pc[i] = new TR1_PC(f, LevelID(i));
            pc[i]->generateLODs();
            pc[i]->cutData();
            printf("loaded %s\n", fileName);
        } else {
            printf("skip \"%s\" (not found)\n", fileName);
        }
    }

    out_GBA* out = new out_GBA();
    out->process(argv[2], pc, NULL);
    delete out;

    for (int32 i = 0; i < LVL_MAX; i++)
        delete pc[i];

    return 0;
}
'''
    )

    # Fresh upstream copy each port so replacements do not stack.
    download(f"{UPSTREAM}/out_GBA.h", PACKER_DIR / "out_GBA.h", force=True)
    gba = (PACKER_DIR / "out_GBA.h").read_text(encoding="utf-8", errors="replace")
    gba = gba.replace('#include "common.h"', '#include "common_unix.h"')
    # Do not open/truncate .PKD until we know the level was loaded.
    old_loop = (
        "        for (int32 i = 0; i < LVL_MAX; i++)\n"
        "        {\n"
        '            sprintf(buf, "%s/%s.PKD", dir, levelNames[i]);\n'
        "            FileStream f(buf, true);\n"
        "            \n"
        "            if (!f.isValid()) {\n"
        '                printf("can\'t save \\"%s\\"\\n", buf);\n'
        "                continue;\n"
        "            }\n\n"
        "            convertGBA(f, pc[i]);\n"
        "        }"
    )
    new_loop = (
        "        for (int32 i = 0; i < LVL_MAX; i++)\n"
        "        {\n"
        "            if (!pc[i]) {\n"
        '                printf("skip null level %s\\n", levelNames[i]);\n'
        "                continue;\n"
        "            }\n"
        '            sprintf(buf, "%s/%s.PKD", dir, levelNames[i]);\n'
        "            FileStream f(buf, true);\n"
        "            if (!f.isValid()) {\n"
        '                printf("can\'t save \\"%s\\"\\n", buf);\n'
        "                continue;\n"
        "            }\n"
        "            convertGBA(f, pc[i]);\n"
        "        }"
    )
    if old_loop not in gba:
        raise SystemExit("out_GBA.h convert loop not found — packer patch failed")
    gba = gba.replace(old_loop, new_loop)
    gba = gba.replace(
        '        convertScreen(dir, "TITLE", pc[LVL_TR1_TITLE]->palette);',
        '        if (pc[LVL_TR1_TITLE])\n'
        '            convertScreen(dir, "TITLE", pc[LVL_TR1_TITLE]->palette);',
    )
    # Upstream hard-requires screens/TITLE.bmp (240x160). Make it optional so
    # PHD→PKD still works when only an existing TITLE.SCR is available.
    gba = gba.replace(
        "        uint32* data = (uint32*)loadBitmap(path, &width, &height, &bpp);\n\n"
        "        ASSERT(data);\n"
        "        ASSERT(width == 240 && height == 160 && bpp == 32);",
        "        uint32* data = (uint32*)loadBitmap(path, &width, &height, &bpp);\n\n"
        "        if (!data) {\n"
        '            printf("skip screen \\"%s\\" (missing %s) — keep existing .SCR if any\\n", name, path);\n'
        "            return;\n"
        "        }\n"
        "        if (!(width == 240 && height == 160 && bpp == 32)) {\n"
        '            printf("skip screen \\"%s\\" (need 240x160x32, got %dx%dx%d)\\n", name, width, height, bpp);\n'
        "            delete[] data;\n"
        "            return;\n"
        "        }",
    )
    gba = gba.replace(
        '        // audio tracks\n'
        '        {\n'
        '            sprintf(buf, "%s/TRACKS.AD4", dir);\n'
        '            FileStream f(buf, true);\n'
        '            convertTracks(f, "tracks/conv_demo");\n'
        '        }',
        '        // audio tracks (only rewrite if source .ad4 chunks exist)\n'
        '        {\n'
        '            WIN32_FIND_DATA fdTracks;\n'
        '            HANDLE hTracks = FindFirstFile("tracks/conv_demo/*.ad4", &fdTracks);\n'
        '            if (hTracks != INVALID_HANDLE_VALUE) {\n'
        '                FindClose(hTracks);\n'
        '                sprintf(buf, "%s/TRACKS.AD4", dir);\n'
        '                FileStream f(buf, true);\n'
        '                convertTracks(f, "tracks/conv_demo");\n'
        '            } else {\n'
        '                printf("skip TRACKS.AD4 (no tracks/conv_demo/*.ad4)\\n");\n'
        '            }\n'
        '        }',
    )
    (PACKER_DIR / "out_GBA.h").write_text(gba)


def build_packer() -> Path:
    bin_path = PACKER_DIR / "phd_packer"
    print("Building phd_packer…")
    subprocess.check_call(["make", "-C", str(PACKER_DIR), "-j"], stdout=sys.stdout)
    if not bin_path.exists():
        raise SystemExit("build failed: phd_packer missing")
    return bin_path


def resolve_phd_data(src: Path) -> Path:
    """Return directory that contains TITLE.PHD / LEVEL1.PHD."""
    src = src.resolve()
    candidates = [
        src / "DATA",
        src / "data",
        src / "TR1_PC" / "DATA",
        src,
    ]
    for c in candidates:
        if (c / "LEVEL1.PHD").is_file() or (c / "TITLE.PHD").is_file():
            return c
    raise SystemExit(
        f"No .PHD levels found under {src}.\n"
        "Need Tomb Raider 1 PC files, e.g. TR1_PC/DATA/LEVEL1.PHD\n"
        "(Retail .PSX dumps cannot be converted — upstream TR1_PSX is a stub.)"
    )


def stage_workdir(phd_data: Path, out_dir: Path) -> Path:
    """
    Packer hardcodes relative paths:
      TR1_PC/DATA/<name>.PHD
      tracks/conv_demo/*.ad4   (optional)
      screens/TITLE.bmp        (optional)
    """
    work = PACKER_DIR / "work"
    if work.exists():
        shutil.rmtree(work)
    data = work / "TR1_PC" / "DATA"
    data.mkdir(parents=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    linked = 0
    for name in LEVEL_NAMES:
        src = phd_data / f"{name}.PHD"
        if not src.is_file():
            # also try lowercase
            alt = phd_data / f"{name}.phd"
            src = alt if alt.is_file() else src
        if src.is_file():
            dest = data / f"{name}.PHD"
            try:
                dest.symlink_to(src.resolve())
            except OSError:
                shutil.copy2(src, dest)
            linked += 1
            print(f"  + {name}.PHD")

    if linked == 0:
        raise SystemExit(f"No PHD files copied from {phd_data}")

    (work / "tracks" / "conv_demo").mkdir(parents=True, exist_ok=True)
    (work / "screens").mkdir(parents=True, exist_ok=True)

    # Prefer shipping existing TRACKS.AD4 rather than empty conversion
    tracks_src = ROOT / "sd_assets" / "openlara" / "TRACKS.AD4"
    if tracks_src.is_file():
        print(f"  (will copy existing {tracks_src.name} after pack)")

    return work


def run_packer(binary: Path, work: Path, out_dir: Path) -> None:
    print(f"Running packer → {out_dir}")
    env = os.environ.copy()

    # Packer used to truncate TRACKS.AD4 even with no source .ad4; keep a backup.
    tracks_dst = out_dir / "TRACKS.AD4"
    tracks_bak = PACKER_DIR / "TRACKS.AD4.bak"
    if tracks_dst.is_file() and tracks_dst.stat().st_size > 1024:
        shutil.copy2(tracks_dst, tracks_bak)

    subprocess.check_call(
        [str(binary), "gba", str(out_dir.resolve())],
        cwd=str(work),
        env=env,
    )

    tracks_src = ROOT / "sd_assets" / "openlara" / "TRACKS.AD4"
    if tracks_bak.is_file() and (
        not tracks_dst.is_file() or tracks_dst.stat().st_size < 1024
    ):
        shutil.copy2(tracks_bak, tracks_dst)
        print(f"  restored {tracks_dst.name} from backup")
    elif (
        tracks_src.is_file()
        and tracks_src.resolve() != tracks_dst.resolve()
        and (not tracks_dst.is_file() or tracks_dst.stat().st_size < 1024)
    ):
        shutil.copy2(tracks_src, tracks_dst)
        print(f"  copied {tracks_src} → {tracks_dst}")

    pkds = sorted(out_dir.glob("*.PKD"))
    print(f"Done. {len(pkds)} PKD file(s) in {out_dir}")
    for p in pkds:
        print(f"  {p.name:12} {p.stat().st_size / 1024:.0f} KiB")
    if tracks_dst.is_file():
        print(f"  {tracks_dst.name:12} {tracks_dst.stat().st_size / 1024:.0f} KiB")
    else:
        print("  WARNING: TRACKS.AD4 missing — run scripts/fetch_openlara_pkd.py")


def convert_fmv_assets(
    tr1_root: Path,
    phd_data: Path,
    out_dir: Path,
    *,
    fmv_override: Path | None = None,
    force: bool = False,
) -> None:
    """Convert TR1 .RPL cutscenes → MJPEG AVI for the G&W player."""
    fmv_out = out_dir / "fmv"
    search_dirs: list[Path] = []

    if fmv_override is not None:
        fmv_override = fmv_override.resolve()
        if not fmv_override.is_dir():
            print(f"WARNING: --fmv not a directory: {fmv_override}")
        else:
            search_dirs = [fmv_override]
    else:
        search_dirs = resolve_fmv_dirs(tr1_root, phd_data)

    if not search_dirs:
        print("\nFMV: no FMV/ folder found (looked next to DATA/) — skipping cutscenes")
        return

    if not ffmpeg_available():
        print("\nFMV: ffmpeg not found — skipping cutscene conversion")
        print("      Install ffmpeg and re-run, or: python3 scripts/rpl_to_avi.py …")
        return

    print(f"\nFMV: converting cutscenes → {fmv_out}")
    for d in search_dirs:
        print(f"  search {d}")
    n = convert_clips(search_dirs, fmv_out, force=force)
    if n == 0:
        avis = sorted(fmv_out.glob("*.AVI")) if fmv_out.is_dir() else []
        if avis:
            print(f"  {len(avis)} AVI(s) already present")
        else:
            print("  no cutscenes converted (missing .RPL in FMV/?)")
    else:
        print(f"  wrote {n} new AVI(s)")


def main() -> None:
    ap = argparse.ArgumentParser(description="Convert TR1 PC .PHD levels to OpenLara .PKD")
    ap.add_argument(
        "tr1_pc",
        type=Path,
        help="Path to TR1_PC folder, TR1_PC/DATA, or a folder of .PHD files",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=ROOT / "sd_assets" / "openlara",
        help="Output directory for .PKD / .SCR (default: sd_assets/openlara)",
    )
    ap.add_argument(
        "--skip-fetch",
        action="store_true",
        help="Do not re-download packer sources",
    )
    ap.add_argument(
        "--skip-build",
        action="store_true",
        help="Do not rebuild phd_packer (use existing binary)",
    )
    ap.add_argument(
        "--skip-fmv",
        action="store_true",
        help="Do not convert FMV/.RPL cutscenes to AVI",
    )
    ap.add_argument(
        "--fmv",
        type=Path,
        default=None,
        metavar="DIR",
        help="FMV source directory (default: auto-detect FMV/ next to DATA/)",
    )
    ap.add_argument(
        "--force-fmv",
        action="store_true",
        help="Re-encode FMV even when AVI is newer than source",
    )
    args = ap.parse_args()

    print(
        "Note: this converts PC .PHD → .PKD using upstream OpenLara logic.\n"
        "      Retail .PSX files are NOT supported by the packer.\n"
    )

    if not args.skip_fetch:
        fetch_sources()
    write_port_files()

    binary = PACKER_DIR / "phd_packer"
    if not args.skip_build or not binary.exists():
        binary = build_packer()

    phd_data = resolve_phd_data(args.tr1_pc)
    work = stage_workdir(phd_data, args.output)
    run_packer(binary, work, args.output.resolve())

    if not args.skip_fmv:
        convert_fmv_assets(
            args.tr1_pc.resolve(),
            phd_data,
            args.output.resolve(),
            fmv_override=args.fmv,
            force=args.force_fmv,
        )


if __name__ == "__main__":
    main()
