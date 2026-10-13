#pragma once
// LAN play: one machine hosts the game (its NativeGame runs the D2Game session); others join over TCP and play in the
// same game. The host steps the game once a tick for everyone; each guest sends its requests and gets its own
// snapshot (native::Reply) every tick. Both ends are the same version (a Windows PC and an Android phone alike: both
// 64-bit little-endian, and NativeProtocol.h checks the sizes), so the structures go over the wire as they are.
//
//   guest -> host: "D2L2", protocol magic, save size (u32), the save bytes; then native::Request after native::Request
//   host -> guest: the guest's client number (u32, 0 = refused); then frames of kind (u32), size (u32) and payload:
//                  kind 1 a native::Reply (one per host tick) as its change from the one before, kind 2 the guest's
//                  character as the game saved it when the guest left (sent after its Quit; size 0 when the game
//                  saved none), after which the host closes.
#include "NativeProtocol.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace d2::native {class Session;}
namespace d2::lan {
inline constexpr uint16_t defaultPort=4000; // the port Diablo II's own TCP/IP games use (1.10f D2Net.dll: htons(4000))
// This computer's address on the network as the TCP/IP screen shows it ("Your IP Address is:"): the one of the network
// card its traffic goes out on, else its first IPv4 address that is not a loopback one; empty when it has none.
std::string localAddress();
// Whether "address" or "address:port" names a computer (a dotted address, or a name that resolves), as the TCP/IP
// join box checks before going on to character selection.
bool validAddress(const std::string& address);
// The host side, kept by the host's NativeGame next to its session.
class Host {
public:
    explicit Host(uint16_t port);
    ~Host();
    Host(const Host&)=delete;Host& operator=(const Host&)=delete;
    // Before the host's tick: takes in new guests (joining them into the game) and queues their requests.
    void beforeTick(native::Session& session);
    // After it: sends every guest its snapshot of this tick.
    void afterTick(native::Session& session);
    size_t guests() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// The guest side: a connection to a host, used by the guest's NativeGame instead of a session of its own.
class Guest {
public:
    // Connects to "address" or "address:port", sends the save and waits for the host to take the character in and send
    // the first snapshot, calling `waiting` about every 50 ms meanwhile (the loading screen). Throws ConnectionLost.
    Guest(const std::string& address,const std::vector<uint8_t>& save,const std::function<void()>& waiting={});
    ~Guest();
    Guest(const Guest&)=delete;Guest& operator=(const Guest&)=delete;
    void send(const native::Request& request);
    // Takes in whatever has arrived, without waiting (throws ConnectionLost when the host has closed).
    void poll();
    // The snapshots that have arrived and are not played yet, the oldest of them (buffered() must not be 0), and the
    // seconds since the last one arrived. Over Wi-Fi they come in bursts: the game plays one a tick from a small store
    // instead of waiting for each.
    size_t buffered() const;
    void next(native::Reply& reply);
    double quietSeconds() const;
    // The next snapshot, waiting for it up to 4 seconds (the first one; throws ConnectionLost).
    void receive(native::Reply& reply);
    int32_t client() const;
    // Leaves the game: sends Quit, waits for the character the game saved and writes it to saveOut (when not empty).
    // False when the host sent none in time.
    bool leave(const std::filesystem::path& saveOut);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
