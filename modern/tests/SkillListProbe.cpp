// Lists the class skills with what decides how they behave, for modern/SKILLS-PROGRESS.md: id, class, name in the game's
// language, and the skills.txt columns that sort a skill into spell, weapon skill, aura, passive or summon.
// SkillListProbe <game dir>   (tab-separated: id class name passive aura summon anim seqnum range srvdofunc cltdofunc)
#include "Assets.h"
#include "Tables.h"
#include <iostream>
int main(int argc,char** argv){try {
    if(argc<2)return 2;
    d2::Archives archives(argv[1]);
    d2::StringTable strings(archives);
    d2::SkillTable skills(archives,strings);
    const auto bytes=archives.read("data\\global\\excel\\skills.txt");
    const d2::TextTable table(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    static const char* classes[]={"ama","sor","nec","pal","bar","dru","ass"};
    for(size_t r=0;r<table.rows();++r) {
        const auto id=table.cell(r,"Id");if(id.empty())continue;
        const auto* info=skills.find(uint32_t(std::stoul(id)));if(!info||info->charClass<0)continue;
        std::cout<<id<<'\t'<<classes[info->charClass]<<'\t'<<info->displayName<<'\t'<<table.cell(r,"passive")<<'\t'<<table.cell(r,"aura")<<'\t'
            <<table.cell(r,"summon")<<'\t'<<table.cell(r,"anim")<<'\t'<<table.cell(r,"seqnum")<<'\t'<<table.cell(r,"range")<<'\t'
            <<table.cell(r,"srvdofunc")<<'\t'<<table.cell(r,"cltdofunc")<<'\n';
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
