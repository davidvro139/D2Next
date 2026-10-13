#ifndef STORM_STRING_HPP
#define STORM_STRING_HPP

#include "storm/StormDLLDef.h"

#include <cstdint>
#include <cstdlib>

#define STORM_MAX_PATH 260
#define STORM_MAX_STR 0x7FFFFFFF

D2FUNC_DLL_NP_X64(STORM, SStrCopy, size_t, __stdcall, (char* dest, const char* source, size_t destsize), 0x2a120); //Storm.#501

D2FUNC_DLL_NP(STORM, SStrTokenize, void, __stdcall, (const char** string, char* buffer, size_t bufferchars, const char* delimiter, int32_t* quoted), 0x2abe0); //Storm.#504

D2FUNC_DLL_NP_X64(STORM, SStrLen, size_t, __stdcall, (const char* string), 0x2a330); //Storm.#506

#define SStrDupA SStrDup
D2FUNC_DLL_NP(STORM, SStrDup, void*, __stdcall, (int a1), 0x2a260); //Storm.#507

D2FUNC_DLL_NP(STORM, SStrCmp, int32_t, __stdcall, (const char* Str1, const char* Str2, size_t MaxCount), 0x2a0a0); //Storm.#508
D2FUNC_DLL_NP_X64(STORM, SStrCmpI, int32_t, __stdcall, (const char* Str1, const char* Str, size_t MaxCount), 0x2a0e0); //Storm.#509

D2FUNC_DLL_NP(STORM, SStrUpper, char*, __stdcall, (char* Str), 0x2b240); //Storm.#510

D2FUNC_DLL_NP(STORM, SStrChr, const char*, __stdcall, (const char* string, char c), 0x29fe0); //Storm.#571
D2FUNC_DLL_NP(STORM, SStrChrR, const char*, __stdcall, (const char* string, char c), 0x2a020); //Storm.#572

char* SStrChr(char* string, char search);
char* SStrChrR(char* string, char search);

D2FUNC_DLL_NP(STORM, SStrLower, int, __stdcall, (char* Str), 0x2b250); //Storm.#579
D2FUNC_DLL_NP(STORM, SStrPrintf, int, __cdecl, (char* DstBuf, size_t MaxCount, const char* Format, ...), 0x2a570); //Storm.#578

D2FUNC_DLL_NP(STORM, SStrToFloat, float, __stdcall, (const char* string), 0x2a830);    //Storm.#574
D2FUNC_DLL_NP(STORM, SStrToInt, int32_t, __stdcall, (const char* string), 0x2aa10); //Storm.#575

uint32_t SStrHashHT(const char* string);
uint32_t SStrPack(char* dest, const char* source, uint32_t destsize);


const char* SStrStr(const char* string, const char* search);

#endif
