// Plays a character for a moment and saves it: turns a header-only new character (what Create Character writes) into a full
// save with its starting gear, stats and skills. usage: SaveInit DLL_DIR GAME_DIR SAVE OUT (SAVE is only read)
#include "NativeGame.h"
#include "NativeTown.h"
#include <filesystem>
#include <iostream>
int wmain(int argc,wchar_t** argv){try {
    if(argc!=5)throw std::runtime_error("Usage: SaveInit DLL_DIR GAME_DIR SAVE OUT");
    const std::filesystem::path original=argv[3],out=argv[4];
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    {
        auto session=d2::startNativeGame(argv[1],argv[2],original,town.seed,town.collision,town.worldOrigin,{1},out);
        for(int tick=0;tick<50;++tick)session.backend->tick();
    } // ending the session saves the character to OUT
    if(!std::filesystem::exists(out))throw std::runtime_error("no save was written");
    std::cout<<"saved "<<out.string()<<": "<<std::filesystem::file_size(out)<<" bytes\n";return 0;
} catch(const std::exception& e){std::cerr<<"SaveInit: "<<e.what()<<"\n";return 1;}}
