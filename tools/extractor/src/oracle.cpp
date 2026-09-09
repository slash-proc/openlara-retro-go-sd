/*
 * The native oracle: upstream's own directory-at-a-time packer.
 *
 * check.sh builds this from the same patched sources as the wasm module and
 * compares one level's .PKD byte for byte. It exists to answer the only
 * question that matters about a port like this -- does restructuring the
 * packer to convert ONE level in memory change what it produces? -- and it
 * can only answer it by doing the thing the module does not: loading the
 * whole DATA directory in a single run, exactly as main.cpp does.
 *
 * The loop below is the body of out_GBA::process() with the two steps the
 * PHD -> PKD path has nothing to do with removed: convertScreen, which wants
 * a 240x160 screens/TITLE.bmp that ships with no TR1 release, and
 * convertTracks, which converts pre-encoded audio chunks. Neither reads a
 * .PHD or writes a .PKD. Everything that does is upstream's, untouched.
 *
 *   oracle <DATA-dir> <out-dir>     pack every level found
 *   oracle --fingerprint <DATA-dir> print the table src/level_ids.h holds
 */
#include <string>

#include "common.h"
#include "IMA.h"
#include "TR1_PC.h"
#include "TR1_PSX.h"
#include "out_GBA.h"

static TR1_PC* pc[LVL_MAX];

int main(int argc, char** argv)
{
    bool fingerprint = (argc > 1 && strcmp(argv[1], "--fingerprint") == 0);
    if (fingerprint) { argv++; argc--; }

    if (argc < (fingerprint ? 2 : 3)) {
        printf("usage: oracle <DATA-dir> <out-dir>\n");
        printf("       oracle --fingerprint <DATA-dir>\n");
        return 2;
    }

    const char* data = argv[1];
    char buf[1024];

    for (int32 i = 0; i < LVL_MAX; i++)
    {
        snprintf(buf, sizeof(buf), "%s/%s.PHD", data, levelNames[i]);
        FileStream f(buf, false);
        if (!f.isValid()) {
            printf("skip %s (not found)\n", buf);
            continue;
        }
        pc[i] = new TR1_PC(f, LevelID(i));
        pc[i]->generateLODs();
        pc[i]->cutData();
        printf("loaded %s\n", buf);
    }

    if (fingerprint)
    {
        for (int32 i = 0; i < LVL_MAX; i++) {
            if (!pc[i]) continue;
            /* Counts a re-release cannot change without changing the level.
               Read straight off the parsed structure, so the module computes
               them the same way from the bytes it is handed. */
            printf("    { LVL_%-12s %5d, %5d, %5d, %5d },  // %s\n",
                   (std::string(levelNames[i]) + ",").c_str(),
                   pc[i]->roomsCount, pc[i]->objectTexturesCount,
                   pc[i]->itemsCount, pc[i]->meshOffsetsCount,
                   levelNames[i]);
        }
        return 0;
    }

    out_GBA* out = new out_GBA();
    out->roomVerticesCount = 0;
    out->roomVertices = new out_GBA::RoomVertex[MAX_ROOM_VERTICES];

    for (int32 i = 0; i < LVL_MAX; i++)
    {
        if (!pc[i]) continue;
        snprintf(buf, sizeof(buf), "%s/%s.PKD", argv[2], levelNames[i]);
        FileStream f(buf, true);
        if (!f.isValid()) {
            printf("can't save \"%s\"\n", buf);
            continue;
        }
        out->convertGBA(f, pc[i]);
        printf("wrote %s\n", buf);
    }

    delete[] out->roomVertices;
    delete out;

    for (int32 i = 0; i < LVL_MAX; i++) delete pc[i];
    return 0;
}
