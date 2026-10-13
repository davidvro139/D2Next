// Writes every frame of an object's animation (objects.txt row, mode) to BMP files and prints the frame offsets.
// usage: ObjectArtDump GAME_DIR ROW MODE OUT_PREFIX
#include "Tables.h"
#include <fstream>
#include <iostream>
static void writeBmp(const std::string& path,const d2::Image& img) {
    const uint32_t w=img.width,h=img.height,row=w*4,size=54+row*h;
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](uint16_t v){out.put(char(v&255));out.put(char(v>>8));};
    auto u32=[&](uint32_t v){u16(uint16_t(v&0xffff));u16(uint16_t(v>>16));};
    out.put('B');out.put('M');u32(size);u32(0);u32(54);u32(40);u32(w);u32(h);u16(1);u16(32);u32(0);u32(row*h);u32(2835);u32(2835);u32(0);u32(0);
    for(uint32_t y=h;y-->0;)for(uint32_t x=0;x<w;++x){const size_t i=(size_t(y)*w+x)*4;const uint8_t a=img.rgba[i+3];
        // Transparent pixels in magenta so the shape is obvious.
        out.put(char(a?img.rgba[i+2]:255));out.put(char(a?img.rgba[i+1]:0));out.put(char(a?img.rgba[i]:255));out.put(char(255));}
}
int main(int argc,char** argv){try{
    d2::Archives archives(argv[1]);
    const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
    d2::ObjectTable table(archives);d2::ObjectArt art(archives,palette,table);
    const auto* animation=art.get(uint32_t(std::stoul(argv[2])),uint32_t(std::stoul(argv[3])));
    if(!animation){std::cout<<"no animation\n";return 1;}
    std::cout<<animation->directions<<" directions, "<<animation->framesPerDirection<<" frames per direction\n";
    for(size_t i=0;i<animation->frames.size();++i){const auto& f=animation->frames[i];size_t opaque=0;for(size_t p=3;p<f.image.rgba.size();p+=4)opaque+=f.image.rgba[p]!=0;
        std::cout<<"frame "<<i<<": "<<f.image.width<<"x"<<f.image.height<<" at "<<f.x<<","<<f.y<<", "<<opaque<<" opaque pixels\n";
        writeBmp(std::string(argv[4])+std::to_string(i)+".bmp",f.image);}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
