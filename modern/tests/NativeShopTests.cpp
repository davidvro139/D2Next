// Talks to Akara in the Rogue Encampment through the backend, opens her store, buys a cheap item and sells it back.
// The test gives the character gold through the host's D2MODERN_TEST_GOLD hook.
// usage: NativeShopTests DLL_DIR GAME_DIR SAVE
#include "NativeGame.h"
#include "NativeTown.h"
#include <algorithm>
#include <cstdlib>
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
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeShopTests DLL_DIR GAME_DIR SAVE");
    _putenv_s("D2MODERN_TEST_GOLD","5000");
    d2::Archives archives(argv[2]);auto town=d2::loadNativeTown(argv[1],argv[2],archives,0x12345678,{1});
    auto session=d2::startNativeGame(argv[1],argv[2],argv[3],town.seed,town.collision,town.worldOrigin,{1});
    auto& backend=*session.backend;const auto id=backend.snapshot().localPlayer;
    require(backend.snapshot().vitals.gold==5000,"test gold was not given");
    constexpr uint32_t akara=148;
    int32_t guid=0;
    for(const auto& e:backend.snapshot().entities)if(e.classId==akara&&e.id!=id){guid=int32_t(e.id-1000);require((e.services&1)!=0,"Akara is not marked as a merchant");}
    require(guid!=0,"Akara is not in the encampment");
    require(backend.apply(id,d2::Command::interact(guid))==d2::CommandResult::Accepted,"interact rejected");
    bool menu=false;
    for(int tick=0;tick<1500&&!menu;++tick){backend.tick();menu=sawPacket(backend.snapshot(),0x27);}
    require(menu,"Akara never opened her menu");
    std::cout<<"conversation opened (packet 0x27)\n";
    // The client sends 0x2F when the player picks Trade or Talk, then asks for the store.
    backend.apply(id,d2::Command::raw(0x2F|(9<<8),1,guid,0,0));
    backend.apply(id,d2::Command::raw(0x38|(13<<8),1,guid,0,0));
    for(int tick=0;tick<40;++tick)backend.tick();
    const d2::ItemSnapshot* offer=nullptr;auto view=backend.snapshot();size_t stock=0;
    for(const auto& i:view.items)if(i.vendor==uint32_t(guid)){++stock;require(i.price>0,"a store item has no price");if(i.price<=400&&(!offer||i.price<offer->price))offer=&i;}
    require(stock>20,"Akara's store is empty");
    require(offer!=nullptr,"nothing affordable in the store");
    const uint32_t bought=offer->id;const int32_t price=int32_t(offer->price);
    std::cout<<"store open: "<<stock<<" items, buying item "<<bought<<" for "<<price<<" gold\n";
    require(backend.apply(id,d2::Command::raw(0x32|(17<<8),guid,int32_t(bought),0,price))==d2::CommandResult::Accepted,"buy rejected");
    for(int tick=0;tick<40;++tick)backend.tick();
    view=backend.snapshot();
    require(view.vitals.gold==5000-price,"the price was not charged");
    // The purchase is a new item of the same kind in the inventory (its own number).
    uint32_t mine=0;
    for(const auto& i:view.items)if(!i.vendor&&(i.mode==0||i.mode==2)&&i.id>=bought)mine=i.id;
    require(mine!=0,"the bought item did not arrive");
    std::cout<<"bought: gold "<<view.vitals.gold<<", item "<<mine<<" in the inventory\n";
    // Sell it back: to the cursor, then to Akara.
    require(backend.apply(id,d2::Command::takeItem(int32_t(mine)))==d2::CommandResult::Accepted,"take rejected");
    for(int tick=0;tick<10;++tick)backend.tick();
    bool onCursor=false;for(const auto& i:backend.snapshot().items)if(i.id==mine&&i.mode==4)onCursor=true;
    require(onCursor,"the item did not reach the cursor");
    backend.apply(id,d2::Command::raw(0x33|(17<<8),guid,int32_t(mine),4,0));
    for(int tick=0;tick<40;++tick)backend.tick();
    view=backend.snapshot();
    require(view.vitals.gold>5000-price,"selling paid nothing");
    for(const auto& i:view.items)if(i.id==mine&&!i.vendor)throw std::runtime_error("the sold item is still ours");
    std::cout<<"sold: gold "<<view.vitals.gold<<"\n";
    // Antidote potions are consumed even at full life, so this checks the original
    // 0x20 item-use handler without changing the player's stats in the host.
    // PlrMsg.cpp Rcv0x20_UseGridItem requires a position near the player, not (0,0).
    uint32_t antidoteOffer=0;int32_t antidotePrice=0;
    constexpr uint32_t antidoteCode=uint32_t('y')|(uint32_t('p')<<8)|(uint32_t('s')<<16)|(uint32_t(' ')<<24);
    for(const auto& i:view.items)if(i.vendor==uint32_t(guid)&&i.code==antidoteCode){antidoteOffer=i.id;antidotePrice=int32_t(i.price);break;}
    require(antidoteOffer&&antidotePrice>0&&antidotePrice<=view.vitals.gold,"no affordable antidote potion");
    std::vector<uint32_t> priorItems;for(const auto& i:view.items)if(!i.vendor)priorItems.push_back(i.id);
    backend.apply(id,d2::Command::raw(0x32|(17<<8),guid,int32_t(antidoteOffer),0,antidotePrice));
    for(int tick=0;tick<20;++tick)backend.tick();
    uint32_t antidote=0;
    for(const auto& i:backend.snapshot().items)if(!i.vendor&&i.code==antidoteCode&&i.mode==0&&i.page==0&&
        std::find(priorItems.begin(),priorItems.end(),i.id)==priorItems.end())antidote=i.id;
    require(antidote!=0,"antidote did not arrive in the inventory");
    backend.apply(id,d2::Command::raw(0x30|(9<<8),1,guid,0,0));
    for(int tick=0;tick<10;++tick)backend.tick();
    for(const auto& i:backend.snapshot().items)require(!i.vendor,"the store did not close");
    require(backend.apply(id,d2::Command::useItem(int32_t(antidote)))==d2::CommandResult::Accepted,"use item rejected");
    for(int tick=0;tick<10;++tick)backend.tick();
    for(const auto& i:backend.snapshot().items)require(i.id!=antidote,"inventory antidote was not consumed by the native game");

    constexpr uint32_t charsi=154;
    int32_t charsiGuid=0;
    for(const auto& e:backend.snapshot().entities)if(e.classId==charsi&&e.id!=id){charsiGuid=int32_t(e.id-1000);require((e.services&1)!=0,"Charsi is not marked as a merchant");}
    require(charsiGuid!=0,"Charsi is not in the encampment");
    require(backend.apply(id,d2::Command::interact(charsiGuid))==d2::CommandResult::Accepted,"Charsi interact rejected");
    menu=false;
    for(int tick=0;tick<1500&&!menu;++tick){backend.tick();menu=sawPacket(backend.snapshot(),0x27);}
    require(menu,"Charsi never opened her menu");
    backend.apply(id,d2::Command::raw(0x2F|(9<<8),1,charsiGuid,0,0));
    backend.apply(id,d2::Command::raw(0x38|(13<<8),1,charsiGuid,0,0));
    for(int tick=0;tick<40;++tick)backend.tick();
    view=backend.snapshot();
    const d2::ItemSnapshot* armorOffer=nullptr;size_t armorStock=0;
    for(const auto& i:view.items)if(i.vendor==uint32_t(charsiGuid)&&i.page==0){++armorStock;require(i.price>0,"an armor store item has no price");if(i.price<=uint32_t(view.vitals.gold)&&(!armorOffer||i.price<armorOffer->price))armorOffer=&i;}
    require(armorStock>0,"Charsi's armor page is empty");
    require(armorOffer!=nullptr,"nothing affordable on Charsi's armor page");
    const uint32_t armorCode=armorOffer->code;const uint32_t armorBought=armorOffer->id;const int32_t armorPrice=int32_t(armorOffer->price);
    priorItems.clear();for(const auto& i:view.items)if(!i.vendor)priorItems.push_back(i.id);
    require(backend.apply(id,d2::Command::raw(0x32|(17<<8),charsiGuid,int32_t(armorBought),0,armorPrice))==d2::CommandResult::Accepted,"armor buy rejected");
    for(int tick=0;tick<40;++tick)backend.tick();
    uint32_t armor=0;
    // As 1.10f: worn when its place on the body is free (on a real body location, 1..10), else in the backpack (page 0).
    for(const auto& i:backend.snapshot().items)if(!i.vendor&&i.code==armorCode&&((i.mode==0&&i.page==0)||(i.mode==1&&i.bodyLoc>=1&&i.bodyLoc<=10))&&
        std::find(priorItems.begin(),priorItems.end(),i.id)==priorItems.end())armor=i.id;
    require(armor!=0,"the bought armor did not arrive in the backpack or on a body location");
    std::cout<<"PASS: talked to Akara, bought and sold an item, used an antidote, and bought armor from Charsi\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
