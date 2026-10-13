#pragma once

// Host configuration for the native x64 Fog. The original Fog.dll read the install and save locations from
// the registry and the command line; a standalone host supplies them here instead (see FogHost_Configure).
// All strings are copied, and any field left null keeps its default.
#include <cstdint>

struct FogHostConfig
{
	uint32_t structSize;                            // sizeof(FogHostConfig), for forward compatibility
	const char* installPath;                        // Folder holding the MPQs. Default: the host executable's folder
	const char* savePath;                           // Folder for character saves, created on demand. Default: <installPath>\Save
	const char* logDirectory;                       // Where the trace/assert logs go. Default: <installPath>\Logs
	int32_t isExpansion;                            // Value returned by FOG_IsExpansion (nonzero = Lord of Destruction). Default: 1
	void (*fatalHandler)(const char* szMessage);    // Called for FOG_DisplayHalt before the process is terminated
	int32_t haltTerminatesProcess;                  // Nonzero (default) = FOG_DisplayHalt ends the process after the handler
};

// Exported by name from Fog.dll (x64 only).
extern "C" __declspec(dllexport) void __cdecl FogHost_Configure(const FogHostConfig* pConfig);
