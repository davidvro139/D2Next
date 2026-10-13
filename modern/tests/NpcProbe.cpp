// Talks to a town NPC through the backend and dumps the packets the game sends back. usage: NpcProbe DLL_DIR GAME_DIR SAVE [class id]
#include "NativeGame.h"
#include "NativeTown.h"
#include "Tables.h"
#include <cstdio>
#include <iostream>
static void dump(uint64_t tick,const std::vector<uint8_t>& stream) {
    for(size_t at=0;at+2<=stream.size();) {
        const size_t length=size_t(stream[at])|(size_t(stream[at+1])<<8);at+=2;
        std::printf("tick %llu packet 0x%02x len %zu:",static_cast<unsigned long long>(tick),length?stream[at]:0,length);
        for(size_t i=0;i<length&&i<48;++i)std::printf(" %02x",stream[at+i]);
        std::printf("\n");at+=length;
    }
}
int wmain(int argc,wchar_t** argv){try{
    const uint32_t wantClass=argc>4?uint32_t(std::stoul(argv[4])):148;
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    std::printf("-- join packets\n");dump(0,backend.snapshot().server);
    int32_t guid=0;
    for(const auto& e:backend.snapshot().entities)if(e.classId==wantClass&&e.id!=id)guid=int32_t(e.id-1000);
    if(!guid){std::printf("class %u not found\n",wantClass);return 1;}
    std::printf("-- interact with unit %d\n",guid);
    if(backend.apply(id,d2::Command::interact(guid))!=d2::CommandResult::Accepted){std::printf("rejected\n");return 1;}
    bool traded=false;
    for(int tick=0;tick<500;++tick){backend.tick();const auto view=backend.snapshot();dump(view.tick,view.server);
        for(size_t at=0;at+2<=view.server.size();){const size_t length=size_t(view.server[at])|(size_t(view.server[at+1])<<8);if(length&&view.server[at+2]==0x27&&!traded){traded=true;
            std::printf("-- trade requested\n");backend.apply(id,d2::Command::raw(0x38|(13<<8),1,guid,0,0));}at+=2+length;}}
    for(int tick=0;tick<80;++tick)backend.tick();
    d2::StringTable strings(archives);d2::ItemTable itemTable(archives,strings);
    for(const auto& i:backend.snapshot().items)if(i.vendor)std::printf("vendor item %u %s page %u cell %d,%d price %u\n",i.id,itemTable.displayName(i.code,i.quality,i.prefix,i.suffix,i.rarePrefix,i.rareSuffix,i.fileIndex).c_str(),i.page,i.x,i.y,i.price);
    uint32_t potion=0;
    for(const auto& i:backend.snapshot().items)if(i.vendor&&i.price&&i.price<=400&&!potion){potion=i.id;}
    const auto before=backend.snapshot();
    std::printf("-- gold before %d, buying vendor item %u\n",before.vitals.gold,potion);
    backend.apply(id,d2::Command::raw(0x32|(17<<8),guid,int32_t(potion),0,0));
    for(int tick=0;tick<60;++tick){backend.tick();dump(backend.snapshot().tick,backend.snapshot().server);}
    const auto after=backend.snapshot();
    std::printf("-- gold after %d\n",after.vitals.gold);
    for(const auto& i:after.items)if(!i.vendor)std::printf("item %u code %u mode %u page %u at %d,%d body %u\n",i.id,i.code,i.mode,i.page,i.x,i.y,i.bodyLoc);
    // Sell it back: take it to the cursor, then offer it to the NPC.
    uint32_t bought=0;for(const auto& i:after.items)if(!i.vendor&&i.id>=53)bought=i.id;
    backend.apply(id,d2::Command::raw(0x2F|(9<<8),1,guid,0,0));for(int tick=0;tick<5;++tick)backend.tick();
    std::printf("-- taking item %u to the cursor\n",bought);
    backend.apply(id,d2::Command::takeItem(int32_t(bought)));for(int tick=0;tick<10;++tick)backend.tick();
    for(const auto& i:backend.snapshot().items)if(!i.vendor&&i.id>=53)std::printf("  after take: item %u mode %u page %u at %d,%d\n",i.id,i.mode,i.page,i.x,i.y);
    std::printf("-- selling\n");
    backend.apply(id,d2::Command::raw(0x33|(17<<8),guid,int32_t(bought),4,0));
    for(int tick=0;tick<60;++tick){backend.tick();dump(backend.snapshot().tick,backend.snapshot().server);}
    const auto sold=backend.snapshot();
    std::printf("-- gold after selling %d\n",sold.vitals.gold);
    for(const auto& i:sold.items)if(!i.vendor&&i.id>=53)std::printf("still have item %u mode %u\n",i.id,i.mode);
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
