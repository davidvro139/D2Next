#include "ArchivePlan.h"
#include <fstream>
#include <chrono>
#include <iostream>
#include <algorithm>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

int main(int argc,char** argv) {
    if(argc>3)return 2;
    namespace fs=std::filesystem;
    const fs::path root=argc>=2?fs::path(argv[1]):fs::temp_directory_path()/("d2next-archive-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(fs::exists(root))throw std::runtime_error("Test directory must not exist");
    fs::create_directories(root);
    auto check=[](bool ok){if(!ok)throw std::runtime_error("Archive plan regression");};
    auto touch=[&](const fs::path& p){fs::create_directories(p.parent_path());std::ofstream(p).put('x');};
    auto rejected=[&]{try{d2::vanillaArchivePlan(root);return false;}catch(const std::runtime_error&){return true;}};
    check(rejected());
    touch(root/"d2data.mpq");check(rejected());
    touch(root/"Patch_D2.mpq");
    auto plan=d2::vanillaArchivePlan(root);check(plan.size()==2&&plan[0].filename()=="Patch_D2.mpq");
    touch(root/"d2exp.mpq");touch(root/"d2sfx.mpq");touch(root/"d2music.mpq");
    touch(root/"D2SE/CORES/1.10f/Patch_D2.mpq");
    plan=d2::vanillaArchivePlan(root);
    check(plan.size()==5&&plan[0]==root/"D2SE/CORES/1.10f/Patch_D2.mpq");
    check(plan[1].filename()=="d2exp.mpq"&&plan[2].filename()=="d2data.mpq");
    check(plan[3].filename()=="d2sfx.mpq"&&plan[4].filename()=="d2music.mpq");
    for(size_t i=1;i<plan.size();++i)check(d2::archivePriority(i-1)>d2::archivePriority(i));
#ifndef _WIN32
    fs::rename(root/"d2music.mpq",root/"D2MUSIC.MPQ");
    plan=d2::vanillaArchivePlan(root);
    check(plan.back().filename()=="D2MUSIC.MPQ");
#endif
    const auto original=d2::archivePlanIdentity(root,plan);
    check(original==d2::archivePlanIdentity(root/".",d2::vanillaArchivePlan(root/".")));
    touch(root/"d2video.mpq");
    auto expanded=d2::vanillaArchivePlan(root);
    const auto added=d2::archivePlanIdentity(root,expanded);check(added!=original);
    auto reordered=expanded;std::reverse(reordered.begin(),reordered.end());
    check(d2::archivePlanIdentity(root,reordered)!=added);
    const auto changed=expanded.front();
    std::ofstream(changed,std::ios::binary|std::ios::app).put('y');
    const auto resized=d2::archivePlanIdentity(root,expanded);
    check(resized!=added&&resized.front().bytes==added.front().bytes+1);
    std::ofstream(changed,std::ios::binary).write("zz",2);
    fs::last_write_time(changed,resized.front().modified+std::chrono::seconds(2));
    const auto retimed=d2::archivePlanIdentity(root,expanded);
    check(retimed!=resized&&retimed.front().bytes==resized.front().bytes);
    bool missing=false;try{d2::archivePlanIdentity(root,{root/"missing.mpq"});}catch(const std::runtime_error&){missing=true;}
    check(missing);
#ifndef _WIN32
    // Android SAF mounts use /proc/self/fd symlinks. Do not canonicalize them
    // into provider paths that may be inaccessible without the open descriptor.
    const int fd=open(changed.c_str(),O_RDONLY);check(fd>=0);
    const auto linked=root/"descriptor.mpq";
    fs::create_symlink("/proc/self/fd/"+std::to_string(fd),linked);
    const auto descriptor=d2::archivePlanIdentity(root,{linked});
    check(descriptor.front().bytes==retimed.front().bytes&&descriptor.front().modified==retimed.front().modified);
    close(fd);
#endif
    // Mods: <root>/Mods/<name>/*.mpq before vanilla, by file name; bad names and empty or missing folders refused.
    {
        const auto vanilla=d2::vanillaArchivePlan(root);
        check(d2::archivePlan(root,"")==vanilla);
        auto refused=[&](const std::string& name){try{d2::archivePlan(root,name);return false;}catch(const std::runtime_error&){return true;}};
        check(refused("nomod")&&refused("..")&&refused(".")&&refused("a/b")&&refused("a\\b")&&refused("c:x"));
        fs::create_directories(root/"Mods"/"empty");touch(root/"Mods"/"empty"/"readme.txt");check(refused("empty"));
        touch(root/"Mods"/"test"/"2-items.mpq");touch(root/"Mods"/"test"/"1-base.MPQ");touch(root/"Mods"/"test"/"notes.txt");
        const auto modded=d2::archivePlan(root,"test");
        check(modded.size()==vanilla.size()+2&&modded[0].filename()=="1-base.MPQ"&&modded[1].filename()=="2-items.mpq");
        check(std::equal(vanilla.begin(),vanilla.end(),modded.begin()+2));
        check(d2::archivePlanIdentity(root,modded)!=d2::archivePlanIdentity(root,vanilla));
        check(d2::modSaveDirectory(root/"Save","")==root/"Save"&&d2::modSaveDirectory(root/"Save","test")==root/"Save"/"Mods"/"test");
    }
    if(argc==3) {
        const fs::path game=argv[2];
        const auto real=d2::archivePlanIdentity(game,d2::vanillaArchivePlan(game));
        check(real==d2::archivePlanIdentity(game/".",d2::vanillaArchivePlan(game/".")));
        std::cout<<"Real archive metadata: "<<real.size()<<" unchanged entries\n";
    }
    // No recursive cleanup: the caller owns the test directory.
    std::cout<<"PASS: missing base/patch, core precedence, optional archives, strict priorities; identity aliases, added/reordered/resized/retimed files, missing metadata and descriptor symlinks\n";
}
