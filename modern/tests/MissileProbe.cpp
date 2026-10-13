// Walks to the Blood Moor, learns Holy Bolt (a paladin projectile skill) and casts it at the nearest monster, watching
// for the resulting missile entity. usage: MissileProbe DLL_DIR GAME_DIR SAVE
#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)throw std::runtime_error("Usage: MissileProbe DLL_DIR GAME_DIR SAVE");
    _putenv_s("D2MODERN_TEST_EXP","30000"); // enough for several level-ups (Holy Bolt needs character level 6)
    const std::vector<int> levels{1,2,3,4};
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,levels);
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,levels);
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    // Same route NativeAreaTests uses to leave the Rogue Encampment for the Blood Moor.
    const d2::GridPoint route[]={{5812,4474},{5814,4506},{5814,4530},{5814,4580},{5814,4610},{5830,4608},{5838,4624},{5840,4656},{5846,4688},{5870,4720}};
    std::vector<d2::GridPoint> hops;d2::GridPoint previous{5813,4443};
    for(const auto& waypoint:route) {
        const int steps=std::max(1,int(std::ceil(std::max(std::abs(waypoint.x-previous.x),std::abs(waypoint.y-previous.y))/14.0)));
        for(int s=1;s<=steps;++s)hops.push_back({previous.x+(waypoint.x-previous.x)*s/steps,previous.y+(waypoint.y-previous.y)*s/steps});
        previous=waypoint;
    }
    for(const auto& waypoint:hops) {
        const d2::GridPoint local{waypoint.x-town.worldOrigin.x,waypoint.y-town.worldOrigin.y};
        if(!town.collision.walkable(local))continue;
        backend.apply(id,d2::Command::moveTo(local));
        for(int tick=0;tick<400;++tick){backend.tick();if(!backend.snapshot().player().moving&&tick>5)break;}
    }
    require(backend.snapshot().level==2,"player did not reach the Blood Moor");
    auto view=backend.snapshot();
    std::cout<<"level "<<view.stats[12]<<" skill points "<<view.stats[5]<<"\n";
    require(view.stats[5]>0,"no skill points available; increase D2MODERN_TEST_EXP");
    constexpr int holyBolt=101;
    backend.apply(id,d2::Command::raw(0x3B|(3<<8),holyBolt,0,0,0)); // learn Holy Bolt
    for(int t=0;t<5;++t)backend.tick();
    backend.apply(id,d2::Command::raw(0x3C|(9<<8),holyBolt,-1,0,0)); // right skill
    for(int t=0;t<5;++t)backend.tick();
    view=backend.snapshot();
    require(view.rightSkill==holyBolt,"Holy Bolt was not selected as the right skill");
    std::cout<<"Holy Bolt learned and selected\n";
    d2::MissileTable missiles(archives);
    bool sawMissile=false;
    for(int tick=0;tick<2000&&!sawMissile;++tick) {
        view=backend.snapshot();
        if(tick%30==0) {
            const d2::EntitySnapshot* nearest=nullptr;double best=1e18;
            for(const auto& e:view.entities) {
                if(e.kind!=d2::EntityKind::Enemy)continue;
                const double dx=(e.position.x-view.player().position.x)/double(d2::fixedOne),dy=(e.position.y-view.player().position.y)/double(d2::fixedOne);
                if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=&e;}
            }
            if(nearest)backend.apply(id,d2::Command::castOn(true,int32_t(nearest->id-1000)));
        }
        backend.tick();
        for(const auto& e:backend.snapshot().entities)if(e.kind==d2::EntityKind::Missile) {
            sawMissile=true;
            const auto* info=missiles.find(e.classId);
            std::cout<<"missile entity "<<e.id<<" class "<<e.classId<<" ("<<(info?info->celFile:"?")<<") facing "<<int(e.facing)<<"\n";
        }
    }
    require(sawMissile,"Holy Bolt never produced a missile entity");
    std::cout<<"PASS: cast Holy Bolt at a Blood Moor monster and saw its missile entity through the backend\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
