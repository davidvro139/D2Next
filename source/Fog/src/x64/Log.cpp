// Native x64 replacements for the logging and error reporting half of the original Fog.dll.
#include <Windows.h>

#include <Fog.h>

#include "FogState.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fogx64
{
State& state()
{
	static State instance;
	return instance;
}

std::string withTrailingBackslash(std::string path)
{
	if (!path.empty() && path.back() != '\\' && path.back() != '/')
	{
#ifdef _WIN32
		path.push_back('\\');
#else
		path.push_back('/'); // elsewhere a backslash is part of a file name
#endif
	}
	return path;
}

void resolveDefaultsLocked(State& s)
{
	if (s.resolved)
	{
		return;
	}

	if (s.installPath.empty())
	{
		char exePath[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exePath, MAX_PATH);
		s.installPath = std::filesystem::path(exePath).parent_path().string();
	}
	s.installPath = withTrailingBackslash(s.installPath);

	if (s.savePath.empty())
	{
		s.savePath = s.installPath + "Save";
	}
	s.savePath = withTrailingBackslash(s.savePath);

	if (s.logDirectory.empty())
	{
		s.logDirectory = s.installPath + "Logs";
	}
	s.logDirectory = withTrailingBackslash(s.logDirectory);
	s.resolved = true;
}

void writeLog(const char* fileSubName, const char* text, bool decorate)
{
	if (!text)
	{
		return;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);

	std::lock_guard<std::mutex> lock(state().mutex);
	resolveDefaultsLocked(state());

	// Default log: D2<prefix><date>.txt. Named logs: D2<name><date>.txt, as the original did.
	char fileName[MAX_PATH];
	sprintf_s(fileName, "D2%s%04d%02d%02d.txt", fileSubName ? fileSubName : state().logPrefix.c_str(), now.wYear, now.wMonth, now.wDay);

	std::error_code ignored;
	std::filesystem::create_directories(state().logDirectory, ignored);
	std::ofstream file(state().logDirectory + fileName, std::ios::app);
	if (!file)
	{
		return;
	}

	if (decorate)
	{
		char stamp[32];
		sprintf_s(stamp, "%02d:%02d:%02d ", now.wHour, now.wMinute, now.wSecond);
		file << stamp << text << '\n';
	}
	else
	{
		file << text;
	}
}
}

extern "C" __declspec(dllexport) void __cdecl FogHost_Configure(const FogHostConfig* pConfig)
{
	if (!pConfig || pConfig->structSize < sizeof(uint32_t))
	{
		return;
	}

	auto& s = fogx64::state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (pConfig->installPath) s.installPath = pConfig->installPath;
	if (pConfig->savePath) s.savePath = pConfig->savePath;
	if (pConfig->logDirectory) s.logDirectory = pConfig->logDirectory;
	s.isExpansion = pConfig->isExpansion;
	s.fatalHandler = pConfig->fatalHandler;
	s.haltTerminatesProcess = pConfig->haltTerminatesProcess != 0;
	s.resolved = false;
	fogx64::resolveDefaultsLocked(s);
}

namespace
{
void report(const char* kind, const char* szMsg, const char* szFile, int nLine)
{
	char line[1024];
	sprintf_s(line, "%s: %s (%s:%d)", kind, szMsg ? szMsg : "(no message)", szFile ? szFile : "?", nLine);
	OutputDebugStringA(line);
	OutputDebugStringA("\n");
#ifndef _WIN32
	// No debugger output here: errors and halts (not the routine warnings) go to the program's own log too.
	if (std::strncmp(kind, "WARNING", 7) != 0)
	{
		std::fprintf(stderr, "%s\n", line);
	}
#endif
	fogx64::writeLog(nullptr, line, true);
}
}

const char* __cdecl FOG_csprintf(char* szDest, const char* szFormat, ...)
{
	va_list args;
	va_start(args, szFormat);
	vsprintf(szDest, szFormat, args); // Unbounded on purpose: the original API has no size argument
	va_end(args);
	return szDest;
}

void __fastcall FOG_InitErrorMgr(const char* szProgramName, D2ExceptionCallback pExceptionCallback, const char* szVersion, BOOL bLogToFile)
{
	(void)pExceptionCallback;
	(void)bLogToFile;
	if (szProgramName)
	{
		std::lock_guard<std::mutex> lock(fogx64::state().mutex);
		fogx64::state().logPrefix = szProgramName;
	}
	if (szVersion)
	{
		fogx64::writeLog(nullptr, szVersion, true);
	}
}

int __fastcall FOG_SetLogPrefix(const char* szLogPrefix)
{
	if (szLogPrefix)
	{
		std::lock_guard<std::mutex> lock(fogx64::state().mutex);
		fogx64::state().logPrefix = szLogPrefix;
	}
	return 1;
}

void __cdecl FOG_DisplayAssert(const char* szMsg, const char* szFile, int nLine)
{
	report("ASSERT", szMsg, szFile, nLine);
}

void __cdecl FOG_DisplayWarning(const char* szMsg, const char* szFile, int nLine)
{
	report("WARNING", szMsg, szFile, nLine);
}

void __cdecl FOG_DisplayError(int nCategory, const char* szMsg, const char* szFile, int nLine)
{
	char kind[32];
	sprintf_s(kind, "ERROR(%d)", nCategory);
	report(kind, szMsg, szFile, nLine);
}

void __cdecl FOG_DisplayHalt(const char* szMsg, const char* szFile, int nLine)
{
	report("HALT", szMsg, szFile, nLine);

	void (*handler)(const char*);
	bool terminate;
	{
		std::lock_guard<std::mutex> lock(fogx64::state().mutex);
		handler = fogx64::state().fatalHandler;
		terminate = fogx64::state().haltTerminatesProcess;
	}

	if (handler)
	{
		handler(szMsg ? szMsg : "(no message)");
	}
	if (terminate)
	{
		TerminateProcess(GetCurrentProcess(), 1);
	}
}

void __cdecl FOG_Trace(const char* szFormat, ...)
{
	if (!szFormat)
	{
		return;
	}
	char text[2048];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(text, sizeof(text), szFormat, args);
	va_end(args);
	fogx64::writeLog(nullptr, text, true);
}

void __cdecl FOG_TraceF(const char* pFileSubName, const char* szFormat, ...)
{
	if (!szFormat)
	{
		return;
	}
	char text[2048];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(text, sizeof(text), szFormat, args);
	va_end(args);
	fogx64::writeLog(pFileSubName, text, true);
}

void __cdecl FOG_TraceAppend(const char* szFormat, ...)
{
	if (!szFormat)
	{
		return;
	}
	char text[2048];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(text, sizeof(text), szFormat, args);
	va_end(args);
	fogx64::writeLog(nullptr, text, false);
}

BOOL __cdecl FOG_IsHandlingError()
{
	return FALSE;
}

void __fastcall FOG_10082_Noop()
{
}
