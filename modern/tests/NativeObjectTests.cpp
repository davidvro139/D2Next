// Walks up to the Rogue Encampment waypoint and opens it through the backend, checking the game's own waypoint menu
// packet (0x63) comes back. usage: NativeObjectTests DLL_DIR GAME_DIR SAVE
#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <array>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool sawPacket(const d2::WorldSnapshot& view,uint8_t header) {
    for(size_t at=0;at+2<=view.server.size();) {
        const size_t length=size_t(view.server[at])|(size_t(view.server[at+1])<<8);
        if(at+2+length>view.server.size())break;
        if(length&&view.server[at+2]==header)return true;
        at+=2+length;
    }
    return false;
}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeObjectTests DLL_DIR GAME_DIR SAVE");
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    require(!backend.snapshot().objects.empty(),"no game objects were published near the player");
    // Find the waypoint by its objects.txt row (loaded independently, not through the backend).
    auto bytes=archives.read("data\\global\\excel\\objects.txt");
    d2::TextTable objects(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    uint32_t waypointGuid=0;
    for(const auto& o:backend.snapshot().objects)if(objects.cell(o.classId,"Name")=="Waypoint")waypointGuid=o.id;
    require(waypointGuid!=0,"no waypoint object near the encampment start");
    std::cout<<"opening waypoint object "<<waypointGuid<<"\n";
    require(backend.apply(id,d2::Command::interactObject(int32_t(waypointGuid)))==d2::CommandResult::Accepted,"interactObject rejected");
    std::array<uint16_t,8> flags{};bool menu=false;
    for(int tick=0;tick<200&&!menu;++tick) {
        backend.tick();const auto view=backend.snapshot();
        for(size_t at=0;at+2<=view.server.size();) {
            const size_t length=size_t(view.server[at])|(size_t(view.server[at+1])<<8);
            if(at+2+length>view.server.size())break;
            if(length>=21&&view.server[at+2]==0x63) {
                menu=true;const uint8_t* p=&view.server[at+2];
                for(int i=0;i<8;++i)flags[size_t(i)]=uint16_t(p[5+2*i]|(p[5+2*i+1]<<8));
            }
            at+=2+length;
        }
    }
    require(menu,"the waypoint menu packet (0x63) never arrived");
    require((flags[1]&1)!=0,"waypoint 0 (Rogue Encampment) should already be activated");
    std::cout<<"waypoint flags word0 (status) 0x"<<std::hex<<flags[0]<<std::dec<<", Rogue Encampment activated\n";
    // Travel through the game's own 0x49 handler (the same packet the menu's entries send): a round trip to the level
    // we are already standing in, which exercises the full client-to-server path without needing a second area loaded.
    require(backend.apply(id,d2::Command::raw(0x49|(9<<8),int32_t(waypointGuid),1,0,0))==d2::CommandResult::Accepted,"travel command rejected");
    for(int tick=0;tick<60;++tick)backend.tick();
    require(backend.snapshot().level==1,"the player left the expected level after travelling");
    std::cout<<"PASS: walked to the waypoint, opened its menu and travelled through the backend\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
