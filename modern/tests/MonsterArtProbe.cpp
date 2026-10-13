// Prints what the client knows about a monster class's art (MonsterTable from monstats.txt + monstats2.txt) and tries to
// load its neutral animation: for a monster that is in the game but not drawn.
// MonsterArtProbe <game dir> <class id>...
#include "Assets.h"
#include "Tables.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <algorithm>
int main(int argc,char** argv){try {
    if(argc<3)return 2;
    d2::Archives archives(argv[1]);
    d2::MonsterTable table(archives);
    d2::Palette palette{};for(int i=0;i<256;++i)palette[size_t(i)]={uint8_t(i),uint8_t(i),uint8_t(i),255};
    d2::UnitArt art(archives,palette);
    for(int a=2;a<argc;++a) {
        const uint32_t id=uint32_t(std::stoul(argv[a]));
        const auto* s=table.find(id);
        if(!s){std::cout<<id<<": not in the table\n";continue;}
        std::cout<<id<<": "<<s->id<<" code "<<s->code<<" weapon "<<s->baseWeapon<<" modes";
        for(int m=0;m<16;++m)if(s->hasMode[m])std::cout<<" "<<d2::monsterModeTokens[m]<<"/"<<int(s->directions[m]);
        std::cout<<"\n  components:";
        for(int i=0;i<16;++i)if(!s->variants[i].empty()){std::cout<<" "<<i<<"=";for(const auto& v:s->variants[i])std::cout<<(v.empty()?"-":v)<<",";}
        try { // the COF's layers for the neutral mode: component, shadow, selectable, transparent, effect, weapon class
            const auto cof=archives.read("data\\global\\monsters\\"+s->code+"\\cof\\"+s->code+(std::getenv("MONSTERART_MODE")?d2::monsterModeTokens[std::atoi(std::getenv("MONSTERART_MODE"))]:"nu")+s->baseWeapon+".cof");
            static const char* names[16]={"hd","tr","lg","ra","la","rh","lh","sh","s1","s2","s3","s4","s5","s6","s7","s8"};
            std::cout<<"\n  cof: "<<int(cof[0])<<" layers, "<<int(cof[2])<<" directions";
            for(unsigned l=0;l<cof[0];++l){const size_t p=28+9*l;std::cout<<" | "<<(cof[p]<16?names[cof[p]]:"?")<<" shadow "<<int(cof[p+1])<<" trans "<<int(cof[p+3])<<" effect "<<int(cof[p+4]);}
        } catch(const std::exception& e){std::cout<<"\n  cof: "<<e.what();}
        try { // palshift.dat: how many maps, and how far map 0 is from the identity
            const auto shift=archives.read("data\\global\\monsters\\"+s->code+"\\cof\\palshift.dat");
            int changed=0;for(int i=0;i<256&&i<int(shift.size());++i)changed+=shift[size_t(i)]!=i;
            std::cout<<"\n  palshift: "<<shift.size()/256<<" maps, map 0 changes "<<changed<<" of 256 indices";
        } catch(const std::exception&){std::cout<<"\n  palshift: none";}
        const auto* neutral=art.get(id,1,0u);
        std::cout<<"\n  neutral art: "<<(neutral?std::to_string(neutral->directions)+" directions x "+std::to_string(neutral->framesPerDirection):"none")<<"\n";
        if(const char* dump=std::getenv("MONSTERART_DUMP");dump&&neutral) {
            // MONSTERART_DUMP=file.bmp: the first 8 neutral frames of direction 0, in the Act I palette, on grey.
            d2::UnitArt actArt(archives,d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")));
            const int dumpMode=std::getenv("MONSTERART_MODE")?std::atoi(std::getenv("MONSTERART_MODE")):1;const auto* a=actArt.get(id,uint32_t(dumpMode),0u);if(!a)throw std::runtime_error("no art in that mode");const unsigned n=std::min(8u,a->framesPerDirection),cell=128;
            const unsigned W=cell*n,H=cell;std::vector<uint8_t> px(size_t(W)*H*4,96);
            for(unsigned f=0;f<n;++f){const auto& fr=a->frames[f];
                for(unsigned y=0;y<fr.image.height;++y)for(unsigned x=0;x<fr.image.width;++x){
                    const uint8_t* s=&fr.image.rgba[(size_t(y)*fr.image.width+x)*4];if(!s[3])continue;
                    const int X=int(f*cell+cell/2)+fr.x+int(x),Y=int(cell*3/4)+fr.y+int(y);if(X<0||Y<0||X>=int(W)||Y>=int(H))continue;
                    uint8_t* d=&px[(size_t(H-1-Y)*W+X)*4];d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=255;}}
            std::ofstream out(dump,std::ios::binary);
            const uint32_t size=54+uint32_t(px.size());uint8_t h[54]={'B','M'};
            auto put=[&](int at,uint32_t v){for(int i=0;i<4;++i)h[at+i]=uint8_t(v>>(8*i));};
            put(2,size);put(10,54);put(14,40);put(18,W);put(22,H);h[26]=1;h[28]=32;put(34,uint32_t(px.size()));
            out.write(reinterpret_cast<const char*>(h),54);out.write(reinterpret_cast<const char*>(px.data()),std::streamsize(px.size()));
        }
        if(!neutral)try {d2::loadCharacter(archives,palette,"nu",d2::monsterAppearance(*s,0),"monsters");}
            catch(const std::exception& e){std::cout<<"  why: "<<e.what()<<"\n";}
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
