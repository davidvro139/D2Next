#pragma once
#include "GpuBackend.h"
#include <memory>
namespace d2 {
// The Direct3D 11 backend, drawing into a Win32 window (an HWND). forceSoftware uses WARP instead of the graphics card.
std::unique_ptr<GpuBackend> makeD3D11Backend(void* window,bool forceSoftware);
}
