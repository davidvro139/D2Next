// Loads every game data table through D2Common and dumps the loaded records, so the original 32-bit D2Common
// (with the original Fog) and the x64 build can be compared record by record.
//
// The same source is built as x86 and as x64. Both call DATATBLS_LoadAllTxts against the real MPQs, then read
// tables back through D2Common's exported accessors and write a hash of every record.
//
// usage: D2CommonLoadTest <dll dir> <game dir> <output file> [--hex]
//   --hex writes the record bytes instead of a hash, to see which bytes differ.
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "RefStorm.h"

namespace fs = std::filesystem;


namespace
{
HMODULE gCommon;
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

uint64_t fnv(const uint8_t* p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

FILE* out;
bool gHex;
void record(const char* table, int index, const void* data, size_t size)
{
    if (gHex)
    {
        std::fprintf(out, "%s[%d] ", table, index);
        for (size_t i = 0; i < size; ++i)
            std::fprintf(out, "%02X", static_cast<const uint8_t*>(data)[i]);
        std::fputc('\n', out);
    }
    else
    {
        std::fprintf(out, "%s[%d] size=%zu hash=%016llX\n", table, index, size, (unsigned long long)fnv(static_cast<const uint8_t*>(data), size));
    }
}

int filter(unsigned code, EXCEPTION_POINTERS* info)
{
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(info->ExceptionRecord->ExceptionAddress), &m);
    char name[MAX_PATH] = "?";
    if (m)
        GetModuleFileNameA(m, name, MAX_PATH);
    std::printf("CRASH: exception %08X at %p in %s (offset 0x%llX)\n", code, info->ExceptionRecord->ExceptionAddress, name,
                (unsigned long long)((uintptr_t)info->ExceptionRecord->ExceptionAddress - (uintptr_t)m));
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        std::printf("  access violation %s address %p\n", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                    (void*)info->ExceptionRecord->ExceptionInformation[1]);
    auto process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);
    CONTEXT context=*info->ContextRecord;STACKFRAME64 frame{};
#ifdef _WIN64
    DWORD machine=IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;frame.AddrFrame.Offset=context.Rbp;
#else
    DWORD machine=IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset=context.Eip;frame.AddrStack.Offset=context.Esp;frame.AddrFrame.Offset=context.Ebp;
#endif
    frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
    for(int i=0;i<12&&StackWalk64(machine,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr);++i){
        char buffer[sizeof(SYMBOL_INFO)+256]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;DWORD64 displacement=0;
        if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))std::printf("  %s + 0x%llX\n",symbol->Name,(unsigned long long)displacement);
    }
    SymCleanup(process);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Reads records 0.. until the accessor returns null (capped), dumping each.
template <typename Accessor>
void dumpTable(const char* name, size_t recordSize, Accessor get, int cap = 4000, int first = 0)
{
    int n = first;
    std::printf("dumping %s\n", name);
    for (; n < cap; ++n)
    {
        const void* p = get(n);
        if (!p)
            break;
        record(name, n, p, recordSize);
    }
    std::fprintf(out, "%s count=%d\n", name, n);
}
} // namespace

// Record count of a shipped .bin (a 4 byte count followed by the records), read from the MPQs independently of
// D2Common. Some accessors assert past the end of their table, so the dump needs the real count.
static HMODULE gStorm;
static int binRecordCount(HMODULE storm, const char* path, size_t recordSize)
{
    HANDLE f = nullptr;
    DWORD size = 0;
#ifdef _WIN64
    if (!fn<BOOL(__stdcall*)(const char*, HANDLE*)>(storm, 267)(path, &f))
        return -1;
    size = fn<DWORD(__stdcall*)(HANDLE, DWORD*)>(storm, 265)(f, nullptr);
    fn<BOOL(__stdcall*)(HANDLE)>(storm, 253)(f);
#else
    (void)storm;
    if (!refstorm::openFile(path, &f))
        return -1;
    size = refstorm::fileSize(f, nullptr);
    refstorm::closeFile(f);
#endif
    return size >= 4 ? static_cast<int>((size - 4) / recordSize) : -1;
}

using LoadAll_t = void(__stdcall*)(void*, int, int);
using UnloadAll_t = void(__stdcall*)();
static int guardedLanguage(BOOL(__fastcall* init)(void*,const char*,int)) {
    __try {return init(nullptr,"ENG",1)?0:1;}
    __except(filter(GetExceptionCode(),GetExceptionInformation())){return 1;}
}

// Each guarded call lives in its own function: MSVC cannot combine __try with objects that need unwinding.
static bool gPresets; // also load the .ds1 presets and tile libraries, as a running game does
static int guardedLoad(LoadAll_t loadAll)
{
    __try
    {
        loadAll(nullptr, gPresets ? 1 : 0, gPresets ? 1 : 0);
        return 0;
    }
    __except (filter(GetExceptionCode(), GetExceptionInformation()))
    {
        return 1;
    }
}

static int guardedUnload(UnloadAll_t unloadAll)
{
    __try
    {
        unloadAll();
        return 0;
    }
    __except (filter(GetExceptionCode(), GetExceptionInformation()))
    {
        return 1;
    }
}

static int guardedDump()
{
    __try
    {
        // Record sizes are the same on x86 and x64: none of these structs contains a pointer.
        dumpTable("levels", 0x220, fn<void*(__stdcall*)(int)>(gCommon, 10631), 300, 1); // the original returns null for level 0
        dumpTable("items", 0x1A8, fn<void*(__stdcall*)(int)>(gCommon, 10600));
        dumpTable("leveldef", 0x9C, fn<void*(__fastcall*)(int)>(gCommon, 10010), 200);
        dumpTable("objects", 0x1C0, fn<void*(__stdcall*)(int)>(gCommon, 10626), binRecordCount(gStorm, "data\\global\\excel\\objects.bin", 0x1C0)); // asserts past the end
        dumpTable("shrines", 0xB8, fn<void*(__stdcall*)(int)>(gCommon, 10624), fn<int(__stdcall*)()>(gCommon, 10625)()); // asserts past the end
        dumpTable("magicaffix", 0x90, fn<void*(__stdcall*)(int)>(gCommon, 10604), 4000, 1); // ids start at 1
        dumpTable("rareaffix", 0x48, fn<void*(__stdcall*)(int)>(gCommon, 10606), 4000, 1);
        dumpTable("qualityitems", 0x70, fn<void*(__stdcall*)(int)>(gCommon, 10612), 200);
        dumpTable("gems", 0xC0, fn<void*(__stdcall*)(int)>(gCommon, 10616), 200);
        dumpTable("runes", 0x120, fn<void*(__stdcall*)(int)>(gCommon, 10620),
                  *static_cast<int*>(fn<void*(__fastcall*)()>(gCommon, 10619)()), 1); // asserts on id 0; count is the first field
        dumpTable("lvlprest", 0x1B0, fn<void*(__stdcall*)(int)>(gCommon, 10024), 4000);
        dumpTable("cubemain", 0x148, fn<void*(__stdcall*)(int)>(gCommon, 11232), fn<int(__stdcall*)()>(gCommon,11233)());
        dumpTable("difficulty", 0x58, fn<void*(__stdcall*)(int)>(gCommon, 10655), 3);
        dumpTable("books", 0x20, fn<void*(__stdcall*)(int)>(gCommon, 10618), 100);
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
        std::printf("usage: D2CommonLoadTest <dll dir> <game dir> <out file> [--hex]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep the log if a DLL takes the process down
    const fs::path dllDir = fs::absolute(argv[1]), gameDir = argv[2];
    for (int i = 4; i < argc; ++i)
    {
        gHex |= std::string(argv[i]) == "--hex";
        gPresets |= std::string(argv[i]) == "--presets";
    }
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
    HMODULE storm = gStorm = load("Storm");
    HMODULE fog=load("Fog");
    HMODULE lang=load("D2Lang");
    HMODULE cmp = load("D2CMP");
    gCommon = load("D2Common");

    struct Entry { fs::path path; int priority; };
    const fs::path core = gameDir / "D2SE/CORES/1.10f/Patch_D2.mpq";
    const Entry entries[] = {{fs::exists(core) ? core : gameDir / "Patch_D2.mpq", 5000}, {gameDir / "d2exp.mpq", 4000},
                             {gameDir / "d2data.mpq", 3000}, {gameDir / "d2char.mpq", 2000}, {gameDir / "d2sfx.mpq", 1000}};
#ifdef _WIN64
    using OpenArchive_t = BOOL(__stdcall*)(const char*, int, int, void**);
    auto openArchive = fn<OpenArchive_t>(storm, 266);
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
    // The original Storm.dll mounts the archives but then cannot open any file from them on this install, for
    // reasons that were not found. Its file API is replaced with StormLib in every module that imports it, so
    // the original D2Common/D2Lang/Fog code still runs, but on the same bytes the x64 build reads.
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
    int patched = 0;
    for (HMODULE m : {fog, lang, gCommon, cmp})
        patched += refstorm::patchImports(m);
    std::printf("redirected %d Storm file imports to StormLib\n", patched);
#endif

#ifndef _WIN64
    (void)storm;
    fn<BOOL(__fastcall*)(int,int)>(fog,10101)(0,0);
    for(const char* path:{"data\\local\\font\\latin\\default.map","data\\local\\lng\\eng\\string.tbl"}) {
        void* file=nullptr;
        BOOL ok=fn<BOOL(__fastcall*)(const char*,void**)>(fog,10102)(path,&file);
        std::printf("reference file %s: %d\n",path,ok);std::fflush(stdout);
        if(ok)fn<void(__fastcall*)(void*)>(fog,10103)(file);
    }
    // The original DLL requires explicit table initialization; native x64 loads lazily.
    auto initializeLanguage=fn<BOOL(__fastcall*)(void*,const char*,int)>(lang,10000);
    if(guardedLanguage(initializeLanguage)){std::printf("language initialization failed\n");return 3;}
#endif
    out = std::fopen(argv[3], "w");
    if(!out){std::printf("cannot open output file\n");return 2;}
    auto loadAll = fn<LoadAll_t>(gCommon, 10576);
    auto unloadAll = fn<UnloadAll_t>(gCommon, 10575);

    if (guardedLoad(loadAll))
    {
        std::printf("DATATBLS_LoadAllTxts crashed\n");
        std::fclose(out);
        return 4;
    }
    std::printf("DATATBLS_LoadAllTxts returned\n");

    if (guardedDump())
    {
        std::printf("reading tables crashed\n");
        std::fclose(out);
        return 5;
    }

    if (guardedUnload(unloadAll))
    {
        std::printf("DATATBLS_UnloadAllBins crashed\n");
        std::fclose(out);
        return 6;
    }
    std::fclose(out);
    std::printf("done: %s\n", argv[3]);
    return 0;
}
