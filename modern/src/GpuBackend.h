#pragma once
#include "DrawList.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
namespace d2 {
// What the renderer needs from a graphics API: frames the size of the window, draw lists (DrawList.h) drawn in order, and
// captures. Direct3D 11 implements it on Windows (D3D11Backend); OpenGL ES / WebGL can implement it elsewhere.
// Textures are made from the pictures the commands point at when first drawn and kept by the picture's address, so the
// renderer says when pictures go away (forgetTextures / forgetTexture).
class GpuBackend {
public:
    virtual ~GpuBackend()=default;
    // Starts a frame cleared to `clear` (r,g,b,a); false when there is nothing to draw into (the window is minimised).
    virtual bool begin(const std::array<float,4>& clear)=0;
    virtual uint32_t width() const=0;
    virtual uint32_t height() const=0;
    virtual void draw(const DrawList& commands)=0;
    virtual void present()=0;
    // Writes the frame drawn so far as a 32-bit BMP (when the path is not empty); returns how many pixels differ from the
    // top-left one.
    virtual size_t readback(const std::filesystem::path& file)=0;
    virtual void forgetTextures()=0;
    virtual void forgetTexture(const Image* image)=0;
    // Drawing without a graphics card (Direct3D's WARP), as tests may ask.
    virtual bool software() const=0;
    // What draws, for the window title ("D3D11 hardware", "OpenGL ES 3.0 ...").
    virtual std::string name() const=0;
};
}
