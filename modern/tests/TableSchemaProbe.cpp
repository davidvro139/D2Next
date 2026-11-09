// Local read-only metadata audit. Never emits table rows or modifies mounted game archives.
#include "Tables.h"
#include <StormLib.h>
#include <charconv>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
static std::set<std::string> columns(const d2::Bytes& bytes){
    const std::string text(bytes.begin(),bytes.end());
    std::istringstream line(text.substr(0,text.find_first_of("\r\n")));
    std::set<std::string> out;std::string column;
    while(std::getline(line,column,'\t'))if(!column.empty())out.insert(column);
    return out;
}
static std::string quoted(const std::string& text){
    std::string out="\"";for(char c:text){if(c=='\"')out+='\"';out+=c;}return out+'\"';
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Usage: TableSchemaProbe BASE_GAME_DIR EXPLICIT_PATCH_MPQ");
    d2::Archives base(argv[1]);HANDLE patch=nullptr;
    const std::filesystem::path path=argv[2];
    if(!SFileOpenArchive(path.c_str(),0,MPQ_OPEN_READ_ONLY,&patch))throw std::runtime_error("Cannot open explicit patch archive");
    struct CloseArchive{HANDLE value;~CloseArchive(){SFileCloseArchive(value);}} closeArchive{patch};
    std::cout<<"table,status,base_rows,patch_rows,base_columns,patch_columns,change,column\n";
    // Every excel table the explicit archive lists (its (listfile)); without one, the tables mods change most.
    std::vector<std::string> names;
    {SFILE_FIND_DATA found{};HANDLE find=SFileFindFirstFile(patch,"*",&found,nullptr);
     const std::string folder="data\\global\\excel\\";
     if(find){do{std::string n=found.cFileName;for(auto& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));
         if(n.size()>folder.size()+4&&n.compare(0,folder.size(),folder)==0&&n.compare(n.size()-4,4,".txt")==0&&n.find('\\',folder.size())==std::string::npos)
             names.push_back(n.substr(folder.size(),n.size()-folder.size()-4));}while(SFileFindNextFile(find,&found));SFileFindClose(find);}
     // No (listfile): every table 1.10f has.
     if(names.empty())names={"armor","armtype","automagic","automap","belts","bodylocs","books","charstats","colors","compcode","composit","cubemain","cubemod","difficultylevels","elemtypes","events","experience","gamble","gems","hireling","hitclass","inventory","itemratio","itemstatcost","itemtypes","levels","lowqualityitems","lvlmaze","lvlprest","lvlsub","lvltypes","lvlwarp","magicprefix","magicsuffix","misc","misscalc","missiles","monai","monequip","monlvl","monmode","monplace","monpreset","monprop","monseq","monsounds","monstats","monstats2","montype","monumod","npc","objects","objgroup","objmode","objtype","overlay","pettype","playerclass","plrmode","plrtype","properties","qualityitems","rareprefix","raresuffix","runes","setitems","sets","shrines","skillcalc","skilldesc","skills","soundenviron","sounds","states","storepage","superuniques","treasureclass","treasureclassex","uniqueappellation","uniqueitems","uniqueprefix","uniquesuffix","uniquetitle","weapons","weaponclass"};}
    for(const auto& nameText:names){const char* name=nameText.c_str();
        bool inBase=true;try{base.read(std::string("data\\global\\excel\\")+name+".txt");}catch(...){inBase=false;}
        if(!inBase){std::cout<<name<<",absent-from-base,,,,,,,\n";continue;}
        const std::string file=std::string("data\\global\\excel\\")+name+".txt";
        const auto baseline=base.read(file);HANDLE stream=nullptr;
        if(!SFileOpenFileEx(patch,file.c_str(),SFILE_OPEN_FROM_MPQ,&stream)){
            std::cout<<name<<",absent-from-explicit-archive,,,,,,,\n";continue;
        }
        struct CloseFile{HANDLE value;~CloseFile(){SFileCloseFile(value);}} closeFile{stream};
        DWORD high=0,read=0;const DWORD size=SFileGetFileSize(stream,&high);
        if(high||size==SFILE_INVALID_SIZE||size>16*1024*1024)throw std::runtime_error("Invalid table size");
        d2::Bytes modified(size);
        if(size&&(!SFileReadFile(stream,modified.data(),size,&read,nullptr)||read!=size))throw std::runtime_error("Incomplete table read");
        const auto before=columns(baseline),after=columns(modified);
        const d2::TextTable a(std::string_view(reinterpret_cast<const char*>(baseline.data()),baseline.size()));
        const d2::TextTable b(std::string_view(reinterpret_cast<const char*>(modified.data()),modified.size()));
        const std::string prefix=std::string(name)+",read,"+std::to_string(a.rows())+","+std::to_string(b.rows())+","+std::to_string(before.size())+","+std::to_string(after.size())+",";
        std::cout<<prefix<<"summary,"<<(baseline==modified?"identical-bytes":"different-bytes")<<"\n";
        if(std::string_view(name)=="states") {
            unsigned maximum=0,outside=0,ids=0;
            for(size_t row=0;row<b.rows();++row) {
                const auto text=b.cell(row,"id");unsigned id=0;
                const auto parsed=std::from_chars(text.data(),text.data()+text.size(),id);
                if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())continue;
                ++ids;maximum=std::max(maximum,id);outside+=id>=192;
            }
            std::cout<<prefix<<"numeric-state-ids,"<<ids<<'\n'
                     <<prefix<<"maximum-state-id,"<<maximum<<'\n'
                     <<prefix<<"state-ids-at-or-above-192,"<<outside<<'\n';
        }
        for(const auto& column:after)if(!before.count(column))std::cout<<prefix<<"added,"<<quoted(column)<<'\n';
        for(const auto& column:before)if(!after.count(column))std::cout<<prefix<<"removed,"<<quoted(column)<<'\n';
        // The compiled table (what the game reads; after 1.10 Blizzard updated only these): a 4-byte record count, then
        // the records. Its record size is the binary layout.
        const std::string binFile=std::string("data\\global\\excel\\")+name+".bin";
        d2::Bytes baseBin;try{baseBin=base.read(binFile);}catch(...){}
        d2::Bytes patchBin;HANDLE binStream=nullptr;
        if(SFileOpenFileEx(patch,binFile.c_str(),SFILE_OPEN_FROM_MPQ,&binStream)){
            DWORD binHigh=0,got=0;const DWORD binSize=SFileGetFileSize(binStream,&binHigh);
            if(!binHigh&&binSize!=SFILE_INVALID_SIZE&&binSize<=64*1024*1024){patchBin.resize(binSize);if(!SFileReadFile(binStream,patchBin.data(),binSize,&got,nullptr)||got!=binSize)patchBin.clear();}
            SFileCloseFile(binStream);
        }
        auto layout=[](const d2::Bytes& bin)->std::string{
            if(bin.size()<4)return "none,";
            uint32_t count=0;std::memcpy(&count,bin.data(),4);
            const size_t body=bin.size()-4;
            return std::to_string(count)+","+(count&&body%count==0?std::to_string(body/count):"?");
        };
        std::cout<<name<<",bin,"<<layout(baseBin)<<","<<layout(patchBin)<<",records-and-record-size,"<<(baseBin==patchBin?"identical-bytes":"different-bytes")<<'\n';
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
