// Lists the game's interface pictures (data\global\ui) with their frame sizes, or writes one DC6's frames as BMP files.
#include "Font.h"
#include <fcntl.h>
#include <io.h>
#include <map>
#include <chrono>
#include "Tables.h"
#include "FrontEnd.h"
#include <windows.h>
// usage: UiDump GAME_DIR list [FILTER]        | UiDump GAME_DIR dump MPQ_PATH OUT_PREFIX [PALETTE_ACT]
#include "Assets.h"
#include <StormLib.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
static uint32_t u32(const d2::Bytes& b,size_t o){return b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(uint32_t(b[o+3])<<24);}
static void writeBmp(const std::string& path,const d2::Image& img) {
    const uint32_t w=img.width,h=img.height,row=w*4,size=54+row*h;
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](uint16_t v){out.put(char(v&255));out.put(char(v>>8));};
    auto o32=[&](uint32_t v){u16(uint16_t(v&0xffff));u16(uint16_t(v>>16));};
    out.put('B');out.put('M');o32(size);o32(0);o32(54);o32(40);o32(w);o32(h);u16(1);u16(32);o32(0);o32(row*h);o32(2835);o32(2835);o32(0);o32(0);
    for(uint32_t y=h;y-->0;)for(uint32_t x=0;x<w;++x){const size_t i=(size_t(y)*w+x)*4;const uint8_t a=img.rgba[i+3];
        out.put(char(a?img.rgba[i+2]:255));out.put(char(a?img.rgba[i+1]:0));out.put(char(a?img.rgba[i]:255));out.put(char(255));}
}
static void describe(const std::string& name,const d2::Bytes& b) {
    std::cout<<name<<" ("<<b.size()<<" bytes)";
    if(b.size()<24||u32(b,0)!=6){std::cout<<"\n";return;}
    const uint32_t dirs=u32(b,16),per=u32(b,20);std::cout<<" dirs "<<dirs<<" frames "<<per<<":";
    for(uint32_t f=0;f<dirs*per&&f<40;++f){const size_t h=u32(b,24+f*4);if(h+32>b.size())break;
        std::cout<<" ["<<f<<"] "<<u32(b,h+4)<<"x"<<u32(b,h+8)<<"@"<<int32_t(u32(b,h+12))<<","<<int32_t(u32(b,h+16));}
    std::cout<<"\n";
}
int wmain(int argc,wchar_t** argv){try{
    const std::filesystem::path root=argv[1];const std::wstring mode=argv[2];
    d2::Archives archives(root);
    if(mode==L"frontend") {
        // UiDump GAME frontend SCREEN(title|main|select|loading) OUT.bmp [SECONDS]: one front-end screen composed as the game draws it.
        const std::wstring which=argv[3];
        const auto profile=_wgetenv(L"LOCALAPPDATA")?std::filesystem::path(_wgetenv(L"LOCALAPPDATA"))/L"D2Next":std::filesystem::path();
        // "delete" opens the confirmation popup on a character of the profile folder (it never presses OK).
        d2::FrontEnd front(archives,d2::findCharacters({root/L"save",profile}),{profile},L"dfsd.d2s",which==L"delete"?profile:std::filesystem::temp_directory_path()/L"D2Next-preview");
        if(which==L"delete"){front.key(VK_SPACE);front.key(VK_RETURN);front.mouseDown(517,498);front.mouseUp(517,498);}
        if(which!=L"title"&&which!=L"delete")front.key(VK_SPACE);
        if(which.rfind(L"create",0)==0) {
            // create: the heroes by the fire; create-N: class N chosen (after SECONDS of walking), with a name typed.
            front.key(VK_RETURN);front.mouseDown(117,498);front.mouseUp(117,498);
            if(which.size()>7) {
                static const float hx[]={100,626,300,521,400,720,231},hy[]={390,352,335,338,330,370,365};
                const int c=std::stoi(which.substr(7));front.mouseMove(hx[c],hy[c]-60);front.mouseDown(hx[c],hy[c]-60);front.mouseUp(hx[c],hy[c]-60);
                for(char ch:std::string("Newhero"))front.character(unsigned(ch));
            }
        }
        if(which==L"select"||which==L"loading")front.key(VK_RETURN);
        if(which==L"loading")front.key(VK_RETURN);
        if(argc>5)front.advance(std::stod(argv[5]));
        d2::Image canvas{800,600,d2::Bytes(800*600*4,0)};
        for(size_t i=3;i<canvas.rgba.size();i+=4)canvas.rgba[i]=255;
        for(const auto& s:front.draw())for(uint32_t y=0;y<s.image->height;++y)for(uint32_t x=0;x<s.image->width;++x) {
            const size_t p=(size_t(y)*s.image->width+x)*4;if(!s.image->rgba[p+3])continue;
            const int X=int(s.x)+int(x),Y=int(s.y)+int(y);if(X<0||Y<0||X>=800||Y>=600)continue;
            auto* d=&canvas.rgba[(size_t(Y)*800+X)*4];
            for(int k=0;k<3;++k)d[k]=s.additive?uint8_t(std::min(255,d[k]+s.image->rgba[p+k])):s.image->rgba[p+k];
        }
        if(std::getenv("UIDUMP_LIST"))for(const auto& s:front.draw())std::cout<<"sprite "<<s.image->width<<"x"<<s.image->height<<" at "<<s.x<<","<<s.y<<(s.additive?" add":"")<<"\n";
        writeBmp(std::filesystem::path(argv[4]).string(),canvas);
        return 0;
    }
    if(mode==L"cat") {
        // UiDump GAME cat ARCHIVE_PATH: writes the file as stored (for example data\global\excel\overlay.txt).
        const auto bytes=archives.read(std::filesystem::path(argv[3]).string());
        std::cout.flush();_setmode(_fileno(stdout),_O_BINARY); // exactly the stored bytes (text mode turns \n into \r\n)
        std::cout.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
        return 0;
    }
    if(mode==L"dccframes") {
        // UiDump GAME dccframes ARCHIVE_PATH: each frame's place relative to the anchor (x, y of its top edge) and size.
        const auto a=d2::decodeDcc(archives.read(std::filesystem::path(argv[3]).string()),d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")));
        std::cout<<a.directions<<" directions x "<<a.framesPerDirection<<" frames\n";
        for(size_t i=0;i<a.frames.size();++i) {
            const auto& f=a.frames[i];int top=int(f.image.height),bottom=-1;
            for(uint32_t y=0;y<f.image.height;++y)for(uint32_t x=0;x<f.image.width;++x)if(f.image.rgba[(size_t(y)*f.image.width+x)*4+3]){top=std::min(top,int(y));bottom=std::max(bottom,int(y));}
            std::cout<<"frame "<<i<<": at "<<f.x<<","<<f.y<<" size "<<f.image.width<<"x"<<f.image.height<<", drawn rows "<<f.y+top<<" to "<<f.y+bottom<<"\n";
        }
        return 0;
    }
    if(mode==L"shadowdir") {
        // UiDump GAME shadowdir DT1_PATH: for each shadow tile (orientation 13) with a wall of the same index, the middle of each
        // one's pixels and their bottom rows (y grows downward), to see which way the map's shadows fall.
        const auto bytes=archives.read(std::filesystem::path(argv[3]).string());
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        std::vector<d2::Tile> tiles;for(uint32_t i=0;i<d2::dt1TileCount(bytes);++i)tiles.push_back(d2::decodeDt1(bytes,palette,i));
        auto measure=[](const d2::Tile& t){double sx=0,sy=0,n=0;int bottom=-99999,top=99999;
            for(uint32_t y=0;y<t.image.height;++y)for(uint32_t x=0;x<t.image.width;++x)if(t.image.rgba[(size_t(y)*t.image.width+x)*4+3]){sx+=int(x)+t.originX;sy+=int(y)+t.originY;++n;bottom=std::max(bottom,int(y)+t.originY);top=std::min(top,int(y)+t.originY);}
            return std::array<double,5>{n?sx/n:0,n?sy/n:0,n,double(top),double(bottom)};};
        for(const auto& s:tiles)if(s.orientation==13)for(const auto& w:tiles)if(w.orientation!=13&&w.orientation!=0&&w.mainIndex==s.mainIndex&&w.subIndex==s.subIndex) {
            const auto a=measure(w),b=measure(s);if(!a[2]||!b[2])continue;
            std::cout<<"tile "<<w.mainIndex<<"/"<<w.subIndex<<" orient "<<w.orientation<<": wall middle "<<int(a[0])<<","<<int(a[1])<<" rows "<<int(a[3])<<".."<<int(a[4])
                <<"; shadow middle "<<int(b[0])<<","<<int(b[1])<<" rows "<<int(b[3])<<".."<<int(b[4])<<"\n";
            break;
        }
        return 0;
    }
    if(mode==L"automapcheck") {
        // UiDump GAME automapcheck LVLTYPE_ID: for each tile orientation of the level type's DT1 files, how many tiles each
        // AutoMap.txt tile code covers (its Style = main index, StartSequence..EndSequence = sub index).
        auto table=[&](const char* name){auto b=archives.read(name);return d2::TextTable(std::string_view(reinterpret_cast<const char*>(b.data()),b.size()));};
        const auto types=table("data\\global\\excel\\LvlTypes.txt"),automap=table("data\\global\\excel\\AutoMap.txt");
        const size_t row=std::stoul(argv[3]);
        std::string name=types.cell(row,"Name");               // "Act 1 - Wilderness" -> "1 Wilderness"
        if(name.rfind("Act ",0)==0&&name.size()>8)name=name.substr(4,1)+" "+name.substr(8);
        std::cout<<"level type "<<types.cell(row,"Name")<<" -> automap "<<name<<"\n";
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        std::map<std::pair<int,std::string>,int> hits;std::map<int,int> totals;
        for(int f=1;f<=32;++f) {
            auto file=types.cell(row,"File "+std::to_string(f));if(file.empty()||file=="0")continue;
            for(auto& c:file)if(c=='/')c='\\';
            d2::Bytes bytes;try{bytes=archives.read("data\\global\\tiles\\"+file);}catch(...){continue;}
            for(uint32_t i=0;i<d2::dt1TileCount(bytes);++i) {
                const auto t=d2::decodeDt1(bytes,palette,i);++totals[int(t.orientation)];
                for(size_t r=0;r<automap.rows();++r) {
                    if(automap.cell(r,"LevelName")!=name)continue;
                    const auto style=automap.cell(r,"Style"),first=automap.cell(r,"StartSequence"),last=automap.cell(r,"EndSequence");
                    if(style.empty()||first.empty()||last.empty())continue;
                    if(uint32_t(std::stoi(style))==t.mainIndex&&int(t.subIndex)>=std::stoi(first)&&int(t.subIndex)<=std::stoi(last))++hits[{int(t.orientation),automap.cell(r,"TileName")}];
                }
            }
        }
        for(const auto& [o,n]:totals){std::cout<<"orientation "<<o<<" ("<<n<<" tiles):";for(const auto& [k,v]:hits)if(k.first==o)std::cout<<" "<<k.second<<"="<<v;std::cout<<"\n";}
        return 0;
    }
    if(mode==L"charshadow") {
        // UiDump GAME charshadow TOKEN MODE: whether the first frame of a character animation (light armour, no weapon) casts a shadow.
        d2::CharacterAppearance look;look.token=std::filesystem::path(argv[3]).string();
        const auto a=d2::loadCharacter(archives,d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")),std::filesystem::path(argv[4]).string(),look);
        std::cout<<"shadow "<<int(a.frames[0].shadow)<<" (0 none, 1 whole picture, 2 some layers)\n";
        return 0;
    }
    if(mode==L"tilepair") {
        // UiDump GAME tilepair DT1_PATH MAIN SUB OUT.bmp: the walls with that index in grey and their shadow tile in red, together.
        const auto bytes=archives.read(std::filesystem::path(argv[3]).string());
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        const uint32_t main=std::stoul(argv[4]),sub=std::stoul(argv[5]);
        d2::Image out{400,400,d2::Bytes(400*400*4,0)};
        for(size_t i=0;i<out.rgba.size();i+=4){out.rgba[i]=out.rgba[i+1]=out.rgba[i+2]=30;out.rgba[i+3]=255;}
        for(uint32_t i=0;i<d2::dt1TileCount(bytes);++i) {
            const auto t=d2::decodeDt1(bytes,palette,i);if(t.mainIndex!=main||t.subIndex!=sub||t.orientation==0)continue;
            std::cout<<"orientation "<<t.orientation<<" origin "<<t.originX<<","<<t.originY<<" size "<<t.image.width<<"x"<<t.image.height<<"\n";
            for(uint32_t y=0;y<t.image.height;++y)for(uint32_t x=0;x<t.image.width;++x) {
                const size_t s=(size_t(y)*t.image.width+x)*4;if(!t.image.rgba[s+3])continue;
                const int X=120+int(x)+t.originX,Y=300+int(y)+t.originY;if(X<0||Y<0||X>=400||Y>=400)continue;
                auto* d=&out.rgba[(size_t(Y)*400+X)*4];
                if(t.orientation==13){d[0]=uint8_t(std::min(255,d[0]+120));}else{d[1]=d[2]=uint8_t(std::min(255,t.image.rgba[s]/2+60));}
            }
        }
        for(int x=0;x<400;++x){auto* d=&out.rgba[(size_t(300)*400+x)*4];d[1]=200;} // y 0 of the tiles
        writeBmp(std::filesystem::path(argv[6]).string(),out);
        return 0;
    }
    if(mode==L"monster") {
        // UiDump GAME monster CLASS MODE OUT.bmp: the first frame of a monster's animation on dark grey, its see-through layers
        // blended the way the renderer does (added on for effects 3/6, else 75/50/25% opaque).
        d2::UnitArt art(archives,d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")));
        // A sixth argument "exact": through the game's path (first piece of each list, the monster's palette shift).
        const auto* a=argc>6&&std::wstring(argv[6])==L"exact"?art.get(uint32_t(std::stoul(argv[3])),uint32_t(std::stoul(argv[4])),std::array<uint8_t,16>{},0):art.get(uint32_t(std::stoul(argv[3])),uint32_t(std::stoul(argv[4])),0);
        if(!a||a->frames.empty())throw std::runtime_error("no such animation");
        const auto& f=a->frames[size_t(a->framesPerDirection)*0];
        d2::Image out{f.image.width,f.image.height,d2::Bytes(size_t(f.image.width)*f.image.height*4,0)};
        for(size_t i=0;i<out.rgba.size();i+=4){out.rgba[i]=out.rgba[i+1]=out.rgba[i+2]=40;out.rgba[i+3]=255;
            if(f.image.rgba[i+3])std::copy_n(f.image.rgba.begin()+i,3,out.rgba.begin()+i);
            if(!f.glow.rgba.empty()&&f.glow.rgba[i+3])for(int k=0;k<3;++k){const int g=f.glow.rgba[i+k];const float o=f.glowEffect==0?.75f:f.glowEffect==2?.25f:.5f;
                out.rgba[i+k]=uint8_t(f.glowEffect==3||f.glowEffect==6?std::min(255,out.rgba[i+k]+g):int(g*o+out.rgba[i+k]*(1-o)));}}
        std::cout<<"glow effect "<<int(f.glowEffect)<<(f.glow.rgba.empty()?" (no glow layers)":"")<<"\n";
        writeBmp(std::filesystem::path(argv[5]).string(),out);
        return 0;
    }
    if(mode==L"dcc") {
        // UiDump GAME dcc ARCHIVE_PATH PALETTE OUT.bmp: the first 8 frames of a DCC side by side on dark grey, with a palette
        // (act1, units...).
        const auto a=d2::decodeDcc(archives.read(std::filesystem::path(argv[3]).string()),d2::decodePalette(archives.read("data\\global\\palette\\"+std::filesystem::path(argv[4]).string()+"\\pal.dat")));
        const size_t n=std::min<size_t>(8,a.frames.size());uint32_t w=0,h=0;
        for(size_t i=0;i<n;++i){w+=a.frames[i].image.width+4;h=std::max(h,a.frames[i].image.height);}
        d2::Image out{std::max(1u,w),std::max(1u,h),d2::Bytes(size_t(std::max(1u,w))*std::max(1u,h)*4,0)};
        for(size_t i=0;i<out.rgba.size();i+=4){out.rgba[i]=out.rgba[i+1]=out.rgba[i+2]=40;out.rgba[i+3]=255;}
        uint32_t x0=0;
        for(size_t i=0;i<n;++i){const auto& f=a.frames[i].image;
            for(uint32_t y=0;y<f.height;++y)for(uint32_t x=0;x<f.width;++x){const size_t s=(size_t(y)*f.width+x)*4,d=(size_t(y)*out.width+x0+x)*4;if(f.rgba[s+3])std::copy_n(f.rgba.begin()+s,3,out.rgba.begin()+d);}
            x0+=f.width+4;}
        writeBmp(std::filesystem::path(argv[5]).string(),out);
        std::cout<<a.frames.size()<<" frames, "<<a.directions<<" directions\n";
        return 0;
    }
    if(mode==L"coflayers") {
        // UiDump GAME coflayers: every COF under data\global\monsters and \chars with a transparent layer: component, transparent flag, draw effect.
        std::map<std::string,int> effects;
        for(const auto* mpq:{L"Patch_D2.mpq",L"d2exp.mpq",L"d2data.mpq",L"d2char.mpq"}) {
            HANDLE archive=nullptr;if(!SFileOpenArchive((root/mpq).c_str(),0,MPQ_OPEN_READ_ONLY,&archive))continue;
            SFILE_FIND_DATA data{};HANDLE find=SFileFindFirstFile(archive,"*.cof",&data,nullptr);
            if(find){do{std::string n=data.cFileName;std::transform(n.begin(),n.end(),n.begin(),::tolower);
                if(n.find("\\monsters\\")==std::string::npos&&n.find("\\chars\\")==std::string::npos)continue;
                d2::Bytes b;try{b=archives.read(n);}catch(...){continue;}
                if(b.size()<28)continue;const unsigned layers=b[0];if(28+9*layers>b.size())continue;
                std::string line;
                for(unsigned i=0;i<layers;++i){const size_t p=28+9*i;if(b[p+3]){line+=" comp"+std::to_string(b[p])+"/eff"+std::to_string(b[p+4]);effects[(n.find("\\chars\\")!=std::string::npos?"chars eff":"monsters eff")+std::to_string(b[p+4])]++;}}
                if(!line.empty()&&effects.size()<200)std::cout<<n.substr(n.rfind('\\')+1)<<line<<"\n";
            }while(SFileFindNextFile(find,&data));SFileFindClose(find);}
            SFileCloseArchive(archive);
        }
        for(const auto& [k,v]:effects)std::cout<<k<<": "<<v<<" layers\n";
        return 0;
    }
    if(mode==L"modes") {
        // UiDump GAME modes LOOK_FILE: which player animation modes load for that appearance, with frames and directions.
        const auto look=d2::loadAppearance(argv[3]);if(!look)throw std::runtime_error("cannot read the appearance file");
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        static const char* tokens[]={"dt","nu","wl","rn","gh","tn","tw","a1","a2","bl","sc","th","kk","s1","s2","s3","s4","dd","sq","kb"};
        std::cout<<look->token<<" / "<<look->weaponClass<<"\n";
        for(int m=0;m<20;++m) {
            const auto start=std::chrono::steady_clock::now();
            try{const auto a=d2::loadCharacter(archives,palette,tokens[m],*look);
                std::cerr.flush();std::cout<<"mode "<<m<<" "<<tokens[m]<<": "<<a.directions<<" dirs x "<<a.framesPerDirection<<" frames, "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" ms\n";}
            catch(const std::exception& e){std::cout<<"mode "<<m<<" "<<tokens[m]<<": FAILED "<<e.what()<<"\n";}
        }
        return 0;
    }
    if(mode==L"tree") {
        // UiDump GAME tree CLASS: each skill of the class's tree with its page, row and column.
        const d2::StringTable strings(archives);const d2::SkillTable skills(archives,strings);
        for(const auto* s:skills.tree(std::stoi(argv[3])))std::cout<<"page "<<s->page<<" row "<<s->row<<" column "<<s->column<<" icon "<<s->iconCel<<" "<<s->name<<"\n";
        return 0;
    }
    if(mode==L"panel") {
        // UiDump GAME panel MPQ_PATH FIRST_FRAME PALETTE OUT.bmp: four frames (256/64 wide, 256/176 tall) assembled into one 320x432 panel.
        std::string name=std::filesystem::path(argv[3]).string();std::replace(name.begin(),name.end(),'/','\\');
        const uint32_t first=uint32_t(std::stoul(argv[4]));const std::string pal=std::filesystem::path(argv[5]).string();
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\"+pal+"\\pal.dat"));
        const auto b=archives.read(name);
        d2::Image out{320,432,d2::Bytes(320*432*4,0)};
        for(size_t i=0;i<out.rgba.size();i+=4){out.rgba[i]=255;out.rgba[i+2]=255;out.rgba[i+3]=255;}
        const uint32_t xs[]={0,256,0,256},ys[]={0,0,256,256};
        for(uint32_t k=0;k<4;++k){const auto f=d2::decodeDc6(b,palette,first+k);
            for(uint32_t y=0;y<f.height&&ys[k]+y<432;++y)for(uint32_t x=0;x<f.width&&xs[k]+x<320;++x){const size_t s=(size_t(y)*f.width+x)*4;if(f.rgba[s+3])std::copy_n(f.rgba.begin()+s,4,out.rgba.begin()+(size_t(ys[k]+y)*320+xs[k]+x)*4);}}
        writeBmp(std::filesystem::path(argv[6]).string(),out);
        return 0;
    }
    if(mode==L"directions") {
        // UiDump GAME directions TOKEN OUT.bmp: the first town-neutral frame of every direction, side by side, to see which way each faces.
        d2::CharacterAppearance look;look.token=std::filesystem::path(argv[3]).string();
        const auto a=d2::loadCharacter(archives,d2::decodePalette(archives.read("data\\global\\palette\\units\\pal.dat")),"tn",look);
        d2::Image sheet{a.directions*90,120,d2::Bytes(size_t(a.directions)*90*120*4,0)};
        for(size_t i=3;i<sheet.rgba.size();i+=4){sheet.rgba[i-3]=sheet.rgba[i-2]=sheet.rgba[i-1]=70;sheet.rgba[i]=255;}
        for(uint32_t d=0;d<a.directions;++d){const auto& f=a.frames[size_t(d)*a.framesPerDirection];
            for(uint32_t y=0;y<f.image.height;++y)for(uint32_t x=0;x<f.image.width;++x){const size_t p=(size_t(y)*f.image.width+x)*4;if(!f.image.rgba[p+3])continue;
                const int X=int(d*90+45)+f.x+int(x),Y=110+f.y+int(y);if(X<0||Y<0||X>=int(sheet.width)||Y>=120)continue;std::copy_n(f.image.rgba.begin()+p,4,sheet.rgba.begin()+(size_t(Y)*sheet.width+X)*4);}}
        writeBmp(std::filesystem::path(argv[4]).string(),sheet);std::cout<<a.directions<<" directions\n";
        return 0;
    }
    if(mode==L"text") {
        // UiDump GAME text FONT PALETTE "TEXT" OUT.bmp: a string in one of the game's fonts, white, on grey.
        const std::string font=std::filesystem::path(argv[3]).string(),pal=std::filesystem::path(argv[4]).string(),text=std::filesystem::path(argv[5]).string();
        const d2::Font f(archives,d2::decodePalette(archives.read("data\\global\\palette\\"+pal+"\\pal.dat")),font);
        auto img=f.render(text,{255,255,255},true);
        for(size_t i=0;i<img.rgba.size();i+=4)if(!img.rgba[i+3]){img.rgba[i]=img.rgba[i+1]=img.rgba[i+2]=60;img.rgba[i+3]=255;}
        writeBmp(std::filesystem::path(argv[6]).string(),img);
        std::cout<<"line height "<<f.lineHeight()<<", width "<<f.width(text)<<"\n";
        return 0;
    }
    if(mode==L"hud") {
        // The 800x600 control panel composed from its pieces, to check the layout. Globes at LIFE and MANA percent (argv 3, 4).
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        const float life=argc>3?std::stof(std::filesystem::path(argv[3]).string())/100:1,mana=argc>4?std::stof(std::filesystem::path(argv[4]).string())/100:1;
        d2::Image canvas{800,600,d2::Bytes(800*600*4,0)};
        for(size_t i=0;i<canvas.rgba.size();i+=4){canvas.rgba[i]=40;canvas.rgba[i+1]=60;canvas.rgba[i+2]=40;canvas.rgba[i+3]=255;}
        auto blit=[&](const d2::Image& f,int x,int y,int skipTop=0){for(uint32_t r=uint32_t(skipTop);r<f.height;++r)for(uint32_t c=0;c<f.width;++c){const size_t s=(size_t(r)*f.width+c)*4;if(!f.rgba[s+3])continue;const int X=x+int(c),Y=y+int(r);if(X<0||Y<0||X>=800||Y>=600)continue;std::copy_n(f.rgba.begin()+s,4,canvas.rgba.begin()+(size_t(Y)*800+X)*4);}};
        const auto panel=archives.read("data\\global\\ui\\panel\\800ctrlpnl7.dc6"),globes=archives.read("data\\global\\ui\\panel\\hlthmana.dc6"),glass=archives.read("data\\global\\ui\\panel\\overlap.dc6");
        auto frame=[&](const d2::Bytes& b,uint32_t f){return d2::decodeDc6(b,palette,f);};
        const int gx=std::atoi(std::getenv("GX")?std::getenv("GX"):"30"),gy=std::atoi(std::getenv("GY")?std::getenv("GY"):"0");
        const int ox=std::atoi(std::getenv("OX")?std::getenv("OX"):"28"),oy=std::atoi(std::getenv("OY")?std::getenv("OY"):"5");
        const int xs[]={0,165,293,421,549,683};const uint32_t fs[]={0,1,2,3,4,5};
        for(int i=0;i<6;++i){const auto f=frame(panel,fs[i]);blit(f,xs[i],600-int(f.height));}
        // Globe fills over the holders, the glass over them.
        {const auto f=frame(globes,0);blit(f,gx,600-gy-int(f.height),int(f.height)-int(f.height*life));}
        {const auto f=frame(globes,1);blit(f,800-gx-int(f.width),600-gy-int(f.height),int(f.height)-int(f.height*mana));}
        {const auto f=frame(glass,0);blit(f,ox,600-oy-int(f.height));}
        {const auto f=frame(glass,1);blit(f,800-ox-int(f.width),600-oy-int(f.height));}
        writeBmp(std::filesystem::path(argv[argc>5?5:1]).string()=="C:/games/Diablo II 1.10"?"hud.bmp":std::filesystem::path(argv[argc-1]).string(),canvas);
        return 0;
    }
    if(mode==L"list") {
        std::string filter=argc>3?std::filesystem::path(argv[3]).string():"";std::transform(filter.begin(),filter.end(),filter.begin(),::tolower);
        std::set<std::string> names;
        for(const auto* mpq:{L"Patch_D2.mpq",L"d2exp.mpq",L"d2data.mpq",L"d2char.mpq"}) {
            HANDLE archive=nullptr;if(!SFileOpenArchive((root/mpq).c_str(),0,MPQ_OPEN_READ_ONLY,&archive))continue;
            // A filter that is a path (data\...) is searched as it is, anything else within data\global\ui.
            const bool isPath=filter.rfind("data",0)==0;
            SFILE_FIND_DATA data{};HANDLE find=SFileFindFirstFile(archive,isPath?"*":"data\\global\\ui\\*",&data,nullptr);
            if(find){do{std::string n=data.cFileName;std::string low=n;std::transform(low.begin(),low.end(),low.begin(),::tolower);
                if(filter.empty()||low.find(filter)!=std::string::npos)names.insert(low);}while(SFileFindNextFile(find,&data));SFileFindClose(find);}
            SFileCloseArchive(archive);
        }
        for(const auto& n:names){try{describe(n,archives.read(n));}catch(...){std::cout<<n<<" (unreadable)\n";}}
        std::cout<<names.size()<<" files\n";
    } else {
        std::string name=std::filesystem::path(argv[3]).string();const std::string prefix=std::filesystem::path(argv[4]).string();
        std::replace(name.begin(),name.end(),'/','\\');
        const std::string act=argc>5?std::filesystem::path(argv[5]).string():"units";
        const bool actNumber=!act.empty()&&std::all_of(act.begin(),act.end(),::isdigit);
        const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\"+(actNumber?"act"+act:act)+"\\pal.dat"));
        const auto b=archives.read(name);describe(name,b);
        // 800x600 screens (12 frames of 256/256/256/32 by 256/256/88): assembled, row by row from the top.
        if(d2::dc6FrameCount(b)==12&&d2::decodeDc6(b,palette,0).width==256&&d2::decodeDc6(b,palette,3).width==32) {
            d2::Image screen{800,600,d2::Bytes(800*600*4,0)};
            for(uint32_t f=0;f<12;++f){const auto piece=d2::decodeDc6(b,palette,f);const uint32_t x0=(f%4)*256,y0=(f/4)*256;
                for(uint32_t y=0;y<piece.height&&y0+y<600;++y)for(uint32_t x=0;x<piece.width&&x0+x<800;++x)std::copy_n(piece.rgba.begin()+(size_t(y)*piece.width+x)*4,4,screen.rgba.begin()+(size_t(y0+y)*800+x0+x)*4);}
            for(size_t i=3;i<screen.rgba.size();i+=4)screen.rgba[i]=255;
            writeBmp(prefix+"screen.bmp",screen);
        }
        // One sheet: the frames side by side on grey, 8 px apart, top aligned.
        std::vector<d2::Image> frames;uint32_t w=0,h=0;
        for(uint32_t f=0;f<d2::dc6FrameCount(b);++f){frames.push_back(d2::decodeDc6(b,palette,f));w+=frames.back().width+8;h=std::max(h,frames.back().height);}
        d2::Image sheet{w,h,d2::Bytes(size_t(w)*h*4,0)};
        for(size_t i=0;i<sheet.rgba.size();i+=4){sheet.rgba[i]=sheet.rgba[i+1]=sheet.rgba[i+2]=90;sheet.rgba[i+3]=255;}
        uint32_t x0=0;
        for(const auto& f:frames){for(uint32_t y=0;y<f.height;++y)for(uint32_t x=0;x<f.width;++x){const size_t s=(size_t(y)*f.width+x)*4;if(!f.rgba[s+3])continue;std::copy_n(f.rgba.begin()+s,4,sheet.rgba.begin()+(size_t(y)*w+x0+x)*4);}x0+=f.width+8;}
        writeBmp(prefix+"sheet.bmp",sheet);
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
