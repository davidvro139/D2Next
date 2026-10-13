// Native string tables for the standalone x64 host. Disk fields are read explicitly;
// no native pointer or packed runtime struct is overlaid on archive bytes.
#include <Windows.h>
#include <D2Lang.h>
#include <D2Unicode.h>
#include <Fog.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Bytes=std::vector<uint8_t>;
struct Entry {std::string key;std::vector<Unicode> text;bool used=false;};
// The hash table as stored in the file. A key is looked up the way the original does it: start at the slot the key
// hashes to and probe forward, at most `loops` slots, so of two nodes with the same key the first one probed wins.
struct Node {bool active=false;std::string key;uint16_t index=0;};
struct Table {std::vector<Entry> entries;std::vector<Node> nodes;uint32_t loops=0;};
struct Tables {std::array<Table,3> tables;};
constexpr unsigned bases[]={0,20000,10000}; // base, expansion, patch
constexpr int lookupOrder[]={2,1,0};        // patch, then expansion, then base: the first table that has the key wins
constexpr int notFound=-1;
Tables tables;
std::once_flag initialized;
const Unicode missing[]={Unicode('x'),Unicode(0)};
const Unicode empty[]={Unicode(0)};
std::mutex fallbackMutex;
std::unordered_map<std::string,std::vector<Unicode>> fallbacks;
const Unicode* fallback(const char* key) {
    if(!key||!*key)return empty;
    std::lock_guard<std::mutex> lock(fallbackMutex);
    auto& value=fallbacks[key];
    if(value.empty()) {
        // Like the original, convert with a count equal to the text length: toUnicode keeps at most count-1
        // characters, so the trailing "w" of the message is dropped.
        const std::string text=std::string(key)+" -not xlated call ken w";
        value.assign(text.size()+1,Unicode(0));Unicode::toUnicode(value.data(),text.c_str(),int(text.size()));
    }
    return value.data();
}
uint32_t read(const Bytes& b,size_t at,unsigned n) {
    if(at>b.size()||n>b.size()-at)throw std::runtime_error("Truncated TBL field");
    uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(b[at+i])<<(8*i);return v;
}
Bytes load(const std::string& path) {
    HSFILE file=nullptr;
    if(!FOG_FOpenFile(path.c_str(),&file))throw std::runtime_error("Missing string table: "+path);
    struct Close {HSFILE h;~Close(){FOG_FCloseFile(h);}} close{file};
    uint32_t high=0;auto size=FOG_FGetFileSize(file,&high);
    if(high||size>16*1024*1024)throw std::runtime_error("TBL exceeds 16 MiB");
    Bytes b(size);int count=0;
    if(!FOG_FReadFile(file,b.data(),b.size(),&count,0,0,0)||count!=int(size))throw std::runtime_error("Cannot read string table: "+path);
    return b;
}
std::string stringAt(const Bytes& b,size_t at,size_t limit) {
    if(at>=b.size()||!limit||limit>b.size()-at)throw std::runtime_error("Invalid TBL string range");
    auto end=std::find(b.begin()+at,b.begin()+at+limit,0);
    if(end==b.begin()+at+limit)throw std::runtime_error("Unterminated TBL string");
    return std::string(b.begin()+at,end);
}
Table decode(const Bytes& b) {
    const auto count=read(b,2,2),slots=read(b,4,4),start=read(b,9,4),size=read(b,17,4);
    if(read(b,8,1)!=1||count>10000||slots>65536||size!=b.size()||start>b.size()||21+size_t(count)*2+size_t(slots)*17>start)
        throw std::runtime_error("Invalid TBL header");
    Table t;t.entries.resize(count);t.loops=read(b,13,4);t.nodes.resize(slots);
    for(unsigned s=0;s<slots;++s) {
        const size_t p=21+2*size_t(count)+17*size_t(s);
        if(read(b,p,1)!=1)continue;
        const auto key=read(b,p+7,4);
        if(key<start)throw std::runtime_error("TBL text overlaps header");
        t.nodes[s]={true,stringAt(b,key,b.size()-key),uint16_t(read(b,p+1,2))};
    }
    for(unsigned i=0;i<count;++i) {
        const auto slot=read(b,21+2*i,2);
        if(slot>=slots)throw std::runtime_error("Invalid TBL index");
        const size_t p=21+2*count+17*slot;
        if(!read(b,p,1))continue;
        if(read(b,p+1,2)!=i)throw std::runtime_error("TBL index/node mismatch");
        const auto key=read(b,p+7,4),value=read(b,p+11,4),length=read(b,p+15,2);
        if(key<start||value<start)throw std::runtime_error("TBL text overlaps header");
        auto& e=t.entries[i];e.key=stringAt(b,key,b.size()-key);
        const auto utf8=stringAt(b,value,length);
        const int units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,nullptr,0);
        if(!units)throw std::runtime_error("Invalid UTF-8 in TBL");
        std::vector<wchar_t> wide(units);
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,wide.data(),units))throw std::runtime_error("TBL Unicode conversion failed");
        e.text.reserve(units);for(auto c:wide)e.text.emplace_back(uint16_t(c));e.used=true;
    }
    return t;
}
void initialize() {
    std::call_once(initialized,[] {
        Tables loaded;const char* names[]={"string","expansionstring","patchstring"};
        for(unsigned i=0;i<3;++i) {
            loaded.tables[i]=decode(load(std::string("data\\local\\lng\\eng\\")+names[i]+".tbl"));
        }
        tables=std::move(loaded);
    });
}
void ensure() {
    try {initialize();}
    catch(const std::exception& e){FOG_DisplayHalt(e.what(),__FILE__,__LINE__);throw;}
}
const Unicode* lookup(uint16_t id) {
    const unsigned which=id>=20000?1:id>=10000?2:0,index=id-bases[which];
    const auto& entries=tables.tables[which].entries;
    return index<entries.size()&&entries[index].used?entries[index].text.data():missing;
}
// The string id of a key, or notFound. Same hash and probing as the original (case sensitive).
int find(const char* key) {
    uint32_t hash=0;
    for(const char* c=key;*c;++c) {
        hash=(hash<<4)+uint32_t(int32_t(int8_t(*c)));
        if(const uint32_t high=hash&0xF0000000u)hash=(hash&0x0FFFFFFFu)^(high>>24);
    }
    for(const int which:lookupOrder) {
        const Table& t=tables.tables[which];
        if(t.nodes.empty())continue;
        size_t slot=hash%t.nodes.size();
        for(uint32_t probe=0;probe<t.loops;++probe,slot=(slot+1)%t.nodes.size())
            if(t.nodes[slot].active&&t.nodes[slot].key==key)return int(bases[which]+t.nodes[slot].index);
    }
    return notFound;
}
}
const Unicode* __fastcall D2LANG_GetStringByReferenceString(char* key) {
    ensure();if(!key||!*key)return empty;
    const int found=find(key);return found==notFound?fallback(key):lookup(uint16_t(found));
}
const Unicode* __fastcall D2LANG_GetStringFromTblIndex(short index) {
    ensure();return lookup(uint16_t(index));
}
short __stdcall D2LANG_GetTblIndex(char* key,const Unicode** text) {
    ensure();const int found=key&&*key?find(key):notFound;
    if(found==notFound){if(text)*text=fallback(key);return 0;}
    if(text)*text=lookup(uint16_t(found));return short(found);
}
