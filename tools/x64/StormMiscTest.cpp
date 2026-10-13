// Exercises Storm's string and memory helpers (SStrCopy, SStrLen, SStrCmpI, SMemAlloc/Free/ReAlloc) and writes every
// result, so the original 32-bit Storm.dll and the native x64 one can be compared line by line.
//
// usage: StormMiscTest <Storm.dll> <output file>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using SStrCopy_t = size_t(__stdcall*)(char*, const char*, size_t);
using SStrLen_t = size_t(__stdcall*)(const char*);
using SStrCmpI_t = int32_t(__stdcall*)(const char*, const char*, size_t);
using SMemAlloc_t = void*(__stdcall*)(int, const char*, int, uint32_t);
using SMemFree_t = BOOL(__stdcall*)(void*, const char*, int, uint32_t);
using SMemReAlloc_t = void*(__stdcall*)(void*, int, const char*, int, uint32_t);

int main(int argc, char** argv)
{
    if (argc < 3)
        return 2;
    HMODULE storm = LoadLibraryA(argv[1]);
    if (!storm)
    {
        std::printf("cannot load %s\n", argv[1]);
        return 1;
    }
    auto get = [&](int ordinal) {
        FARPROC p = GetProcAddress(storm, MAKEINTRESOURCEA(ordinal));
        if (!p)
        {
            std::printf("missing ordinal %d\n", ordinal);
            std::exit(3);
        }
        return p;
    };
    auto sstrcopy = reinterpret_cast<SStrCopy_t>(get(501));
    auto sstrlen = reinterpret_cast<SStrLen_t>(get(506));
    auto sstrcmpi = reinterpret_cast<SStrCmpI_t>(get(509));
    auto smemalloc = reinterpret_cast<SMemAlloc_t>(get(401));
    auto smemfree = reinterpret_cast<SMemFree_t>(get(403));
    auto smemrealloc = reinterpret_cast<SMemReAlloc_t>(get(405));
    FILE* out = std::fopen(argv[2], "w");

    const std::vector<std::string> strings = {"", "a", "A", "abc", "ABC", "abd", "Abc", "abcd", "hello world", "HELLO WORLD", "hello worle", "\xE9t\xE9", "\xC9T\xC9",
                                              "path\to\file", "path/to/file", "x_y", "X_Y", "12345678901234567890"};
    const size_t sizes[] = {0, 1, 2, 3, 4, 5, 11, 12, 20, 21, 100, 0x7FFFFFFF};

    for (const auto& s : strings)
    {
        std::fprintf(out, "len '%s' = %zu\n", s.c_str(), sstrlen(s.c_str()));
        for (size_t size : sizes)
        {
            char dest[64];
            std::memset(dest, 0xAA, sizeof(dest));
            const size_t n = sstrcopy(dest, s.c_str(), size > 40 ? 40 : size); // never claim more than the buffer holds
            std::fprintf(out, "copy '%s' size=%zu -> %zu:", s.c_str(), size > 40 ? size_t(40) : size, n);
            for (int i = 0; i < 44; ++i)
                std::fprintf(out, " %02X", (unsigned char)dest[i]);
            std::fputc('\n', out);
        }
        if (!s.empty())
        {
            // A source of exactly the size that fits, and the huge size the game passes for "unbounded".
            char dest[64];
            std::memset(dest, 0xAA, sizeof(dest));
            const size_t n = sstrcopy(dest, s.c_str(), 0x7FFFFFFF);
            std::fprintf(out, "copy '%s' unbounded -> %zu '%s'\n", s.c_str(), n, dest);
        }
    }
    for (const auto& a : strings)
        for (const auto& b : strings)
            for (size_t count : sizes)
                std::fprintf(out, "cmpi '%s' '%s' %zu -> %d\n", a.c_str(), b.c_str(), count, sstrcmpi(a.c_str(), b.c_str(), count));

    // Memory: zeroing, preservation over a resize, and that freed blocks do not break later allocations.
    for (int size : {1, 7, 16, 100, 4096, 100000})
    {
        for (uint32_t flags : {0u, 8u})
        {
            auto* p = static_cast<uint8_t*>(smemalloc(size, "test", 1, flags));
            long long sum = 0;
            if (flags == 8)
                for (int i = 0; i < size; ++i)
                    sum += p[i];
            std::fprintf(out, "alloc %d flags=%u -> %s zeroSum=%lld\n", size, flags, p ? "ok" : "null", sum);
            for (int i = 0; i < size; ++i)
                p[i] = uint8_t(i * 7 + 1);
            auto* q = static_cast<uint8_t*>(smemrealloc(p, size * 2, "test", 2, 0));
            int intact = 0;
            for (int i = 0; i < size; ++i)
                intact += q[i] == uint8_t(i * 7 + 1);
            std::fprintf(out, "realloc %d -> %d intact=%d\n", size, size * 2, intact);
            std::fprintf(out, "free -> %d\n", smemfree(q, "test", 3, 0) ? 1 : 0);
        }
    }
    std::fclose(out);
    return 0;
}
