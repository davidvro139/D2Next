// Loads monster and NPC animations through monstats/monstats2 and the COF/DCC files of the game install, and writes a contact sheet.
// usage: UnitSpriteTests GAME_DIR [sheet.bmp]
#include "Tables.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
static void writeBmp(const char* path,unsigned w,unsigned h,const std::vector<uint8_t>& rgba) {
    std::ofstream f(path,std::ios::binary);const uint32_t size=54+w*h*4;
    uint8_t header[54]={'B','M'};auto put=[&](int at,uint32_t v){for(int i=0;i<4;++i)header[at+i]=uint8_t(v>>(8*i));};
    put(2,size);put(10,54);put(14,40);put(18,w);put(22,uint32_t(-int(h)));header[26]=1;header[28]=32;put(34,w*h*4);
    f.write(reinterpret_cast<char*>(header),54);
    for(size_t i=0;i<rgba.size();i+=4){uint8_t px[4]={rgba[i+2],rgba[i+1],rgba[i],255};f.write(reinterpret_cast<char*>(px),4);}
}
int main(int argc,char** argv){try {
    require(argc>1,"usage: UnitSpriteTests GAME_DIR [sheet.bmp]");
    d2::Archives archives(argv[1]);d2::MonsterTable table(archives);
    require(table.size()>400,"monster table too small");
    auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
    struct Want{uint32_t id;const char* name;};
    const Want wanted[]={{147,"Gheed"},{148,"Akara"},{150,"Kashya"},{154,"Charsi"},{155,"Warriv"},{152,"Rogue"},{5,"Zombie"},{19,"Fallen"},{63,"Quill Rat"},{16,"Skeleton"},{179,"Cow"}};
    std::vector<d2::AnimationFrame> firsts;
    unsigned attempted=0,failed=0;
    for(const auto& want:wanted) {
        const auto* sprite=table.find(want.id);require(sprite,std::string("no sprite entry for ")+want.name);
        const auto appearance=d2::monsterAppearance(*sprite,want.id);
        for(const char* mode:{"nu","wl"}) {
            int m=0;while(std::string(d2::monsterModeTokens[m])!=mode)++m;
            if(!sprite->hasMode[m])continue;
            ++attempted;
            try {
                auto animation=d2::loadCharacter(archives,palette,mode,appearance,"monsters");
                size_t opaque=0;for(const auto& f:animation.frames)for(size_t p=3;p<f.image.rgba.size();p+=4)opaque+=f.image.rgba[p]!=0;
                require(opaque>50,std::string("empty animation ")+want.name+" "+mode);
                std::cout<<want.name<<" ("<<sprite->code<<") "<<mode<<": "<<animation.directions<<" directions, "<<animation.framesPerDirection<<" frames\n";
                if(std::string(mode)=="nu")firsts.push_back(animation.frames[animation.frames.size()>=1?0:0]);
            } catch(const std::exception& e){++failed;std::cerr<<"FAIL: "<<want.name<<" "<<mode<<": "<<e.what()<<"\n";}
        }
    }
    if(argc>2&&!firsts.empty()) {
        const unsigned cell=160;const unsigned columns=6,rows=(unsigned(firsts.size())+columns-1)/columns;
        std::vector<uint8_t> sheet(size_t(cell)*columns*cell*rows*4,40);
        for(size_t i=0;i<firsts.size();++i) {
            const auto& f=firsts[i];const int ox=int(i%columns)*cell+cell/2+f.x,oy=int(i/columns)*cell+cell*3/4+f.y;
            for(unsigned y=0;y<f.image.height;++y)for(unsigned x=0;x<f.image.width;++x) {
                int px=ox+int(x),py=oy+int(y);if(px<0||py<0||px>=int(cell*columns)||py>=int(cell*rows))continue;
                const auto* src=&f.image.rgba[(size_t(y)*f.image.width+x)*4];if(!src[3])continue;
                std::copy_n(src,4,&sheet[(size_t(py)*cell*columns+px)*4]);
            }
        }
        writeBmp(argv[2],cell*columns,cell*rows,sheet);
    }
    require(attempted>0,"no monster animations were tested");
    require(failed==0,std::to_string(failed)+" of "+std::to_string(attempted)+" monster animations failed");
    std::cout<<"PASS: monster table and "<<attempted<<" animations\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
