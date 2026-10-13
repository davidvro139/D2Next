// Runs a D2Game headless: creates a game, lets a scripted client join with a character save, ticks it, and writes
// every packet the server sends. Built as x86 (for the original 32-bit D2Game) and as x64 (for the native build)
// from this one source, and compiled against D2Game's own headers so the few host-side structures have the right
// layout for each. The network is replaced by MockD2Net.dll (see there); nothing is sent over a socket.
//
// usage: D2GameHostTest <dll dir> <game dir> <mock D2Net dir> <save file> <output file> [frames] [seed] [D2Game.dll to use instead] [act 0-4]
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <D2PacketDef.h>
#include <GAME/Game.h>
#include <UNIT/SUnit.h>
#include <Path/Path.h>

#ifndef _WIN64
bool refstormMount(const char* path);
int refstormPatch(HMODULE module);
#endif

namespace fs = std::filesystem;

// The squall containers in D2Game's headers (the game table) refer to Storm's allocator and error reporting.
// D2Game does its own allocation with its own copy of them; the host only needs these to link.
void* __stdcall SMemAlloc(size_t size, const char*, int, unsigned int flags)
{
    (void)flags;
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
int __stdcall SMemFree(void* p, const char*, int, unsigned int)
{
    return p ? HeapFree(GetProcessHeap(), 0, p) : 1;
}
void* __stdcall SMemReAlloc(void* p, int size, const char*, int, unsigned int)
{
    return p ? HeapReAlloc(GetProcessHeap(), 0, p, size) : HeapAlloc(GetProcessHeap(), 0, size);
}
void __cdecl SErrDisplayAppFatal(char* format, ...)
{
    std::printf("fatal: %s\n", format);
    std::exit(9);
}
int __cdecl SErrDisplayErrorFmt(unsigned int, const char*, int, int, unsigned int, char* format, ...)
{
    std::printf("error: %s\n", format);
    return 0;
}
int __stdcall SErrPrepareAppFatal(const char*, int)
{
    return 0;
}

namespace
{
FILE* out;
std::string gSymbolPath; // where the PDBs of the DLLs under test are
HMODULE gCommon, gGame, gMock, gFog;

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
template <typename T>
T byName(HMODULE m, const char* name)
{
    auto p = reinterpret_cast<T>(GetProcAddress(m, name));
    if (!p)
    {
        std::printf("missing export %s\n", name);
        std::exit(3);
    }
    return p;
}

using Push_t = void(__cdecl*)(int, const void*, int);
using Pop_t = int(__cdecl*)(int*, int*, void*, int);
Push_t pushMessage;
Pop_t popSent;

// Return addresses found on the stack, for when the DLLs under test have no symbols to walk with.
void printStackHints(const CONTEXT* context)
{
#ifdef _WIN64
    const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(context->Rsp);
#else
    const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(context->Esp);
#endif
    int printed = 0;
    for (int i = 0; i < 600 && printed < 24; ++i)
    {
        const uintptr_t value = sp[i];
        HMODULE owner = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(value), &owner) && owner)
        {
            char module[MAX_PATH];
            GetModuleFileNameA(owner, module, MAX_PATH);
            const char* base = std::strrchr(module, '\\');
            if (base && (std::strstr(base, "D2") || std::strstr(base, "Fog") || std::strstr(base, "Storm")))
            {
                std::printf("  stack[%d] %s+0x%llX\n", i, base + 1, (unsigned long long)(value - reinterpret_cast<uintptr_t>(owner)));
                ++printed;
            }
        }
    }
}

// Prints the first exception raised, before any handler of the DLLs under test (the original Fog installs its own
// and can fail inside it) gets to hide where it came from.
LONG CALLBACK firstChance(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != 0x406D1388 && code != 0x4001000A && code != 0xE06D7363 && code != 0x40010006)
    {
        HMODULE m = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(info->ExceptionRecord->ExceptionAddress), &m);
        char name[MAX_PATH] = "?";
        if (m)
            GetModuleFileNameA(m, name, MAX_PATH);
        std::printf("first-chance exception %08lX at %p in %s (offset 0x%llX)\n", code, info->ExceptionRecord->ExceptionAddress, name,
                    (unsigned long long)((uintptr_t)info->ExceptionRecord->ExceptionAddress - (uintptr_t)m));
        if (code == 0xC0000374)
            printStackHints(info->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
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
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        std::printf("  access violation %s address %p\n", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading", (void*)info->ExceptionRecord->ExceptionInformation[1]);
    printStackHints(info->ContextRecord);
    auto process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, gSymbolPath.c_str(), TRUE);
    {
        char buffer[sizeof(SYMBOL_INFO) + 256]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, DWORD64(info->ExceptionRecord->ExceptionAddress), &displacement, symbol))
            std::printf("  in %s + 0x%llX\n", symbol->Name, (unsigned long long)displacement);
    }
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
#ifdef _WIN64
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
#else
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrFrame.Offset = context.Ebp;
#endif
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    for (int i = 0; i < 14 && StackWalk64(machine, process, GetCurrentThread(), &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++i)
    {
        char buffer[sizeof(SYMBOL_INFO) + 256]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
        {
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
                std::printf("  %s + 0x%llX  (%s:%lu)\n", symbol->Name, (unsigned long long)displacement, line.FileName, line.LineNumber);
            else
                std::printf("  %s + 0x%llX\n", symbol->Name, (unsigned long long)displacement);
        }
        else
            std::printf("  %p\n", (void*)frame.AddrPC.Offset);
    }
    SymCleanup(process);
    if (out)
        std::fprintf(out, "CRASH %08X\n", code);
    return EXCEPTION_EXECUTE_HANDLER;
}

// The DLLs report failed assertions through Fog (DisplayAssert 10023, DisplayHalt 10024). The original Fog shows a
// dialog and then crashes in its own handler, and the native one only logs, so both are redirected here: the
// message goes to the output, then an exception ends the run at the failing assertion.
void __cdecl hostAssert(const char* message, const char* file, int line)
{
    const char* base = file ? std::strrchr(file, '\\') : nullptr;
    std::fprintf(out, "ASSERT %s (%s:%d)\n", message ? message : "?", base ? base + 1 : (file ? file : "?"), line);
    std::printf("ASSERT %s (%s:%d)\n", message ? message : "?", base ? base + 1 : (file ? file : "?"), line);
    std::fflush(out);
    RaiseException(0xDEADA55E, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

void hookFogAsserts(HMODULE module)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d)
    {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), "Fog.dll") != 0)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto ordinal = IMAGE_ORDINAL(names->u1.Ordinal);
            void* target = nullptr;
            if (ordinal == 10023 || ordinal == 10024)
                target = reinterpret_cast<void*>(&hostAssert);
#ifndef _WIN64
            // The .def of this source lists Fog's GetMemoryUsage as 10052, which in the original Fog is its exception
            // filter; the original D2Game imports it as 10147. Only the 32-bit build made from this source is affected.
            else if (ordinal == 10052)
                target = reinterpret_cast<void*>(GetProcAddress(gFog, MAKEINTRESOURCEA(10147)));
#endif
            if (!target)
                continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<DWORD_PTR>(target);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
        }
    }
}

// Optional call trace (D2HOST_CALLS="first-last"): every import D2Game takes from D2Common is redirected through a stub that
// logs the ordinal while the trace frame is inside the range, so two builds can be compared call by call.
FILE* gCallLog = nullptr;
bool gCallsOn = false;
int gCallsFirst = 0, gCallsLast = -1;

void __cdecl logCall(int ordinal)
{
    if (gCallsOn && gCallLog)
        std::fprintf(gCallLog, "%d\n", ordinal);
}

void hookCommonCalls(HMODULE module)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return;
    BYTE* cells = static_cast<BYTE*>(VirtualAlloc(nullptr, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    size_t used = 0;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d)
    {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), "D2Common.dll") != 0)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const uint32_t ordinal = IMAGE_ORDINAL(names->u1.Ordinal);
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(slots->u1.Function), &mbi, sizeof(mbi)) || !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                continue; // data exports
            BYTE* cell = cells + used;
            used += 128;
            if (used > (1 << 20) - 128)
                return;
            BYTE* p = cell;
            uintptr_t* realSlot = reinterpret_cast<uintptr_t*>(cell + 112);
            *realSlot = static_cast<uintptr_t>(slots->u1.Function);
            auto emit = [&](std::initializer_list<int> bytes) {
                for (int b : bytes)
                    *p++ = static_cast<BYTE>(b);
            };
            auto emit32 = [&](uint32_t v) {
                std::memcpy(p, &v, 4);
                p += 4;
            };
#ifdef _WIN64
            emit({0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53}); // push rcx, rdx, r8, r9, r10, r11
            emit({0x48, 0x83, 0xEC, 0x68});                                     // sub rsp, 0x68
            emit({0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20});                         // movdqu [rsp+20h], xmm0
            emit({0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30});
            emit({0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40});
            emit({0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50});
            emit({0xB9});
            emit32(ordinal); // mov ecx, ordinal
            emit({0x48, 0xB8});
            const uint64_t logger = reinterpret_cast<uint64_t>(&logCall);
            std::memcpy(p, &logger, 8);
            p += 8; // mov rax, logCall
            emit({0xFF, 0xD0}); // call rax
            emit({0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20});
            emit({0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30});
            emit({0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40});
            emit({0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50});
            emit({0x48, 0x83, 0xC4, 0x68});                                     // add rsp, 0x68
            emit({0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59}); // pop r11, r10, r9, r8, rdx, rcx
            emit({0xFF, 0x25});
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<BYTE*>(realSlot) - (p + 4));
            emit32(static_cast<uint32_t>(rel)); // jmp [rip+rel]
#else
            emit({0x60, 0x9C, 0x68});
            emit32(ordinal); // pushad, pushfd, push ordinal
            emit({0xE8});
            emit32(static_cast<uint32_t>(reinterpret_cast<BYTE*>(&logCall) - (p + 4))); // call logCall
            emit({0x83, 0xC4, 0x04, 0x9D, 0x61});                                       // add esp, 4; popfd; popad
            emit({0xFF, 0x25});
            emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(realSlot))); // jmp [realSlot]
#endif
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<DWORD_PTR>(cell);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
        }
    }
}
// Everything the server has sent since the last call.
void drainSent()
{
    uint8_t buffer[2048];
    int list, client;
    for (int n; (n = popSent(&list, &client, buffer, sizeof(buffer))) >= 0;)
    {
        std::fprintf(out, "sent list=%d client=%d size=%d:", list, client, n);
        for (int i = 0; i < n; ++i)
            std::fprintf(out, " %02X", buffer[i]);
        std::fputc('\n', out);
    }
}

// The callbacks of the host application (the "realm server" the game reports to). They log what D2Game reports.
struct PendingCharacter
{
    int requested = 0;
    int clientId = 0;
};
PendingCharacter gPending;
int gTransactionToken = 77;

void __cdecl cbLog(int32_t level, const char* message)
{
    if (out)
        std::fprintf(out, "log[%d] %s\n", level, message ? message : "");
}
void __fastcall cbCloseGame(WORD gameId, uint32_t product, uint32_t players, int32_t frame)
{
    std::fprintf(out, "cb CloseGame game=%u product=%u players=%u frame=%d\n", gameId, product, players, frame);
}
void __fastcall cbLeaveGame(D2ClientInfoStrc**, WORD gameId, int32_t classId, int32_t level, uint64_t experience, uint32_t flags, const char* name, const char*, int32_t, int32_t,
                            int32_t, const char* account, int32_t token, FILETIME*)
{
    std::fprintf(out, "cb LeaveGame game=%u class=%d level=%d exp=%llu flags=%08X name=%s account=%s token=%d\n", gameId, classId, level, (unsigned long long)experience, flags,
                 name, account, token);
}
void __fastcall cbGetCharacter(D2ClientInfoStrc**, const char* name, DWORD clientId, const char* account)
{
    std::fprintf(out, "cb GetDatabaseCharacter name=%s client=%lu account=%s\n", name, clientId, account);
    gPending.requested = 1;
    gPending.clientId = int(clientId);
}
void __fastcall cbSaveCharacter(D2ClientInfoStrc**, const char* name, const char* account, BYTE* data, uint32_t size, int32_t token)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    std::fprintf(out, "cb SaveDatabaseCharacter name=%s account=%s size=%u token=%d hash=%016llX\n", name, account, size, token, (unsigned long long)h);
}
void __fastcall cbEnterGame(WORD gameId, const char* name, int32_t classId, int32_t level, uint32_t flags)
{
    std::fprintf(out, "cb EnterGame game=%u name=%s class=%d level=%d flags=%08X\n", gameId, name, classId, level, flags);
}
int32_t __fastcall cbFindToken(const char* name, int32_t token, WORD gameId, char* account, int32_t* transaction, int32_t* a6, int32_t* a7)
{
    std::fprintf(out, "cb FindPlayerToken name=%s token=%d game=%u\n", name, token, gameId);
    std::strcpy(account, "account");
    *transaction = gTransactionToken;
    *a6 = 0;
    *a7 = 0;
    return 1;
}
void __fastcall cbUnlock(uint32_t*, const char* name, const char* account) { std::fprintf(out, "cb UnlockDatabaseCharacter %s %s\n", name, account); }
void __fastcall cbRelock(D2ClientInfoStrc**, const char* name, const char* account) { std::fprintf(out, "cb RelockDatabaseCharacter %s %s\n", name, account); }
void __fastcall cbLadder(const char* name, int32_t classId, int32_t level, uint32_t experience, int32_t, uint32_t flags, FILETIME*)
{
    std::fprintf(out, "cb UpdateCharacterLadder %s class=%d level=%d exp=%u flags=%08X\n", name, classId, level, experience, flags);
}
void __fastcall cbGameInfo(WORD gameId, const char* name, int32_t classId, int32_t level)
{
    std::fprintf(out, "cb UpdateGameInformation game=%u %s class=%d level=%d\n", gameId, name, classId, level);
}
void __fastcall cbHandlePacket(void*, int32_t size) { std::fprintf(out, "cb HandlePacket size=%d\n", size); }
uint32_t __fastcall cbSetGameData() { return 0; }
using Void_t = void(__stdcall*)();
using ProcessMessages_t = void(__fastcall*)();
using UpdateProgress_t = int32_t(__stdcall*)(int32_t);

struct Host
{
    ProcessMessages_t processMessages;
    UpdateProgress_t updateProgress;
    void(__stdcall* updateClients)(int32_t, int32_t);
};

int32_t statOf(int32_t type, int32_t guid, int32_t stat)
{
    D2UnitStrc* unit = fn<D2UnitStrc*(__stdcall*)(int32_t, int32_t)>(gGame, 10025)(type, guid);
    return unit ? fn<int32_t(__stdcall*)(D2UnitStrc*, int32_t, int32_t)>(gCommon, 10519)(unit, stat, 0) : -1;
}
// Life points of a unit (the stat is in 1/256 points), or -1 when the unit is not there.
int32_t lifeOf(int32_t type, int32_t guid)
{
    D2UnitStrc* unit = fn<D2UnitStrc*(__stdcall*)(int32_t, int32_t)>(gGame, 10025)(type, guid);
    if (!unit)
        return -1;
    return fn<int32_t(__stdcall*)(D2UnitStrc*, int32_t, int32_t)>(gCommon, 10519)(unit, 6, 0);
}
// What the server says about the game: no pointers, no clocks, so both builds must agree.
void report(const char* when, uint16_t gameId)
{
    D2GameInfoStrc info{};
    const BOOL ok = fn<BOOL(__stdcall*)(uint16_t, D2GameInfoStrc*)>(gGame, 10014)(gameId, &info);
    const int32_t players = fn<int32_t(__stdcall*)(uint16_t)>(gGame, 10016)(gameId);
    std::fprintf(out, "report %s: ok=%d clients=%d players=%d monsters=%d objects=%d items=%d missiles=%d npcs=%d frames/100=%d unitPlayers=%d\n", when, ok, info.nClients,
                 info.nPlayers, info.nMonsters, info.nObjects, info.nItems, info.nMissiles, info.nNPCs, info.nFrames, players);
    D2UnitInfoStrc units[4]{};
    const int32_t n = fn<int32_t(__stdcall*)(uint16_t, D2UnitInfoStrc*, int32_t)>(gGame, 10017)(gameId, units, 4);
    for (int32_t i = 0; i < n; ++i)
        std::fprintf(out, "  player '%s' class=%d guid=%d mode=%d pos=%d,%d life=%d\n", units[i].szDescription, units[i].nClassId, units[i].nUnitGUID, units[i].nAnimMode, units[i].nX, units[i].nY,
                     lifeOf(UNIT_PLAYER, units[i].nUnitGUID));

    // Every monster the server knows, looked up by GUID (they are handed out one after the other)
    constexpr int32_t maxMonsters = 400;
    const int32_t lastMonster = std::min<int32_t>(int32_t(info.dwLastUsedUnitGUID[UNIT_MONSTER]), maxMonsters);
    if (lastMonster > 0)
    {
        static D2UnitInfoStrc monsters[maxMonsters];
        int32_t types[maxMonsters], guids[maxMonsters];
        for (int32_t i = 0; i < lastMonster; ++i)
        {
            types[i] = UNIT_MONSTER;
            guids[i] = i + 1;
        }
        std::memset(monsters, 0, sizeof(monsters));
        const int32_t found = fn<int32_t(__stdcall*)(uint16_t, int32_t, D2UnitInfoStrc*, int32_t*, int32_t*)>(gGame, 10018)(gameId, lastMonster, monsters, types, guids);
        for (int32_t i = 0; i < found; ++i)
            std::fprintf(out, "  monster '%s' class=%d guid=%d mode=%d pos=%d,%d life=%d\n", monsters[i].szDescription, monsters[i].nClassId, monsters[i].nUnitGUID, monsters[i].nAnimMode, monsters[i].nX,
                         monsters[i].nY, lifeOf(UNIT_MONSTER, monsters[i].nUnitGUID));
    }
}
uint16_t gTraceGame = 0;
bool gTrace = false;
int gTraceFrame = 0;

// With D2HOST_TRACE set: the state of the first monsters after every frame, to find the frame where two builds part.
void traceUnits(int frame)
{
    for (int32_t k = 0; k <= 40; ++k)
    {
        const int32_t type = k == 0 ? UNIT_PLAYER : UNIT_MONSTER;
        const int32_t guid = k == 0 ? 1 : k;
        D2UnitStrc* unit = fn<D2UnitStrc*(__stdcall*)(int32_t, int32_t)>(gGame, 10025)(type, guid);
        if (!unit || !unit->pDynamicPath)
            continue;
        const D2DynamicPathStrc* path = unit->pDynamicPath;
        const uint32_t* raw = reinterpret_cast<const uint32_t*>(path);
        const int32_t life = fn<int32_t(__stdcall*)(D2UnitStrc*, int32_t, int32_t)>(gCommon, 10519)(unit, 6, 0);
        std::fprintf(out, "trace f%d %c%d seed=%08X,%08X cls=%d mode=%u life=%d pos=%08X,%08X tgt=%u,%u fin=%u,%u idx=%d/%d flg=%X type=%u dir=%u vel=%d cf=%d/%d spd=%d af=%u\n", frame,
                     type == UNIT_PLAYER ? 'p' : 'm', guid, unit->pSeed.nLowSeed, unit->pSeed.nHighSeed, unit->dwClassId, unit->dwAnimMode, life, raw[0], raw[1], path->tTargetCoord.X,
                     path->tTargetCoord.Y, path->tFinalTargetCoord.X, path->tFinalTargetCoord.Y, path->dwCurrentPointIdx, path->dwPathPoints, path->dwFlags, path->dwPathType, path->nDirection,
                     path->dwVelocity, unit->nSeqCurrentFramePrecise, unit->dwFrameCountPrecise, unit->wAnimSpeed, unit->nActionFrame);
    }
}
void step(const Host& host, int rounds)
{
    for (int i = 0; i < rounds; ++i)
    {
        if (gCallLog)
        {
            gCallsOn = gTraceFrame + 1 >= gCallsFirst && gTraceFrame + 1 <= gCallsLast;
            if (gCallsOn)
                std::fprintf(gCallLog, "F %d\n", gTraceFrame + 1);
        }
        host.processMessages();
        host.updateProgress(1);
        host.updateClients(1, 0); // flushes what the game queued for its clients
        if (gCallLog)
            std::fflush(gCallLog);
        if (gTrace && gTraceGame)
            traceUnits(++gTraceFrame);
        Sleep(2);
    }
    drainSent();
}

std::string gRoute; // absolute run targets "x,y;x,y" tried after the fixed walk, to leave the town

int run(const std::vector<uint8_t>& save, int frames, uint32_t seed, int act)
{
    Host host{fn<ProcessMessages_t>(gGame, 10003), fn<UpdateProgress_t>(gGame, 10004), fn<void(__stdcall*)(int32_t, int32_t)>(gGame, 10005)};

    fn<Void_t>(gGame, 10039)(); // TASK_InitializeClock
    fn<int32_t(__stdcall*)()>(gGame, 10046)();
    fn<void(__stdcall*)(int32_t)>(gGame, 10010)(int32_t(seed)); // GAME_SetInitSeed
    fn<void(__stdcall*)(int32_t)>(gGame, 10009)(act);           // GAME_SetGlobalAct: the act the characters start in

    static D2ServerCallbackFunctions callbacks;
    callbacks.pfCloseGame = cbCloseGame;
    callbacks.pfLeaveGame = cbLeaveGame;
    callbacks.pfGetDatabaseCharacter = cbGetCharacter;
    callbacks.pfSaveDatabaseCharacter = cbSaveCharacter;
    callbacks.pfServerLogMessage = cbLog;
    callbacks.pfEnterGame = cbEnterGame;
    callbacks.pfFindPlayerToken = cbFindToken;
    callbacks.pfUnlockDatabaseCharacter = cbUnlock;
    callbacks.pfUpdateCharacterLadder = cbLadder;
    callbacks.pfUpdateGameInformation = cbGameInfo;
    callbacks.pfHandlePacket = cbHandlePacket;
    callbacks.pfSetGameData = cbSetGameData;
    callbacks.pfRelockDatabaseCharacter = cbRelock;
    fn<void(__stdcall*)(D2ServerCallbackFunctions*)>(gGame, 10023)(&callbacks);

    static D2GameDataTableStrc table;
    static int gameList[64];
    fn<void(__stdcall*)(void*, void*)>(gGame, 10002)(&table, gameList);

    uint16_t gameId = 0;
    char name[] = "hosttest";
    const uint32_t flags = GAMEFLAG_ARENA_EXPANSION | GAMEFLAG_ARENA_UPDATECLIENTS; // normal difficulty; the server requires the update flag
    const BOOL created = fn<BOOL(__stdcall*)(char*, const char*, const char*, uint32_t, uint8_t, uint8_t, uint8_t, uint16_t*)>(gGame, 10047)(name, "", "", flags, 0, 0, 8, &gameId);
    std::fprintf(out, "created=%d gameId=%u\n", created, gameId);
    gTrace = std::getenv("D2HOST_TRACE") != nullptr;
    gTraceGame = gameId;
    if (!created)
        return 1;

    // The client joins: a 0x67 system message, prefixed with the id of the client.
    const int client = 1;
#pragma pack(push, 1)
    struct
    {
        int32_t client;
        D2GSPacketClt67 packet;
    } join{};
#pragma pack(pop)
    join.client = client;
    join.packet.nHeader = 0x67;
    join.packet.nTokenId = 0x1234;
    join.packet.nGameId = gameId;
    join.packet.nPlayerClass = save.size() > 0x28 ? save[0x28] : 0;
    join.packet.unk0x08 = 10; // the client version the server accepts
    join.packet.nLocale = 0;
    std::memcpy(join.packet.szClientName, &save[0x14], 16);
    pushMessage(0, &join, int(sizeof(join)));
    step(host, 5);

    // The "database" answers with the character save.
    FILETIME time{};
    const int32_t received = fn<int32_t(__stdcall*)(int32_t, const uint8_t*, uint16_t, uint16_t, int32_t, int32_t, FILETIME*, int32_t)>(gGame, 10007)(
        client, save.data(), uint16_t(save.size()), uint16_t(save.size()), 0, 0, &time, gTransactionToken);
    std::fprintf(out, "receive character (requested=%d) -> %d\n", gPending.requested, received);
    step(host, 5);

    // The client asks to enter the act.
#pragma pack(push, 1)
    struct
    {
        int32_t client;
        uint8_t header;
    } joinAct{client, 0x6A};
#pragma pack(pop)
    pushMessage(0, &joinAct, int(sizeof(joinAct)));
    step(host, 5);
    report("after join", gameId);
    std::fprintf(out, "frames: %d\n", frames);
    step(host, frames);
    report("after frames", gameId);

    // The client plays: it runs, then walks. Coordinates are relative to where the server put the character.
    D2UnitInfoStrc where{};
    if (fn<int32_t(__stdcall*)(uint16_t, D2UnitInfoStrc*, int32_t)>(gGame, 10017)(gameId, &where, 1) == 1)
    {
        auto command = [&](uint8_t header, int dx, int dy) {
#pragma pack(push, 1)
            struct
            {
                int32_t client;
                D2GSPacketClt03 packet;
            } message{};
#pragma pack(pop)
            message.client = client;
            message.packet.nHeader = header;
            message.packet.nPosX = uint16_t(where.nX + dx);
            message.packet.nPosY = uint16_t(where.nY + dy);
            std::fprintf(out, "client command %02X to %u,%u\n", header, message.packet.nPosX, message.packet.nPosY);
            pushMessage(1, &message, int(sizeof(message)));
        };
        command(0x03, 12, 6); // run
        step(host, frames);
        report("after run", gameId);
        command(0x01, -8, 10); // walk
        step(host, frames);
        report("after walk", gameId);
        // A route of absolute run targets, one after the other.
        for (size_t at = 0; at < gRoute.size();)
        {
            const size_t end = gRoute.find(';', at);
            const std::string target = gRoute.substr(at, end == std::string::npos ? std::string::npos : end - at);
            int x = 0, y = 0;
            if (std::sscanf(target.c_str(), "%d,%d", &x, &y) == 2)
            {
                D2UnitInfoStrc now{};
                fn<int32_t(__stdcall*)(uint16_t, D2UnitInfoStrc*, int32_t)>(gGame, 10017)(gameId, &now, 1);
                where.nX = now.nX;
                where.nY = now.nY;
                command(std::getenv("D2HOST_ROUTEWALK") ? 0x01 : 0x03, x - now.nX, y - now.nY);
                step(host, frames / 2);
                report("after route step", gameId);
            }
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
        // D2HOST_FIGHT=n: the client attacks the nearest hostile monster n times (left skill on a unit), watching it.
        if (const char* fight = std::getenv("D2HOST_FIGHT"))
        {
            const int rounds = std::atoi(fight);
            if (std::getenv("D2HOST_KICK"))
            {
                // Left skill: kick (id 1); bit 31 of the id word selects the left hand.
#pragma pack(push, 1)
                struct
                {
                    int32_t client;
                    uint8_t header;
                    uint32_t skill;
                    uint32_t owner;
                } select{client, 0x3C, 0x80000001u, 0xFFFFFFFFu};
#pragma pack(pop)
                std::fprintf(out, "client selects the kick skill\n");
                pushMessage(1, &select, int(sizeof(select)));
                step(host, 10);
                report("after select", gameId);
            }
            for (int round = 0; round < rounds; ++round)
            {
                if (const char* learn = std::getenv("D2HOST_LEARN"))
                {
                    // Spend the first skill point as soon as there is one, then use that skill as the left skill.
                    static bool learned = false;
                    if (!learned && statOf(UNIT_PLAYER, 1, 5) > 0) // STAT_NEWSKILLS
                    {
                        learned = true;
                        const uint16_t skill = uint16_t(std::atoi(learn));
#pragma pack(push, 1)
                        struct
                        {
                            int32_t client;
                            uint8_t header;
                            uint16_t skill;
                        } assign{client, 0x3B, skill};
                        struct
                        {
                            int32_t client;
                            uint8_t header;
                            uint32_t skill;
                            uint32_t owner;
                        } select{client, 0x3C, 0x80000000u | skill, 0xFFFFFFFFu};
#pragma pack(pop)
                        std::fprintf(out, "client learns skill %u\n", skill);
                        pushMessage(1, &assign, int(sizeof(assign)));
                        step(host, 10);
                        report("after learning", gameId);
                        pushMessage(1, &select, int(sizeof(select)));
                        step(host, 10);
                        report("after selecting the new skill", gameId);
                    }
                }
                D2UnitInfoStrc me{};
                fn<int32_t(__stdcall*)(uint16_t, D2UnitInfoStrc*, int32_t)>(gGame, 10017)(gameId, &me, 1);
                D2GameInfoStrc info{};
                fn<BOOL(__stdcall*)(uint16_t, D2GameInfoStrc*)>(gGame, 10014)(gameId, &info);
                int bestGuid = 0;
                int64_t bestDistance = INT64_MAX;
                int bestX = 0, bestY = 0;
                for (int32_t guid = 1; guid <= int32_t(info.dwLastUsedUnitGUID[UNIT_MONSTER]); ++guid)
                {
                    int32_t type = UNIT_MONSTER, id = guid;
                    D2UnitInfoStrc m{};
                    if (fn<int32_t(__stdcall*)(uint16_t, int32_t, D2UnitInfoStrc*, int32_t*, int32_t*)>(gGame, 10018)(gameId, 1, &m, &type, &id) != 1)
                        continue;
                    if (m.nAnimMode == 0 || m.nAnimMode == 12) // dying or dead
                        continue;
                    const int c = m.nClassId; // the town people and critters of act 1 are not enemies
                    if (c == 146 || c == 147 || c == 148 || c == 149 || c == 150 || c == 151 || c == 152 || c == 154 || c == 155)
                        continue;
                    const int64_t dx = m.nX - me.nX, dy = m.nY - me.nY, d = dx * dx + dy * dy;
                    if (d < bestDistance)
                    {
                        bestDistance = d;
                        bestGuid = guid;
                        bestX = m.nX;
                        bestY = m.nY;
                    }
                }
                if (!bestGuid)
                {
                    std::fprintf(out, "fight: nothing left to attack\n");
                    break;
                }
                if (std::getenv("D2HOST_PASSIVE"))
                {
                    // Stand next to it instead of attacking: what does the monster do to us?
                    where.nX = me.nX;
                    where.nY = me.nY;
                    command(0x03, bestX - me.nX, bestY - me.nY);
                    step(host, frames / 2);
                    report("after approach", gameId);
                    continue;
                }
#pragma pack(push, 1)
                struct
                {
                    int32_t client;
                    D2GSPacketClt06 packet;
                } attack{};
#pragma pack(pop)
                attack.client = client;
                attack.packet.nHeader = 0x06;
                attack.packet.dwUnitType = UNIT_MONSTER;
                attack.packet.dwUnitGUID = bestGuid;
                std::fprintf(out, "client attacks monster %d\n", bestGuid);
                if (!std::getenv("D2HOST_PASSIVE"))
                    pushMessage(1, &attack, int(sizeof(attack)));
                step(host, frames / 2);
                report("after attack", gameId);
            }
        }
        // D2HOST_ITEMS=1: the client handles items: picks up what lies near it, puts it in the inventory, drinks the belt potions
        // and selects/uses a skill.
        if (std::getenv("D2HOST_ITEMS"))
        {
            auto sendItemPacket = [&](const void* packet, int size) {
                struct { int32_t client; uint8_t data[32]; } message{};
                message.client = client;
                std::memcpy(message.data, packet, size);
                pushMessage(1, &message, 4 + size);
            };
            auto listItems = [&](std::vector<D2UnitInfoStrc>& items) {
                items.clear();
                D2GameInfoStrc info{};
                fn<BOOL(__stdcall*)(uint16_t, D2GameInfoStrc*)>(gGame, 10014)(gameId, &info);
                for (int32_t guid = 1; guid <= int32_t(info.dwLastUsedUnitGUID[UNIT_ITEM]); ++guid)
                {
                    int32_t type = UNIT_ITEM, id = guid;
                    D2UnitInfoStrc item{};
                    if (fn<int32_t(__stdcall*)(uint16_t, int32_t, D2UnitInfoStrc*, int32_t*, int32_t*)>(gGame, 10018)(gameId, 1, &item, &type, &id) == 1)
                        items.push_back(item);
                }
            };
            std::vector<D2UnitInfoStrc> items;
            listItems(items);
            for (const auto& item : items)
                std::fprintf(out, "  item guid=%d class=%d mode=%d pos=%d,%d\n", item.nUnitGUID, item.nClassId, item.nAnimMode, item.nX, item.nY);
            D2UnitInfoStrc me{};
            fn<int32_t(__stdcall*)(uint16_t, D2UnitInfoStrc*, int32_t)>(gGame, 10017)(gameId, &me, 1);

            // Pick up everything on the ground within reach, and store it.
            int slot = 0;
            for (const auto& item : items)
            {
                if (item.nAnimMode != 3)
                    continue;
                const int64_t dx = item.nX - me.nX, dy = item.nY - me.nY;
                if (dx * dx + dy * dy > 150 * 150)
                    continue;
                where.nX = me.nX;
                where.nY = me.nY;
                command(0x03, item.nX - me.nX, item.nY - me.nY); // walk up to it first
                step(host, 100);
                fn<int32_t(__stdcall*)(uint16_t, D2UnitInfoStrc*, int32_t)>(gGame, 10017)(gameId, &me, 1);
                D2GSPacketClt16 pickup{};
                pickup.nHeader = 0x16;
                pickup.dwUnitType = UNIT_ITEM;
                pickup.dwUnitGUID = item.nUnitGUID;
                pickup.bCursor = 1;
                std::fprintf(out, "client picks up item %d\n", item.nUnitGUID);
                sendItemPacket(&pickup, sizeof(pickup));
                step(host, 60);
                report("after pickup", gameId);
                D2GSPacketClt18 store{};
                store.nHeader = 0x18;
                store.dwItemGUID = item.nUnitGUID;
                store.dwPosX = uint32_t(slot++ % 10);
                store.dwPosY = uint32_t(slot / 10 * 2);
                store.dwInvPage = 0;
                sendItemPacket(&store, sizeof(store));
                step(host, 30);
                report("after store", gameId);
            }

            // Drink whatever sits in the belt.
            listItems(items);
            for (const auto& item : items)
            {
                if (item.nAnimMode != 2)
                    continue;
                D2GSPacketClt26 use{};
                use.nHeader = 0x26;
                use.nItemGUID = item.nUnitGUID;
                use.bUseOnMerc = 0;
                use.unk0x09 = 0;
                std::fprintf(out, "client uses item %d (class %d)\n", item.nUnitGUID, item.nClassId);
                sendItemPacket(&use, sizeof(use));
                step(host, 60);
                report("after use", gameId);
            }

            // Select the kick skill as the left skill (0x3C), then attack with it.
            D2GSPacketClt3C select{};
            select.nHeader = 0x3C;
            select.nSkill = 1;   // kick
            select.nMode = 0x8000; // left (bit 31 of the id word)
            select.dwFlags = 0xFFFFFFFF;
            std::fprintf(out, "client selects skill 1 (kick)\n");
            sendItemPacket(&select, sizeof(select));
            step(host, 20);
            report("after select", gameId);
        }
        // D2HOST_RESPAWN=1: a dead player asks to come back (0x41) and the game is watched for a while.
        if (std::getenv("D2HOST_RESPAWN"))
        {
            report("before respawn", gameId);
            if (statOf(UNIT_PLAYER, 1, 6) <= 0)
            {
#pragma pack(push, 1)
                struct
                {
                    int32_t client;
                    uint8_t header;
                } respawn{client, 0x41};
#pragma pack(pop)
                std::fprintf(out, "client asks to respawn\n");
                pushMessage(1, &respawn, int(sizeof(respawn)));
                step(host, 100);
                report("after respawn", gameId);
                step(host, 100);
                report("later", gameId);
            }
        }
    }

    // The client leaves.
#pragma pack(push, 1)
    struct
    {
        int32_t client;
        uint8_t header;
    } leave{client, 0x6D};
#pragma pack(pop)
    pushMessage(0, &leave, int(sizeof(leave)));
    step(host, 10);
    report("after leaving", gameId);
    return 0;
}

int guardedRun(const std::vector<uint8_t>& save, int frames, uint32_t seed, int act)
{
    __try
    {
        return run(save, frames, seed, act);
    }
    __except (filter(GetExceptionCode(), GetExceptionInformation()))
    {
        return 100;
    }
}

int guardedLoad()
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
} // namespace

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::printf("usage: D2GameHostTest <dll dir> <game dir> <mock D2Net dir> <save file> <out file> [frames] [seed]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const fs::path dllDir = fs::absolute(argv[1]), gameDir = argv[2], mockDir = fs::absolute(argv[3]);
    std::vector<uint8_t> save;
    {
        std::ifstream f(argv[4], std::ios::binary);
        save.assign(std::istreambuf_iterator<char>(f), {});
    }
    if (save.size() < 0x30)
    {
        std::printf("cannot read %s\n", argv[4]);
        return 2;
    }
    const int frames = argc > 6 ? std::atoi(argv[6]) : 50;
    const uint32_t seed = argc > 7 ? uint32_t(std::strtoul(argv[7], nullptr, 0)) : 0x12345678;
    gSymbolPath = dllDir.string();
    AddVectoredExceptionHandler(1, firstChance);
    SetDllDirectoryW(gameDir.c_str());
    SetCurrentDirectoryW(dllDir.c_str());

    auto load = [&](const fs::path& path) {
        HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m)
        {
            std::printf("cannot load %s (error %lu)\n", path.string().c_str(), GetLastError());
            std::exit(1);
        }
        return m;
    };
    HMODULE storm = load(dllDir / "Storm.dll");
    HMODULE fog = gFog = load(dllDir / "Fog.dll");
    HMODULE lang = load(dllDir / "D2Lang.dll");
    HMODULE cmp = load(dllDir / "D2CMP.dll");
    gMock = load(mockDir / "D2Net.dll"); // loaded first, so D2Game's import of D2Net.dll resolves to it
    gCommon = load(std::getenv("D2HOST_COMMON") ? fs::path(std::getenv("D2HOST_COMMON")) : dllDir / "D2Common.dll");
    // The original D2Game cannot be driven by this host: its game table is built by the original host with the
    // 1.10 era container classes. The D2Game built from this source as x86 stands in for it (same code, 32-bit).
    gGame = load(argc > 8 && argv[8][0] != '-' ? fs::path(argv[8]) : dllDir / "D2Game.dll");
    for (HMODULE m : {gCommon, gGame})
        hookFogAsserts(m);
    if (const char* calls = std::getenv("D2HOST_CALLS"))
    {
        std::sscanf(calls, "%d-%d", &gCallsFirst, &gCallsLast);
        gCallLog = std::fopen((std::string(argv[5]) + ".calls").c_str(), "w");
        hookCommonCalls(gGame);
    }
    pushMessage = byName<Push_t>(gMock, "Mock_PushMessage");
    popSent = byName<Pop_t>(gMock, "Mock_PopSent");

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
    (void)lang;
    (void)cmp;
    (void)fog;
#else
    (void)storm;
    for (const auto& e : entries)
    {
        if (!fs::exists(e.path))
            continue;
        if (!refstormMount(e.path.string().c_str()))
        {
            std::printf("cannot open %s\n", e.path.string().c_str());
            return 1;
        }
    }
    for (HMODULE m : {fog, lang, cmp, gCommon, gGame})
        refstormPatch(m);
    fn<BOOL(__fastcall*)(int, int)>(fog, 10101)(0, 0);
    // The start-up calls the game makes right after that (see GameStart in source/Game).
    fn<void(__fastcall*)(BOOL)>(fog, 10089)(TRUE);
    fn<void(__fastcall*)()>(fog, 10218)();
    if (!fn<BOOL(__fastcall*)(void*, const char*, int)>(lang, 10000)(nullptr, "ENG", 1))
    {
        std::printf("language initialization failed\n");
        return 3;
    }
#endif

    out = std::fopen(argv[5], "w");
    if (!out)
        return 2;
    if (guardedLoad())
    {
        std::printf("DATATBLS_LoadAllTxts crashed\n");
        return 4;
    }
    const int act = argc > 9 ? std::atoi(argv[9]) : 0;
    if (argc > 10)
        gRoute = argv[10];
    const int result = guardedRun(save, frames, seed, act);
    std::fclose(out);
    std::printf("finished with %d -> %s\n", result, argv[5]);
    std::fflush(stdout);
    // No static destructors: the game table would free memory that the game allocated with its own heap.
    TerminateProcess(GetCurrentProcess(), UINT(result));
    return result;
}
