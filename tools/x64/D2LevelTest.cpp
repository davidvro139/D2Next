// Generates levels through D2Common's dungeon code and writes what came out, so the original 32-bit D2Common
// (with the original Fog, D2Lang and D2CMP) and the x64 build can be compared line by line.
//
// The same source is built as x86 and as x64. For each act and seed it creates the act, generates every level of
// the act, streams in the rooms by scanning the level, and dumps for each room its coordinates, its tiles
// (position, kind and flags, read back through D2CMP) and the collision mask of every subtile.
//
// D2Common's structures differ between the two builds (pointer size, packed layout). The few fields read directly
// are given by their offsets, computed from the pointer size and checked at run time; everything else goes
// through exported functions.
//
// usage: D2LevelTest <dll dir> <game dir> <output file> [seed ...]
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "RefStorm.h"

namespace fs = std::filesystem;

namespace
{
constexpr size_t P = sizeof(void*);

HMODULE gCommon, gCmp, gFog;
FILE* out;

template <typename T>
T fn(HMODULE m, int ordinal)
{
    auto p = reinterpret_cast<T>(GetProcAddress(m, MAKEINTRESOURCEA(ordinal)));
    if (!p)
    {
        std::printf("missing ordinal %d\n", ordinal);
        std::exit(3);
    }
    return p;
}

uint64_t fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull)
{
    for (size_t i = 0; i < n; ++i)
        h = (h ^ static_cast<const uint8_t*>(data)[i]) * 1099511628211ull;
    return h;
}

template <typename T>
T at(const void* base, size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(T));
    return value;
}

using AllocAct_t = void*(__stdcall*)(uint8_t, uint32_t, BOOL, void*, uint8_t, void*, int, void*, void*);
using FreeAct_t = void(__stdcall*)(void*);
using GetDrlg_t = void*(__stdcall*)(void*);
using GetLevel_t = void*(__stdcall*)(void*, int);
using InitLevel_t = void(__stdcall*)(void*);
using Stream_t = void*(__stdcall*)(void*, int, int);
using RoomCoords_t = void(__stdcall*)(void*, int*);
using RoomLevel_t = int(__stdcall*)(void*);
using Tiles_t = uint8_t*(__stdcall*)(void*, int*);
using Adjacent_t = void(__stdcall*)(void*, void***, int*);
using Collision_t = uint16_t(__stdcall*)(void*, int, int, unsigned, unsigned, uint16_t);
using TownId_t = int(__stdcall*)(uint8_t);
using ActOfLevel_t = uint8_t(__stdcall*)(int);
using TileGetter_t = int(__stdcall*)(void*);
using FlagArray_t = uint8_t*(__stdcall*)(void*);

AllocAct_t allocAct;
FreeAct_t freeAct;
GetDrlg_t getDrlg;
GetLevel_t getLevel;
InitLevel_t initLevel;
Stream_t streamRoom;
RoomCoords_t roomCoords;
RoomLevel_t roomLevel;
Tiles_t floorTiles, wallTiles, roofTiles;
Adjacent_t adjacentRooms;
Collision_t collision;
TownId_t townId;
ActOfLevel_t actOfLevel;
TileGetter_t tileType, tileStyle, tileFlags, tileRarity, tileSequence;
FlagArray_t tileFlagArray;

// D2DrlgLevelStrc (packed): pDrlg, nLevelId, nLevelType, nDrlgType, dwFlags, pSeed (8), dwInitSeed, coords[4], pFirstRoomEx, nRooms
constexpr size_t kLevelId = P, kLevelType = P + 4, kLevelCoords = P + 0x1C, kLevelRooms = 2 * P + 0x2C;
// D2DrlgTileDataStrc (packed): nWidth, nHeight, nPosX, nPosY, pCoordList, dwFlags, pTile, nTileType, unk, unk, rgba, unk
constexpr size_t kTileWidth = 0, kTileFlagsAt = 0x10 + P, kTilePtr = 0x14 + P, kTileTypeAt = 0x14 + 2 * P;
constexpr size_t kTileSize = 0x30 + 3 * (P - 4);

int gRooms, gTiles;
bool gVerbose;
int gMapLevel = -1; // --map N: also print the walkable subtiles of level N as text

void dumpTiles(const char* kind, void* room, Tiles_t get)
{
    int count = 0;
    const uint8_t* tiles = get(room, &count);
    uint64_t hash = 1469598103934665603ull;
    if (gVerbose)
        std::fprintf(out, "  %s n=%d\n", kind, count);
    for (int i = 0; i < count; ++i)
    {
        const uint8_t* t = tiles + size_t(i) * kTileSize;
        void* pTile = at<void*>(t, kTilePtr);
        char line[256];
        int n = std::snprintf(line, sizeof(line), "%d %d %d %d %08X %d", at<int32_t>(t, kTileWidth), at<int32_t>(t, 4), at<int32_t>(t, 8), at<int32_t>(t, 12),
                              at<uint32_t>(t, kTileFlagsAt), at<int32_t>(t, kTileTypeAt));
        if (pTile)
            n += std::snprintf(line + n, sizeof(line) - size_t(n), " t=%d s=%d q=%d f=%d r=%d fa=%016llX", tileType(pTile), tileStyle(pTile), tileSequence(pTile),
                               tileFlags(pTile), tileRarity(pTile), (unsigned long long)fnv(tileFlagArray(pTile), 25));
        else
            n += std::snprintf(line + n, sizeof(line) - size_t(n), " null");
        hash = fnv(line, size_t(n), hash);
        if (gVerbose)
            std::fprintf(out, "   %s %s\n", kind, line);
        ++gTiles;
    }
    if (!gVerbose)
        std::fprintf(out, "  %s n=%d hash=%016llX\n", kind, count, (unsigned long long)hash);
}
void dumpRoom(int index, void* room)
{
    int coords[8] = {};
    roomCoords(room, coords);
    std::fprintf(out, " room %d level=%d coords=%d,%d,%d,%d tile=%d,%d,%d,%d\n", index, roomLevel(room), coords[0], coords[1], coords[2], coords[3], coords[4], coords[5],
                 coords[6], coords[7]);

    // Collision of every subtile of the room.
    uint64_t hash = 1469598103934665603ull;
    int blocked = 0;
    for (int y = coords[1]; y < coords[1] + coords[3]; ++y)
    {
        for (int x = coords[0]; x < coords[0] + coords[2]; ++x)
        {
            const uint16_t mask = collision(room, x, y, 1, 1, 0xFFFF);
            hash = fnv(&mask, 2, hash);
            blocked += mask != 0;
        }
    }
    std::fprintf(out, "  collision blocked=%d hash=%016llX\n", blocked, (unsigned long long)hash);

    dumpTiles("floor", room, floorTiles);
    dumpTiles("wall", room, wallTiles);
    dumpTiles("roof", room, roofTiles);

    // The room's list of coordinate boxes (what monster population walks over): boxes, node flag and index of every entry.
    {
        void* node = fn<void*(__stdcall*)(void*)>(gCommon, 10097)(room);
        uint64_t listHash = 1469598103934665603ull;
        int nodes = 0;
        for (; node && nodes < 1000; node = at<void*>(node, 0x2C), ++nodes)
        {
            char line[200];
            const int n = std::snprintf(line, sizeof(line), "%d %d %d %d %d %d %d %d node=%d index=%d", at<int32_t>(node, 0), at<int32_t>(node, 4), at<int32_t>(node, 8), at<int32_t>(node, 12),
                                        at<int32_t>(node, 16), at<int32_t>(node, 20), at<int32_t>(node, 24), at<int32_t>(node, 28), at<int32_t>(node, 0x20), at<int32_t>(node, 0x28));
            listHash = fnv(line, size_t(n), listHash);
            if (gVerbose)
                std::fprintf(out, "   coord %s\n", line);
        }
        std::fprintf(out, "  coordlist n=%d hash=%016llX\n", nodes, (unsigned long long)listHash);
    }

    void** adjacent = nullptr;
    int nAdjacent = 0;
    adjacentRooms(room, &adjacent, &nAdjacent);
    std::fprintf(out, "  adjacent=%d\n", nAdjacent);
    ++gRooms;
}

// Generates a level, then finds its rooms by asking for the room at every few tiles.
void dumpLevel(void* act, void* drlg, int levelId)
{
    void* level = getLevel(drlg, levelId);
    if (!level)
    {
        std::fprintf(out, "level %d: none\n", levelId);
        return;
    }
    if (at<int32_t>(level, kLevelId) != levelId)
    {
        std::fprintf(out, "level %d: LAYOUT MISMATCH (id field reads %d)\n", levelId, at<int32_t>(level, kLevelId));
        return;
    }
    const int x0 = at<int32_t>(level, kLevelCoords), y0 = at<int32_t>(level, kLevelCoords + 4);
    const int w = at<int32_t>(level, kLevelCoords + 8), h = at<int32_t>(level, kLevelCoords + 12);
    std::fprintf(out, "level %d type=%d pos=%d,%d size=%dx%d rooms=%d\n", levelId, at<int32_t>(level, kLevelType), x0, y0, w, h, at<int32_t>(level, kLevelRooms));

    std::vector<void*> rooms;
    std::set<void*> seen;
    for (int y = y0; y < y0 + h; y += 2)
    {
        for (int x = x0; x < x0 + w; x += 2)
        {
            void* room = streamRoom(act, x, y);
            if (room && seen.insert(room).second)
                rooms.push_back(room);
        }
    }
    std::fprintf(out, "level %d streamed rooms=%zu\n", levelId, rooms.size());
    if (levelId == gMapLevel && !rooms.empty())
    {
        // Every subtile of every room, by the room's own collision: '#' blocks walking, '.' is free, ' ' has no room.
        const int sx0 = x0 * 5, sy0 = y0 * 5, sw = w * 5, sh = h * 5;
        std::vector<std::string> grid(size_t(sh), std::string(size_t(sw), ' '));
        for (void* room : rooms)
        {
            int c[8] = {};
            roomCoords(room, c);
            for (int y = c[1]; y < c[1] + c[3]; ++y)
                for (int x = c[0]; x < c[0] + c[2]; ++x)
                    if (x >= sx0 && x < sx0 + sw && y >= sy0 && y < sy0 + sh)
                        grid[size_t(y - sy0)][size_t(x - sx0)] = collision(room, x, y, 1, 1, 0x3C01) ? '#' : '.';
        }
        std::fprintf(out, "map of level %d (subtiles %d..%d x %d..%d, every 2nd: # blocked . free)\n", levelId, sx0, sx0 + sw, sy0, sy0 + sh);
        for (int y = 0; y < sh; y += 2)
        {
            std::string row;
            for (int x = 0; x < sw; x += 2)
                row += grid[size_t(y)][size_t(x)];
            std::fprintf(out, "%5d %s\n", sy0 + y, row.c_str());
        }
    }    for (size_t i = 0; i < rooms.size(); ++i)
        dumpRoom(int(i), rooms[i]);
}

int filter(unsigned code, EXCEPTION_POINTERS* info)
{
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(info->ExceptionRecord->ExceptionAddress), &m);
    char name[MAX_PATH] = "?";
    if (m)
        GetModuleFileNameA(m, name, MAX_PATH);
    std::printf("CRASH: exception %08X at %p in %s (offset 0x%llX)\n", code, info->ExceptionRecord->ExceptionAddress, name,
                (unsigned long long)((uintptr_t)info->ExceptionRecord->ExceptionAddress - (uintptr_t)m));
    std::fprintf(out, "CRASH %08X\n", code);
    return EXCEPTION_EXECUTE_HANDLER;
}

int guardedAct(int act, uint32_t seed)
{
    __try
    {
        const int town = townId(uint8_t(act));
        std::fprintf(out, "act %d seed %08X town=%d\n", act, seed, town);
        void* pAct = allocAct(uint8_t(act), seed, FALSE, nullptr, 0, nullptr, town, nullptr, nullptr);
        if (!pAct)
        {
            std::fprintf(out, "act: null\n");
            return 0;
        }
        void* drlg = getDrlg(pAct);
        for (int level = 1; level < 133; ++level) // 133 and up are not levels the game uses
        {
            if (actOfLevel(level) == act)
                dumpLevel(pAct, drlg, level);
        }
        freeAct(pAct);
        return 0;
    }
    __except (filter(GetExceptionCode(), GetExceptionInformation()))
    {
        return 1;
    }
}
} // namespace
static int guardedLoad()
{
    __try
    {
        fn<void(__stdcall*)(void*, int, int)>(gCommon, 10576)(nullptr, 1, 1);
        return 0;
    }
    __except (filter(GetExceptionCode(), GetExceptionInformation()))
    {
        return 1;
    }
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: D2LevelTest <dll dir> <game dir> <out file> [seed ...]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const fs::path dllDir = fs::absolute(argv[1]), gameDir = argv[2];
    std::vector<uint32_t> seeds;
    for (int i = 4; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--verbose")
            gVerbose = true;
        else if (std::string(argv[i]) == "--map" && i + 1 < argc)
            gMapLevel = std::atoi(argv[++i]);
        else
            seeds.push_back(uint32_t(std::strtoul(argv[i], nullptr, 0)));
    }
    if (seeds.empty())
        seeds = {0x12345678};
    SetDllDirectoryW(gameDir.c_str());
    SetCurrentDirectoryW(dllDir.c_str());

    auto load = [&](const char* name) {
        HMODULE m = LoadLibraryExW((dllDir / (std::string(name) + ".dll")).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m)
        {
            std::printf("cannot load %s (error %lu)\n", name, GetLastError());
            std::exit(1);
        }
        return m;
    };
    HMODULE storm = load("Storm");
    gFog = load("Fog");
    HMODULE lang = load("D2Lang");
    gCmp = load("D2CMP");
    gCommon = load("D2Common");

    struct Entry { fs::path path; int priority; };
    const fs::path core = gameDir / "D2SE/CORES/1.10f/Patch_D2.mpq";
    const Entry entries[] = {{fs::exists(core) ? core : gameDir / "Patch_D2.mpq", 5000}, {gameDir / "d2exp.mpq", 4000},
                             {gameDir / "d2data.mpq", 3000}, {gameDir / "d2char.mpq", 2000}, {gameDir / "d2sfx.mpq", 1000}};
#ifdef _WIN64
    auto openArchive = fn<BOOL(__stdcall*)(const char*, int, int, void**)>(storm, 266);
    for (const auto& e : entries)
    {
        void* h = nullptr;
        if (fs::exists(e.path) && !openArchive(e.path.string().c_str(), e.priority, 2, &h))
        {
            std::printf("cannot open %s\n", e.path.string().c_str());
            return 1;
        }
    }
#else
    (void)storm;
    for (const auto& e : entries)
    {
        if (!fs::exists(e.path))
            continue;
        HANDLE h = nullptr;
        if (!SFileOpenArchive(e.path.string().c_str(), 0, MPQ_OPEN_READ_ONLY, &h))
        {
            std::printf("cannot open %s\n", e.path.string().c_str());
            return 1;
        }
        refstorm::archives.push_back(h);
    }
    for (HMODULE m : {gFog, lang, gCmp, gCommon})
        refstorm::patchImports(m);
    fn<BOOL(__fastcall*)(int, int)>(gFog, 10101)(0, 0);
    if (!fn<BOOL(__fastcall*)(void*, const char*, int)>(lang, 10000)(nullptr, "ENG", 1))
    {
        std::printf("language initialization failed\n");
        return 3;
    }
#endif
    (void)lang;

    allocAct = fn<AllocAct_t>(gCommon, 10038);
    freeAct = fn<FreeAct_t>(gCommon, 10039);
    getDrlg = fn<GetDrlg_t>(gCommon, 10088);
    getLevel = fn<GetLevel_t>(gCommon, 10005);
    initLevel = fn<InitLevel_t>(gCommon, 10006);
    streamRoom = fn<Stream_t>(gCommon, 10065);
    roomCoords = fn<RoomCoords_t>(gCommon, 10034);
    roomLevel = fn<RoomLevel_t>(gCommon, 10057);
    floorTiles = fn<Tiles_t>(gCommon, 10030);
    wallTiles = fn<Tiles_t>(gCommon, 10031);
    roofTiles = fn<Tiles_t>(gCommon, 10032);
    adjacentRooms = fn<Adjacent_t>(gCommon, 10035);
    collision = fn<Collision_t>(gCommon, 10120);
    townId = fn<TownId_t>(gCommon, 10085);
    actOfLevel = fn<ActOfLevel_t>(gCommon, 10001);
    tileType = fn<TileGetter_t>(gCmp, 10077);
    tileStyle = fn<TileGetter_t>(gCmp, 10078);
    tileFlags = fn<TileGetter_t>(gCmp, 10079);
    tileRarity = fn<TileGetter_t>(gCmp, 10081);
    tileSequence = fn<TileGetter_t>(gCmp, 10082);
    tileFlagArray = fn<FlagArray_t>(gCmp, 10085);

    out = std::fopen(argv[3], "w");
    if (!out)
    {
        std::printf("cannot open output file\n");
        return 2;
    }

    // Loads every table and every preset/tile library, as a running game does.
    if (guardedLoad())
    {
        std::printf("DATATBLS_LoadAllTxts crashed\n");
        return 4;
    }
    std::fprintf(out, "expansion=%d\n", fn<int(__fastcall*)()>(gFog, 10227)());

    int failures = 0;
    for (const uint32_t seed : seeds)
    {
        for (int act = 0; act < 5; ++act)
        {
            failures += guardedAct(act, seed);
            std::fflush(out);
        }
    }
    std::fclose(out);
    std::printf("%d rooms, %d tiles, %d crashes -> %s\n", gRooms, gTiles, failures, argv[3]);
    return failures ? 5 : 0;
}
