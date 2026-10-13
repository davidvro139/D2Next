#include "Assets.h"
#include <iostream>
#include <stdexcept>
static void require(bool b){if(!b)throw std::runtime_error("Animation test failed");}
struct Writer {
    d2::Bytes b;size_t bit=0;
    void put(uint32_t v,unsigned n){for(unsigned i=0;i<n;++i,++bit){if(bit/8>=b.size())b.push_back(0);b[bit/8]|=((v>>i)&1)<<(bit%8);}}
};
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}require(caught);}
int main(int argc,char** argv){try {
    d2::Palette palette{};palette[1]={25,50,75,255};
    Writer w;w.put(0x74,8);w.put(6,8);w.put(1,8);w.put(2,32);w.put(1,32);w.put(0,32);w.put(19,32);
    w.put(0,32);w.put(2,2);for(auto n:{0,1,1,0,0,0,0})w.put(n,4);
    for(int f=0;f<2;++f){w.put(1,1);w.put(1,1);w.put(0,1);}
    w.put(1,20);w.put(0,20);for(int i=0;i<256;++i)w.put(i<2,1);
    w.put(1,1); // Second frame reuses the first cell.
    w.put(1,4);w.put(0,4);w.put(0,1);
    auto a=d2::decodeDcc(w.b,palette);
    require(a.directions==1&&a.framesPerDirection==2&&a.frames.size()==2);
    require(a.frames[0].image.rgba==d2::Bytes({25,50,75,255}));
    require(a.frames[0].image.rgba==a.frames[1].image.rgba);
    for(size_t n=0;n<w.b.size();++n)rejects([&]{d2::decodeDcc(std::span(w.b).first(n),palette);});
    auto bad=w.b;bad[0]=0;rejects([&]{d2::decodeDcc(bad,palette);});
    bad=w.b;bad[2]=33;rejects([&]{d2::decodeDcc(bad,palette);});
    bad=w.b;bad[15]=0;rejects([&]{d2::decodeDcc(bad,palette);});
    // Raw palette updates, inherited palette entries, signed anchors, two streams.
    Writer raw;raw.put(0x74,8);raw.put(6,8);raw.put(1,8);raw.put(2,32);raw.put(1,32);raw.put(0,32);raw.put(19,32);
    raw.put(0,32);raw.put(1,2);for(auto n:{0,1,1,2,2,0,0})raw.put(n,4);
    for(int f=0;f<2;++f){raw.put(1,1);raw.put(1,1);raw.put(3,2);raw.put(2,2);raw.put(0,1);}
    raw.put(4,20);raw.put(2,20);raw.put(24,20);for(int i=0;i<256;++i)raw.put(i<3,1);
    raw.put(1,4);raw.put(3,2);raw.put(1,8);raw.put(1,8);raw.put(2,8);raw.put(0,2);
    palette[2]={90,80,70,255};auto changed=d2::decodeDcc(raw.b,palette);
    require(changed.frames[0].x==-1&&changed.frames[0].y==-2);
    require(changed.frames[0].image.rgba==d2::Bytes({25,50,75,255}));
    require(changed.frames[1].image.rgba==d2::Bytes({90,80,70,255}));
    // Bottom-up flag: explicitly rejected rather than silently misrendered.
    bad=w.b;bad[27]|=1;rejects([&]{d2::decodeDcc(bad,palette);});
    if(argc>1) {
        d2::Archives archives(argv[1]);auto pal=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
        for(const auto* mode:{"tn","tw"}) {
            auto character=d2::loadCharacter(archives,pal,mode);
            size_t opaque=0;for(const auto& f:character.frames) {
                size_t pixels=0;for(size_t p=3;p<f.image.rgba.size();p+=4)pixels+=f.image.rgba[p]!=0;
                require(pixels>100);opaque+=pixels;
            }
            require(character.frames[0].image.rgba!=character.frames[1].image.rgba);
            std::cout<<mode<<": "<<character.directions<<" directions, "<<character.framesPerDirection<<" frames per direction\n";
        }
    }
    std::cout<<"PASS: DCC literal/raw pixels, palette updates, signed anchors, equal-cell reuse, truncation and invalid headers\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
