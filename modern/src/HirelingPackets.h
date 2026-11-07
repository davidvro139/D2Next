#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <utility>
namespace d2 {
struct HirelingOwnership {uint32_t type,classId,guid,name;};
class HirelingPackets {
    std::vector<std::vector<uint8_t>> held_;
    static uint32_t word(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
public:
    void clear(){held_.clear();}
    size_t pending() const{return held_.size();}
    template<class Apply>void receive(std::span<const uint8_t> packet,uint32_t hero,Apply apply) {
        if(packet.size()<20||packet[0]!=0x81)return;
        if(!hero){held_.emplace_back(packet.begin(),packet.end());return;}
        if(word(packet.data()+4)==hero)
            apply(HirelingOwnership{packet[1],uint32_t(packet[2])|(uint32_t(packet[3])<<8),word(packet.data()+8),uint32_t(packet[16])|(uint32_t(packet[17])<<8)});
    }
    template<class Apply>void ready(uint32_t hero,Apply apply) {
        if(!hero)return;
        auto packets=std::move(held_);held_.clear();
        for(const auto& packet:packets)receive(packet,hero,apply);
    }
};
}
