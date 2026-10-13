#include "Occlusion.h"
#include <algorithm>
namespace d2 {
namespace {
constexpr uint32_t floorType=0;
// Cell-relative box (subtiles, 0..5) for tiles that have no solid collision subtiles.
Footprint fallbackBox(uint32_t orientation) {
    switch(orientation) {
    case 1: case 8: case 10: case 16: return {0,0,1,5};   // left wall: upper-left edge
    case 2: case 9: case 11: case 17: return {0,0,5,1};   // right wall: upper-right edge
    case 3: case 4: return {0,0,2,2};                     // north corner
    case 5: return {0,0,5,1};                             // top-right wall
    case 6: return {0,4,5,5};                             // bottom-left wall: lower-left edge
    case 7: return {4,0,5,5};                             // bottom-right wall: lower-right edge
    case 12: case 14: return {2,2,3,3};                   // column, tree trunk: cell centre
    default: return {0,0,5,5};                            // whole cell
    }
}
}
Footprint tileFootprint(const Tile& tile,uint32_t cellX,uint32_t cellY) {
    int x0=5,y0=5,x1=-1,y1=-1;
    for(int y=0;y<5;++y)for(int x=0;x<5;++x) {
        // DT1 flag rows are stored bottom to top, the same mapping buildCollision uses.
        if(tile.collision[size_t(4-y)*5+x]&collideSolid){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
    }
    const Footprint local=x1<0?fallbackBox(tile.orientation):Footprint{float(x0),float(y0),float(x1+1),float(y1+1)};
    const float bx=float(cellX)*5,by=float(cellY)*5;
    return {bx+local.x0,by+local.y0,bx+local.x1,by+local.y1};
}
bool drawnAfterUnit(const Tile& tile,uint32_t cellX,uint32_t cellY,float ux,float uy) {
    if(tile.orientation==floorType)return false;
    // Roofs need the same depth test as walls: a unit standing in front of a building is drawn over
    // its roof art, and one inside is covered by the roof pieces in front of it (RenderWorld.cpp).
    const auto f=tileFootprint(tile,cellX,cellY);
    // Classic isometric ordering for axis-aligned footprints: the unit is behind the tile when it
    // lies before the footprint along x or y, and in front when it lies past it.
    const bool before=ux<=f.x0||uy<=f.y0,past=ux>=f.x1||uy>=f.y1;
    if(before&&!past)return true;
    if(past&&!before)return false;
    // Side by side on the same row, or overlapping: order by depth of the footprint centre.
    return ux+uy<(f.x0+f.x1+f.y0+f.y1)*.5f;
}
}
