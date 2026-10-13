// Loads the tile libraries (.dt1) of every level type through D2CMP and looks tiles up the way the level
// generator does, so the original 32-bit D2CMP.dll and the native x64 one can be compared line by line.
//
// The same source is built as x86 and as x64. The queries come from the DT1 files themselves (read here, not by
// D2CMP), so the test asks for exactly the (type, style, sequence) keys that exist, plus keys that do not.
//
// usage: D2CmpTileTest <dll dir> <game dir> <output file>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "RefStorm.h"

namespace fs = std::filesystem;

namespace
{
HMODULE gStorm;

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

// A whole file from the mounted archives, without going through D2CMP.
std::vector<uint8_t> readFile(const std::string& path)
{
    std::vector<uint8_t> data;
    HANDLE f = nullptr;
    DWORD size = 0, got = 0;
#ifdef _WIN64
    if (!fn<BOOL(__stdcall*)(const char*, HANDLE*)>(gStorm, 267)(path.c_str(), &f))
        return data;
    size = fn<DWORD(__stdcall*)(HANDLE, DWORD*)>(gStorm, 265)(f, nullptr);
    data.resize(size);
    fn<BOOL(__stdcall*)(HANDLE, void*, DWORD, DWORD*, void*)>(gStorm, 269)(f, data.data(), size, &got, nullptr);
    fn<BOOL(__stdcall*)(HANDLE)>(gStorm, 253)(f);
#else
    if (!refstorm::openFile(path.c_str(), &f))
        return data;
    size = refstorm::fileSize(f, nullptr);
    data.resize(size);
    refstorm::readFile(f, data.data(), size, &got, nullptr);
    refstorm::closeFile(f);
#endif
    if (got != size)
        data.clear();
    return data;
}

uint64_t fnv(const uint8_t* p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

using LoadSlot_t = void(__stdcall*)(void**, const char*);
using GetTiles_t = int(__stdcall*)(void**, int, int, int, void**, int);
using Getter_t = int(__stdcall*)(void*);
using FlagArray_t = uint8_t*(__stdcall*)(void*);
using Mix_t = uint8_t*(__stdcall*)(uint8_t, int);

LoadSlot_t loadSlot;
GetTiles_t getTiles;
Getter_t getType, getStyle, getFlags, getRarity, getSequence;
FlagArray_t getFlagArray;
Mix_t mixPalette;
FILE* out;
int gLines;

// Everything observable about a tile, without the parts that differ by design (pointers).
void describe(void* tile)
{
    const uint8_t* flags = getFlagArray(tile);
    std::fprintf(out, " {t=%d s=%d q=%d f=%d r=%d fa=%016llX hdr=%016llX}", getType(tile), getStyle(tile), getSequence(tile), getFlags(tile),
                 getRarity(tile), (unsigned long long)fnv(flags, 25), (unsigned long long)fnv(static_cast<const uint8_t*>(tile), 0x54));
}

void query(int array, void** hashes, int type, int style, int sequence, int size)
{
    std::vector<void*> list(size_t(size > 0 ? size : 1) + 2, reinterpret_cast<void*>(uintptr_t(0x5A5A5A5A)));
    const int n = getTiles(hashes, type, style, sequence, list.data(), size);
    std::fprintf(out, "a%d t=%d s=%d q=%d n=%d ->", array, type, style, sequence, size);
    std::fprintf(out, " got=%d", n);
    for (int i = 0; i < n && i < size; ++i)
        describe(list[size_t(i)]);
    // Entries past the count must stay untouched.
    for (size_t i = size_t(n); i < list.size(); ++i)
        if (list[i] != reinterpret_cast<void*>(uintptr_t(0x5A5A5A5A)))
            std::fprintf(out, " OVERWRITE[%zu]", i);
    std::fputc('\n', out);
    ++gLines;
}

// (type, style, sequence) of every tile in a .dt1
std::set<std::tuple<int, int, int>> keysOf(const std::vector<uint8_t>& dt1)
{
    std::set<std::tuple<int, int, int>> keys;
    if (dt1.size() < 0x114)
        return keys;
    int32_t tiles = 0, start = 0;
    std::memcpy(&tiles, dt1.data() + 0x10C, 4);
    std::memcpy(&start, dt1.data() + 0x110, 4);
    for (int32_t i = 0; i < tiles; ++i)
    {
        const size_t at = size_t(start) + size_t(i) * 0x60;
        if (at + 0x60 > dt1.size())
            break;
        int32_t t, s, q;
        std::memcpy(&t, dt1.data() + at + 0x14, 4);
        std::memcpy(&s, dt1.data() + at + 0x18, 4);
        std::memcpy(&q, dt1.data() + at + 0x1C, 4);
        keys.insert({t, s, q});
    }
    return keys;
}

std::vector<std::string> split(const std::string& line, char sep)
{
    std::vector<std::string> parts;
    std::stringstream stream(line);
    std::string part;
    while (std::getline(stream, part, sep))
        parts.push_back(part);
    return parts;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: D2CmpTileTest <dll dir> <game dir> <out file>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const fs::path dllDir = fs::absolute(argv[1]), gameDir = argv[2];
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
    gStorm = load("Storm");
    HMODULE fog = load("Fog");
    HMODULE cmp = load("D2CMP");

    struct Entry { fs::path path; int priority; };
    const fs::path core = gameDir / "D2SE/CORES/1.10f/Patch_D2.mpq";
    const Entry entries[] = {{fs::exists(core) ? core : gameDir / "Patch_D2.mpq", 5000}, {gameDir / "d2exp.mpq", 4000},
                             {gameDir / "d2data.mpq", 3000}, {gameDir / "d2char.mpq", 2000}, {gameDir / "d2sfx.mpq", 1000}};
#ifdef _WIN64
    auto openArchive = fn<BOOL(__stdcall*)(const char*, int, int, void**)>(gStorm, 266);
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
    for (HMODULE m : {fog, cmp})
        refstorm::patchImports(m);
    fn<BOOL(__fastcall*)(int, int)>(fog, 10101)(0, 0);
#endif
    (void)fog;

    loadSlot = fn<LoadSlot_t>(cmp, 10087);
    getTiles = fn<GetTiles_t>(cmp, 10088);
    getType = fn<Getter_t>(cmp, 10077);
    getStyle = fn<Getter_t>(cmp, 10078);
    getFlags = fn<Getter_t>(cmp, 10079);
    getRarity = fn<Getter_t>(cmp, 10081);
    getSequence = fn<Getter_t>(cmp, 10082);
    getFlagArray = fn<FlagArray_t>(cmp, 10085);
    mixPalette = fn<Mix_t>(cmp, 10098);

    // The file lists of the level types, as the level generator loads them.
    const auto lvltypes = readFile("data\\global\\excel\\lvltypes.txt");
    if (lvltypes.empty())
    {
        std::printf("cannot read lvltypes.txt\n");
        return 1;
    }
    std::vector<std::string> lines = split(std::string(lvltypes.begin(), lvltypes.end()), '\n');
    const auto header = split(lines.at(0), '\t');
    std::vector<size_t> fileColumns;
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i].rfind("File", 0) == 0)
            fileColumns.push_back(i);

    out = std::fopen(argv[3], "w");
    if (!out)
    {
        std::printf("cannot open output file\n");
        return 2;
    }

    int arrays = 0, libraries = 0;
    for (size_t row = 1; row < lines.size(); ++row)
    {
        auto cells = split(lines[row], '\t');
        std::vector<std::string> files;
        for (size_t c : fileColumns)
        {
            if (c >= cells.size() || cells[c].empty() || cells[c] == "0" || cells[c] == "0\r")
                break;
            std::string name = cells[c];
            while (!name.empty() && (name.back() == '\r' || name.back() == ' '))
                name.pop_back();
            for (char& ch : name)
                if (ch == '/')
                    ch = '\\';
            files.push_back("data\\global\\tiles\\" + name);
        }
        if (files.empty())
            continue;

        // A fresh array of 32 slots per level type, exactly like DATATBLS_AllocGlobalTileLibraryHash.
        std::vector<void*> hashes(32 + 1, nullptr);
        std::set<std::tuple<int, int, int>> keys;
        for (const auto& file : files)
        {
            const auto dt1 = readFile(file);
            if (dt1.empty())
            {
                std::fprintf(out, "a%d MISSING %s\n", arrays, file.c_str());
                continue;
            }
            loadSlot(hashes.data(), file.c_str());
            ++libraries;
            for (const auto& key : keysOf(dt1))
                keys.insert(key);
        }
        int slots = 0;
        while (slots < 32 && hashes[size_t(slots)])
            ++slots;
        std::fprintf(out, "a%d %s files=%zu slots=%d keys=%zu\n", arrays, cells[0].c_str(), files.size(), slots, keys.size());

        for (const auto& [t, s, q] : keys)
        {
            query(arrays, hashes.data(), t, s, q, 40);
            query(arrays, hashes.data(), t, s, q, 1);
            query(arrays, hashes.data(), t, s, q, 2);
            query(arrays, hashes.data(), t, s, q, 0);
            query(arrays, hashes.data(), t, s, q + 100, 40);  // keys that do not exist
            query(arrays, hashes.data(), t, s + 1000, q, 40);
            query(arrays, hashes.data(), t + 1, s, q, 40);
        }
        ++arrays;
    }

    // Colour maps: which (transformation, colour) pairs exist and where they are relative to each other.
    const uint8_t* base = mixPalette(1, 0);
    for (int trans = 0; trans < 11; ++trans)
    {
        for (int color = -1; color < 23; ++color)
        {
            const uint8_t* p = mixPalette(uint8_t(trans), color);
            if (p)
                std::fprintf(out, "mix %d %d -> +%lld\n", trans, color, (long long)(p - base));
            else
                std::fprintf(out, "mix %d %d -> null\n", trans, color);
        }
    }
    std::fclose(out);
    std::printf("%d level types, %d library loads, %d queries -> %s\n", arrays, libraries, gLines, argv[3]);
    return 0;
}
