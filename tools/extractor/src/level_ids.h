/*
 * Which level is this?
 *
 * A .PHD carries no name. The packer needs one anyway: TR1_PC::cutData()
 * applies hand-listed fixups to GYM, LEVEL1 and LEVEL2 -- hidden training
 * rooms, tutorial sounds, and a list of object textures whose transparency
 * bit is cleared -- and getting the identification wrong means a .PKD that
 * differs from the reference in exactly those bytes. Every other level is
 * unaffected, so an unrecognised file is packed correctly as long as it is
 * not one of those three.
 *
 * Identification is by content, as the ABI requires (spec/04-processor.md:
 * "input_add takes no name and no role"), and it is done twice:
 *
 *   1. SHA-256 of the whole file. Exact, and the hashes below are from a
 *      retail TR1 PC CD -- the same DATA files the GOG and Steam releases
 *      ship, since the levels were never re-mastered for the PC re-releases.
 *
 *   2. Failing that, four counts read out of the parsed level: rooms,
 *      object textures, items, mesh offsets. These are unique across all 21
 *      levels and they cannot change without the level itself changing, so
 *      they still identify a file that some installer or patch has touched
 *      in a way a hash cannot survive.
 *
 * A file that matches neither is still converted. The module warns and skips
 * the fixups, because refusing is the wrong answer for an input the host has
 * already decided to allow -- see "A module hashes to resolve roles, never to
 * refuse a file" in the spec.
 *
 * Regenerate the counts with:  build/native/oracle --fingerprint <DATA-dir>
 */
#ifndef H_LEVEL_IDS
#define H_LEVEL_IDS

struct LevelIdent {
    const char* sha256;
    int rooms;
    int objectTextures;
    int items;
    int meshOffsets;
    LevelID id;
    const char* name;
};

static const LevelIdent kLevelIdents[] = {
    { "f2f8d28e2803d69ce096df0d34e9fbe70406e93cd838e045e491805bfe52ad39",   2,   20,   1,  13, LVL_TR1_TITLE, "TITLE" },
    { "52ff324648a3af7f6f48729108406d2811c632a3257ccf71f1196c05d98932fe",  19,  926,   1, 172, LVL_TR1_GYM,   "GYM" },
    { "75306547c82d8e142f6dc80d85703d1fd5a5871071e4d04233660347865d26c7",  38,  898,  60, 218, LVL_TR1_1,     "LEVEL1" },
    { "48375822b93f01606d1765c5ff1057216e309a88a802b05bf0175d91a4188117",  94,  951,  95, 228, LVL_TR1_2,     "LEVEL2" },
    { "747010f36abbf9afc2b21e774daf48551b01c35308f287c832f3c89230866e0c",  91, 1027,  64, 263, LVL_TR1_3A,    "LEVEL3A" },
    { "d2c51c1752fb1f58cab9a1510359b885ad7239628e6f96dbfe9ad6a14124ab90",  54, 1086,  77, 265, LVL_TR1_3B,    "LEVEL3B" },
    { "4d1f7e1a1b0577fbbcec711d4bd4dfce358178459382ba023278eb17f47816de",  15,  324,   3,  31, LVL_TR1_CUT_1, "CUT1" },
    { "a1a14ef34e8d1255563a73a1f66ba946b61e93c4032207064a4d2e60dade2ff1",  63, 1074, 107, 315, LVL_TR1_4,     "LEVEL4" },
    { "c0ee4b0f0b430e7b480c469d2a3b234be9ae418c19e5185627ea6c77103189ba",  89,  933,  86, 324, LVL_TR1_5,     "LEVEL5" },
    { "f7fa9eac88678dd48b37651c02ef1b14d8bbea0d3bbcf9f39c515ff82b6157f7",  79,  846, 136, 304, LVL_TR1_6,     "LEVEL6" },
    { "c21b9eb4c2552fe36438a5279acb75636dedfd01c93819a3629364b416882caa", 139, 1020, 112, 337, LVL_TR1_7A,    "LEVEL7A" },
    { "34f4075b9f94c4cff42e9715554bcbf2ded734a05d278b0f978ade95cc10f12b", 114, 1278,  88, 383, LVL_TR1_7B,    "LEVEL7B" },
    { "3615d71c335cc3dd5e0253a4e80af71f2fbf5ec7713b57bfcfa8c45a4c32c95d",   4,  162,   1,  33, LVL_TR1_CUT_2, "CUT2" },
    { "3288d49ccdf11dfacc63f1f7828e829bd7c2f04ca28a3facfea7e77dbbd06a44",  71,  938,  93, 251, LVL_TR1_8A,    "LEVEL8A" },
    { "0316e36efe46ab750b65310c17390d6bf167169b3e8c90ecaa3708653bfb299d",  84,  862, 103, 255, LVL_TR1_8B,    "LEVEL8B" },
    { "e377894f4780f63cb377e062b492dadcba5bbf347fb356435ca9d030d93e008e",  64,  934,  74, 243, LVL_TR1_8C,    "LEVEL8C" },
    { "ca511769786af1b09d6eedf07e81350242374957d50a96d8ca6f3b705eb05492", 108,  908, 101, 230, LVL_TR1_10A,   "LEVEL10A" },
    { "6064ea0581c5f477f4562a3a54f2be618fb0c0786814f316634e6ffc7cae6ff8",  26,  193,   4,  44, LVL_TR1_CUT_3, "CUT3" },
    { "d4f09f7b371330979ab94b5eb536964fb27f8ec57862f7bf528d38043d4fe9ce", 101, 1031, 192, 291, LVL_TR1_10B,   "LEVEL10B" },
    { "1ca3faae25dc9cfcdaa56555d56d416f315cf4afcfe74926b330c66611d0e083",  18,  386,   6,  90, LVL_TR1_CUT_4, "CUT4" },
    { "29c63dc57455b02b57f6dc320ccc650bdd3cf9cb843f159205794f3560c5ed05",  67, 1081, 129, 312, LVL_TR1_10C,   "LEVEL10C" },
};

static const int kLevelIdentCount = (int)(sizeof(kLevelIdents) / sizeof(kLevelIdents[0]));

#endif
