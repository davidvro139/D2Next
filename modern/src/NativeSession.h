#pragma once
#include "NativeProtocol.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace d2::native {
// The game server: D2MOO's D2Game running one game with the save's character (and LAN guests), one tick per
// request. NativeGameHost.exe serves it over pipes; the in-process build (D2MODERN_INPROCESS) calls it directly.
// One per process: D2Game keeps its games in globals.
class Session {
public:
    struct Settings {
        std::filesystem::path dlls;    // the x64 DLLs (ignored in-process, where they are linked in)
        std::filesystem::path game;    // the Diablo II folder with the MPQ archives
        std::filesystem::path save;    // read once at the start
        uint32_t seed=0;
        std::filesystem::path saveOut; // where the game saves the character (empty: nowhere)
        std::filesystem::path log;     // the game's log messages (empty: stderr)
    };
    // Creates the game and joins the character; `first` describes the game after joining.
    static std::unique_ptr<Session> start(const Settings& settings,Reply& first);
    virtual ~Session()=default;
    // One tick with the request's action in it. Quit leaves the game (it saves the character) and returns false.
    virtual bool handle(const Request& request,Reply& out)=0;
    // LAN: more players in the same game. join adds a player with their save and returns their client number (2, 3...);
    // apply queues one of their requests for the next tick (handle steps the game for everyone); describeClient is
    // their snapshot after it; leaveClient takes them out of the game.
    virtual int32_t join(const std::vector<uint8_t>& save)=0;
    virtual void apply(const Request& request,int32_t client)=0;
    virtual void describeClient(Reply& out,int32_t client)=0;
    virtual void leaveClient(int32_t client)=0;
    // A guest's character as the game saved it (when it left), for its own machine; false while there is none yet.
    virtual bool takeGuestSave(const std::string& name,std::vector<uint8_t>& save)=0;
};
}
