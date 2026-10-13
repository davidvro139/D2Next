// String tables, item names and monster names against the game install. usage: TableTests GAME_DIR
#include "Tables.h"
#include <iostream>
#include <cstring>
static void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    require(argc>1,"usage: TableTests GAME_DIR");
    d2::Archives archives(argv[1]);d2::StringTable strings(archives);
    std::cout<<"strings: "<<strings.size()<<std::endl;require(strings.size()>3000,"string tables too small");
    require(strings.get("cap")=="Cap","string cap");
    d2::ItemTable items(archives,strings);
    auto code=[](const char* c){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(i<int(strlen(c))?uint8_t(c[i]):uint8_t(' '))<<(8*i);return v;};
    require(items.displayName(code("ssd"),2,0,0,0,0,0)=="Short Sword","normal item name");
    std::cout<<"magic 1: "<<items.displayName(code("ssd"),4,1,0,0,0,0)<<" | magic 2: "<<items.displayName(code("ssd"),4,2,0,0,0,0)<<" | unique 0: "<<items.displayName(code("ssd"),7,0,0,0,0,0)<<std::endl;
    require(items.displayName(code("ssd"),4,1,0,0,0,0)=="Sturdy Short Sword","magic prefix name");
    require(items.displayName(code("ssd"),7,0,0,0,0,0)=="The Gnasher","unique name");
    const auto* sword=items.find(code("ssd"));require(sword&&sword->invWidth==1&&sword->invHeight==3,"short sword inventory size");
    require(sword->minDamage==2&&sword->maxDamage==7&&sword->durability==24,"short sword tooltip specs");
    require(!sword->useable,"short sword should not be right-click useable");
    const auto* potion=items.find(code("hpo"));require(potion&&potion->useable,"healing potion should be right-click useable");
    const auto* cap=items.find(code("cap"));require(cap&&cap->maxArmor>=5&&cap->durability==12,"cap tooltip specs");
    d2::MonsterTable monsters(archives);
    const auto* gheed=monsters.find(147);require(gheed&&strings.get(gheed->nameKey)=="Gheed","Gheed's name");
    const auto* zombie=monsters.find(5);require(zombie&&strings.get(zombie->nameKey)=="Zombie","zombie name");
    require(zombie->overlayHeight>=1&&zombie->overlayHeight<=4,"zombie overlay height");
    std::cout<<"short sword inv file: "<<sword->invFile<<" | strings: "<<strings.size()<<"\n";
    d2::SkillTable skills(archives,strings);
    const auto* might=skills.find(98);
    require(might&&might->displayName=="Might"&&might->charClass==3,"Might in the skill table");
    const auto* holyBolt=skills.find(101);require(holyBolt&&holyBolt->requiredLevel==6&&holyBolt->maxLevel==20,"Holy Bolt requirements");
    const auto* holyFire=skills.find(102);require(holyFire&&holyFire->prerequisites[0]==98,"Holy Fire prerequisite");
    const auto* frozenArmor=skills.find(40);require(frozenArmor&&frozenArmor->clientOverlays[0]=="frozenarmor_hit"&&frozenArmor->doSounds[0]=="sorceress_frozenarmor","Frozen Armor client overlay/sound");
    const auto* thunderStorm=skills.find(57);require(thunderStorm&&thunderStorm->progressOverlay=="lightning","Thunder Storm progress overlay");
    const auto* psychicHammer=skills.find(253);require(psychicHammer&&psychicHammer->targetOverlay=="psychic_hammer_hit"&&psychicHammer->targetSound=="paladin_holybolt_impact_1","Psychic Hammer target overlay/sound");
    const auto* vineAttack=skills.find(294);require(vineAttack&&vineAttack->clientMissiles[3]=="vine beast neutral","Vine Attack fourth client missile");
    const auto* shiverArmor=skills.find(50);require(shiverArmor&&shiverArmor->clientMissiles[0]=="sparkle"&&shiverArmor->serverMissiles[0].empty(),"Shiver Armor visual-only client missile");
    const auto* chainLightningSkill=skills.find(53);require(chainLightningSkill&&chainLightningSkill->clientMissiles[0]=="chainlightning"&&chainLightningSkill->serverMissiles[0]=="chainlightning","Chain Lightning server/client missile pair");
    require(skills.tree(3).size()>=28,"the paladin skill tree is incomplete");
    std::cout<<"Might: page "<<might->page<<" row "<<might->row<<" column "<<might->column<<" icon "<<might->iconCel<<"\n";
    d2::SkillArt skillArt(archives,d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")));
    const auto* icon=skillArt.get(might->charClass,might->iconCel);
    require(icon&&icon->width>=30&&icon->height>=30,"the Might icon did not decode");
    std::cout<<"skill icon "<<icon->width<<"x"<<icon->height<<"\n";
    for(int cls=0;cls<7;++cls)for(const auto* info:skills.tree(cls))if(!skillArt.get(cls,info->iconCel))std::cout<<"  no icon: class "<<cls<<" "<<info->displayName<<" cel "<<info->iconCel<<" page "<<info->page<<"\n";
    d2::LevelTable levels(archives);
    const auto* town=levels.byId(1);
    require(town&&town->name=="Rogue Encampment"&&town->waypoint==0,"Act 1 town level entry");
    const auto* coldPlains=levels.byWaypoint(1);
    require(coldPlains&&coldPlains->name=="Cold Plains"&&coldPlains->id==3,"waypoint 1 should resolve to Cold Plains");
    std::cout<<"levels: "<<town->name<<" (waypoint "<<town->waypoint<<"), waypoint 1 = "<<coldPlains->name<<"\n";
    d2::MissileTable missiles(archives);
    const auto* firebolt=missiles.find(58);
    require(firebolt&&firebolt->celFile=="Firebolt"&&firebolt->loop,"firebolt missile row");
    require(firebolt->explosion==29,"firebolt explosion missile");
    const auto* holyBoltMissile=missiles.find(55);require(holyBoltMissile&&holyBoltMissile->clientHit[0]==204,"holy bolt hit effect");
    const auto* lightningBolt=missiles.find(98);require(lightningBolt&&lightningBolt->clientSub[0]==99,"lightning bolt client sub missile");
    const auto* chainLightning=missiles.find(93);require(chainLightning&&chainLightning->progressOverlay=="lightning","chain lightning progress overlay");
    const auto* moltenBoulder=missiles.find(452);require(moltenBoulder&&moltenBoulder->progressSound=="sorceress_fireball_1","molten boulder progress sound");
    const auto* blizzardCenter=missiles.find(158);require(blizzardCenter&&blizzardCenter->clientSub[0]==159&&blizzardCenter->clientSub[1]==162,"blizzard client sub missiles");
    const auto* shockField=missiles.find(389);require(shockField&&shockField->randStart,"shock field random start");
    const auto* boneSpirit=missiles.find(193);require(boneSpirit&&boneSpirit->zOffset==-30,"bone spirit missile offset");
    d2::MissileArt missileArt(archives,d2::decodePalette(archives.read("data\\global\\palette\\act1\\pal.dat")),missiles);
    const auto* missileAnim=missileArt.get(58);
    require(missileAnim&&missileAnim->directions>0&&!missileAnim->frames.empty(),"the firebolt missile did not decode");
    std::cout<<"missile: "<<firebolt->celFile<<", "<<missileAnim->directions<<" directions, "<<missileAnim->framesPerDirection<<" frames\n";
    d2::SoundBank soundBank(archives,monsters);
    const auto zombieHit=soundBank.monsterHit(5),zombieDeath=soundBank.monsterDeath(5);
    require(!zombieHit.empty()&&!zombieDeath.empty(),"zombie hit/death sounds");
    require(!archives.read("data\\global\\sfx\\"+zombieHit).empty(),"zombie hit sound file");
    std::cout<<"zombie sounds: "<<zombieHit<<" | "<<zombieDeath<<"\n";
    std::cout<<"PASS: string tables, item names, monster names, skills, levels, missiles and sounds\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
