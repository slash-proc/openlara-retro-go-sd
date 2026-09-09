# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v0.1.0`); CI reads the matching
section and uses it as the GitHub Release notes.

## [v0.1.0]

First release published to the GWRG distribution spec: alongside the binary the
release now carries a `manifest.json`, an offline bundle and a GitHub Pages
`dist/` tree, so a catalogue or web installer can find and verify this build
without being told where to look.

### Added

- `gwrg.json`, declaring `dataDir: openlara`. OpenLara does not load its levels
  from beside the binary the way most homebrew do: `src/platform/gnw/os.cpp`
  searches a compiled-in list of paths, all of them under a folder spelled
  `openlara`, which matches neither `OpenLara.bin` nor the display name and so
  cannot be derived by anything reading the manifest.
- `manifest.json`, the offline bundle and the `dist/` tree, built by the shared
  `make_manifest.py` / `make_bundle.py` / `build_dist.py`.
- `print-SIDECARS` and `print-RO_BIN` Makefile targets. This homebrew installs
  no sidecar, but `stage_release.py` reads the Makefile positionally and a
  missing target fails the release outright.

### Changed

- `scripts/stage_release.py` replaced with the shared copy, which understands
  `SIDECARS` and publishes full-size cover art.
- `CORE_VERSION` now comes from `git describe --tags --abbrev=0` with a `0.0.0`
  fallback. Plain `git describe --tags --dirty` yields `v1.2.3-4-gabcdef` on any
  commit past a tag, and the packer accepts only `X.Y.Z`.

### Fixed

- Nothing.

### Install

- Unzip the release archive onto the SD card root (`homebrews/OpenLara.bin`).
- Supply your own Tomb Raider data: put the converted `.PKD` levels in
  `/homebrews/openlara/` and cutscenes in `/homebrews/openlara/fmv/`. The
  release publishes no game data, and the PHD to PKD converter is not yet part
  of the manifest — run `scripts/phd_to_pkd.py` locally against a copy of the
  game you own.
- Optional coverflow override: `/covers/homebrew/OpenLara.img` (JPEG <=186x100,
  <=10 KiB).
