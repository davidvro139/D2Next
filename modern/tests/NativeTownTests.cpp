#include "NativeTown.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static uint64_t hash(const d2::NativeTown& town) {
    uint64_t h=14695981039346656037ull;
    auto mix=[&](uint64_t value){h=(h^value)*1099511628211ull;};
    mix(town.rooms);mix(town.worldOrigin.x);mix(town.worldOrigin.y);
    const auto& s=town.scene;
    mix(s.width);mix(s.height);mix(s.cellsX);mix(s.cellsY);mix(s.originX);mix(s.originY);
    for(const auto& tile:s.tiles) {
        mix(tile.image.width);mix(tile.image.height);mix(tile.originX);mix(tile.originY);
        mix(tile.orientation);mix(tile.mainIndex);mix(tile.subIndex);mix(tile.rarity);
        for(auto b:tile.collision)mix(b);
        for(auto b:tile.image.rgba)mix(b);
    }
    for(const auto& p:s.placements) {
        require(p.tile<s.tiles.size(),"placement references invalid tile");
        const auto& tile=s.tiles[p.tile];
        require(p.x>=0&&p.y>=0&&uint64_t(p.x)+tile.image.width<=s.width&&uint64_t(p.y)+tile.image.height<=s.height,"placement outside scene");
        require(p.cellX<s.cellsX&&p.cellY<s.cellsY,"placement outside grid");
        mix(p.tile);mix(p.x);mix(p.y);mix(p.cellX);mix(p.cellY);mix(p.shadow);
    }
    mix(town.collision.width);mix(town.collision.height);
    for(auto b:town.collision.flags)mix(b);
    for(auto b:town.collision.ground)mix(b);
    return h;
}
int wmain(int argc,wchar_t** argv) {try {
    if(argc!=3)throw std::runtime_error("Usage: NativeTownTests <x64 DLL directory> <game directory>");
    const auto cwd=std::filesystem::current_path();
    d2::Archives archives(argv[2]);
    auto first=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678);
    require(first.rooms>0 && first.scene.placements.size()>1000,"empty native town");
    require(first.collision.walkable(d2::findSpawn(first.collision)),"town has no walkable spawn");
    const auto originalHash=hash(first);
    auto second=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678);
    require(hash(second)==originalHash,"same seed after unload/reload differs");
    auto other=d2::loadNativeTown(argv[1],argv[2],archives,0xbeef);
    require(hash(other)!=originalHash,"different seed did not change generated town");
    auto river=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{107});
    require(river.rooms>0 && river.scene.placements.size()>1000,"River of Flame native scene did not load");
    require(river.worldOrigin.x>=0 && river.worldOrigin.y>=0,"River of Flame retained invalid level origin");
    require(hash(first)==originalHash,"retained scene changed after native reload");
    require(std::filesystem::current_path()==cwd,"bridge changed working directory");
    for(auto name:{L"D2Common.dll",L"D2CMP.dll",L"D2Lang.dll",L"Fog.dll",L"Storm.dll"})
        require(!GetModuleHandleW(name),"native module remains loaded");
    std::cout<<"PASS: native generation, seed repeatability/variation, owned snapshots, tile bounds and module cleanup; hash "<<originalHash<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
