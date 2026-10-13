#include "Assets.h"
#include <iostream>
#include <stdexcept>

static void require(bool value) { if(!value) throw std::runtime_error("Decoder test failed"); }
static void put(d2::Bytes& b,size_t n,uint32_t value) { for(int i=0;i<4;++i) b.at(n+i)=uint8_t(value>>(i*8)); }
template<class F> void rejects(F fn) { bool caught=false; try{fn();}catch(const std::runtime_error&){caught=true;} require(caught); }
int main() {
    try {
        d2::Bytes colors(768); colors[3]=30; colors[4]=20; colors[5]=10;
        const auto palette=d2::decodePalette(colors);
        require(palette[1][0]==10&&palette[1][2]==30);
        // Two bottom-up rows: lower [color 1, transparent], upper [0, 1].
        d2::Bytes b(60); put(b,0,6); put(b,16,1); put(b,20,1); put(b,24,28);
        put(b,32,2); put(b,36,2); put(b,56,8);
        for(uint8_t v:{1,1,0x81,0x80,2,0,1,0x80}) b.push_back(v);
        auto image=d2::decodeDc6(b,palette);
        require(image.width==2&&image.height==2);
        require(image.rgba[3]==255); // Palette index zero is opaque when explicitly encoded.
        require(image.rgba[4]==10&&image.rgba[8]==10&&image.rgba[15]==0);
        auto flipped=b; put(flipped,28,1);
        require(d2::decodeDc6(flipped,palette).rgba[7]==0);
        rejects([&]{d2::decodeDc6(b,palette,1);});
        for(size_t n=0;n<b.size();++n) rejects([&]{d2::decodeDc6(std::span(b).first(n),palette);});
        auto bad=b; bad[60]=3; rejects([&]{d2::decodeDc6(bad,palette);});
        bad=b; put(bad,32,0xffffffff); rejects([&]{d2::decodeDc6(bad,palette);});
        bad=b; put(bad,24,0xffffffff); rejects([&]{d2::decodeDc6(bad,palette);});
        bad=b; put(bad,24,4); rejects([&]{d2::decodeDc6(bad,palette);});
        bad=b; put(bad,16,0xffffffff); rejects([&]{d2::decodeDc6(bad,palette);});
        rejects([&]{d2::decodePalette(std::span(colors).first(767));});
        std::cout<<"PASS: palette, DC6 orientation/transparency, frame bounds, truncation, malformed runs and size limits\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
