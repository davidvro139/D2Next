// Native x64 replacement for Fog's file functions. As in the original DLL these are thin wrappers over
// Storm, which owns the list of open MPQ archives (see SFileOpenArchive in the Storm module).
#include <Windows.h>

#include <Fog.h>
#include <Storm.h>

BOOL __fastcall FOG_MPQSetConfig(int dwDirectAccessFlags, int bEnableSeekOptimization)
{
	(void)dwDirectAccessFlags;      // The original configured Storm's direct-file access and seek optimisation.
	(void)bEnableSeekOptimization;  // StormLib handles both itself.
	return TRUE;
}

BOOL __fastcall FOG_FOpenFile(const char* szFile, HSFILE* pFileHandle)
{
	return SFileOpenFile(szFile, pFileHandle);
}

void __fastcall FOG_FCloseFile(HSFILE pFile)
{
	SFileCloseFile(pFile);
}

// The three trailing arguments were passed through to Storm's read function and are unused by the game.
BOOL __fastcall FOG_FReadFile(HSFILE pFile, void* pBuffer, size_t nSize, int* nBytesRead, uint32_t, uint32_t, uint32_t)
{
	DWORD nRead = 0;
	const BOOL bResult = SFileReadFile(pFile, pBuffer, static_cast<DWORD>(nSize), &nRead, nullptr);
	if (nBytesRead)
	{
		*nBytesRead = static_cast<int>(nRead);
	}
	return bResult;
}

uint32_t __fastcall FOG_FGetFileSize(HSFILE pFileHandle, uint32_t* lpFileSizeHigh)
{
	return SFileGetFileSize(pFileHandle, reinterpret_cast<LPDWORD>(lpFileSizeHigh));
}

size_t __fastcall FOG_FSetFilePointer(HSFILE hFile, int32_t lDistanceToMove, int32_t* lpDistanceToMoveHigh, uint32_t dwMoveMethod)
{
	// Storm returns a 32-bit position (0xFFFFFFFF on failure); keep it unsigned 32-bit so callers comparing
	// against INVALID_SET_FILE_POINTER behave identically on x64.
	return static_cast<uint32_t>(SFileSetFilePointer(hFile, lDistanceToMove, reinterpret_cast<LONG*>(lpDistanceToMoveHigh), dwMoveMethod));
}

BOOL __fastcall FOG_UseDirect()
{
	return FALSE;
}
