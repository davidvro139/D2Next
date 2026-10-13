// Travels by waypoint from the Rogue Encampment into the Jail (a dungeon), generates that level's scene with the same seed, hands it to
// the backend and checks the player stands on walkable ground there and can walk. usage: NativeDungeonTests DLL_DIR GAME_DIR SAVE
#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <cstdlib>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeDungeonTests DLL_DIR GAME_DIR SAVE");
    _putenv_s("D2MODERN_TEST_WAYPOINTS","1");
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    auto bytes=archives.read("data\\global\\excel\\objects.txt");
    d2::TextTable objects(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    uint32_t waypointGuid=0;
    for(const auto& o:backend.snapshot().objects)if(objects.cell(o.classId,"Name")=="Waypoint")waypointGuid=o.id;
    require(waypointGuid!=0,"no waypoint object near the encampment start");
    backend.apply(id,d2::Command::interactObject(int32_t(waypointGuid)));
    for(int tick=0;tick<150;++tick)backend.tick();
    constexpr int jail=29;
    require(backend.apply(id,d2::Command::raw(0x49|(9<<8),int32_t(waypointGuid),jail,0,0))==d2::CommandResult::Accepted,"travel command rejected");
    for(int tick=0;tick<100&&backend.snapshot().level!=jail;++tick)backend.tick();
    require(backend.snapshot().level==jail,"waypoint travel did not reach the Jail");
    std::cout<<"the game moved the player to level "<<backend.snapshot().level<<"\n";
    // The client's job now: the scene of the new level, generated with the game's seed.
    auto dungeon=d2::loadNativeTown(argv[1],argv[2],archives,town.seed,{jail},true);
    require(backend.setScene(dungeon.collision,dungeon.worldOrigin,dungeon.levels),"backend refused the new scene");
    for(int tick=0;tick<5;++tick)backend.tick();
    auto view=backend.snapshot();const auto here=d2::subtileOf(view.player().position);
    std::cout<<"Jail scene: "<<dungeon.scene.placements.size()<<" tiles; player at subtile "<<here.x<<","<<here.y<<" | "<<view.entities.size()-1<<" other units\n";
    require(dungeon.collision.walkable(here),"the player does not stand on walkable ground of the generated level");
    // Walk somewhere else on the level and arrive.
    d2::GridPoint goal=here;
    for(int r=6;r<=14&&goal==here;++r)for(int dx=-r;dx<=r&&goal==here;++dx)for(int dy:{-r,r}){d2::GridPoint p{here.x+dx,here.y+dy};if(dungeon.collision.walkable(p)&&d2::findPath(dungeon.collision,here,p).size()>=4){goal=p;break;}}
    require(goal!=here,"no walkable destination near the arrival point");
    require(backend.apply(id,d2::Command::moveTo(goal))==d2::CommandResult::Accepted,"walk in the dungeon rejected");
    for(int tick=0;tick<200&&d2::subtileOf(backend.snapshot().player().position)!=goal;++tick)backend.tick();
    require(d2::subtileOf(backend.snapshot().player().position)==goal,"the player did not arrive in the dungeon");
    std::cout<<"PASS: waypoint travel into the Jail, its scene generated and the player walks on it\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
