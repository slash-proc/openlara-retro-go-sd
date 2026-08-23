# OpenLara for Game & Watch (Retro-Go SD)

GWHB homebrew port of [XProger/OpenLara](https://github.com/XProger/OpenLara)
**fixed-point** engine + GBA MODE4 software rasterizer (C path, no GBA ASM)
for the STM32H7B0 Game & Watch with [Retro-Go SD](https://github.com/sylverb/game-and-watch-retro-go-sd).

Milestone 1: builds `OpenLara.bin`, boots on device, soft-rasters 240×160 and
stretches to the 320×240 LCD (native 320×240 render is next). With `TITLE.PKD` on the SD card it runs `gameInit`; without
it, shows a short “put PKD here” screen.

## Requirements

- `arm-none-eabi-gcc` (v10+, hard-float `fpv5-d16`) **or** Docker image
  `sylverb/retro-go-sd-builder` (default tag `v1.5`)
- Python 3 + Pillow (`pip install -r requirements.txt`) for the cover JPEG
- OpenLara sources under `third_party/OpenLara` (see below)

## Fetch OpenLara + apply GNW patch

```bash
./scripts/fetch_openlara.sh
```

This sparse-clones `src/fixed` + `src/platform/gba` and applies
`patches/0001-openlara-gnw.patch` (`__GNW__` platform block).

Pinned commit is recorded in `third_party/OpenLara/UPSTREAM.txt`.

## Build

```bash
make                    # PROJECT_KIND=homebrew by default
# or:
make docker
```

Output: `OpenLara.bin` → copy to the SD card as `/homebrews/OpenLara.bin`.

### Desktop dev (macOS / Linux, no FMV)

Requires SDL2 (`brew install sdl2` on macOS):

```bash
make host
./OpenLara_host sd_assets/openlara/
# or: OPENLARA_DATA=/path/to/pkds ./OpenLara_host
```

PKDs are read from the path argument, `OPENLARA_DATA`, or `./data/` / `./openlara/`.
Cutscenes are not played on host (device-only HW MJPEG player).

## SD layout (levels)

This homebrew needs **`.PKD`** levels (OpenLara GBA packed format), **not**
raw `.PSX` / `.PHD` from the retail game.

| You have | Use for this port? |
|----------|--------------------|
| `LEVEL1.PSX` (PlayStation dump) | No — wrong container |
| `LEVEL1.PHD` (PC) | No — must be packed to PKD first |
| `LEVEL1.PKD` (GBA OpenLara data) | Yes |

Pre-built PKDs live in the upstream repo under
[`src/platform/gba/data/`](https://github.com/XProger/OpenLara/tree/master/src/platform/gba/data).
A copy for local testing is in `sd_assets/openlara/` after you fetch them
(or copy from that GitHub path).

```
/homebrews/OpenLara.bin
/homebrews/openlara/TITLE.PKD
/homebrews/openlara/TITLE.SCR
/homebrews/openlara/TRACKS.AD4
/homebrews/openlara/GYM.PKD
/homebrews/openlara/LEVEL1.PKD
/homebrews/openlara/LEVEL2.PKD
```

(Also accepted: `/roms/homebrew/openlara/*` for older layouts.)

PKDs are loaded into the **QSPI flash cache** (too large for RAM_EMU). `ROM_READ`
keeps mutable texture tables in RAM so fixups do not write the flash mapping.
`TRACKS.AD4` (~3 MiB) is **streamed from the SD** so it does not fight the PKD
for the flash-cache slot; SFX live inside each PKD.

### Cutscenes (optional FMV)

When you run ``phd_to_pkd.py``, cutscenes are converted automatically if
``FMV/*.RPL`` exists next to your ``DATA/`` folder (typical TR1 PC / GOG
layout) and ``ffmpeg`` is installed:

```bash
python3 scripts/phd_to_pkd.py /path/to/TR1_PC -o sd_assets/openlara
# → sd_assets/openlara/fmv/CAFE.AVI, SNOW.AVI, …
```

Copy the whole ``sd_assets/openlara/`` tree to ``/homebrews/openlara/`` on
the SD card (including the ``fmv/`` subfolder). Missing clips are skipped at
runtime. To convert FMV only:

```bash
python3 scripts/rpl_to_avi.py /path/to/FMV -o sd_assets/openlara/fmv --clips-only
# or: ./scripts/rpl_to_avi.sh /path/to/FMV sd_assets/openlara/fmv
# Must be MJPEG yuvj420p + mono MP3 @ 48 kHz (default ffmpeg MJPEG is often
# yuvj444p and crashes the G&W JPEG decoder).
```

| Level enter | Clip |
|-------------|------|
| TITLE (1st boot) | CORE → CAFE |
| GYM | MANSION |
| LEVEL1 | SNOW |
| LEVEL4 | LIFT |
| LEVEL8A | VISION |
| LEVEL10A | CANYON |
| LEVEL10B | PYRAMID |
| CUT4 | PRISON |
| EGYPT | END |

During playback: **A** skips the cutscene. **PAUSE/SET** opens the normal
Retro-Go game menu (no video transport OSD).

AVI files can live in either place:

```
/homebrews/openlara/CAFE.AVI
/homebrews/openlara/fmv/CAFE.AVI
```

### Converting your own PSX/PHD later

Retail **`.PSX` cannot be converted** today (`TR1_PSX.h` in the upstream packer
is empty). You need Tomb Raider 1 **PC** levels (`.PHD`).

```bash
# Demo PKDs already published by OpenLara (TITLE/GYM/LEVEL1/LEVEL2 + TRACKS):
python3 scripts/fetch_openlara_pkd.py

# Full game from PC PHD files (builds a native Unix port of packer.exe):
python3 scripts/phd_to_pkd.py /path/to/TR1_PC -o sd_assets/openlara
# expects TR1_PC/DATA/TITLE.PHD, LEVEL1.PHD, …
```

Then copy `sd_assets/openlara/*` to `/homebrews/openlara/` on the SD card.
### Controls (v1)

| G&W | OpenLara |
|-----|----------|
| D-pad | Move |
| A | **Action** (switches, grab, push/pull) / confirm |
| B | Jump |
| GAME (Y) | **Walk** (hold while moving; also passport Select) |
| VOLUME + A | Weapon draw / holster |
| VOLUME + B | Look up/down (swim dive aid) |
| VOLUME + GAME | Look (camera) |
| TIME (X) | Inventory |
| MENU | Retro-Go pause (not bound in-game) |

To push a block: face it, hold **A** (Action) and press into it. Holster weapons first (**VOLUME+A**) if Lara has pistols out — Action then shoots instead.

Copy `TITLE.SCR` (38 KiB) next to the PKDs for the title background, and
`TRACKS.AD4` for music (both from OpenLara GBA `data/`).
Without `TRACKS.AD4` the game still runs; only music is silent.

## Layout

```
Makefile                 Homebrew pack → OpenLara.bin
src/platform/gnw/        OS, present, input, sound, app_main
src/platform/gnw/ol →    symlink to third_party/OpenLara/src/fixed
third_party/OpenLara/    Upstream fixed engine + GBA rasterizer (patched)
patches/                 __GNW__ diffs for OpenLara
sdk/                     Retro-Go SD core SDK (ABI bridge)
scripts/fetch_openlara.sh
```

`src/main.c` is the old template skeleton and is **not** linked for this
project.

## Notes / next milestones

- Saves / settings not wired yet.
- Render is still GBA MODE4 240×160, stretched to the LCD; switching to the
  DOS MODE13 rasterizer (native 320×240) is the real resolution upgrade.
- Retail `.PSX` levels are not supported by the fixed engine (no `fmt/psx.h`);
  keep using GBA `.PKD` (from PC `.PHD` via the upstream packer).
- Hot paths may move to ITCM later; v1 keeps everything in RAM_EMU.

## License

Port glue in this repo is MIT (see `LICENSE`). OpenLara remains
BSD-2-Clause (see `third_party/OpenLara/LICENSE`). Vendored SDK headers keep
their upstream licenses. You need legally obtained Tomb Raider level data
(PKD); this repo does not ship game assets.
