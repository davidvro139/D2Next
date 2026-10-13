// Differential test for the native x64 Fog.
//
// The same source is built twice: as x86 against the ORIGINAL 1.10f Fog.dll, and as x64 against the native
// Fog.dll in this repo. Each build calls Fog by ordinal with identical (pseudo-random but deterministic) inputs
// and writes every result to a text file. The two files must be byte-for-byte identical.
//
// usage: FogDiffTest <path to Fog.dll> <output text file> [extra DLL search directory]
// See tools/x64/Run-FogDiff.ps1, which builds both and compares them.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 16);
    }
};

HMODULE gFog;
template <typename T>
T fn(int ordinal)
{
    auto p = reinterpret_cast<T>(GetProcAddress(gFog, MAKEINTRESOURCEA(ordinal)));
    if (!p)
    {
        std::printf("missing ordinal %d\n", ordinal);
        std::exit(3);
    }
    return p;
}

FILE* out;
int gCases;
void line(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::vfprintf(out, fmt, a);
    va_end(a);
    std::fputc('\n', out);
    ++gCases;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: FogDiffTest <Fog.dll> <output file> [dll search dir]\n");
        return 2;
    }
    if (argc > 3)
        SetDllDirectoryA(argv[3]);
    gFog = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!gFog)
    {
        std::printf("cannot load %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    out = std::fopen(argv[2], "w");
    Rng rng;

    // ---- ComputeChecksum (10229) and PopCount (10230) over random buffers of assorted sizes
    auto checksum = fn<uint32_t(__stdcall*)(void*, size_t)>(10229);
    auto popcount = fn<uint32_t(__stdcall*)(void*, size_t)>(10230);
    for (int i = 0; i < 400; ++i)
    {
        uint8_t buf[600];
        const size_t n = rng.next() % 600;
        for (size_t k = 0; k < n; ++k)
            buf[k] = uint8_t(rng.next());
        if (i % 7 == 0)
            std::memset(buf, i % 3 ? 0xFF : 0x00, n);
        line("checksum n=%zu -> %08X", n, checksum(buf, n));
        line("popcount n=%zu -> %u", n, popcount(buf, n));
    }

    // ---- LeadingZeroesCount (10252), which is really "index of lowest set bit"
    auto lowbit = fn<uint32_t(__stdcall*)(uint32_t)>(10252);
    for (int i = 0; i < 32; ++i)
        line("lowbit %08X -> %08X", 1u << i, lowbit(1u << i));
    line("lowbit 0 -> %08X", lowbit(0));
    for (int i = 0; i < 2000; ++i)
    {
        uint32_t v = rng.next() << (rng.next() % 17);
        line("lowbit %08X -> %08X", v, lowbit(v));
    }

    // ---- ComputeStringCRC16 (10137)
    auto crc16 = fn<uint16_t(__stdcall*)(const char*)>(10137);
    const char* fixed[] = {"", "a", "A", "Rogue Encampment", "ROGUE ENCAMPMENT", "hello world", "Mixed_Case-123", "\x7F~", "abcdefghijklmnopqrstuvwxyz"};
    for (const char* s : fixed)
        line("crc16 '%s' -> %04X", s, crc16(s));
    for (int i = 0; i < 1000; ++i)
    {
        char s[40];
        const int n = 1 + rng.next() % 38;
        for (int k = 0; k < n; ++k)
            s[k] = char(1 + rng.next() % 126); // ASCII only: the original used the C locale
        s[n] = 0;
        line("crc16 %02X%02X%02X.. n=%d -> %04X", uint8_t(s[0]), uint8_t(s[n > 1 ? 1 : 0]), uint8_t(s[n > 2 ? 2 : 0]), n, crc16(s));
    }

    // ---- Sin/Cos lookup tables (10083 Cos, 10084 Sin): every index of a wide range, printed as raw float bits
    auto cosLut = fn<float(__stdcall*)(int16_t)>(10083);
    auto sinLut = fn<float(__stdcall*)(int16_t)>(10084);
    for (int i = -1100; i <= 1100; ++i)
    {
        const float c = cosLut(int16_t(i)), s = sinLut(int16_t(i));
        uint32_t cb, sb;
        std::memcpy(&cb, &c, 4);
        std::memcpy(&sb, &s, 4);
        line("lut %d cos=%08X sin=%08X", i, cb, sb);
    }
    for (int i : {32767, -32768, 0x1234, -0x1234})
    {
        const float c = cosLut(int16_t(i)), s = sinLut(int16_t(i));
        uint32_t cb, sb;
        std::memcpy(&cb, &c, 4);
        std::memcpy(&sb, &s, 4);
        line("lut %d cos=%08X sin=%08X", i, cb, sb);
    }

    // ---- 14-bit and 32-bit string encodings (10085..10088)
    auto dec14 = fn<unsigned(__stdcall*)(uint16_t*)>(10085);
    auto enc14 = fn<void(__stdcall*)(uint16_t*, uint32_t)>(10086);
    auto dec32 = fn<unsigned(__stdcall*)(uint32_t*)>(10087);
    auto enc32 = fn<void(__stdcall*)(uint32_t*, uint32_t)>(10088);
    for (uint32_t v = 0; v < 16384; v += (v < 300 ? 1 : 37))
    {
        uint16_t e = 0;
        enc14(&e, v);
        line("enc14 %u -> %04X dec=%u", v, e, dec14(&e));
    }
    for (int i = 0; i < 300; ++i)
    {
        uint16_t raw = uint16_t(rng.next());
        line("dec14 %04X -> %u", raw, dec14(&raw));
    }
    const uint32_t edge32[] = {0, 1, 254, 255, 256, 65024, 65025, 65026, 16581374, 16581375, 16581376, 0x7FFFFFFF, 0xFC05FBFF, 0xFC05FC00};
    for (uint32_t v : edge32)
    {
        uint32_t e = 0;
        enc32(&e, v);
        line("enc32 %u -> %08X dec=%u", v, e, dec32(&e));
    }
    for (int i = 0; i < 1500; ++i)
    {
        const uint32_t v = rng.next() % 0xFC05FC00u;
        uint32_t e = 0;
        enc32(&e, v);
        line("enc32 %u -> %08X dec=%u", v, e, dec32(&e));
    }
    for (int i = 0; i < 300; ++i)
    {
        uint32_t raw = rng.next() | 0x01010101u;
        line("dec32 %08X -> %u", raw, dec32(&raw));
    }

    // ---- csprintf (10018): formats into the buffer and returns it
    auto csprintf = fn<const char*(__cdecl*)(char*, const char*, ...)>(10018);
    for (int i = 0; i < 100; ++i)
    {
        char buf[128];
        const char* r = csprintf(buf, "%s|%d|%05X|%c", "text", int(rng.next() % 100000) - 50000, rng.next() & 0xFFFFF, 'a' + int(rng.next() % 26));
        line("csprintf '%s' same_buffer=%d", buf, r == buf);
    }

    // ---- Linkers (10211 Alloc, 10212 Free, 10213 GetLinkIndex, 10214 GetStringFromLinkIndex, 10215 add code,
    //      10216 add name, 10217 GetRowFromTxt, 10255 name of row)
    auto allocLinker = fn<void*(__stdcall*)(const char*, int)>(10211);
    auto freeLinker = fn<void(__stdcall*)(void*)>(10212);
    auto getLinkIndex = fn<int(__stdcall*)(void*, uint32_t, BOOL)>(10213);
    auto getString = fn<int(__stdcall*)(void*, int, char*)>(10214);
    auto addCode = fn<int(__stdcall*)(void*, int)>(10215);
    auto addName = fn<int(__stdcall*)(void*, const char*)>(10216);
    auto getRow = fn<int(__stdcall*)(void*, char*, int)>(10217);
    auto nameOf = fn<char*(__stdcall*)(void*, int, int)>(10255);

    // Code table: 300 codes over a tiny alphabet, so there are many duplicates, and more than 64 entries, so the table grows.
    {
        void* l = allocLinker("test", 1);
        const char alpha[] = {'a', 'b', ' ', 'c', 'Z'};
        std::vector<uint32_t> added;
        for (int i = 0; i < 300; ++i)
        {
            uint32_t code = 0;
            for (int k = 0; k < 4; ++k)
                code |= uint32_t(uint8_t(alpha[rng.next() % 5])) << (8 * k);
            if (i % 5 == 0)
                code = 0x20202061u + (rng.next() % 4); // codes near the printable limit
            added.push_back(code);
            line("addCode %08X -> %d", code, addCode(l, int(code)));
        }
        for (uint32_t code : added)
            line("getLinkIndex %08X -> %d", code, getLinkIndex(l, code, 0));
        for (int i = 0; i < 200; ++i)
        {
            const uint32_t code = rng.next() | (rng.next() << 16);
            line("getLinkIndex(absent) %08X -> %d", code, getLinkIndex(l, code, i & 1));
        }
        for (int idx = -2; idx < 305; ++idx)
        {
            char s[16];
            std::memset(s, 0x7F, sizeof(s));
            const int r = getString(l, idx, s);
            char shown[24] = "";
            if (r)
                std::snprintf(shown, sizeof(shown), "'%s' tail=%02X", s, uint8_t(s[4]));
            line("getString %d -> %d %s", idx, r, shown);
        }
        freeLinker(l);
    }

    // Name tree
    {
        void* l = allocLinker("test", 2);
        const char* fixedNames[] = {"Amazon", "amazon", "AMAZON", "Sorceress", "necromancer", "", "*skip", "a", "B",
                                    "This name is definitely longer than thirty-one characters", "ThirtyOneCharactersExactlyHere!", "Zed", "zed "};
        for (const char* n : fixedNames)
            line("addName '%s' -> %d", n, addName(l, n));
        for (int i = 0; i < 400; ++i)
        {
            char n[12];
            const int len = 1 + rng.next() % 5;
            for (int k = 0; k < len; ++k)
                n[k] = char((rng.next() & 1 ? 'a' : 'A') + rng.next() % 4);
            n[len] = 0;
            line("addName '%s' -> %d", n, addName(l, n));
        }
        for (const char* n : fixedNames)
        {
            char buf[80];
            std::strcpy(buf, n);
            line("getRow '%s' -> %d", n, getRow(l, buf, 0));
            line("getRow(log) '%s' -> %d", n, getRow(l, buf, 1));
        }
        for (const char* n : {"SORCERESS", "sorceress", "Necromancer", "missing", "*star", "ZED", "Zed ", "AmAzOn"})
        {
            char buf[80];
            std::strcpy(buf, n);
            line("getRow '%s' -> %d", n, getRow(l, buf, 0));
        }
        for (int id = -3; id < 425; ++id)
            line("nameOf %d -> '%s'", id, nameOf(l, id, id & 1));
        freeLinker(l);
    }

    // Empty and null linkers
    {
        void* l = allocLinker("test", 3);
        char buf[8] = "x";
        line("empty getRow -> %d", getRow(l, buf, 1));
        line("empty nameOf -> '%s'", nameOf(l, 0, 1));
        line("empty getLinkIndex -> %d", getLinkIndex(l, 0x41414141u, 1));
        freeLinker(l);
        line("null nameOf -> '%s'", nameOf(nullptr, 5, 1)); // (the original dereferences a null linker in GetRowFromTxt, so that is not tested)
    }

    std::fclose(out);
    std::printf("%s: %d cases written\n", argv[1], gCases);
    return 0;
}
