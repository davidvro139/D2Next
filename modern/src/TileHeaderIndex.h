#pragma once
#include "Assets.h"
#include <map>
#include <cstring>
#include <stdexcept>
namespace d2 {
class TileHeaderIndex {
    using Header=std::array<uint8_t,96>;
    std::map<Header,uint32_t> entries_;
public:
    explicit TileHeaderIndex(std::span<const uint8_t> bytes) {
        const auto count=dt1TileCount(bytes);
        if(bytes.size()<276)throw std::runtime_error("Truncated DT1 header offset");
        const size_t start=uint32_t(bytes[272])|(uint32_t(bytes[273])<<8)|(uint32_t(bytes[274])<<16)|(uint32_t(bytes[275])<<24);
        if(start>bytes.size()||count>(bytes.size()-start)/96)throw std::runtime_error("Truncated DT1 tile headers");
        for(uint32_t index=0;index<count;++index) {
            Header key{};std::memcpy(key.data(),bytes.data()+start+size_t(index)*96,96);
            entries_.emplace(key,index); // the old linear scan picks the first duplicate
        }
    }
    uint32_t find(const void* header) const {
        Header key{};std::memcpy(key.data(),header,96);
        const auto found=entries_.find(key);
        if(found==entries_.end())throw std::runtime_error("Native tile header does not match MPQ");
        return found->second;
    }
};
}
