// On Windows StormLib is built with STORM_UNICODE, so its functions take wide-character paths. The functions are plain C
// symbols, so a mismatch would link fine and only fail at runtime; make StormLib.h see the same UNICODE setting it was
// built with. Elsewhere (Android, Linux, the web) it takes UTF-8 paths.
#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#endif

#include "Mpq.h"

#include "StormLibRename.h" // Must come before StormLib.h, see the comment in that header

#include <StormLib.h>

#ifdef _WIN32
static_assert(sizeof(TCHAR) == sizeof(wchar_t), "StormLib must be used with wide-character paths");
#else
static_assert(sizeof(TCHAR) == sizeof(char), "StormLib must be used with UTF-8 paths");
#endif

#include <algorithm>
#include <cerrno>
#include <mutex>
#include <string>
#include <vector>

namespace
{
struct Archive
{
	HANDLE handle;
	int priority;
	uint64_t order; // Opening sequence number, larger = opened later
};

std::mutex gMutex;
std::vector<Archive> gArchives; // Kept sorted, best match first
uint64_t gNextOrder = 0;

#ifndef _WIN32
std::basic_string<TCHAR> widen(const char* szText)
{
	// Archive paths come from the game and may use either kind of slash; this system's separator is '/'.
	std::string path = szText ? szText : "";
	std::replace(path.begin(), path.end(), '\\', '/');
	return path;
}
#else
std::basic_string<TCHAR> widen(const char* szText)
{
	// Archive paths come from the game as ANSI strings and may use either kind of slash.
	std::wstring wide;
	const int nLength = MultiByteToWideChar(CP_ACP, 0, szText, -1, nullptr, 0);
	if (nLength > 1)
	{
		wide.resize(size_t(nLength - 1));
		MultiByteToWideChar(CP_ACP, 0, szText, -1, wide.data(), nLength);
	}
	for (auto& c : wide)
	{
		if (c == L'/')
		{
			c = L'\\';
		}
	}
	return std::basic_string<TCHAR>(wide.begin(), wide.end());
}
#endif
}

namespace mpq
{
bool openArchive(const char* szPath, int nPriority, void** ppArchive)
{
	HANDLE handle = nullptr;
	if (!SFileOpenArchive(widen(szPath).c_str(), 0, MPQ_OPEN_READ_ONLY, &handle))
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(gMutex);
	gArchives.push_back({handle, nPriority, gNextOrder++});
	std::stable_sort(gArchives.begin(), gArchives.end(), [](const Archive& a, const Archive& b)
	{
		return a.priority != b.priority ? a.priority > b.priority : a.order > b.order;
	});

	if (ppArchive)
	{
		*ppArchive = handle;
	}
	return true;
}

bool closeArchive(void* pArchive)
{
	std::lock_guard<std::mutex> lock(gMutex);
	const auto it = std::find_if(gArchives.begin(), gArchives.end(), [&](const Archive& a) { return a.handle == pArchive; });
	if (it == gArchives.end())
	{
		return false;
	}
	gArchives.erase(it);
	return SFileCloseArchive(pArchive) != 0;
}

bool openFile(const char* szName, void** ppFile)
{
	HANDLE file = nullptr;
	{
		std::lock_guard<std::mutex> lock(gMutex);
		for (const Archive& archive : gArchives)
		{
			if (SFileOpenFileEx(archive.handle, szName, SFILE_OPEN_FROM_MPQ, &file))
			{
				*ppFile = file;
				return true;
			}
		}
	}

#ifdef _WIN32
	const char* szLocalName = szName;
#else
	const std::string localName = widen(szName);
	const char* szLocalName = localName.c_str();
#endif
	if (SFileOpenFileEx(nullptr, szLocalName, SFILE_OPEN_LOCAL_FILE, &file))
	{
		*ppFile = file;
		return true;
	}

#ifdef _WIN32
	SetLastError(ERROR_FILE_NOT_FOUND);
#else
	errno = ENOENT;
#endif
	return false;
}

bool closeFile(void* pFile)
{
	return SFileCloseFile(pFile) != 0;
}

bool readFile(void* pFile, void* pBuffer, uint32_t nBytesToRead, uint32_t* pnBytesRead)
{
	DWORD nRead = 0;
	const bool ok = SFileReadFile(pFile, pBuffer, nBytesToRead, &nRead, nullptr) != 0;
	if (pnBytesRead)
	{
		*pnBytesRead = nRead;
	}
	return ok;
}

uint32_t fileSize(void* pFile, uint32_t* pnSizeHigh)
{
	DWORD nHigh = 0;
	const DWORD nSize = SFileGetFileSize(pFile, &nHigh);
	if (pnSizeHigh)
	{
		*pnSizeHigh = nHigh;
	}
	return nSize;
}

uint32_t setFilePointer(void* pFile, int32_t nDistance, int32_t* pnDistanceHigh, uint32_t nMoveMethod)
{
	LONG nHigh = pnDistanceHigh ? *pnDistanceHigh : 0;
	const DWORD nResult = SFileSetFilePointer(pFile, nDistance, pnDistanceHigh ? &nHigh : nullptr, nMoveMethod);
	if (pnDistanceHigh)
	{
		*pnDistanceHigh = nHigh;
	}
	return nResult;
}

bool fileName(void* pFile, char* szBuffer, int nBufferSize)
{
	if (nBufferSize <= 0)
	{
		return false;
	}

	// Unlike the path functions, StormLib's SFileGetFileName always fills a narrow buffer of at least MAX_PATH.
	char name[MAX_PATH] = {};
	if (!SFileGetFileName(pFile, name))
	{
		szBuffer[0] = 0;
		return false;
	}

	int i = 0;
	for (; name[i] && i < nBufferSize - 1; ++i)
	{
		szBuffer[i] = name[i];
	}
	szBuffer[i] = 0;
	return true;
}
}
