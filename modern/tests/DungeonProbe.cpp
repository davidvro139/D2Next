// Tries to generate the scene of the given Act 1 levels (comma separated, e.g. 1,2,3,4) and draws its ground as a coarse map.
// usage: DungeonProbe DLL_DIR GAME_DIR SEED LEVELS [neighbours]
#include "NativeTown.h"
#include <chrono>
#include <iostream>
#include <sstream>
int wmain(int argc,wchar_t** argv){
    d2::Archives archives(argv[2]);
    std::vector<int> levels;{std::wstringstream in(argv[4]);std::wstring item;while(std::getline(in,item,L','))levels.push_back(std::stoi(item));}
    try{
        const auto start=std::chrono::steady_clock::now();
        auto town=d2::loadNativeTown(argv[1],argv[2],archives,uint32_t(std::stoul(argv[3])),levels,argc>5);
        size_t walkable=0;for(auto g:town.collision.ground)walkable+=g;
        std::cout<<town.levels.size()<<" level(s): "<<town.rooms<<" rooms, "<<town.scene.placements.size()<<" tiles, "<<walkable<<" ground subtiles, origin "<<town.worldOrigin.x<<","<<town.worldOrigin.y<<", "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
        const auto& g=town.collision;const int step=std::max(10,int(g.width)/100);
        for(int y=0;y<int(g.height);y+=step){std::string row;for(int x=0;x<int(g.width);x+=step){int ground=0,total=0;for(int dy=0;dy<step&&y+dy<int(g.height);++dy)for(int dx=0;dx<step&&x+dx<int(g.width);++dx){++total;ground+=g.ground[size_t(y+dy)*g.width+x+dx];}row+=ground==0?' ':ground*2>total?'#':'.';}std::cout<<row<<"\n";}
    }catch(const std::exception& e){std::cout<<e.what()<<"\n";}
}
