// The game server session (NativeSession.h). No save data is written to disk unless a save path is given.
// Startup/callback sequence follows tools/x64/D2GameHostTest.cpp.
#include "NativeSession.h"
#include "NativeLink.h"
#include <windows.h>
#include <GAME/Game.h>
#include <UNIT/SUnit.h>
#include <Path/Path.h>
#include <Units/Player.h>
#include <D2PacketDef.h>
#include <DataTbls/ItemsTbls.h>
#include <DataTbls/TokenTbls.h>
#include <DataTbls/MonsterTbls.h>
#include <D2DataTbls.h>
#include <D2StatList.h>
#include <D2States.h>
#include <D2Skills.h>
#include <DataTbls/SequenceTbls.h>
#include <D2Seed.h>
#include <Units/Monster.h>
#include <Units/Missile.h>
#include <D2Monsters.h>
#include <D2Waypoints.h>
#include <D2Environment.h>
#include <Drlg/D2DrlgDrlg.h>
#include <DataTbls/MonsterIds.h>
#include <algorithm>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <array>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace d2::native {
namespace {
struct PendingCharacter
{
    int requested = 0;
    int clientId = 0;
};
PendingCharacter gPending;
std::filesystem::path gSaveOut; // where the character is written when the game saves it (empty: nowhere)
std::FILE* gLog = stderr;
int gTransactionToken = 77;

void __cdecl cbLog(int32_t level, const char* message)
{
    std::fprintf(gLog, "log[%d] %s\n", level, message ? message : "");
}
void __fastcall cbCloseGame(WORD gameId, uint32_t product, uint32_t players, int32_t frame)
{
    std::fprintf(gLog, "cb CloseGame game=%u product=%u players=%u frame=%d\n", gameId, product, players, frame);
}
void __fastcall cbLeaveGame(D2ClientInfoStrc**, WORD gameId, int32_t classId, int32_t level, uint64_t experience, uint32_t flags, const char* name, const char*, int32_t, int32_t,
                            int32_t, const char* account, int32_t token, FILETIME*)
{
    std::fprintf(gLog, "cb LeaveGame game=%u class=%d level=%d exp=%llu flags=%08X name=%s account=%s token=%d\n", gameId, classId, level, (unsigned long long)experience, flags,
                 name, account, token);
}
void __fastcall cbGetCharacter(D2ClientInfoStrc**, const char* name, DWORD clientId, const char* account)
{
    std::fprintf(gLog, "cb GetDatabaseCharacter name=%s client=%lu account=%s\n", name, clientId, account);
    gPending.requested = 1;
    gPending.clientId = int(clientId);
}
// Where each character in the game is saved, by its name: the host's own file, or nothing for a LAN guest (whose save
// is kept in gGuestSaves for its own machine). Several players share one game, so a save must not land in the host's file.
std::map<std::string, std::filesystem::path> gSavePaths;
std::map<std::string, std::vector<uint8_t>> gGuestSaves;
// Each LAN guest's latest save: the one it joined with, then each one D2Game makes. D2Game compares a new save with the
// latest one and relocks instead of saving when nothing changed; this save (the one it found unchanged) then goes back.
std::map<std::string, std::vector<uint8_t>> gGuestJoined;
void syncWebStorage()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var fs = typeof FS !== 'undefined' ? FS : (typeof Module !== 'undefined' ? Module.FS : null);
        if (fs && fs.syncfs) fs.syncfs(false, function(err) { if (err) console.error(err); });
    });
#endif
}
void __fastcall cbSaveCharacter(D2ClientInfoStrc**, const char* name, const char* account, BYTE* data, uint32_t size, int32_t token)
{
    const auto target = gSavePaths.find(name ? name : "");
    if (target == gSavePaths.end() && size > 2)
    {
        gGuestSaves[name ? name : ""].assign(data + 2, data + size);
        gGuestJoined[name ? name : ""].assign(data + 2, data + size);
    }
    if (target != gSavePaths.end() && !target->second.empty() && size > 2)
    {
        // The first two bytes are the length prefix of the realm message, not part of the file.
        const auto& gSaveOut = target->second;
        auto temporary = gSaveOut;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data + 2), std::streamsize(size - 2));
        }
        std::error_code error;
        std::filesystem::rename(temporary, gSaveOut, error);
        if (error)
        {
            std::filesystem::remove(gSaveOut, error);
            std::filesystem::rename(temporary, gSaveOut, error);
        }
        syncWebStorage();
    }
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    std::fprintf(gLog, "cb SaveDatabaseCharacter name=%s account=%s size=%u token=%d hash=%016llX\n", name, account, size, token, (unsigned long long)h);
}
void __fastcall cbEnterGame(WORD gameId, const char* name, int32_t classId, int32_t level, uint32_t flags)
{
    std::fprintf(gLog, "cb EnterGame game=%u name=%s class=%d level=%d flags=%08X\n", gameId, name, classId, level, flags);
}
int32_t __fastcall cbFindToken(const char* name, int32_t token, WORD gameId, char* account, int32_t* transaction, int32_t* a6, int32_t* a7)
{
    std::fprintf(gLog, "cb FindPlayerToken name=%s token=%d game=%u\n", name, token, gameId);
    std::strcpy(account, "account");
    *transaction = gTransactionToken;
    *a6 = 0;
    *a7 = 0;
    return 1;
}
void __fastcall cbUnlock(uint32_t*, const char* name, const char* account) { std::fprintf(gLog, "cb UnlockDatabaseCharacter %s %s\n", name, account); }
void __fastcall cbRelock(D2ClientInfoStrc**, const char* name, const char* account)
{
    std::fprintf(gLog, "cb RelockDatabaseCharacter %s %s\n", name, account);
    const std::string key = name ? name : "";
    if (const auto joined = gGuestJoined.find(key); joined != gGuestJoined.end() && !gSavePaths.count(key))
        gGuestSaves[key] = joined->second;
}
void __fastcall cbLadder(const char* name, int32_t classId, int32_t level, uint32_t experience, int32_t, uint32_t flags, FILETIME*)
{
    std::fprintf(gLog, "cb UpdateCharacterLadder %s class=%d level=%d exp=%u flags=%08X\n", name, classId, level, experience, flags);
}
void __fastcall cbGameInfo(WORD gameId, const char* name, int32_t classId, int32_t level)
{
    std::fprintf(gLog, "cb UpdateGameInformation game=%u %s class=%d level=%d\n", gameId, name, classId, level);
}
void __fastcall cbHandlePacket(void*, int32_t size) { std::fprintf(gLog, "cb HandlePacket size=%d\n", size); }
uint32_t __fastcall cbSetGameData() { return 0; }

// Packets D2Game sent to the player, each with a two-byte length (the client reads them for sounds, stores and the like).
std::map<int32_t,std::vector<uint8_t>> gServerLogs; // by client

class GameSession final:public Session {
public:
    explicit GameSession(const Settings& settings) {
        namespace fs=std::filesystem;
        gSaveOut=settings.saveOut.empty()?fs::path():fs::absolute(settings.saveOut);
#ifdef _WIN32
        if(!settings.log.empty()){if(auto* file=_wfopen(settings.log.c_str(),L"w")){gLog=file;std::setvbuf(file,nullptr,_IOLBF,4096);}}
#else
        if(!settings.log.empty()){if(auto* file=std::fopen(settings.log.c_str(),"w")){gLog=file;std::setvbuf(file,nullptr,_IOLBF,4096);}}
#endif
        const auto saveSize=fs::file_size(settings.save);
        if(saveSize<0x30||saveSize>65535)throw std::runtime_error("Unsupported save size");
        save_.assign(size_t(saveSize),0);
        {
            // Closed straight away: the game later saves over this same file, which Windows refuses while it is open.
            std::ifstream input(settings.save,std::ios::binary);
            if(!input.read(reinterpret_cast<char*>(save_.data()),save_.size()))throw std::runtime_error("Cannot read save");
        }
        if(save_[0x28]>6||!std::memchr(save_.data()+0x14,0,16))throw std::runtime_error("Invalid save character header");
        const auto dllDir=fs::absolute(settings.dlls),gameDir=fs::absolute(settings.game);
        auto storm=loadModule(dllDir/L"Storm.dll",Library::Storm);auto fog=loadModule(dllDir/L"Fog.dll",Library::Fog);
        if(!findNamed(fog,"FogHost_Configure"))throw std::runtime_error("Requires native x64 Fog");
        loadModule(dllDir/L"D2Lang.dll",Library::D2Lang);cmp_=loadModule(dllDir/L"D2CMP.dll",Library::D2CMP);
#ifdef D2MODERN_INPROCESS
        net_=loadModule({},Library::Net);
#else
        wchar_t ownPath[32768];GetModuleFileNameW(nullptr,ownPath,32768);
        net_=loadModule(fs::path(ownPath).parent_path()/L"native-host"/L"D2Net.dll",Library::Net);
#endif
        common_=loadModule(dllDir/L"D2Common.dll",Library::D2Common);game_=loadModule(dllDir/L"D2Game.dll",Library::D2Game);
        push_=named<void(__cdecl*)(int,const void*,int)>(net_,"Mock_PushMessage");
        pop_=named<int(__cdecl*)(int*,int*,void*,int)>(net_,"Mock_PopSent");
#ifdef D2MODERN_INPROCESS
        (void)storm;useGameData(gameDir); // shared with the level loader, which may have loaded them already
#else
        auto open=ordinal<BOOL(__stdcall*)(const char*,int,int,void**)>(storm,266);
        auto patch=gameDir/L"D2SE/CORES/1.10f/Patch_D2.mpq";
        const fs::path paths[]={fs::exists(patch)?patch:gameDir/L"Patch_D2.mpq",gameDir/L"d2exp.mpq",gameDir/L"d2data.mpq",gameDir/L"d2char.mpq",gameDir/L"d2sfx.mpq"};
        int priority=5000;
        for(const auto& path:paths){void* h=nullptr;if(fs::exists(path)&&!open(path.string().c_str(),priority,2,&h))throw std::runtime_error("Archive mount failed");priority-=1000;}
        ordinal<void(__stdcall*)(void*,int,int)>(common_,10576)(nullptr,1,1);
#endif
        auto game=game_;
        gPending={};gServerLogs.clear();
        if(inProcess)named<void(__cdecl*)()>(net_,"Mock_Reset")(); // nothing left over from an earlier session
        std::printf("Native session: modules ready\n");std::fflush(stdout);
        ordinal<void(__fastcall*)(int32_t)>(game,10036)(1); // Saves go to cbSaveCharacter, never to disk; it writes the host's file only when there is a path (none in tests) and keeps a LAN guest's for its machine.
        ordinal<void(__stdcall*)(int32_t)>(game,10010)(int32_t(settings.seed));ordinal<void(__stdcall*)(int32_t)>(game,10009)(0);
        // The server itself is set up once per process; a later session (in-process) only creates its game.
        static bool serverReady=false;
        if(!serverReady)setUpServer();
        serverReady=true;
        std::printf("Native session: server ready\n");std::fflush(stdout);
        char name[]="modern";
        // The difficulty the front end chose (D2NEXT_DIFFICULTY: 0 Normal, 1 Nightmare, 2 Hell), in the game's flags as D2Game reads it.
        const char* chosen=std::getenv("D2NEXT_DIFFICULTY");
        const uint32_t difficulty=chosen?uint32_t(std::clamp(std::atoi(chosen),0,2)):0u;
        // A hardcore character (save status 0x24 bit 4) only joins a hardcore game: D2Game disconnects it from any other.
        const bool hardcore=save_.size()>0x24&&(save_[0x24]&0x04)!=0;
        // A classic character (status bit 0x20 clear) only joins a classic game, as D2Game checks it against the game's flags.
        const bool expansion=save_.size()<=0x24||(save_[0x24]&0x20)!=0;
        if(!ordinal<BOOL(__stdcall*)(char*,const char*,const char*,uint32_t,uint8_t,uint8_t,uint8_t,uint16_t*)>(game,10047)
            (name,"","",(expansion?GAMEFLAG_ARENA_EXPANSION:0u)|GAMEFLAG_ARENA_UPDATECLIENTS|(hardcore?GAMEFLAG_ARENA_HARDCORE:0u)|(difficulty<<GAMEFLAG_DIFFICULTY_BIT),0,0,1,&gameId_))throw std::runtime_error("Create game failed");
        std::printf("Native session: CreateGame id=%u\n",unsigned(gameId_));std::fflush(stdout);
        gSavePaths.clear();gGuestSaves.clear();gGuestJoined.clear();
        gSavePaths[std::string(reinterpret_cast<const char*>(save_.data()+0x14))]=gSaveOut;
        joinClient(1,save_);
        std::printf("Native session: client joined\n");std::fflush(stdout);
        if(auto* player=named<D2UnitStrc*(__cdecl*)(uint16_t,int32_t)>(game_,"D2GameHost_GetClientPlayer")(gameId_,1);player&&player->pGame)
            std::printf("Native game difficulty %d, act level %d\n",int(player->pGame->nDifficulty),player->pDynamicPath&&player->pDynamicPath->pRoom?ordinal<int(__stdcall*)(void*)>(common_,10057)(player->pDynamicPath->pRoom):-1);
    }
    ~GameSession() override {
        for(auto& [client,peer]:peers_)if(client!=1&&!peer.left)try{leaveClient(client);}catch(...){}
        if(!peers_[1].left)try{leave();}catch(...){}
        if(gLog!=stderr){std::fclose(gLog);gLog=stderr;}
    }
    void describe(Reply& output) {describe(output,1);}
    void describe(Reply& output,int32_t client);
    bool handle(const Request& request,Reply& out) override;
    // LAN: another player joins the same game with their save (client 2, 3...); their requests and snapshots go through
    // apply/describe, and the host's own tick (handle) steps the game for everyone.
    int32_t join(const std::vector<uint8_t>& save) override {
        if(save.size()<0x30||save.size()>65535||save[0x28]>6||!std::memchr(save.data()+0x14,0,16))throw std::runtime_error("Invalid guest save");
        const int32_t client=nextClient_++;
        joinClient(client,save);
        gGuestJoined[std::string(reinterpret_cast<const char*>(save.data()+0x14))]=save;
        return client;
    }
    void apply(const Request& request,int32_t client) override;
    void describeClient(Reply& out,int32_t client) override {describe(out,client);}
    bool takeGuestSave(const std::string& name,std::vector<uint8_t>& save) override {
        auto found=gGuestSaves.find(name);
        if(found==gGuestSaves.end())return false;
        save=std::move(found->second);gGuestSaves.erase(found);return true;
    }
    void leaveClient(int32_t client) override {
        auto& peer=peers_[client];if(peer.left)return;peer.left=true;
        // Saved first, as D2Game saves every player when a multiplayer game ends for one (the callback keeps it for the guest).
        named<int32_t(__cdecl*)(uint16_t,int32_t)>(game_,"D2GameHost_SaveClient")(gameId_,client);
        enterOrLeave(0x6d,client);gServerLogs.erase(client);
    }
private:
    Module net_{},common_{},game_{},cmp_{};
    void(__cdecl* push_)(int,const void*,int)=nullptr;
    int(__cdecl* pop_)(int*,int*,void*,int)=nullptr;
    std::vector<uint8_t> save_;
    uint16_t gameId_=0;
    uint32_t ticks_=0;
    // What the session keeps for each player in the game (client 1 is the host's own).
    struct Peer {
        int32_t vendorGuid=0; // the NPC whose store the player has opened
        std::vector<std::pair<uint16_t,uint32_t>> hirelings; // the mercenaries offered (packets 0x4F clear, 0x4E name and seed)
        int32_t lightAct=-1,lightIntensity=-1; // the client's own outdoor light intensity there (D2Common moves it a step a frame)
        bool left=false,testGiftsGiven=false,waypointsGiven=false;
        uint32_t lastMode=~0u,modeStart=0; // the player's animation mode and the tick it began
        uint32_t nextEventFrame=0; // frames of that mode already checked for AnimData events
    };
    std::map<int32_t,Peer> peers_;
    int32_t nextClient_=2;
    // Joins a client: the 0x67 join message, the character the server then asks for, and entering the act.
    void joinClient(int32_t client,const std::vector<uint8_t>& save) {
        auto game=game_;
        peers_[client]=Peer{};gPending={};
#pragma pack(push,1)
        struct {int32_t client=1;D2GSPacketClt67 packet{};} join;
#pragma pack(pop)
        join.client=client;
        join.packet.nHeader=0x67;join.packet.nTokenId=0x1234+client;join.packet.nGameId=gameId_;
        join.packet.nPlayerClass=save[0x28];join.packet.unk0x08=10;std::memcpy(join.packet.szClientName,save.data()+0x14,16);
        push_(0,&join,sizeof(join));for(int i=0;i<5;++i)step();
        if(!gPending.requested)throw std::runtime_error("Native host did not request character");
        std::printf("Native session: character requested client=%d\n",client);std::fflush(stdout);
        FILETIME time{};
        ordinal<int32_t(__stdcall*)(int32_t,const uint8_t*,uint16_t,uint16_t,int32_t,int32_t,FILETIME*,int32_t)>(game,10007)
            (client,save.data(),uint16_t(save.size()),uint16_t(save.size()),0,0,&time,gTransactionToken);
        std::printf("Native session: character loaded client=%d\n",client);std::fflush(stdout);
        for(int i=0;i<5;++i)step();enterOrLeave(0x6a,client);for(int i=0;i<5;++i)step();
    }
    void setUpServer() {
        auto game=game_;
        ordinal<void(__stdcall*)()>(game,10039)();ordinal<int32_t(__stdcall*)()>(game,10046)();
        static D2ServerCallbackFunctions callbacks{};
        callbacks.pfCloseGame=cbCloseGame;callbacks.pfLeaveGame=cbLeaveGame;
        callbacks.pfGetDatabaseCharacter=cbGetCharacter;callbacks.pfSaveDatabaseCharacter=cbSaveCharacter;
        callbacks.pfServerLogMessage=cbLog;callbacks.pfEnterGame=cbEnterGame;callbacks.pfFindPlayerToken=cbFindToken;
        callbacks.pfUnlockDatabaseCharacter=cbUnlock;callbacks.pfUpdateCharacterLadder=cbLadder;
        callbacks.pfUpdateGameInformation=cbGameInfo;callbacks.pfHandlePacket=cbHandlePacket;
        callbacks.pfSetGameData=cbSetGameData;callbacks.pfRelockDatabaseCharacter=cbRelock;
        ordinal<void(__stdcall*)(D2ServerCallbackFunctions*)>(game,10023)(&callbacks);
        // Kept for the rest of the process: D2Game holds on to them.
        static auto* table=new D2GameDataTableStrc;static auto* list=new int[64]{};
        ordinal<void(__stdcall*)(void*,void*)>(game,10002)(table,list);
        named<void(__cdecl*)(int32_t)>(game,"D2GameHost_AllowTimeouts")(0); // a local host has no network and no heartbeat packets
        named<void(__cdecl*)(void(__cdecl*)(int32_t,const void*,int32_t))>(game,"D2GameHost_SetPacketTap")([](int32_t client,const void* data,int32_t size){
            if(client<1||size<=0||size>65535)return;
            auto& log=gServerLogs[client];
            log.push_back(uint8_t(size&255));log.push_back(uint8_t(size>>8));
            log.insert(log.end(),static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size);
        });
    }
    void step() {
        ordinal<void(__fastcall*)()>(game_,10003)();
        ordinal<int32_t(__stdcall*)(int32_t)>(game_,10004)(1);
        ordinal<void(__stdcall*)(int32_t,int32_t)>(game_,10005)(1,0);
        uint8_t buffer[65536];int listId=0,client=0;
        for(int n;(n=pop_(&listId,&client,buffer,sizeof(buffer)))>=0;) {
            if(listId==-1) {
                if(client==1)throw std::runtime_error("Native server disconnected the player");
                std::fprintf(gLog,"server disconnected LAN client %d\n",client);peers_[client].left=true; // a guest: the host plays on
            }
        }
    }
    void enterOrLeave(uint8_t header,int32_t client=1) {
#pragma pack(push,1)
        struct {int32_t client=1;uint8_t header=0;} message;
#pragma pack(pop)
        message.client=client;message.header=header;push_(0,&message,sizeof(message));
    }
    void leave() {
        peers_[1].left=true;enterOrLeave(0x6d);
        ordinal<void(__fastcall*)()>(game_,10003)(); // leave callbacks discard save bytes
        // In-process the program goes on: the game is closed, so a later session starts with no other game.
        if(inProcess){ordinal<void(__stdcall*)()>(game_,10006)();gServerLogs.clear();}
    }
};
void GameSession::describe(Reply& output,int32_t client) {
    auto& peer=peers_[client];
    auto common=common_,game=game_;const auto gameId=gameId_;const auto ticks=ticks_;const auto vendorGuid=peer.vendorGuid;
    output=Reply{};
    auto getUnit=named<D2UnitStrc*(__cdecl*)(uint16_t,int32_t,int32_t)>(game,"D2GameHost_GetUnit");
    auto unitOf=[&](int32_t type,int32_t guid){return getUnit(gameId,type,guid);}; // a unit of this session's game
    D2UnitInfoStrc info{};
    auto* unit=named<D2UnitStrc*(__cdecl*)(uint16_t,int32_t)>(game,"D2GameHost_GetClientPlayer")(gameId,client); // this client's player
    if(!unit)throw std::runtime_error("No native player for the client");
    info.nUnitGUID=int32_t(unit->dwUnitId);info.nClassId=int32_t(unit->dwClassId);info.nAnimMode=int32_t(unit->dwAnimMode);
    if(!unit||!unit->pDynamicPath||!unit->pDynamicPath->pRoom)throw std::runtime_error("Missing native player path");
    {
        // Test hook: D2MODERN_TEST_GOLD gives the character that much gold once (the shop and stash paths need gold).
        // D2MODERN_TEST_WAYPOINTS (on by default; --no-waypoints) discovers every waypoint in Normal (global waypoint
        // numbers 0..38), for every player in the game: a LAN guest travels as the host does.
        if(!peer.waypointsGiven){peer.waypointsGiven=true;
            if(const char* waypoints=std::getenv("D2MODERN_TEST_WAYPOINTS");waypoints&&*waypoints) {
                auto* playerData=ordinal<D2PlayerDataStrc*(__stdcall*)(D2UnitStrc*)>(common,10424)(unit);
                for(uint16_t number=0;number<39;++number)ordinal<void(__stdcall*)(D2WaypointDataStrc*,uint16_t)>(common,11147)(playerData->pWaypointData[0],number);
            }
        }
        if(!peer.testGiftsGiven&&client==1){peer.testGiftsGiven=true; // the other test hooks are the host's
            // D2MODERN_TEST_QUESTS="1:0x1 2:0x4": sets those quest flag bits (QFLAG_*) of those quests (Normal), for the quest log.
            if(const char* quests=std::getenv("D2MODERN_TEST_QUESTS");quests&&*quests) {
                auto* playerData=ordinal<D2PlayerDataStrc*(__stdcall*)(D2UnitStrc*)>(common,10424)(unit);
                auto set=ordinal<void(__stdcall*)(void*,int,int)>(common,11108);
                std::istringstream list(quests);std::string entry;
                while(list>>entry) {
                    unsigned quest=0,bits=0;
                    if(std::sscanf(entry.c_str(),"%u:%x",&quest,&bits)==2&&quest<41)for(int bit=0;bit<16;++bit)if(bits&(1u<<bit))set(playerData->pQuestData[0],int(quest),bit);
                }
            }
            if(const char* give=std::getenv("D2MODERN_TEST_GIVE")) {
                // Test hook: "code[:quality[:sockets]] ..." items given to the player as a quest reward gives them (quality 2
                // normal), sockets added as D2Common ITEMS_AddSockets adds them.
                std::istringstream list(give);std::string entry;
                while(list>>entry) {
                    std::istringstream fields(entry);std::string code,field;std::getline(fields,code,':');
                    int quality=2,sockets=0;
                    if(std::getline(fields,field,':'))quality=std::atoi(field.c_str());
                    if(std::getline(fields,field,':'))sockets=std::atoi(field.c_str());
                    uint32_t packed=0x20202020;for(size_t i=0;i<code.size()&&i<4;++i)packed=(packed&~(0xFFu<<(8*i)))|(uint32_t(uint8_t(code[i]))<<(8*i));
                    auto* given=named<D2UnitStrc*(__cdecl*)(D2UnitStrc*,uint32_t,int32_t)>(game,"D2GameHost_GiveItem")(unit,packed,quality);
                    if(given&&sockets>0)ordinal<void(__stdcall*)(D2UnitStrc*,int)>(common,10817)(given,sockets);
                    if(given&&given->pItemData&&std::getenv("D2MODERN_AFFIX_LOG")) {
                        // Test hook: the affix numbers of the item given and what D2Common's own lookups (#10604 magic,
                        // #10606 rare) name them, to check the client's names against.
                        const auto* d=given->pItemData;
                        auto magic=ordinal<D2MagicAffixTxt*(__stdcall*)(int)>(common,10604);
                        auto rare=ordinal<D2RareAffixTxt*(__stdcall*)(int)>(common,10606);
                        const auto* p=d->wMagicPrefix[0]?magic(d->wMagicPrefix[0]):nullptr;const auto* s=d->wMagicSuffix[0]?magic(d->wMagicSuffix[0]):nullptr;
                        const auto* rp=d->wRarePrefix?rare(d->wRarePrefix):nullptr;const auto* rs=d->wRareSuffix?rare(d->wRareSuffix):nullptr;
                        std::fprintf(stdout,"Affixes: item %u quality %u prefix %u '%s' suffix %u '%s' rare %u '%s' %u '%s'\n",given->dwUnitId,d->dwQualityNo,
                            d->wMagicPrefix[0],p?p->szName:"",d->wMagicSuffix[0],s?s->szName:"",d->wRarePrefix,rp?rp->szName:"",d->wRareSuffix,rs?rs->szName:"");
                    }
                }
            }
            if(const char* experience=std::getenv("D2MODERN_TEST_EXP");experience&&*experience)named<void(__cdecl*)(D2UnitStrc*,uint32_t)>(game,"D2GameHost_AddExperience")(unit,uint32_t(std::atoi(experience)));if(const char* gold=std::getenv("D2MODERN_TEST_GOLD");gold&&*gold)ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t,int32_t)>(common,10517)(unit,14,std::atoi(gold),0);}
    }
    // Test hook: D2MODERN_TEST_DIE=N kills the player on tick N (the death screen and respawn).
    if(const char* die=std::getenv("D2MODERN_TEST_DIE");die&&*die&&ticks==uint32_t(std::atoi(die)))named<void(__cdecl*)(D2UnitStrc*)>(game,"D2GameHost_Kill")(unit);
    D2GameInfoStrc gameInfo{};ordinal<BOOL(__stdcall*)(uint16_t,D2GameInfoStrc*)>(game,10014)(gameId,&gameInfo);
    // A unit's states (states.txt rows 0..191: auras, curses, armours...) as bits (D2Common STATES_CheckState).
    auto statesOf=[&](D2UnitStrc* who,uint32_t* out){
        auto check=ordinal<BOOL(__stdcall*)(D2UnitStrc*,int)>(common,10487);
        for(int s=0;s<192;++s)if(check(who,s))out[s>>5]|=1u<<(s&31);
    };
    // A sequence (Whirlwind, Charge, Zeal, Leap...): D2Game counts the mode from its start frame (nSeqCurrentFramePrecise
    // = dwGameFrame << 8 when the mode starts, moved by rewinds) and steps it by dwSeqSpeed a game frame from the frame
    // bonus (0x6FCBCE70). That step's row of the sequence table names the mode and frame the game draws.
    auto sequenceRow=[&](D2UnitStrc* who,uint32_t& mode,uint32_t& frame) {
        if(who->dwAnimMode!=PLRMODE_SEQUENCE||!who->pAnimSeq||!who->pGame)return;
        const int32_t elapsed=who->pGame->dwGameFrame-(who->nSeqCurrentFramePrecise>>8);
        const int32_t bonus=ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10436)(who); // UNITS_GetFrameBonus
        const int32_t step=((bonus<<8)+elapsed*int32_t(who->dwSeqSpeed))>>8,steps=int32_t(who->dwSeqFrameCount>>8);
        if(steps<=0)return;
        const auto& row=who->pAnimSeq[std::clamp(step,0,steps-1)];
        mode=row.nMode;frame=row.nFrame;
    };
    output.tick=ticks;output.seed=gameInfo.nInitSeed;output.guid=uint32_t(info.nUnitGUID);
    output.characterClass=info.nClassId;output.mode=info.nAnimMode;
    output.pathType=unit->pDynamicPath->dwPathType;
    output.pathTargetX=unit->pDynamicPath->tTargetCoord.X;output.pathTargetY=unit->pDynamicPath->tTargetCoord.Y;
    if(output.pathType==PATHTYPE_LEAP&&unit->pSkills)if(auto* skill=unit->pSkills->pUsedSkill) {
        // Leap stores the server-adjusted landing tile in the active skill. The
        // dynamic path target can still contain the originally requested tile.
        output.pathTargetX=int32_t(skill->nPar1);
        output.pathTargetY=int32_t(skill->nPar2);
    }
    {   // The server never steps the player's own ordinary frame counter (only monsters), so derive it from time in the mode.
        if(uint32_t(info.nAnimMode)!=peer.lastMode){peer.lastMode=uint32_t(info.nAnimMode);peer.modeStart=ticks;peer.nextEventFrame=0;}
        const int32_t count=std::max(1,unit->dwFrameCountPrecise>>8);
        const uint32_t speed=info.nAnimMode==PLRMODE_SEQUENCE?unit->dwSeqSpeed:uint32_t(std::max<int32_t>(unit->wAnimSpeed,0));
        output.animationFrame=uint32_t(((ticks-peer.modeStart)*speed>>8)%uint32_t(count));
        // The frames passed since the last reply that carry an AnimData event (D2Common #10382, UNITS_GetEventFrameInfo):
        // D2Client 1.10f runs a skill's client do-function (skills.txt cltdofunc) on that frame. Sequence skills use their own table.
        if(info.nAnimMode!=PLRMODE_SEQUENCE&&unit->pAnimData) {
            const uint32_t passed=uint32_t((ticks-peer.modeStart)*speed>>8);
            for(uint32_t f=peer.nextEventFrame;f<=passed&&f<peer.nextEventFrame+uint32_t(count);++f)
                if(ordinal<int(__stdcall*)(D2UnitStrc*,int)>(common,10382)(unit,int(f%uint32_t(count))))output.frameEvent=1;
            peer.nextEventFrame=passed+1;
        }
        sequenceRow(unit,output.sequenceMode,output.sequenceFrame);
        // A shapeshifted player (Werewolf, Werebear) is drawn as a monster: D2Common #11013 turns the mode it shows into the
        // form's monstats class (states.txt gfxclass) and a monster mode the form has art for.
        {
            int type=0,cls=int(info.nClassId),mode=output.sequenceMode!=255?int(output.sequenceMode):int(info.nAnimMode);
            ordinal<void(__stdcall*)(D2UnitStrc*,int*,int*,int*,const char*,int)>(common,11013)(unit,&type,&cls,&mode,__FILE__,__LINE__);
            if(type==1){output.shapeClass=uint32_t(cls);output.shapeMode=uint32_t(mode);}
        }
    }
    {
        auto stat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common,10519);
        output.life=stat(unit,6,0)>>8;output.maxLife=stat(unit,7,0)>>8;output.mana=stat(unit,8,0)>>8;output.maxMana=stat(unit,9,0)>>8;
        output.characterLevel=stat(unit,12,0);output.experience=stat(unit,13,0);output.gold=stat(unit,14,0);output.stashGold=stat(unit,15,0);output.goldLost=stat(unit,175,0);
        statesOf(unit,output.playerStates);
        if(std::getenv("D2MODERN_STAT_LOG")) {
            // Test hook: the player's absorb pool (Bone Armor, Cyclone Armor: stats bonearmor/bonearmormax) when it changes.
            auto poolStat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common,10519);
            static int32_t lastPool=-1;
            const int32_t pool=poolStat(unit,STAT_BONEARMOR,0);
            if(pool!=lastPool){std::cout<<"Absorb pool: "<<pool<<"/"<<poolStat(unit,STAT_BONEARMORMAX,0)<<" (1/256), life "<<(poolStat(unit,6,0)>>8)<<", tick "<<ticks<<"\n";lastPool=pool;}
        }
    }
    std::memcpy(&output.x,&unit->pDynamicPath->tGameCoords,8);
    output.level=ordinal<int(__stdcall*)(void*)>(common,10057)(unit->pDynamicPath->pRoom);
    // The act's time of day, as D2Client has it. The server only moves the clock (GAME_UpdateEnvironment, packet 0x53); the
    // client works out the light every frame with ENVIRONMENT_UpdatePeriodOfDay (#10923) on its own act. That is done here
    // on a copy of the server act's environment, so the server's clock is left alone; then #10930, #10926 and #10927 read
    // the cycle (for the ambience), the intensity and the colour.
    if(unit->pGame) {
        const int act=ordinal<uint8_t(__stdcall*)(int)>(common,10001)(int(output.level));
        if(act>=0&&act<5)if(auto* serverAct=unit->pGame->pAct[act];serverAct&&serverAct->pEnvironment) {
            D2DrlgEnvironmentStrc environment=*serverAct->pEnvironment;
            if(peer.lightAct==act&&peer.lightIntensity>=0)environment.nIntensity=peer.lightIntensity; // as the client's own act keeps it
            D2DrlgActStrc copy=*serverAct;copy.pEnvironment=&environment;
            ordinal<BOOL(__stdcall*)(D2DrlgActStrc*,D2ActiveRoomStrc*)>(common,10923)(&copy,unit->pDynamicPath->pRoom);
            output.envCycle=uint8_t(ordinal<int(__stdcall*)(D2DrlgActStrc*)>(common,10930)(&copy));
            output.envIntensity=ordinal<int(__stdcall*)(D2DrlgActStrc*)>(common,10926)(&copy);
            peer.lightAct=act;peer.lightIntensity=environment.nIntensity;
            ordinal<void(__stdcall*)(D2DrlgActStrc*,uint8_t*,uint8_t*,uint8_t*)>(common,10927)(&copy,&output.envRed,&output.envGreen,&output.envBlue);
        }
        if(std::getenv("D2MODERN_ENV_LOG")) {
            static int lastCycle=-1;
            if(output.envCycle!=lastCycle||ticks%250==0)std::cout<<"Environment: cycle "<<int(output.envCycle)<<", light "<<int(output.envRed)<<","<<int(output.envGreen)<<","<<int(output.envBlue)<<" x "<<output.envIntensity<<", tick "<<ticks<<"\n";
            lastCycle=output.envCycle;
        }
    }
    // A player's look (the weapon class, then the 16 COF component tokens) from what the unit wears: the player's own, and
    // a monster's drawn as a player (Valkyrie, Decoy, Shadow Warrior).
    // An item's colour (D2Common ITEMS_GetColor, type 1 the inventory picture, 0 the character's gear): the colour map it
    // returns is D2CMP_MixPalette(trans, colour), map number trans * 105 + colour in D2CMP's table, decoded back here.
    auto colorOf=[&](D2UnitStrc* who,D2UnitStrc* item,int type,uint8_t& trans,uint8_t& color) {
        trans=0;color=0;uint8_t packed=0;
        const uint8_t* map=ordinal<uint8_t*(__stdcall*)(D2UnitStrc*,D2UnitStrc*,uint8_t*,int)>(common,10829)(who,item,&packed,type);
        if(!map)return;
        const uint8_t* base=ordinal<uint8_t*(__stdcall*)(uint8_t,int)>(cmp_,10098)(1,0)-105*256;
        const auto index=size_t(map-base)/256;
        if(index<9*105&&index%105<21){trans=uint8_t(index/105);color=uint8_t(index%105);}
    };
    auto lookOf=[&](D2UnitStrc* who,int playerClass,uint32_t& weaponClass,uint32_t* components,uint32_t& coloredComponents,uint8_t* componentTrans=nullptr,uint8_t* componentColor=nullptr) {
        int weaponId=0;
        weaponClass=ordinal<int(__stdcall*)(D2UnitStrc*,int,int,int,D2InventoryStrc*,int*)>(common,10887)
            (who,0,playerClass,5,who->pInventory,&weaponId); // town neutral
        constexpr uint32_t light=uint32_t('l')|(uint32_t('i')<<8)|(uint32_t('t')<<16);
        for(int i=0;i<16;++i)components[i]=(i>=5&&i<=7)?0:light;
        if(!who->pInventory)return;
        for(int location:{1,3,4,5}) { // head, torso, active right/left hand
            auto item=ordinal<D2UnitStrc*(__stdcall*)(D2InventoryStrc*,int)>(common,10257)(who->pInventory,location);
            if(!item)continue;
            uint8_t color=0;
            const bool colored=ordinal<uint8_t*(__stdcall*)(D2UnitStrc*,D2UnitStrc*,uint8_t*,int)>(common,10829)(who,item,&color,0)!=nullptr;
            if(location==3) {
                auto record=ordinal<D2ItemsTxt*(__stdcall*)(int)>(common,10600)(item->dwClassId);
                if(!record)throw std::runtime_error("Missing armor appearance record");
                for(int component:{1,2,3,4,8,9}) {
                    auto armor=ordinal<unsigned(__stdcall*)(int,uint8_t*)>(common,10891)(component,record->nArmorComp);
                    auto type=ordinal<D2ArmTypeTxt*(__stdcall*)(int)>(common,10650)(int(armor));
                    if(!type)throw std::runtime_error("Missing armor appearance token");
                    components[component]=0;std::memcpy(&components[component],type->szToken,3);
                    if(colored)coloredComponents|=1u<<component;
                    if(colored&&componentTrans&&componentColor)colorOf(who,item,0,componentTrans[component],componentColor[component]);
                }
            } else {
                auto component=ordinal<uint8_t(__stdcall*)(D2UnitStrc*,D2UnitStrc*)>(common,10442)(who,item);
                if(component<16) {
                    components[component]=ordinal<uint32_t(__stdcall*)(D2UnitStrc*)>(common,10747)(item)&0xffffff;
                    if(colored)coloredComponents|=1u<<component;
                    if(colored&&componentTrans&&componentColor)colorOf(who,item,0,componentTrans[component],componentColor[component]);
                }
            }
        }
    };
    lookOf(unit,int(info.nClassId),output.weaponClass,output.components,output.coloredComponents,output.componentTrans,output.componentColor);
    // Holy Shield (state 101) with a shield in hand: the shield is drawn from its own glowing art, token "hsh", as D2Client
    // 1.10 picks component tokens (6FB5E7FC).
    if(output.components[7]&&ordinal<BOOL(__stdcall*)(D2UnitStrc*,int)>(common,10487)(unit,101)) {
        output.components[7]=0;std::memcpy(&output.components[7],"hsh",3);output.coloredComponents&=~(1u<<7);
    }
    // Every monster and town person, nearest to the player first.
    {
        D2DataTablesStrc* tables=*ordinal<D2DataTablesStrc**>(common,10042);
        auto statValue=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common,10519);
        std::vector<Unit> found;
        for(int32_t guid=1;guid<=int32_t(gameInfo.dwLastUsedUnitGUID[UNIT_MONSTER])&&guid<=1000;++guid) {
            auto other=unitOf(UNIT_MONSTER,guid);
            if(!other||!other->pDynamicPath)continue;
            Unit u;u.guid=uint32_t(guid);u.classId=uint32_t(other->dwClassId);u.mode=other->dwAnimMode;
            u.direction=other->pDynamicPath->nDirection;u.animSpeed=uint32_t(other->wAnimSpeed);
            u.life=statValue(other,6,0)>>8;u.maxLife=statValue(other,7,0)>>8;statesOf(other,u.states);u.hitClass=uint8_t(other->dwLastHitClass);
            if(std::getenv("D2MODERN_STAT_LOG")) {
                // Test hook: a monster's states changed (a curse starts or ends): the stats the curses change, as the game holds
                // them (physical/fire/lightning/cold/poison resistance, damage %, velocity %, attack rate).
                static std::map<int32_t,std::pair<std::array<uint32_t,6>,std::string>> seen;
                std::array<uint32_t,6> now{};std::copy(std::begin(u.states),std::end(u.states),now.begin());
                std::ostringstream line;
                line<<"resist "<<statValue(other,STAT_DAMAGERESIST,0)<<"/"<<statValue(other,STAT_FIRERESIST,0)<<"/"<<statValue(other,STAT_LIGHTRESIST,0)
                    <<"/"<<statValue(other,STAT_COLDRESIST,0)<<"/"<<statValue(other,STAT_POISONRESIST,0)<<", damage% "<<statValue(other,STAT_DAMAGEPERCENT,0)
                    <<", velocity% "<<statValue(other,STAT_VELOCITYPERCENT,0)<<", attackrate "<<statValue(other,STAT_ATTACKRATE,0)
                    <<", defense "<<ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10431)(other)<<", to-hit% "<<statValue(other,STAT_ITEM_TOHIT_PERCENT,0);
                if(const auto was=seen.find(guid);was!=seen.end()&&was->second.first!=now) {
                    std::cout<<"Monster "<<guid<<" (class "<<other->dwClassId<<") states";
                    for(int s=0;s<192;++s)if(now[size_t(s>>5)]&(1u<<(s&31)))std::cout<<" "<<s;
                    std::cout<<": "<<was->second.second<<" -> "<<line.str()<<"\n";
                }
                seen[guid]={now,line.str()};
            }
            {   // Drawn as a player? (D2Common #11013: a states.txt gfxtype 2 state, e.g. Valkyrie, Decoy, Shadow Warrior.)
                int type=UNIT_MONSTER,cls=int(other->dwClassId),mode=int(other->dwAnimMode);
                ordinal<void(__stdcall*)(D2UnitStrc*,int*,int*,int*,const char*,int)>(common,11013)(other,&type,&cls,&mode,__FILE__,__LINE__);
                if(type==UNIT_PLAYER) {
                    uint32_t colored=0;
                    u.lookClass=uint32_t(cls);u.lookMode=uint32_t(mode);lookOf(other,cls,u.look[0],&u.look[1],colored);
                }
            }
            if(const auto* data=other->pMonsterData) {
                // What kind of monster it is, what it can do, what it is called and how it is coloured.
                u.monsterType=data->nTypeFlag;
                std::copy(std::begin(data->nMonUmod),std::end(data->nMonUmod),u.umods);
                std::copy(std::begin(data->nComponent),std::end(data->nComponent),u.components);
                const int difficulty=other->pGame?std::clamp<int>(other->pGame->nDifficulty,0,2):0;
                if((data->nTypeFlag&MONTYPEFLAG_SUPERUNIQUE)&&data->wBossHcIdx<tables->nSuperUniquesTxtRecordCount) {
                    const auto& super=tables->pSuperUniquesTxt[data->wBossHcIdx];
                    u.name[0]=super.wNameStr;u.colorTransform=uint8_t(super.nUtrans[difficulty]+1);
                } else if((data->nTypeFlag&MONTYPEFLAG_UNIQUE)&&!(data->nTypeFlag&MONTYPEFLAG_CHAMPION)) { // champions carry the unique flag too
                    // A random unique's name: prefix, suffix and appellation rolled from its name seed (D2Common's own
                    // tables and seed), "Gloomtouch the Hammer"; its colour is monstats2.txt Utrans or the next roll.
                    D2SeedStrc seed{};seed.nLowSeed=data->wNameSeed;seed.nHighSeed=666;
                    auto roll=[&](int count){return count>0?int(SEED_RollLimitedRandomNumber(&seed,count)):-1;};
                    if(const int i=roll(tables->nUniquePrefixTxtRecordCount);i>=0)u.name[0]=tables->pUniquePrefixTxt[i].wStringId;
                    if(const int i=roll(tables->nUniqueSuffixTxtRecordCount);i>=0)u.name[1]=tables->pUniqueSuffixTxt[i].wStringId;
                    if(const int i=roll(tables->nUniqueAppellationTxtRecordCount);i>=0)u.name[2]=tables->pUniqueAppellationTxt[i].wStringId;
                    const auto* stats2=data->pMonstatsTxt&&data->pMonstatsTxt->wMonStatsEx<tables->nMonStats2TxtRecordCount?&tables->pMonStats2Txt[data->pMonstatsTxt->wMonStatsEx]:nullptr;
                    const int utrans=stats2?stats2->nUtrans[difficulty]:0;
                    u.colorTransform=uint8_t((utrans?utrans:roll(30))+1);
                }
            }
            std::memcpy(&u.x,&other->pDynamicPath->tGameCoords,8);
            {const auto& t=other->pDynamicPath->tFinalTargetCoord.X?other->pDynamicPath->tFinalTargetCoord:other->pDynamicPath->tTargetCoord;u.targetX=t.X;u.targetY=t.Y;}
            if(other->dwClassId>=0&&other->dwClassId<tables->nMonStatsTxtRecordCount&&(tables->pMonStatsTxt[other->dwClassId].dwMonStatsFlags&MONSTATSFLAG_NPC))u.flags|=unitTownPerson;
            if(u.mode==MONMODE_DEATH||u.mode==MONMODE_DEAD)u.flags|=unitDead;
            switch(other->dwClassId){
            case MONSTER_GHEED:case MONSTER_ELZIX:case MONSTER_ALKOR:case MONSTER_JAMELLA:case MONSTER_DREHYA:u.flags|=unitVendor|unitGambler;break;
            case MONSTER_AKARA:case MONSTER_CHARSI:case MONSTER_DROGNAN:case MONSTER_FARA:case MONSTER_LYSANDER:case MONSTER_ASHEARA:case MONSTER_HRATLI:case MONSTER_ORMUS:case MONSTER_HALBU:case MONSTER_LARZUK:case MONSTER_MALAH:u.flags|=unitVendor;break;
            default:break;
            }
            found.push_back(u);
        }
        // The other players in the game (LAN): drawn as players in their own gear (the look of a monster drawn as a player).
        for(const auto& [otherClient,otherPeer]:peers_) {
            if(otherClient==client||otherPeer.left)continue;
            auto* other=named<D2UnitStrc*(__cdecl*)(uint16_t,int32_t)>(game,"D2GameHost_GetClientPlayer")(gameId,otherClient);
            if(!other||!other->pDynamicPath)continue;
            Unit u;u.guid=other->dwUnitId;u.classId=uint32_t(other->dwClassId);u.mode=other->dwAnimMode;u.flags=unitPlayer;
            u.direction=other->pDynamicPath->nDirection;u.animSpeed=uint32_t(other->wAnimSpeed);
            u.life=statValue(other,6,0)>>8;u.maxLife=statValue(other,7,0)>>8;statesOf(other,u.states);u.hitClass=uint8_t(other->dwLastHitClass);
            std::memcpy(&u.x,&other->pDynamicPath->tGameCoords,8);
            uint32_t colored=0;
            u.lookClass=uint32_t(other->dwClassId);u.lookMode=uint32_t(other->dwAnimMode);lookOf(other,int(other->dwClassId),u.look[0],&u.look[1],colored);
            // Charge, Leap, Whirlwind...: what the sequence draws, the skill and the path, as for this player's own character.
            uint32_t rowMode=255,rowFrame=0;sequenceRow(other,rowMode,rowFrame);
            u.sequenceMode=uint8_t(rowMode);u.sequenceFrame=uint8_t(rowFrame);u.pathType=other->pDynamicPath->dwPathType;
            if(other->pSkills&&other->pSkills->pUsedSkill&&other->pSkills->pUsedSkill->pSkillsTxt)u.usedSkill=uint16_t(other->pSkills->pUsedSkill->pSkillsTxt->nSkillId);
            found.push_back(u);
        }
        // Players' corpses (1.10: D2Game's PlrModes 0x6FC7FBD0 leaves a dead body holding the gear the player wore, a player
        // unit lying dead that no client plays; this build leaves UNITFLAGEX_ISCORPSE off): drawn lying dead in that gear; a
        // click takes it back (0x13, unit type 0).
        for(int32_t guid=1;guid<=int32_t(gameInfo.dwLastUsedUnitGUID[UNIT_PLAYER])&&guid<=64;++guid) {
            auto* body=unitOf(UNIT_PLAYER,guid);
            if(!body||body->dwAnimMode!=PLRMODE_DEAD||!body->pDynamicPath)continue;
            // Not a client's own player lying dead (that one is drawn as the player, or as a LAN player above).
            bool someone=false;
            for(const auto& [peerClient,bodyPeer]:peers_)if(!bodyPeer.left&&named<D2UnitStrc*(__cdecl*)(uint16_t,int32_t)>(game,"D2GameHost_GetClientPlayer")(gameId,peerClient)==body)someone=true;
            if(someone)continue;
            Unit u;u.guid=body->dwUnitId;u.classId=uint32_t(body->dwClassId);u.mode=body->dwAnimMode;u.flags=unitPlayer|unitDead;
            u.direction=body->pDynamicPath->nDirection;u.animSpeed=uint32_t(body->wAnimSpeed);
            std::memcpy(&u.x,&body->pDynamicPath->tGameCoords,8);
            uint32_t colored=0;
            u.lookClass=uint32_t(body->dwClassId);u.lookMode=uint32_t(body->dwAnimMode);lookOf(body,int(body->dwClassId),u.look[0],&u.look[1],colored);
            found.push_back(u);
        }
        // Missiles (spell/arrow projectiles and their impact effects) near the player, drawn the same way.
        for(int32_t guid=1;guid<=int32_t(gameInfo.dwLastUsedUnitGUID[UNIT_MISSILE])&&guid<=2000;++guid) {
            auto missile=unitOf(UNIT_MISSILE,guid);
            if(!missile||!missile->pDynamicPath)continue;
            Unit u;u.guid=uint32_t(guid);u.classId=uint32_t(missile->dwClassId);u.mode=missile->dwAnimMode;
            u.direction=missile->pDynamicPath->nDirection;u.animSpeed=uint32_t(missile->wAnimSpeed);u.flags=unitMissile;
            // Frame counts down to 0 at the end of the range (MissMode's collision step); a hit removes the missile sooner.
            // The skill is on the missile data. The caster is the unit's owner (UNITS_StoreOwner). The copy inside the
            // missile data is filled only for LastCollide missiles, so it is not the one to use.
            if(const auto* data=missile->pMissileData){u.life=data->nCurrentFrame;u.maxLife=data->nSkill;}
            u.name[0]=uint16_t(missile->dwOwnerGUID);u.name[1]=uint16_t(missile->dwOwnerGUID>>16);
            std::memcpy(&u.x,&missile->pDynamicPath->tGameCoords,8);
            const int64_t dx=int64_t(u.x)-output.x,dy=int64_t(u.y)-output.y;
            if(dx*dx+dy*dy>int64_t(120)*120*65536*65536)continue; // far off-screen missiles are not worth publishing
            found.push_back(u);
        }
        auto distance=[&](const Unit& u){const int64_t dx=int64_t(u.x)-output.x,dy=int64_t(u.y)-output.y;return dx*dx+dy*dy;};
        std::sort(found.begin(),found.end(),[&](const auto& a,const auto& b){return distance(a)<distance(b);});
        output.unitCount=uint32_t(std::min<size_t>(found.size(),maxUnits));
        for(uint32_t i=0;i<output.unitCount;++i)output.units[i]=found[i];
    }
    // Items on the ground and everything the player carries.
    {
        std::vector<Item> found;
        std::set<uint32_t> carriedGuids,vendorGuids,hirelingGuids;std::map<uint32_t,std::pair<uint32_t,uint32_t>> socketOf; // socketed item -> (its parent, its place)
        auto first=ordinal<D2UnitStrc*(__stdcall*)(D2InventoryStrc*)>(common,10277);
        auto next=ordinal<D2UnitStrc*(__stdcall*)(D2UnitStrc*)>(common,10304);
        auto isItem=ordinal<D2UnitStrc*(__stdcall*)(D2UnitStrc*)>(common,10305);
        auto unitStat=ordinal<int32_t(__stdcall*)(const D2UnitStrc*,int,uint16_t)>(common,10519);
        auto baseStat=ordinal<int(__stdcall*)(D2UnitStrc*,int,uint16_t)>(common,10521);
        auto maxDurability=ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10567);
        if(vendorGuid)if(auto* npc=unitOf(UNIT_MONSTER,vendorGuid);npc&&npc->pInventory)for(D2UnitStrc* item=first(npc->pInventory);item;item=isItem(next(item)))vendorGuids.insert(item->dwUnitId);
        // What the hireling wears (its owner in Item::owner, as a store's items carry their NPC).
        auto* hireling=named<D2UnitStrc*(__cdecl*)(D2UnitStrc*)>(game,"D2GameHost_GetHireling")(unit);
        if(hireling&&hireling->pInventory)for(D2UnitStrc* item=first(hireling->pInventory);item;item=isItem(next(item)))hirelingGuids.insert(item->dwUnitId);
        {
            for(D2UnitStrc* item=first(unit->pInventory);item;item=isItem(next(item)))carriedGuids.insert(item->dwUnitId);
            if(auto* cursor=ordinal<D2UnitStrc*(__stdcall*)(D2InventoryStrc*)>(common,10262)(unit->pInventory))carriedGuids.insert(cursor->dwUnitId);
            // What is in each carried item's sockets, in the order D2Client draws it (the item's own inventory list, 6FAE0DC0).
            std::vector<uint32_t> parents(carriedGuids.begin(),carriedGuids.end());
            for(uint32_t parentGuid:parents)if(auto* parent=unitOf(UNIT_ITEM,int32_t(parentGuid));parent&&parent->pInventory) {
                uint32_t index=0;
                for(D2UnitStrc* child=first(parent->pInventory);child;child=isItem(next(child))){carriedGuids.insert(child->dwUnitId);socketOf[child->dwUnitId]={parentGuid,index++};}
            }
        }
        D2UnitStrc* cursorItem=unit->pInventory?ordinal<D2UnitStrc*(__stdcall*)(D2InventoryStrc*)>(common,10262)(unit->pInventory):nullptr;
        // Where the cursor's item may go on the body, by D2Common's rule (D2Client switches on this; see Reply).
        if(unit->pInventory)output.wieldType=int8_t(ordinal<int(__stdcall*)(D2UnitStrc*,D2InventoryStrc*)>(common,10291)(unit,unit->pInventory));
        // The sets' bonuses on the player: a list per item-set state, stat 71 its set (sets.txt row), as D2Client 0x6FAF33C0
        // reads them (#10480; stat 71 as #10466 gives it).
        for(int i=0;i<6;++i)if(auto* list=ordinal<D2StatListStrc*(__stdcall*)(D2UnitStrc*,int)>(common,10480)(unit,STATE_ITEMSET1+i)) {
            auto& bonus=output.setBonuses[i];
            D2StatStrc stats[32]{};
            const int count=ordinal<int(__stdcall*)(D2StatListStrc*,D2StatStrc*,int)>(common,11243)(list,stats,32);
            for(int k=0;k<count;++k)
                if(stats[k].nStat==71)bonus.set=stats[k].nValue; // what #10466 (list, 71) gives D2Client
                else if(bonus.statCount<16)bonus.stats[bonus.statCount++]={stats[k].nStat,stats[k].nLayer,stats[k].nValue};
        }
        // The set items the player has, by D2Client's rule (0x6FAE5990): set quality, inventory page 0, 3, 4 or none (the body)
        // and node page 1, 3 or 4 (#10695, #10719 and #10307 read these item data fields).
        if(unit->pInventory)for(D2UnitStrc* item=first(unit->pInventory);item&&output.ownedSetItemCount<maxOwnedSetItems;item=isItem(next(item))) {
            if(!item->pItemData||item->pItemData->dwQualityNo!=5)continue;
            const uint8_t page=item->pItemData->nInvPage;
            const int node=item->pItemData->pExtraData.nNodePosOther;
            if((page==0||page==3||page==4||page==0xFF)&&(node==1||node==3||node==4))output.ownedSetItems[output.ownedSetItemCount++]=uint16_t(item->pItemData->dwFileIndex);
        }
        if(cursorItem)for(int loc=1;loc<=12;++loc)if(ordinal<BOOL(__stdcall*)(D2UnitStrc*,uint8_t)>(common,10758)(cursorItem,uint8_t(loc)))output.bodyFits|=uint16_t(1u<<loc);
        if(cursorItem)for(int loc=1;loc<=12;++loc)output.bodyPlacement[loc]=uint8_t(ordinal<int(__stdcall*)(D2UnitStrc*,int,D2UnitStrc*,BOOL)>(common,10299)(unit,loc,cursorItem,FALSE));
        for(int32_t guid=1;guid<=int32_t(gameInfo.dwLastUsedUnitGUID[UNIT_ITEM])&&guid<=2000;++guid) {
            int32_t type=UNIT_ITEM,id=guid;D2UnitInfoStrc info2{};
            if(ordinal<int32_t(__stdcall*)(uint16_t,int32_t,D2UnitInfoStrc*,int32_t*,int32_t*)>(game,10018)(gameId,1,&info2,&type,&id)!=1)continue;
            const bool ground=info2.nAnimMode==3||info2.nAnimMode==5;
            const bool carried=carriedGuids.count(uint32_t(guid))!=0;
            const bool vendor=vendorGuids.count(uint32_t(guid))!=0,hired=hirelingGuids.count(uint32_t(guid))!=0;
            if(!ground&&!carried&&!vendor&&!hired)continue;
            auto item=unitOf(UNIT_ITEM,guid);
            if(!item||!item->pItemData)continue;
            auto record=ordinal<D2ItemsTxt*(__stdcall*)(int)>(common,10600)(item->dwClassId);
            Item out;out.guid=uint32_t(guid);out.classId=uint32_t(item->dwClassId);out.code=record?record->dwCode:0;out.mode=info2.nAnimMode;
            out.x=info2.nX;out.y=info2.nY;out.owner=vendor?uint32_t(vendorGuid):hired?hireling->dwUnitId:0;
            if(vendor){
                auto* vendorUnit=unitOf(UNIT_MONSTER,vendorGuid);
                auto* playerData=ordinal<D2PlayerDataStrc*(__stdcall*)(D2UnitStrc*)>(common,10424)(unit);
                if(vendorUnit&&playerData)out.price=uint32_t(ordinal<int(__stdcall*)(D2UnitStrc*,D2UnitStrc*,int,void*,int,int)>(common,10775)(unit,item,0,playerData->pQuestData[0],int(vendorUnit->dwClassId),0));
            }
            const auto* data=item->pItemData;
            out.quality=data->dwQualityNo;out.flags=data->dwItemFlags;out.invGraphic=data->nInvGfxIdx;
            out.prefix=data->wMagicPrefix[0];out.suffix=data->wMagicSuffix[0];out.rarePrefix=data->wRarePrefix;out.rareSuffix=data->wRareSuffix;out.fileIndex=uint32_t(data->dwFileIndex);out.page=data->nInvPage;out.bodyLoc=info2.nAnimMode==1?uint32_t(info2.nX):data->nBodyLoc;
            out.minDamage=unitStat(item,STAT_MINDAMAGE,0);out.maxDamage=unitStat(item,STAT_MAXDAMAGE,0);
            out.twoHandMinDamage=unitStat(item,STAT_SECONDARY_MINDAMAGE,0);out.twoHandMaxDamage=unitStat(item,STAT_SECONDARY_MAXDAMAGE,0);
            out.throwMinDamage=unitStat(item,STAT_ITEM_THROW_MINDAMAGE,0);out.throwMaxDamage=unitStat(item,STAT_ITEM_THROW_MAXDAMAGE,0);
            out.armorClass=baseStat(item,STAT_ARMORCLASS,0);
            out.durability=unitStat(item,STAT_DURABILITY,0);out.maxDurability=maxDurability(item);
            out.quantity=unitStat(item,STAT_QUANTITY,0);out.sockets=uint32_t(std::max(0,ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10816)(item)));
            if(auto s=socketOf.find(uint32_t(guid));s!=socketOf.end()){out.parent=s->second.first;out.x=int32_t(s->second.second);out.y=0;}
            {   // What D2Client 1.10f asks D2Common before an item packet (see itemTrait* in NativeProtocol.h).
                auto flag=[&](uint32_t f){return ordinal<BOOL(__stdcall*)(D2UnitStrc*,uint32_t,int,const char*)>(common,10707)(item,f,__LINE__,__FILE__)!=0;};
                if(ordinal<uint32_t(__stdcall*)(D2UnitStrc*)>(common,10768)(item))out.traits|=itemTraitBeltable;
                if(ordinal<BOOL(__stdcall*)(D2UnitStrc*)>(common,10821)(item))out.traits|=itemTraitSocketFiller;
                if(flag(0x800)&&ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10810)(item)&&ordinal<uint8_t(__stdcall*)(D2UnitStrc*)>(common,10815)(item)>0
                    &&flag(0x10)&&!flag(0x100)
                    // The server gives a socketed item its inventory only when the first filler goes in (D2Game
                    // D2GAME_ITEMSOCKET_PlaceItem); until then nothing is inside.
                    &&ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10816)(item)>(item->pInventory?ordinal<int(__stdcall*)(D2InventoryStrc*)>(common,10284)(item->pInventory):0))out.traits|=itemTraitTakesFiller;
                if(cursorItem&&cursorItem!=item&&ordinal<BOOL(__stdcall*)(D2UnitStrc*,D2UnitStrc*)>(common,10833)(cursorItem,item))out.traits|=itemTraitStacksWithCursor;
                if(ordinal<BOOL(__stdcall*)(D2UnitStrc*,D2UnitStrc*,BOOL,BOOL*,BOOL*,BOOL*)>(common,10756)(item,unit,FALSE,nullptr,nullptr,nullptr))out.traits|=itemTraitUsable;
                if(ordinal<int32_t(__stdcall*)(D2UnitStrc*)>(common,10781)(item))out.traits|=itemTraitUsesAmmo;
                if(ordinal<int32_t(__stdcall*)(D2UnitStrc*)>(common,10783)(item))out.traits|=itemTraitQuiver;
            }
            {   // The tooltip's base lines (see Item::armorTotal): D2Common's defense and requirement rules for this player.
                out.armorTotal=unitStat(item,STAT_ARMORCLASS,0);
                colorOf(unit,item,1,out.invTrans,out.invColor);colorOf(unit,item,0,out.chrTrans,out.chrColor);
                const int percent=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,uint16_t)>(common,10520)(item,STAT_ITEM_REQ_PERCENT,0);
                const bool ethereal=(data->dwItemFlags&0x00400000)!=0; // IFLAG_ETHEREAL
                if(record) {
                    out.reqStr=record->wReqStr?record->wReqStr+int32_t(int64_t(record->wReqStr)*percent/100)-(ethereal?10:0):0;
                    out.reqDex=record->wReqDex?record->wReqDex+int32_t(int64_t(record->wReqDex)*percent/100)-(ethereal?10:0):0;
                }
                out.reqLevel=ordinal<int(__stdcall*)(D2UnitStrc*,D2UnitStrc*)>(common,10757)(item,unit);
                BOOL strength=FALSE,dexterity=FALSE,level=FALSE;
                ordinal<BOOL(__stdcall*)(D2UnitStrc*,D2UnitStrc*,BOOL,BOOL*,BOOL*,BOOL*)>(common,10756)(item,unit,FALSE,&strength,&dexterity,&level);
                out.reqMet=(strength?1u:0u)|(dexterity?2u:0u)|(level?4u:0u);
            }
            {   // The magic properties (see Item::stats): state 0, flag 0x40 (STATLIST_MAGIC); a stat's layer is its parameter.
                if(auto* magic=ordinal<D2StatListStrc*(__stdcall*)(D2UnitStrc*,int,int)>(common,10484)(item,0,0x40)) {
                    D2StatStrc stats[64]{};
                    const int count=ordinal<int(__stdcall*)(D2StatListStrc*,D2StatStrc*,int)>(common,11243)(magic,stats,64);
                    for(int k=0;k<count&&out.statCount<maxItemStats;++k)out.stats[out.statCount++]={stats[k].nStat,stats[k].nLayer,stats[k].nValue};
                }
                // A set item's bonuses in force: the game gives the item a list for an item-set state when enough pieces are
                // worn (the state bit itself stays off on the item), so the lists that exist are the bonuses in force.
                for(int state=STATE_ITEMSET1;state<=STATE_ITEMSET5;++state)
                    if(auto* bonus=ordinal<D2StatListStrc*(__stdcall*)(D2UnitStrc*,int,int)>(common,10484)(item,state,0)) {
                            D2StatStrc stats[32]{};
                            const int count=ordinal<int(__stdcall*)(D2StatListStrc*,D2StatStrc*,int)>(common,11243)(bonus,stats,32);
                            for(int k=0;k<count&&out.statCount+out.setStatCount<maxItemStats;++k)out.stats[out.statCount+out.setStatCount++]={stats[k].nStat,stats[k].nLayer,stats[k].nValue};
                        }
            }
            found.push_back(out);
        }
        auto distance=[&](const Item& it){if(it.mode!=3&&it.mode!=5)return int64_t(-1);const int64_t dx=int64_t(it.x)*65536-output.x,dy=int64_t(it.y)*65536-output.y;return dx*dx+dy*dy;};
        std::stable_sort(found.begin(),found.end(),[&](const auto& a,const auto& b){return distance(a)<distance(b);});
        output.itemCount=uint32_t(std::min<size_t>(found.size(),maxItems));
        for(uint32_t i=0;i<output.itemCount;++i)output.items[i]=found[i];
    }
    // Game objects near the player, nearest first.
    {
        std::vector<Object> found;
        for(int32_t guid=1;guid<=int32_t(gameInfo.dwLastUsedUnitGUID[UNIT_OBJECT])&&guid<=3000;++guid) {
            int32_t type=UNIT_OBJECT,id=guid;D2UnitInfoStrc info2{};
            if(ordinal<int32_t(__stdcall*)(uint16_t,int32_t,D2UnitInfoStrc*,int32_t*,int32_t*)>(game,10018)(gameId,1,&info2,&type,&id)!=1)continue;
            const int64_t dx=int64_t(info2.nX)-(output.x>>16),dy=int64_t(info2.nY)-(output.y>>16);
            if(dx*dx+dy*dy>90*90)continue;
            Object out;out.guid=uint32_t(guid);out.classId=uint32_t(info2.nClassId);out.mode=uint32_t(info2.nAnimMode);out.x=info2.nX;out.y=info2.nY;
            found.push_back(out);
        }
        // Level entrances and exits (tiles): cave mouths, stairs, crypt doors. Their unit class is the lvlwarp.txt row.
        for(int32_t guid=1;guid<=int32_t(gameInfo.dwLastUsedUnitGUID[UNIT_TILE])&&guid<=3000;++guid) {
            auto tile=unitOf(UNIT_TILE,guid);
            if(!tile||!tile->pStaticPath)continue;
            const int32_t tx=tile->pStaticPath->tGameCoords.nX,ty=tile->pStaticPath->tGameCoords.nY;
            const int64_t dx=int64_t(tx)-(output.x>>16),dy=int64_t(ty)-(output.y>>16);
            if(dx*dx+dy*dy>90*90)continue;
            Object out;out.guid=uint32_t(guid);out.classId=uint32_t(tile->dwClassId);out.x=tx;out.y=ty;out.flags=1;
            found.push_back(out);
        }
        auto distance=[&](const Object& o){const int64_t dx=int64_t(o.x)-(output.x>>16),dy=int64_t(o.y)-(output.y>>16);return dx*dx+dy*dy;};
        std::sort(found.begin(),found.end(),[&](const auto& a,const auto& b){return distance(a)<distance(b);});
        output.objectCount=uint32_t(std::min<size_t>(found.size(),maxObjects));
        for(uint32_t i=0;i<output.objectCount;++i)output.objects[i]=found[i];
    }
    // Stats and skills of the player.
    {
        auto statValue=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common,10519);
        for(uint32_t s=0;s<statCount;++s)output.stats[s]=statValue(unit,int32_t(s),0);
        auto potionPrediction=[&](int32_t state,int32_t current,int32_t maximum,int32_t perFrame) {
            if(maximum<=0||perFrame<=0||!unit->pGame)return int32_t(0);
            auto stateList=ordinal<D2StatListStrc*(__stdcall*)(D2UnitStrc*,int32_t)>(common,10480);
            auto expireFrame=ordinal<int32_t(__stdcall*)(D2StatListStrc*)>(common,10473);
            D2StatListStrc* list=stateList(unit,state);
            if(!list)return int32_t(0);
            const int32_t frames=std::max(0,expireFrame(list)-unit->pGame->dwGameFrame);
            const int64_t predicted=int64_t(current)+int64_t(frames)*perFrame;
            return int32_t(std::clamp<int64_t>(predicted,0,maximum));
        };
        if(const int32_t hpRegen=statValue(unit,STAT_HPREGEN,0);hpRegen>0)
            output.potionLife=potionPrediction(STATE_HEALTHPOT,output.stats[STAT_HITPOINTS],output.stats[STAT_MAXHP],hpRegen);
        if(output.stats[STAT_MAXMANA]>0) {
            D2CharStatsTxt* charStats=nullptr;
            if(unit->dwClassId<7) {
                D2DataTablesStrc* tables=*ordinal<D2DataTablesStrc**>(common,10042);
                if(tables&&tables->pCharStatsTxt)charStats=tables->pCharStatsTxt+unit->dwClassId;
            }
            if(charStats) {
                int32_t manaRegen=25*charStats->nManaRegen;
                if(!manaRegen)manaRegen=7500;
                int32_t manaMultiplier=output.stats[STAT_MAXMANA]/manaRegen;
                if(manaMultiplier<1)manaMultiplier=1;
                const int32_t frameMultiplier=manaMultiplier*(statValue(unit,STAT_MANARECOVERYBONUS,0)+100)/100+statValue(unit,STAT_MANARECOVERY,0);
                output.potionMana=potionPrediction(STATE_MANAPOT,output.stats[STAT_MANA],output.stats[STAT_MAXMANA],frameMultiplier);
            }
        }
        auto* playerData=ordinal<D2PlayerDataStrc*(__stdcall*)(D2UnitStrc*)>(common,10424)(unit);
        if(playerData){output.leftSkill=playerData->nLeftSkillId;output.rightSkill=playerData->nRightSkillId;}
        auto firstSkill=ordinal<void*(__stdcall*)(void*)>(common,10948);
        auto nextSkill=ordinal<void*(__stdcall*)(void*)>(common,10947);
        auto skillId=ordinal<int(__stdcall*)(void*,const char*,int)>(common,10963);
        auto skillLevel=ordinal<int(__stdcall*)(D2UnitStrc*,void*,int)>(common,10968);
        auto skillQuantity=ordinal<int(__stdcall*)(void*)>(common,11011);
        if(unit->pSkills)for(void* skill=firstSkill(unit->pSkills);skill&&output.skillCount<maxSkills;skill=nextSkill(skill)) {
            Skill out;out.id=uint32_t(skillId(skill,__FILE__,__LINE__));out.level=skillLevel(unit,skill,1);out.baseLevel=skillLevel(unit,skill,0);out.quantity=skillQuantity(skill);
            if(out.level>0||out.baseLevel>0||out.quantity>0)output.skills[output.skillCount++]=out;
        }
    }
    // The character sheet, by the server's own rules without the dice: the damage roll of SUnitDmg.cpp (weapon or bare
    // hands, +damage, damage percent with strength/dexterity bonus and weapon mastery, min/max damage percent) and the attack
    // rating of its hit check (D2Common's rate, raised by the mastery's and items' to-hit percent).
    {
        auto stat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common,10519);
        auto itemStat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,uint16_t)>(common,10520);
        auto baseStat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,uint16_t)>(common,10521);
        auto mastery=ordinal<int(__stdcall*)(D2UnitStrc*,D2UnitStrc*,void*,int)>(common,11023);
        auto* weapon=ordinal<D2UnitStrc*(__stdcall*)(D2UnitStrc*,BOOL)>(common,10434)(unit,TRUE);
        auto& sheet=output.sheet;
        int32_t low=0,high=0,percent=stat(unit,STAT_DAMAGEPERCENT,0);
        if(weapon) {
            const bool twoHanded=ordinal<int(__stdcall*)(D2UnitStrc*,D2InventoryStrc*)>(common,10291)(unit,unit->pInventory)==2;
            low=stat(unit,twoHanded?STAT_SECONDARY_MINDAMAGE:STAT_MINDAMAGE,0)<<8;high=stat(unit,twoHanded?STAT_SECONDARY_MAXDAMAGE:STAT_MAXDAMAGE,0)<<8;
            if(const int bonus=ordinal<short(__stdcall*)(D2UnitStrc*)>(common,10807)(weapon))percent+=bonus*stat(unit,STAT_STRENGTH,0)/100;
            if(const int bonus=ordinal<short(__stdcall*)(D2UnitStrc*)>(common,10808)(weapon))percent+=bonus*stat(unit,STAT_DEXTERITY,0)/100;
            percent+=mastery(unit,weapon,nullptr,1);
        } else {
            low=std::max(stat(unit,STAT_MINDAMAGE,0),1)<<8;high=std::max(stat(unit,STAT_MAXDAMAGE,0),2)<<8;
            percent+=stat(unit,STAT_STRENGTH,0);
        }
        const int32_t plus=itemStat(unit,STAT_ITEM_NORMALDAMAGE,0)<<8;
        // The damage of an attack with a skill: the weapon's part of it (skills.txt SrcDam, 128 = all) and the skill's own
        // damage percent (DmgSymPerCalc) join the rest.
        auto damageWith=[&](int32_t sourcePart,int32_t skillPercent,int32_t& outLow,int32_t& outHigh) {
            int32_t l=int32_t(int64_t(low)*sourcePart/128),h=int32_t(int64_t(high)*sourcePart/128);
            const int32_t p=std::max(percent+skillPercent,-90);
            l=std::max(l+plus,256);h=std::max(h+plus,l+256);
            l+=int32_t(int64_t(l)*(p+stat(unit,STAT_ITEM_MINDAMAGE_PERCENT,0))/100);
            h+=int32_t(int64_t(h)*(p+stat(unit,STAT_ITEM_MAXDAMAGE_PERCENT,0))/100);
            outLow=l>>8;outHigh=h>>8;
        };
        damageWith(128,0,sheet.damageMin,sheet.damageMax);
        const int32_t rate=ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10432)(unit);
        const int32_t toHitPercent=(weapon?mastery(unit,weapon,nullptr,0):0)+itemStat(unit,STAT_ITEM_TOHIT_PERCENT,0);
        sheet.attackRating=rate+int32_t(int64_t(rate)*toHitPercent/100);
        // The same for the left and right skills, as the sheet's two rows show them; skills that don't strike with the weapon
        // (spells) leave their rows empty.
        if(auto* playerData=ordinal<D2PlayerDataStrc*(__stdcall*)(D2UnitStrc*)>(common,10424)(unit)) {
            D2DataTablesStrc* tables=*ordinal<D2DataTablesStrc**>(common,10042);
            const int32_t skills[2]={playerData->nLeftSkillId,playerData->nRightSkillId};
            for(int side=0;side<2;++side) {
                const int32_t id=skills[side];
                if(id<0||id>=tables->nSkillsTxtRecordCount)continue;
                const auto& record=tables->pSkillsTxt[id];
                if(!record.nSrcDam)continue;
                auto* skill=ordinal<void*(__stdcall*)(D2UnitStrc*,int)>(common,10950)(unit,id);
                const int level=skill?ordinal<int(__stdcall*)(D2UnitStrc*,void*,int)>(common,10968)(unit,skill,1):0;
                const int32_t skillPercent=record.dwDmgSymPerCalc!=uint32_t(-1)?ordinal<int(__stdcall*)(D2UnitStrc*,unsigned,int,int)>(common,11276)(unit,record.dwDmgSymPerCalc,id,level):0;
                damageWith(record.nSrcDam,skillPercent,sheet.skillDamageMin[side],sheet.skillDamageMax[side]);
                const int32_t skillToHit=ordinal<int(__stdcall*)(D2UnitStrc*,int,int)>(common,10999)(unit,id,level);
                sheet.skillAttack[side]=rate+int32_t(int64_t(rate)*(toHitPercent+skillToHit)/100);
            }
        }
        sheet.defense=ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10431)(unit);
        // Resistances lose 40 in Nightmare and 100 in Hell, and stop at 75 plus the max resistance items give.
        const int32_t penalty=unit->pGame?(unit->pGame->nDifficulty==1?40:unit->pGame->nDifficulty==2?100:0):0;
        static const int resists[4][2]={{STAT_FIRERESIST,STAT_MAXFIRERESIST},{STAT_COLDRESIST,STAT_MAXCOLDRESIST},{STAT_LIGHTRESIST,STAT_MAXLIGHTRESIST},{STAT_POISONRESIST,STAT_MAXPOISONRESIST}};
        for(int i=0;i<4;++i) {
            sheet.resistMax[i]=std::min(75+stat(unit,resists[i][1],0),95); // D2Client 0x6FAD0C87: never above 95, nor below -100
            sheet.resist[i]=std::max(std::min(stat(unit,resists[i][0],0)-penalty,sheet.resistMax[i]),-100);
        }
        static const int attributes[4]={STAT_STRENGTH,STAT_ENERGY,STAT_DEXTERITY,STAT_VITALITY};
        for(int i=0;i<4;++i)sheet.baseAttribute[i]=baseStat(unit,attributes[i],0);
        sheet.baseMax[0]=baseStat(unit,STAT_MAXHP,0);sheet.baseMax[1]=baseStat(unit,STAT_MAXMANA,0);sheet.baseMax[2]=baseStat(unit,STAT_MAXSTAMINA,0);
    }
    {   // What the game sent this client since its last snapshot.
        auto& log=gServerLogs[client];
        for(size_t at=0;at+2<=log.size();) {
            const size_t length=size_t(log[at])|(size_t(log[at+1])<<8);
            if(output.serverBytes+2+length>maxServerBytes)break;
            if(const uint8_t* p=log.data()+at+2;length>=1&&p[0]==0x4F)peer.hirelings.clear();
            else if(length>=7&&p[0]==0x4E&&peer.hirelings.size()<maxHirelings)
                peer.hirelings.push_back({uint16_t(p[1]|(p[2]<<8)),uint32_t(p[3]|(p[4]<<8)|(p[5]<<16)|(uint32_t(p[6])<<24))});
            std::memcpy(output.server+output.serverBytes,log.data()+at,2+length);output.serverBytes+=uint32_t(2+length);at+=2+length;
        }
        log.clear();
    }
    // The player's hireling, as its panel shows it (D2Client 0x6FAEDA20 and the stats of its value table 0x6FB795F8).
    if(auto* hireling=named<D2UnitStrc*(__cdecl*)(D2UnitStrc*)>(game,"D2GameHost_GetHireling")(unit)) {
        auto stat=ordinal<int32_t(__stdcall*)(const D2UnitStrc*,int,uint16_t)>(common,10519);
        auto base=ordinal<int(__stdcall*)(D2UnitStrc*,int,uint16_t)>(common,10521);
        auto& h=output.hireling;h.guid=hireling->dwUnitId;
        h.level=stat(hireling,STAT_LEVEL,0);h.experience=stat(hireling,STAT_EXPERIENCE,0);h.nextExperience=stat(hireling,STAT_NEXTEXP,0);
        h.strength=stat(hireling,STAT_STRENGTH,0);h.dexterity=stat(hireling,STAT_DEXTERITY,0);
        h.baseStrength=base(hireling,STAT_STRENGTH,0);h.baseDexterity=base(hireling,STAT_DEXTERITY,0);
        h.life=stat(hireling,STAT_HITPOINTS,0)>>8;h.maxLife=stat(hireling,STAT_MAXHP,0)>>8;h.defense=stat(hireling,STAT_ARMORCLASS,0);
        {   // Damage as D2Client 0x6FAEDB97 works it out from what the server sends of the hireling (base damage plus the
            // secondary, MonsterAI's D2GAME_MERCS_SendStat): the bonus percent is damage% plus, with a weapon in the right
            // hand, the weapon's strength and dexterity bonuses and mastery (a two-handed one's own damage taken out of the base and
            // the secondary added; its normal damage added after), or without one the dexterity (Act I rogues) or strength; at least -90.
            const int32_t baseMin=base(hireling,STAT_MINDAMAGE,0)+base(hireling,STAT_SECONDARY_MINDAMAGE,0);
            const int32_t baseMax=base(hireling,STAT_MAXDAMAGE,0)+base(hireling,STAT_SECONDARY_MAXDAMAGE,0);
            int32_t percent=stat(hireling,STAT_DAMAGEPERCENT,0),minimum=baseMin,maximum=baseMax,flat=0;
            D2UnitStrc* weapon=hireling->pInventory?ordinal<D2UnitStrc*(__stdcall*)(D2InventoryStrc*,int)>(common,10257)(hireling->pInventory,4):nullptr;
            if(weapon) {
                if(ordinal<BOOL(__stdcall*)(D2UnitStrc*)>(common,10765)(weapon)) {
                    minimum=baseMin-stat(weapon,STAT_MINDAMAGE,0)+stat(hireling,STAT_SECONDARY_MINDAMAGE,0);
                    maximum=baseMax-stat(weapon,STAT_MAXDAMAGE,0)+stat(hireling,STAT_SECONDARY_MAXDAMAGE,0);
                }
                if(const int bonus=int16_t(ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10807)(weapon)))percent+=stat(hireling,STAT_STRENGTH,0)*bonus/100;
                if(const int bonus=int16_t(ordinal<int(__stdcall*)(D2UnitStrc*)>(common,10808)(weapon)))percent+=stat(hireling,STAT_DEXTERITY,0)*bonus/100;
                percent+=ordinal<int(__stdcall*)(D2UnitStrc*,D2UnitStrc*,void*,int)>(common,11023)(hireling,weapon,nullptr,1);
                flat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,uint16_t)>(common,10520)(weapon,STAT_ITEM_NORMALDAMAGE,0);
            } else percent+=stat(hireling,hireling->dwClassId==271?STAT_DEXTERITY:STAT_STRENGTH,0);
            percent=std::max(percent,-90);
            h.minDamage=(stat(hireling,STAT_ITEM_MINDAMAGE_PERCENT,0)+percent+100)*minimum/100+flat;
            h.maxDamage=(stat(hireling,STAT_ITEM_MAXDAMAGE_PERCENT,0)+percent+100)*maximum/100+flat;
        }
        h.resist[0]=stat(hireling,STAT_FIRERESIST,0);h.resist[1]=stat(hireling,STAT_COLDRESIST,0);h.resist[2]=stat(hireling,STAT_LIGHTRESIST,0);h.resist[3]=stat(hireling,STAT_POISONRESIST,0);
    }
    // The mercenaries on offer, with what D2Client shows of each (0x6FAF5028): MONSTERS_HirelingInit for this player, in the
    // act MONSTERS_GetActFromHirelingTxt gives the name.
    if(unit->pGame)for(const auto& [name,seed]:peer.hirelings) {
        D2HirelingInitStrc init{};
        const BOOL expansion=unit->pGame->bExpansion?TRUE:FALSE;
        const int act=ordinal<int(__fastcall*)(BOOL,int,uint16_t)>(common,11086)(expansion,0,name);
        if(!ordinal<int(__fastcall*)(BOOL,D2UnitStrc*,int,int,int,D2HirelingInitStrc*)>(common,11082)(expansion,unit,int(seed),act,unit->pGame->nDifficulty,&init))continue;
        if(std::getenv("D2MODERN_HIRE_LOG"))std::cout<<"Hireling init: name "<<name<<" seed "<<seed<<" act "<<act<<" diff "<<int(unit->pGame->nDifficulty)<<" exp "<<expansion<<" player level "<<ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common,10519)(unit,STAT_LEVEL,0)<<" -> id "<<init.nId<<" level "<<init.nLevel<<" desc "<<init.nHireDesc<<std::endl;
        // HireDesc as a string id, as MONSTERS_GetHirelingDescString (#11085) has it: Fire Arrow, Cold Arrow, Combat, Defensive
        // Auras, Offensive Auras, Fire / Cold / Lightning Spells.
        static constexpr int16_t descriptions[9]={0,3371,3372,11090,11088,11089,3377,3376,3375};
        output.hirelings[output.hirelingCount++]={name,init.nHireDesc>=0&&init.nHireDesc<9?descriptions[init.nHireDesc]:int16_t(0),init.nLevel,init.nHitpoints,init.nDefense,init.nGold,init.nMinDamage,init.nMaxDamage,init.nResist};
    }
}
// A client's request becomes the packets its D2 client would send (the host steps the game afterwards, for everyone).
void GameSession::apply(const Request& request,int32_t client) {
    if(request.signature!=magic)throw std::runtime_error("Invalid native protocol");
    if(request.operation==Operation::Walk) {
        if(request.x<0||request.y<0||request.x>65535||request.y>65535)throw std::runtime_error("Invalid native target");
#pragma pack(push,1)
        struct {int32_t client=1;D2GSPacketClt03 packet{};} move;
#pragma pack(pop)
        move.client=client;move.packet.nHeader=request.z?0x03:0x01;move.packet.nPosX=uint16_t(request.x);move.packet.nPosY=uint16_t(request.y);
        push_(1,&move,sizeof(move));
    } else if(request.operation==Operation::Attack) {
#pragma pack(push,1)
        struct {int32_t client=1;D2GSPacketClt06 packet{};} attack;
#pragma pack(pop)
        attack.client=client;attack.packet.nHeader=0x06;attack.packet.dwUnitType=UNIT_MONSTER;attack.packet.dwUnitGUID=uint32_t(request.x);
        push_(1,&attack,sizeof(attack));
    } else if(request.operation==Operation::Pickup) {
#pragma pack(push,1)
        struct {int32_t client=1;D2GSPacketClt16 packet{};} pickup;
#pragma pack(pop)
        pickup.client=client;pickup.packet.nHeader=0x16;pickup.packet.dwUnitType=UNIT_ITEM;pickup.packet.dwUnitGUID=uint32_t(request.x);pickup.packet.bCursor=0;
        push_(1,&pickup,sizeof(pickup));
    } else if(request.operation==Operation::Packet) {
        const uint32_t header=uint32_t(request.x)&255,size=(uint32_t(request.x)>>8)&255;
        if(size<1||size>17)throw std::runtime_error("Invalid native packet size");
        uint8_t bytes[4+17]{};std::memcpy(bytes,&client,4);bytes[4]=uint8_t(header);
        const int32_t words[4]={request.y,request.z,request.w,request.v};std::memcpy(bytes+5,words,std::min<size_t>(16,size-1));
        if(header==0x38&&words[0]>=1&&words[0]<=2)peers_[client].vendorGuid=words[1];
        if(header==0x30)peers_[client].vendorGuid=0;
        if(std::getenv("D2MODERN_PACKET_LOG")){std::printf("Packet 0x%02X size %u: %d %d %d %d (client %d)\n",header,size,words[0],words[1],words[2],words[3],client);std::fflush(stdout);}
        push_(1,bytes,int(4+size));
    } else if(request.operation==Operation::Respawn) {
#pragma pack(push,1)
        struct {int32_t client=1;uint8_t header=0x41;} respawn;
#pragma pack(pop)
        respawn.client=client;push_(1,&respawn,5);
    } else if(request.operation==Operation::Quit) {
        if(client!=1)leaveClient(client);
    } else if(request.operation==Operation::Cheat) {
        auto* unit=named<D2UnitStrc*(__cdecl*)(uint16_t,int32_t)>(game_,"D2GameHost_GetClientPlayer")(gameId_,client);
        if(!unit)return;
        auto getStat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t)>(common_,10519);
        auto setStat=ordinal<int32_t(__stdcall*)(D2UnitStrc*,int32_t,int32_t,int32_t)>(common_,10517);
        auto addGold=[&](int32_t stat,int32_t amount,unsigned limit) {
            const int32_t current=getStat(unit,stat,0);
            const int64_t cap=int64_t(limit);
            int64_t next=amount==0?cap:int64_t(current)+amount;
            if(next<0)next=0;
            if(next>cap)next=cap;
            setStat(unit,stat,int32_t(next),0);
        };
        if(request.x==0)addGold(14,request.y,ordinal<unsigned(__stdcall*)(D2UnitStrc*)>(common_,10439)(unit));
        else if(request.x==1)addGold(15,request.y,ordinal<unsigned(__stdcall*)(D2UnitStrc*)>(common_,10339)(unit));
        else if(request.x==2){setStat(unit,6,getStat(unit,7,0),0);setStat(unit,8,getStat(unit,9,0),0);}
        else if(request.x==6)setStat(unit,6,int32_t(int64_t(getStat(unit,7,0))*std::clamp(request.y,1,100)/100),0); // test: life to y% of its maximum
        else if(request.x==7) { // test: easy to hit (absorb tests). Monsters' chance to hit is 2*AR/(AR+defense)*alvl/(alvl+dlvl),
            // at least 5%: a level 99 player is hit by a level 1 monster that 5%. Level 1, and defense 0 through the base armour
            // class. Prints what still reduces damage.
            setStat(unit,STAT_LEVEL,1,0);
            setStat(unit,STAT_ARMORCLASS,getStat(unit,STAT_ARMORCLASS,0)-ordinal<int(__stdcall*)(D2UnitStrc*)>(common_,10431)(unit),0);
            std::cout<<"Cheat 7: damage reduction "<<getStat(unit,STAT_NORMAL_DAMAGE_REDUCTION,0)<<", physical resist "<<getStat(unit,STAT_DAMAGERESIST,0)
                <<"%, magic reduction "<<getStat(unit,STAT_MAGIC_DAMAGE_REDUCTION,0)<<", fire resist "<<getStat(unit,STAT_FIRERESIST,0)<<"\n";
        }
        else if(request.x==8&&unit->pGame&&unit->pDynamicPath&&unit->pDynamicPath->pRoom) { // test: the act's time of day to cycle y (0-5)
            static const int begins[6]={320,340,0,160,180,200}; // D2Common gNormalEnvironmentCycle nTicksBegin, in degrees
            const int cycle=std::clamp(request.y,0,5);
            void* room=unit->pDynamicPath->pRoom;
            const int act=ordinal<uint8_t(__stdcall*)(int)>(common_,10001)(ordinal<int(__stdcall*)(void*)>(common_,10057)(room));
            if(void* pAct=act>=0&&act<5?unit->pGame->pAct[act]:nullptr) {
                const int rate=ordinal<int(__stdcall*)(void*)>(common_,10934)(pAct);
                ordinal<void(__stdcall*)(void*,void*,int,int,BOOL)>(common_,10932)(pAct,room,cycle,begins[cycle]*rate,FALSE);
            }
        }
        else if(request.x==10) { // test: everything carried with durability is worn down to half
            auto first=ordinal<D2UnitStrc*(__stdcall*)(D2InventoryStrc*)>(common_,10277);
            auto next=ordinal<D2UnitStrc*(__stdcall*)(D2UnitStrc*)>(common_,10304);
            auto maxDurability=ordinal<int32_t(__stdcall*)(D2UnitStrc*)>(common_,10567);
            for(D2UnitStrc* item=unit->pInventory?first(unit->pInventory):nullptr;item;item=next(item))
                if(const int32_t most=maxDurability(item);most>1)setStat(item,STAT_DURABILITY,most/2,0);
        }
        else if(request.x==9)named<void(__cdecl*)(D2UnitStrc*)>(game_,"D2GameHost_KillHireling")(unit); // test: the hireling dies
        else if(request.x==3)named<D2UnitStrc*(__cdecl*)(D2UnitStrc*,uint32_t,int32_t)>(game_,"D2GameHost_GiveItem")(unit,uint32_t(request.y),request.z?request.z:2);
        else if(request.x==4) {
            auto classSkill=ordinal<int(__stdcall*)(int,int)>(common_,11041);
            auto assign=ordinal<void(__stdcall*)(D2UnitStrc*,int,int,int,const char*,int)>(common_,10953);
            const int classId=unit->dwClassId;
            const int count=ordinal<int(__stdcall*)(int)>(common_,11042)(classId);
            for(int i=0;i<count;++i) {
                const int skillId=classSkill(classId,i);
                if(skillId>=0)assign(unit,skillId,20,0,"cheat",0);
            }
            ordinal<void(__stdcall*)(D2UnitStrc*)>(common_,10941)(unit);
        } else if(request.x==5) {
            const int classId=unit->dwClassId;
            const int level=getStat(unit,STAT_LEVEL,0);
            const int maxLevel=ordinal<int(__stdcall*)(int)>(common_,10629)(classId);
            if(level>0&&level<maxLevel) {
                const uint32_t current=uint32_t(getStat(unit,STAT_EXPERIENCE,0));
                const uint32_t next=ordinal<uint32_t(__stdcall*)(int,uint32_t)>(common_,10628)(classId,uint32_t(level+1));
                if(next>current)named<void(__cdecl*)(D2UnitStrc*,uint32_t)>(game_,"D2GameHost_AddExperience")(unit,next-current);
            }
        }
    } else if(request.operation!=Operation::Tick)throw std::runtime_error("Unknown native request");
}
bool GameSession::handle(const Request& request,Reply& out) {
    if(request.signature!=magic)throw std::runtime_error("Invalid native protocol");
    if(request.operation==Operation::Quit){leave();return false;}
    apply(request,1);
    step();++ticks_;describe(out,1);
    return true;
}
}
std::unique_ptr<Session> Session::start(const Settings& settings,Reply& first) {
    auto session=std::make_unique<GameSession>(settings);
    session->describe(first);
    return session;
}
}
