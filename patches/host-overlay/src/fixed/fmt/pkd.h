#ifndef H_PKD
#define H_PKD

#include "common.h"
#include "stream.h"

#if defined(HOST_BUILD)
/* On-disk PKD was packed for 32-bit (GBA / Cortex-M). Native Level/RoomInfo
 * use 64-bit pointers, so we must relocate field-by-field. */

struct RoomDataDisk {
    uint32 quads;
    uint32 triangles;
    uint32 vertices;
    uint32 sprites;
    uint32 portals;
    uint32 sectors;
    uint32 lights;
    uint32 meshes;
};

struct RoomInfoDisk {
    int16 x;
    int16 z;
    int16 yBottom;
    int16 yTop;
    uint16 quadsCount;
    uint16 trianglesCount;
    uint16 verticesCount;
    uint16 spritesCount;
    uint8 portalsCount;
    uint8 lightsCount;
    uint8 meshesCount;
    uint8 ambient;
    uint8 xSectors;
    uint8 zSectors;
    uint8 alternateRoom;
    uint8 flags;
    RoomDataDisk data;
};

struct TextureDisk {
    uint32 tile;
    uint32 uv01;
    uint32 uv23;
};

struct SpriteDisk {
    uint32 tile;
    uint32 uwvh;
    int16 l, t, r, b;
};

struct LevelDisk {
    uint32 version;
    uint16 tilesCount;
    uint16 roomsCount;
    uint16 modelsCount;
    uint16 meshesCount;
    uint16 staticMeshesCount;
    uint16 spriteSequencesCount;
    uint16 soundSourcesCount;
    uint16 boxesCount;
    uint16 texturesCount;
    uint16 spritesCount;
    uint16 itemsCount;
    uint16 camerasCount;
    uint16 cameraFramesCount;
    uint16 soundOffsetsCount;
    uint32 palette;
    uint32 lightmap;
    uint32 tiles;
    uint32 roomsInfo;
    uint32 floors;
    uint32 meshes;
    uint32 meshOffsets;
    uint32 anims;
    uint32 animStates;
    uint32 animRanges;
    uint32 animCommands;
    uint32 nodes;
    uint32 animFrames;
    uint32 models;
    uint32 staticMeshes;
    uint32 textures;
    uint32 sprites;
    uint32 spriteSequences;
    uint32 cameras;
    uint32 soundSources;
    uint32 boxes;
    uint32 overlaps;
    uint32 zones[2][ZONE_MAX];
    uint32 animTexData;
    uint32 itemsInfo;
    uint32 cameraFrames;
    uint32 soundMap;
    uint32 soundsInfo;
    uint32 soundData;
    uint32 soundOffsets;
};

static RoomInfo s_host_room_infos[MAX_ROOMS];

template <typename T>
static inline const T *pkd_ptr(const uint8 *base, uint32 off)
{
    return (const T *)(base + off);
}
#endif /* HOST_BUILD */

bool read_PKD(DataStream &f)
{
    const uint8* data = f.getPtr();

#if defined(HOST_BUILD)
    LevelDisk disk;
    memcpy(&disk, data, sizeof(disk));

    level.version = disk.version;
    level.tilesCount = disk.tilesCount;
    level.roomsCount = disk.roomsCount;
    level.modelsCount = disk.modelsCount;
    level.meshesCount = disk.meshesCount;
    level.staticMeshesCount = disk.staticMeshesCount;
    level.spriteSequencesCount = disk.spriteSequencesCount;
    level.soundSourcesCount = disk.soundSourcesCount;
    level.boxesCount = disk.boxesCount;
    level.texturesCount = disk.texturesCount;
    level.spritesCount = disk.spritesCount;
    level.itemsCount = disk.itemsCount;
    level.camerasCount = disk.camerasCount;
    level.cameraFramesCount = disk.cameraFramesCount;
    level.soundOffsetsCount = disk.soundOffsetsCount;

    level.palette = pkd_ptr<uint16>(data, disk.palette);
    level.lightmap = pkd_ptr<uint8>(data, disk.lightmap);
    level.tiles = pkd_ptr<uint8>(data, disk.tiles);
    level.floors = pkd_ptr<FloorData>(data, disk.floors);
    level.meshes = (const Mesh **)pkd_ptr<uint8>(data, disk.meshes);
    level.meshOffsets = pkd_ptr<int32>(data, disk.meshOffsets);
    level.anims = pkd_ptr<Anim>(data, disk.anims);
    level.animStates = pkd_ptr<AnimState>(data, disk.animStates);
    level.animRanges = pkd_ptr<AnimRange>(data, disk.animRanges);
    level.animCommands = pkd_ptr<int16>(data, disk.animCommands);
    level.nodes = pkd_ptr<ModelNode>(data, disk.nodes);
    level.animFrames = pkd_ptr<uint16>(data, disk.animFrames);
    level.models = pkd_ptr<Model>(data, disk.models);
    level.staticMeshes = pkd_ptr<StaticMesh>(data, disk.staticMeshes);
    level.spriteSequences = pkd_ptr<SpriteSeq>(data, disk.spriteSequences);
    level.soundSources = pkd_ptr<SoundSource>(data, disk.soundSources);
    level.overlaps = pkd_ptr<uint16>(data, disk.overlaps);
    for (int32 a = 0; a < 2; a++)
        for (int32 z = 0; z < ZONE_MAX; z++)
            level.zones[a][z] = pkd_ptr<uint16>(data, disk.zones[a][z]);
    level.animTexData = pkd_ptr<uint16>(data, disk.animTexData);
    level.itemsInfo = pkd_ptr<ItemObjInfo>(data, disk.itemsInfo);
    level.cameraFrames = pkd_ptr<CameraFrame>(data, disk.cameraFrames);
    level.soundMap = pkd_ptr<uint16>(data, disk.soundMap);
    level.soundsInfo = pkd_ptr<SoundInfo>(data, disk.soundsInfo);
    level.soundData = pkd_ptr<uint8>(data, disk.soundData);
    level.soundOffsets = pkd_ptr<int32>(data, disk.soundOffsets);

    /* Expand RoomInfo (embedded RoomData pointers are 32-bit on disk). */
    if (level.roomsCount > MAX_ROOMS)
        return false;
    {
        const RoomInfoDisk *src = pkd_ptr<RoomInfoDisk>(data, disk.roomsInfo);
        for (int32 i = 0; i < level.roomsCount; i++) {
            RoomInfo *dst = &s_host_room_infos[i];
            dst->x = src[i].x;
            dst->z = src[i].z;
            dst->yBottom = src[i].yBottom;
            dst->yTop = src[i].yTop;
            dst->quadsCount = src[i].quadsCount;
            dst->trianglesCount = src[i].trianglesCount;
            dst->verticesCount = src[i].verticesCount;
            dst->spritesCount = src[i].spritesCount;
            dst->portalsCount = src[i].portalsCount;
            dst->lightsCount = src[i].lightsCount;
            dst->meshesCount = src[i].meshesCount;
            dst->ambient = src[i].ambient;
            dst->xSectors = src[i].xSectors;
            dst->zSectors = src[i].zSectors;
            dst->alternateRoom = src[i].alternateRoom;
            dst->flags = src[i].flags;
            dst->data.quads = pkd_ptr<RoomQuad>(data, src[i].data.quads);
            dst->data.triangles = pkd_ptr<RoomTriangle>(data, src[i].data.triangles);
            dst->data.vertices = pkd_ptr<RoomVertex>(data, src[i].data.vertices);
            dst->data.sprites = pkd_ptr<RoomSprite>(data, src[i].data.sprites);
            dst->data.portals = pkd_ptr<Portal>(data, src[i].data.portals);
            dst->data.sectors = pkd_ptr<Sector>(data, src[i].data.sectors);
            dst->data.lights = pkd_ptr<Light>(data, src[i].data.lights);
            dst->data.meshes = pkd_ptr<RoomMesh>(data, src[i].data.meshes);
        }
        level.roomsInfo = s_host_room_infos;
    }

    for (int32 i = 0; i < level.roomsCount; i++) {
        Room *room = rooms + i;
        room->info = level.roomsInfo + i;
        room->data = room->info->data;
        room->sectors = room->data.sectors;
        room->firstItem = NULL;
    }

    /* textures / sprites / boxes / cameras — expand tile offsets to native ptrs */
    {
        const TextureDisk *td = pkd_ptr<TextureDisk>(data, disk.textures);
        if (level.texturesCount > MAX_TEXTURES)
            return false;
        for (int32 i = 0; i < level.texturesCount; i++) {
            textures[i].tile = (uintptr_t)level.tiles + td[i].tile;
            textures[i].uv01 = td[i].uv01;
            textures[i].uv23 = td[i].uv23;
        }
        level.textures = textures;

        const SpriteDisk *sd = pkd_ptr<SpriteDisk>(data, disk.sprites);
        if (level.spritesCount > MAX_SPRITES)
            return false;
        for (int32 i = 0; i < level.spritesCount; i++) {
            sprites[i].tile = (uintptr_t)level.tiles + sd[i].tile;
            sprites[i].uwvh = sd[i].uwvh;
            sprites[i].l = sd[i].l;
            sprites[i].t = sd[i].t;
            sprites[i].r = sd[i].r;
            sprites[i].b = sd[i].b;
        }
        level.sprites = sprites;

        if (level.boxesCount > MAX_BOXES)
            return false;
        memcpy(boxes, pkd_ptr<Box>(data, disk.boxes), level.boxesCount * sizeof(Box));
        level.boxes = boxes;

        if (level.camerasCount > MAX_CAMERAS)
            return false;
        if (level.camerasCount)
            memcpy(cameras, pkd_ptr<FixedCamera>(data, disk.cameras),
                   level.camerasCount * sizeof(FixedCamera));
        level.cameras = cameras;
    }

#else /* !HOST_BUILD — original 32-bit path */
    memcpy(&level, data, sizeof(level));

    { // fix level data offsets
        uint32* ptr = (uint32*)&level.palette;
        while (ptr <= (uint32*)&level.soundOffsets)
        {
            *ptr++ += (uint32)data;
        }
    }

    { // prepare rooms
        for (int32 i = 0; i < level.roomsCount; i++)
        {
            Room* room = rooms + i;
            room->info = level.roomsInfo + i;
            room->data = room->info->data;

            for (uint32 j = 0; j < sizeof(room->data) / 4; j++)
            {
                int32* x = (int32*)&room->data + j;
                *x += (int32)data;
            }

            room->sectors = room->data.sectors;
            room->firstItem = NULL;
        }
    }
#endif

#ifndef MODEHW
    // initialize global pointers
    gBrightness = -128;
    palSet(level.palette, gSettings.video_gamma << 4, gBrightness);
    memcpy(gLightmap, level.lightmap, sizeof(gLightmap));
#endif

#if !defined(HOST_BUILD)
#ifdef ROM_READ
    // prepare textures (required by anim tex logic)
    memcpy(textures, level.textures, level.texturesCount * sizeof(Texture));
    level.textures = textures;

    // prepare sprites (TODO preprocess tile address in packer)
    memcpy(sprites, level.sprites, level.spritesCount * sizeof(Sprite));
    level.sprites = sprites;

    // prepare boxes
    memcpy(boxes, level.boxes, level.boxesCount * sizeof(Box));
    level.boxes = boxes;

    // prepare fixed cameras
    memcpy(cameras, level.cameras, level.camerasCount * sizeof(FixedCamera));
    level.cameras = cameras;
#endif

#ifdef __3DO__
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        Texture* tex = level.textures + i;
        tex->data += intptr_t(RAM_TEX);
    }
#else
    // TODO preprocess in packer
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        level.textures[i].tile += (uint32)level.tiles;
    }

    for (int32 i = 0; i < level.spritesCount; i++)
    {
        level.sprites[i].tile += (uint32)level.tiles;
    }
#endif
#endif /* !HOST_BUILD */

    return true;
}

#endif
