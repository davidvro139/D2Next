// Prints the record count at the start of compiled tables (data\global\excel\NAME.bin). usage: BinCountProbe GAME_DIR NAME...
#include "Assets.h"
#include <iostream>
int main(int argc,char** argv){try{
    d2::Archives a(argv[1]);
    for(int i=2;i<argc;++i) {
        const auto b=a.read(std::string("data\\global\\excel\\")+argv[i]+".bin");
        const uint32_t n=b.size()>=4?uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24):0;
        std::cout<<argv[i]<<".bin: "<<n<<" records ("<<b.size()<<" bytes)\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
