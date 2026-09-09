// Page translations and the locale store.
//
// Dependency-free by necessity: this page ships as static files to GitHub
// Pages and the whole point of the project is that what runs is settled at
// build time, so pulling an i18n library from a CDN at load time would be
// working against it. The shape mirrors the one the other projects in this
// family vendor: strings nested by section rather than flat dotted keys,
// English as the base that every other locale is checked against, values that
// need arguments written as functions returning template literals, and the
// language remembered in localStorage under "locale".
//
// House style for the copy below:
//   - "convert", not "extract". Users are converting files they own into ones
//     the port can read; "extraction" is our word, not theirs.
//   - "level", not "asset" or "input file". The user has 21 levels.
//   - no em dashes anywhere. Break the sentence or use a comma.
//   - translations are written the way someone would actually speak. German in
//     particular should not chain every proper noun into one compound.

export const SUPPORTED = [
  { code: "en", label: "English" },
  { code: "fr", label: "Français" },
  { code: "de", label: "Deutsch" },
];

// English is the base. Every other locale below is a translation of exactly
// these keys; a missing key falls back to the English one rather than showing
// a blank or a key name to the user.
const en = {
  app: {
    heading: (title) => `${title} level converter`,
    lede: (game, ext) => `Converts ${game} levels into the ${ext} files this port reads`,
  },
  lang: {
    label: "Language",
    aria: "Choose the language of this page",
  },
  io: {
    input: "Input",
    output: "Output",
  },
  why: {
    label: "What this does",
    text: "Runs in the browser. Game files are not uploaded anywhere.",
  },
  input: {
    heading: "Game folder",
    // Two ways in, and the folder is the one to reach for: the levels sit in a
    // subfolder whose name differs between a CD, a GOG install and a copy
    // someone made, and the page looks through all of it either way.
    chooseFolder: "Choose folder",
    chooseFiles: "Choose files instead",
    scanning: (n) => `${n} file${n === 1 ? "" : "s"}`,
    reading: (name) => name,
    bytes: (n) => `${n.toLocaleString("en-GB")} bytes`,
    // The heading over the list, which is the whole point of showing one: the
    // user sees what was found before a single conversion is spent.
    foundHeading: (n) => `${n} level${n === 1 ? "" : "s"} found`,
    foundCount: (n, ignored, root) =>
      `${n} level${n === 1 ? "" : "s"} found${root ? ` in ${root}` : ""}` +
      (ignored ? `, and ${ignored} other file${ignored === 1 ? "" : "s"} ignored.` : "."),
    // The likeliest thing to go wrong, so it says what was looked for and
    // where rather than leaving an empty list that reads like a crash.
    noneFound: (exts, where, n) =>
      `No ${exts} files anywhere in ${where}, out of ${n} file${n === 1 ? "" : "s"}. ` +
      `Choose the folder Tomb Raider is installed in, or the CD itself.`,
    theSelection: "the selection",
    // Recognised means "this hashes to a release the project knows". It is not
    // the last word: the module has its own table and says so after the run.
    recognised: "known release",
    unknownYet: "not a known release",
    duplicateStem: (stem, kept) =>
      `another copy of ${stem}; converting ${kept} instead, since both would be ` +
      `written as the same file. Choose a narrower folder if that is the wrong one.`,
    notRecognised: (sha1) => `not a release this version accepts (SHA-1 ${sha1}).`,
    tooLarge: (size, max) => `${size} bytes, larger than the ${max} a level can be.`,
    unusableName: (why) => `cannot be named on the card: ${why}.`,
    tooMany: (n, max) =>
      `${n} levels is more than the ${max} this version converts in one go. ` +
      `Choose a folder holding fewer of them.`,
  },
  run: {
    heading: "Convert",
    button: "Convert",
    converting: (i, n, name) => `${name}, ${i} of ${n}`,
    timedOut: (name) => `${name} took too long and was stopped.`,
    done: (n) => `${n} file${n === 1 ? "" : "s"} ready.`,
    failed: (name, msg) => `${name} could not be converted: ${msg}`,
    tooBig: (name, size, max) =>
      `${name} came out at ${size} bytes, more than the ${max} its manifest allows.`,
  },
  version: {
    label: "Version",
    showPrereleases: "Show pre-releases",
    prerelease: "pre-release",
    abi: (version, minSize) => `firmware ABI ${version}+ (${minSize} bytes)`,
    retained: (n) => `Showing the ${n} most recent releases.`,
    olderReleases: "Older releases",
    pinned: (tag) => `Version ${tag}`,
    noConverter: "This version needs no conversion: install the published files as they are.",
  },
  zip: {
    button: (n, size) =>
      `Download the install zip (${n} file${n === 1 ? "" : "s"}, ${size})`,
    // The layout is the useful fact: the zip already puts each file where it
    // goes, so all the user has to do is copy the folder onto the card.
    note: (binaries, dir, n) =>
      `One zip, laid out for the card: ${binaries}, and ${n} level` +
      `${n === 1 ? "" : "s"} in ${dir}`,
    building: "Packing the archive",
    ready: (name, bytes) => `${name} saved, ${bytes.toLocaleString("en-GB")} bytes.`,
    failed: (msg) => `Could not build the zip: ${msg}`,
    fetchFailed: (name, status) => `could not fetch ${name} (${status})`,
    sizeMismatch: (name, got, want) =>
      `${name} is ${got} bytes, but its manifest says ${want}`,
    hashMismatch: (name) => `${name} does not match the hash in its manifest`,
  },
  results: {
    heading: "Converted files",
    bytes: (n) => `${n.toLocaleString("en-GB")} bytes`,
    mb: (n) => `${n.toLocaleString("en-GB", { maximumFractionDigits: 1 })} MB`,
    // The table is collapsed by default, so its summary has to say what is
    // inside it and how much of it there is.
    summary: (n) => `Per-file detail, ${n} file${n === 1 ? "" : "s"}`,
    colSource: "Level",
    colFile: "Output",
    colStatus: "Status",
    colSize: "Size",
    hash: "SHA-256",
    colDownload: "Download",
    save: "Save",
    // Three outcomes, and only one of them is a problem. A level the module
    // cannot place against its table of retail releases still converted: a
    // modded or fan-translated level is not a known release by construction.
    known: "Known release",
    unknown: "Not a known release",
    failed: "Failed",
  },
  footer: {
    source: "Source and documentation:",
    repo: "the project repository",
    published: "This page is published from the same CI run that builds and " +
      "verifies the module, so the two always match.",
  },
  fatal: {
    cannotRun: (msg) => `This page cannot run: ${msg}`,
    noManifest: "the converter manifest could not be loaded",
    noVersions: "this site publishes no versions",
    mismatch: "the converter does not match its manifest",
    unsafe: (errs) => `the converter failed its safety checks: ${errs}`,
  },
};

const fr = {
  app: {
    heading: (title) => `Convertisseur de niveaux ${title}`,
    lede: (game, ext) =>
      `Convertit les niveaux ${game} en fichiers ${ext}, ceux que ce portage lit`,
  },
  lang: {
    label: "Langue",
    aria: "Choisir la langue de cette page",
  },
  io: {
    input: "Entrée",
    output: "Sortie",
  },
  why: {
    label: "Ce que fait cette page",
    text: "Tout se passe dans le navigateur. Aucun fichier de jeu n'est envoyé.",
  },
  input: {
    heading: "Dossier de jeu",
    chooseFolder: "Choisir un dossier",
    chooseFiles: "Choisir des fichiers",
    scanning: (n) => `${n} fichier${n === 1 ? "" : "s"}`,
    reading: (name) => name,
    bytes: (n) => `${n.toLocaleString("fr-FR")} octets`,
    foundHeading: (n) => `${n} niveau${n === 1 ? "" : "x"} trouvé${n === 1 ? "" : "s"}`,
    foundCount: (n, ignored, root) =>
      `${n} niveau${n === 1 ? "" : "x"} trouvé${n === 1 ? "" : "s"}${root ? ` dans ${root}` : ""}` +
      (ignored ? `, et ${ignored} autre${ignored === 1 ? "" : "s"} fichier${ignored === 1 ? "" : "s"} ignoré${ignored === 1 ? "" : "s"}.` : "."),
    noneFound: (exts, where, n) =>
      `Aucun fichier ${exts} dans ${where}, sur ${n} fichier${n === 1 ? "" : "s"}. ` +
      `Choisir le dossier où Tomb Raider est installé, ou le CD lui-même.`,
    theSelection: "la sélection",
    recognised: "version connue",
    unknownYet: "version inconnue",
    duplicateStem: (stem, kept) =>
      `autre copie de ${stem} ; c'est ${kept} qui est converti, les deux portant ` +
      `le même nom une fois écrits. Choisir un dossier plus précis si ce n'est pas le bon.`,
    notRecognised: (sha1) => `version refusée par ce convertisseur (SHA-1 ${sha1}).`,
    tooLarge: (size, max) => `${size} octets, plus que les ${max} d'un niveau.`,
    unusableName: (why) => `ne peut pas porter ce nom sur la carte : ${why}.`,
    tooMany: (n, max) =>
      `${n} niveaux, c'est plus que les ${max} convertis en une fois par cette version. ` +
      `Choisir un dossier qui en contient moins.`,
  },
  run: {
    heading: "Convertir",
    button: "Convertir",
    converting: (i, n, name) => `${name}, ${i} sur ${n}`,
    timedOut: (name) => `${name} a pris trop de temps et a été interrompu.`,
    done: (n) => `${n} fichier${n === 1 ? "" : "s"} prêt${n === 1 ? "" : "s"}.`,
    failed: (name, msg) => `${name} n'a pas pu être converti : ${msg}`,
    tooBig: (name, size, max) =>
      `${name} fait ${size} octets, plus que les ${max} autorisés par son manifeste.`,
  },
  version: {
    label: "Version",
    showPrereleases: "Afficher les préversions",
    prerelease: "préversion",
    abi: (version, minSize) => `ABI firmware ${version}+ (${minSize} octets)`,
    retained: (n) => `Les ${n} versions les plus récentes.`,
    olderReleases: "Versions plus anciennes",
    pinned: (tag) => `Version ${tag}`,
    noConverter: "Cette version ne demande aucune conversion : installer les fichiers publiés tels quels.",
  },
  zip: {
    button: (n, size) =>
      `Télécharger l'archive d'installation (${n} fichier${n === 1 ? "" : "s"}, ${size})`,
    note: (binaries, dir, n) =>
      `Une archive, déjà rangée pour la carte : ${binaries}, et ${n} niveau` +
      `${n === 1 ? "" : "x"} dans ${dir}`,
    building: "Création de l'archive",
    ready: (name, bytes) => `${name} enregistré, ${bytes.toLocaleString("fr-FR")} octets.`,
    failed: (msg) => `Impossible de créer l'archive : ${msg}`,
    fetchFailed: (name, status) => `${name} n'a pas pu être récupéré (${status})`,
    sizeMismatch: (name, got, want) =>
      `${name} fait ${got} octets alors que son manifeste en annonce ${want}`,
    hashMismatch: (name) => `${name} ne correspond pas à l'empreinte de son manifeste`,
  },
  results: {
    heading: "Fichiers convertis",
    bytes: (n) => `${n.toLocaleString("fr-FR")} octets`,
    mb: (n) => `${n.toLocaleString("fr-FR", { maximumFractionDigits: 1 })} Mo`,
    summary: (n) => `Détail par fichier, ${n} fichier${n === 1 ? "" : "s"}`,
    colSource: "Niveau",
    colFile: "Sortie",
    colStatus: "État",
    colSize: "Taille",
    hash: "SHA-256",
    colDownload: "Téléchargement",
    save: "Enregistrer",
    known: "Version connue",
    unknown: "Version inconnue",
    failed: "Échec",
  },
  footer: {
    source: "Code source et documentation :",
    repo: "le dépôt du projet",
    published: "Cette page est publiée par la même exécution CI que celle qui " +
      "construit et vérifie le module : les deux vont toujours ensemble.",
  },
  fatal: {
    cannotRun: (msg) => `Cette page ne peut pas fonctionner : ${msg}`,
    noManifest: "le manifeste du convertisseur n'a pas pu être chargé",
    noVersions: "ce site ne publie aucune version",
    mismatch: "le convertisseur ne correspond pas à son manifeste",
    unsafe: (errs) => `le convertisseur a échoué aux contrôles de sécurité : ${errs}`,
  },
};

const de = {
  app: {
    heading: (title) => `${title} Level-Konverter`,
    lede: (game, ext) =>
      `Wandelt ${game}-Level in die ${ext}-Dateien um, die dieser Port liest`,
  },
  lang: {
    label: "Sprache",
    aria: "Sprache dieser Seite wählen",
  },
  io: {
    input: "Eingabe",
    output: "Ausgabe",
  },
  why: {
    label: "Was hier passiert",
    text: "Läuft im Browser. Es werden keine Spieldateien hochgeladen.",
  },
  input: {
    heading: "Spielordner",
    chooseFolder: "Ordner auswählen",
    chooseFiles: "Dateien auswählen",
    scanning: (n) => `${n} Datei${n === 1 ? "" : "en"}`,
    reading: (name) => name,
    bytes: (n) => `${n.toLocaleString("de-DE")} Bytes`,
    foundHeading: (n) => `${n} Level gefunden`,
    foundCount: (n, ignored, root) =>
      `${n} Level gefunden${root ? `, in ${root}` : ""}` +
      (ignored ? `, und ${ignored} andere Datei${ignored === 1 ? "" : "en"} übergangen.` : "."),
    noneFound: (exts, where, n) =>
      `Keine ${exts}-Dateien in ${where}, bei ${n} Datei${n === 1 ? "" : "en"} insgesamt. ` +
      `Den Ordner wählen, in dem Tomb Raider installiert ist, oder die CD selbst.`,
    theSelection: "der Auswahl",
    recognised: "bekannte Fassung",
    unknownYet: "unbekannte Fassung",
    duplicateStem: (stem, kept) =>
      `noch eine Kopie von ${stem}; umgewandelt wird ${kept}, weil beide unter ` +
      `demselben Namen landen würden. Einen engeren Ordner wählen, falls das die falsche ist.`,
    notRecognised: (sha1) => `keine Fassung, die diese Version annimmt (SHA-1 ${sha1}).`,
    tooLarge: (size, max) => `${size} Bytes, mehr als die ${max} eines Levels.`,
    unusableName: (why) => `kann auf der Karte nicht so heißen: ${why}.`,
    tooMany: (n, max) =>
      `${n} Level sind mehr als die ${max}, die diese Version auf einmal umwandelt. ` +
      `Einen Ordner mit weniger davon wählen.`,
  },
  run: {
    heading: "Umwandeln",
    button: "Umwandeln",
    converting: (i, n, name) => `${name}, ${i} von ${n}`,
    timedOut: (name) => `${name} hat zu lange gedauert und wurde abgebrochen.`,
    done: (n) => `${n} Datei${n === 1 ? "" : "en"} bereit.`,
    failed: (name, msg) => `${name} konnte nicht umgewandelt werden: ${msg}`,
    tooBig: (name, size, max) =>
      `${name} ist ${size} Bytes groß, mehr als die im Manifest erlaubten ${max}.`,
  },
  version: {
    label: "Version",
    showPrereleases: "Vorabversionen anzeigen",
    prerelease: "Vorabversion",
    abi: (version, minSize) => `Firmware-ABI ${version}+ (${minSize} Bytes)`,
    retained: (n) => `Die ${n} neuesten Versionen.`,
    olderReleases: "Ältere Versionen",
    pinned: (tag) => `Version ${tag}`,
    noConverter: "Diese Version braucht keine Umwandlung: die veröffentlichten Dateien so installieren, wie sie sind.",
  },
  zip: {
    button: (n, size) =>
      `Installations-Archiv herunterladen (${n} Datei${n === 1 ? "" : "en"}, ${size})`,
    note: (binaries, dir, n) =>
      `Ein Archiv, schon passend für die Karte: ${binaries}, und ${n} Level in ${dir}`,
    building: "Archiv wird gepackt",
    ready: (name, bytes) => `${name} gespeichert, ${bytes.toLocaleString("de-DE")} Bytes.`,
    failed: (msg) => `Das Archiv konnte nicht erstellt werden: ${msg}`,
    fetchFailed: (name, status) => `${name} konnte nicht geladen werden (${status})`,
    sizeMismatch: (name, got, want) =>
      `${name} hat ${got} Bytes, das Manifest nennt aber ${want}`,
    hashMismatch: (name) => `${name} passt nicht zur Prüfsumme im Manifest`,
  },
  results: {
    heading: "Umgewandelte Dateien",
    bytes: (n) => `${n.toLocaleString("de-DE")} Bytes`,
    mb: (n) => `${n.toLocaleString("de-DE", { maximumFractionDigits: 1 })} MB`,
    summary: (n) => `Details je Datei, ${n} Datei${n === 1 ? "" : "en"}`,
    colSource: "Level",
    colFile: "Ausgabe",
    colStatus: "Status",
    colSize: "Größe",
    hash: "SHA-256",
    colDownload: "Download",
    save: "Speichern",
    known: "Bekannte Fassung",
    unknown: "Unbekannte Fassung",
    failed: "Fehlgeschlagen",
  },
  footer: {
    source: "Quellcode und Dokumentation:",
    repo: "das Projekt-Repository",
    published: "Diese Seite wird vom selben CI-Lauf veröffentlicht, der das Modul " +
      "baut und prüft. Beide passen also immer zusammen.",
  },
  fatal: {
    cannotRun: (msg) => `Diese Seite funktioniert nicht: ${msg}`,
    noManifest: "das Manifest des Konverters konnte nicht geladen werden",
    noVersions: "diese Seite veröffentlicht keine Versionen",
    mismatch: "der Konverter passt nicht zu seinem Manifest",
    unsafe: (errs) => `der Konverter hat die Sicherheitsprüfungen nicht bestanden: ${errs}`,
  },
};

const STRINGS = { en, fr, de };

// Fall back key by key rather than whole-locale, so a partial translation
// degrades to English only where it is actually missing.
function withFallback(locale) {
  const base = STRINGS.en;
  const over = STRINGS[locale] ?? {};
  const out = {};
  for (const section of Object.keys(base)) {
    out[section] = { ...base[section], ...(over[section] ?? {}) };
  }
  return out;
}

// A visitor whose browser is set to a language we speak should get it without
// touching anything. Region is ignored: de-AT and de-DE both get German.
function matchBrowser() {
  for (const tag of navigator.languages ?? [navigator.language ?? "en"]) {
    const want = tag.toLowerCase().split("-")[0];
    if (SUPPORTED.some((l) => l.code === want)) return want;
  }
  return "en";
}

function initial() {
  let stored = null;
  try {
    stored = localStorage.getItem("locale");
  } catch { /* storage blocked: fall back to the browser's language */ }
  // An explicit choice outranks the browser's setting, but only once made.
  return SUPPORTED.some((l) => l.code === stored) ? stored : matchBrowser();
}

let current = initial();
let strings = withFallback(current);
const listeners = new Set();

export const locale = () => current;
export const t = () => strings;

export function setLocale(code) {
  if (!SUPPORTED.some((l) => l.code === code)) return;
  current = code;
  strings = withFallback(code);
  try {
    localStorage.setItem("locale", code);
  } catch { /* not persisting is survivable; the page still switches */ }
  document.documentElement.lang = code;
  for (const fn of listeners) fn();
}

/** Runs `fn` now and again on every language change. */
export function onLocaleChange(fn) {
  listeners.add(fn);
  fn();
}

/**
 * Reads a localised string out of the manifest, which stores them as
 * `{en, fr, de}` objects. Plain strings are passed through so a manifest that
 * has not been localised still renders.
 */
export function localeText(value) {
  if (value == null) return "";
  if (typeof value === "string") return value;
  return value[current] ?? value.en ?? "";
}

/**
 * Applies translations to any element carrying `data-i18n="section.key"`, plus
 * `data-i18n-title` and `data-i18n-aria-label` for the attribute forms. Keeps
 * the static markup readable and means adding a string to the page is one
 * attribute rather than a line of JS.
 */
export function applyStatic(root = document) {
  const lookup = (path) => {
    const [section, key] = path.split(".");
    const v = strings[section]?.[key];
    return typeof v === "function" ? v() : v;
  };
  for (const el of root.querySelectorAll("[data-i18n]")) {
    const v = lookup(el.dataset.i18n);
    if (v != null) el.textContent = v;
  }
  for (const el of root.querySelectorAll("[data-i18n-title]")) {
    const v = lookup(el.dataset.i18nTitle);
    if (v != null) el.title = v;
  }
  for (const el of root.querySelectorAll("[data-i18n-aria-label]")) {
    const v = lookup(el.dataset.i18nAriaLabel);
    if (v != null) el.setAttribute("aria-label", v);
  }
}
