// The Storm file API (as declared in Storm.h) for x64, on top of the StormLib-backed archive registry.
#include <Windows.h>

#include <D2Dll.h>
#include <Storm.h>

#include "Mpq.h"

BOOL __stdcall SFileOpenArchive(const char* szName, int nPriority, int nFlags, HSARCHIVE* phArchive)
{
	(void)nFlags; // Storm's flag bits only selected CD-ROM/read-only variants; archives are always opened read-only here
	void* pArchive = nullptr;
	if (!mpq::openArchive(szName, nPriority, &pArchive))
	{
		return FALSE;
	}
	if (phArchive)
	{
		*phArchive = static_cast<HSARCHIVE>(pArchive);
	}
	return TRUE;
}

BOOL __stdcall SFileCloseArchive(HSARCHIVE hArchive)
{
	return mpq::closeArchive(hArchive);
}

BOOL __stdcall SFileOpenFile(const char* szName, HSFILE* phFile)
{
	void* pFile = nullptr;
	if (!mpq::openFile(szName, &pFile))
	{
		return FALSE;
	}
	*phFile = static_cast<HSFILE>(pFile);
	return TRUE;
}

BOOL __stdcall SFileCloseFile(HSFILE hFile)
{
	return mpq::closeFile(hFile);
}

BOOL __stdcall SFileReadFile(HSFILE hFile, void* pBuffer, DWORD nBytesToRead, DWORD* pnBytesRead, void* pOverlappedOrReserved)
{
	(void)pOverlappedOrReserved; // Asynchronous reads are not supported
	uint32_t nRead = 0;
	const bool ok = mpq::readFile(hFile, pBuffer, nBytesToRead, &nRead);
	if (pnBytesRead)
	{
		*pnBytesRead = nRead;
	}
	return ok;
}

DWORD __stdcall SFileGetFileSize(HSFILE hFile, LPDWORD lpFileSizeHigh)
{
	uint32_t nHigh = 0;
	const uint32_t nSize = mpq::fileSize(hFile, &nHigh);
	if (lpFileSizeHigh)
	{
		*lpFileSizeHigh = nHigh;
	}
	return nSize;
}

DWORD __stdcall SFileSetFilePointer(HSFILE hFile, LONG lDistanceToMove, LONG* lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	static_assert(sizeof(LONG) == sizeof(int32_t), "LONG and int32_t must have the same size");
	return mpq::setFilePointer(hFile, lDistanceToMove, reinterpret_cast<int32_t*>(lpDistanceToMoveHigh), dwMoveMethod);
}

BOOL __stdcall SFileGetFileName(HSFILE hFile, char* buffer, int length)
{
	return mpq::fileName(hFile, buffer, length);
}
