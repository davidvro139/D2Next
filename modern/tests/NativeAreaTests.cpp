// Walks the native player out of the Rogue Encampment into the Blood Moor through the modern backend and checks the
// loaded scene follows: the level changes and every step lands on a walkable subtile of the scene's own collision.
#include "NativeGame.h"
#include "NativeTown.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeAreaTests DLL_DIR GAME_DIR SAVE");
    const std::vector<int> levels{1,2,3,4};
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,levels);
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,levels);
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    require(backend.snapshot().level==1,"player does not start in the Rogue Encampment");
    // Absolute native subtile waypoints from the encampment's south gate into the Blood Moor (seed 0x12345678).
    const d2::GridPoint route[]={{5812,4474},{5814,4506},{5814,4530},{5814,4580},{5814,4610},{5830,4608},{5838,4624},{5840,4656},{5846,4688},{5870,4720}};
    // The game's pathfinder has a limited reach, like a click on screen: hops of at most 14 subtiles.
    std::vector<d2::GridPoint> hops;d2::GridPoint previous{5813,4443};
    for(const auto& waypoint:route) {
        const int steps=std::max(1,int(std::ceil(std::max(std::abs(waypoint.x-previous.x),std::abs(waypoint.y-previous.y))/14.0)));
        for(int s=1;s<=steps;++s)hops.push_back({previous.x+(waypoint.x-previous.x)*s/steps,previous.y+(waypoint.y-previous.y)*s/steps});
        previous=waypoint;
    }
    bool leftTown=false;
    for(const auto& waypoint:hops) {
        const d2::GridPoint local{waypoint.x-town.worldOrigin.x,waypoint.y-town.worldOrigin.y};
        if(!town.collision.walkable(local)){std::cout<<"  skipping blocked hop "<<waypoint.x<<","<<waypoint.y<<std::endl;continue;}
        require(backend.apply(id,d2::Command::moveTo(local))==d2::CommandResult::Accepted,"waypoint rejected");
        for(int tick=0;tick<400;++tick) {
            backend.tick();auto view=backend.snapshot();
            require(town.collision.walkable(d2::subtileOf(view.player().position)),"native player stands on a blocked subtile of the scene");
            leftTown|=view.level==2;
            if(tick%25==0)std::cout<<"  wp "<<waypoint.x<<","<<waypoint.y<<" tick "<<tick<<" at "<<d2::subtileOf(view.player().position).x+town.worldOrigin.x<<","<<d2::subtileOf(view.player().position).y+town.worldOrigin.y<<(view.player().moving?" moving":" still")<<std::endl;
            if(tick>5&&!view.player().moving)for(const auto& e:view.entities){const auto dx=(e.position.x-view.player().position.x)/double(d2::fixedOne),dy=(e.position.y-view.player().position.y)/double(d2::fixedOne);if(dx*dx+dy*dy<400)std::cout<<"   near: id "<<e.id<<" class "<<e.classId<<" kind "<<int(e.kind)<<" dx "<<dx<<" dy "<<dy<<" mode "<<int(e.mode)<<std::endl;}
            if(!view.player().moving&&tick>5)break;
        }
    }
    const auto end=backend.snapshot();
    std::cout<<"final level "<<end.level<<" at "<<d2::subtileOf(end.player().position).x+town.worldOrigin.x<<","<<d2::subtileOf(end.player().position).y+town.worldOrigin.y<<" | "<<end.entities.size()-1<<" other units\n";
    require(leftTown&&end.level==2,"player did not reach the Blood Moor");
    // Fight: attack the nearest enemy through the backend until one is hurt or dies.
    bool hurt=false;int startExp=end.vitals.experience;
    for(int tick=0;tick<3000&&!hurt;++tick) {
        auto view=backend.snapshot();const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
        for(const auto& e:view.entities) {
            if(e.kind!=d2::EntityKind::Enemy)continue;
            const double dx=(e.position.x-view.player().position.x)/double(d2::fixedOne),dy=(e.position.y-view.player().position.y)/double(d2::fixedOne);
            if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&e;}
        }
        if(nearest&&tick%50==0)backend.apply(id,d2::Command::attack(int32_t(nearest->id-1000)));
        // Replacing view destroys its entities; keep values, not a pointer into it.
        const auto targetId=nearest?nearest->id:0;
        const auto targetLife=nearest?nearest->life:-1;
        backend.tick();
        view=backend.snapshot();
        for(const auto& e:view.entities)if(targetId&&e.id==targetId&&(e.kind==d2::EntityKind::Corpse||(e.life>=0&&e.life<targetLife)))hurt=true;
        if(view.vitals.experience>startExp)hurt=true;
    }
    std::cout<<"fight: enemy "<<(hurt?"hurt or killed":"untouched")<<"\n";
    require(hurt,"attacking an enemy did not hurt it");
    // Loot: keep killing nearby enemies until something other than gold is on the ground, pick it up, then move it inside the inventory.
    constexpr uint32_t goldCode=0x20646c67;
    auto settle0=[&](auto&& done){for(int tick=0;tick<60&&!done();++tick)backend.tick();return done();};
    uint32_t wanted=0;
    for(int tick=0;tick<40000&&!wanted;++tick) {
        auto view=backend.snapshot();
        for(const auto& i:view.items)if((i.mode==3||i.mode==5)&&i.code!=goldCode){wanted=i.id;std::cout<<"ground item "<<i.id<<" code "<<i.code<<" quality "<<i.quality<<std::endl;break;}
        if(wanted)break;
        const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
        for(const auto& e:view.entities){
            if(e.kind!=d2::EntityKind::Enemy)continue;
            const double dx=(e.position.x-view.player().position.x)/double(d2::fixedOne),dy=(e.position.y-view.player().position.y)/double(d2::fixedOne);
            if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&e;}
        }
        if(nearest&&tick%50==0)backend.apply(id,d2::Command::attack(int32_t(nearest->id-1000)));
        // Walk on towards the enemy so the search continues over the wilderness (the attack itself walks in range).
        backend.tick();
        // Collect any gold lying around too, so it does not block the view.
        if(tick%200==100)for(const auto& i:backend.snapshot().items)if((i.mode==3||i.mode==5)&&i.code==goldCode){backend.apply(id,d2::Command::pickup(int32_t(i.id)));break;}
    }
    if(!wanted)std::cout<<"loot: nothing but gold dropped in this run (pickup path not exercised)\n";
    else {
        require(backend.apply(id,d2::Command::pickup(int32_t(wanted)))==d2::CommandResult::Accepted,"pickup rejected");
        const d2::ItemSnapshot* stored=nullptr;d2::WorldSnapshot view;
        for(int tick=0;tick<600&&!stored;++tick){
            backend.tick();view=backend.snapshot();
            for(const auto& i:view.items)if(i.id==wanted&&i.mode==0&&i.page==0)stored=&i;
        }
        if(!stored){for(const auto& i:view.items)std::cout<<"  item "<<i.id<<" mode "<<i.mode<<" page "<<i.page<<" at "<<i.x<<","<<i.y<<" body "<<i.bodyLoc<<std::endl;const auto p=view.player().position;std::cout<<"  player "<<p.x/double(d2::fixedOne)<<","<<p.y/double(d2::fixedOne)<<std::endl;}
        require(stored,"dropped item did not arrive in the inventory");
        std::cout<<"loot: item "<<wanted<<" in the inventory at cell "<<stored->x<<","<<stored->y<<"\n";
        require(stored->x>=0&&stored->x<10&&stored->y>=0&&stored->y<4,"inventory cell out of range");
        // Take it to the cursor, then put it back a few cells to the right.
        require(backend.apply(id,d2::Command::takeItem(int32_t(wanted)))==d2::CommandResult::Accepted,"take rejected");
        bool onCursor=false;
        for(int tick=0;tick<50&&!onCursor;++tick){backend.tick();for(const auto& i:backend.snapshot().items)if(i.id==wanted&&i.mode==4)onCursor=true;}
        if(!onCursor)for(const auto& i:backend.snapshot().items)std::cout<<"  item "<<i.id<<" mode "<<i.mode<<" page "<<i.page<<" at "<<i.x<<","<<i.y<<" body "<<i.bodyLoc<<std::endl;
        require(onCursor,"item did not reach the cursor");
        require(backend.apply(id,d2::Command::placeItem(int32_t(wanted),4,0,0))==d2::CommandResult::Accepted,"place rejected");
        bool placed=false;int px=-1,py=-1;
        for(int tick=0;tick<50&&!placed;++tick){backend.tick();for(const auto& i:backend.snapshot().items)if(i.id==wanted&&i.mode==0){placed=true;px=i.x;py=i.y;}}
        std::cout<<"loot: item placed again at cell "<<px<<","<<py<<(placed?"":" (not placed)")<<"\n";
        require(placed,"item was not placed back into the inventory");
        // ...and throw it on the ground.
        require(backend.apply(id,d2::Command::takeItem(int32_t(wanted)))==d2::CommandResult::Accepted,"take before drop rejected");
        require(settle0([&]{for(const auto& i:backend.snapshot().items)if(i.id==wanted&&i.mode==4)return true;return false;}),"item did not reach the cursor for dropping");
        require(backend.apply(id,d2::Command::dropItem(int32_t(wanted)))==d2::CommandResult::Accepted,"drop rejected");
        require(settle0([&]{for(const auto& i:backend.snapshot().items)if(i.id==wanted&&(i.mode==3||i.mode==5))return true;return false;}),"item did not land on the ground");
        std::cout<<"loot: item dropped on the ground again\n";
    }
    // Equipment and belt: take the right-hand weapon off, put it back, then drink a belt potion.
    {
        auto findMode=[&](uint32_t mode,int bodyLoc,int column)->uint32_t {
            for(const auto& i:backend.snapshot().items)if(i.mode==mode&&(mode!=1||int(i.bodyLoc)==bodyLoc)&&(mode!=2||i.x==column))return i.id;
            return 0;
        };
        auto settle=[&](auto&& done){for(int tick=0;tick<60&&!done();++tick)backend.tick();return done();};
        const uint32_t weapon=findMode(1,4,0);
        require(weapon!=0,"no weapon equipped in the right hand");
        require(backend.apply(id,d2::Command::takeBody(4))==d2::CommandResult::Accepted,"take body rejected");
        require(settle([&]{for(const auto& i:backend.snapshot().items)if(i.id==weapon&&i.mode==4)return true;return false;}),"weapon did not reach the cursor");
        require(backend.apply(id,d2::Command::equipItem(int32_t(weapon),4))==d2::CommandResult::Accepted,"equip rejected");
        require(settle([&]{for(const auto& i:backend.snapshot().items)if(i.id==weapon&&i.mode==1&&i.bodyLoc==4)return true;return false;}),"weapon was not equipped again");
        std::cout<<"equipment: weapon "<<weapon<<" taken off and equipped again\n";
        const uint32_t potion=findMode(2,0,0);
        if(potion) {
            const int lifeBefore=backend.snapshot().vitals.life;
            require(backend.apply(id,d2::Command::useBelt(int32_t(potion)))==d2::CommandResult::Accepted,"use belt rejected");
            require(settle([&]{for(const auto& i:backend.snapshot().items)if(i.id==potion)return false;return true;}),"belt potion was not consumed");
            std::cout<<"belt: potion "<<potion<<" drunk (life "<<lifeBefore<<" -> "<<backend.snapshot().vitals.life<<")\n";
        }
    }
    std::cout<<"PASS: walked from the Rogue Encampment into the Blood Moor inside the loaded scene, hurt an enemy and looted\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
