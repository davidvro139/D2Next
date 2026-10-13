#pragma once
#include "Assets.h"
namespace d2 {
// Ground footprint of a placed tile in world subtile coordinates (a subtile index i spans i..i+1).
struct Footprint {float x0{},y0{},x1{},y1{};};
// Where a tile stands on the ground. Uses the tile's own solid collision subtiles when it has any;
// otherwise a rule by tile type (edge strip for walls, cell centre for trees and columns).
Footprint tileFootprint(const Tile& tile,uint32_t cellX,uint32_t cellY);
// True when the tile must be drawn after (on top of) a unit standing at the given continuous
// subtile position, false when it is drawn before (behind) the unit. Floors are always behind.
bool drawnAfterUnit(const Tile& tile,uint32_t cellX,uint32_t cellY,float unitX,float unitY);
}
