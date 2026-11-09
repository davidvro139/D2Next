// Read-only original MPQs; mutations stay inside a newly created symlink fixture.
#include "Assets.h"
#include "NativeLink.h"
#include "NativeTown.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>static bool rejects(F&& operation,const char* expected) {
    try{operation();}catch(const std::exception& e){return std::string(e.what()).find(expected)!=std::string::npos;}
    return false;
}
int wmain(int argc,wchar_t** argv){try {
    require(argc==3,"Usage: AndroidArchiveConsistencyTests GAME_DIR NEW_FIXTURE_DIR");
    namespace fs=std::filesystem;
    const fs::path original=fs::absolute(argv[1]),fixture=fs::absolute(argv[2]);
    require(!fs::exists(fixture),"fixture directory must not exist");
    const auto sourcePlan=d2::vanillaArchivePlan(original);
    const auto originalIdentity=d2::archivePlanIdentity(original,sourcePlan);
    fs::create_directories(fixture);
    for(const auto& source:sourcePlan) {
        const auto relative=source.lexically_relative(original);
        const auto destination=fixture/relative;
        fs::create_directories(destination.parent_path());fs::create_symlink(source,destination);
    }
    // No player session and no save output: only native data initialization.
    d2::native::useGameData(fixture);
    d2::Archives client(fixture);client.validateCurrent();
    require(rejects([&]{d2::loadNativeTown({},original,client,0x12345678);},"Scene archive directory differs"),"scene accepted client archives from another data root");
    d2::native::useGameData(fixture/".");
    const auto core=fixture/"D2SE/CORES/1.10f/Patch_D2.mpq";
    require(!fs::exists(core),"fixture already selected a core patch; choose a root-only game directory");
    fs::create_directories(core.parent_path());std::ofstream(core).put('x');
    require(rejects([&]{client.validateCurrent();},"Game archives changed"),"client accepted changed archive precedence");
    require(rejects([&]{d2::native::useGameData(fixture);},"Cannot change game archive plan"),"native tables silently reused after changed precedence");
    require(fs::remove(core),"cannot remove test-only core marker");
    client.validateCurrent();d2::native::useGameData(fixture);

    const auto patch=fixture/sourcePlan.front().lexically_relative(original);
    require(fs::is_symlink(patch),"test patch must be a symlink before replacement");
    require(fs::remove(patch),"cannot unlink test patch");
    std::ofstream(patch).put('y');
    require(rejects([&]{client.validateCurrent();},"Game archives changed"),"client accepted replaced archive metadata");
    require(rejects([&]{d2::native::useGameData(fixture);},"Cannot change game archive plan"),"native accepted replaced archive metadata");
    require(fs::remove(patch),"cannot remove test-only replacement");
    fs::create_symlink(sourcePlan.front(),patch);
    client.validateCurrent();d2::native::useGameData(fixture/".");
    require(rejects([&]{d2::native::useGameData(original);},"Cannot change game data directory"),"different root was accepted");
    require(originalIdentity==d2::archivePlanIdentity(original,d2::vanillaArchivePlan(original)),"original archive metadata changed");
    std::cout<<"PASS: actual client/native guards reject changed precedence and replacement, recover original plan, retain root isolation; original MPQs unchanged\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
