// Engine-backed transform conversion and actual player-art sequence bounds.
#include "NativeLink.h"
#include "NativeVisualSequence.h"
#include "Tables.h"
#include <Units/Units.h>
#include <D2Skills.h>
#include <D2States.h>
#include <D2StatList.h>
#include <D2DataTbls.h>
#include <iostream>
#include <set>
#include <stdexcept>
static void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static std::string upper(std::string value){for(char& c:value)if(c>='a'&&c<='z')c=char(c-'a'+'A');return value;}
int wmain(int argc,wchar_t** argv){try {
    require(argc==2,"Usage: AndroidSequenceConversionTests GAME_DIR");
    d2::native::useGameData(argv[1]);
    D2UnitStrc shadow{};shadow.dwUnitType=UNIT_MONSTER;shadow.dwClassId=417;
    auto converted=[&](uint8_t mode){return d2::native::monsterVisualSequenceMode(&shadow,mode,D2COMMON_11013_ConvertMode);};
    require(converted(4)==4,"ordinary monster mode changed");
    require(converted(255)==255,"missing sequence did not preserve sentinel");
    require(converted(16)==255,"invalid monster sequence mode accepted");
    D2StatListExStrc stats{};stats.dwFlags=STATLIST_EXTENDED;
    std::vector<uint32_t> flags((sgptDataTables->nStatesTxtRecordCount+31)/32);
    require(STATE_SHADOWWARRIOR<sgptDataTables->nStatesTxtRecordCount,"shadow state missing");
    flags[STATE_SHADOWWARRIOR/32]|=uint32_t(1)<<(STATE_SHADOWWARRIOR%32);
    stats.StatFlags=flags.data();shadow.pStatListEx=&stats;shadow.dwFlagEx|=UNITFLAGEX_ISSHAPESHIFTED;
    int type=UNIT_MONSTER,cls=417,mode=4;
    D2COMMON_11013_ConvertMode(&shadow,&type,&cls,&mode,__FILE__,__LINE__);
    require(type==UNIT_PLAYER&&cls==6&&mode==12,"original Shadow Warrior state did not select Assassin kick");
    require(converted(4)==12&&converted(5)==8&&converted(11)==16&&converted(14)==18,"visual sequence mode did not follow original transform conversion");
    shadow.dwClassId=418;
    require(converted(4)==12,"Shadow Master lost the Assassin kick conversion");
    shadow.dwClassId=417;
    d2::Archives archives(argv[1]);
    auto table=[&](const char* name){const auto bytes=archives.read(std::string("data\\global\\excel\\")+name+".txt");return d2::TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    const auto monsters=table("monstats"),sequences=table("monseq");
    std::set<std::string> names;
    for(size_t row=0;row<monsters.rows();++row)if(monsters.cell(row,"Id")=="shadowwarrior")
        for(int skill=1;skill<=8;++skill)if(auto name=upper(monsters.cell(row,"Sk"+std::to_string(skill)+"mode"));name.size()>3)names.insert(name);
    require(names.size()==3,"expected three distinct shadow skill sequences");
    const auto palette=d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat"));
    d2::UnitArt art(archives,palette);d2::CharacterAppearance look;look.token="ai";
    size_t steps=0;
    for(const char* weapon:{"ht1","ht2"}) {
    look.weaponClass=weapon;
    for(size_t row=0;row<sequences.rows();++row)if(names.count(upper(sequences.cell(row,"sequence")))) {
        const auto token=upper(sequences.cell(row,"mode"));uint8_t raw=0;
        while(raw<16&&upper(d2::monsterModeTokens[raw])!=token)++raw;
        require(raw<16,"unknown MonSeq mode");
        const auto visual=converted(raw);
        const auto* animation=art.player(look,visual);
        require(animation&&!animation->frames.empty(),"missing converted Assassin art for "+token+" mode "+std::to_string(visual));
        const auto text=sequences.cell(row,"frame");const size_t frame=text.empty()?0:std::stoul(text);
        require(frame<animation->framesPerDirection,"sequence frame outside converted Assassin art for "+token);
        ++steps;
    }
    }
    require(steps>0,"no converted sequence steps checked");
    std::cout<<"PASS: original transform-state conversion, Assassin kick exception, invalid/missing modes and "<<steps<<" actual player-art sequence frames (HT1/HT2)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
