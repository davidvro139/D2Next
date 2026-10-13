// Stand-ins for the file functions of the original 32-bit Storm.dll, for the differential tests that run the
// original DLLs (32-bit builds only).
//
// On this Diablo II 1.10 install the original Storm.dll mounts the MPQs but then cannot open any file from them
// (the cause was not found), so the tests redirect the Storm file imports of the original Fog, D2Lang, D2CMP and
// D2Common to StormLib. Both the original and the native x64 side then read the same bytes.
#pragma once
#ifndef _WIN64
#include <windows.h>

#include <vector>

#define __STORMLIB_NO_STATIC_LINK__ // The build script links the library explicitly
#include <StormLib.h>

// Stand-ins for the Storm.dll file functions that the original Fog/D2Lang/D2Common import by ordinal.
namespace refstorm
{
std::vector<HANDLE> archives; // highest priority first

BOOL __stdcall openFile(const char* name, HANDLE* out)
{
    for (HANDLE archive : archives)
    {
        if (SFileOpenFileEx(archive, name, SFILE_OPEN_FROM_MPQ, out))
            return TRUE;
    }
    if (out)
        *out = nullptr;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
BOOL __stdcall closeFile(HANDLE f) { return SFileCloseFile(f); }
DWORD __stdcall fileSize(HANDLE f, DWORD* high) { return SFileGetFileSize(f, high); }
DWORD __stdcall seek(HANDLE f, LONG distance, LONG* high, DWORD method) { return SFileSetFilePointer(f, distance, high, method); }
BOOL __stdcall read(HANDLE f, void* buffer, DWORD size, DWORD* bytesRead, int, int, int)
{
    DWORD n = 0;
    const BOOL ok = SFileReadFile(f, buffer, size, &n, nullptr);
    if (bytesRead)
        *bytesRead = n;
    return ok;
}
BOOL __stdcall readFile(HANDLE f, void* buffer, DWORD size, DWORD* bytesRead, void*)
{
    return read(f, buffer, size, bytesRead, 0, 0, 0);
}

void* replacement(unsigned ordinal)
{
    switch (ordinal)
    {
    case 267: return reinterpret_cast<void*>(&openFile);
    case 253: return reinterpret_cast<void*>(&closeFile);
    case 265: return reinterpret_cast<void*>(&fileSize);
    case 271: return reinterpret_cast<void*>(&seek);
    case 289: return reinterpret_cast<void*>(&read);
    case 269: return reinterpret_cast<void*>(&readFile);
    }
    return nullptr;
}

// Rewrites the import address table entries a module has for those Storm ordinals. Returns how many.
int patchImports(HMODULE module)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int count = 0;
    if (!dir.VirtualAddress)
        return 0;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d)
    {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), "Storm.dll") != 0)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            void* target = replacement(IMAGE_ORDINAL(names->u1.Ordinal));
            if (!target)
                continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<DWORD_PTR>(target);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            ++count;
        }
    }
    return count;
}
} // namespace refstorm
#endif
