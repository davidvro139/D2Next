#include "Assets.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace d2 {
namespace {
struct Reader {
    std::span<const uint8_t> b; size_t pos=0;
    void skip(size_t n) { if(n>b.size()-pos) throw std::runtime_error("Truncated DS1 layout"); pos+=n; }
    uint32_t read() { skip(4); size_t p=pos-4; return uint32_t(b[p])|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24; }
    std::string string() {
        std::string s;
        while(pos<b.size()) { auto c=b[pos++]; if(!c)return s; if(s.size()>=1024)throw std::runtime_error("DS1 dependency name too long"); s+=char(c); }
        throw std::runtime_error("Unterminated DS1 dependency");
    }
};
std::string normalize(std::string name) {
    for(auto& c:name) c=c=='/'?'\\':char(std::tolower(static_cast<unsigned char>(c)));
    auto start=name.find("data\\global\\tiles\\");
    if(start!=std::string::npos) name=name.substr(start);
    else if(name.starts_with("act")) name="data\\global\\tiles\\"+name;
    if(name.ends_with(".tg1")) name.replace(name.size()-4,4,".dt1");
    return name;
}
using Key=std::tuple<uint32_t,uint32_t,uint32_t>;
Key key(const MapCell& c) { return {(c.value>>20)&63,(c.value>>8)&255,c.orientation}; }
}
MapLayout decodeDs1(std::span<const uint8_t> bytes) {
    Reader r{bytes}; MapLayout map;
    map.version=r.read();
    if(map.version<4||map.version>18)throw std::runtime_error("DS1 viewer supports layout versions 4 through 18");
    const auto w=r.read(),h=r.read();
    if(w>=128||h>=128)throw std::runtime_error("DS1 map exceeds 128x128 preview limit");
    map.width=w+1;map.height=h+1;
    if(map.version>=8)map.act=std::min(r.read(),4u);
    const uint32_t tag=map.version>=10?r.read():0;
    const auto dependencies=r.read();
    if(dependencies>128)throw std::runtime_error("Too many DS1 dependencies");
    for(uint32_t i=0;i<dependencies;++i)map.dependencies.push_back(normalize(r.string()));
    if(map.version>=9&&map.version<=13)r.skip(8);
    const auto walls=r.read(),floors=map.version>=16?r.read():1;
    if(walls>4||floors>2||!floors)throw std::runtime_error("Unsupported DS1 layer count");
    const size_t cells=size_t(map.width)*map.height;
    auto layer=[&](uint32_t orientation) { std::vector<MapCell> result(cells);for(auto& c:result)c={r.read(),orientation};return result; };
    constexpr uint32_t oldDirections[]={0,1,2,1,2,3,3,5,5,6,6,7,7,8,9,10,11,12,13,14,15,16,17,18,20};
    for(uint32_t i=0;i<walls;++i) {
        auto l=layer(0);
        for(auto& c:l) {auto o=r.read(); if(map.version<7){if(o>=std::size(oldDirections))throw std::runtime_error("Invalid DS1 orientation");o=oldDirections[o];} if(o>255)throw std::runtime_error("Invalid DS1 orientation");c.orientation=o;}
        map.walls.push_back(std::move(l));
    }
    for(uint32_t i=0;i<floors;++i)map.floors.push_back(layer(0));
    map.shadows=layer(13);
    if(tag==1||tag==2)r.skip(cells*4);
    map.objects=r.read();
    if(map.objects>65536)throw std::runtime_error("DS1 object count exceeds limit");
    // Each: type (1 monster, 2 object), number (monpreset.txt or obj.txt, per act), x and y in subtiles, and from version 6
    // flags.
    for(uint32_t i=0;i<map.objects;++i) {
        MapObject o;o.type=r.read();o.id=r.read();o.x=r.read();o.y=r.read();
        if(map.version>5)o.flags=r.read();
        map.objectList.push_back(o);
    }
    // Optional groups and NPC paths are not used by the static layout preview.
    return map;
}
MapScene buildMapScene(const MapLayout& map,const Archives& archives) {
    if(!map.width||!map.height||map.width>128||map.height>128||map.act>4)throw std::runtime_error("Invalid map dimensions or act");
    const size_t cellCount=size_t(map.width)*map.height;
    for(const auto& layer:map.floors)if(layer.size()!=cellCount)throw std::runtime_error("Invalid floor layer size");
    for(const auto& layer:map.walls)if(layer.size()!=cellCount)throw std::runtime_error("Invalid wall layer size");
    if(map.shadows.size()!=cellCount)throw std::runtime_error("Invalid shadow layer size");
    auto palette=decodePalette(archives.read("data\\global\\palette\\act"+std::to_string(map.act+1)+"\\pal.dat"));
    std::map<Key,uint32_t> tiles;
    MapScene result;
    size_t pixelBudget=0;
    std::set<std::string> loaded;
    for(const auto& dependency:map.dependencies) {
        if(!dependency.ends_with(".dt1")||!loaded.insert(dependency).second)continue;
        auto b=archives.read(dependency);++result.dependencies;
        const auto count=dt1TileCount(b);
        for(uint32_t i=0;i<count;++i) {
            auto t=decodeDt1(b,palette,i); Key k{t.mainIndex,t.subIndex,t.orientation};
            if(tiles.contains(k))continue; // Stable first variant for this inspection slice.
            pixelBudget+=t.image.rgba.size();
            if(pixelBudget>256*1024*1024)throw std::runtime_error("Map tile cache exceeds 256 MiB");
            tiles.emplace(k,uint32_t(result.tiles.size()));
            result.tiles.push_back(std::move(t));
        }
    }
    struct Draw {uint32_t tile;int x,y,depth,layer;uint32_t cellX,cellY;bool shadow;};
    std::vector<Draw> draws;
    int left=0,top=0,right=1,bottom=1;
    auto add=[&](const std::vector<MapCell>& cells,int layer,bool shadow) {
        for(uint32_t y=0;y<map.height;++y)for(uint32_t x=0;x<map.width;++x) {
            const auto& cell=cells[size_t(y)*map.width+x];
            if(!(cell.value&255))continue;
            auto found=tiles.find(key(cell));
            if(found==tiles.end()){++result.missing;continue;}
            const Tile& t=result.tiles[found->second];
            const int px=(int(x)-int(y))*80+t.originX;
            const int py=(int(x)+int(y))*40+t.originY+(t.orientation==0?0:t.orientation==15?-t.roofHeight:80);
            draws.push_back({found->second,px,py,int(x+y),layer,x,y,shadow});
            left=std::min(left,px);top=std::min(top,py);
            right=std::max(right,px+int(t.image.width));bottom=std::max(bottom,py+int(t.image.height));
        }
    };
    for(const auto& floor:map.floors)add(floor,0,false);
    add(map.shadows,1,true);
    for(const auto& wall:map.walls)add(wall,2,false);
    if(draws.empty())throw std::runtime_error("No DS1 tile references resolved");
    auto order=[&](const Draw& d) {
        const auto& tile=result.tiles[d.tile];
        return std::tuple{d.shadow?1:tileDrawsWithGround(tile)?0:tile.orientation==15?3:2,d.depth,d.x};
    };
    std::stable_sort(draws.begin(),draws.end(),[&](const Draw& a,const Draw& b){return order(a)<order(b);});
    result.width=right-left;result.height=bottom-top;
    result.cellsX=map.width;result.cellsY=map.height;
    result.originX=80-left;result.originY=-top;
    for(const auto& d:draws)result.placements.push_back({d.tile,d.x-left,d.y-top,d.cellX,d.cellY,d.shadow});
    return result;
}
std::vector<uint32_t> visibleTiles(const MapScene& scene,float left,float top,float right,float bottom) {
    std::vector<uint32_t> visible;
    if(right<=left||bottom<=top)return visible;
    for(uint32_t i=0;i<scene.placements.size();++i) {
        const auto& p=scene.placements[i];const auto& img=scene.tiles.at(p.tile).image;
        if(p.x<right&&p.y<bottom&&float(p.x)+img.width>left&&float(p.y)+img.height>top)visible.push_back(i);
    }
    return visible;
}
Image composeMap(const MapScene& scene) {
    const auto width=scene.width,height=scene.height;
    if(width>16384||height>16384||uint64_t(width)*height>32*1024*1024)throw std::runtime_error("Composited map exceeds 128 MiB preview budget");
    Image result{width,height,Bytes(size_t(width)*height*4,0)};
    for(const auto& draw:scene.placements){const auto& img=scene.tiles.at(draw.tile).image;
        for(uint32_t y=0;y<img.height;++y)for(uint32_t x=0;x<img.width;++x){
            size_t src=(size_t(y)*img.width+x)*4;if(!img.rgba[src+3])continue;
            size_t dst=(size_t(y+draw.y)*width+x+draw.x)*4;
            if(draw.shadow){for(int c=0;c<3;++c)result.rgba[dst+c]=uint8_t(result.rgba[dst+c]/2);}
            else std::copy_n(img.rgba.begin()+src,4,result.rgba.begin()+dst);
        }
    }
    return result;
}
MapPreview renderMap(const MapLayout& map,const Archives& archives) {
    auto scene=buildMapScene(map,archives);
    return {composeMap(scene),uint32_t(scene.placements.size()),scene.missing,scene.dependencies};
}
}
