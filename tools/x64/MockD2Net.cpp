// Stand-in for D2Net.dll in the D2Game host tests. It replaces the network: the test pushes the packets a
// client would send, and reads back the packets D2Game sends to its clients. Built as x86 and as x64 from this
// one source, so the original 32-bit D2Game and the x64 one see the same transport.
//
// Only what D2Game imports from D2Net is implemented (ordinals 10006, 10010-10012, 10014-10016, 10019-10021, 10024).
//
// With MOCKD2NET_STATIC it is a static library instead, linked into a program together with D2MOO's static D2Game
// (modern/'s in-process build): the functions then have D2Net's own C++ declarations (Server.h) and no exports.
#include <windows.h>
#ifdef MOCKD2NET_STATIC
#include <Server.h>
#define MOCK_EXPORT
#define MOCK_NET_BEGIN
#define MOCK_NET_END
using MockCallback = D2NET_Unk_Callback;
#else
#define MOCK_EXPORT __declspec(dllexport)
#define MOCK_NET_BEGIN extern "C" {
#define MOCK_NET_END }
using MockCallback = void*;
#endif

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace
{
struct Sent
{
    int list;
    int client;
    std::vector<uint8_t> data;
};

std::mutex gMutex;
std::deque<std::vector<uint8_t>> gInbound[3]; // index = message list number (0 system, 1 game, 2 other)
std::deque<Sent> gSent;
std::map<int, int> gGuids;

int readList(int list, uint8_t* buffer, int size)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (gInbound[list].empty())
        return -1;
    std::vector<uint8_t> message = std::move(gInbound[list].front());
    gInbound[list].pop_front();
    const int n = int(message.size() < size_t(size) ? message.size() : size_t(size));
    std::memcpy(buffer, message.data(), size_t(n));
    // A queued message is the id of the client followed by its packet; the size handed back is the packet's.
    return n >= 4 ? n - 4 : n;
}
} // namespace

// Exports by ordinal only, as the original D2Net.dll has them. x86 stdcall names are decorated with the argument size.
#ifdef MOCKD2NET_STATIC
#define EXPORT_ORDINAL(name, ordinal, bytes)
#elif defined(_WIN64)
#define EXPORT_ORDINAL(name, ordinal, bytes) __pragma(comment(linker, "/EXPORT:" #name ",@" #ordinal ",NONAME"))
#else
#define EXPORT_ORDINAL(name, ordinal, bytes) __pragma(comment(linker, "/EXPORT:" #name "=_" #name "@" #bytes ",@" #ordinal ",NONAME"))
#endif
EXPORT_ORDINAL(D2NET_10006, 10006, 16)
EXPORT_ORDINAL(SERVER_ReadFromMessageList1, 10010, 8)
EXPORT_ORDINAL(SERVER_ReadFromMessageList0, 10011, 8)
EXPORT_ORDINAL(SERVER_ReadFromMessageList2, 10012, 8)
EXPORT_ORDINAL(SERVER_GetIpAddressStringFromClientId, 10014, 12)
EXPORT_ORDINAL(D2NET_10015, 10015, 12)
EXPORT_ORDINAL(D2NET_10016, 10016, 4)
EXPORT_ORDINAL(D2NET_10019, 10019, 4)
EXPORT_ORDINAL(SERVER_SetClientGameGUID, 10020, 8)
EXPORT_ORDINAL(SERVER_GetClientGameGUID, 10021, 4)
EXPORT_ORDINAL(SERVER_WSAGetLastError, 10024, 0)

MOCK_NET_BEGIN
uint32_t __stdcall D2NET_10006(int8_t list, int32_t client, void* buffer, uint32_t size)
{
    std::lock_guard<std::mutex> lock(gMutex);
    Sent sent{list, client, std::vector<uint8_t>(static_cast<uint8_t*>(buffer), static_cast<uint8_t*>(buffer) + size)};
    gSent.push_back(std::move(sent));
    return size;
}
int32_t __stdcall SERVER_ReadFromMessageList1(uint8_t* buffer, int32_t size) { return readList(1, buffer, size); }
int32_t __stdcall SERVER_ReadFromMessageList0(uint8_t* buffer, int32_t size) { return readList(0, buffer, size); }
int32_t __stdcall SERVER_ReadFromMessageList2(uint8_t* buffer, int32_t size) { return readList(2, buffer, size); }
void __stdcall SERVER_GetIpAddressStringFromClientId(int32_t, char* buffer, int32_t size)
{
    if (size > 9)
        std::strcpy(buffer, "127.0.0.1");
}
void __stdcall D2NET_10015(int32_t client, const char*, int32_t)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gSent.push_back({-1, client, {}}); // list -1: the server disconnected this client
}
void __stdcall D2NET_10016(int32_t client)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gSent.push_back({-2, client, {}}); // list -2: the server flushed this client
}
int32_t __stdcall D2NET_10019(MockCallback) { return 1; }
int32_t __stdcall SERVER_SetClientGameGUID(int32_t client, int32_t guid)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gGuids[client] = guid;
    return 1;
}
int32_t __stdcall SERVER_GetClientGameGUID(int32_t client)
{
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gGuids.find(client);
    return it == gGuids.end() ? 0 : it->second;
}
int32_t __stdcall SERVER_WSAGetLastError() { return 0; }
MOCK_NET_END

// The test side.
extern "C"
{
MOCK_EXPORT void __cdecl Mock_PushMessage(int list, const void* data, int size)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gInbound[list].emplace_back(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
}
// Returns the size of the next packet the server sent (or 0 for a disconnect/flush marker), or -1 when there is none.
MOCK_EXPORT int __cdecl Mock_PopSent(int* list, int* client, void* buffer, int capacity)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (gSent.empty())
        return -1;
    Sent sent = std::move(gSent.front());
    gSent.pop_front();
    *list = sent.list;
    *client = sent.client;
    const int n = int(sent.data.size() < size_t(capacity) ? sent.data.size() : size_t(capacity));
    std::memcpy(buffer, sent.data.data(), size_t(n));
    return n;
}
MOCK_EXPORT void __cdecl Mock_Reset()
{
    std::lock_guard<std::mutex> lock(gMutex);
    for (auto& q : gInbound)
        q.clear();
    gSent.clear();
    gGuids.clear();
}
}
