#include "Renderer.h"
#include <set>
#include <iostream>
#include <stdexcept>
struct FakeGpu final: d2::GpuBackend {
    std::set<const d2::Image*> textures;
    int fullClears=0;
    bool begin(const std::array<float,4>&) override{return true;}
    uint32_t width()const override{return 800;}
    uint32_t height()const override{return 600;}
    void draw(const d2::DrawList&)override{}
    void present()override{}
    size_t readback(const std::filesystem::path&)override{return 0;}
    void forgetTextures()override{textures.clear();++fullClears;}
    void forgetTexture(const d2::Image* p)override{textures.erase(p);}
    bool software()const override{return true;}
    std::string name()const override{return "lifetime fixture";}
};
int main(){try{
    auto owned=std::make_unique<FakeGpu>();auto& gpu=*owned;
    Renderer renderer(std::move(owned));
    d2::MapScene oldMap,newMap;oldMap.tiles.resize(3);newMap.tiles.resize(1);
    d2::Image ui{1,1,{1,2,3,255}};
    d2::Animation unit;unit.frames.resize(1);unit.frames[0].image=ui;unit.frames[0].shadow=1;
    auto require=[](bool value){if(!value)throw std::runtime_error("renderer lifetime regression");};
    const auto* uiDim=&renderer.dimmedImage(ui);
    const auto* tileDim=&renderer.dimmedImage(oldMap.tiles[1].image);
    const auto* shadow=renderer.shadowOf(unit.frames[0]);require(shadow);
    gpu.textures={&ui,uiDim,&unit.frames[0].image,&shadow->image,tileDim};
    for(const auto& tile:oldMap.tiles)gpu.textures.insert(&tile.image);
    renderer.forgetScene(oldMap);
    for(const auto& tile:oldMap.tiles)require(!gpu.textures.count(&tile.image));
    require(!gpu.textures.count(tileDim)&&renderer.dimImages.size()==1);
    renderer.upload(newMap,true);
    require(gpu.fullClears==0&&gpu.textures.size()==4);
    require(gpu.textures.count(&ui)&&gpu.textures.count(uiDim)&&gpu.textures.count(&shadow->image));
    renderer.forgetAnimation(unit);
    require(gpu.textures.size()==2&&renderer.shadowImages.empty());
    auto tint=std::make_unique<d2::Animation>();tint->frames.resize(1);tint->frames[0].image=ui;tint->frames[0].shadow=1;
    const auto* tintedImage=&tint->frames[0].image;
    const auto* tintedDim=&renderer.dimmedImage(*tintedImage);
    const auto* tintedShadow=&renderer.shadowOf(tint->frames[0])->image;
    gpu.textures.insert(tintedImage);gpu.textures.insert(tintedDim);gpu.textures.insert(tintedShadow);
    renderer.playerTints.emplace(std::pair<uint8_t,uint8_t>{1,2},std::move(tint));
    renderer.clearMovement();
    require(renderer.playerTints.empty()&&renderer.shadowImages.empty()&&renderer.dimImages.size()==1);
    require(gpu.textures.size()==2&&!gpu.textures.count(tintedImage)&&!gpu.textures.count(tintedDim)&&!gpu.textures.count(tintedShadow));
    renderer.upload(oldMap); // initial/new-game path must still invalidate everything
    require(gpu.fullClears==1&&gpu.textures.empty()&&renderer.dimImages.empty());
    d2::Palette reusedPalette{};
    renderer.chargeSteps=5;renderer.chargeTick=99;renderer.otherChargeSteps[7]={3,99};
    renderer.weatherPalette=&reusedPalette;renderer.nearestWeatherColors[123]={1,2,3};
    renderer.resetSessionHistory();
    require(renderer.chargeSteps==0&&renderer.chargeTick==0&&renderer.otherChargeSteps.empty());
    require(!renderer.weatherPalette&&renderer.nearestWeatherColors.empty());
    std::cout<<"PASS scene textures/derived images released, unit/UI/shadows retained, animation eviction and full reset\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
