#include "Assets.h"
#include "ProgramPath.h"
#include <StormLib.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2 {
static bool accessiblePathExists(const std::filesystem::path& path) {
    std::error_code error;
    const bool exists=std::filesystem::exists(path,error);
    if(error)throw std::runtime_error("Cannot access game file "+path.string()+": "+error.message());
    return exists;
}
static uint32_t u32(std::span<const uint8_t> b, size_t offset) {
    if (offset > b.size() || b.size() - offset < 4) throw std::runtime_error("Truncated DC6 header");
    return uint32_t(b[offset]) | uint32_t(b[offset+1]) << 8 | uint32_t(b[offset+2]) << 16 | uint32_t(b[offset+3]) << 24;
}
Palette decodePalette(std::span<const uint8_t> bytes) {
    if (bytes.size() != 768) throw std::runtime_error("PAL.DAT must contain 256 BGR colors");
    Palette palette{};
    for (size_t i=0; i<256; ++i) palette[i] = {bytes[i*3+2], bytes[i*3+1], bytes[i*3], 255};
    return palette;
}
uint32_t dc6FrameCount(std::span<const uint8_t> b) {
    if (u32(b,0) != 6) throw std::runtime_error("Unsupported DC6 version");
    const uint64_t count = uint64_t(u32(b,16)) * u32(b,20);
    if (!count || count > 4096 || b.size() < 24 + count*4) throw std::runtime_error("Invalid DC6 frame table");
    return uint32_t(count);
}
std::array<int32_t,2> dc6FrameOffset(std::span<const uint8_t> b,uint32_t frame) {
    const auto count=dc6FrameCount(b);
    if(frame>=count) throw std::runtime_error("DC6 frame index out of range");
    const size_t header=u32(b,24+size_t(frame)*4);
    if(header < 24+size_t(count)*4 || header > b.size() || b.size()-header < 32) throw std::runtime_error("Invalid DC6 frame offset");
    return {int32_t(u32(b,header+12)),int32_t(u32(b,header+16))};
}
Image decodeDc6(std::span<const uint8_t> b, const Palette& palette, uint32_t frame) {
    const auto count=dc6FrameCount(b);
    if (frame >= count) throw std::runtime_error("DC6 frame index out of range");
    const size_t header = u32(b,24+size_t(frame)*4);
    if (header < 24+size_t(count)*4 || header > b.size() || b.size()-header < 32) throw std::runtime_error("Invalid DC6 frame offset");
    const uint32_t flip=u32(b,header), w=u32(b,header+4), h=u32(b,header+8), length=u32(b,header+28);
    if (!w || !h || w>4096 || h>4096 || uint64_t(w)*h>16*1024*1024 || flip>1)
        throw std::runtime_error("Invalid DC6 dimensions or orientation");
    size_t cursor=header+32;
    if (length > b.size()-cursor) throw std::runtime_error("Truncated DC6 pixel stream");
    const size_t end=cursor+length;
    Image image{w,h,Bytes(size_t(w)*h*4,0)};
    uint32_t x=0,row=0;
    while(cursor<end && row<h) {
        const uint8_t code=b[cursor++];
        if(code==0x80) { x=0; ++row; continue; }
        const uint32_t run=code&0x7f;
        if(!run || run>w-x) throw std::runtime_error("DC6 run exceeds scanline");
        if(code&0x80) { x+=run; continue; }
        if(run>end-cursor) throw std::runtime_error("Truncated DC6 literal run");
        const uint32_t y=flip ? row : h-1-row;
        for(uint32_t i=0;i<run;++i) {
            const auto& color=palette[b[cursor++]];
            std::copy(color.begin(),color.end(),image.rgba.begin()+(size_t(y)*w+x++)*4);
        }
    }
    if(row!=h) throw std::runtime_error("DC6 pixel stream ends before final scanline");
    return image;
}
Archives::Archives(const std::filesystem::path& root):root_(root) {
    // Most specific archive first. Expansion data overrides base data.
    std::vector<std::filesystem::path> paths;
    const auto core=d2::gameFile(root,L"D2SE/CORES/1.10f/Patch_D2.mpq");
    if(accessiblePathExists(core)) paths.push_back(core);
    else if(accessiblePathExists(d2::gameFile(root,L"Patch_D2.mpq"))) paths.push_back(d2::gameFile(root,L"Patch_D2.mpq"));
    // Then the sounds: effects, the NPCs' speech (d2xtalk the expansion's), the music (d2xmusic Act V's).
    for(const auto* name : {L"d2exp.mpq", L"d2data.mpq", L"d2char.mpq", L"d2sfx.mpq", L"d2xtalk.mpq", L"d2speech.mpq", L"D2XVIDEO.MPQ", L"d2video.mpq",
        L"d2xmusic.mpq", L"d2music.mpq"}) {
        auto path=d2::gameFile(root,name);
        if(accessiblePathExists(path)) paths.push_back(path);
    }
    try {
        if(!accessiblePathExists(d2::gameFile(root,L"d2data.mpq"))) throw std::runtime_error("Game directory must contain d2data.mpq");
        for(const auto& path:paths) {
            HANDLE archive=nullptr;
            if(!SFileOpenArchive(path.c_str(),0,MPQ_OPEN_READ_ONLY,&archive))
                throw std::runtime_error("Cannot open archive: "+path.string());
            handles_.push_back(archive);
        }
    } catch(...) { for(auto h:handles_) SFileCloseArchive(h); throw; }
}
Archives::~Archives() { for(auto h:handles_) SFileCloseArchive(h); }
Bytes Archives::read(const std::string& name,size_t maxSize) const {
    for(auto archive:handles_) {
        HANDLE file=nullptr;
        if(!SFileOpenFileEx(archive,name.c_str(),SFILE_OPEN_FROM_MPQ,&file)) continue;
        struct Close { HANDLE file; ~Close(){SFileCloseFile(file);} } close{file};
        DWORD high=0;
        const DWORD size=SFileGetFileSize(file,&high);
        if(high || size==SFILE_INVALID_SIZE || size>maxSize) throw std::runtime_error("Asset exceeds size limit: "+name);
        Bytes bytes(size); DWORD read=0;
        if(size && (!SFileReadFile(file,bytes.data(),size,&read,nullptr) || read!=size))
            throw std::runtime_error("Cannot read asset: "+name);
        return bytes;
    }
    throw std::runtime_error("Asset not found in mounted MPQs: "+name);
}
}
