# The browser conversion page

A static page that converts a Tomb Raider 1 install into the `.PKD` files this
port reads, published to GitHub Pages by CI. It serves two purposes:

1. **For users** — a way to produce a card's worth of levels without a
   terminal, an ARM toolchain, or a copy of the packer.
2. **For the project** — the reference consumer of the GWRG distribution spec.
   It uses the same `verify.mjs`, `extract.mjs` and `install.mjs` a command line
   or a web builder does, so if the spec or the ABI drifts, this page breaks in
   CI first.

It is also the **distribution endpoint**: GitHub release assets are not
CORS-fetchable, so a consuming web tool reads `manifest.json` and the module
from this Pages site. See
[spec/01-distribution.md](https://github.com/slash-proc/gwrg-dist-spec/blob/main/spec/01-distribution.md).

## Files

| | |
|---|---|
| `index.html` | markup |
| `style.css` | styles; light and dark |
| `app.js` | fetches and verifies the module, drives the flow |
| `worker.js` | runs one conversion off the main thread |
| `i18n.js` | page translations, English base |

`build-page.sh` assembles these with `verify.mjs`, `extract.mjs`,
`install.mjs`, `zip.mjs` and a mirrored `dist/` into `site/`.

## What makes this one different

Every other project in this family converts one ROM into one file. This one
takes a library. The manifest states that rather than the page assuming it: the
input declares `runPerFile`, so the module runs once for every level, and the
output declares an `extension` rather than a `filename`, so each produced file
is named from the level it came from.

**The user picks one folder and the page finds the levels underneath it.** The
retail CD keeps them in `DATA/`, an installed copy keeps them somewhere else,
and someone may well point at a folder holding several of those. The page walks
the whole subtree and filters by extension, case-insensitively; the shape of the
tree decides nothing.

**It says what it found before it converts anything.** Pointing at the wrong
folder is then visible in the list rather than after twenty conversions, and the
count of files that were none of our business is stated so nobody has to wonder
what happened to the rest of the disc.

**Two copies of one level are a question, not a surprise.** They would be
written under the same name, so the first is converted, the second is named in
the skipped list beside the path that won, and the user can narrow the selection
if that was the wrong call.

**The zip is laid out for the card.** `kind` says which directory a homebrew
installs into and `dataDir` says which folder below it holds the data, so the
archive already contains `homebrews/OpenLara.bin` and
`homebrews/openlara/LEVEL1.PKD` rather than a pile of files and a layout to work
out. Every path segment is validated; none of them comes from the module.

## Design notes

**Verification is silent.** Users cannot act on its details and saying
"verified!" is reassurance, not information. The info box, which answers *what
goes in and what comes out*, renders only once the module has been hash-matched
and verified, so its presence is the result of the check while its content is
something a user wants. On failure the page says it cannot run, and why.

**"Not a release we know" is a note, not an error.** The module identifies a
level by content and warns, with a sha256, when it matches none of the 21 retail
levels. The file still converted, correctly, unless it is one of the three the
packer carries hand-listed fixups for. So it reads as a line under the file
rather than as a failed run.

**One worker per level.** A level costs the module up to about 16 MiB and wasm
memory only ever grows, so a single instance converting all 21 would hold the
largest level's high-water mark until the page was closed. Terminating the
worker is the only way to give that back, and it is also the only way to
implement a timeout, since the ABI has no cancellation flag.

## Two failure modes to know about

`verify.mjs`, `extract.mjs`, `install.mjs` and `zip.mjs` are loaded directly by
the browser as well as run under node. Node-only constructs in them throw at
import time and take the page down **silently** — no error anywhere, just a page
where nothing happens. Both a `#!/usr/bin/env node` shebang and a bare
`process.argv` in a CLI block have bitten the page this one is descended from.
`build-page.sh` fails the build on either, and `test-site.mjs` checks the same
thing on the assembled result, because the two can be run apart.

## Checking it

```bash
./build-page.sh                                  # assembles site/
node test-site.mjs site                          # the wiring, no browser
node test-install.mjs                            # the naming and placement rules
node test-i18n.mjs                               # every string, every locale
node test-convert.mjs site /path/to/TR1_CD       # a real install, end to end
```

`test-convert.mjs` imports its code out of `site/` rather than out of the source
tree, so what it exercises is the files a browser would fetch. It converts every
level it finds, builds the zip, reads the archive's central directory back, and
checks `LEVEL1.PKD` against the hash of XProger's published one.
