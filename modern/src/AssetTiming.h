#pragma once
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <ostream>
#if defined(__ANDROID__)
#include <malloc.h>
#endif
namespace d2 {
// Opt-in CPU wall times on the calling thread. Nested categories overlap;
// GL measures API submission, not asynchronous GPU completion. No GPU fences.
enum class AssetStage {ArchiveRead,Ds1,Dt1,Dcc,Dc6,Scene,Tables,Act,Level,Rooms,GlUpload,GlDelete,DimRelease,ShadowRelease,SceneRelease,Collision,TilePlace,Count};
struct AssetTime {uint64_t calls=0,nanos=0,maxNanos=0;};
inline bool assetTimingEnabled() {
    static const bool enabled=[] {const char* value=std::getenv("D2NEXT_ASSET_TIMING");return value&&value[0]=='1';}();
    return enabled;
}
// Profiling: the native heap in use (MiB), logged at stages of starting a game to see what fills it (Android only).
inline void logHeap(const char* stage) {
#if defined(__ANDROID__)
    if(!assetTimingEnabled())return;
    const struct mallinfo info=mallinfo();
    std::cout<<"Asset timing heap "<<(size_t(info.uordblks)>>20)<<" MiB in use after "<<stage<<std::endl;
#else
    (void)stage;
#endif
}
inline auto& assetTimes() {
    static thread_local std::array<AssetTime,size_t(AssetStage::Count)> values{};
    return values;
}
class AssetTimer {
    AssetStage stage_;bool enabled_;std::chrono::steady_clock::time_point start_{};
public:
    explicit AssetTimer(AssetStage stage):stage_(stage),enabled_(assetTimingEnabled()) {
        if(enabled_)start_=std::chrono::steady_clock::now();
    }
    ~AssetTimer() {
        if(!enabled_)return;
        const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start_).count());
        auto& value=assetTimes()[size_t(stage_)];++value.calls;value.nanos+=elapsed;
        if(elapsed>value.maxNanos)value.maxNanos=elapsed;
    }
    AssetTimer(const AssetTimer&)=delete;
    AssetTimer& operator=(const AssetTimer&)=delete;
};
inline void reportAssetTimes(std::ostream& out,const char* context) {
    if(!assetTimingEnabled())return;
    constexpr const char* names[]={"archive-read","ds1-decode","dt1-decode","dcc-decode","dc6-decode","scene-build","game-tables","act-generation","level-generation","room-streaming","gl-upload-submit","gl-delete-textures","dim-release","shadow-release","scene-image-release","collision-grid","tile-placement"};
    static_assert(std::size(names)==size_t(AssetStage::Count));
    auto& values=assetTimes();
    for(size_t i=0;i<values.size();++i)if(values[i].calls)
        out<<"Asset timing "<<context<<" "<<names[i]<<" calls="<<values[i].calls<<" total_us="<<values[i].nanos/1000<<" max_us="<<values[i].maxNanos/1000<<'\n';
    out.flush();values={};
}
}
