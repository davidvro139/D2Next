// usage: StringProbe GAME_DIR ID... | StringProbe GAME_DIR find TEXT (case-insensitive search of every string id)
#include "Tables.h"
#include <algorithm>
#include <iostream>
int main(int argc,char** argv){d2::Archives a(argv[1]);d2::StringTable s(a);
    auto lower=[](std::string t){std::transform(t.begin(),t.end(),t.begin(),::tolower);return t;};
    if(argc>3&&std::string(argv[2])=="find"){const auto needle=lower(argv[3]);
        for(uint32_t id=0;id<30000;++id){const auto text=s.byId(id);if(!text.empty()&&lower(text).find(needle)!=std::string::npos)std::cout<<id<<": "<<text<<"\n";}
        return 0;}
    // StringProbe GAME key KEY...: the text of string keys (as the client names them, e.g. youdiedinst).
    if(argc>3&&std::string(argv[2])=="key"){for(int i=3;i<argc;++i)std::cout<<argv[i]<<(s.has(argv[i])?": ":" (missing): ")<<s.get(argv[i])<<"\n";return 0;}
    for(int i=2;i<argc;++i){const uint32_t id=uint32_t(std::stoul(argv[i]));std::cout<<id<<" ["<<s.keyById(id)<<"]: "<<s.byId(id)<<"\n";}
    return 0;}
