// Prints how the server's unit path directions relate to the movement direction seen on screen (for calibrating sprite facing).
#include "NativeGame.h"
#include "NativeTown.h"
#include <iostream>
#include <map>
#include <cmath>
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)return 2;
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678);
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin);
    auto& backend=*session.backend;
    std::map<d2::EntityId,std::vector<d2::EntitySnapshot>> history;
    std::map<std::pair<int,int>,int> pairs;
    for(int tick=0;tick<1500;++tick) {
        backend.apply(backend.snapshot().localPlayer,d2::Command::stop());backend.tick();
        for(const auto& e:backend.snapshot().entities) {
            if(e.kind==d2::EntityKind::Player)continue;
            auto& h=history[e.id];h.push_back(e);
            if(h.size()>10&&e.mode==2) {
                const auto& past=h[h.size()-11];
                const double dx=(e.position.x-past.position.x)/double(d2::fixedOne),dy=(e.position.y-past.position.y)/double(d2::fixedOne);
                if(std::abs(dx)<0.3&&std::abs(dy)<0.3)continue;
                const unsigned facing=d2::facingFor(std::abs(dx)<0.3?0:dx>0?1:-1,std::abs(dy)<0.3?0:dy>0?1:-1);
                ++pairs[{int(e.direction>>3),int(facing)}];
            }
        }
    }
    for(const auto& [k,v]:pairs)std::cout<<"server dir/8="<<k.first<<" movement facing="<<k.second<<" x"<<v<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
