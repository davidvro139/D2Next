// The mod MPQ chain on real game data: ModChainTests <game folder> [scratch folder, same drive: hard links]. A scratch game folder of hard links to the real
// MPQs (nothing is copied or written in the game folder) gets Mods/test/1-test.mpq holding one changed file; with
// D2NEXT_MOD=test that file comes from the mod, the others still from the game, and without it all from the game.
#include "Assets.h"
#include "ArchivePlan.h"
#include "Tables.h"
#include <StormLib.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void setMod(const char* name) {
#ifdef _WIN32
    _putenv_s("D2NEXT_MOD",name);
#else
    if(*name)setenv("D2NEXT_MOD",name,1);else unsetenv("D2NEXT_MOD");
#endif
}
std::string text(const d2::Bytes& bytes) {return std::string(bytes.begin(),bytes.end());}
// A table with one more row: a copy of its last data row with the given cells replaced (column name -> value), as a mod adds
// a row past the game's.
std::string withRow(const std::string& table,const std::vector<std::pair<std::string,std::string>>& cells) {
    std::vector<std::vector<std::string>> rows;size_t at=0;
    while(at<table.size()){size_t end=table.find('\n',at);if(end==std::string::npos)end=table.size();std::string line=table.substr(at,end-at);if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::vector<std::string> row;size_t c=0;for(;;){size_t t=line.find('\t',c);row.push_back(line.substr(c,t==std::string::npos?std::string::npos:t-c));if(t==std::string::npos)break;c=t+1;}
        if(!line.empty())rows.push_back(row);at=end+1;}
    auto row=rows.back();
    for(const auto& [column,value]:cells)for(size_t i=0;i<rows[0].size();++i)if(rows[0][i]==column)row[i]=value;
    std::string out=table;while(!out.empty()&&(out.back()=='\n'||out.back()=='\r'))out.pop_back();out+="\r\n";
    for(size_t i=0;i<row.size();++i)out+=(i?"\t":"")+row[i];
    return out+"\r\n";
}
}

int main(int argc,char** argv) {
    namespace fs=std::filesystem;
    if(argc<2||argc>3){std::cerr<<"usage: ModChainTests <game folder> [scratch folder on the same drive]\n";return 2;}
    const fs::path game=argv[1];
    auto check=[](bool ok,const char* what){if(!ok){std::cerr<<"FAIL: "<<what<<"\n";std::exit(1);}};
    const fs::path root=(argc==3?fs::path(argv[2]):fs::temp_directory_path())/("d2next-mod-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    for(const auto& path:d2::vanillaArchivePlan(game)) {
        std::error_code error;
        fs::create_hard_link(path,root/path.filename(),error);
        if(error){std::cout<<"SKIP: cannot hard-link "<<path.string()<<" ("<<error.message()<<")\n";return 0;}
    }
    const std::string changed="data\\global\\excel\\misc.txt",kept="data\\global\\excel\\weapons.txt",loose="data\\global\\excel\\armor.txt";
    std::string vanillaChanged,vanillaKept,vanillaSounds,vanillaLevels;
    const std::string soundsName="data\\global\\excel\\sounds.txt",levelsName="data\\global\\excel\\levels.txt";
    const std::string cofName="data\\global\\monsters\\zz\\cof\\zzneu1hth.cof"; // art of a monster the game lacks
    {
        setMod("");
        d2::Archives archives(root);
        vanillaChanged=text(archives.read(changed));vanillaKept=text(archives.read(kept));
        vanillaSounds=text(archives.read(soundsName));vanillaLevels=text(archives.read(levelsName));
        bool missing=false;try{archives.read(cofName);}catch(...){missing=true;}
        check(missing,"the game has no zz COF");
    }
    // Rows past the game's: a sound (Index 9000) and a level (Id 300), as mods add them.
    const std::string modSounds=withRow(vanillaSounds,{{"Sound","zz_mod_sound"},{"Index","9000"},{"FileName","zz\\mod.wav"}});
    const std::string modLevels=withRow(vanillaLevels,{{"Name","Zz Mod Level"},{"LevelName","Zz Mod Level"},{"Id","300"},{"Waypoint","255"}});
    const std::string modCof="mod cof bytes";
    // The mod's misc.txt: the game's with a column the game does not know and a new item (a copy of the minor healing
    // potion, code zz1), as a mod adds them.
    std::string modText;
    {
        std::vector<std::string> lines;size_t at=0;
        while(at<vanillaChanged.size()){size_t end=vanillaChanged.find('\n',at);if(end==std::string::npos)end=vanillaChanged.size();std::string line=vanillaChanged.substr(at,end-at);if(!line.empty()&&line.back()=='\r')line.pop_back();lines.push_back(line);at=end+1;}
        while(!lines.empty()&&lines.back().empty())lines.pop_back();
        std::string copy;
        for(size_t i=0;i<lines.size();++i) {
            modText+=lines[i]+(i==0?"\tmodcolumn":"\tx")+"\r\n";
            if(i&&copy.empty())if(const auto tab=lines[i].find("\thp1\t");tab!=std::string::npos){copy=lines[i];copy.replace(tab,5,"\tzz1\t");}
        }
        check(!copy.empty(),"the game's misc.txt has hp1");
        modText+=copy+"\tx\r\n";
    }
    fs::create_directories(root/"Mods"/"test");
    // A loose file in the mod's data folder, in other letter case than the game asks for (Android is case-sensitive).
    const std::string looseText="loose armor.txt from the mod folder\r\n";
    fs::create_directories(root/"Mods"/"test"/"Data"/"Global"/"EXCEL");
    {std::ofstream(root/"Mods"/"test"/"Data"/"Global"/"EXCEL"/"Armor.TXT",std::ios::binary)<<looseText;}
    {
        HANDLE archive=nullptr;
        const auto mpq=(root/"Mods"/"test"/"1-test.mpq").native(); // TCHAR: wide on Windows
        check(SFileCreateArchive(mpq.c_str(),MPQ_CREATE_ARCHIVE_V1,16,&archive)!=0,"create the mod MPQ");
        HANDLE file=nullptr;
        check(SFileCreateFile(archive,changed.c_str(),0,DWORD(modText.size()),0,MPQ_FILE_COMPRESS|MPQ_FILE_REPLACEEXISTING,&file)!=0,"add a file");
        check(SFileWriteFile(file,modText.data(),DWORD(modText.size()),MPQ_COMPRESSION_ZLIB)!=0,"write the file");
        check(SFileFinishFile(file)!=0,"finish the file");
        for(const auto& [name,body]:{std::pair{soundsName,modSounds},{levelsName,modLevels},{cofName,modCof}}) {
            check(SFileCreateFile(archive,name.c_str(),0,DWORD(body.size()),0,MPQ_FILE_COMPRESS|MPQ_FILE_REPLACEEXISTING,&file)!=0,"add a file");
            check(SFileWriteFile(file,body.data(),DWORD(body.size()),MPQ_COMPRESSION_ZLIB)!=0,"write the file");
            check(SFileFinishFile(file)!=0,"finish the file");
        }
        SFileCloseArchive(archive);
    }
    auto packed=[](const char* code){uint32_t p=0;for(int i=0;i<4;++i)p|=uint32_t(code[i]&&i<3?uint8_t(code[i]):uint8_t(' '))<<(8*i);return p;};
    {
        setMod("");
        d2::Archives archives(root);
        const d2::StringTable strings(archives);const d2::ItemTable items(archives,strings);
        check(items.find(packed("hp1"))&&!items.find(packed("zz1")),"vanilla has no zz1");
    }
    {
        setMod("test");
        d2::Archives archives(root);
        check(archives.count()==d2::vanillaArchivePlan(root).size()+1,"the mod MPQ is mounted");
        check(text(archives.read(changed))==modText,"a file the mod has comes from the mod");
        check(text(archives.read(kept))==vanillaKept,"a file the mod lacks comes from the game");
        check(text(archives.read(loose))==looseText,"a loose file in Mods/<name>/data comes before the MPQs, any letter case");
        check(d2::modSaveDirectory(root/"Save")==root/"Save"/"Mods"/"test","the mod's saves are apart");
        const d2::StringTable strings(archives);const d2::ItemTable items(archives,strings);
        const auto* added=items.find(packed("zz1"));const auto* base=items.find(packed("hp1"));
        check(added&&base,"the mod's new item and the game's both load (an unknown column is ignored)");
        check(added->invFile==base->invFile&&added->invWidth==base->invWidth&&added->type==base->type,"the new item's columns are read");
        const d2::MonsterTable monsters(archives);const d2::SoundBank sounds(archives,monsters);
        check(sounds.file("zz_mod_sound")=="zz\\mod.wav"&&sounds.fileByIndex(9000)=="zz\\mod.wav","a sounds.txt row past the game's is known by name and Index");
        const d2::LevelTable levels(archives);
        check(levels.byId(300)&&levels.byId(300)->name=="Zz Mod Level"&&levels.byId(1),"a levels.txt row past the game's loads");
        check(text(archives.read(cofName))==modCof,"a COF the game lacks comes from the mod");
    }
    // D2NEXT_MODTEST_OUT=<folder>: keep a copy of the test mod's MPQ there (to try it in the game: <game>/Mods/test).
    if(const char* out=std::getenv("D2NEXT_MODTEST_OUT");out&&*out) {
        std::error_code copyError;fs::create_directories(out,copyError);
        fs::copy_file(root/"Mods"/"test"/"1-test.mpq",fs::path(out)/"1-test.mpq",fs::copy_options::overwrite_existing,copyError);
        check(!copyError,"copy the test mod out");
    }
    setMod("");
    std::error_code error;fs::remove_all(root,error); // hard links only: the game's MPQs stay
    std::cout<<"PASS: mod file replaces the game's, other files from the game, vanilla unchanged without the mod; a mod item row and an unknown column load; mod sound, level and COF past the game's\n";
    return 0;
}
