// Prints the first lines of a text asset, or its size and DC6 frame count. usage: HeaderProbe GAME_DIR PATH [lines | size]
#include "Tables.h"
#include <iostream>
int main(int argc,char** argv){try{d2::Archives a(argv[1]);auto b=a.read(argv[2]);
    if(argc>3&&std::string(argv[3])=="size"){std::cout<<b.size()<<" bytes";
        if(b.size()>=28){uint32_t frames=uint32_t(b[16])|(uint32_t(b[17])<<8)|(uint32_t(b[18])<<16)|(uint32_t(b[19])<<24),dirs=uint32_t(b[12])|(uint32_t(b[13])<<8)|(uint32_t(b[14])<<16)|(uint32_t(b[15])<<24);std::cout<<" (DC6 directions "<<dirs<<", frames per direction "<<frames<<")";}
        std::cout<<std::endl;return 0;}
    size_t n=0;size_t lines=argc>3?std::stoul(argv[3]):1;
    size_t at=0;for(size_t l=0;l<lines&&at<b.size();++l){n=at;while(n<b.size()&&b[n]!='\n')++n;std::cout<<std::string(reinterpret_cast<char*>(b.data())+at,n-at)<<std::endl;at=n+1;}return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
