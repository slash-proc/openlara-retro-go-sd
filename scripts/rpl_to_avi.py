#!/usr/bin/env python3
"""
Convert Tomb Raider 1 FMV (.RPL / ffmpeg-readable video) to OpenLara G&W AVI.

Output contract (src/platform/gnw/video/ — same as file-manager / videoplayer):
  - 320×240 MJPEG @ 30 fps, **yuvj420p** (4:2:0 — not 4:4:4)
  - mono MP3 @ 48000 Hz
  - JPEG frames ideally < 64 KiB (VIDEO_FRAME_MAX)

yuvj444p overflows the HW JPEG YCbCr scratch (~115 KiB @ 320×240) and
hard-faults the device; always force ``-pix_fmt yuvj420p``.

Used by phd_to_pkd.py and optionally standalone:

  python3 scripts/rpl_to_avi.py /path/to/FMV -o sd_assets/openlara/fmv
  python3 scripts/rpl_to_avi.py FMV/CAFE.RPL -o sd_assets/openlara/fmv/CAFE.AVI
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Clips played by gnw_play_fmv_for_level() in src/platform/gnw/fmv.cpp
FMV_CLIPS = (
    "CORE",
    "CAFE",
    "MANSION",
    "SNOW",
    "LIFT",
    "VISION",
    "CANYON",
    "PYRAMID",
    "PRISON",
    "END",
)

VIDEO_EXTS = (".RPL", ".rpl", ".FMV", ".fmv", ".AVI", ".avi", ".MP4", ".mp4", ".MKV", ".mkv")


def ffmpeg_available() -> bool:
    return shutil.which("ffmpeg") is not None


def resolve_fmv_dirs(tr1_root: Path, phd_data: Path | None = None) -> list[Path]:
    """Common TR1 PC / GOG layouts: sibling FMV/ next to DATA/."""
    roots = [tr1_root.resolve()]
    if phd_data is not None:
        roots.append(phd_data.resolve().parent)
        if phd_data.name.upper() in ("DATA", "data"):
            roots.append(phd_data.resolve().parent.parent)

    seen: set[Path] = set()
    out: list[Path] = []
    for root in roots:
        for name in ("FMV", "fmv", "video/1", "VIDEO/1"):
            d = (root / name).resolve()
            if d.is_dir() and d not in seen:
                seen.add(d)
                out.append(d)
    return out


def find_fmv_source(name: str, search_dirs: list[Path]) -> Path | None:
    """Find CAFE.RPL (case-insensitive stem) under search_dirs."""
    want = name.upper()
    for d in search_dirs:
        if not d.is_dir():
            continue
        for p in d.iterdir():
            if not p.is_file():
                continue
            if p.suffix in VIDEO_EXTS and p.stem.upper() == want:
                return p
    return None


def convert_one(src: Path, dst: Path, *, qscale: int = 6, fps: int = 30) -> None:
    if not ffmpeg_available():
        raise RuntimeError("ffmpeg not found in PATH")

    dst = dst.resolve()
    dst.parent.mkdir(parents=True, exist_ok=True)

    # Must be yuvj420p: STM32 JPEG→DMA2D work buffer is sized for 4:2:0
    # (~115 KiB @ 320×240). Default ffmpeg MJPEG often emits yuvj444p, which
    # needs ~230 KiB and overflows / hard-faults the G&W player (same as
    # file-manager / videoplayer). Match the known-good Amélie encode:
    # 320×240 MJPEG 4:2:0 + mono MP3 @ 48 kHz.
    vf = (
        f"scale=320:240:force_original_aspect_ratio=decrease,"
        f"pad=320:240:(ow-iw)/2:(oh-ih)/2,fps={fps},format=yuvj420p"
    )

    with tempfile.TemporaryDirectory(prefix="ol_fmv_") as tmp:
        tmp_path = Path(tmp)
        vid_tmp = tmp_path / "vid.avi"
        aud_tmp = tmp_path / "aud.mp3"

        subprocess.check_call(
            [
                "ffmpeg",
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-i",
                str(src),
                "-vf",
                vf,
                "-pix_fmt",
                "yuvj420p",
                "-c:v",
                "mjpeg",
                "-q:v",
                str(qscale),
                "-an",
                "-f",
                "avi",
                str(vid_tmp),
            ]
        )
        subprocess.check_call(
            [
                "ffmpeg",
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-i",
                str(src),
                "-vn",
                "-ac",
                "1",
                "-ar",
                "48000",
                "-b:a",
                "96k",
                "-c:a",
                "libmp3lame",
                "-f",
                "mp3",
                str(aud_tmp),
            ]
        )
        subprocess.check_call(
            [
                "ffmpeg",
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-i",
                str(vid_tmp),
                "-i",
                str(aud_tmp),
                "-c",
                "copy",
                "-map",
                "0:v:0",
                "-map",
                "1:a:0",
                str(dst),
            ]
        )


def convert_clips(
    search_dirs: list[Path],
    out_dir: Path,
    clips: tuple[str, ...] = FMV_CLIPS,
    *,
    qscale: int = 6,
    fps: int = 30,
    force: bool = False,
) -> int:
    """Convert named clips; skip missing sources and up-to-date outputs."""
    if not search_dirs:
        print("  (no FMV search directories)")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    converted = 0

    for name in clips:
        dst = out_dir / f"{name}.AVI"
        if dst.is_file() and not force:
            src_mtime = 0.0
            src = find_fmv_source(name, search_dirs)
            if src is not None:
                src_mtime = src.stat().st_mtime
            if dst.stat().st_mtime >= src_mtime:
                print(f"  = {dst.name} (up to date)")
                continue

        src = find_fmv_source(name, search_dirs)
        if src is None:
            print(f"  - {name}.AVI (no source in FMV dirs)")
            continue

        print(f"  → {name}.AVI  ← {src.name}")
        convert_one(src, dst, qscale=qscale, fps=fps)
        converted += 1
        print(f"     {dst.stat().st_size / 1024:.0f} KiB")

    return converted


def convert_tree(
    src: Path,
    out_dir: Path,
    *,
    qscale: int = 6,
    fps: int = 30,
    force: bool = False,
) -> int:
    """Convert every video file in a directory (batch mode)."""
    if src.is_file():
        dst = out_dir
        if out_dir.is_dir() or str(out_dir).endswith("/"):
            dst = out_dir / f"{src.stem.upper()}.AVI"
        if dst.is_file() and not force and dst.stat().st_mtime >= src.stat().st_mtime:
            print(f"  = {dst.name} (up to date)")
            return 0
        print(f"  → {dst.name}  ← {src.name}")
        convert_one(src, dst, qscale=qscale, fps=fps)
        print(f"     {dst.stat().st_size / 1024:.0f} KiB")
        return 1

    if not src.is_dir():
        raise SystemExit(f"not a file or directory: {src}")

    out_dir.mkdir(parents=True, exist_ok=True)
    converted = 0
    for p in sorted(src.iterdir()):
        if not p.is_file() or p.suffix not in VIDEO_EXTS:
            continue
        dst = out_dir / f"{p.stem.upper()}.AVI"
        if dst.is_file() and not force and dst.stat().st_mtime >= p.stat().st_mtime:
            print(f"  = {dst.name} (up to date)")
            continue
        print(f"  → {dst.name}  ← {p.name}")
        convert_one(p, dst, qscale=qscale, fps=fps)
        converted += 1
        print(f"     {dst.stat().st_size / 1024:.0f} KiB")
    return converted


def main() -> None:
    ap = argparse.ArgumentParser(description="Convert TR1 FMV to OpenLara MJPEG AVI")
    ap.add_argument(
        "src",
        type=Path,
        help="FMV file, FMV/ directory, or TR1 root (with --tr1-root)",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="Output .AVI file or directory",
    )
    ap.add_argument(
        "--clips-only",
        action="store_true",
        help=f"Only convert port clips: {', '.join(FMV_CLIPS)}",
    )
    ap.add_argument(
        "--force",
        action="store_true",
        help="Re-encode even when output is newer than source",
    )
    ap.add_argument(
        "--qscale",
        type=int,
        default=6,
        help="MJPEG quality (default 6; lower = better/larger frames)",
    )
    ap.add_argument(
        "--fps",
        type=int,
        default=30,
        help="Output frame rate (default 30)",
    )
    args = ap.parse_args()

    if not ffmpeg_available():
        print("ERROR: ffmpeg required (install ffmpeg and retry)", file=sys.stderr)
        sys.exit(1)

    if args.clips_only:
        dirs = resolve_fmv_dirs(args.src)
        if args.src.is_dir() and args.src not in dirs:
            dirs.insert(0, args.src.resolve())
        n = convert_clips(
            dirs,
            args.output,
            qscale=args.qscale,
            fps=args.fps,
            force=args.force,
        )
    else:
        n = convert_tree(
            args.src,
            args.output,
            qscale=args.qscale,
            fps=args.fps,
            force=args.force,
        )

    print(f"Done. {n} AVI(s) written under {args.output}")


if __name__ == "__main__":
    main()
