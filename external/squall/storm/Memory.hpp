#ifndef STORM_MEMORY_HPP
#define STORM_MEMORY_HPP

#include "storm/StormDLLDef.h"

#include <cstdint>
#include <cstdlib>

D2FUNC_DLL_NP_X64(STORM, SMemAlloc, void*, __stdcall, (size_t bytes, const char* logfilename, int logline, uint32_t flags), 0x1b130); //Storm.#401
void SMemFree(void* ptr);
D2FUNC_DLL_NP_X64(STORM, SMemFree, int32_t, __stdcall, (void* location, const char* logfilename, int logline, uint32_t flags), 0x1c2e0); //Storm.#403
D2FUNC_DLL_NP(STORM, SMemGetSize, size_t, __stdcall, (void* location, const char* logfilename, int logline), 0x1c4f0); //Storm.#404
D2FUNC_DLL_NP_X64(STORM, SMemReAlloc, void*, __stdcall, (void* location, int amount, const char* logfilename, int logline, uint32_t flags), 0x1ce40); //Storm.#405

#endif
