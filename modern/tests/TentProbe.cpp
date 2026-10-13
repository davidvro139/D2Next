// Lists the tile placements around a world subtile of the generated scene. usage: TentProbe DLL_DIR GAME_DIR SEED LEVEL WORLD_X WORLD_Y RADIUS_TILES
#include "NativeTown.h"
#include <fstream>
#include <iostream>
#include <map>
static void writeBmp(const std::string& path,const d2::Image& img) {
    std::ofstream out(path,std::ios::binary);
    const uint32_t header=54,pixels=img.width*img.height*4,size=header+pixels;
    out.write("BM",2);out.write(reinterpret_cast<const char*>(&size),4);
    const uint32_t zero=0;out.write(reinterpret_cast<const char*>(&zero),4);out.write(reinterpret_cast<const char*>(&header),4);
    const uint32_t dib=40;const int32_t w=int32_t(img.width),h=int32_t(img.height);
    out.write(reinterpret_cast<const char*>(&dib),4);out.write(reinterpret_cast<const char*>(&w),4);out.write(reinterpret_cast<const char*>(&h),4);
    const uint16_t planes=1,bpp=32;out.write(reinterpret_cast<const char*>(&planes),2);out.write(reinterpret_cast<const char*>(&bpp),2);
    out.write(reinterpret_cast<const char*>(&zero),4);out.write(reinterpret_cast<const char*>(&pixels),4);
    out.write(reinterpret_cast<const char*>(&zero),4);out.write(reinterpret_cast<const char*>(&zero),4);
    out.write(reinterpret_cast<const char*>(&zero),4);out.write(reinterpret_cast<const char*>(&zero),4);
    for(uint32_t y=img.height;y-->0;)for(uint32_t x=0;x<img.width;++x) {
        const size_t i=(size_t(y)*img.width+x)*4;
        const uint8_t a=img.rgba[i+3];
        out.put(char(a?img.rgba[i+2]:255));out.put(char(a?img.rgba[i+1]:0));out.put(char(a?img.rgba[i]:255));out.put(char(255));
    }
}
int wmain(int argc,wchar_t** argv){try{
    d2::Archives archives(argv[2]);
    auto town=d2::loadNativeTown(argv[1],argv[2],archives,uint32_t(std::stoul(argv[3])),{std::stoi(argv[4])});
    if(argc>8&&std::wstring(argv[8])==L"all"){std::map<std::pair<int,int>,int> seen;for(const auto& t:town.scene.tiles)if(t.roofHeight)++seen[{int(t.orientation),t.roofHeight}];for(const auto& [k,n]:seen)std::cout<<"orientation "<<k.first<<" roofHeight "<<k.second<<": "<<n<<" tiles\n";return 0;}
    if(argc>8&&std::wstring(argv[8])==L"scan"){
        // Every wall/roof placement keyed by cell; report any same-orientation-family neighbour pair whose py does not
        // step by the expected +/-40 per cell (the isometric grid constant), which is what a genuine misalignment looks like.
        std::map<std::pair<int,int>,std::vector<size_t>> byCell;
        for(size_t i=0;i<town.scene.placements.size();++i){const auto& p=town.scene.placements[i];if(!p.shadow)byCell[{int(p.cellX),int(p.cellY)}].push_back(i);}
        int anomalies=0;
        for(const auto& [cell,list]:byCell) {
            for(auto pi:list) {
                const auto& p=town.scene.placements[pi];const auto& t=town.scene.tiles[p.tile];
                if(t.orientation==0)continue;
                for(auto [dx,dy]:{std::pair{1,0},std::pair{0,1}}) {
                    auto it=byCell.find({cell.first+dx,cell.second+dy});if(it==byCell.end())continue;
                    for(auto pj:it->second) {
                        const auto& q=town.scene.placements[pj];const auto& u=town.scene.tiles[q.tile];
                        if(u.orientation!=t.orientation)continue; // only compare like with like (same wall family)
                        const int expectedY=40,expectedX=dx?80:-80;
                        const int actualX=q.x-p.x,actualY=q.y-p.y;
                        if(actualY!=expectedY||actualX!=expectedX){std::cout<<"ANOMALY cell "<<cell.first<<","<<cell.second<<" (orient "<<t.orientation<<" main "<<t.mainIndex<<" sub "<<t.subIndex<<" px "<<p.x<<" py "<<p.y<<") -> cell "<<cell.first+dx<<","<<cell.second+dy<<" (orient "<<u.orientation<<" main "<<u.mainIndex<<" sub "<<u.subIndex<<" px "<<q.x<<" py "<<q.y<<"): dx "<<actualX<<" (expect "<<expectedX<<") dy "<<actualY<<" (expect "<<expectedY<<")\n";++anomalies;}
                    }
                }
            }
        }
        std::cout<<anomalies<<" anomalies out of "<<town.scene.placements.size()<<" placements\n";
        return 0;
    }
    if(argc>8&&std::wstring(argv[8])==L"walls"){
        for(const auto& p:town.scene.placements) {
            const auto& t=town.scene.tiles[p.tile];
            if(t.orientation==0||t.orientation==15||p.shadow)continue;
            std::cout<<"cell "<<p.cellX<<","<<p.cellY<<" orient "<<t.orientation<<" main "<<t.mainIndex<<" sub "<<t.subIndex<<" originY "<<t.originY<<" size "<<t.image.width<<"x"<<t.image.height<<" py "<<p.y<<"\n";
        }
        return 0;
    }
    if(argc>8&&std::wstring(argv[8])==L"white"){
        for(size_t index=0;index<town.scene.placements.size();++index) {
            const auto& p=town.scene.placements[index];const auto& t=town.scene.tiles[p.tile];
            size_t opaque=0,white=0;
            for(size_t i=0;i<t.image.rgba.size();i+=4)if(t.image.rgba[i+3]){++opaque;if(t.image.rgba[i]>220&&t.image.rgba[i+1]>220&&t.image.rgba[i+2]>220)++white;}
            if(white>200)std::cout<<"#"<<index<<" cell "<<p.cellX<<","<<p.cellY<<" orient "<<t.orientation<<" main "<<t.mainIndex<<" sub "<<t.subIndex<<" size "<<t.image.width<<"x"<<t.image.height<<" origin "<<t.originX<<","<<t.originY<<(p.shadow?" SHADOW":"")<<" px "<<p.x<<","<<p.y<<" white "<<white<<" opaque "<<opaque<<"\n";
        }
        return 0;
    }
    if(argc>8&&std::wstring(argv[8])==L"bright"){
        for(size_t index=0;index<town.scene.placements.size();++index) {
            const auto& p=town.scene.placements[index];const auto& t=town.scene.tiles[p.tile];
            size_t opaque=0,bright=0;
            for(size_t i=0;i<t.image.rgba.size();i+=4)if(t.image.rgba[i+3]){++opaque;if(t.image.rgba[i]>170&&t.image.rgba[i+1]>170&&t.image.rgba[i+2]>170)++bright;}
            if(bright>200)std::cout<<"#"<<index<<" cell "<<p.cellX<<","<<p.cellY<<" orient "<<t.orientation<<" main "<<t.mainIndex<<" sub "<<t.subIndex<<" size "<<t.image.width<<"x"<<t.image.height<<" origin "<<t.originX<<","<<t.originY<<(p.shadow?" SHADOW":"")<<" px "<<p.x<<","<<p.y<<" bright "<<bright<<" opaque "<<opaque<<"\n";
        }
        return 0;
    }
    if(argc>8&&std::wstring(argv[8])==L"dump17"){
        for(size_t index=0;index<town.scene.placements.size();++index) {
            const auto& p=town.scene.placements[index];const auto& t=town.scene.tiles[p.tile];
            if(t.orientation==17&&t.mainIndex==3) {
                const auto path="out/modern/tile17_"+std::to_string(t.subIndex)+".bmp";
                writeBmp(path,t.image);
                std::cout<<"wrote "<<path<<" from #"<<index<<" cell "<<p.cellX<<","<<p.cellY<<"\n";
            }
        }
        return 0;
    }
    const int cx=(std::stoi(argv[5])-town.worldOrigin.x)/5,cy=(std::stoi(argv[6])-town.worldOrigin.y)/5,r=std::stoi(argv[7]);
    std::cout<<"scene cells "<<town.scene.cellsX<<"x"<<town.scene.cellsY<<", looking at cell "<<cx<<","<<cy<<"\n";
    size_t index=0;
    for(const auto& p:town.scene.placements){
        if(std::abs(int(p.cellX)-cx)<=r&&std::abs(int(p.cellY)-cy)<=r){
            const auto& t=town.scene.tiles[p.tile];size_t opaque=0;unsigned rs=0,gs=0,bs=0;
            for(size_t i=0;i<t.image.rgba.size();i+=4)if(t.image.rgba[i+3]){++opaque;rs+=t.image.rgba[i];gs+=t.image.rgba[i+1];bs+=t.image.rgba[i+2];}
            std::cout<<"#"<<index<<" cell "<<p.cellX<<","<<p.cellY<<" orient "<<t.orientation<<" main "<<t.mainIndex<<" sub "<<t.subIndex<<" size "<<t.image.width<<"x"<<t.image.height<<" roofH "<<t.roofHeight<<" origin "<<t.originX<<","<<t.originY<<(p.shadow?" SHADOW":"")<<" px "<<p.x<<","<<p.y<<" opaque "<<opaque*100/(t.image.width*t.image.height)<<"% mean rgb "<<(opaque?rs/opaque:0)<<","<<(opaque?gs/opaque:0)<<","<<(opaque?bs/opaque:0)<<"\n";
        }
        ++index;
    }
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
