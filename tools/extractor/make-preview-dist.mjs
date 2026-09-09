// Builds a local `dist/` so the page can be assembled and tested before CI
// publishes a real one.
//
// The page reads a version index and a manifest and refuses to do anything
// without them, which is correct: a converter it has not hash-matched against
// a manifest is a converter it will not run. That leaves a gap on a developer's
// machine and in the tests, because this project's published manifest declares
// `tools: []` until the manifest generator is taught about the converter, and
// nothing here is allowed to change that.
//
// So this writes a dist of the same shape from local files, and says loudly
// that it did. It is not a second source of truth: CI hands build-page.sh the
// real mirrored dist through DIST_DIR and this never runs there. What it is
// for is making `./build-page.sh && node test-site.mjs site` a real check of
// the page's fetch path rather than a check of nothing.
//
// The binary is a placeholder unless a real one is given, because building
// OpenLara.bin needs an ARM toolchain and this script needs to work without
// one. It is labelled as such in the tag, so a preview build can never be
// mistaken for something installable.
//
//   node make-preview-dist.mjs <out-dir> [--data <TR1 DATA dir>] [--bin <OpenLara.bin>]

import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync } from "node:fs";
import { createHash } from "node:crypto";
import { join, dirname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { inspect } from "./verify.mjs";

const here = dirname(fileURLToPath(import.meta.url));

const args = process.argv.slice(2);
const out = args[0];
if (!out) {
  console.error("usage: make-preview-dist.mjs <out-dir> [--data <DATA dir>] [--bin <file>]");
  process.exit(2);
}
const flag = (name) => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : null;
};
const dataDir = flag("--data");
const binPath = flag("--bin");

const TAG = "v0.0.0-preview";
const sha256 = (b) => createHash("sha256").update(b).digest("hex");
const sha1 = (b) => createHash("sha1").update(b).digest("hex").toUpperCase();

const wasm = new Uint8Array(readFileSync(join(here, "phd_pkd.wasm")));
// The ceiling the page holds the module to comes from the module, not from a
// number typed here twice. test-site.mjs checks the two agree, so reading it
// off the binary is what makes that check meaningful.
const maxMemoryPages = inspect(wasm).memories?.[0]?.max ?? 512;

// The retail levels, when a DATA directory is at hand. The real manifest
// generator will do this from whatever it decides is canonical; here it exists
// so the page's "known release" path has something to match against, and so a
// preview built with no data still works with none.
const variants = [];
let maxInputBytes = 4 * 1024 * 1024;
if (dataDir) {
  for (const name of readdirSync(dataDir).sort()) {
    if (!name.toLowerCase().endsWith(".phd")) continue;
    const bytes = readFileSync(join(dataDir, name));
    variants.push({
      id: basename(name, ".PHD").toLowerCase().replace(/[^a-z0-9]+/g, "-"),
      label: { en: basename(name, ".PHD") },
      sha1: sha1(bytes),
      bytes: bytes.length,
    });
    maxInputBytes = Math.max(maxInputBytes, bytes.length);
  }
}

// A placeholder unless a real binary was handed over. Recognisable on sight,
// and the tag says preview, so nobody can install this by accident and wonder
// why the device does nothing.
const bin = binPath
  ? new Uint8Array(readFileSync(binPath))
  : new TextEncoder().encode(
    "This is not OpenLara.bin. It is a placeholder written by " +
    "make-preview-dist.mjs so the conversion page could be assembled and " +
    "tested without an ARM toolchain. A published release carries the real " +
    "binary in its place.\n");

const manifest = {
  schemaVersion: 1,
  project: "openlara",
  title: "OpenLara",
  docs: "https://github.com/slash-proc/openlara-retro-go-sd#readme",
  originalSystem: "dos",
  source: { repo: "slash-proc/openlara-retro-go-sd", commit: "0".repeat(40), ref: TAG },
  tools: [
    {
      id: "openlara-levels",
      processor: { type: "wasm", version: 1 },
      title: { en: "Tomb Raider level conversion" },
      binary: {
        file: "phd_pkd.wasm",
        url: "phd_pkd.wasm",
        bytes: wasm.length,
        sha256: sha256(wasm),
      },
      limits: { maxMemoryPages, maxOutputBytes: 16 * 1024 * 1024 },
      options: [],
      inputs: [
        {
          id: "levels",
          required: true,
          allowMultiple: true,
          // The axis that makes this page what it is: one run per level, one
          // .PKD out of each, names derived from the file that was converted.
          runPerFile: true,
          maxCount: 64,
          label: { en: "Tomb Raider 1 levels" },
          description: {
            en: "Choose the folder Tomb Raider 1 is installed in, or the CD itself. " +
              "The levels are the .PHD files; they usually sit in a DATA folder, " +
              "and this page will find them wherever they are.",
          },
          extensions: [".PHD"],
          maxBytes: maxInputBytes,
          // Not strict: a level from a patched or fan-translated install cannot
          // match a known hash and converts perfectly well.
          strict: false,
          variants,
        },
      ],
      outputs: [
        {
          id: "pkd",
          // Derived, not fixed: LEVEL1.PHD becomes LEVEL1.PKD and the tool runs
          // once per level, so a fixed filename would collide with itself.
          extension: ".PKD",
          maxBytes: 8 * 1024 * 1024,
          label: { en: "Packed level" },
        },
      ],
    },
  ],
  targets: [
    {
      id: "gnw-retro-go",
      platform: "game-and-watch",
      label: "Game & Watch (Retro-Go SD)",
      kind: "homebrew",
      requiresAbi: { version: 2, minSize: 832 },
      // The folder name compiled into the binary. os.cpp searches a fixed list
      // of paths and every one of them sits under a folder spelled "openlara",
      // which matches neither OpenLara.bin nor the display name, so nothing can
      // derive it and the manifest has to state it.
      dataDir: "openlara",
      artifacts: [
        { filename: "OpenLara.bin", bytes: bin.length, sha256: sha256(bin), url: "OpenLara.bin" },
      ],
      uses: [{ tool: "openlara-levels", outputs: ["pkd"], required: true }],
    },
  ],
};

const versions = {
  schemaVersion: 1,
  project: "openlara",
  versions: [
    {
      tag: TAG,
      manifest: `${TAG}/manifest.json`,
      publishedAt: new Date(0).toISOString(),
      prerelease: true,
      requiresAbi: { version: 2, minSize: 832 },
    },
  ],
};

const dir = join(out, TAG);
mkdirSync(dir, { recursive: true });
writeFileSync(join(out, "versions.json"), JSON.stringify(versions, null, 2) + "\n");
writeFileSync(join(dir, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
writeFileSync(join(dir, "phd_pkd.wasm"), wasm);
writeFileSync(join(dir, "OpenLara.bin"), bin);

console.log(`preview dist written to ${out} (${TAG})`);
console.log(`  ${variants.length} known level hashes` +
  (dataDir ? ` from ${dataDir}` : "; pass --data <DATA dir> to record them"));
console.log(`  OpenLara.bin is ${binPath ? basename(binPath) : "a PLACEHOLDER, not installable"}`);
if (!existsSync(join(dir, "manifest.json"))) process.exit(1);
