// Read-only AnimData/COF audit. Layout: source/D2Common/include/DataTbls/AnimTbls.h
// usage: AnimationAudit GAME_DIR REPORT.csv
// Exit 0 only when raw metadata and the reverse table coverage have no open cases.
// Does not establish speed formulas, layer order, pixel fidelity or skill sequencing.
#include "Tables.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

static uint32_t read32(const d2::Bytes& data,size_t at) {
    if(at>data.size()||data.size()-at<4)throw std::runtime_error("Truncated AnimData count");
    return uint32_t(data[at])|(uint32_t(data[at+1])<<8)|(uint32_t(data[at+2])<<16)|(uint32_t(data[at+3])<<24);
}
static d2::Bytes optionalRead(const d2::Archives& archives,const std::string& name) {
    try{return archives.read(name);}
    catch(const std::exception& e) {
        if(std::string(e.what()).starts_with("Asset not found in mounted MPQs:"))return {};
        throw;
    }
}
static std::string upper(std::string value) {
    for(char& c:value)if(c>='a'&&c<='z')c=char(c-'a'+'A');
    return value;
}
static std::string csv(const std::string& value) {
    std::string out="\"";for(char c:value){if(c=='\"')out+='\"';out+=c;}return out+'\"';
}
static std::string tokenCell(const d2::TextTable& table,size_t row) {
    // TokenTbls' linker accepts case-insensitive headers; TextTable is exact.
    for(const char* column:{"token","Token","TOKEN"})if(table.hasColumn(column))return table.cell(row,column);
    throw std::runtime_error("Token table has no token column");
}
// Candidates, not proof of reachability: includes unused rows and disabled modes.
// Keep these visible so missing art is not silently excused by a token collision.
struct Reference {std::string root,label,weapon;std::set<std::string> modes;bool compositeDeath=false;std::map<std::string,std::string> sequences;};
static std::map<std::string,std::vector<Reference>> references(const d2::Archives& archives) {
    auto load=[&](const char* name) {
        const auto bytes=archives.read(std::string("data\\global\\excel\\")+name+".txt");
        return d2::TextTable(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    };
    std::map<std::string,std::vector<Reference>> out;
    const auto players=load("plrtype"),playerModes=load("plrmode"),monsters=load("monstats"),visual=load("monstats2"),objects=load("objects");
    std::set<std::string> playerTokens;
    for(size_t r=0;r<playerModes.rows();++r)if(auto mode=upper(tokenCell(playerModes,r));mode.size()==2)playerTokens.insert(mode);
    size_t playerClasses=0;
    for(size_t r=0;r<players.rows();++r)if(const auto token=upper(tokenCell(players,r));token.size()==2) {
        out[token].push_back({"chars","plrtype-row:"+std::to_string(r),"",playerTokens});++playerClasses;
    }
    if(!playerClasses||playerTokens.empty())throw std::runtime_error("Player class/mode coverage is empty");
    std::cout<<"player_classes="<<playerClasses<<" player_mode_rows="<<playerModes.rows()<<" player_art_tokens="<<playerTokens.size()<<'\n';
    std::map<std::string,size_t> byId;
    for(size_t r=0;r<visual.rows();++r)byId[upper(visual.cell(r,"Id"))]=r;
    for(size_t r=0;r<monsters.rows();++r) {
        const auto token=upper(monsters.cell(r,"Code"));if(token.size()!=2)continue;
        Reference ref{"monsters","monstats:"+monsters.cell(r,"hcIdx")+":"+monsters.cell(r,"Id"),"",{}};
        if(auto it=byId.find(upper(monsters.cell(r,"MonStatsEx")));it!=byId.end()) {
            ref.weapon=upper(visual.cell(it->second,"BaseW"));if(ref.weapon.empty())ref.weapon="HTH";
            ref.compositeDeath=visual.cell(it->second,"compositeDeath")=="1";
            for(const char* mode:d2::monsterModeTokens)if(visual.cell(it->second,"m"+upper(mode))=="1")ref.modes.insert(upper(mode));
        }
        // DATATBLS_MonStatsSkillModeLinker: non-mode names resolve through MonSeq.
        for(int skill=1;skill<=8;++skill) {
            const auto name=monsters.cell(r,"Skill"+std::to_string(skill));
            const auto mode=upper(monsters.cell(r,"Sk"+std::to_string(skill)+"mode"));
            if(name.empty()||mode.empty())continue;
            bool ordinary=false;
            for(const char* token:d2::monsterModeTokens)ordinary|=mode==upper(token)&&mode!="SQ";
            if(!ordinary)ref.sequences.emplace("Skill"+std::to_string(skill)+":"+name,mode);
        }
        out[token].push_back(std::move(ref));
    }
    const char* objectModes[]={"NU","OP","ON","S1","S2","S3","S4","S5"};
    for(size_t r=0;r<objects.rows();++r) {
        const auto token=upper(objects.cell(r,"Token"));if(token.size()!=2)continue;
        Reference ref{"objects","objects-row:"+std::to_string(r)+":"+objects.cell(r,"Name"),"HTH",{}};
        for(int m=0;m<8;++m)if(objects.cell(r,"Mode"+std::to_string(m))=="1")ref.modes.insert(objectModes[m]);
        out[token].push_back(std::move(ref));
    }
    return out;
}
// Reverse coverage: enumerate tables first so absent AnimData keys are visible.
// This is a static mode/weapon matrix, not proof that every combination is legal
// equipment, reachable in a level, or animated with the correct speed in game.
static size_t coverage(const d2::Archives& archives,const d2::Bytes& data,
        const std::map<std::string,size_t>& firstRecords,
        const std::map<std::string,std::vector<Reference>>& refs,const std::string& path) {
    std::ofstream report(path);if(!report)throw std::runtime_error("Cannot open coverage report");
    report<<"root,reference,requested_mode,key,cof_frames,anim_frames,speed,status\n";
    size_t rows=0,unverified=0;std::map<std::string,size_t> counts;
    // D2Common D2Composit.cpp weapon classes, plus default HTH.
    const std::set<std::string> playerWeapons={"HTH","1HS","1HT","BOW","2HS","2HT","1JS","1JT","1SS","1ST","STF","XBW","HT1","HT2"};
    for(const auto& [token,candidates]:refs)for(const auto& ref:candidates)for(const auto& requested:ref.modes) {
        auto weapons=ref.root=="chars"?playerWeapons:std::set<std::string>{ref.weapon};
        if((requested=="DT"||requested=="DD")&&(ref.root=="chars"||!ref.compositeDeath))weapons={"HTH"};
        for(const auto& weapon:weapons) {
            const auto mode=requested=="KB"?std::string("GH"):requested;
            const auto key=token+mode+weapon;
            unsigned cofFrames=0,animFrames=0;int32_t speed=0;std::string status;
            if(requested=="SQ")status="requires-sequence-audit";
            else {
                std::string lower=key;for(char& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
                const auto cof=optionalRead(archives,"data\\global\\"+ref.root+"\\"+lower.substr(0,2)+"\\cof\\"+lower+".cof");
                const auto record=firstRecords.find(key);
                if(record!=firstRecords.end()){animFrames=read32(data,record->second+8);speed=int32_t(read32(data,record->second+12));}
                if(cof.empty())status=record==firstRecords.end()?"absent-combination-unverified":"missing-cof";
                else if(cof.size()<28||!cof[0]||cof[0]>16||!cof[1]||!cof[2])status="malformed-cof";
                else {
                    cofFrames=cof[1];const size_t events=28+size_t(cof[0])*9;
                    if(events+cofFrames+size_t(cof[0])*cofFrames*cof[2]>cof.size())status="truncated-cof";
                    else if(record==firstRecords.end())status="missing-animdata";
                    else if(cofFrames!=animFrames)status="frame-count";
                    else {
                        status="ok";
                        for(size_t f=0;f<cofFrames;++f)if((f<144?data[record->second+16+f]:0)!=cof[events+f]){status="frame-tags";break;}
                    }
                }
            }
            ++rows;++counts[status];if(status!="ok")++unverified;
            report<<ref.root<<','<<csv(ref.label)<<','<<requested<<','<<key<<','<<cofFrames<<','<<animFrames<<','<<speed<<','<<status<<'\n';
        }
    }
    report.flush();if(!report)throw std::runtime_error("Coverage report write failed");
    std::cout<<"coverage_rows="<<rows<<" coverage_unverified="<<unverified<<'\n';
    for(const auto& [status,count]:counts)std::cout<<"coverage."<<status<<'='<<count<<'\n';
    return unverified;
}
static size_t sequenceCoverage(const d2::Archives& archives,const d2::Bytes& data,
        const std::map<std::string,size_t>& firstRecords,
        const std::map<std::string,std::vector<Reference>>& refs,const std::string& path) {
    const auto bytes=archives.read("data\\global\\excel\\monseq.txt");
    const d2::TextTable sequences(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    for(const char* column:{"sequence","mode","frame","event"})if(!sequences.hasColumn(column))throw std::runtime_error("Missing MonSeq column: "+std::string(column));
    std::map<std::string,std::vector<size_t>> steps;
    for(size_t row=0;row<sequences.rows();++row)if(auto name=upper(sequences.cell(row,"sequence"));!name.empty())steps[name].push_back(row);
    std::ofstream report(path);if(!report)throw std::runtime_error("Cannot open sequence report");
    report<<"reference,skill,sequence,step,mode,key,frame,cof_frames,event,status\n";
    size_t links=0,rows=0,unverified=0;std::map<std::string,size_t> counts;
    for(const auto& [token,candidates]:refs)for(const auto& ref:candidates)for(const auto& [skill,name]:ref.sequences) {
        ++links;
        auto found=steps.find(name);
        if(found==steps.end()) {
            report<<csv(ref.label)<<','<<csv(skill)<<','<<csv(name)<<",,,,,,,missing-sequence\n";
            ++rows;++unverified;++counts["missing-sequence"];continue;
        }
        size_t step=0;
        for(const auto row:found->second) {
            const auto mode=upper(sequences.cell(row,"mode"));
            const auto frameText=sequences.cell(row,"frame");
            const unsigned frame=frameText.empty()?0:unsigned(std::stoul(frameText));
            auto weapon=ref.weapon;
            if((mode=="DT"||mode=="DD")&&!ref.compositeDeath)weapon="HTH";
            const auto key=token+(mode=="KB"?std::string("GH"):mode)+weapon;
            std::string lower=key;for(char& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            const auto cof=optionalRead(archives,"data\\global\\monsters\\"+lower.substr(0,2)+"\\cof\\"+lower+".cof");
            unsigned cofFrames=0;std::string status="ok";
            if(cof.empty())status="missing-cof";
            else if(cof.size()<28||!cof[0]||cof[0]>16||!cof[1]||!cof[2])status="malformed-cof";
            else {
                cofFrames=cof[1];const size_t events=28+size_t(cof[0])*9;
                if(events+cofFrames+size_t(cof[0])*cofFrames*cof[2]>cof.size())status="truncated-cof";
                else if(frame>=cofFrames)status="sequence-frame-outside-cof";
                else if(auto record=firstRecords.find(key);record==firstRecords.end())status="missing-animdata";
                else if(read32(data,record->second+8)!=cofFrames)status="animdata-frame-count";
            }
            if(status=="ok"&&!ref.modes.count(mode=="KB"?"GH":mode))status="mode-disabled-in-monstats2";
            ++rows;++counts[status];if(status!="ok")++unverified;
            report<<csv(ref.label)<<','<<csv(skill)<<','<<csv(name)<<','<<step++<<','<<mode<<','<<key<<','<<frame<<','<<cofFrames<<','<<csv(sequences.cell(row,"event"))<<','<<status<<'\n';
        }
    }
    report.flush();if(!report)throw std::runtime_error("Sequence report write failed");
    std::cout<<"sequence_skill_links="<<links<<" sequence_steps="<<rows<<" sequence_unverified="<<unverified<<'\n';
    for(const auto& [status,count]:counts)std::cout<<"sequence."<<status<<'='<<count<<'\n';
    return unverified;
}
int main(int argc,char** argv) {try {
    if(argc!=3){std::cerr<<"Usage: AnimationAudit GAME_DIR REPORT.csv\n";return 2;}
    d2::Archives archives(argv[1]);
    const auto tableReferences=references(archives);
    const auto data=archives.read("data\\global\\animdata.d2");
    std::ofstream report(argv[2]);if(!report)throw std::runtime_error("Cannot open report");
    report<<"key,root,anim_frames,cof_frames,speed,anim_events,cof_events,status,table_candidates,per_root_checks\n";
    size_t cursor=0,records=0,matching=0,unresolved=0;
    std::set<std::string> names;
    std::map<std::string,size_t> firstRecords;
    std::map<std::string,size_t> statuses;
    for(unsigned bucket=0;bucket<256;++bucket) {
        const auto count=read32(data,cursor);cursor+=4;
        if(count>(data.size()-cursor)/160)throw std::runtime_error("Truncated AnimData bucket");
        for(uint32_t index=0;index<count;++index,cursor+=160) {
            const auto* record=data.data()+cursor;
            size_t length=0;while(length<8&&record[length])++length;
            if(length!=7)throw std::runtime_error("Unexpected AnimData key length at "+std::to_string(cursor));
            std::string key(reinterpret_cast<const char*>(record),length);
            unsigned hash=0;
            for(unsigned char c:key) {if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')))throw std::runtime_error("Invalid AnimData key");hash+=c;}
            if((hash&255)!=bucket)throw std::runtime_error("AnimData hash mismatch: "+key);
            const bool duplicate=!names.insert(key).second;
            firstRecords.emplace(key,cursor);
            const auto frames=read32(data,cursor+8),speed=read32(data,cursor+12);
            std::string lower=key;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;
            d2::Bytes cof;std::string root,rootChecks;unsigned rootCount=0;
            for(const auto* folder:{"chars","monsters","objects"}) {
                auto candidate=optionalRead(archives,"data\\global\\"+std::string(folder)+"\\"+lower.substr(0,2)+"\\cof\\"+lower+".cof");
                if(!candidate.empty()) {
                    if(!rootChecks.empty())rootChecks+='|';rootChecks+=folder;rootChecks+=':';
                    if(candidate.size()<28||!candidate[0]||candidate[0]>16||!candidate[1]||!candidate[2])rootChecks+="malformed";
                    else {
                        const size_t events=28+size_t(candidate[0])*9;
                        if(events+candidate[1]+size_t(candidate[0])*candidate[1]*candidate[2]>candidate.size())rootChecks+="truncated";
                        else if(frames!=candidate[1])rootChecks+="frame-count="+std::to_string(candidate[1]);
                        else {
                            bool equal=true;
                            for(size_t f=0;f<frames;++f)equal&=(f<144?record[16+f]:0)==candidate[events+f];
                            rootChecks+=equal?"ok":"frame-tags";
                        }
                    }
                    if(cof.empty())cof=std::move(candidate);
                    if(!root.empty())root+='|';root+=folder;++rootCount;
                }
            }
            std::string status="ok";unsigned cofFrames=0,animEvents=0,cofEvents=0;
            for(size_t i=0;i<std::min<size_t>(frames,144);++i)animEvents+=record[16+i]!=0;
            if(cof.empty())status="missing-cof";
            else if(cof.size()<28||!cof[0]||cof[0]>16||!cof[1]||!cof[2])status="malformed-cof";
            else {
                cofFrames=cof[1];const size_t events=28+size_t(cof[0])*9;
                const size_t end=events+cofFrames+size_t(cof[0])*cofFrames*cof[2];
                if(end>cof.size())status="truncated-cof";
                else {
                    for(unsigned i=0;i<cofFrames;++i)cofEvents+=cof[events+i]!=0;
                    if(frames!=cofFrames)status="frame-count";
                    else for(size_t i=0;i<frames;++i)if((i<144?record[16+i]:0)!=cof[events+i]){status="frame-tags";break;}
                }
            }
            // A token can exist in multiple unit types (e.g. a monster and an object).
            // Do not silently assume the first folder is the caller's unit type.
            if(rootCount>1)status="ambiguous-root";
            // D2Common lookup scans a bucket from its first entry; later duplicates are inactive.
            if(duplicate)status="duplicate-ignored";
            ++records;++statuses[status];if(status=="ok")++matching;else if(!duplicate)++unresolved;
            std::string candidates;
            if(auto it=tableReferences.find(key.substr(0,2));it!=tableReferences.end())for(const auto& ref:it->second) {
                if(!candidates.empty())candidates+='|';
                candidates+=ref.root+":"+ref.label;
                if(ref.root!="chars")candidates+=(ref.modes.count(key.substr(2,2))?":mode-enabled":":mode-disabled")+std::string(":basew=")+ref.weapon;
            }
            report<<key<<','<<root<<','<<frames<<','<<cofFrames<<','<<int32_t(speed)<<','<<animEvents<<','<<cofEvents<<','<<status<<','<<csv(candidates)<<','<<csv(rootChecks)<<'\n';
        }
    }
    if(cursor!=data.size())throw std::runtime_error("Unexpected trailing AnimData bytes");
    report.flush();if(!report)throw std::runtime_error("Report write failed");
    std::cout<<"records="<<records<<" matching="<<matching<<" unresolved="<<unresolved<<'\n';
    for(const auto& [status,count]:statuses)std::cout<<status<<'='<<count<<'\n';
    const auto uncovered=coverage(archives,data,firstRecords,tableReferences,std::string(argv[2])+".coverage.csv");
    const auto sequenceUncovered=sequenceCoverage(archives,data,firstRecords,tableReferences,std::string(argv[2])+".sequences.csv");
    return unresolved||uncovered||sequenceUncovered?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
