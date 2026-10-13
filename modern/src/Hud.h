#pragma once
#include "Assets.h"
#include <array>
#include <cstdint>
#include <vector>
namespace d2 {
// One piece of the interface to draw, in window pixels: a picture (image) or a plain colour (image null), drawn in list
// order over what is already there. Normal: the picture's opaque pixels replace what is below. Mix: blended in at
// `amount` opacity. Darken: what is below is halved (the game menu's backdrop).
// The interface code only builds these lists; each platform's renderer draws them.
struct HudSprite {
    enum class Blend:uint8_t {Normal,Mix,Darken,Scale};
    const Image* image=nullptr;
    std::array<uint8_t,3> color{};
    float x=0,y=0,w=0,h=0;
    Blend blend=Blend::Normal;
    float amount=1;
};
// What the interface code hands to its drawing helper: a picture or a plain colour.
struct HudPaint {
    const Image* image=nullptr;
    std::array<uint8_t,3> color{};
    HudPaint(const Image* picture):image(picture) {}
    HudPaint(std::array<uint8_t,3> plain):color(plain) {}
};
}
