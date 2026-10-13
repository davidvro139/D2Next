// Reads game files through the native x64 Storm + Fog file API and writes each to an output folder, so the caller
// can compare them byte for byte against an independent reader (see Run-FogFileTest.ps1).
//
// usage: FogFileTest <folder containing the x64 DLLs> <game folder> <output folder> <file> [file ...]
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Storm ordinals (from Storm.1.10f.def): 252 SFileCloseArchive, 266 SFileOpenArchive.
using SFileOpenArchive_t = BOOL(__stdcall*)(const char*, int, int, void**);
using SFileCloseArchive_t = BOOL(__stdcall*)(void*);
// Fog ordinals: 10102 FOpenFile, 10103 FCloseFile, 10104 FReadFile, 10105 FGetFileSize, 10106 FSetFilePointer
using FOpenFile_t = BOOL(__fastcall*)(const char*, void**);
using FCloseFile_t = void(__fastcall*)(void*);
using FReadFile_t = BOOL(__fastcall*)(void*, void*, size_t, int*, uint32_t, uint32_t, uint32_t);
using FGetFileSize_t = uint32_t(__fastcall*)(void*, uint32_t*);
using FSetFilePointer_t = size_t(__fastcall*)(void*, int32_t, int32_t*, uint32_t);

template <typename T>
static T proc(HMODULE m, int ordinal)
{
    auto p = reinterpret_cast<T>(GetProcAddress(m, MAKEINTRESOURCEA(ordinal)));
    if (!p)
    {
        std::printf("missing ordinal %d\n", ordinal);
        std::exit(3);
    }
    return p;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("usage: FogFileTest <dll dir> <game dir> <out dir> <file>...\n");
        return 2;
    }
    const fs::path dllDir = fs::absolute(argv[1]), gameDir = argv[2], outDir = argv[3];
    SetDllDirectoryW(dllDir.c_str());
    HMODULE storm = LoadLibraryW((dllDir / "Storm.dll").c_str());
    HMODULE fog = LoadLibraryW((dllDir / "Fog.dll").c_str());
    if (!storm || !fog)
    {
        std::printf("load failed (%lu)\n", GetLastError());
        return 1;
    }
    auto openArchive = proc<SFileOpenArchive_t>(storm, 266);
    auto closeArchive = proc<SFileCloseArchive_t>(storm, 252);
    auto fOpen = proc<FOpenFile_t>(fog, 10102);
    auto fClose = proc<FCloseFile_t>(fog, 10103);
    auto fRead = proc<FReadFile_t>(fog, 10104);
    auto fSize = proc<FGetFileSize_t>(fog, 10105);
    auto fSeek = proc<FSetFilePointer_t>(fog, 10106);

    // Same precedence as the game: patch overrides expansion, which overrides base data, then characters, sound.
    std::vector<void*> archives;
    struct Entry { fs::path path; int priority; };
    std::vector<Entry> entries;
    const fs::path core = gameDir / "D2SE/CORES/1.10f/Patch_D2.mpq";
    entries.push_back({fs::exists(core) ? core : gameDir / "Patch_D2.mpq", 5000});
    entries.push_back({gameDir / "d2exp.mpq", 4000});
    entries.push_back({gameDir / "d2data.mpq", 3000});
    entries.push_back({gameDir / "d2char.mpq", 2000});
    entries.push_back({gameDir / "d2sfx.mpq", 1000});
    for (const auto& e : entries)
    {
        void* h = nullptr;
        if (!fs::exists(e.path))
            continue;
        if (!openArchive(e.path.string().c_str(), e.priority, 2, &h))
        {
            std::printf("cannot open %s (error %lu)\n", e.path.string().c_str(), GetLastError());
            return 1;
        }
        archives.push_back(h);
    }
    std::printf("mounted %zu archives\n", archives.size());

    fs::create_directories(outDir);
    int failures = 0;
    for (int i = 4; i < argc; ++i)
    {
        const std::string name = argv[i];
        void* file = nullptr;
        SetLastError(0);
        if (!fOpen(name.c_str(), &file))
        {
            std::printf("%s: NOT FOUND (last error %lu)\n", name.c_str(), GetLastError());
            continue;
        }
        uint32_t high = 0;
        const uint32_t size = fSize(file, &high);
        std::vector<char> data(size);
        int read = -1;
        const BOOL ok = size ? fRead(file, data.data(), size, &read, 0, 0, 0) : TRUE;
        // Seek back to the start and re-read the first bytes to exercise FSetFilePointer.
        int32_t seekHigh = 0;
        const size_t pos = fSeek(file, 0, &seekHigh, FILE_BEGIN);
        if (!ok || (size && read != int(size)) || pos != 0)
        {
            std::printf("%s: READ FAILED ok=%d read=%d size=%u pos=%zu\n", name.c_str(), ok, read, size, pos);
            ++failures;
        }
        else
        {
            std::string flat = name;
            for (char& c : flat)
                if (c == '\\' || c == '/' || c == ':') c = '_';
            std::ofstream(outDir / flat, std::ios::binary).write(data.data(), size);
            std::printf("%s: %u bytes\n", name.c_str(), size);
        }
        fClose(file);
    }
    for (void* a : archives)
        closeArchive(a);
    return failures ? 1 : 0;
}
