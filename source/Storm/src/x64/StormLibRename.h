#pragma once

// StormLib is linked statically into the x64 Storm.dll. Fourteen of its C function names are identical to
// functions D2MOO's Storm exports (listed in Storm.1.10f.def by undecorated name). The linker matches those .def
// entries to StormLib's C symbols first, so the exports would point at the wrong functions and D2MOO's own C++
// versions could no longer be imported.
//
// This header renames StormLib's copies. It is force-included (/FI) into every StormLib source file and included
// by Mpq.cpp before <StormLib.h>, so declarations, definitions and calls all agree.
#define SFileCloseArchive StormLib_SFileCloseArchive
#define SFileCloseFile StormLib_SFileCloseFile
#define SFileGetFileArchive StormLib_SFileGetFileArchive
#define SFileGetFileSize StormLib_SFileGetFileSize
#define SFileOpenArchive StormLib_SFileOpenArchive
#define SFileOpenFileEx StormLib_SFileOpenFileEx
#define SFileReadFile StormLib_SFileReadFile
#define SFileSetFilePointer StormLib_SFileSetFilePointer
#define SFileSetLocale StormLib_SFileSetLocale
#define SFileGetFileName StormLib_SFileGetFileName
#define SErrGetLastError StormLib_SErrGetLastError
#define SErrSetLastError StormLib_SErrSetLastError
#define SCompCompress StormLib_SCompCompress
#define SCompDecompress StormLib_SCompDecompress
