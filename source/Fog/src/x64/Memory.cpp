// Native x64 replacement for the memory functions of the original Fog.dll.
//
// The original ignored its memory pool arguments unless a global "pool mode" flag (FOG_SetMemoryPoolSystemMode)
// was set, which the game never did, so every allocation was an ordinary heap allocation. Pools are therefore
// opaque tokens here. Memory is always zero-filled, which is a safe superset of the original behaviour.
#include <Windows.h>

#include <Fog.h>

namespace
{
struct PoolSystem
{
	uint32_t nPools;
};

void* allocate(size_t nSize, const char* szFile, int nLine)
{
	void* pMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, nSize ? nSize : 1);
	if (!pMemory)
	{
		FOG_DisplayError(2, "Out of memory", szFile, nLine);
	}
	return pMemory;
}

void* reallocate(void* pMemory, size_t nSize, const char* szFile, int nLine)
{
	if (!pMemory)
	{
		return allocate(nSize, szFile, nLine);
	}

	void* pResult = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pMemory, nSize ? nSize : 1);
	if (!pResult)
	{
		FOG_DisplayError(2, "Out of memory", szFile, nLine);
	}
	return pResult;
}
}

void* __fastcall FOG_Alloc(int nSize, const char* szFile, int nLine, int n0)
{
	(void)n0;
	return allocate(size_t(nSize < 0 ? 0 : nSize), szFile, nLine);
}

void __fastcall FOG_Free(void* pFree, const char* szFile, int nLine, int n0)
{
	(void)szFile;
	(void)nLine;
	(void)n0;
	if (pFree)
	{
		HeapFree(GetProcessHeap(), 0, pFree);
	}
}

void* __fastcall FOG_Realloc(void* pMemory, int nSize, const char* szFile, int nLine, int n0)
{
	(void)n0;
	return reallocate(pMemory, size_t(nSize < 0 ? 0 : nSize), szFile, nLine);
}

void* __fastcall FOG_AllocPool(void* pMemPool, int nSize, const char* szFile, int nLine, int n0)
{
	(void)pMemPool;
	(void)n0;
	return allocate(size_t(nSize < 0 ? 0 : nSize), szFile, nLine);
}

void __fastcall FOG_FreePool(void* pMemPool, void* pFree, const char* szFile, int nLine, int n0)
{
	(void)pMemPool;
	(void)szFile;
	(void)nLine;
	(void)n0;
	if (pFree)
	{
		HeapFree(GetProcessHeap(), 0, pFree);
	}
}

void* __fastcall FOG_ReallocPool(void* pMemPool, void* pMemory, int nSize, const char* szFile, int nLine, int n0)
{
	(void)pMemPool;
	(void)n0;
	return reallocate(pMemory, size_t(nSize < 0 ? 0 : nSize), szFile, nLine);
}

void __cdecl FOG_CreateNewPoolSystem(void** pMemPoolSystem, const char* szName, uint32_t nPools, uint32_t nUnused)
{
	(void)szName;
	(void)nUnused;
	auto* pSystem = static_cast<PoolSystem*>(allocate(sizeof(PoolSystem), __FILE__, __LINE__));
	if (pSystem)
	{
		pSystem->nPools = nPools;
	}
	if (pMemPoolSystem)
	{
		*pMemPoolSystem = pSystem;
	}
}

void __cdecl FOG_DestroyMemoryPoolSystem(void* pMemoryPoolSystem)
{
	if (pMemoryPoolSystem)
	{
		HeapFree(GetProcessHeap(), 0, pMemoryPoolSystem);
	}
}

void __cdecl FOG_SetMemoryPoolSystemMode(void* pMemoryPoolSystem, BOOL bFreePoolAlloc)
{
	(void)pMemoryPoolSystem;
	(void)bFreePoolAlloc;
}

DWORD __cdecl FOG_GetMemoryUsage(void* pMemoryPoolSystem)
{
	(void)pMemoryPoolSystem;
	return 0;
}
