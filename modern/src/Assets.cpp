#include "Assets.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include "AssetTiming.h"
#include "ArchivePlan.h"
#include "ProgramPath.h"
#include <StormLib.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace d2 {
Bytes Image::expanded() const {
    if(indices.empty())return rgba;
    Bytes out(indices.size()*4);
    for(size_t i=0;i<indices.size();++i)std::copy_n((*colors)[indices[i]].begin(),4,out.begin()+i*4);
    return out;
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
    AssetTimer timing(AssetStage::Dc6);
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
// A DC6 as an animation layer, laid out like decodeDcc's: directions in file order, each frame placed by its offset
// (the offset's y is the frame's bottom edge, as in a DCC). Some monsters' COF layers are DC6 files (Diablo, Mephisto,
// Tyrael, the Maggot Queen, Baal's clones and crabs).
Animation decodeDc6Animation(std::span<const uint8_t> b,const Palette& palette) {
    const auto count=dc6FrameCount(b);
    Animation result{u32(b,16),u32(b,20),{}};
    if(!result.directions||!result.framesPerDirection||result.directions>32)throw std::runtime_error("Invalid DC6 animation layout");
    result.frames.reserve(count);
    for(uint32_t f=0;f<count;++f) {
        auto image=decodeDc6(b,palette,f);
        const auto offset=dc6FrameOffset(b,f);
        const int top=offset[1]-int(image.height);
        result.frames.push_back(AnimationFrame{std::move(image),offset[0],top});
    }
    return result;
}
Archives::Archives(const std::filesystem::path& root):root_(std::filesystem::absolute(root).lexically_normal()) {
    const auto paths=archivePlan(root_);
    identity_=archivePlanIdentity(root_,paths);
    try {
        for(const auto& path:paths) {
            HANDLE archive=nullptr;
            if(!SFileOpenArchive(path.c_str(),0,MPQ_OPEN_READ_ONLY,&archive))
                throw std::runtime_error("Cannot open archive: "+path.string());
            handles_.push_back(archive);
        }
        validateCurrent();
        if(const auto mod=activeMod();!mod.empty())loose_=d2loose::Folder(root_/"Mods"/mod);
    } catch(...) { for(auto h:handles_) SFileCloseArchive(h); throw; }
}
Archives::~Archives() { for(auto h:handles_) SFileCloseArchive(h); }
void Archives::validateCurrent() const {
    if(identity_!=archivePlanIdentity(root_,archivePlan(root_)))
        throw std::runtime_error("Game archives changed after opening; restart the process");
}
// This thread's CPU time in ms (profiling: a slow read that used little CPU was waiting).
static long threadCpuMs() {
#ifdef __ANDROID__
    timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return long(t.tv_sec*1000+t.tv_nsec/1000000);
#else
    return long(std::clock()*1000/CLOCKS_PER_SEC);
#endif
}
Bytes Archives::read(const std::string& name,size_t maxSize) const {
    AssetTimer timing(AssetStage::ArchiveRead);
    const auto* loose=loose_.find(name);
    for(size_t i=loose?0:1;i<=handles_.size();++i) {
        HANDLE file=nullptr;
        if(i==0){if(!SFileOpenFileEx(nullptr,reinterpret_cast<const char*>(loose->u8string().c_str()),SFILE_OPEN_LOCAL_FILE,&file))continue;}
        else if(!SFileOpenFileEx(handles_[i-1],name.c_str(),SFILE_OPEN_FROM_MPQ,&file)) continue;
        struct Close { HANDLE file; ~Close(){SFileCloseFile(file);} } close{file};
        DWORD high=0;
        const DWORD size=SFileGetFileSize(file,&high);
        if(high || size==SFILE_INVALID_SIZE || size>maxSize) throw std::runtime_error("Asset exceeds size limit: "+name);
        Bytes bytes(size); DWORD read=0;
        const auto started=std::chrono::steady_clock::now();const auto cpuStarted=threadCpuMs();
        if(size && (!SFileReadFile(file,bytes.data(),size,&read,nullptr) || read!=size))
            throw std::runtime_error("Cannot read asset: "+name);
        // Profiling: a read long enough to drop frames, by name.
        if(assetTimingEnabled())if(const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();ms>=15)
            std::cout<<"Asset timing slow-read "<<ms<<" ms (cpu "<<threadCpuMs()-cpuStarted<<") "<<size<<" bytes "<<name<<std::endl;
        return bytes;
    }
    throw std::runtime_error("Asset not found in mounted MPQs: "+name);
}
Archives::Piecewise::~Piecewise() {if(file_)SFileCloseFile(file_);}
Bytes Archives::Piecewise::read(uint32_t offset,uint32_t length) {
    if(offset>=size_)return {};
    length=std::min(length,size_-offset);
    Bytes bytes(length);DWORD done=0;
    SFileSetFilePointer(file_,LONG(offset),nullptr,FILE_BEGIN);
    if(length&&(!SFileReadFile(file_,bytes.data(),length,&done,nullptr)||done!=length))bytes.resize(done);
    return bytes;
}
std::unique_ptr<Archives::Piecewise> Archives::open(const std::string& name) const {
    const auto* loose=loose_.find(name);
    for(size_t i=loose?0:1;i<=handles_.size();++i) {
        HANDLE file=nullptr;
        if(i==0){if(!SFileOpenFileEx(nullptr,reinterpret_cast<const char*>(loose->u8string().c_str()),SFILE_OPEN_LOCAL_FILE,&file))continue;}
        else if(!SFileOpenFileEx(handles_[i-1],name.c_str(),SFILE_OPEN_FROM_MPQ,&file))continue;
        DWORD high=0;const DWORD size=SFileGetFileSize(file,&high);
        if(high||size==SFILE_INVALID_SIZE){SFileCloseFile(file);continue;}
        auto piece=std::make_unique<Piecewise>();piece->file_=file;piece->size_=size;
        return piece;
    }
    return nullptr;
}
}
namespace d2 {
struct BackgroundReads::State {
    std::filesystem::path root;
    std::mutex lock;std::condition_variable wake;
    std::deque<std::pair<const void*,Work>> wanted;
    std::vector<std::pair<const void*,Done>> ready;
    std::atomic<size_t> readyCount{0};
    bool stop=false;
    const void* running=nullptr; // the owner of the job being done
    std::condition_variable finished;
    std::thread thread;
    void run() {
        std::unique_ptr<Archives> archives;
        for(;;) {
            std::pair<const void*,Work> job;
            {
                std::unique_lock<std::mutex> hold(lock);
                wake.wait(hold,[&]{return stop||!wanted.empty();});
                if(stop)return;
                job=std::move(wanted.front());wanted.pop_front();
                running=job.first;
            }
            Done done;
            try {if(!archives)archives=std::make_unique<Archives>(root);done=job.second(*archives);}
            catch(const std::exception&) {done=nullptr;}
            {
                std::lock_guard<std::mutex> hold(lock);
                ready.emplace_back(job.first,std::move(done));++readyCount;
                running=nullptr;
            }
            finished.notify_all();
        }
    }
};
std::shared_ptr<BackgroundReads> BackgroundReads::shared(const std::filesystem::path& root) {
    static std::weak_ptr<BackgroundReads> current;
    auto reads=current.lock();
    if(!reads){reads=std::make_shared<BackgroundReads>(root);current=reads;}
    return reads;
}
BackgroundReads::BackgroundReads(std::filesystem::path root):state_(std::make_unique<State>()) {
    state_->root=std::move(root);
    state_->thread=std::thread([s=state_.get()]{s->run();});
}
BackgroundReads::~BackgroundReads() {
    {std::lock_guard<std::mutex> hold(state_->lock);state_->stop=true;}
    state_->wake.notify_all();state_->thread.join();
}
void BackgroundReads::queue(const void* owner,Work work) {
    {std::lock_guard<std::mutex> hold(state_->lock);state_->wanted.emplace_back(owner,std::move(work));}
    state_->wake.notify_one();
}
void BackgroundReads::forget(const void* owner) {
    std::vector<std::pair<const void*,Done>> dropped; // destroyed outside the lock
    std::deque<std::pair<const void*,Work>> droppedWork;
    {
        std::unique_lock<std::mutex> hold(state_->lock);
        for(auto i=state_->wanted.begin();i!=state_->wanted.end();)
            if(i->first==owner){droppedWork.push_back(std::move(*i));i=state_->wanted.erase(i);}else ++i;
        state_->finished.wait(hold,[&]{return state_->running!=owner;});
        for(auto i=state_->ready.begin();i!=state_->ready.end();)
            if(i->first==owner){dropped.push_back(std::move(*i));i=state_->ready.erase(i);--state_->readyCount;}else ++i;
    }
}
void BackgroundReads::pump(const void* owner) {
    if(!state_->readyCount.load(std::memory_order_relaxed))return;
    std::vector<Done> mine;
    {
        std::lock_guard<std::mutex> hold(state_->lock);
        for(size_t i=0;i<state_->ready.size();)
            if(state_->ready[i].first==owner){mine.push_back(std::move(state_->ready[i].second));state_->ready.erase(state_->ready.begin()+long(i));--state_->readyCount;}
            else ++i;
    }
    for(auto& done:mine)if(done)done();
}
}
