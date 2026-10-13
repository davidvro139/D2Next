#include "Assets.h"
#include <iostream>
#include <stdexcept>
static void require(bool b){if(!b)throw std::runtime_error("DS1 test failed");}
static void append(d2::Bytes& b,uint32_t n){for(int i=0;i<4;++i)b.push_back(uint8_t(n>>(i*8)));}
static void put(d2::Bytes& b,size_t p,uint32_t n){for(int i=0;i<4;++i)b.at(p+i)=uint8_t(n>>(i*8));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}require(caught);}
int main(){try{
    d2::MapScene scene;scene.width=30;scene.height=10;
    d2::Tile tile;tile.image={10,10,d2::Bytes(400,255)};scene.tiles.push_back(tile);
    scene.placements={{0,20,0,2,0,false},{0,0,0,0,0,false},{0,10,0,1,0,true}};
    require(d2::visibleTiles(scene,10,0,20,10)==std::vector<uint32_t>{2});
    require(d2::visibleTiles(scene,9,-1,21,11)==std::vector<uint32_t>({0,1,2}));
    require(d2::visibleTiles(scene,0,10,30,20).empty());
    require(d2::visibleTiles(scene,5,0,5,10).empty());
    scene.placements.push_back({0,0,0,0,0,true});
    auto composed=d2::composeMap(scene);
    require(composed.rgba[0]==127&&composed.rgba[3]==255);
    require(composed.rgba[20*4]==255);
    d2::Bytes b;
    for(uint32_t n:{18,1,0,0,0,1})append(b,n);
    for(char c:std::string("C:\\d2\\data\\global\\tiles\\ACT1\\Town\\Floor.tg1"))b.push_back(uint8_t(c));b.push_back(0);
    size_t layers=b.size();append(b,1);append(b,1);
    append(b,0x01230301);append(b,0); // wall cells
    append(b,2);append(b,0); // wall orientation layer
    append(b,0x00100201);append(b,0x00100301); // floor cells
    append(b,0);append(b,0); // shadows
    append(b,1);for(uint32_t n:{1,5,2,3,0})append(b,n); // object
    auto map=d2::decodeDs1(b);
    require(map.width==2&&map.height==1&&map.objects==1);
    require(map.dependencies[0]=="data\\global\\tiles\\act1\\town\\floor.dt1");
    require(map.walls[0][0].orientation==2&&map.floors[0][1].value==0x00100301);
    require(map.shadows[0].orientation==13);
    for(size_t n=0;n<b.size();++n)rejects([&]{d2::decodeDs1(std::span(b).first(n));});
    auto bad=b;put(bad,4,0xffffffff);rejects([&]{d2::decodeDs1(bad);});
    bad=b;put(bad,20,129);rejects([&]{d2::decodeDs1(bad);});
    bad=b;put(bad,layers,5);rejects([&]{d2::decodeDs1(bad);});
    bad=b;put(bad,layers+4,3);rejects([&]{d2::decodeDs1(bad);});
    bad=b;put(bad,0,19);rejects([&]{d2::decodeDs1(bad);});
    // Version 4 uses one implicit floor and the old orientation remapping table.
    d2::Bytes old;for(uint32_t n:{4,0,0,0,1,1,3,1,0,0})append(old,n);
    auto legacy=d2::decodeDs1(old);require(legacy.walls[0][0].orientation==1);
    put(old,24,99);rejects([&]{d2::decodeDs1(old);});
    std::cout<<"PASS: DS1 dependency normalization, dimensions, layers, objects, orientation remapping, truncation and count limits\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
