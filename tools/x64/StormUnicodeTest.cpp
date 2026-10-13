// Runs Storm's UTF-8 to UTF-16 converter (ordinal 904) over a fixed set of inputs and writes every result, so the
// original 32-bit Storm.dll and the native x64 one can be compared line by line.
//
// usage: StormUnicodeTest <Storm.dll> <output file>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Convert_t = int(__stdcall*)(wchar_t*, int, const char*, int, int*, int*);

namespace
{
uint32_t gSeed = 12345;
uint32_t next()
{
    gSeed = gSeed * 1664525u + 1013904223u;
    return gSeed >> 8;
}

FILE* out;
Convert_t convert;
int gCases;

// One call, on private copies of the buffers so a converter that reads or writes out of bounds shows up as a
// difference (the guard units around the destination must stay untouched).
void run(const std::vector<uint8_t>& src, int bufferCount, int srcCount, bool withOut, bool withConsumed)
{
    std::vector<uint8_t> source(src);
    source.resize(src.size() + 8, 0x41); // bytes after the input; an unbounded count (0x7FFFFFFF) reads on until a NUL
    source.push_back(0);
    std::vector<uint16_t> dest(size_t(bufferCount) + 8, 0xABCD);
    int units = -7, consumed = -7;
    const int result = convert(reinterpret_cast<wchar_t*>(dest.data()), bufferCount, reinterpret_cast<const char*>(source.data()), srcCount,
                               withOut ? &units : nullptr, withConsumed ? &consumed : nullptr);
    std::fprintf(out, "src=");
    for (uint8_t b : src)
        std::fprintf(out, "%02X", b);
    std::fprintf(out, " buf=%d cnt=%d -> ret=%d units=%d used=%d dst=", bufferCount, srcCount, result, units, consumed);
    for (uint16_t u : dest)
        std::fprintf(out, "%04X", u);
    std::fputc('\n', out);
    ++gCases;
}

void runAll(const std::vector<uint8_t>& src)
{
    const int len = int(src.size());
    for (int buffer : {0, 1, 2, 3, len, len + 1, len + 4})
    {
        for (int count : {len, len + 1, len - 1, 0x7FFFFFFF})
        {
            if (count < 0)
                continue;
            run(src, buffer, count, true, true);
        }
    }
    run(src, len + 2, len + 1, false, false);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return 2;
    HMODULE storm = LoadLibraryA(argv[1]);
    if (!storm)
    {
        std::printf("cannot load %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    convert = reinterpret_cast<Convert_t>(GetProcAddress(storm, MAKEINTRESOURCEA(904)));
    if (!convert)
    {
        std::printf("Storm has no ordinal 904\n");
        return 1;
    }
    out = std::fopen(argv[2], "w");

    // Every single byte, alone and followed by a NUL, and followed by two continuation bytes.
    for (int b = 0; b < 256; ++b)
    {
        runAll({uint8_t(b)});
        runAll({uint8_t(b), 0});
        runAll({uint8_t(b), 0x80, 0x80, 0x80, 0x80, 0x80, 0});
        runAll({uint8_t(b), 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0});
    }
    // Text as D2 uses it, with the NUL the callers include.
    runAll({'D', 'i', 'a', 'b', 'l', 'o', 0});
    runAll({'H', 0xC3, 0xA9, 'l', 'l', 'o', 0});
    runAll({0xE2, 0x82, 0xAC, 0});
    runAll({0xF0, 0x9F, 0x98, 0x80, 0});           // a code point that needs a surrogate pair
    runAll({0xF4, 0x8F, 0xBF, 0xBF, 0});           // U+10FFFF
    runAll({0xF4, 0x90, 0x80, 0x80, 0});           // above U+10FFFF
    runAll({0xED, 0xA0, 0x80, 0});                 // a surrogate encoded as UTF-8
    runAll({0xC0, 0x80, 0});                       // overlong NUL
    runAll({0xC3, 0xA9});                          // no terminator
    // Random byte strings, biased to lead and continuation bytes.
    for (int i = 0; i < 6000; ++i)
    {
        std::vector<uint8_t> s(next() % 14);
        for (auto& b : s)
        {
            switch (next() % 6)
            {
            case 0: b = uint8_t(next()); break;
            case 1: b = uint8_t(0x80 + next() % 0x40); break;
            case 2: b = uint8_t(0xC0 + next() % 0x40); break;
            case 3: b = uint8_t(0x20 + next() % 0x5F); break;
            case 4: b = 0; break;
            default: b = uint8_t(0xE0 + next() % 0x20); break;
            }
        }
        runAll(s);
    }
    std::fclose(out);
    std::printf("wrote %d cases to %s\n", gCases, argv[2]);
    return 0;
}
