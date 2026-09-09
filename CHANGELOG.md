# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v0.1.0`); CI reads the matching
section and uses it as the GitHub Release notes.

## [Unreleased]

### Added

- The `tool` block in `gwrg.json`, so a published manifest declares the level
  converter rather than only the binary. One input that runs per file and one
  output that derives its name from it, plus a variant for each of the 21 levels
  a retail Tomb Raider 1 PC release ships. The SHA-1 values are computed from
  those files and each was cross-checked against the SHA-256 table in
  `tools/extractor/src/level_ids.h`, so the manifest recognises exactly what the
  module recognises and nothing more.
- The converter half of `.github/workflows/ci.yml`: an `extractor` job that
  builds the module, runs its checks, assembles the page and walks the
  assembled site, then attaches the `.wasm` to the release and passes it to
  `make_manifest.py`, with the Pages job rebuilding the page against the dist
  tree it just mirrored.
- Four more page translations, for the seven languages the web builder actually
  offers: Spanish, Polish, Japanese and Korean beside the existing English,
  German and French.

- A browser conversion page under `tools/extractor/page/`. The user points it at
  a Tomb Raider 1 install, it finds the `.PHD` files anywhere in the tree, runs
  the wasm packer once per level in a worker, and hands back one zip already
  laid out for the card: `homebrews/OpenLara.bin` beside
  `homebrews/openlara/*.PKD`. The folder comes from the manifest's `dataDir`,
  which is why that field exists.
- `install.mjs`, the naming and placement half of the spec's host requirements,
  kept out of the page so the tests exercise the same code the page does. The
  module labels its output `pkd` and never names anything; a level's name on the
  card is its own stem with the manifest's declared extension in place of
  whatever it had, checked as a filename and compared case-insensitively per
  directory, because the card folds case and two levels that differ only in
  case would silently become one file there.
- `build-page.sh`, and `test-site.mjs`, `test-install.mjs`, `test-i18n.mjs` and
  `test-convert.mjs` so CI can check the assembled site, the naming rules, the
  translations and a real end-to-end conversion without a browser.

### Changed

- `extract.mjs` is now a library with the command line guarded behind it, the
  way the other projects in this family have it, so the page and `check.sh` drive
  one implementation rather than two. Its command line and its output are
  unchanged.

## [v0.0.1]

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
