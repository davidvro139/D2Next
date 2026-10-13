#pragma once
#include "Assets.h"
namespace d2 {
struct GridPoint {int x{},y{};bool operator==(const GridPoint&) const=default;};
struct CollisionGrid {
    uint32_t width{},height{};
    Bytes flags,ground;
    bool walkable(GridPoint p) const;
    bool canStep(GridPoint from,GridPoint to) const;
};
CollisionGrid buildCollision(const MapScene& scene);
GridPoint findSpawn(const CollisionGrid& grid);
std::vector<GridPoint> findPath(const CollisionGrid& grid,GridPoint from,GridPoint to);
std::array<float,2> projectSubtile(const MapScene& scene,GridPoint p);
// Continuous subtile coordinates (a subtile centre is index + 0.5) to scene pixels.
std::array<float,2> projectPoint(const MapScene& scene,float x,float y);
GridPoint pickSubtile(const MapScene& scene,float x,float y);
}
