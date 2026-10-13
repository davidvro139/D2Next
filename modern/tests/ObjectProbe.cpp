// Lists the game objects around the player at the start of the game. usage: ObjectProbe DLL_DIR GAME_DIR SAVE
#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    const auto view=session.backend->snapshot();
    auto bytes=archives.read("data\\global\\excel\\objects.txt");d2::TextTable objects(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    const auto player=d2::subtileOf(view.player().position);
    std::cout<<"player at "<<player.x<<","<<player.y<<" | "<<view.objects.size()<<" objects\n";
    for(const auto& o:view.objects)std::cout<<"object "<<o.id<<" class "<<o.classId<<" '"<<objects.cell(o.classId,"Name")<<"' token "<<objects.cell(o.classId,"Token")<<" mode "<<o.mode<<" at "<<o.x-player.x<<","<<o.y-player.y<<" selectable "<<objects.cell(o.classId,"Selectable0")<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
