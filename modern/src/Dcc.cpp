// DCC algorithm adapted from Diablerie by Alexander Egorov (MIT).
// See ../external/Diablerie-LICENSE and ../external/Diablerie-NOTICE.md.
#include "Assets.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
namespace d2 { namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
struct Bits {
    std::span<const uint8_t> bytes;size_t at{},end{};
    uint32_t read(unsigned n) {
        require(n<=32&&at<=end&&n<=end-at,"Truncated DCC bit stream");
        uint32_t v=0;for(unsigned i=0;i<n;++i,++at)v|=uint32_t((bytes[at/8]>>(at%8))&1)<<i;return v;
    }
    int32_t signedRead(unsigned n) {
        uint32_t v=read(n);if(n&&n<32&&(v&(1u<<(n-1))))v|=~((1u<<n)-1);
        return std::bit_cast<int32_t>(v);
    }
    Bits take(size_t n){require(at<=end&&n<=end-at,"DCC stream outside direction");Bits r{bytes,at,at+n};at+=n;return r;}
};
struct Cell {int x,y,w,h,slot;std::array<uint8_t,4> colors{};bool equal=false;};
struct Frame {int x,y,w,h;std::vector<Cell> cells;};
std::vector<int> partitions(int offset,int length) {
    std::vector<int> r;int first=4-offset%4;
    if(length<=first+1)return {length};
    r.push_back(first);length-=first;
    while(length>5){r.push_back(4);length-=4;}
    r.push_back(length);return r;
}
}
Animation decodeDcc(std::span<const uint8_t> b,const Palette& palette) {
    require(b.size()>=15&&b[0]==0x74&&b[1]==6,"Unsupported or truncated DCC header");
    Bits header{b,16,b.size()*8};Animation result;result.directions=header.read(8);result.framesPerDirection=header.read(32);
    require(result.directions&&result.directions<=32&&result.framesPerDirection&&result.framesPerDirection<=256,"DCC animation count exceeds limit");
    header.read(32);header.read(32);
    std::vector<size_t> offsets;
    for(uint32_t d=0;d<result.directions;++d) {
        size_t p=header.read(32);require(p>=15+4*result.directions&&p<b.size()&&(offsets.empty()||p>offsets.back()),"Invalid DCC direction offset");offsets.push_back(p);
    }
    offsets.push_back(b.size());size_t budget=0;
    constexpr unsigned widths[]={0,1,2,4,6,8,10,12,14,16,20,24,26,28,30,32};
    for(uint32_t direction=0;direction<result.directions;++direction) {
        Bits bits{b,offsets[direction]*8,offsets[direction+1]*8};bits.read(32);auto flags=bits.read(2);
        unsigned sizes[7];for(auto& n:sizes)n=widths[bits.read(4)];
        std::vector<Frame> frames;int left=32767,top=32767,right=-32768,bottom=-32768;size_t optional=0;
        for(uint32_t f=0;f<result.framesPerDirection;++f) {
            bits.read(sizes[0]);auto w=bits.read(sizes[1]),h=bits.read(sizes[2]);
            int x=bits.signedRead(sizes[3]),y=bits.signedRead(sizes[4]);
            optional+=bits.read(sizes[5]);bits.read(sizes[6]);const auto bottomUp=bits.read(1);
            require(!bottomUp,"Bottom-up DCC frames are not supported yet");
            require(w&&h&&w<=2048&&h<=2048&&x>=-8192&&x<=8192&&y>=-8192&&y<=8192,"Invalid DCC frame bounds");
            y-=int(h)-1;frames.push_back({x,y,int(w),int(h),{}});
            left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+int(w));bottom=std::max(bottom,y+int(h));
        }
        if(optional){bits.take((8-bits.at%8)%8);require(optional<=b.size(),"DCC optional data exceeds file");bits.take(optional*8);}
        const int w=right-left,h=bottom-top;
        require(w<=2048&&h<=2048,"DCC direction bounds exceed limit");
        budget+=size_t(w)*h*4*frames.size();require(budget<=256*1024*1024,"DCC decoded frames exceed 256 MiB");
        size_t equalSize=(flags&2)?bits.read(20):0,maskSize=bits.read(20);
        size_t encodingSize=(flags&1)?bits.read(20):0,rawSize=(flags&1)?bits.read(20):0;
        std::vector<uint8_t> values;for(int i=0;i<256;++i)if(bits.read(1))values.push_back(uint8_t(i));
        require(!values.empty(),"DCC has no palette entries");
        auto equal=bits.take(equalSize),mask=bits.take(maskSize),encoding=bits.take(encodingSize),raw=bits.take(rawSize);
        Bits code=bits;
        const int columns=(w+3)/4,rows=(h+3)/4;
        struct PaletteCell {bool seen=false;std::array<uint8_t,4> codes{};};
        std::vector<PaletteCell> history(size_t(columns)*rows);
        size_t cellBudget=0;
        for(auto& f:frames) {
            const auto ws=partitions(f.x-left,f.w),hs=partitions(f.y-top,f.h);
            int cy=f.y-top;
            for(int ch:hs){int cx=f.x-left;for(int cw:ws) {
                require(++cellBudget<=2000000,"DCC cell count exceeds limit");
                Cell cell{cx,cy,cw,ch,(cy/4)*columns+cx/4};auto& previous=history.at(cell.slot);
                if(previous.seen&&equalSize&&equal.read(1))cell.equal=true;
                else {
                    uint32_t m=previous.seen?mask.read(4):15;
                    const unsigned count=std::popcount(m);bool rawEncoding=count&&encodingSize&&encoding.read(1);
                    std::vector<uint8_t> stack;uint32_t last=0;
                    for(unsigned i=0;i<count;++i) {
                        uint32_t value=0;
                        if(rawEncoding)value=raw.read(8);
                        else {value=last;uint32_t delta;do {delta=code.read(4);value+=delta;require(value<256,"Invalid DCC pixel code");}while(delta==15);}
                        if(value==last)break;
                        require(value<values.size(),"DCC palette index outside table");stack.push_back(uint8_t(value));last=value;
                    }
                    for(unsigned i=0;i<4;++i)if(m&(1u<<i)) {
                        previous.codes[i]=stack.empty()?0:stack.back();if(!stack.empty())stack.pop_back();
                    }
                    previous.seen=true;
                    for(int i=0;i<4;++i)cell.colors[i]=values.at(previous.codes[i]);
                }
                f.cells.push_back(cell);cx+=cw;
            }cy+=ch;}
        }
        struct SavedCell {int w=0,h=0;std::array<uint8_t,25> pixels{};};
        std::vector<SavedCell> saved(history.size());
        for(auto& f:frames) {
            AnimationFrame output{{uint32_t(w),uint32_t(h),Bytes(size_t(w)*h*4,0)},left,top};
            for(auto& cell:f.cells) {
                auto& old=saved.at(cell.slot);std::array<uint8_t,25> pixels{};
                require(cell.w<=5&&cell.h<=5,"Invalid DCC cell size");
                if(cell.equal){if(old.w==cell.w&&old.h==cell.h)pixels=old.pixels;}
                else {
                    unsigned n=cell.colors[0]==cell.colors[1]?0:cell.colors[1]==cell.colors[2]?1:2;
                    for(int i=0;i<cell.w*cell.h;++i)pixels[i]=cell.colors[code.read(n)];
                }
                for(int y=0;y<cell.h;++y)for(int x=0;x<cell.w;++x) {
                    const auto color=pixels[y*cell.w+x];if(!color)continue;
                    const size_t at=(size_t(cell.y+y)*w+cell.x+x)*4;
                    std::copy(palette[color].begin(),palette[color].end(),output.image.rgba.begin()+at);
                }
                old={cell.w,cell.h,pixels};
            }
            result.frames.push_back(std::move(output));
        }
        require(equal.at==equal.end&&mask.at==mask.end&&encoding.at==encoding.end&&raw.at==raw.end,"DCC control stream not fully consumed");
    }
    return result;
}
}
