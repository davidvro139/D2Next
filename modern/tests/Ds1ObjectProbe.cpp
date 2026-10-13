// Lists a DS1's preset units (what the map itself places). usage: Ds1ObjectProbe GAME_DIR DS1_PATH
#include "Assets.h"
#include <iostream>
int main(int argc,char** argv){try{
    d2::Archives a(argv[1]);const auto map=d2::decodeDs1(a.read(argv[2]));
    std::cout<<argv[2]<<": version "<<map.version<<", act "<<map.act<<", "<<map.width<<"x"<<map.height<<" tiles, "<<map.objectList.size()<<" units\n";
    for(const auto& o:map.objectList)std::cout<<"  "<<(o.type==1?"monster":o.type==2?"object":"type "+std::to_string(o.type))<<" "<<o.id<<" at "<<o.x<<","<<o.y<<" flags "<<o.flags<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
