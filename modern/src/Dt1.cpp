#include "Assets.h"
#include <algorithm>
#include <stdexcept>

namespace d2 {
namespace {
void range(std::span<const uint8_t> b,size_t at,size_t length) {
    if(at>b.size()||length>b.size()-at) throw std::runtime_error("DT1 range outside file");
}
uint32_t u32(std::span<const uint8_t> b,size_t at) {
    range(b,at,4);
    return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);
}
uint16_t u16(std::span<const uint8_t> b,size_t at) { range(b,at,2); return uint16_t(b[at]|uint16_t(b[at+1])<<8); }
int32_t s16(std::span<const uint8_t> b,size_t at) { const auto v=u16(b,at); return v<32768?v:int32_t(v)-65536; }
struct Block { int32_t x,y; uint16_t format; size_t at,length; };
void pixel(Image& image,uint32_t x,uint32_t y,const Palette& palette,uint8_t color) {
    if(x>=image.width||y>=image.height) throw std::runtime_error("DT1 pixel outside tile");
    std::copy(palette[color].begin(),palette[color].end(),image.rgba.begin()+(size_t(y)*image.width+x)*4);
}
}
uint32_t dt1TileCount(std::span<const uint8_t> b) {
    range(b,0,276);
    if(u32(b,0)!=7||u32(b,4)!=6) throw std::runtime_error("Unsupported DT1 version (expected 7.6)");
    const auto count=u32(b,268), headers=u32(b,272);
    if(!count||count>8192||headers<276) throw std::runtime_error("Invalid DT1 tile table");
    range(b,headers,size_t(count)*96);
    return count;
}
Tile decodeDt1(std::span<const uint8_t> b,const Palette& palette,uint32_t tileIndex) {
    const auto count=dt1TileCount(b);
    if(tileIndex>=count) throw std::runtime_error("DT1 tile index out of range");
    const size_t h=u32(b,272)+size_t(tileIndex)*96;
    Tile tile;
    tile.orientation=u32(b,h+20); tile.mainIndex=u32(b,h+24);
    tile.subIndex=u32(b,h+28); tile.rarity=u32(b,h+32);
    tile.roofHeight=s16(b,h+4);tile.material=b[h+6];tile.animated=b[h+7]!=0;
    std::copy_n(b.begin()+h+40,25,tile.collision.begin());
    const size_t start=u32(b,h+72), length=u32(b,h+76), blocks=u32(b,h+80);
    if(blocks>4096) throw std::runtime_error("DT1 block count exceeds limit");
    if(!blocks) { tile.image={1,1,Bytes(4,0)}; return tile; }
    if(start<size_t(u32(b,272))+size_t(count)*96||length<blocks*20) throw std::runtime_error("Invalid DT1 block table");
    range(b,start,length);
    std::vector<Block> entries;
    int32_t left=32767,top=32767,right=-32768,bottom=-32768;
    for(size_t i=0;i<blocks;++i) {
        const size_t p=start+i*20;
        const auto format=u16(b,p+8);
        if(format!=1&&format!=0x1001&&format!=0x2005) throw std::runtime_error("Unsupported DT1 block format");
        const size_t offset=u32(b,p+16), size=u32(b,p+10);
        if(offset<blocks*20||offset>length||size>length-offset) throw std::runtime_error("Invalid DT1 block payload range");
        if(format==1&&size!=256) throw std::runtime_error("DT1 isometric block must contain 256 pixels");
        Block block{s16(b,p),s16(b,p+2),format,start+offset,size};
        entries.push_back(block);
        left=std::min(left,block.x); top=std::min(top,block.y);
        right=std::max(right,block.x+32); bottom=std::max(bottom,block.y+(format==1?15:32));
    }
    const uint32_t width=right-left,height=bottom-top;
    if(width>4096||height>4096||uint64_t(width)*height>4*1024*1024) throw std::runtime_error("DT1 decoded tile exceeds image limit");
    tile.originX=left; tile.originY=top;
    tile.image={width,height,Bytes(size_t(width)*height*4,0)};
    constexpr int starts[]={14,12,10,8,6,4,2,0,2,4,6,8,10,12,14};
    constexpr int lengths[]={4,8,12,16,20,24,28,32,28,24,20,16,12,8,4};
    for(const auto& block:entries) {
        size_t p=block.at,end=p+block.length;
        if(block.format==1) {
            for(int y=0;y<15;++y) for(int x=0;x<lengths[y];++x)
                pixel(tile.image,block.x-left+starts[y]+x,block.y-top+y,palette,b[p++]);
        } else {
            uint32_t x=0,y=0;
            while(p<end) {
                if(end-p<2) throw std::runtime_error("Truncated DT1 RLE command");
                const auto skip=b[p++],run=b[p++];
                if(!skip&&!run) { x=0; if(++y>32) throw std::runtime_error("DT1 RLE exceeds block height"); continue; }
                if(y>=32||skip>32-x||run>32-x-skip||run>end-p) throw std::runtime_error("Invalid DT1 RLE run");
                x+=skip;
                for(uint32_t j=0;j<run;++j) pixel(tile.image,block.x-left+x++,block.y-top+y,palette,b[p++]);
            }
        }
    }
    return tile;
}
Image dt1Atlas(std::span<const uint8_t> bytes,const Palette& palette,uint32_t page) {
    const auto total=dt1TileCount(bytes);
    if(page>=(total+tilesPerPage-1)/tilesPerPage) throw std::runtime_error("DT1 atlas page out of range");
    const uint32_t first=page*tilesPerPage,count=std::min(tilesPerPage,total-first);
    std::vector<Tile> tiles;
    uint32_t cellWidth=0,cellHeight=0;
    for(uint32_t i=0;i<count;++i) {
        tiles.push_back(decodeDt1(bytes,palette,first+i));
        cellWidth=std::max(cellWidth,tiles.back().image.width+16);
        cellHeight=std::max(cellHeight,tiles.back().image.height+16);
    }
    const uint32_t columns=std::min(4u,count),rows=(count+columns-1)/columns;
    const uint32_t width=columns*cellWidth,height=rows*cellHeight;
    if(width>4096||height>4096||uint64_t(width)*height>16*1024*1024) throw std::runtime_error("DT1 atlas page too large; use individual tile mode");
    Image atlas{width,height,Bytes(size_t(width)*height*4,0)};
    for(uint32_t i=0;i<count;++i) {
        const auto& image=tiles[i].image;
        const uint32_t ox=(i%columns)*cellWidth+(cellWidth-image.width)/2;
        const uint32_t oy=(i/columns)*cellHeight+(cellHeight-image.height)/2;
        for(uint32_t y=0;y<image.height;++y) for(uint32_t x=0;x<image.width;++x) {
            const size_t src=(size_t(y)*image.width+x)*4,dst=(size_t(y+oy)*width+x+ox)*4;
            std::copy_n(image.rgba.begin()+src,4,atlas.rgba.begin()+dst);
        }
    }
    return atlas;
}
}
