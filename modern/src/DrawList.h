#pragma once
#include "Assets.h"
#include <array>
#include <cstdint>
#include <vector>
namespace d2 {
// One picture to draw, as every renderer (Direct3D now; OpenGL ES / WebGL later) draws it: a picture (image) or a plain
// colour (image null) stretched over a quad, lit and blended. Commands are drawn in order.
//   quad: the quad in clip space, the window spanning -1..1 with y up: its half width and half height, then its centre.
//   light: the light (r,g,b) the picture's colours are multiplied by at its corners, top left, top right, bottom left,
//     bottom right, blended across it.
//   blend: Normal, the picture's opaque pixels replace what is below; Add, added onto it (glows, fire); Mix, blended in at
//     `amount` opacity; Darken, what is below is halved where the picture is opaque; Shadow, the same but only over pixels
//     drawn earlier in the frame (shadows fall on the ground, not into the void).
// Pixels whose alpha is below one half are never drawn.
struct DrawCommand {
    enum class Blend:uint8_t {Normal,Add,Mix,Darken,Shadow,Scale}; // Scale: what is below times twice the colour (grey keeps it)
    const Image* image=nullptr;
    std::array<uint8_t,3> color{};
    std::array<float,4> quad{};
    std::array<float,12> light{1,1,1,1,1,1,1,1,1,1,1,1};
    Blend blend=Blend::Normal;
    float amount=1;
};
using DrawList=std::vector<DrawCommand>;
}
