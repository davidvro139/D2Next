#pragma once
#include <cstdint>
namespace d2 {
enum class HeroCry {None,Hit,Death};
class HeroCryState {
    uint8_t previous_=255;
public:
    HeroCry observe(uint8_t mode,bool validHero) {
        if(!validHero){previous_=255;return HeroCry::None;}
        HeroCry result=HeroCry::None;
        if(mode!=previous_) {
            if(mode==4)result=HeroCry::Hit;
            else if(mode==0&&previous_!=255)result=HeroCry::Death;
        }
        previous_=mode;
        return result;
    }
};
}
