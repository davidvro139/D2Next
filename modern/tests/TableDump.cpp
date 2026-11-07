// D2MOO's tables, loaded the way the game ships them (.bin) or from the .txt files (the path a mod uses):
// TableDump <game folder> bin|txt <output folder>. Writes each table's records as raw bytes (<name>.dat, count * size)
// and a list (tables.txt: name count size). Pointer fields differ between runs by nature; tools/compare-tables.py
// compares two dumps field by field and reports the rest.
#include "ArchivePlan.h"
#include <windows.h>
#include <Storm.h>
#include <D2DataTbls.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    namespace fs=std::filesystem;
    if(argc!=4||(std::string(argv[2])!="bin"&&std::string(argv[2])!="txt")){std::cerr<<"usage: TableDump <game folder> bin|txt <output folder>\n";return 2;}
    const fs::path root=fs::canonical(argv[1]),out=argv[3];
    fs::create_directories(out);
    const auto paths=d2::vanillaArchivePlan(root);
    for(size_t i=0;i<paths.size();++i) {
        HSARCHIVE handle=nullptr;
        if(!SFileOpenArchive(fs::absolute(paths[i]).string().c_str(),d2::archivePriority(i),2,&handle)){std::cerr<<"cannot mount "<<paths[i].string()<<"\n";return 1;}
    }
    // D2NEXT_TABLEDUMP_RAW=name,name: also copy those archive files (data\global\excel\<name>) to the output folder.
    if(const char* raw=std::getenv("D2NEXT_TABLEDUMP_RAW")) {
        std::string names=raw;
        for(size_t at=0;at<names.size();) {
            size_t end=names.find(',',at);if(end==std::string::npos)end=names.size();
            const std::string name=names.substr(at,end-at);at=end+1;
            size_t size=0;
            void* data=ARCHIVE_ALLOC_BUFFER_AND_READ_FILE_TO_IT(nullptr,("DATA\\GLOBAL\\EXCEL\\"+name).c_str(),&size);
            if(data){std::ofstream(out/name,std::ios::binary).write(static_cast<const char*>(data),std::streamsize(size));}
        }
    }
    DATATBLS_LoadFromBin=std::string(argv[2])=="bin";
    // As the game's -txt switch: also compile the small code tables (modes, body locations, classes...) the other
    // tables link to. Their .bin copies are written under DATA\GLOBAL\EXCEL of the working folder when it exists.
    if(!DATATBLS_LoadFromBin)DATATBLS_ToggleCompileTxtFlag(FALSE);
    DATATBLS_LoadAllTxts(nullptr,1,1);
    std::ofstream list(out/"tables.txt");
    auto dump=[&](const char* name,const void* records,int count,size_t size) {
        list<<name<<" "<<count<<" "<<size<<"\n";
        std::ofstream file(out/(std::string(name)+".dat"),std::ios::binary);
        if(records&&count>0)file.write(static_cast<const char*>(records),std::streamsize(size_t(count)*size));
    };
    auto* t=sgptDataTables;
#define D2_DUMP(NAME) dump(#NAME,t->p##NAME##Txt,t->n##NAME##TxtRecordCount,sizeof(*t->p##NAME##Txt))
    D2_DUMP(CompCode);D2_DUMP(MonAi);D2_DUMP(Properties);D2_DUMP(States);D2_DUMP(Hireling);D2_DUMP(Npc);
    D2_DUMP(MonStats);D2_DUMP(MonSounds);D2_DUMP(MonStats2);D2_DUMP(MonPlace);D2_DUMP(SuperUniques);D2_DUMP(Missiles);
    D2_DUMP(MonLvl);D2_DUMP(MonSeq);D2_DUMP(SkillDesc);D2_DUMP(Skills);D2_DUMP(Overlay);D2_DUMP(CharStats);
    D2_DUMP(ItemStatCost);D2_DUMP(MonEquip);D2_DUMP(PetType);D2_DUMP(ItemTypes);D2_DUMP(Sets);D2_DUMP(SetItems);
    D2_DUMP(UniqueItems);D2_DUMP(MonProp);D2_DUMP(MonType);D2_DUMP(MonUMod);D2_DUMP(Levels);D2_DUMP(LvlPrest);
    D2_DUMP(DifficultyLevels);D2_DUMP(CubeMain);D2_DUMP(Inventory);D2_DUMP(LvlTypes);D2_DUMP(LvlWarp);D2_DUMP(LvlMaze);
    D2_DUMP(LvlSub);D2_DUMP(UniqueTitle);D2_DUMP(UniquePrefix);D2_DUMP(UniqueSuffix);D2_DUMP(UniqueAppellation);
    D2_DUMP(Shrines);D2_DUMP(Objects);D2_DUMP(ObjGroup);
#undef D2_DUMP
    // The compiled formulas the records point into (one-byte "records")
    dump("SkillsCode",t->pSkillsCode,int(t->nSkillsCodeSize),1);dump("SkillDescCode",t->pSkillDescCode,int(t->nSkillDescCodeSize),1);
    dump("MissCode",t->pMissCode,int(t->nMissCodeSize),1);dump("ItemsCode",t->pItemsCode,int(t->nItemsCodeSize),1);
    std::cout<<"dumped "<<argv[2]<<" tables to "<<out.string()<<"\n";
    std::fflush(stdout);
    std::_Exit(0); // no teardown: the dump is all this is for
}
