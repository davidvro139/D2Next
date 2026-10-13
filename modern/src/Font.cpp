#include "Font.h"
#include <algorithm>
#include <stdexcept>
namespace d2 {
namespace {
// The fonts are indexed by Latin-1 code; the string tables arrive as UTF-8 (e.g. the copyright sign).
std::string latin1(const std::string& text) {
    std::string out;out.reserve(text.size());
    for(size_t i=0;i<text.size();++i) {
        const auto c=static_cast<unsigned char>(text[i]);
        if((c==0xC2||c==0xC3)&&i+1<text.size()&&(static_cast<unsigned char>(text[i+1])&0xC0)==0x80){out.push_back(char(((c&0x03)<<6)|(static_cast<unsigned char>(text[i+1])&0x3F)));++i;}
        else out.push_back(char(c));
    }
    return out;
}
}
Font::Font(const Archives& archives,const Palette& palette,const std::string& name) {
    const std::string base="data\\local\\font\\latin\\"+name;
    const auto sheet=archives.read(base+".dc6");
    const auto table=archives.read(base+".tbl");
    // "Woo!", a 12 byte header, then 14 bytes per character: code, ?, width, height, ?, ?, ?, frame, 4 unknown bytes.
    if(table.size()<12||table[0]!='W'||table[1]!='o'||table[2]!='o'||table[3]!='!')throw std::runtime_error("Not a font table: "+base+".tbl");
    const auto count=dc6FrameCount(sheet);
    frames_.reserve(count);
    for(uint32_t f=0;f<count;++f)frames_.push_back(decodeDc6(sheet,palette,f));
    for(size_t offset=12;offset+14<=table.size();offset+=14) {
        const uint16_t code=uint16_t(table[offset]|(table[offset+1]<<8));
        if(code>255)continue;
        Glyph& g=glyphs_[code];
        g.width=table[offset+3];g.height=table[offset+4];g.frame=uint16_t(table[offset+8]|(table[offset+9]<<8));
        g.present=g.frame<frames_.size();
        if(g.present)lineHeight_=std::max<int>(lineHeight_,int(frames_[g.frame].height));
    }
}
int Font::width(const std::string& utf8) const {
    int w=0;
    for(unsigned char c:latin1(utf8))if(glyphs_[c].present)w+=glyphs_[c].width;
    return w;
}
Image Font::render(const std::string& utf8,std::array<uint8_t,3> color,bool centered) const {
    const std::string text=latin1(utf8);
    std::vector<std::string> lines;size_t start=0;
    while(true){const auto end=text.find('\n',start);lines.push_back(text.substr(start,end==std::string::npos?std::string::npos:end-start));if(end==std::string::npos)break;start=end+1;}
    auto lineWidth=[&](const std::string& line){int lw=0;for(unsigned char c:line)if(glyphs_[c].present)lw+=glyphs_[c].width;return lw;};
    int w=1;for(const auto& line:lines)w=std::max(w,lineWidth(line));
    Image image{uint32_t(w),uint32_t(std::max(1,lineHeight_*int(lines.size()))),Bytes(size_t(w)*size_t(std::max(1,lineHeight_*int(lines.size())))*4,0)};
    for(size_t l=0;l<lines.size();++l) {
        int x=centered?(w-lineWidth(lines[l]))/2:0;const int y0=int(l)*lineHeight_;
        for(unsigned char c:lines[l]) {
            const auto& g=glyphs_[c];if(!g.present)continue;
            const auto& f=frames_[g.frame];
            for(uint32_t y=0;y<f.height&&y0+int(y)<int(image.height);++y)for(uint32_t fx=0;fx<f.width;++fx) {
                const int X=x+int(fx);if(X<0||X>=w)continue;
                const size_t s=(size_t(y)*f.width+fx)*4;if(!f.rgba[s+3])continue;
                const size_t d=(size_t(y0+int(y))*image.width+size_t(X))*4;
                for(int k=0;k<3;++k)image.rgba[d+k]=uint8_t(f.rgba[s+k]*color[k]/255);
                image.rgba[d+3]=255;
            }
            x+=g.width;
        }
    }
    return image;
}
const Image& TextCache::get(const std::string& text,std::array<uint8_t,3> color,bool centered) {
    const std::string key=std::to_string(color[0])+","+std::to_string(color[1])+","+std::to_string(color[2])+(centered?"c:":":")+text;
    auto found=images_.find(key);
    if(found==images_.end())found=images_.emplace(key,std::make_unique<Image>(font_.render(text,color,centered))).first;
    return *found->second;
}
}
