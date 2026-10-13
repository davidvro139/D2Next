#pragma once
#include "Backend.h"
#include <functional>
namespace d2 {
struct NativeGameStart {
    std::unique_ptr<GameBackend> backend;
    uint32_t seed{},characterClass{};
    CharacterAppearance appearance;
    bool coloredEquipment=false;
};
// True when D2MOO is linked into this program (D2MODERN_INPROCESS): the game runs here and needs no DLL folder.
// Otherwise it runs in NativeGameHost.exe with the x64 DLLs of the folder given to startNativeGame and loadNativeTown.
bool nativeGameLinkedIn();
// LAN play through the TCP/IP menu (only when the game is linked in): this computer's address (empty: none, or no LAN
// in this build), and whether a join address names a computer.
bool lanAvailable();
std::string lanLocalAddress();
bool lanValidAddress(const std::string& address);
// The appearance of a character of that class from WorldSnapshot::appearance codes.
CharacterAppearance appearanceFromCodes(uint32_t characterClass,const std::array<uint32_t,17>& codes);
// The child owns all game state. The save is read-only. With saveOut the child writes the character there whenever the game saves it (an
// empty path keeps everything in memory). A LAN guest calls `waiting` while it connects (the loading screen); one that
// reaches no host throws ConnectionLost.
NativeGameStart startNativeGame(const std::filesystem::path& dllDirectory,
    const std::filesystem::path& gameDirectory,const std::filesystem::path& save,
    uint32_t seed,const CollisionGrid& collision,GridPoint worldOrigin,const std::vector<int>& levels={1},const std::filesystem::path& saveOut={},
    const std::function<void()>& waiting={});
}
