/*
 * The wasm processor ABI for the Tomb Raider 1 .PHD -> OpenLara .PKD packer
 * (gwrg-dist-spec spec/04-processor.md, wasm/1).
 *
 * Everything the outside world can reach is in this file: 21 functions and a
 * linear memory. There is no import, no host callback and no filesystem --
 * the packer's fopen/fread/fwrite reach memfs.c and nothing else.
 *
 * ONE LEVEL IN, ONE LEVEL OUT.
 *
 * That is the one real departure from upstream. The OpenLara packer is a
 * directory tool: main.cpp walks TR1_PC/DATA, loads all 21 levels into an
 * array indexed by a hardcoded level enum, and out_GBA::process() then writes
 * a .PKD for each, plus a title screen and an audio track file. The manifest
 * models this input as one file per run (allowMultiple, runPerFile), a
 * browser has no directory to walk, and a host that hands over 21 levels at
 * once would have to hold 21 unpacked levels in memory to get 21 outputs.
 *
 * The restructuring is smaller than it sounds, because out_GBA::convertGBA()
 * -- the function that turns a level into a .PKD -- already takes exactly one
 * level and one output stream, and passes NULL for the cross-level remap
 * table on every call. It shares nothing with the other levels: the only
 * mutable state on out_GBA that convertGBA touches is roomVertices, and
 * writeRooms resets its count at the top of every room. So calling it once,
 * on its own, produces the same bytes as calling it inside the directory
 * loop. check.sh proves that rather than asserting it -- the oracle it
 * compares against is the directory loop, run over the whole of DATA.
 *
 * The two steps that are genuinely per-directory, convertScreen and
 * convertTracks, are not called. Neither reads a .PHD or writes a .PKD:
 * one wants a 240x160 bitmap that ships with no TR1 release, the other
 * converts pre-encoded audio chunks that have nothing to do with levels.
 *
 * Stages exist because a module that imports nothing cannot report progress
 * by calling out, and its memory is not shared, so the host cannot watch a
 * counter either. Work is therefore divided into steps that RETURN, and the
 * four below are the boundaries that genuinely exist in the packer: parse the
 * level, build the texture LODs, pack it, publish the bytes.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common.h"
#include "IMA.h"
#include "TR1_PC.h"
#include "TR1_PSX.h"
#include "out_GBA.h"

#include "level_ids.h"

/*
 * A "pointer" in this ABI is a u32 offset into linear memory, which is what
 * every address on wasm32 already is. The native build in check.sh links the
 * same file against a 64-bit host, where truncating a pointer to 32 bits
 * would corrupt it, so the type widens there and nothing else changes: on the
 * target that ships, abi_ptr IS uint32_t and the exported signatures are
 * exactly what spec/04-processor.md specifies.
 */
#ifdef __wasm__
typedef uint32_t abi_ptr;
#else
typedef uintptr_t abi_ptr;
#endif

extern "C" {
void pkd_memfs_set_input(const unsigned char *data, size_t len);
const unsigned char *pkd_memfs_output(size_t *len);
void pkd_memfs_reset(void);
int  pkd_memfs_input_truncated(void);
void pkd_sha256_hex(const unsigned char *data, size_t len, char out[65]);
#ifdef __wasm__
void __wasm_call_ctors(void);
#else
/* The native harness in check.sh links the same ABI against the host's crt,
   which has already run the constructors before main. */
static inline void __wasm_call_ctors(void) {}
#endif
}

namespace {

/* ---- module state ----------------------------------------------------- */

bool g_ctors_done = false;

struct Input {
    const unsigned char *ptr;
    size_t len;
};

std::vector<Input> g_inputs;
std::vector<unsigned char> g_phd;      /* our own copy of the level */
std::vector<unsigned char> g_result;   /* the finished .PKD */

std::string g_error;
std::string g_warnings;

/* An id from the manifest's tools[].outputs[], NOT a filename. The host
   names the file -- from the level the user supplied, since a .PKD is a
   derived output -- so the module cannot propose a path or an extension. */
const char *const kOutputId = "pkd";

const char *const kStages[] = {
    "Reading level", "Building texture LODs", "Packing", "Writing"
};
constexpr uint32_t kStageCount = sizeof kStages / sizeof kStages[0];

uint32_t g_stage = 0;
bool g_running = false;
bool g_done = false;

TR1_PC *g_level = NULL;

enum : uint32_t {
    kOk = 0,
    kMore = 1,
    kErrBadInput = 2,
    kErrConvert = 3,
    kErrState = 4,
};

void ensure_ctors() {
    /*
     * Built with -nostartfiles and --no-entry, so nothing runs at
     * instantiation: there is no start section and no _initialize export for
     * a host to call (either would be an export the ABI does not declare).
     * The module therefore runs its own static constructors, once, before any
     * state they touch is used.
     */
    if (!g_ctors_done) {
        g_ctors_done = true;
        __wasm_call_ctors();
    }
}

void warn(const std::string &w) {
    if (w.empty()) return;
    if (!g_warnings.empty()) g_warnings += "\n";
    g_warnings += w;
}

void drop_level() {
    delete g_level;
    g_level = NULL;
}

/*
 * Resolve which of the 21 levels this is, by content.
 *
 * Nothing about the conversion depends on the answer except cutData(), which
 * carries hand-listed fixups for GYM, LEVEL1 and LEVEL2. So a file we cannot
 * place is still packed -- the spec is explicit that a module hashes to
 * resolve roles, never to refuse a file -- and the warning says which fixups
 * were therefore skipped.
 */
LevelID identify(const unsigned char *data, size_t len, const TR1_PC *lvl,
                 const char **nameOut) {
    char hex[65];
    pkd_sha256_hex(data, len, hex);
    for (int i = 0; i < kLevelIdentCount; i++) {
        if (strcmp(hex, kLevelIdents[i].sha256) == 0) {
            *nameOut = kLevelIdents[i].name;
            return kLevelIdents[i].id;
        }
    }
    for (int i = 0; i < kLevelIdentCount; i++) {
        const LevelIdent &e = kLevelIdents[i];
        if (lvl->roomsCount == e.rooms && lvl->objectTexturesCount == e.objectTextures &&
            lvl->itemsCount == e.items && lvl->meshOffsetsCount == e.meshOffsets) {
            *nameOut = e.name;
            warn(std::string("this is a modified copy of ") + e.name +
                 " (sha256 " + hex + " is not a known release); packing it as " + e.name);
            return e.id;
        }
    }
    *nameOut = NULL;
    warn(std::string("unrecognised level (sha256 ") + hex +
         "); packed without the level-specific fixups, which only affect "
         "GYM, LEVEL1 and LEVEL2");
    return LVL_TR1_TITLE; /* no cutData() branch keys off TITLE */
}

} // namespace

/* ---- exports ----------------------------------------------------------- */

extern "C" {

uint32_t abi_version(void) { return 1; }

abi_ptr alloc(uint32_t len) {
    ensure_ctors();
    if (len == 0) len = 1;
    void *p = malloc(len);
    return (abi_ptr)(uintptr_t)p;
}

void input_clear(void) {
    ensure_ctors();
    g_inputs.clear();
}

uint32_t input_add(abi_ptr ptr, uint32_t len) {
    ensure_ctors();
    g_inputs.push_back(Input{ (const unsigned char *)(uintptr_t)ptr, (size_t)len });
    return (uint32_t)(g_inputs.size() - 1);
}

uint32_t run_begin(uint32_t flags) {
    ensure_ctors();

    /* The manifest declares no options: the .PKD layout is the one the
       shipped core reads, so there is no bit a caller could set. */
    (void)flags;

    g_error.clear();
    g_warnings.clear();
    g_result.clear();
    g_phd.clear();
    g_stage = 0;
    g_done = false;
    g_running = false;
    drop_level();
    pkd_memfs_reset();

    if (g_inputs.size() != 1) {
        g_error = "expected exactly one .PHD level, got " +
                  std::to_string(g_inputs.size());
        return kErrBadInput;
    }
    /* Smallest real level, CUT2, is 400 KB; the header alone is 4 bytes of
       version plus a tile count. Anything under a kilobyte is not a level. */
    if (g_inputs[0].len < 1024) {
        g_error = "file is too small to be a TR1 level";
        return kErrBadInput;
    }
    {
        uint32_t magic;
        memcpy(&magic, g_inputs[0].ptr, 4);
        if (magic != 0x00000020) {
            /* 0x0000002D is TR2, 0x FF prefixes a PSX .PSX dump. Either way
               the packer would read the file as TR1 and produce nonsense. */
            char buf[64];
            snprintf(buf, sizeof buf, "0x%08X", (unsigned)magic);
            g_error = std::string("not a Tomb Raider 1 PC level: version is ") + buf +
                      ", expected 0x00000020 (PSX .PSX levels are not supported)";
            return kErrBadInput;
        }
    }

    g_running = true;
    return kOk;
}

uint32_t run_step(void) {
    if (!g_running) {
        g_error = "run_step called without run_begin";
        return kErrState;
    }
    if (g_done) return kOk;

    switch (g_stage) {
    case 0: {
        /* Reading level. The copy is not defensive tidiness: TR1_PC keeps
           pointers into nothing it owns, but generateLODs reallocates tiles
           in place, and the host's buffer is not ours to rewrite. */
        g_phd.assign(g_inputs[0].ptr, g_inputs[0].ptr + g_inputs[0].len);
        pkd_memfs_set_input(g_phd.data(), g_phd.size());

        {
            FileStream f("input.PHD", false);
            if (!f.isValid()) {
                g_error = "could not open the level for reading";
                g_running = false;
                return kErrConvert;
            }
            g_level = new TR1_PC(f, LVL_TR1_TITLE);
        }

        /*
         * A .PHD is a chain of counted arrays: there is no way to validate it
         * without parsing it, and no way to unwind out of the parse -- wasm
         * has neither exceptions nor setjmp here. So the parse runs, and
         * memfs records whether it ever read past the end of the file. It
         * does that for every array at once, which is exactly the thing a
         * truncated or wrong-format file gets wrong.
         */
        if (pkd_memfs_input_truncated()) {
            drop_level();
            g_error = "the level is truncated: the packer read past the end of the file";
            g_running = false;
            return kErrConvert;
        }

        g_stage = 1;
        return kMore;
    }
    case 1: {
        /* Building texture LODs, then the level-specific fixups. Both are
           upstream's, called in upstream's order.
           
           Identification sits between them because that is where the counts
           in level_ids.h were measured: generateLODs appends object textures
           for the mipmaps it builds, so a fingerprint taken before it and a
           fingerprint taken after it are different numbers for the same
           level. Taking it here makes the table right by construction --
           build/native/oracle --fingerprint prints it from the same point. */
        g_level->generateLODs();

        const char *name = NULL;
        g_level->id = identify(g_phd.data(), g_phd.size(), g_level, &name);

        g_level->cutData();
        g_stage = 2;
        return kMore;
    }
    case 2: {
        /* Packing. The scope matters: FileStream closes on destruction, and
           closing is what hands the bytes to memfs. */
        out_GBA *out = new out_GBA();
        out->roomVerticesCount = 0;
        out->roomVertices = new out_GBA::RoomVertex[MAX_ROOM_VERTICES];
        {
            FileStream f("output.PKD", true);
            if (!f.isValid()) {
                delete[] out->roomVertices;
                delete out;
                g_error = "could not open the output for writing";
                g_running = false;
                return kErrConvert;
            }
            out->convertGBA(f, g_level);
        }
        delete[] out->roomVertices;
        delete out;

        drop_level();
        g_stage = 3;
        return kMore;
    }
    case 3:
    default: {
        /* Writing: take the bytes memfs collected. */
        size_t len = 0;
        const unsigned char *out = pkd_memfs_output(&len);
        if (!out || len == 0) {
            g_error = "the packer produced no output";
            g_running = false;
            return kErrConvert;
        }
        g_result.assign(out, out + len);
        pkd_memfs_reset();
        g_phd.clear();
        g_phd.shrink_to_fit();
        g_stage = kStageCount;
        g_done = true;
        g_running = false;
        return kOk;
    }
    }
}

uint32_t run(uint32_t flags) {
    uint32_t rc = run_begin(flags);
    if (rc != kOk) return rc;
    for (;;) {
        rc = run_step();
        if (rc == kMore) continue;
        return rc;
    }
}

uint32_t stage_count(void) { return kStageCount; }
uint32_t stage_index(void) { return g_stage; }

abi_ptr stage_name_ptr(uint32_t i) {
    if (i >= kStageCount) return 0;
    return (abi_ptr)(uintptr_t)kStages[i];
}
uint32_t stage_name_len(uint32_t i) {
    if (i >= kStageCount) return 0;
    return (uint32_t)strlen(kStages[i]);
}

uint32_t output_count(void) { return g_result.empty() ? 0u : 1u; }

abi_ptr output_name_ptr(uint32_t i) {
    if (i != 0 || g_result.empty()) return 0;
    return (abi_ptr)(uintptr_t)kOutputId;
}
uint32_t output_name_len(uint32_t i) {
    if (i != 0 || g_result.empty()) return 0;
    return (uint32_t)strlen(kOutputId);
}
abi_ptr output_ptr(uint32_t i) {
    if (i != 0 || g_result.empty()) return 0;
    return (abi_ptr)(uintptr_t)g_result.data();
}
uint32_t output_len(uint32_t i) {
    if (i != 0) return 0;
    return (uint32_t)g_result.size();
}

abi_ptr error_ptr(void) { return (abi_ptr)(uintptr_t)g_error.data(); }
uint32_t error_len(void) { return (uint32_t)g_error.size(); }
abi_ptr warnings_ptr(void) { return (abi_ptr)(uintptr_t)g_warnings.data(); }
uint32_t warnings_len(void) { return (uint32_t)g_warnings.size(); }

} // extern "C"
