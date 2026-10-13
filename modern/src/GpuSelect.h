#pragma once
#include "Platform.h"
#ifdef D2MODERN_GL
#include "GlBackend.h"
#else
#include "D3D11Backend.h"
#endif
namespace d2 {
// The graphics backend of the build: OpenGL ES / WebGL (D2MODERN_RENDERER=GL, with the SDL window) or Direct3D 11.
inline std::unique_ptr<GpuBackend> makeGpuBackend(Window& window,bool forceSoftware) {
#ifdef D2MODERN_GL
    (void)forceSoftware;
    return makeGlBackend(window.nativeHandle());
#else
    return makeD3D11Backend(window.nativeHandle(),forceSoftware);
#endif
}
}
