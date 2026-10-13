// Native x64 replacements for the small Storm helpers D2Common, D2Game and D2Net call: strings, memory and
// error reporting. Checked against the original 1.10f Storm.dll by tools/x64/StormMiscTest (Run-StormMiscDiff.ps1).
#include <Windows.h>

#include <Storm.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
// Storm's SMemAlloc flag: zero the block. The other flags only steer its own bookkeeping.
constexpr uint32_t kZeroMemory = 0x8;

char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c + 0x20) : c;
}
}

void* __stdcall SMemAlloc(size_t amount, const char* szFile, int nLine, uint32_t nFlags)
{
	(void)szFile;
	(void)nLine;
	return HeapAlloc(GetProcessHeap(), (nFlags & kZeroMemory) ? HEAP_ZERO_MEMORY : 0, size_t(amount ? amount : 1));
}

int32_t __stdcall SMemFree(void* pLocation, const char* szFile, int nLine, uint32_t nFlags)
{
	(void)szFile;
	(void)nLine;
	(void)nFlags;
	return pLocation ? int32_t(HeapFree(GetProcessHeap(), 0, pLocation)) : 1;
}

void* __stdcall SMemReAlloc(void* pLocation, int amount, const char* szFile, int nLine, uint32_t nFlags)
{
	(void)szFile;
	(void)nLine;
	const DWORD dwFlags = (nFlags & kZeroMemory) ? HEAP_ZERO_MEMORY : 0;
	if (!pLocation)
	{
		return HeapAlloc(GetProcessHeap(), dwFlags, size_t(amount > 0 ? amount : 1));
	}
	return HeapReAlloc(GetProcessHeap(), dwFlags, pLocation, size_t(amount > 0 ? amount : 1));
}

// Copies at most nDestSize - 1 characters and always terminates when there is room. Returns the characters copied.
size_t __stdcall SStrCopy(char* szDest, const char* szSource, size_t nDestSize)
{
	if (nDestSize == 0)
	{
		return 0;
	}
	size_t nCopied = 0;
	while (nCopied + 1 < nDestSize && szSource[nCopied])
	{
		szDest[nCopied] = szSource[nCopied];
		++nCopied;
	}
	szDest[nCopied] = 0;
	return nCopied;
}

size_t __stdcall SStrLen(const char* szString)
{
	return strlen(szString);
}

// Case-insensitive comparison of at most nMaxCount characters: -1, 0 or 1.
int32_t __stdcall SStrCmpI(const char* szStr1, const char* szStr2, size_t nMaxCount)
{
	for (size_t i = 0; i < nMaxCount; ++i)
	{
		const unsigned char c1 = (unsigned char)lower(szStr1[i]);
		const unsigned char c2 = (unsigned char)lower(szStr2[i]);
		if (c1 != c2)
		{
			return c1 < c2 ? -1 : 1;
		}
		if (!c1)
		{
			break;
		}
	}
	return 0;
}

// The registry values Storm keeps under HKEY_CURRENT_USER\Software\Blizzard Entertainment\<key>. Only numbers.
BOOL __stdcall SRegLoadValue(const char* szKeyName, const char* szValueName, uint32_t nFlags, DWORD* pValue)
{
	(void)nFlags;
	char szPath[MAX_PATH];
	sprintf_s(szPath, "Software\\Blizzard Entertainment\\%s", szKeyName);
	DWORD nValue = 0, nSize = sizeof(nValue), nType = 0;
	if (RegGetValueA(HKEY_CURRENT_USER, szPath, szValueName, RRF_RT_REG_DWORD, &nType, &nValue, &nSize) != ERROR_SUCCESS)
	{
		return FALSE;
	}
	if (pValue)
	{
		*pValue = nValue;
	}
	return TRUE;
}

// Reports an error the way a headless host wants it: on standard error, and ends the process when it is not
// recoverable. The original shows a dialog.
int32_t __cdecl SErrDisplayErrorFmt(uint32_t dwMessageId, const char* szFile, int32_t nLine, int32_t bRecoverable, uint32_t nExitCode, char* szFormat, ...)
{
	char szMessage[1024];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(szMessage, sizeof(szMessage), szFormat ? szFormat : "", args);
	va_end(args);
	fprintf(stderr, "Storm error %lu at %s(%d): %s\n", dwMessageId, szFile ? szFile : "?", nLine, szMessage);
	if (!bRecoverable)
	{
		ExitProcess(nExitCode);
	}
	return 0;
}
