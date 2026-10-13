#include "Assets.h"
#include <iostream>
#include <stdexcept>
static void require(bool b){if(!b)throw std::runtime_error("DT1 test failed");}
static void put(d2::Bytes& b,size_t at,uint32_t v){for(int i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(8*i));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}require(caught);}
static d2::Bytes fixture(uint16_t format,d2::Bytes payload){
    d2::Bytes b(392); put(b,0,7);put(b,4,6);put(b,268,1);put(b,272,276);
    put(b,300,12);put(b,304,3);b[316]=9;
    put(b,348,372);put(b,352,uint32_t(20+payload.size()));put(b,356,1);
    b[372]=0xfe;b[373]=0xff;b[374]=0xfd;b[375]=0xff;
    b[380]=uint8_t(format);b[381]=uint8_t(format>>8);
    put(b,382,uint32_t(payload.size()));put(b,388,20);
    b.insert(b.end(),payload.begin(),payload.end());return b;
}
int main(){try{
    d2::Palette pal{};pal[1]={11,22,33,255};pal[0]={0,0,0,255};
    auto b=fixture(1,d2::Bytes(256,1));auto t=d2::decodeDt1(b,pal,0);
    require(t.image.width==32&&t.image.height==15&&t.originX==-2&&t.originY==-3);
    require(t.mainIndex==12&&t.subIndex==3&&t.collision[0]==9);
    size_t opaque=0;for(size_t i=3;i<t.image.rgba.size();i+=4)if(t.image.rgba[i])++opaque;
    require(opaque==256&&t.image.rgba[3]==0&&t.image.rgba[14*4]==11);
    for(size_t n=0;n<b.size();++n)rejects([&]{d2::decodeDt1(std::span(b).first(n),pal,0);});
    auto r=fixture(0x2005,{1,2,0,1,0,0});auto image=d2::decodeDt1(r,pal,0).image;
    require(image.rgba[3]==0&&image.rgba[7]==255&&image.rgba[8]==11);
    auto bad=b;put(bad,388,0xffffffff);rejects([&]{d2::decodeDt1(bad,pal,0);});
    bad=b;put(bad,356,0xffffffff);rejects([&]{d2::decodeDt1(bad,pal,0);});
    bad=b;bad[380]=2;rejects([&]{d2::decodeDt1(bad,pal,0);});
    for(const auto& p:std::vector<d2::Bytes>{{33,0},{0,2,1},{1},{0,33}}){auto invalid=fixture(0x1001,p);rejects([&]{d2::decodeDt1(invalid,pal,0);});}
    rejects([&]{d2::decodeDt1(b,pal,1);});rejects([&]{d2::dt1Atlas(b,pal,1);});
    require(d2::dt1Atlas(b,pal,0).width==48);
    std::cout<<"PASS: DT1 isometric/RLE pixels, transparency, signed origins, metadata, truncation, invalid ranges/runs/formats\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
