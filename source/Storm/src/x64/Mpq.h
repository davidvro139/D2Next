#pragma once

// MPQ archive registry for the native x64 Storm, backed by StormLib.
// This header deliberately has no Storm/StormLib types: StormLib.h and D2MOO's Storm.h both declare SFile*
// functions and cannot be included in the same translation unit.
#include <cstdint>

namespace mpq
{
// Opens an archive and adds it to the search list. A higher priority wins; among equal priorities the archive
// opened last wins, which is how patches override the base data.
bool openArchive(const char* szPath, int nPriority, void** ppArchive);
bool closeArchive(void* pArchive);

// Opens a file from the highest-priority archive that contains it, then from disk. Sets the Windows last error
// to ERROR_FILE_NOT_FOUND when the file exists nowhere.
bool openFile(const char* szName, void** ppFile);
bool closeFile(void* pFile);

bool readFile(void* pFile, void* pBuffer, uint32_t nBytesToRead, uint32_t* pnBytesRead);
uint32_t fileSize(void* pFile, uint32_t* pnSizeHigh);
uint32_t setFilePointer(void* pFile, int32_t nDistance, int32_t* pnDistanceHigh, uint32_t nMoveMethod);
bool fileName(void* pFile, char* szBuffer, int nBufferSize);
}
