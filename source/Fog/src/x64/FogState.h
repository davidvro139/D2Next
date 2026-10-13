#pragma once

// Shared state of the native x64 Fog. Internal to the Fog module.
#include "FogHost.h"

#include <mutex>
#include <string>

namespace fogx64
{
struct State
{
	std::mutex mutex;                    // Guards the strings below and log file writes
	std::string installPath;             // Always ends with a backslash once resolved
	std::string savePath;                // Always ends with a backslash once resolved
	std::string logDirectory;
	std::string logPrefix = "Fog";
	int32_t isExpansion = 1;
	void (*fatalHandler)(const char*) = nullptr;
	bool haltTerminatesProcess = true;
	bool resolved = false;               // Defaults for unset paths have been filled in
};

State& state();

// Fills in default paths on first use. Caller must hold state().mutex.
void resolveDefaultsLocked(State& s);

// Appends one line to a log file. fileSubName may be null for the default log file. When decorate is
// true the line gets a timestamp prefix and a trailing newline (FOG_Trace), otherwise it is written as is.
void writeLog(const char* fileSubName, const char* text, bool decorate);

std::string withTrailingBackslash(std::string path);
}
