#pragma once
#include "Navigation.h"
#include <cstdint>
namespace d2 {
// Shared movement types have no graphics or Windows dependencies. Positions are 16.16
// fixed point in subtile units (a subtile centre is index + 0.5), so replays of
// the same command list produce bit-identical state on every machine.
inline constexpr uint32_t simulationHz=25;
inline constexpr int32_t fixedOne=1<<16;
// About 6 subtiles per second (original town walk speed is not yet verified).
inline constexpr int32_t walkSpeed=(6*fixedOne+int32_t(simulationHz)/2)/int32_t(simulationHz);
struct FixedPoint {int32_t x{},y{};bool operator==(const FixedPoint&) const=default;};
FixedPoint subtileCenter(GridPoint p);
GridPoint subtileOf(FixedPoint p);
// Facing 0..15 for a step between neighbouring subtiles; matches the sprite direction order.
unsigned facingFor(int dx,int dy);
struct Command {
    enum class Type:uint8_t{MoveTo,Hold,Stop,Attack,Respawn,Pickup,TakeItem,PlaceItem,EquipItem,SwapBody,TakeBody,UseItem,UseBelt,DropItem,Interact,Raw,CastAt,CastOn,SetRun,InteractObject,DropGold,Cheat};
    Type type{Type::Stop};
    int32_t x{},y{}; // MoveTo: destination subtile. Hold: direction, each component clamped to -1..1.
    int32_t z{},w{},v{}; // inventory and raw packet commands only
    static Command moveTo(GridPoint p){return {Type::MoveTo,p.x,p.y};}
    static Command hold(int dx,int dy){return {Type::Hold,dx,dy};}
    static Command stop(){return {Type::Stop,0,0};}
    static Command setRun(bool enabled){return {Type::SetRun,enabled?1:0,0};}
    // Attack: x holds the target's native unit number (entity id minus 1000).
    static Command attack(int32_t unit){return {Type::Attack,unit,0};}
    static Command pickup(int32_t item){return {Type::Pickup,item,0};} // item = native item number
    static Command respawn(){return {Type::Respawn,0,0};} // after death: come back to life in town
    // Inventory (native item numbers): grid item to cursor, cursor to a grid cell (page 0 inventory, 4 stash, 3 cube), cursor to an empty body slot,
    // cursor swapped with the equipped item, equipped item to cursor (body locations 1 head .. 10 gloves), use a grid item, use a belt item.
    static Command takeItem(int32_t item){return {Type::TakeItem,item,0};}
    static Command placeItem(int32_t item,int32_t cellX,int32_t cellY,int32_t page){return {Type::PlaceItem,item,cellX,cellY,page};}
    static Command equipItem(int32_t item,int32_t bodyLoc){return {Type::EquipItem,item,bodyLoc};}
    static Command swapBody(int32_t item,int32_t bodyLoc){return {Type::SwapBody,item,bodyLoc};}
    static Command takeBody(int32_t bodyLoc){return {Type::TakeBody,bodyLoc,0};}
    static Command useItem(int32_t item){return {Type::UseItem,item,0};}
    static Command useBelt(int32_t item){return {Type::UseBelt,item,0};}
    static Command dropItem(int32_t item){return {Type::DropItem,item,0};} // cursor item to the ground
    static Command dropGold(int32_t amount){return {Type::DropGold,amount,0};} // that much of the carried gold to the ground
    // Host-only save edits from the Cheats menu. x = kind: 0 add/fill gold (y amount, 0 fills), 1 stash gold,
    // 2 fill life and mana, 3 give an item (y packed items.txt code, z quality), 4 max class skills, 5 level up by 1.
    static Command cheat(int32_t kind,int32_t a=0,int32_t b=0){return {Type::Cheat,kind,a,b};}
    static Command interact(int32_t monster){return {Type::Interact,monster,0};} // talk to a town person (native unit number)
    static Command interactObject(int32_t object){return {Type::InteractObject,object,0};} // open/use a game object (native object number)
    // The selected skill (right: the right button skill) at a subtile (x,y), or on a native unit (x = unit number).
    static Command castAt(bool right,GridPoint p){return {Type::CastAt,p.x,p.y,right?1:0};}
    static Command castOn(bool right,int32_t unit){return {Type::CastOn,unit,0,right?1:0};}
    // A client packet as is: x = header, size shifted by 8, y..w = the body words (see native::Request).
    static Command raw(int32_t headerAndSize,int32_t a,int32_t b,int32_t c,int32_t d){return {Type::Raw,headerAndSize,a,b,c,d};}
};
std::array<float,2> interpolatePosition(FixedPoint previous, FixedPoint current, float alpha);
}
