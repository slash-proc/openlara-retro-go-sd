#!/usr/bin/env python3
"""Download OpenLara GBA demo PKD/SCR/TRACKS into sd_assets/openlara/."""

from __future__ import annotations

import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "sd_assets" / "openlara"
BASE = "https://raw.githubusercontent.com/XProger/OpenLara/master/src/platform/gba/data"

FILES = [
    "TITLE.PKD",
    "TITLE.SCR",
    "GYM.PKD",
    "LEVEL1.PKD",
    "LEVEL2.PKD",
    "TRACKS.AD4",
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        dest = OUT / name
        url = f"{BASE}/{name}"
        print(f"{name} …")
        urllib.request.urlretrieve(url, dest)
        print(f"  → {dest} ({dest.stat().st_size / 1024:.0f} KiB)")
    print("Done. Copy sd_assets/openlara/* to /homebrews/openlara/ on the SD card.")


if __name__ == "__main__":
    main()
