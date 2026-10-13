// usage: ObjectTableProbe GAME_DIR TEXT
#include "Assets.h"
#include "Tables.h"
#include <algorithm>
#include <iostream>

int main(int argc,char** argv){try{
    if(argc<3)throw std::runtime_error("usage: ObjectTableProbe GAME_DIR TEXT");
    d2::Archives archives(argv[1]);d2::StringTable strings(archives);
    const auto bytes=archives.read("data\\global\\excel\\objects.txt");
    d2::TextTable objects(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    std::string needle=argv[2];std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});
    for(size_t r=0;r<objects.rows();++r) {
        const auto name=objects.cell(r,"Name");
        const auto display=strings.has(name)?strings.get(name):name;
        std::string hay=name+" "+display;
        std::transform(hay.begin(),hay.end(),hay.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(hay.find(needle)==std::string::npos)continue;
        std::cout<<"row "<<r<<" name '"<<name<<"' display '"<<display<<"' token "<<objects.cell(r,"Token")
            <<" draw "<<objects.cell(r,"Draw")<<" selectable0 "<<objects.cell(r,"Selectable0")
            <<" framecnt0 "<<objects.cell(r,"FrameCnt0")<<" start0 "<<objects.cell(r,"Start0")
            <<" xoff "<<objects.cell(r,"Xoffset")<<" yoff "<<objects.cell(r,"Yoffset")<<"\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
