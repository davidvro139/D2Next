// Prints the WAVE format of assets. usage: WavProbe GAME_DIR PATH...
#include "Assets.h"
#include <iostream>
int main(int argc,char** argv){try{d2::Archives a(argv[1]);for(int i=2;i<argc;++i){auto b=a.read(argv[i]);
    size_t p=12;while(p+8<=b.size()){uint32_t sz=b[p+4]|(b[p+5]<<8)|(b[p+6]<<16)|(uint32_t(b[p+7])<<24);std::string id(reinterpret_cast<char*>(&b[p]),4);
        if(id=="fmt "){std::cout<<argv[i]<<": tag "<<(b[p+8]|(b[p+9]<<8))<<" ch "<<(b[p+10]|(b[p+11]<<8))<<" rate "<<(b[p+12]|(b[p+13]<<8)|(b[p+14]<<16))<<" bits "<<(b[p+22]|(b[p+23]<<8))<<" fmtsize "<<sz<<"\n";}
        p+=8+sz+(sz&1);}}return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
