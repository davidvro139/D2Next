#include "UIAssets.h"
#include <algorithm>
#include <cmath>
namespace d2 {
const Image* UIAssets::frame(const std::string& file,uint32_t index) {
    const auto key=std::make_pair(file,index);
    if(auto found=frames_.find(key);found!=frames_.end())return found->second.get();
    std::unique_ptr<Image> image;
    try {
        auto bytes=files_.find(file);
        // A name under data\global\ui, or a whole archive path (the language's pictures are in data\local\ui\eng).
        if(bytes==files_.end())bytes=files_.emplace(file,archives_.read((file.rfind("data\\",0)==0?file:"data\\global\\ui\\"+file)+".dc6")).first;
        image=std::make_unique<Image>(decodeDc6(bytes->second,palette_,index));
    } catch(const std::exception&) {}
    return frames_.emplace(key,std::move(image)).first->second.get();
}
std::array<int32_t,2> UIAssets::offset(const std::string& file,uint32_t index) {
    if(!frame(file,index))return {0,0};
    return dc6FrameOffset(files_.at(file),index);
}
const Image* UIAssets::filled(const std::string& file,uint32_t index,float fraction) {
    const auto* full=frame(file,index);
    if(!full)return nullptr;
    const uint32_t rows=uint32_t(std::lround(std::clamp(fraction,0.f,1.f)*float(full->height)));
    const auto key=std::make_tuple(file,index,rows);
    if(auto found=filled_.find(key);found!=filled_.end())return found->second.get();
    auto image=std::make_unique<Image>(*full);
    for(uint32_t y=0;y<full->height-rows;++y)std::fill_n(image->rgba.begin()+size_t(y)*full->width*4,size_t(full->width)*4,uint8_t(0));
    return filled_.emplace(key,std::move(image)).first->second.get();
}
}
