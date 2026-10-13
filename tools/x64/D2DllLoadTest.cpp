// Loads the x64 D2MOO DLLs and checks that every export declared in their .def files resolves by ordinal.
//
// This proves that the DLLs link, load with their dependencies, and export what the .def files promise. It
// does NOT prove that the code behaves correctly: several Fog/D2CMP/D2Lang functions that D2Common and
// D2Game call are still stubs (see BUILD-X64.md).
//
// usage: D2DllLoadTest <directory containing the DLLs> <D2MOO source directory> [ordinals version, default 1.10f]
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Exports are listed as "  Name   @1234 [NONAME]". Skip comments (';'), C++ mangled names ('?', which only
// exist for x86) and the "UnimplementedOrdinal" placeholders, which are markers and not real exports.
static std::vector<int> ordinalsIn(const fs::path& defFile)
{
    std::vector<int> ordinals;
    std::ifstream file(defFile);
    std::string line;
    while (std::getline(file, line))
    {
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == ';' || line[start] == '?')
            continue;
        if (line.find("UnimplementedOrdinal") != std::string::npos)
            continue;
        const size_t at = line.find('@');
        if (at == std::string::npos)
            continue;
        if (const int ordinal = std::atoi(line.c_str() + at + 1))
            ordinals.push_back(ordinal);
    }
    return ordinals;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: D2DllLoadTest <dll directory> <D2MOO source directory> [version]\n");
        return 2;
    }

    const fs::path dllDir = fs::absolute(argv[1]);
    const fs::path sourceDir = argv[2];
    const std::string version = argc > 3 ? argv[3] : "1.10f";
    SetDllDirectoryW(dllDir.c_str());

    int failures = 0;
    // Dependencies first, so a load failure points at the module that is actually broken.
    for (const char* name : {"Fog", "Storm", "D2CMP", "D2Lang", "D2Net", "D2Common", "D2Game"})
    {
        const fs::path dll = dllDir / (std::string(name) + ".dll");
        HMODULE module = LoadLibraryW(dll.c_str());
        if (!module)
        {
            std::printf("%-9s FAILED to load (Windows error %lu)\n", name, GetLastError());
            ++failures;
            continue;
        }

        const fs::path defFile = sourceDir / name / "definitions" / (std::string(name) + "." + version + ".def");
        const auto ordinals = ordinalsIn(defFile);
        if (ordinals.empty())
        {
            // A missing or unreadable .def would otherwise let the module pass with nothing checked.
            std::printf("%-9s FAILED: no ordinals read from %s\n", name, defFile.string().c_str());
            ++failures;
            continue;
        }
        int unresolved = 0;
        for (const int ordinal : ordinals)
        {
            if (!GetProcAddress(module, MAKEINTRESOURCEA(ordinal)))
            {
                std::printf("  %s: ordinal %d does not resolve\n", name, ordinal);
                ++unresolved;
            }
        }
        std::printf("%-9s loaded | %4zu ordinals declared | %d unresolved\n", name, ordinals.size(), unresolved);
        failures += unresolved;
    }

    if (failures)
    {
        std::printf("FAIL: %d problem(s)\n", failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
