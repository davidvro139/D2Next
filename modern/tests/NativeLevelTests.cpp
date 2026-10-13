// A level-up through the backend: the character gets experience (host hook D2MODERN_TEST_EXP), spends the stat point and the skill point
// the game grants, and selects the new skill. usage: NativeLevelTests DLL_DIR GAME_DIR SAVE   (the save must be a paladin)
#include "NativeGame.h"
#include "NativeTown.h"
#include <cstdlib>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeLevelTests DLL_DIR GAME_DIR SAVE");
    _putenv_s("D2MODERN_TEST_EXP","600");
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    for(int tick=0;tick<5;++tick)backend.tick();
    auto view=backend.snapshot();
    std::cout<<"level "<<view.stats[12]<<", experience "<<view.stats[13]<<", stat points "<<view.stats[4]<<", skill points "<<view.stats[5]<<", "<<view.skills.size()<<" skills\n";
    for(const auto& s:view.skills)std::cout<<"  skill "<<s.id<<" level "<<s.level<<" (base "<<s.baseLevel<<")\n";
    require(view.stats[12]==2,"the character did not reach level 2");
    require(view.stats[4]==5,"a level-up should grant 5 stat points");
    require(view.stats[5]==1,"a level-up should grant a skill point");
    const int strength=view.stats[0];
    backend.apply(id,d2::Command::raw(0x3A|(3<<8),0,0,0,0)); // strength
    for(int tick=0;tick<5;++tick)backend.tick();
    view=backend.snapshot();
    require(view.stats[0]==strength+1&&view.stats[4]==4,"the stat point was not spent on strength");
    constexpr int might=98;
    backend.apply(id,d2::Command::raw(0x3B|(3<<8),might,0,0,0));
    for(int tick=0;tick<5;++tick)backend.tick();
    view=backend.snapshot();
    bool learned=false;for(const auto& s:view.skills)learned|=s.id==might&&s.baseLevel==1;
    require(learned&&view.stats[5]==0,"the skill point was not spent on Might");
    // Selecting an aura as the right skill switches it on: Might adds damage percent (stat 25). Auras have no cast animation.
    const int damageBefore=backend.snapshot().stats[25];
    backend.apply(id,d2::Command::raw(0x3C|(9<<8),might,-1,0,0)); // right skill
    for(int tick=0;tick<8;++tick)backend.tick();
    view=backend.snapshot();
    require(view.rightSkill==might,"Might was not selected as the right skill");
    require(view.stats[25]>damageBefore,"Might did not raise the damage percentage");
    std::cout<<"Might selected: damage percent "<<damageBefore<<" -> "<<view.stats[25]<<std::endl;
    std::cout<<"PASS: level-up, stat point, skill point and skill selection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
