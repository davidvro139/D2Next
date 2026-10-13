#pragma once
#include "Movement.h"
#include <array>
#include <cstdint>
#include <vector>
namespace d2 {
struct PlayerState {
    FixedPoint position{},previous{};   // previous = position at the start of the last tick
    std::vector<GridPoint> route;       // waypoints; route[routeIndex] is the current target
    size_t routeIndex=0;
    bool segmentActive=false;           // facing has been chosen for the current target
    GridPoint held{};                   // direction held by the input layer
    uint8_t facing=0;
    bool moving=false;
    uint32_t stateTicks=0;              // ticks since moving/idle last changed; drives animation frames
};
class World {
public:
    World(const CollisionGrid& grid,GridPoint spawn);
    // Returns false when the command is rejected (unreachable, blocked, or outside the map).
    bool apply(const Command& command);
    void tick();
    const PlayerState& player() const {return player_;}
    uint64_t tickCount() const {return ticks_;}
    uint64_t hash() const;
private:
    void refillFromHeld();
    const CollisionGrid* grid_;
    PlayerState player_;
    uint64_t ticks_=0;
};
// Continuous subtile coordinates (centre = index + 0.5) between the previous and current tick.
std::array<float,2> renderPosition(const PlayerState& player,float alpha);
}
