#pragma once
#include "Navigation.h"
#include <functional>
#include <vector>
namespace d2 {
struct NativeTown {
    MapScene scene;
    CollisionGrid collision;
    GridPoint worldOrigin;
    uint32_t seed{}, rooms{};
    int act=0; // zero-based act number of the loaded native levels
    std::vector<int> complete; // the levels whose surroundings are in the scene too (a player there sees no missing neighbour)
    std::vector<int> levels; // every level the scene covers (the requested ones and, with withNeighbours, the levels touching them)
    // The game's rooms in the scene, in scene subtiles, with their level: the automap reveals whole rooms, and draws each
    // with its own level's automap pieces.
    struct Room {int32_t x=0,y=0,width=0,height=0,level=0;};
    std::vector<Room> roomAreas;
};
// Experimental bridge to this repository's x64 DLL layouts. Returns owned data;
// all native resources are released before returning. The scene covers the given levels of one act (they must lie
// in one connected area, e.g. a town and the wilderness around it).
NativeTown loadNativeTown(const std::filesystem::path& dllDirectory,
    const std::filesystem::path& gameDirectory, const Archives& archives, uint32_t seed,
    const std::vector<int>& levelIds={1},bool withNeighbours=false,
    const std::function<void()>& progress={});
}
