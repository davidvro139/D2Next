#pragma once
#include "Assets.h"
#include <map>
#include <memory>
#include <tuple>
namespace d2 {
// The game's own interface pictures (data\global\ui\...\*.dc6), decoded once with the act 1 palette.
class UIAssets {
public:
    UIAssets(const Archives& archives,const Palette& palette):archives_(archives),palette_(palette) {}
    // Frame `index` of a picture, e.g. frame("panel\\800ctrlpnl7",0), or of one elsewhere by its whole path
    // ("data\\local\\ui\\eng\\youdiedinst"). Null when missing; pointers stay valid.
    const Image* frame(const std::string& file,uint32_t index);
    // The frame with only its bottom `fraction` of rows kept: a globe filled to that level.
    const Image* filled(const std::string& file,uint32_t index,float fraction);
    // Where the frame is drawn from its anchor: x of its left edge, y of its bottom row (as dc6FrameOffset).
    std::array<int32_t,2> offset(const std::string& file,uint32_t index);
private:
    const Archives& archives_;
    Palette palette_;
    std::map<std::string,Bytes> files_;
    std::map<std::pair<std::string,uint32_t>,std::unique_ptr<Image>> frames_;
    std::map<std::tuple<std::string,uint32_t,uint32_t>,std::unique_ptr<Image>> filled_;
};
}
