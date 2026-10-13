// Glue for D2GameHostTest (32-bit build): StormLib's headers cannot be included in the same translation unit as
// D2Game's (both declare the Storm error functions), so the StormLib side lives here.
#include "RefStorm.h"

bool refstormMount(const char* path)
{
    HANDLE h = nullptr;
    if (!SFileOpenArchive(path, 0, MPQ_OPEN_READ_ONLY, &h))
        return false;
    refstorm::archives.push_back(h);
    return true;
}

int refstormPatch(HMODULE module)
{
    return refstorm::patchImports(module);
}
