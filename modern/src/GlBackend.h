#pragma once
#include "GpuBackend.h"
#include <memory>
namespace d2 {
// The OpenGL ES 3.0 backend, drawing into an SDL window (an SDL_Window*, made with SDL_WINDOW_OPENGL). The same code runs
// on Android (OpenGL ES 3), in browsers (WebGL 2, through Emscripten) and on desktops (OpenGL ES where the driver offers
// it, otherwise OpenGL 3.3 core).
std::unique_ptr<GpuBackend> makeGlBackend(void* sdlWindow);
}
