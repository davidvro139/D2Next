#ifndef STORM_ERROR_HPP
#define STORM_ERROR_HPP

#include "storm/StormDLLDef.h"
#include <cstdint>

#if defined(WHOA_SYSTEM_WIN)
#include <WinError.h>
#endif

#if defined(WHOA_SYSTEM_MAC) || defined(WHOA_SYSTEM_LINUX)
#define ERROR_SUCCESS           0x0
#define ERROR_INVALID_PARAMETER 0x57
#endif

#if defined(NDEBUG)
#define STORM_ASSERT(x)                          \
    (void)0
#else
#define STORM_ASSERT(x)                          \
    if (!(x)) {                                  \
        SErrPrepareAppFatal(__FILE__, __LINE__); \
        SErrDisplayAppFatal(#x);                 \
    }                                            \
    (void)0
#endif

#define STORM_VALIDATE(x, y, ...)                \
    if (!(x)) {                                  \
        SErrSetLastError(y);                     \
        return __VA_ARGS__;                      \
    }                                            \
    (void)0


D2FUNC_DLL_NP(STORM, SErrDisplayError, int32_t, __stdcall, (uint32_t messageId, const char* filename, int32_t linenumber, const char* description, int32_t recoverable, uint32_t exitcode), 0x12020); //Storm.#461
D2FUNC_DLL_NP(STORM, SErrDisplayAppFatal, void, __declspec(noreturn), (char *Format, ...), 0x11f50);  //Storm.#561
D2FUNC_DLL_NP_X64(STORM, SErrDisplayErrorFmt, int32_t, __cdecl, (uint32_t messageId, const char* filename, int32_t linenumber, int32_t recoverable, uint32_t uExitCode, char* Format, ...), 0x12570);     //Storm.#562
D2FUNC_DLL_NP(STORM, SErrPrepareAppFatal, int32_t, __stdcall, (const char* filename, int32_t linenumber), 0x12710);                                                                                   //Storm.#564

D2FUNC_DLL_NP(STORM, SErrGetLastError, uint32_t, __cdecl, (), 0x126f0); //Storm.#463
D2FUNC_DLL_NP(STORM, SErrSetLastError, void, __stdcall, (uint32_t dwErrCode), 0x12940); //Storm.#465
#endif
