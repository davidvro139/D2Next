#include "NativeTown.h"
#include "NativeLink.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace d2 {
namespace {
using native::ordinal;using native::Library;
template<class T> T read(const void* p,size_t offset) {
    T value;std::memcpy(&value,static_cast<const uint8_t*>(p)+offset,sizeof(value));return value;
}
int actOfLevel(int levelId) {
    if(levelId>=109)return 4;
    if(levelId>=103)return 3;
    if(levelId>=75)return 2;
    if(levelId>=40)return 1;
    return 0;
}
// Matches this checkout's packed D2DrlgTileDataStrc on x64. Isolated here so
// private game layouts never escape into renderer or simulation headers.
#pragma pack(push,1)
struct NativeTile {
    int32_t width,height,x,y;
    void* coordList;
    uint32_t flags;
    void* tile;
    int32_t type;
    void* next;
    int32_t unknown;
    uint8_t rgba[4];
    int32_t tail;
};
#pragma pack(pop)
static_assert(sizeof(void*)==8 && sizeof(NativeTile)==60 && offsetof(NativeTile,tile)==28);
// The DLLs, archives and tables loaded for one scene, released afterwards (in-process, only the act is: the rest is the
// program's, shared with the game session).
struct Host {
    std::vector<native::Module> modules;
    std::vector<void*> archives;
    native::Module storm{},common{};
    void* act{};
    void(__stdcall* freeAct)(void*){};
    void(__stdcall* unload)(){};
    int(__stdcall* closeArchive)(void*){};
    bool tables=false;
    ~Host() {
        if(act)freeAct(act);
        if(tables)unload();
        for(auto it=archives.rbegin();it!=archives.rend();++it)closeArchive(*it);
        for(auto it=modules.rbegin();it!=modules.rend();++it)native::unloadModule(*it);
    }
    native::Module load(const std::filesystem::path& directory,const wchar_t* name,Library library) {
        auto m=native::loadModule(directory/name,library);
        modules.push_back(m);return m;
    }
};
}
NativeTown loadNativeTown(const std::filesystem::path& dllDirectory,
    const std::filesystem::path& gameDirectory,const Archives& archives,uint32_t seed,const std::vector<int>& levelIds,bool withNeighbours,
    const std::function<void()>& progress) {
    auto tickProgress=[&]{if(progress)progress();};
    if(levelIds.empty())throw std::runtime_error("Native scene needs at least one level");
    tickProgress();
    const int act=actOfLevel(levelIds.front());
    for(const int levelId:levelIds)if(actOfLevel(levelId)!=act)throw std::runtime_error("Native scene levels must be in one act");
    Host host;
    const auto directory=std::filesystem::absolute(dllDirectory);
#ifndef D2MODERN_INPROCESS
    // These DLLs have global tables/archive registries. Do not disturb another
    // native host in the same process.
    for(auto name:{L"Storm.dll",L"Fog.dll",L"D2Lang.dll",L"D2CMP.dll",L"D2Common.dll"})
        if(GetModuleHandleW(name))throw std::runtime_error("Native town requires exclusive ownership of the game DLLs");
#endif
    host.storm=host.load(directory,L"Storm.dll",Library::Storm);
    tickProgress();
    auto fog=host.load(directory,L"Fog.dll",Library::Fog);
    if(!native::findNamed(fog,"FogHost_Configure"))throw std::runtime_error("Native town requires this repository's x64 Fog.dll");
    host.load(directory,L"D2Lang.dll",Library::D2Lang);host.load(directory,L"D2CMP.dll",Library::D2CMP);
    host.common=host.load(directory,L"D2Common.dll",Library::D2Common);
    host.freeAct=ordinal<void(__stdcall*)(void*)>(host.common,10039);
#ifdef D2MODERN_INPROCESS
    native::useGameData(gameDirectory);
#else
    auto openArchive=ordinal<int(__stdcall*)(const char*,int,int,void**)>(host.storm,266);
    host.closeArchive=ordinal<int(__stdcall*)(void*)>(host.storm,252);
    auto core=gameDirectory/"D2SE/CORES/1.10f/Patch_D2.mpq";
    // The 1.10f data tables are only in the patch archive; without it D2Common reads a table that is not there and crashes.
    if(!std::filesystem::exists(core)&&!std::filesystem::exists(gameDirectory/"Patch_D2.mpq"))
        throw std::runtime_error("Patch_D2.mpq (the Diablo II 1.10f patch) is missing from "+gameDirectory.string()+". Copy it there from your Diablo II folder.");
    std::vector<std::filesystem::path> paths={std::filesystem::exists(core)?core:gameDirectory/"Patch_D2.mpq",
        gameDirectory/"d2exp.mpq",gameDirectory/"d2data.mpq",gameDirectory/"d2char.mpq",gameDirectory/"d2sfx.mpq"};
    int priority=5000;
    for(const auto& path:paths) {
        if(std::filesystem::exists(path)) {
            void* handle=nullptr;
            if(!openArchive(std::filesystem::absolute(path).string().c_str(),priority,2,&handle))
                throw std::runtime_error("Cannot mount native archive: "+path.string());
            host.archives.push_back(handle);
        }
        priority-=1000;
    }
    host.unload=ordinal<void(__stdcall*)()>(host.common,10575);
    ordinal<void(__stdcall*)(void*,int,int)>(host.common,10576)(nullptr,1,1);host.tables=true;
#endif
    host.act=ordinal<void*(__stdcall*)(uint8_t,uint32_t,int,void*,uint8_t,void*,int,void*,void*)>(host.common,10038)
        (uint8_t(act),seed,0,nullptr,0,nullptr,1,nullptr,nullptr);
    if(!host.act)throw std::runtime_error("Native town act allocation failed");
    tickProgress();
    auto drlg=ordinal<void*(__stdcall*)(void*)>(host.common,10088)(host.act);
    auto initLevel=ordinal<void(__stdcall*)(void*)>(host.common,10006);
    // Packed D2DrlgLevelStrc: id at 8, coordinates at sizeof(void*)+0x1c.
    int x0=INT32_MAX,y0=INT32_MAX,x1=0,y1=0;
    std::set<int> wanted;std::vector<int> complete;
    for(int levelId:levelIds) {
        auto level=ordinal<void*(__stdcall*)(void*,int)>(host.common,10005)(drlg,levelId);
        if(!level||read<int32_t>(level,8)!=levelId)throw std::runtime_error("Unsupported native level layout");
        int lx=read<int32_t>(level,36),ly=read<int32_t>(level,40),lw=read<int32_t>(level,44),lh=read<int32_t>(level,48);
        if((lx<0||ly<0)&&lw>0&&lh>0) {
            initLevel(level);
            lx=read<int32_t>(level,36);ly=read<int32_t>(level,40);lw=read<int32_t>(level,44);lh=read<int32_t>(level,48);
        }
        if(lw<=0||lh<=0||lw>256||lh>256||lx<0||ly<0||lx>100000||ly>100000)
            throw std::runtime_error("Invalid native level bounds for level "+std::to_string(levelId)+": x="+std::to_string(lx)+", y="+std::to_string(ly)+", w="+std::to_string(lw)+", h="+std::to_string(lh));
        x0=std::min(x0,lx);y0=std::min(y0,ly);x1=std::max(x1,lx+lw);y1=std::max(y1,ly+lh);wanted.insert(levelId);
    }
    if(withNeighbours) {
        // Levels whose rectangle touches the requested ones (outdoor areas are laid side by side); the scene stays within 512 tiles.
        // Two rings: the levels touching the requested ones (`complete`: everything around them is in the scene too) and the levels touching those.
        std::set<int> firstRing;
        for(int ring=0;ring<2;++ring) {
        if(ring==1)complete.assign(wanted.begin(),wanted.end());
        const int px0=x0,py0=y0,px1=x1,py1=y1;
        for(int candidate=1;candidate<=132;++candidate) {
            if(actOfLevel(candidate)!=act)continue;
            if(wanted.count(candidate))continue;
            auto level=ordinal<void*(__stdcall*)(void*,int)>(host.common,10005)(drlg,candidate);
            if(!level||read<int32_t>(level,8)!=candidate)continue;
            const int lx=read<int32_t>(level,36),ly=read<int32_t>(level,40),lw=read<int32_t>(level,44),lh=read<int32_t>(level,48);
            if(lw<=0||lh<=0||lw>256||lh>256||lx<0||ly<0||lx>100000||ly>100000)continue;
            if(lx>px1+1||lx+lw<px0-1||ly>py1+1||ly+lh<py0-1)continue;
            if(std::max(x1,lx+lw)-std::min(x0,lx)>512||std::max(y1,ly+lh)-std::min(y0,ly)>512)continue;
            x0=std::min(x0,lx);y0=std::min(y0,ly);x1=std::max(x1,lx+lw);y1=std::max(y1,ly+lh);wanted.insert(candidate);
        }
        }
    }
    if(complete.empty())complete.assign(wanted.begin(),wanted.end());
    const int width=x1-x0,height=y1-y0;
    if(width>512||height>512)throw std::runtime_error("Native levels are too far apart for one scene");
    auto stream=ordinal<void*(__stdcall*)(void*,int,int)>(host.common,10065);
    auto roomLevel=ordinal<int(__stdcall*)(void*)>(host.common,10057);
    auto coords=ordinal<void(__stdcall*)(void*,int*)>(host.common,10034);
    auto collide=ordinal<uint16_t(__stdcall*)(void*,int,int,unsigned,unsigned,uint16_t)>(host.common,10120);
    std::set<void*> seen;std::vector<void*> rooms;
    for(int y=y0;y<y0+height;y+=2)for(int x=x0;x<x0+width;x+=2) {
        auto room=stream(host.act,x,y);
        if(room&&wanted.count(roomLevel(room))&&seen.insert(room).second)rooms.push_back(room);
    }
    if(rooms.empty())throw std::runtime_error("Native town has no rooms");
    tickProgress();
    NativeTown result;result.seed=seed;result.act=act;result.levels.assign(wanted.begin(),wanted.end());result.complete=complete;result.rooms=uint32_t(rooms.size());result.worldOrigin={x0*5,y0*5};
    auto& scene=result.scene;scene.cellsX=width+1;scene.cellsY=height+1;
    auto& grid=result.collision;grid.width=scene.cellsX*5;grid.height=scene.cellsY*5;
    grid.flags.resize(size_t(grid.width)*grid.height);grid.ground.resize(grid.flags.size());
    const auto palette=decodePalette(archives.read("data\\global\\palette\\act"+std::to_string(act+1)+"\\pal.dat"));
    std::map<std::string,Bytes> files;std::map<void*,uint32_t> tileIndices;
    size_t pixelBytes=0,fileBytes=0;
    size_t resolvedTiles=0;
    // Whether a decoded tile is one flat colour (every drawn pixel the same), remembered per tile.
    std::map<uint32_t,bool> flatTiles;
    auto flatTile=[&](uint32_t index) {
        if(auto it=flatTiles.find(index);it!=flatTiles.end())return it->second;
        const auto& rgba=scene.tiles[index].image.rgba;
        bool flat=true,any=false;uint32_t first=0;
        for(size_t p=0;p+3<rgba.size()&&flat;p+=4) {
            if(!rgba[p+3])continue;
            uint32_t c;std::memcpy(&c,&rgba[p],4);
            if(!any){first=c;any=true;}else flat=c==first;
        }
        return flatTiles[index]=any&&flat;
    };
    auto resolve=[&](void* tile) {
        if(auto it=tileIndices.find(tile);it!=tileIndices.end())return it->second;
        // Native D2CMP keeps the original 96-byte DT1 header and then a library
        // name pointer (TileLibrary.cpp). Match the entire header, not just type/
        // style/sequence: multiple art variants deliberately share those keys.
        const char* name=read<const char*>(tile,96);
        if(!name||strnlen(name,260)==260)throw std::runtime_error("Invalid native tile library name");
        const std::string path=name;
        auto it=files.find(path);
        if(it==files.end()) {
            auto bytes=archives.read(path);fileBytes+=bytes.size();
            if(fileBytes>128*1024*1024)throw std::runtime_error("Native tile files exceed budget");
            it=files.emplace(path,std::move(bytes)).first;
        }
        const auto& bytes=it->second;const auto count=dt1TileCount(bytes);
        const auto start=read<uint32_t>(bytes.data(),272);
        uint32_t index=0;
        for(;index<count;++index)if(!std::memcmp(tile,bytes.data()+start+size_t(index)*96,96))break;
        if(index==count)throw std::runtime_error("Native tile header does not match MPQ: "+path);
        auto decoded=decodeDt1(bytes,palette,index);pixelBytes+=decoded.image.rgba.size();
        if(pixelBytes>256*1024*1024)throw std::runtime_error("Native tile pixels exceed budget");
        const auto id=uint32_t(scene.tiles.size());scene.tiles.push_back(std::move(decoded));tileIndices[tile]=id;
        if((++resolvedTiles&15)==0)tickProgress();
        if(scene.tiles[id].animated) {
            // Its frames: the animated tiles of this file with the same orientation, main and sub index, by rarity (the frame
            // number).
            const auto& t=scene.tiles[id];
            std::vector<std::pair<uint32_t,uint32_t>> frames;
            for(uint32_t k=0;k<count;++k) {
                const size_t hk=start+size_t(k)*96;
                if(bytes[hk+7]&&read<uint32_t>(bytes.data(),hk+20)==t.orientation&&read<uint32_t>(bytes.data(),hk+24)==t.mainIndex&&read<uint32_t>(bytes.data(),hk+28)==t.subIndex)
                    frames.push_back({read<uint32_t>(bytes.data(),hk+32),k});
            }
            std::sort(frames.begin(),frames.end());
            std::vector<uint32_t> list;
            for(const auto& [rarity,k]:frames) {
                if(k==index){list.push_back(id);continue;}
                auto frame=decodeDt1(bytes,palette,k);pixelBytes+=frame.image.rgba.size();
                list.push_back(uint32_t(scene.tiles.size()));scene.tiles.push_back(std::move(frame));
            }
            if(list.size()>1){scene.tileFrames.resize(scene.tiles.size());scene.tileFrames[id]=std::move(list);}
        }
        return id;
    };
    int left=0,top=0,right=1,bottom=1;
    size_t processedRooms=0,processedPlacements=0;
    for(auto room:rooms) {
        if((processedRooms++&1)==0)tickProgress();
        int c[8]{};coords(room,c);
        result.roomAreas.push_back({c[0]-x0*5,c[1]-y0*5,c[2],c[3],roomLevel(room)});
        for(int y=std::max(c[1],y0*5);y<std::min(c[1]+c[3],(y0+height)*5);++y)
            for(int x=std::max(c[0],x0*5);x<std::min(c[0]+c[2],(x0+width)*5);++x) {
                const auto at=size_t(y-y0*5)*grid.width+x-x0*5;
                grid.ground[at]=1;grid.flags[at]=collide(room,x,y,1,1,0x3c01)?collideSolid:0;
            }
        for(int ordinalNumber:{10030,10031,10032}) {
            int count=0;
            auto data=ordinal<const NativeTile*(__stdcall*)(void*,int*)>(host.common,ordinalNumber)(room,&count);
            if(count<0||count>100000||(!data&&count))throw std::runtime_error("Invalid native tile array");
            for(int i=0;i<count;++i) {
                const auto& t=data[i];if(!t.tile||(t.flags&8))continue;
                int x=c[4]+t.x-x0,y=c[5]+t.y-y0;
                if(x<0||y<0||x>width||y>height)continue;
                auto index=resolve(t.tile);const auto& tile=scene.tiles[index];
                // Special tiles (orientation 10/11): main index 30 and up are the game's invisible markers (warps, visibility);
                // below 30 they are pictures of the way to another level (the stairs of the Jail and the Catacombs), drawn
                // standing like walls.
                if((tile.orientation==10||tile.orientation==11)&&tile.mainIndex>=30)continue;
                // Placeholder walls: River.dt1 (Act I) has one "wall" (orientation 1, main 5, rarity 0) that is a floor-sized
                // diamond of a single colour, the only tile of that kind, so the riverbanks of the Rogue Encampment get it. The
                // game shows nothing there; a wall drawn in one flat colour is an editor marker, not art.
                if(tile.orientation>=1&&tile.orientation<=14&&tile.orientation!=13&&flatTile(index)) { // not shadows (13), which are one flat colour
                    if(std::getenv("D2MODERN_DEBUG_SPECIAL"))std::fprintf(stderr,"flat wall o%u main %u sub %u at cell %d,%d skipped (%s)\n",tile.orientation,tile.mainIndex,tile.subIndex,x,y,read<const char*>(t.tile,96));
                    continue;
                }
                if(std::getenv("D2MODERN_DEBUG_SPECIAL")&&(tile.orientation==10||tile.orientation==11))
                    std::fprintf(stderr,"special tile o%u main %u sub %u at cell %d,%d: %ux%u\n",tile.orientation,tile.mainIndex,tile.subIndex,x,y,tile.image.width,tile.image.height);
                const int px=(x-y)*80+tile.originX,py=(x+y)*40+tile.originY+(tile.orientation==0?0:tile.orientation==15?-tile.roofHeight:80); // walls stand on the bottom corner of their cell (80 px below the floor tile's top); roofs are raised by their roof height // roofs float above the walls
                scene.placements.push_back({index,px,py,uint32_t(x),uint32_t(y),tile.orientation==13});
                if((++processedPlacements&255)==0)tickProgress();
                if(std::getenv("D2MODERN_DEBUG_ANIMATED")&&index<scene.tileFrames.size()&&!scene.tileFrames[index].empty())
                    std::fprintf(stderr,"animated tile o%u main %u sub %u at cell %d,%d: %zu frames\n",tile.orientation,tile.mainIndex,tile.subIndex,x,y,scene.tileFrames[index].size());
                left=std::min(left,px);top=std::min(top,py);
                right=std::max(right,px+int(tile.image.width));bottom=std::max(bottom,py+int(tile.image.height));
            }
        }
    }
    if(std::getenv("D2MODERN_DEBUG_LEVELS")){for(int id=1;id<=39;++id){auto level=ordinal<void*(__stdcall*)(void*,int)>(host.common,10005)(drlg,id);if(level&&read<int32_t>(level,8)==id)std::fprintf(stderr,"level %d rect %d,%d %dx%d\n",id,read<int32_t>(level,36),read<int32_t>(level,40),read<int32_t>(level,44),read<int32_t>(level,48));}}
    if(scene.placements.empty())throw std::runtime_error("Native town has no drawable tiles");
    auto order=[&](const TilePlacement& p) {
        const auto& tile=scene.tiles[p.tile];
        return std::tuple{p.shadow?1:tileDrawsWithGround(tile)?0:tile.orientation==15?3:2,p.cellX+p.cellY,p.x};
    };
    std::stable_sort(scene.placements.begin(),scene.placements.end(),[&](const auto& a,const auto& b){return order(a)<order(b);});
    tickProgress();
    scene.width=right-left;scene.height=bottom-top;scene.originX=80-left;scene.originY=-top;
    scene.dependencies=uint32_t(files.size());
    for(auto& p:scene.placements){p.x-=left;p.y-=top;}
    tickProgress();
    return result;
}
}
