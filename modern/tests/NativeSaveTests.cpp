// Progress is kept in a profile copy of the character: play a session that buys something, end it, and start again from the profile file.
// The original save is never written. usage: NativeSaveTests DLL_DIR GAME_DIR SAVE
#include "NativeGame.h"
#include "NativeTown.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool sawPacket(const d2::WorldSnapshot& view,uint8_t header) {
    for(size_t at=0;at+2<=view.server.size();) {
        const size_t length=size_t(view.server[at])|(size_t(view.server[at+1])<<8);
        if(at+2+length>view.server.size())break;
        if(length&&view.server[at+2]==header)return true;
        at+=2+length;
    }
    return false;
}
static std::vector<char> readAll(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::vector<char>((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeSaveTests DLL_DIR GAME_DIR SAVE");
    const std::filesystem::path original=argv[3];
    const auto before=readAll(original);
    const auto profile=std::filesystem::temp_directory_path()/L"D2NextSaveTest";
    std::error_code ignored;std::filesystem::remove_all(profile,ignored);std::filesystem::create_directories(profile);
    const auto saved=profile/original.filename();
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    size_t itemsBefore=0;int goldAfterBuying=0;
    {
        _putenv_s("D2MODERN_TEST_GOLD","500");
        auto session=d2::startNativeGame(argv[1],argv[2],original,town.seed,town.collision,town.worldOrigin,{1},saved);
        auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
        int32_t guid=0;
        for(const auto& e:backend.snapshot().entities)if(e.classId==148&&e.id!=id)guid=int32_t(e.id-1000);
        require(guid!=0,"Akara is not in the encampment");
        backend.apply(id,d2::Command::interact(guid));
        bool menu=false;for(int tick=0;tick<1500&&!menu;++tick){backend.tick();menu=sawPacket(backend.snapshot(),0x27);}
        require(menu,"Akara never opened her menu");
        backend.apply(id,d2::Command::raw(0x2F|(9<<8),1,guid,0,0));
        backend.apply(id,d2::Command::raw(0x38|(13<<8),1,guid,0,0));
        for(int tick=0;tick<40;++tick)backend.tick();
        const d2::ItemSnapshot* offer=nullptr;auto view=backend.snapshot();
        for(const auto& i:view.items)if(i.vendor&&i.price&&i.price<=400&&(!offer||i.price<offer->price))offer=&i;
        require(offer!=nullptr,"nothing affordable in the store");
        const int32_t price=int32_t(offer->price);
        backend.apply(id,d2::Command::raw(0x32|(17<<8),guid,int32_t(offer->id),0,price));
        for(int tick=0;tick<40;++tick)backend.tick();
        view=backend.snapshot();
        require(view.vitals.gold==500-price,"the purchase was not charged");
        goldAfterBuying=view.vitals.gold;
        for(const auto& i:view.items)if(!i.vendor&&(i.mode==0||i.mode==2))++itemsBefore;
        backend.apply(id,d2::Command::raw(0x30|(9<<8),1,guid,0,0));
        for(int tick=0;tick<10;++tick)backend.tick();
        std::cout<<"session 1: gold "<<goldAfterBuying<<", "<<itemsBefore<<" items carried\n";
    } // ends the session: the game saves the character on leaving
    require(std::filesystem::exists(saved),"no profile save was written");
    require(std::filesystem::file_size(saved)>335,"the profile save is not larger than the header-only original");
    require(readAll(original)==before,"the original save was modified");
    std::cout<<"profile save written: "<<std::filesystem::file_size(saved)<<" bytes\n";
    {
        // Session 2 plays the profile save (its gold set to 100) and saves over that same file, as every session after the first does.
        _putenv_s("D2MODERN_TEST_GOLD","100");
        auto session=d2::startNativeGame(argv[1],argv[2],saved,town.seed,town.collision,town.worldOrigin,{1},saved);
        const auto view=session.backend->snapshot();
        size_t itemsAfter=0;for(const auto& i:view.items)if(!i.vendor&&(i.mode==0||i.mode==2))++itemsAfter;
        std::cout<<"session 2: gold "<<view.vitals.gold<<", "<<itemsAfter<<" items carried\n";
        require(view.vitals.gold==100,"the test gold was not given");
        require(itemsAfter==itemsBefore,"the carried items were not restored");
    }
    {
        _putenv_s("D2MODERN_TEST_GOLD","");
        auto temporary=saved;temporary+=".tmp";
        require(!std::filesystem::exists(temporary),"the second session's save was left as a temporary file");
        auto session=d2::startNativeGame(argv[1],argv[2],saved,town.seed,town.collision,town.worldOrigin,{1});
        const auto gold=session.backend->snapshot().vitals.gold;
        std::cout<<"session 3: gold "<<gold<<"\n";
        require(gold==100,"the second session's progress was not saved over the profile save");
    }
    std::filesystem::remove_all(profile,ignored);
    std::cout<<"PASS: progress survives a restart through the profile save\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
