#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    const auto view=session.backend->snapshot();
    d2::StringTable strings(archives);d2::ItemTable items(archives,strings);
    d2::ItemArt art(archives,d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")),items);
    for(const auto& i:view.items){const auto* info=items.find(i.code);const auto* img=art.get(i.code,i.quality,i.invGraphic);
        std::cout<<"item "<<i.id<<" "<<d2::ItemTable::codeString(i.code)<<" '"<<items.displayName(i.code,i.quality,i.prefix,i.suffix,i.rarePrefix,i.rareSuffix,i.fileIndex)<<"' mode "<<i.mode<<" page "<<i.page<<" at "<<i.x<<","<<i.y<<" body "<<i.bodyLoc<<" inv "<<(info?info->invFile:"?")<<" image "<<(img?std::to_string(img->width)+"x"+std::to_string(img->height):"none")<<"\n";}
    std::cout<<"gold "<<view.vitals.gold<<" stash "<<view.vitals.stashGold<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
