#pragma once
#include "Assets.h"
#include "Backend.h"
#include "Tables.h"
#include <array>
#include <string>
#include <vector>
namespace d2 {
// itemstatcost.txt: how a stat is written in an item's description.
// descFunc / descVal / descPos, descNeg, desc2: the line's form (D2Client 1.10f's switch at 0x6FAF24CD), whether and where
// the value shows (0 none, 1 before the text, 2 after it) and the string keys for a positive and a negative value.
// group*: the same for the stat's description group (dgrp: the four resistances, the four attributes), written once
// when every stat of the group is there with the same value. valShift: the value is stored shifted left by this many
// bits (life and mana in 1/256). op / opParam: ops 2-5 multiply the value by the character's level and shift it right
// by opParam ("per character level" properties).
struct ItemStatCost {
    int id=-1,descPriority=0,descFunc=0,descVal=0,group=0,groupFunc=0,groupVal=0,valShift=0,op=0,opParam=0;
    std::string name,descPos,descNeg,desc2,groupPos,groupNeg,group2;
};
class ItemStatTable {
public:
    explicit ItemStatTable(const Archives& archives);
    const ItemStatCost* find(int id) const {return id>=0&&size_t(id)<stats_.size()&&stats_[size_t(id)].id>=0?&stats_[size_t(id)]:nullptr;}
    // charstats.txt string keys per class: "+%d to Amazon Skill Levels" (StrAllSkills), a skill tab's line
    // (StrSkillTab1-3, by the tab number an item names), "(Amazon Only)" (StrClassOnly).
    std::array<std::string,7> classAllSkills,classOnly;
    std::array<std::array<std::string,3>,7> classTabs;
    size_t size() const {return stats_.size();}
private:
    std::vector<ItemStatCost> stats_;
};
// The magic property lines of an item, top first, as D2Client 1.10f writes them (0x6FAF19C0 for each stat of the list,
// highest descpriority on top; 0x6FAF21C0 for one line). characterLevel feeds the "per level" properties.
std::vector<std::string> describeItemStats(const std::vector<ItemSnapshot::Stat>& stats,const ItemStatTable& table,
    const StringTable& strings,const SkillTable* skills,int characterLevel);
}
