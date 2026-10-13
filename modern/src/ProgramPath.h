#pragma once
#include <filesystem>
namespace d2 {
// The folder the program's files sit beside (MPQ, Save, logs): the executable's folder on Windows and Linux. On Android
// there is no such folder, so startup names one (the app's storage folder, where the player copies the game files).
std::filesystem::path programDirectory();
void setProgramDirectory(const std::filesystem::path& directory);
// An environment variable as a path (test hooks), empty when not set.
std::filesystem::path environmentPath(const char* name);
}
namespace d2 {
// A game file in a folder, matched without regard to case (systems other than Windows tell "D2DATA.MPQ" from
// "d2data.mpq"); the plain name when there is no such file.
std::filesystem::path gameFile(const std::filesystem::path& directory,const std::filesystem::path& name);
}
namespace d2 {
// Sets an environment variable for this process (test hooks the game libraries read).
void setEnvironment(const char* name,const char* value);
}
