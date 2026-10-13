#include "ItemStats.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
namespace d2 {
namespace {
TextTable loadTable(const Archives& archives,const char* name) {
    const auto bytes=archives.read(std::string("data\\global\\excel\\")+name);
    return TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
}
int numberOf(const std::string& v){return v.empty()||!(std::isdigit(static_cast<unsigned char>(v[0]))||v[0]=='-')?0:std::stoi(v);}
// The game's strings are printf formats: %d, %s and %% are filled in order.
std::string format(const std::string& pattern,const std::vector<std::string>& values) {
    std::string out;size_t next=0;
    for(size_t i=0;i<pattern.size();++i) {
        if(pattern[i]=='%'&&i+1<pattern.size()) {
            const char c=pattern[i+1];
            if(c=='%'){out+='%';++i;continue;}
            if(c=='d'||c=='s'||c=='i'||c=='u'){out+=next<values.size()?values[next++]:std::string();++i;continue;}
            if(c=='+'&&i+2<pattern.size()&&pattern[i+2]=='d'){out+=next<values.size()?values[next++]:std::string();i+=2;continue;}
        }
        out+=pattern[i];
    }
    return out;
}
std::string signedValue(int v){return (v<0?"":"+")+std::to_string(v);}
}
ItemStatTable::ItemStatTable(const Archives& archives) {
    const auto table=loadTable(archives,"itemstatcost.txt");
    for(size_t r=0;r<table.rows();++r) {
        const auto id=table.cell(r,"ID");
        if(id.empty()||!std::isdigit(static_cast<unsigned char>(id[0])))continue;
        ItemStatCost s;s.id=std::stoi(id);s.name=table.cell(r,"Stat");
        s.descPriority=numberOf(table.cell(r,"descpriority"));s.descFunc=numberOf(table.cell(r,"descfunc"));s.descVal=numberOf(table.cell(r,"descval"));
        s.descPos=table.cell(r,"descstrpos");s.descNeg=table.cell(r,"descstrneg");s.desc2=table.cell(r,"descstr2");
        s.group=numberOf(table.cell(r,"dgrp"));s.groupFunc=numberOf(table.cell(r,"dgrpfunc"));s.groupVal=numberOf(table.cell(r,"dgrpval"));
        s.groupPos=table.cell(r,"dgrpstrpos");s.groupNeg=table.cell(r,"dgrpstrneg");s.group2=table.cell(r,"dgrpstr2");
        s.valShift=numberOf(table.cell(r,"ValShift"));s.op=numberOf(table.cell(r,"op"));s.opParam=numberOf(table.cell(r,"op param"));
        if(stats_.size()<=size_t(s.id))stats_.resize(size_t(s.id)+1);
        stats_[size_t(s.id)]=std::move(s);
    }
    const auto classes=loadTable(archives,"charstats.txt");
    size_t c=0;
    for(size_t r=0;r<classes.rows()&&c<7;++r) {
        if(classes.cell(r,"StrAllSkills").empty())continue; // the "Expansion" divider
        classAllSkills[c]=classes.cell(r,"StrAllSkills");classOnly[c]=classes.cell(r,"StrClassOnly");
        for(int t=0;t<3;++t)classTabs[c][size_t(t)]=classes.cell(r,"StrSkillTab"+std::to_string(t+1));
        ++c;
    }
}
std::vector<std::string> describeItemStats(const std::vector<ItemSnapshot::Stat>& stats,const ItemStatTable& table,
    const StringTable& strings,const SkillTable* skills,int characterLevel) {
    struct Line {int priority,id;std::string text;};
    std::vector<Line> lines;
    // The value as the line shows it: per-level ops scaled by the character's level, then the stat's ValShift.
    auto shown=[&](const ItemStatCost& s,int value) {
        if(s.op>=2&&s.op<=5)value=int((int64_t(value)*std::max(characterLevel,1))>>s.opParam);
        return value>>s.valShift;
    };
    std::map<int,std::vector<const ItemSnapshot::Stat*>> byId;
    for(const auto& s:stats)byId[int(s.id)].push_back(&s);
    auto single=[&](int id,int& value){const auto it=byId.find(id);if(it==byId.end()||it->second.size()!=1)return false;value=it->second[0]->value;return true;};
    std::set<int> used;
    auto str=[&](const std::string& key){return key.empty()?std::string():strings.get(key);};
    auto skillName=[&](int id){const auto* s=skills?skills->find(uint32_t(id)):nullptr;return s?(s->displayName.empty()?s->name:s->displayName):std::string("skill ")+std::to_string(id);};
    auto priorityOf=[&](int id){const auto* s=table.find(id);return s?s->descPriority:0;};
    // The general forms (descfunc), for a stat or a group.
    auto write=[&](int func,int val,const std::string& posKey,const std::string& negKey,const std::string& key2,int value,int param,const ItemStatCost* s) -> std::string {
        const std::string text=str(value<0&&!negKey.empty()?negKey:posKey),text2=str(key2);
        auto place=[&](const std::string& number,const std::string& words) {
            if(val==0)return words;
            return val==2?words+" "+number:number+" "+words;
        };
        switch(func) {
        case 1: case 12: case 17: return place(signedValue(value),text);
        case 2: case 18: return place(std::to_string(value)+"%",text);
        case 3: return place(std::to_string(value),text);
        case 4: return place(signedValue(value)+"%",text);
        case 5: return place(std::to_string(value*100/128)+"%",text);
        case 6: return place(signedValue(value),text)+" "+text2;
        case 7: return place(std::to_string(value)+"%",text)+" "+text2;
        case 8: return place(signedValue(value)+"%",text)+" "+text2;
        case 9: return place(std::to_string(value),text)+" "+text2;
        case 10: return place(std::to_string(value*100/128)+"%",text)+" "+text2;
        case 11: return value>0?format(text,{"1",std::to_string(100/value)}):std::string();
        case 13: { // +n to <Class> Skill Levels: the parameter is the class
            const int c=std::clamp(param,0,6);return signedValue(value)+" "+str(table.classAllSkills[size_t(c)]);
        }
        case 14: { // +n to <tab> Skills (<Class> Only): the parameter is class * 8 + tab
            // D2Client 0x6FAF2854: a tab of 3 or more, or no such class, writes nothing.
            const int c=param>>3,t=param&7;
            if(c<0||c>6||t>2)return std::string();
            return format(str(table.classTabs[size_t(c)][size_t(t)]),{std::to_string(value)})+" "+str(table.classOnly[size_t(c)]);
        }
        case 15: // n% chance to cast level L <skill> on ...: parameter skill << 6 | level
            return format(text,{std::to_string(value),std::to_string(param&63),skillName(param>>6)});
        case 16: // Level L <skill> Aura When Equipped
            return format(text,{std::to_string(value),skillName(param)});
        case 19: return format(text,{std::to_string(value)});
        case 20: return place(std::to_string(-value)+"%",text);
        case 21: return place(std::to_string(-value),text);
        case 22: case 23: return place(std::to_string(value)+"%",text);
        case 24: { // Level L <skill> (c/m Charges): parameter skill << 6 | level, value max << 8 | charges
            return str("ModStre10b")+" "+std::to_string(param&63)+" "+skillName(param>>6)+" "+format(text,{std::to_string(value&255),std::to_string((value>>8)&255)});
        }
        case 27: { // +n to <skill> (<Class> Only)
            const auto* sk=skills?skills->find(uint32_t(param)):nullptr;
            const int c=sk?sk->charClass:-1;
            return signedValue(value)+" to "+skillName(param)+(c>=0&&c<7?" "+str(table.classOnly[size_t(c)]):std::string());
        }
        case 28: return signedValue(value)+" to "+skillName(param);
        default: return s?place(signedValue(value),text):std::string();
        }
    };
    // Lines D2Client writes for two or three stats together.
    auto pair=[&](int minId,int maxId,const char* rangeKey,const char* sameKey,std::initializer_list<int> alsoUsed) {
        int lo=0,hi=0;
        if(!single(minId,lo)||!single(maxId,hi))return;
        const auto* s=table.find(minId);if(!s)return;
        lo>>=s->valShift;hi>>=table.find(maxId)?table.find(maxId)->valShift:0;
        lines.push_back({priorityOf(minId),minId,lo==hi?format(str(sameKey),{std::to_string(lo)}):format(str(rangeKey),{std::to_string(lo),std::to_string(hi)})});
        used.insert(minId);used.insert(maxId);for(int id:alsoUsed)used.insert(id);
    };
    pair(21,22,"strModMinDamageRange","strModMinDamage",{23,24,159,160}); // damage (the two-hand and throw copies go with it)
    pair(48,49,"strModFireDamageRange","strModFireDamage",{});
    pair(50,51,"strModLightningDamageRange","strModLightningDamage",{});
    pair(52,53,"strModMagicDamageRange","strModMagicDamage",{});
    pair(54,55,"strModColdDamageRange","strModColdDamage",{56});
    {   // Poison: per-frame damage over a length in frames; shown as the total over seconds.
        int lo=0,hi=0,length=0;
        if(single(57,lo)&&single(58,hi)&&single(59,length)&&length>0) {
            const int a=int(int64_t(lo)*length/256),b=int(int64_t(hi)*length/256),seconds=length/25;
            lines.push_back({priorityOf(57),57,a==b?format(str("strModPoisonDamage"),{std::to_string(a),std::to_string(seconds)})
                :format(str("strModPoisonDamageRange"),{std::to_string(a),std::to_string(b),std::to_string(seconds)})});
            used.insert(57);used.insert(58);used.insert(59);
        }
    }
    {   // Enhanced damage: the max and min damage percent together.
        int maxPercent=0,minPercent=0;
        if(single(17,maxPercent)&&single(18,minPercent)&&maxPercent==minPercent) {
            // D2Client 0x6FAF37B8: plus (4002), the value, percent (4001), space (3995) and string 10023 "Enhanced Damage"
            // (by id: the key strModEnhancedDamage also names two "Enhanced damage" strings).
            lines.push_back({priorityOf(17),17,strings.byId(4002)+std::to_string(maxPercent)+strings.byId(4001)+strings.byId(3995)+strings.byId(10023)});
            used.insert(17);used.insert(18);
        }
    }
    // Description groups (dgrp): every member present with one value gives one line.
    {
        std::map<int,std::vector<int>> members;
        for(size_t id=0;id<table.size();++id)if(const auto* s=table.find(int(id));s&&s->group>0)members[s->group].push_back(int(id));
        for(const auto& [group,ids]:members) {
            int value=0;bool all=true,first=true;int priority=0;
            for(int id:ids) {
                int v=0;
                if(used.count(id)||!single(id,v)){all=false;break;}
                if(first){value=v;first=false;}else if(v!=value){all=false;break;}
                priority=std::max(priority,priorityOf(id));
            }
            if(!all||first)continue;
            const auto* s=table.find(ids.front());
            const std::string text=write(s->groupFunc,s->groupVal,s->groupPos,s->groupNeg,s->group2,shown(*s,value),0,s);
            if(!text.empty())lines.push_back({priority,ids.front(),text});
            for(int id:ids)used.insert(id);
        }
    }
    // The two-hand (23, 24) and throw (159, 160) copies of the minimum and maximum damage are not written when the
    // one-hand stat is there (D2Client 0x6FAF1FF7 looks the one-hand stat up after writing them).
    if(byId.count(21)){used.insert(23);used.insert(159);}
    if(byId.count(22)){used.insert(24);used.insert(160);}
    for(const auto& stat:stats) {
        const int id=int(stat.id);
        if(used.count(id))continue;
        const auto* s=table.find(id);
        if(!s||!s->descFunc)continue; // only stats with a description are written (D2Common's pStatsWithDescFunc)
        const std::string text=write(s->descFunc,s->descVal,s->descPos,s->descNeg,s->desc2,shown(*s,stat.value),int(stat.param),s);
        if(!text.empty())lines.push_back({s->descPriority,id,text});
    }
    // D2Common sorts the described stats by descpriority, lowest first, and D2Client puts each line above the ones before:
    // the highest priority is on top.
    std::stable_sort(lines.begin(),lines.end(),[](const Line& a,const Line& b){return a.priority!=b.priority?a.priority>b.priority:a.id>b.id;});
    std::vector<std::string> out;out.reserve(lines.size());
    // A few strings end with a line break (10023 "Enhanced Damage"); each property is one line here.
    for(auto& l:lines) {
        while(!l.text.empty()&&(l.text.back()==10||l.text.back()==13||l.text.back()==32))l.text.pop_back(); // line break, return, space
        if(!l.text.empty())out.push_back(std::move(l.text));
    }
    return out;
}
}
