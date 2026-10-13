// Walks the real player across the Act 1 wilderness by pathfinding over the generated scene, in hops the game's pathfinder can reach,
// swapping scenes when the game moves the player to a level outside the current one. usage: NativeWalkTests DLL_DIR GAME_DIR SAVE LEVEL...
// (LEVEL: the levels to walk through in order, each reached by walking to the middle of its rectangle)
#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <cstdlib>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){try {
    if(argc<5)throw std::runtime_error("Usage: NativeWalkTests DLL_DIR GAME_DIR SAVE LEVEL...");
    _putenv_s("D2MODERN_TEST_WAYPOINTS","1");
    const uint32_t seed=0x12345678;
    d2::Archives archives(argv[2]);
    auto scene=d2::loadNativeTown(argv[1],argv[2],archives,seed,{1,2,3,4});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],seed,scene.collision,scene.worldOrigin,scene.levels);
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    d2::LevelTable levelTable(archives);
    // D2WALK_START=N: begin by waypoint travel to level N (all Act I waypoints are known in this session).
    if(const char* start=std::getenv("D2WALK_START")) {
        auto bytes=archives.read("data\\global\\excel\\objects.txt");
        d2::TextTable objects(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
        uint32_t waypointGuid=0;for(const auto& o:backend.snapshot().objects)if(objects.cell(o.classId,"Name")=="Waypoint")waypointGuid=o.id;
        require(waypointGuid!=0,"no waypoint near the start");
        backend.apply(id,d2::Command::interactObject(int32_t(waypointGuid)));for(int t=0;t<150;++t)backend.tick();
        const int level=std::atoi(start);
        backend.apply(id,d2::Command::raw(0x49|(9<<8),int32_t(waypointGuid),level,0,0));
        for(int t=0;t<200&&backend.snapshot().level!=uint32_t(level);++t)backend.tick();
        require(backend.snapshot().level==uint32_t(level),"waypoint travel failed");
        scene=d2::loadNativeTown(argv[1],argv[2],archives,seed,{level},true);
        require(backend.setScene(scene.collision,scene.worldOrigin,scene.levels),"setScene refused");
        for(int t=0;t<10;++t)backend.tick();
        std::cout<<"started on level "<<level<<" ("<<scene.levels.size()<<" levels in the scene)\n";
        if(std::getenv("D2WALK_OBJECTS"))for(const auto& o:backend.snapshot().objects)std::cout<<"object "<<o.id<<" class "<<o.classId<<" mode "<<o.mode<<" world "<<o.x+scene.worldOrigin.x<<","<<o.y+scene.worldOrigin.y<<"\n";
        const auto here0=d2::subtileOf(backend.snapshot().player().position);
        std::cout<<"player local subtile "<<here0.x<<","<<here0.y<<" world "<<here0.x+scene.worldOrigin.x<<","<<here0.y+scene.worldOrigin.y<<"\n";
    }
    for(int a=4;a<argc;++a) {
        const int target=std::stoi(argv[a]);
        // Middle of the level's rectangle from levels.txt-independent generator data: ask the generator through a one-level scene.
        auto probe=d2::loadNativeTown(argv[1],argv[2],archives,seed,{target});
        d2::GridPoint goalWorld{probe.worldOrigin.x+int(probe.collision.width)/2,probe.worldOrigin.y+int(probe.collision.height)/2};
        std::cout<<"walking to the middle of level "<<target<<" ("<<levelTable.byId(target)->name<<") at world "<<goalWorld.x<<","<<goalWorld.y<<"\n";
        int stuck=0;d2::GridPoint last{-1,-1};
        for(int tick=0;tick<20000;++tick) {
            auto view=backend.snapshot();
            if(view.level!=0&&std::find(scene.levels.begin(),scene.levels.end(),int(view.level))==scene.levels.end()) {
                std::cout<<"  level "<<view.level<<": loading its scene\n";
                scene=d2::loadNativeTown(argv[1],argv[2],archives,seed,{int(view.level)},true);
                require(backend.setScene(scene.collision,scene.worldOrigin,scene.levels),"setScene refused");
                continue;
            }
            const auto here=d2::subtileOf(view.player().position);
            const d2::GridPoint worldHere{here.x+scene.worldOrigin.x,here.y+scene.worldOrigin.y};
            if(int(view.level)==target&&std::abs(worldHere.x-goalWorld.x)<40&&std::abs(worldHere.y-goalWorld.y)<40){std::cout<<"  reached level "<<target<<" near its middle\n";for(const auto& t:view.tiles)std::cout<<"    entrance: warp row "<<t.classId<<" at world "<<t.x+scene.worldOrigin.x<<","<<t.y+scene.worldOrigin.y<<"\n";break;}
            if(tick%15==0) {
                // Path over the current scene toward the goal when it is inside it, else toward the nearest walkable point in its direction.
                d2::GridPoint goal{goalWorld.x-scene.worldOrigin.x,goalWorld.y-scene.worldOrigin.y};
                for(int r=1;r<=60&&!scene.collision.walkable(goal);r+=1)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){const d2::GridPoint p{goalWorld.x-scene.worldOrigin.x+dx,goalWorld.y-scene.worldOrigin.y+dy};if(scene.collision.walkable(p)){goal=p;dx=dy=r+1;}}
                auto path=d2::findPath(scene.collision,here,scene.collision.walkable(goal)?goal:here);
                if(path.size()>=2) {
                    const auto hop=path[std::min<size_t>(path.size()-1,size_t(std::getenv("D2WALK_HOP")?std::atoi(std::getenv("D2WALK_HOP")):12))];
                    backend.apply(id,d2::Command::moveTo(hop));
                }
                if(here==last&&!view.player().moving){if(++stuck>40){for(int dy=-12;dy<=12;++dy){std::string row;for(int dx=-24;dx<=24;++dx){const d2::GridPoint p{here.x+dx,here.y+dy};row+=(dx==0&&dy==0)?'P':(dx==goal.x-here.x&&dy==goal.y-here.y)?'G':scene.collision.walkable(p)?'.':scene.collision.ground[size_t(std::clamp(p.y,0,int(scene.collision.height)-1))*scene.collision.width+size_t(std::clamp(p.x,0,int(scene.collision.width)-1))]?'#':' ';}std::cout<<row<<"\n";}}if(stuck>40)throw std::runtime_error("the player is stuck at world "+std::to_string(worldHere.x)+","+std::to_string(worldHere.y)+" on level "+std::to_string(view.level));}else stuck=0;
                last=here;
            }
            backend.tick();
            if(tick%400==0)std::cout<<"  tick "<<tick<<" level "<<view.level<<" at "<<worldHere.x<<","<<worldHere.y<<"\n";
            if(tick==19999)throw std::runtime_error("did not reach the level in time");
        }
    }
    // With D2WALK_ENTER: take the nearest level entrance on the level the walk ended on.
    if(std::getenv("D2WALK_ENTER")) {
        auto view=backend.snapshot();require(!view.tiles.empty(),"no level entrance is near the player");
        const auto& tile=view.tiles.front();const auto levelBefore=view.level;
        std::cout<<"entering warp row "<<tile.classId<<" from level "<<levelBefore<<"\n";
        require(backend.apply(id,d2::Command::interactObject(int32_t(tile.id)))==d2::CommandResult::Accepted,"entrance rejected");
        for(int tick=0;tick<1500&&backend.snapshot().level==levelBefore;++tick)backend.tick();
        require(backend.snapshot().level!=levelBefore,"the game did not move the player through the entrance");
        std::cout<<"  now on level "<<backend.snapshot().level<<" ("<<levelTable.byId(int32_t(backend.snapshot().level))->name<<")\n";
        scene=d2::loadNativeTown(argv[1],argv[2],archives,seed,{int(backend.snapshot().level)},true);
        require(backend.setScene(scene.collision,scene.worldOrigin,scene.levels),"setScene refused");
        for(int tick=0;tick<10;++tick)backend.tick();
        const auto here=d2::subtileOf(backend.snapshot().player().position);
        require(scene.collision.walkable(here),"the player does not stand on walkable ground after the entrance");
        std::cout<<"  standing on walkable ground of the new level\n";
    }
    // Then to each far end of the scene the player can reach: what does the game do at the edge?
    if(std::getenv("D2WALK_EDGES")) {
        const d2::GridPoint dirs[]={{1,1},{1,-1},{-1,1},{-1,-1}};
        for(const auto& dir:dirs) {
            auto view=backend.snapshot();const auto here=d2::subtileOf(view.player().position);
            d2::GridPoint best=here;long bestScore=-1000000000;
            for(int y=0;y<int(scene.collision.height);y+=3)for(int x=0;x<int(scene.collision.width);x+=3){d2::GridPoint p{x,y};if(!scene.collision.walkable(p))continue;const long score=long(x)*dir.x+long(y)*dir.y;if(score>bestScore&&d2::findPath(scene.collision,here,p).size()>1){bestScore=score;best=p;}}
            std::cout<<"edge ("<<dir.x<<","<<dir.y<<"): heading to world "<<best.x+scene.worldOrigin.x<<","<<best.y+scene.worldOrigin.y<<" on level "<<view.level<<"\n";
            const auto levelBefore=view.level;int stuck=0;d2::GridPoint last{-1,-1};
            for(int tick=0;tick<6000;++tick) {
                view=backend.snapshot();const auto now=d2::subtileOf(view.player().position);
                if(view.level!=levelBefore){std::cout<<"  the game moved the player to level "<<view.level<<" at scene subtile "<<now.x<<","<<now.y<<"\n";break;}
                if(tick%15==0){auto path=d2::findPath(scene.collision,now,best);if(path.size()>=2)backend.apply(id,d2::Command::moveTo(path[std::min<size_t>(path.size()-1,size_t(std::getenv("D2WALK_HOP")?std::atoi(std::getenv("D2WALK_HOP")):12))]));if(now==last&&!view.player().moving){if(++stuck>30){std::cout<<"  stopped at world "<<now.x+scene.worldOrigin.x<<","<<now.y+scene.worldOrigin.y<<" still on level "<<view.level<<"\n";break;}}else stuck=0;last=now;}
                backend.tick();
            }
            break; // one edge per run keeps the scene consistent
        }
    }
    std::cout<<"PASS: walked through every requested level\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
