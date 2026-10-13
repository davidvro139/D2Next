#pragma once
#include "Assets.h"
#include <array>
#include <string>
namespace d2 {
// Text in the system's font, for the moments before the game's own fonts are loaded: white glyphs coverage as alpha, in
// the given colour. Each platform provides it (PlatformWin32.cpp: GDI, Segoe UI).
Image systemTextImage(const std::string& text,std::array<uint8_t,3> color);
}
