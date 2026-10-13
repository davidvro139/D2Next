#include "Tables.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <tuple>
namespace d2 {
namespace {
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::vector<std::string> split(std::string_view line,char separator) {
    std::vector<std::string> out;size_t start=0;
    for(;;) {
        auto end=line.find(separator,start);
        auto piece=line.substr(start,end==std::string_view::npos?end:end-start);
        while(!piece.empty()&&(piece.back()=='\r'||piece.back()=='\n'))piece.remove_suffix(1);
        out.emplace_back(piece);
        if(end==std::string_view::npos)break;
        start=end+1;
    }
    return out;
}
constexpr const char* componentNames[16]={"HD","TR","LG","RA","LA","RH","LH","SH","S1","S2","S3","S4","S5","S6","S7","S8"};
}
TextTable::TextTable(std::string_view text) {
    size_t at=0;bool header=true;
    while(at<text.size()) {
        auto end=text.find('\n',at);if(end==std::string_view::npos)end=text.size();
        auto line=text.substr(at,end-at);at=end+1;
        if(line.empty()||line=="\r")continue;
        auto cells=split(line,'\t');
        if(header){for(size_t i=0;i<cells.size();++i)columns_.emplace(cells[i],i);header=false;}
        // The "Expansion" line divides the classic rows from Lord of Destruction's; the game's compiled tables (.bin)
        // leave it out (objects.bin has 573 records for objects.txt's 574 lines, levels.bin 133 for 134, and so on
        // for every table that has one), so a row number here is the game's record number.
        else if(cells.empty()||cells[0]!="Expansion")rows_.push_back(std::move(cells));
    }
}
std::string TextTable::cell(size_t row,std::string_view column) const {
    auto it=columns_.find(std::string(column));
    if(row>=rows_.size()||it==columns_.end()||it->second>=rows_[row].size())return {};
    return rows_[row][it->second];
}
MonsterTable::MonsterTable(const Archives& archives) {
    auto load=[&](const char* name){auto bytes=archives.read(name);return TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    const auto stats=load("data\\global\\excel\\monstats.txt"),stats2=load("data\\global\\excel\\monstats2.txt");
    std::map<std::string,size_t> visual;
    for(size_t r=0;r<stats2.rows();++r)visual[lower(stats2.cell(r,"Id"))]=r;
    for(size_t r=0;r<stats.rows();++r) {
        const auto index=stats.cell(r,"hcIdx");
        if(index.empty()||!std::isdigit(static_cast<unsigned char>(index[0])))continue;
        auto match=visual.find(lower(stats.cell(r,"MonStatsEx")));
        if(match==visual.end())continue;
        MonsterSprite sprite;sprite.id=stats.cell(r,"Id");sprite.nameKey=stats.cell(r,"NameStr");sprite.code=lower(stats.cell(r,"Code"));
        sprite.baseWeapon=lower(stats2.cell(match->second,"BaseW"));if(sprite.baseWeapon.empty())sprite.baseWeapon="hth";
        sprite.npc=stats.cell(r,"npc")=="1";sprite.monSound=lower(stats.cell(r,"MonSound"));
        if(const auto t=stats.cell(r,"TransLvl");!t.empty()&&std::isdigit(static_cast<unsigned char>(t[0])))sprite.transLvl=std::stoi(t);
        {
            auto number=[&](const char* column,int fallback){const auto v=stats2.cell(match->second,column);return v.empty()||!std::isdigit(static_cast<unsigned char>(v[0]))?fallback:std::stoi(v);};
            sprite.overlayHeight=std::clamp(number("OverlayHeight",2),1,4);
            sprite.light=number("Light",0);
            sprite.small=number("small",0)!=0;sprite.large=number("large",0)!=0;
            sprite.selectable=number("isSel",0)!=0&&number("noSel",0)==0;
            sprite.lightColor={uint8_t(number("light-r",255)),uint8_t(number("light-g",255)),uint8_t(number("light-b",255))};
        }
        for(int i=0;i<16;++i) {
            if(stats2.cell(match->second,componentNames[i])!="1")continue;
            auto variants=split(stats2.cell(match->second,std::string(componentNames[i])+"v"),',');
            // Every entry keeps its place (the game picks a piece by its index); "nil" is an empty place: no piece.
            for(auto v:variants) {
                v.erase(std::remove_if(v.begin(),v.end(),[](char c){return c=='"'||c==' ';}),v.end());v=lower(v);
                sprite.variants[i].push_back(v.size()==3&&v!="nil"?v:std::string());
            }
        }
        for(int m=0;m<16;++m) {
            const std::string token=monsterModeTokens[m];std::string upper=token;for(auto& c:upper)c=char(std::toupper(static_cast<unsigned char>(c)));
            sprite.hasMode[m]=stats2.cell(match->second,"m"+upper)=="1";
            const auto d=stats2.cell(match->second,"d"+upper);sprite.directions[m]=d.empty()?0:uint8_t(std::stoi(d));
        }
        if(sprite.code.size()==2)sprites_[uint32_t(std::stoul(index))]=std::move(sprite);
    }
}
const MonsterSprite* MonsterTable::find(uint32_t classId) const {
    auto it=sprites_.find(classId);return it==sprites_.end()?nullptr:&it->second;
}
CharacterAppearance monsterAppearance(const MonsterSprite& sprite,uint32_t seed) {
    CharacterAppearance appearance;appearance.token=sprite.code;appearance.weaponClass=sprite.baseWeapon;
    for(size_t i=0;i<16;++i) {
        std::vector<std::string> pieces;for(const auto& v:sprite.variants[i])if(!v.empty())pieces.push_back(v); // a guess among the real pieces
        appearance.components[i]=pieces.empty()?std::string{}:pieces[seed%pieces.size()];
    }
    return appearance;
}
}
namespace d2 {
const Animation* UnitArt::get(uint32_t classId,uint32_t mode,uint32_t variantSeed) {
    if(mode>=16)return nullptr;
    const uint64_t key=(uint64_t(classId)<<40)|(uint64_t(variantSeed)<<8)|mode;
    auto found=cache_.find(key);
    if(found!=cache_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    if(const auto* sprite=table_.find(classId);sprite&&sprite->hasMode[mode]) {
        try {animation=std::make_unique<Animation>(loadCharacter(archives_,palette_,monsterModeTokens[mode],monsterAppearance(*sprite,variantSeed),"monsters"));}
        catch(const std::exception&){animation.reset();}
    }
    return cache_.emplace(key,std::move(animation)).first->second.get();
}
const Animation* UnitArt::get(uint32_t classId,uint32_t mode,const std::array<uint8_t,16>& components,uint8_t colorTransform,uint8_t stateShift) {
    if(mode>=16)return nullptr;
    const auto key=std::make_tuple(classId,mode,components,colorTransform,stateShift);
    auto found=exact_.find(key);
    if(found!=exact_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    if(const auto* sprite=table_.find(classId);sprite&&sprite->hasMode[mode]) {
        // The pieces the game chose (each an index into the component's list of variants in monstats2.txt).
        CharacterAppearance appearance;appearance.token=sprite->code;appearance.weaponClass=sprite->baseWeapon;
        for(size_t i=0;i<16;++i){const auto& list=sprite->variants[i];appearance.components[i]=list.empty()?std::string{}:list[std::min<size_t>(components[i],list.size()-1)];}
        // The colours: a pixel's index goes through the monster's palette shift (its TransLvl map of COF\palshift.dat), then
        // for a unique through its RandTransforms.dat map, then to the palette.
        std::array<uint8_t,256> index{};for(size_t i=0;i<256;++i)index[i]=uint8_t(i);
        {
            auto found=palshifts_.find(sprite->code);
            if(found==palshifts_.end()) {
                Bytes bytes;try{bytes=archives_.read("data\\global\\monsters\\"+sprite->code+"\\cof\\palshift.dat");}catch(const std::exception&){}
                found=palshifts_.emplace(sprite->code,std::move(bytes)).first;
            }
            const size_t at=size_t(sprite->transLvl)*256;
            if(at+256<=found->second.size())for(size_t i=0;i<256;++i)index[i]=found->second[at+i];
        }
        // states.txt colorshift (Pal.PL2) replaces a unique's colour: the highest colorpri is the one the client draws.
        if(stateShift&&stateColors_){if(const auto* map=stateColors_->shift(stateShift))for(auto& i:index)i=(*map)[i];}
        else if(colorTransform) {
            if(transforms_.empty())try{transforms_=archives_.read("data\\global\\monsters\\RandTransforms.dat");}catch(const std::exception&){}
            const size_t at=size_t(colorTransform-1)*256;
            if(at+256<=transforms_.size())for(auto& i:index)i=transforms_[at+i];
        }
        Palette palette=palette_;
        for(size_t i=0;i<256;++i)palette[i]=palette_[index[i]];
        try {animation=std::make_unique<Animation>(loadCharacter(archives_,palette,monsterModeTokens[mode],appearance,"monsters"));}
        catch(const std::exception&){animation.reset();}
    }
    return exact_.emplace(key,std::move(animation)).first->second.get();
}
const Animation* UnitArt::player(const CharacterAppearance& look,uint32_t mode) {
    if(mode>=20)return nullptr;
    auto modeLook=look;if(mode==0||mode==17)modeLook.weaponClass="hth"; // death and dead exist only unarmed
    std::string key=modeLook.token+"/"+modeLook.weaponClass+"/"+std::to_string(mode);
    for(const auto& c:modeLook.components)key+="/"+c;
    auto found=players_.find(key);
    if(found!=players_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    try {animation=std::make_unique<Animation>(loadCharacter(archives_,palette_,playerModeTokens[mode],modeLook,"chars"));}
    catch(const std::exception&){animation.reset();}
    return players_.emplace(key,std::move(animation)).first->second.get();
}
}
namespace d2 {
StringTable::StringTable(const Archives& archives) {
    uint32_t idBase=0;
    // String ids as the game numbers them (D2Lang): string.tbl from 0, patchstring.tbl from 10000, expansionstring.tbl from
    // 20000. (Until 2026-10-02 the last two were swapped here, so ids from the game above 10000 found the wrong text.)
    const std::pair<const char*,uint32_t> files[]={{"data\\local\\lng\\eng\\string.tbl",0},{"data\\local\\lng\\eng\\expansionstring.tbl",20000},{"data\\local\\lng\\eng\\patchstring.tbl",10000}};
    for(const auto& [name,base]:files) {
        idBase=base;
        Bytes bytes;
        try {bytes=archives.read(name);} catch(const std::exception&) {continue;}
        if(bytes.size()<21)continue;
        auto u16=[&](size_t at){return uint32_t(bytes[at])|(uint32_t(bytes[at+1])<<8);};
        auto u32=[&](size_t at){return u16(at)|(u16(at+2)<<16);};
        const uint32_t elements=u16(2),hashSize=u32(4);
        const size_t nodes=21+size_t(elements)*2;
        auto text=[&](size_t offset)->std::string {
            if(offset>=bytes.size())return {};
            size_t end=offset;while(end<bytes.size()&&bytes[end])++end;
            return std::string(reinterpret_cast<const char*>(&bytes[offset]),end-offset);
        };
        for(uint32_t i=0;i<hashSize;++i) {
            const size_t at=nodes+size_t(i)*17;
            if(at+17>bytes.size())break;
            if(!bytes[at])continue;
            auto value=text(u32(at+11));
            byId_[idBase+u16(at+1)]=value;keyById_[idBase+u16(at+1)]=text(u32(at+7));
            strings_[text(u32(at+7))]=std::move(value);
        }
    }
}
std::string StringTable::get(const std::string& key) const {
    auto it=strings_.find(key);return it==strings_.end()?key:it->second;
}
std::string ItemTable::codeString(uint32_t code) {
    std::string s;for(int i=0;i<4;++i){char c=char((code>>(8*i))&255);if(c&&c!=' ')s+=c;}return s;
}
ItemTable::ItemTable(const Archives& archives,const StringTable& strings):strings_(strings) {
    auto load=[&](const char* name){auto bytes=archives.read(name);return TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    std::vector<int> beltBoxes;
    {
        const auto belts=load("data\\global\\excel\\belts.txt");
        for(size_t r=0;r<belts.rows();++r){const auto n=belts.cell(r,"numboxes");beltBoxes.push_back(n.empty()||!std::isdigit(static_cast<unsigned char>(n[0]))?0:std::stoi(n));}
    }
    for(const auto& [file,equipment]:{std::pair{"data\\global\\excel\\weapons.txt",true},
                                      std::pair{"data\\global\\excel\\armor.txt",true},
                                      std::pair{"data\\global\\excel\\misc.txt",false}}) {
        const auto table=load(file);
        auto number=[&](size_t row,const char* column){const auto v=table.cell(row,column);return v.empty()||(!std::isdigit(static_cast<unsigned char>(v[0]))&&v[0]!='-')?0:std::stoi(v);};
        for(size_t r=0;r<table.rows();++r) {
            const auto code=table.cell(r,"code");
            if(code.empty()||code.size()>4)continue;
            uint32_t packed=0;for(size_t i=0;i<4;++i)packed|=uint32_t(i<code.size()?uint8_t(code[i]):uint8_t(' '))<<(8*i);
            ItemInfo info;info.code=code;info.name=table.cell(r,"name");info.nameKey=table.cell(r,"namestr");info.invFile=table.cell(r,"invfile");info.uniqueInvFile=table.cell(r,"uniqueinvfile");info.setInvFile=table.cell(r,"setinvfile");info.type=table.cell(r,"type");
            const auto w=table.cell(r,"invwidth"),h=table.cell(r,"invheight");
            info.invWidth=w.empty()?1:uint8_t(std::stoi(w));info.invHeight=h.empty()?1:uint8_t(std::stoi(h));
            info.equipment=equipment||info.type=="ring"||info.type=="amul";
            info.useable=table.cell(r,"useable")=="1";
            info.minDamage=number(r,"mindam");info.maxDamage=number(r,"maxdam");
            info.twoHandMinDamage=number(r,"2handmindam");info.twoHandMaxDamage=number(r,"2handmaxdam");
            info.throwMinDamage=number(r,"minmisdam");info.throwMaxDamage=number(r,"maxmisdam");
            info.minArmor=number(r,"minac");info.maxArmor=number(r,"maxac");
            info.durability=number(r,"durability");info.reqStr=number(r,"reqstr");info.reqDex=number(r,"reqdex");info.reqLevel=number(r,"levelreq");info.maxStack=number(r,"maxstack");
            info.useSound=table.cell(r,"usesound");info.dropSound=table.cell(r,"dropsound");
            if(const auto belt=table.cell(r,"belt");info.type=="belt"&&!belt.empty()&&std::isdigit(static_cast<unsigned char>(belt[0]))&&size_t(std::stoi(belt))<beltBoxes.size())
                info.beltRows=uint8_t(beltBoxes[size_t(std::stoi(belt))]/4);
            items_[packed]=std::move(info);
        }
    }
    // Every row counts, the empty ones too (the compiled tables keep them). An item's magic affix numbers count from 1
    // through the suffixes, then the prefixes, then the automagic affixes (D2Common #10604, which builds that combined
    // table); its rare name numbers through the rare suffixes, then the rare prefixes (#10606). Unique and set numbers are
    // plain row numbers.
    auto column=[&](const char* file,const char* name,std::vector<std::string>& out){
        const auto table=load(file);
        for(size_t r=0;r<table.rows();++r)out.push_back(table.cell(r,name));
    };
    column("data\\global\\excel\\magicsuffix.txt","Name",magicAffix_);column("data\\global\\excel\\magicprefix.txt","Name",magicAffix_);
    column("data\\global\\excel\\automagic.txt","Name",magicAffix_);
    column("data\\global\\excel\\raresuffix.txt","name",rareAffix_);column("data\\global\\excel\\rareprefix.txt","name",rareAffix_);
    column("data\\global\\excel\\uniqueitems.txt","index",unique_);column("data\\global\\excel\\setitems.txt","index",set_);
    column("data\\global\\excel\\setitems.txt","set",setOfItem_);
    column("data\\global\\excel\\sets.txt","index",setKeys_);column("data\\global\\excel\\sets.txt","name",setNames_);
}
ItemTable::SetInfo ItemTable::setOf(uint32_t fileIndex) const {
    SetInfo info;
    if(fileIndex>=setOfItem_.size()||setOfItem_[fileIndex].empty())return info;
    const auto& key=setOfItem_[fileIndex];
    for(size_t r=0;r<setKeys_.size();++r)if(setKeys_[r]==key){info.set=int(r);info.name=strings_.get(setNames_[r].empty()?key:setNames_[r]);break;}
    for(size_t r=0;r<setOfItem_.size();++r)if(setOfItem_[r]==key)info.items.push_back(uint32_t(r));
    return info;
}
const ItemInfo* ItemTable::find(uint32_t code) const {
    auto it=items_.find(code);return it==items_.end()?nullptr:&it->second;
}
std::string ItemTable::displayName(uint32_t code,uint32_t quality,uint32_t prefix,uint32_t suffix,uint32_t rarePrefix,uint32_t rareSuffix,uint32_t fileIndex) const {
    const auto* info=find(code);
    const std::string base=info?strings_.get(info->nameKey.empty()?info->name:info->nameKey):codeString(code);
    auto at=[](const std::vector<std::string>& list,size_t index)->const std::string* {return index<list.size()&&!list[index].empty()?&list[index]:nullptr;};
    switch(quality) {
    case 1:return "Crude "+base;
    case 3:return "Superior "+base;
    case 4: {
        std::string name;
        if(const auto* p=prefix?at(magicAffix_,prefix-1):nullptr)name+=strings_.get(*p)+" ";
        name+=base;
        if(const auto* s=suffix?at(magicAffix_,suffix-1):nullptr)name+=" "+strings_.get(*s);
        return name;
    }
    case 5:if(const auto* s=at(set_,fileIndex))return strings_.get(*s);break;
    case 6:case 8: { // rare and crafted items carry the same two rare name numbers
        const auto* p=rarePrefix?at(rareAffix_,rarePrefix-1):nullptr;const auto* s=rareSuffix?at(rareAffix_,rareSuffix-1):nullptr;
        if(p&&s)return strings_.get(*p)+" "+strings_.get(*s);
        break;
    }
    case 7:if(const auto* u=at(unique_,fileIndex))return strings_.get(*u);break;
    default:break;
    }
    return base;
}
}
namespace d2 {
const Image* ItemArt::get(uint32_t code,uint32_t quality,uint32_t invGraphic,uint8_t trans,uint8_t color) {
    const auto* info=items_.find(code);
    if(!info)return nullptr;
    // The colour map: data\global\items\palette\<file>.dat (D2CMP's transformation files, in items.txt's numbering).
    static const char* files[9]={nullptr,"grey","grey2","gold","brown","greybrown","invgrey","invgrey2","invgreybrown"};
    const uint8_t* map=nullptr;
    if(trans>0&&trans<9&&color<21) {
        auto loaded=maps_.find(trans);
        if(loaded==maps_.end()) {
            Bytes bytes;
            try{bytes=archives_.read(std::string("data\\global\\items\\palette\\")+files[trans]+".dat");}catch(const std::exception&){}
            loaded=maps_.emplace(trans,std::move(bytes)).first;
        }
        if(loaded->second.size()>=size_t(color+1)*256)map=loaded->second.data()+size_t(color)*256;
    }
    const std::string suffix=map?"#"+std::to_string(trans)+"/"+std::to_string(color):std::string();
    std::vector<std::string> candidates;
    if(quality==7&&!info->uniqueInvFile.empty())candidates.push_back(info->uniqueInvFile);
    if(quality==5&&!info->setInvFile.empty())candidates.push_back(info->setInvFile);
    if(!info->invFile.empty()) {
        candidates.push_back(info->invFile);
        candidates.push_back(info->invFile+std::to_string(invGraphic+1));
    }
    for(const auto& name:candidates) {
        auto found=cache_.find(name+suffix);
        if(found==cache_.end()) {
            std::unique_ptr<Image> image;
            try {
                const auto bytes=archives_.read("data\\global\\items\\"+name+".dc6");
                Palette shifted=palette_;
                if(map)for(size_t i=0;i<256;++i)shifted[i]=palette_[map[i]];
                image=std::make_unique<Image>(decodeDc6(bytes,shifted,0));
            } catch(const std::exception&) {image.reset();}
            found=cache_.emplace(name+suffix,std::move(image)).first;
        }
        if(found->second)return found->second.get();
    }
    return nullptr;
}
}
namespace d2 {
SkillTable::SkillTable(const Archives& archives,const StringTable& strings) {
    auto load=[&](const char* name){auto bytes=archives.read(name);return TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    const auto skills=load("data\\global\\excel\\skills.txt"),descriptions=load("data\\global\\excel\\skilldesc.txt");
    std::map<std::string,size_t> byKey;
    for(size_t r=0;r<descriptions.rows();++r)byKey[lower(descriptions.cell(r,"skilldesc"))]=r;
    std::map<std::string,uint32_t> idByName;
    for(size_t r=0;r<skills.rows();++r) {
        const auto id=skills.cell(r,"Id"),name=lower(skills.cell(r,"skill"));
        if(!id.empty()&&std::isdigit(static_cast<unsigned char>(id[0]))&&!name.empty())idByName[name]=uint32_t(std::stoul(id));
    }
    static const char* classes[]={"ama","sor","nec","pal","bar","dru","ass"};
    for(size_t r=0;r<skills.rows();++r) {
        const auto id=skills.cell(r,"Id");
        if(id.empty()||!std::isdigit(static_cast<unsigned char>(id[0])))continue;
        SkillInfo info;info.id=uint32_t(std::stoul(id));info.name=skills.cell(r,"skill");
        auto skillNumber=[&](const char* column,int fallback){const auto v=skills.cell(r,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?fallback:std::stoi(v);};
        auto skillByName=[&](const char* column){auto found=idByName.find(lower(skills.cell(r,column)));return found==idByName.end()?0u:found->second;};
        info.requiredLevel=skillNumber("reqlevel",0);info.maxLevel=skillNumber("maxlvl",0);
        // How the skill is aimed and what it costs (the cost as D2Game charges it, see SkillInfo::manaCost).
        info.range=lower(skills.cell(r,"range"));
        info.aura=skillNumber("aura",0)!=0;
        info.mana=skillNumber("mana",0);info.levelMana=skillNumber("lvlmana",0);info.manaShift=skillNumber("manashift",0);info.minMana=skillNumber("minmana",0);
        info.prerequisites={skillByName("reqskill1"),skillByName("reqskill2"),skillByName("reqskill3")};
        info.castOverlay=lower(skills.cell(r,"castoverlay"));
        info.startSound=skills.cell(r,"stsound");
        info.doSounds={skills.cell(r,"dosound"),skills.cell(r,"dosound a"),skills.cell(r,"dosound b")};
        info.targetSound=skills.cell(r,"tgtsound");
        info.progressSound=skills.cell(r,"prgsound");
        info.clientMissiles={lower(skills.cell(r,"cltmissilea")),lower(skills.cell(r,"cltmissileb")),lower(skills.cell(r,"cltmissilec")),lower(skills.cell(r,"cltmissiled")),lower(skills.cell(r,"cltmissile"))};
        info.serverMissiles={lower(skills.cell(r,"srvmissilea")),lower(skills.cell(r,"srvmissileb")),lower(skills.cell(r,"srvmissilec")),lower(skills.cell(r,"srvmissiled")),lower(skills.cell(r,"srvmissile"))};
        info.clientOverlays={lower(skills.cell(r,"cltoverlaya")),lower(skills.cell(r,"cltoverlayb"))};
        info.targetOverlay=lower(skills.cell(r,"tgtoverlay"));
        info.progressOverlay=lower(skills.cell(r,"prgoverlay"));
        info.serverOverlay=lower(skills.cell(r,"srvoverlay"));
        info.calc1=lower(skills.cell(r,"calc1"));info.calc2=lower(skills.cell(r,"calc2"));
        info.clientStart=skillNumber("cltstfunc",0);info.clientDo=skillNumber("cltdofunc",0);info.auraRangeCalc=lower(skills.cell(r,"aurarangecalc"));
        for(int i=0;i<3;++i)info.clientProgress[size_t(i)]=skillNumber(("cltprgfunc"+std::to_string(i+1)).c_str(),0);
        info.auraState=lower(skills.cell(r,"aurastate"));
        info.leftSkill=skills.cell(r,"leftskill")=="1";info.passive=skills.cell(r,"passive")=="1";
        for(int i=0;i<8;++i)info.param[size_t(i)]=skillNumber(("Param"+std::to_string(i+1)).c_str(),0);
        const auto cls=lower(skills.cell(r,"charclass"));
        for(int c=0;c<7;++c)if(cls==classes[c])info.charClass=c;
        const auto desc=byKey.find(lower(skills.cell(r,"skilldesc")));
        if(desc!=byKey.end()) {
            auto number=[&](const char* column,int fallback){const auto v=descriptions.cell(desc->second,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?fallback:std::stoi(v);};
            info.page=number("SkillPage",0);info.row=number("SkillRow",0);info.column=number("SkillColumn",0);info.iconCel=number("IconCel",-1);
            info.listRow=number("ListRow",-1);info.listPool=number("ListPool",0);
            {   // The player modes by their tokens (DT NU WL RN GH TN TW A1 A2 BL SC TH KK S1-S4 DD SQ KB).
                static const char* modes[]={"DT","NU","WL","RN","GH","TN","TW","A1","A2","BL","SC","TH","KK","S1","S2","S3","S4","DD","SQ","KB"};
                const auto token=skills.cell(r,"seqtrans");
                for(int m=0;m<20;++m)if(token==modes[m]&&m!=18)info.seqMode=m;
            }
            {const auto v=skills.cell(r,"srvdofunc");info.serverDoFunc=!v.empty()&&std::isdigit(static_cast<unsigned char>(v[0]))?std::stoi(v):0;}
            info.displayName=strings.get(descriptions.cell(desc->second,"str name"));
        }
        if(info.displayName.empty())info.displayName=info.name;
        skills_[info.id]=std::move(info);
    }
}
const SkillInfo* SkillTable::find(uint32_t id) const {
    auto it=skills_.find(id);return it==skills_.end()?nullptr:&it->second;
}
const SkillInfo* SkillTable::findByName(const std::string& name) const {
    const auto wanted=lower(name);
    for(const auto& [id,info]:skills_)if(lower(info.name)==wanted)return &info;
    return nullptr;
}
const SkillInfo* SkillTable::withAuraState(const std::string& state) const {
    if(state.empty())return nullptr;
    for(const auto& [id,info]:skills_)if(info.auraState==state)return &info;
    return nullptr;
}
int SkillInfo::levelCalc(const std::string& calc,int level) const {
    // A small part of D2Common's calc language (SKILLS_EvaluateSkillFormula): numbers, parN, lvl, lnXY (ParamX + ParamY
    // per level above the first), dmXY (diminishing from ParamX toward ParamY), + - * / and brackets. Anything else is 0.
    level=std::max(level,1);
    auto digit=[&](char c){return c>='1'&&c<='8'?param[size_t(c-'1')]:0;};
    size_t at=0;bool known=true;
    auto skip=[&]{while(at<calc.size()&&calc[at]==' ')++at;};
    std::function<int()> sum,term;
    term=[&]() -> int {
        skip();
        if(at<calc.size()&&calc[at]=='('){++at;const int v=sum();skip();if(at<calc.size()&&calc[at]==')')++at;else known=false;return v;}
        if(at<calc.size()&&calc[at]=='-'){++at;return -term();}
        if(at<calc.size()&&std::isdigit(static_cast<unsigned char>(calc[at]))){int v=0;while(at<calc.size()&&std::isdigit(static_cast<unsigned char>(calc[at])))v=v*10+(calc[at++]-'0');return v;}
        size_t end=at;while(end<calc.size()&&std::isalnum(static_cast<unsigned char>(calc[end])))++end;
        const std::string word=calc.substr(at,end-at);at=end;
        if(word=="lvl")return level;
        if(word.size()==4&&word.compare(0,3,"par")==0)return digit(word[3]);
        if(word.size()==4&&word.compare(0,2,"ln")==0)return digit(word[2])+digit(word[3])*(level-1);
        if(word.size()==4&&word.compare(0,2,"dm")==0){const int a=digit(word[2]),b=digit(word[3]);return (110*level)*(b-a)/(100*(level+6))+a;}
        known=false;return 0;
    };
    auto product=[&]() -> int {
        int v=term();
        for(skip();at<calc.size()&&(calc[at]=='*'||calc[at]=='/');skip()) {
            const char op=calc[at++];const int r=term();
            v=op=='*'?v*r:r?v/r:0;
        }
        return v;
    };
    sum=[&]() -> int {
        int v=product();
        for(skip();at<calc.size()&&(calc[at]=='+'||calc[at]=='-');skip()){const char op=calc[at++];const int r=product();v=op=='+'?v+r:v-r;}
        return v;
    };
    const int value=calc.empty()?0:sum();
    skip();
    return known&&at==calc.size()?value:0;
}
std::vector<const SkillInfo*> SkillTable::tree(int charClass) const {
    std::vector<const SkillInfo*> out;
    for(const auto& [id,info]:skills_)if(info.charClass==charClass&&info.page>=1&&info.page<=3&&info.row>=1&&info.column>=1)out.push_back(&info);
    std::sort(out.begin(),out.end(),[](const SkillInfo* a,const SkillInfo* b){return std::tie(a->page,a->row,a->column)<std::tie(b->page,b->row,b->column);});
    return out;
}
std::string SkillTable::iconFile(int charClass) {
    static const char* names[]={"Am","So","Ne","Pa","Ba","Dr","As"};
    // Skills of no class (attack, kick, throw, unsummon) have their icons in the general file.
    return std::string("data\\global\\ui\\spells\\")+(charClass>=0&&charClass<7?names[charClass]:"")+"Skillicon.dc6";
}
const Image* SkillArt::get(int charClass,int iconCel,bool pressed) {
    if(charClass<-1||charClass>=7||iconCel<0)return nullptr;
    const uint64_t key=(uint64_t(charClass+1)<<32)|(uint64_t(iconCel)<<1)|(pressed?1:0);
    auto found=cache_.find(key);
    if(found!=cache_.end())return found->second.get();
    std::unique_ptr<Image> image;
    try {
        auto file=files_.find(charClass);
        if(file==files_.end())file=files_.emplace(charClass,archives_.read(SkillTable::iconFile(charClass))).first;
        image=std::make_unique<Image>(decodeDc6(file->second,palette_,uint32_t(iconCel+(pressed?1:0))));
    } catch(const std::exception&) {image.reset();}
    return cache_.emplace(key,std::move(image)).first->second.get();
}
}
namespace d2 {
ObjectTable::ObjectTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\objects.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    auto number=[&](size_t row,const std::string& column,int fallback){const auto v=table.cell(row,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?fallback:std::stoi(v);};
    for(size_t r=0;r<table.rows();++r) {
        ObjectInfo info;info.name=table.cell(r,"Name");info.token=lower(table.cell(r,"Token"));
        for(int m=0;m<8;++m) {
            const auto suffix=std::to_string(m);
            info.selectable[m]=number(r,"Selectable"+suffix,0)!=0;
            info.frames[m]=unsigned(std::max(0,number(r,"FrameCnt"+suffix,0)));
            info.start[m]=unsigned(std::max(0,number(r,"Start"+suffix,0)));
            info.delta[m]=unsigned(std::max(0,number(r,"FrameDelta"+suffix,256)));
            info.cycle[m]=number(r,"CycleAnim"+suffix,0)!=0;
        }
        info.sizeX=number(r,"SizeX",1);info.sizeY=number(r,"SizeY",1);
        info.xOffset=number(r,"Xoffset",0);info.yOffset=number(r,"Yoffset",0);
        info.draw=number(r,"Draw",1)!=0;
        info.door=number(r,"IsDoor",0)!=0;
        for(int m=0;m<8;++m)info.lit[m]=uint8_t(std::clamp(number(r,"Lit"+std::to_string(m),0),0,255));
        info.lightColor={uint8_t(number(r,"Red",255)),uint8_t(number(r,"Green",255)),uint8_t(number(r,"Blue",255))};
        info.automap=number(r,"AutoMap",0);
        objects_.push_back(std::move(info));
    }
}
const ObjectInfo* ObjectTable::find(uint32_t classId) const {
    return classId<objects_.size()?&objects_[classId]:nullptr;
}
const Animation* ObjectArt::get(uint32_t classId,uint32_t mode) {
    static const char* modes[8]={"nu","op","on","s1","s2","s3","s4","s5"};
    const auto* info=table_.find(classId);
    if(!info||mode>=8||info->token.size()!=2||!info->draw)return nullptr;
    const uint64_t key=(uint64_t(classId)<<8)|mode;
    auto found=cache_.find(key);
    if(found!=cache_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    try {
        CharacterAppearance appearance;appearance.token=info->token;appearance.weaponClass="hth";
        for(auto& component:appearance.components)component="lit";
        animation=std::make_unique<Animation>(loadCharacter(archives_,palette_,modes[mode],appearance,"objects"));
    } catch(const std::exception&) {animation.reset();}
    return cache_.emplace(key,std::move(animation)).first->second.get();
}
}
namespace d2 {
MissileTable::MissileTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\missiles.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    for(size_t r=0;r<table.rows();++r) {
        const auto id=table.cell(r,"Id");
        if(id.empty()||!std::isdigit(static_cast<unsigned char>(id[0])))continue;
        const auto index=size_t(std::stoul(id));
        if(missiles_.size()<=index)missiles_.resize(index+1);
        missiles_[index].celFile=table.cell(r,"CelFile");
        missiles_[index].loop=table.cell(r,"LoopAnim")!="0";
        auto number=[&](const char* column,int fallback){const auto v=table.cell(r,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?fallback:std::stoi(v);};
        missiles_[index].light=number("Light",0);
        missiles_[index].trans=number("Trans",0);
        missiles_[index].travelSound=table.cell(r,"TravelSound");missiles_[index].hitSound=table.cell(r,"HitSound");
        missiles_[index].progressSound=table.cell(r,"ProgSound");missiles_[index].progressOverlay=lower(table.cell(r,"ProgOverlay"));
        missiles_[index].lightColor={uint8_t(number("Red",255)),uint8_t(number("Green",255)),uint8_t(number("Blue",255))};
        auto& m=missiles_[index];
        m.range=number("Range",0);m.velocity=number("Vel",0);m.animRate=number("animrate",1024);m.animLen=number("AnimLen",0);
        m.xOffset=number("xoffset",0);m.yOffset=number("yoffset",0);m.zOffset=number("zoffset",0);
        m.randStart=table.cell(r,"RandStart")=="1";m.subLoop=table.cell(r,"SubLoop")=="1";m.subStart=number("SubStart",0);m.subStop=number("SubStop",0);
        m.alwaysExplode=table.cell(r,"AlwaysExplode")=="1";
        m.clientDo=number("pCltDoFunc",0);m.clientParam={number("CltParam1",0),number("CltParam2",0),number("CltParam3",0)};
        m.clientHitFunc=number("pCltHitFunc",0);m.clientHitParam={number("cHitPar1",0),number("cHitPar2",0),number("cHitPar3",0)};
        m.serverDo=number("pSrvDoFunc",0);m.initSteps=number("InitSteps",0);
    }
    // The client's own hit effects, named in the table: resolved to rows once every name is known.
    auto& byName=byName_;
    for(size_t r=0;r<table.rows();++r){const auto id=table.cell(r,"Id");if(!id.empty()&&std::isdigit(static_cast<unsigned char>(id[0])))byName[lower(table.cell(r,"Missile"))]=std::stoi(id);}
    for(size_t r=0;r<table.rows();++r) {
        const auto id=table.cell(r,"Id");
        if(id.empty()||!std::isdigit(static_cast<unsigned char>(id[0])))continue;
        if(auto f=byName.find(lower(table.cell(r,"ExplosionMissile")));f!=byName.end())missiles_[size_t(std::stoul(id))].explosion=f->second;
        if(auto f=byName.find(lower(table.cell(r,"SubMissile1")));f!=byName.end())missiles_[size_t(std::stoul(id))].serverSub=f->second;
        if(auto f=byName.find(lower(table.cell(r,"SubMissile2")));f!=byName.end())missiles_[size_t(std::stoul(id))].serverSub2=f->second;
        for(int i=0;i<3;++i)if(auto f=byName.find(lower(table.cell(r,"CltSubMissile"+std::to_string(i+1))));f!=byName.end())missiles_[size_t(std::stoul(id))].clientSub[size_t(i)]=f->second;
        for(int i=0;i<4;++i)if(auto f=byName.find(lower(table.cell(r,"CltHitSubMissile"+std::to_string(i+1))));f!=byName.end())missiles_[size_t(std::stoul(id))].clientHit[size_t(i)]=f->second;
        for(int i=0;i<4;++i)if(auto f=byName.find(lower(table.cell(r,"HitSubMissile"+std::to_string(i+1))));f!=byName.end())missiles_[size_t(std::stoul(id))].serverHit[size_t(i)]=f->second;
    }
}
const MissileInfo* MissileTable::find(uint32_t classId) const {
    return classId<missiles_.size()&&!missiles_[classId].celFile.empty()?&missiles_[classId]:nullptr;
}
const Animation* MissileArt::get(uint32_t classId) {
    const auto* info=table_.find(classId);
    if(!info)return nullptr;
    auto found=cache_.find(classId);
    if(found!=cache_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    try {animation=std::make_unique<Animation>(inScreenOrder(decodeDcc(archives_.read("data\\global\\missiles\\"+info->celFile+".dcc"),palette_)));}
    catch(const std::exception&) {animation.reset();}
    return cache_.emplace(classId,std::move(animation)).first->second.get();
}
}
namespace d2 {
LevelTable::LevelTable(const Archives& archives) {
    // Level types by row, named as AutoMap.txt names them: "Act 1 - Wilderness" -> "1 Wilderness".
    std::vector<std::string> typeNames;
    {
        auto typeBytes=archives.read("data\\global\\excel\\LvlTypes.txt");
        const TextTable types(std::string_view(reinterpret_cast<const char*>(typeBytes.data()),typeBytes.size()));
        for(size_t r=0;r<types.rows();++r) {
            auto name=types.cell(r,"Name");
            if(name.rfind("Act ",0)==0&&name.size()>8&&name.compare(5,3," - ")==0)name=name.substr(4,1)+" "+name.substr(8);
            typeNames.push_back(std::move(name));
        }
    }
    std::vector<LevelSounds> environments;
    {
        auto envBytes=archives.read("data\\global\\excel\\soundenviron.txt");
        const TextTable env(std::string_view(reinterpret_cast<const char*>(envBytes.data()),envBytes.size()));
        auto number=[&](size_t r,const char* column){const auto v=env.cell(r,column);return v.empty()||!std::isdigit(static_cast<unsigned char>(v[0]))?0:std::stoi(v);};
        for(size_t r=0;r<env.rows();++r)
            environments.push_back({number(r,"Song"),{number(r,"Day Ambience"),number(r,"Night Ambience")},{number(r,"Day Event"),number(r,"Night Event")},number(r,"Event Delay")});
    }
    auto bytes=archives.read("data\\global\\excel\\levels.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    for(size_t r=0;r<table.rows();++r) {
        const auto idText=table.cell(r,"Id");
        if(idText.empty()||!std::isdigit(static_cast<unsigned char>(idText[0])))continue;
        LevelInfo info;info.id=int32_t(std::stoul(idText));info.name=table.cell(r,"LevelName");
        const auto wp=table.cell(r,"Waypoint");
        info.waypoint=wp.empty()?255:std::stoi(wp);
        info.inside=table.cell(r,"IsInside")=="1";
        info.rain=table.cell(r,"Rain")=="1";info.mud=table.cell(r,"Mud")=="1";
        if(const auto env=table.cell(r,"SoundEnv");!env.empty()&&std::isdigit(static_cast<unsigned char>(env[0]))&&size_t(std::stoi(env))<environments.size())info.sounds=environments[size_t(std::stoi(env))];
        info.warpText=table.cell(r,"LevelWarp");
        if(const auto layer=table.cell(r,"Layer");!layer.empty()&&std::isdigit(static_cast<unsigned char>(layer[0])))info.layer=std::stoi(layer);
        if(const auto type=table.cell(r,"LevelType");!type.empty()&&std::isdigit(static_cast<unsigned char>(type[0]))&&size_t(std::stoi(type))<typeNames.size())
            info.automapName=typeNames[size_t(std::stoi(type))];
        for(int i=0;i<8;++i) {
            auto number=[&](const std::string& column){const auto v=table.cell(r,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?-1:std::stoi(v);};
            info.warp[i]=number("Warp"+std::to_string(i));info.vis[i]=number("Vis"+std::to_string(i));
        }
        if(info.waypoint>=0&&info.waypoint<255)waypointToId_[info.waypoint]=info.id;
        byId_[info.id]=std::move(info);
    }
}
const LevelInfo* LevelTable::byId(int32_t id) const {
    auto it=byId_.find(id);return it==byId_.end()?nullptr:&it->second;
}
PetTypeTable::PetTypeTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\pettype.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    auto number=[&](size_t r,const std::string& column){const auto v=table.cell(r,column);return v.empty()||!std::isdigit(static_cast<unsigned char>(v[0]))?0:std::stoi(v);};
    for(size_t r=0;r<table.rows();++r) {
        PetTypeInfo info;info.name=table.cell(r,"name");info.drawHp=number(r,"drawhp")!=0;info.iconType=number(r,"icontype");info.baseIcon=table.cell(r,"baseicon");
        for(int i=1;i<=4;++i)if(const auto file=table.cell(r,"micon"+std::to_string(i));!file.empty())info.classIcons.push_back({uint32_t(number(r,"mclass"+std::to_string(i))),file});
        rows_.push_back(std::move(info));
    }
}
std::string PetTypeTable::icon(uint32_t type,uint32_t classId) const {
    const auto* info=find(type);if(!info)return {};
    for(const auto& [cls,file]:info->classIcons)if(cls==classId)return file; // golems, totems, and the hirelings of each act
    return info->baseIcon;
}
const LevelInfo* LevelTable::byWaypoint(int waypoint) const {
    auto it=waypointToId_.find(waypoint);return it==waypointToId_.end()?nullptr:byId(it->second);
}
}

namespace d2 {
SoundBank::SoundBank(const Archives& archives,const MonsterTable& monsters):monsters_(monsters) {
    auto load=[&](const char* name){auto bytes=archives.read(name);return TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    const auto sounds=load("data\\global\\excel\\sounds.txt"),monsterTable=load("data\\global\\excel\\monsounds.txt");
    for(size_t r=0;r<sounds.rows();++r) {
        const auto name=sounds.cell(r,"Sound");if(!name.empty())sounds_[lower(name)]=sounds.cell(r,"FileName");
        if(const auto index=sounds.cell(r,"Index");!index.empty()&&std::isdigit(static_cast<unsigned char>(index[0]))) {
            const int i=std::stoi(index);byIndex_[i]=sounds.cell(r,"FileName");
            auto number=[&](const char* column,int fallback){const auto v=sounds.cell(r,column);return v.empty()||!std::isdigit(static_cast<unsigned char>(v[0]))?fallback:std::stoi(v);};
            volumes_[i]={number("Volume",255),number("Group Size",0)};
        }
    }
    for(size_t r=0;r<monsterTable.rows();++r) {
        MonsterSounds s;
        s.attack={lower(monsterTable.cell(r,"Attack1")),lower(monsterTable.cell(r,"Attack2"))};
        s.weapon={lower(monsterTable.cell(r,"Weapon1")),lower(monsterTable.cell(r,"Weapon2"))};
        s.hit=lower(monsterTable.cell(r,"HitSound"));s.death=lower(monsterTable.cell(r,"DeathSound"));
        monsterSounds_[lower(monsterTable.cell(r,"Id"))]=std::move(s);
    }
}
std::string SoundBank::file(const std::string& soundName) const {
    auto found=sounds_.find(lower(soundName));return found==sounds_.end()||found->second=="none.wav"?std::string():found->second;
}
std::string SoundBank::monsterHit(uint32_t classId) const {
    const auto* m=monsters_.find(classId);if(!m)return {};auto f=monsterSounds_.find(m->monSound);return f==monsterSounds_.end()?std::string():file(f->second.hit);
}
std::string SoundBank::monsterDeath(uint32_t classId) const {
    const auto* m=monsters_.find(classId);if(!m)return {};auto f=monsterSounds_.find(m->monSound);return f==monsterSounds_.end()?std::string():file(f->second.death);
}
std::string SoundBank::monsterAttack(uint32_t classId,int mode) const {
    const auto* m=monsters_.find(classId);if(!m)return {};auto f=monsterSounds_.find(m->monSound);return f==monsterSounds_.end()?std::string():file(f->second.attack[size_t(mode==5)]);
}
std::string SoundBank::monsterWeapon(uint32_t classId,int mode) const {
    const auto* m=monsters_.find(classId);if(!m)return {};auto f=monsterSounds_.find(m->monSound);return f==monsterSounds_.end()?std::string():file(f->second.weapon[size_t(mode==5)]);
}
}

namespace d2 {
WarpTable::WarpTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\lvlwarp.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    for(size_t r=0;r<table.rows();++r) {
        const auto id=table.cell(r,"Id");if(id.empty()||!std::isdigit(static_cast<unsigned char>(id[0])))continue;
        const auto index=size_t(std::stoul(id));if(warps_.size()<=index)warps_.resize(index+1);
        warps_[index]={std::stoi(table.cell(r,"SelectX")),std::stoi(table.cell(r,"SelectY")),std::stoi(table.cell(r,"SelectDX")),std::stoi(table.cell(r,"SelectDY"))};
    }
}
OverlayTable::OverlayTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\overlay.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    auto number=[&](size_t r,const char* column,int fallback){const auto v=table.cell(r,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?fallback:std::stoi(v);};
    for(size_t r=0;r<table.rows();++r) {
        auto name=table.cell(r,"overlay");
        {std::string row=name;std::transform(row.begin(),row.end(),row.begin(),[](unsigned char c){return char(std::tolower(c));});rowNames_.push_back(row);}
        if(name.empty()||name=="null")continue;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        OverlayInfo info;info.file=table.cell(r,"Filename");
        info.frames=unsigned(std::max(1,number(r,"Frames",1)));info.x=number(r,"Xoffset",0);info.y=number(r,"Yoffset",0);
        info.rate=std::max(1,number(r,"AnimRate",16));info.trans=number(r,"Trans",3);info.radius=number(r,"Radius",0);
        info.loopWait=std::max(0,number(r,"LoopWaitTime",0));info.oneOfN=std::max(1,number(r,"1ofN",1));
        for(int h=0;h<4;++h)info.height[h]=number(r,("Height"+std::to_string(h+1)).c_str(),0);
        info.preDraw=number(r,"PreDraw",0)!=0;
        info.color={uint8_t(number(r,"Red",255)),uint8_t(number(r,"Green",255)),uint8_t(number(r,"Blue",255))};
        overlays_[name]=std::move(info);
    }
}
const OverlayInfo* OverlayTable::find(const std::string& name) const {auto found=overlays_.find(name);return found==overlays_.end()?nullptr:&found->second;}
const Animation* OverlayArt::get(const std::string& name) {
    if(auto found=cache_.find(name);found!=cache_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    if(const auto* info=table_.find(name))
        try {animation=std::make_unique<Animation>(inScreenOrder(decodeDcc(archives_.read("data\\global\\overlays\\"+info->file+".dcc"),palette_)));}
        catch(const std::exception&) {animation.reset();}
    return cache_.emplace(name,std::move(animation)).first->second.get();
}
const Animation* OverlayArt::dc6(const std::string& path) {
    if(auto found=cache_.find(path);found!=cache_.end())return found->second.get();
    std::unique_ptr<Animation> animation;
    try {
        const auto bytes=archives_.read(path);
        animation=std::make_unique<Animation>();animation->directions=1;animation->framesPerDirection=dc6FrameCount(bytes);
        for(uint32_t f=0;f<animation->framesPerDirection;++f) {
            AnimationFrame frame;frame.image=decodeDc6(bytes,palette_,f);
            const auto at=dc6FrameOffset(bytes,f); // x left, y bottom row
            frame.x=at[0];frame.y=at[1]-int(frame.image.height);
            animation->frames.push_back(std::move(frame));
        }
    } catch(const std::exception&) {animation.reset();}
    return cache_.emplace(path,std::move(animation)).first->second.get();
}
StateTable::StateTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\states.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    auto number=[&](size_t r,const char* column){const auto v=table.cell(r,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?0:std::stoi(v);};
    for(size_t r=0;r<table.rows();++r) {
        const auto id=table.cell(r,"id");
        if(id.empty()||!std::isdigit(static_cast<unsigned char>(id[0])))continue;
        const auto index=size_t(std::stoul(id));
        if(states_.size()<=index)states_.resize(index+1);
        auto& info=states_[index];
        for(int i=0;i<4;++i)info.overlays[i]=lower(table.cell(r,"overlay"+std::to_string(i+1)));
        info.name=lower(table.cell(r,"state"));info.skill=lower(table.cell(r,"skill"));info.clientActive=number(r,"cltactivefunc");
        info.castOverlay=lower(table.cell(r,"castoverlay"));info.removeOverlay=lower(table.cell(r,"removerlay"));
        info.colorPri=number(r,"colorpri");info.colorShift=number(r,"colorshift");
        info.light={uint8_t(number(r,"light-r")),uint8_t(number(r,"light-g")),uint8_t(number(r,"light-b"))};
        {   static const char* kinds[]={"att","dam","arm","rf","rl","rc","rp"};
            for(int k=0;k<7;++k){if(number(r,(std::string(kinds[k])+"blue").c_str()))info.sheetBlue|=uint8_t(1u<<k);if(number(r,(std::string(kinds[k])+"red").c_str()))info.sheetRed|=uint8_t(1u<<k);}
        }
    }
}
StateVisual StateTable::visual(const std::array<uint32_t,6>& states) const {
    StateVisual best;int pri=-1;
    for(uint32_t s=0;s<states_.size()&&s<192;++s) {
        if(!(states[s>>5]&(1u<<(s&31))))continue;
        const auto& info=states_[s];
        if(info.colorPri<=pri)continue;
        if(!info.colorShift&&!info.light[0]&&!info.light[1]&&!info.light[2])continue;
        pri=info.colorPri;best.shift=uint8_t(std::clamp(info.colorShift,0,255));
        best.light=info.light[0]||info.light[1]||info.light[2];best.lightColor=info.light;
    }
    return best;
}
StateColors::StateColors(const Archives& archives,int act) {
    // Pal.PL2 layout matches the 443175-byte file (Worldstone's PL2): the 128 state colour maps begin after the light,
    // inventory, selection, blend and multiply tables, at byte 341248, one 256-byte index remap each. states.txt
    // colorshift picks one (Paul Siramy: 0..127; 108 blue, 104 green, 73 grey).
    Bytes bytes;try{bytes=archives.read("data\\global\\palette\\act"+std::to_string(act)+"\\pal.pl2");}catch(const std::exception&){return;}
    constexpr size_t at=341248;
    if(bytes.size()<at+128*256)return;
    for(int i=0;i<128;++i)std::copy_n(bytes.begin()+at+size_t(i)*256,256,shifts_[size_t(i)].begin());
    loaded_=true;
}
AutomapTable::AutomapTable(const Archives& archives) {
    auto bytes=archives.read("data\\global\\excel\\AutoMap.txt");
    const TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    auto number=[&](size_t r,const char* column){const auto v=table.cell(r,column);return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?-1:std::stoi(v);};
    for(size_t r=0;r<table.rows();++r) {
        const int style=number(r,"Style"),first=number(r,"StartSequence"),last=number(r,"EndSequence");
        if(style<0)continue;
        Row row;row.first=first<0?0:first;row.last=last<0?255:last;
        for(const char* column:{"Cel1","Cel2","Cel3","Cel4"})if(const int cel=number(r,column);cel>=0)row.frames.push_back(cel);
        if(!row.frames.empty())rows_[{table.cell(r,"LevelName"),table.cell(r,"TileName"),style}].push_back(std::move(row));
    }
}
int AutomapTable::frame(const std::string& levelType,uint32_t orientation,uint32_t mainIndex,uint32_t subIndex,uint32_t pick) const {
    // A tile's kind code by its orientation, in the game's order.
    static const char* codes[]={"fl","wl","wr","wtlr","wtll","wtr","wbl","wbr","wld","wrd","wle","wre","co","sh","tr","rf","ld","rd","fd","fi"};
    if(orientation>=std::size(codes))return -1;
    const auto found=rows_.find({levelType,codes[orientation],int(mainIndex)});
    if(found==rows_.end())return -1;
    for(const auto& row:found->second)if(int(subIndex)>=row.first&&int(subIndex)<=row.last)return row.frames[pick%row.frames.size()];
    return -1;
}
}
