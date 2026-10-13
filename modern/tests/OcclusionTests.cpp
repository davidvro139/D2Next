#include "Occlusion.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* what){if(!b)throw std::runtime_error(std::string("Occlusion test failed: ")+what);}
static bool same(const d2::Footprint& f,float x0,float y0,float x1,float y1){
    return std::abs(f.x0-x0)<1e-4f&&std::abs(f.y0-y0)<1e-4f&&std::abs(f.x1-x1)<1e-4f&&std::abs(f.y1-y1)<1e-4f;
}
// Sets the solid flag at world row y, column x within the cell (rows are stored bottom to top).
static void solid(d2::Tile& t,int x,int y){t.collision[size_t(4-y)*5+x]=d2::collideSolid;}
int main(){try{
    // A right wall solid along the upper-right edge (rows 0..1) stands on Y 110..112 in cell (28,22).
    d2::Tile fence;fence.orientation=2;
    for(int x=0;x<5;++x){solid(fence,x,0);solid(fence,x,1);}
    require(same(d2::tileFootprint(fence,28,22),140,110,145,112),"fence footprint comes from solid flags");
    // Behind the fence (smaller y): the fence covers the unit. In front (larger y): the unit covers it.
    require(d2::drawnAfterUnit(fence,28,22,142,100),"unit behind fence is covered");
    require(d2::drawnAfterUnit(fence,28,22,142,109.9f),"unit right behind fence is covered");
    require(!d2::drawnAfterUnit(fence,28,22,142,112.1f),"unit right in front of fence covers it");
    require(!d2::drawnAfterUnit(fence,28,22,142,130),"unit far in front covers it");
    // Beside the fence on the same row it falls back to the depth of the footprint centre.
    require(d2::drawnAfterUnit(fence,28,22,120,120),"unit left and in front, but shallower: covered");
    require(!d2::drawnAfterUnit(fence,28,22,160,112),"unit to the right and deeper covers it");

    // A left wall along the upper-left edge: the unit is behind it only when at smaller x.
    d2::Tile left;left.orientation=1;for(int y=0;y<5;++y)solid(left,0,y);
    require(same(d2::tileFootprint(left,10,4),50,20,51,25),"left wall footprint");
    require(d2::drawnAfterUnit(left,10,4,49,22),"unit behind left wall is covered");
    require(!d2::drawnAfterUnit(left,10,4,52,22),"unit in front of left wall covers it");

    // Tiles without solid subtiles use a footprint rule by type.
    d2::Tile tree;tree.orientation=14;
    require(same(d2::tileFootprint(tree,3,3),17,17,18,18),"tree trunk stands at the cell centre");
    require(d2::drawnAfterUnit(tree,3,3,16,16),"unit behind tree is covered by the canopy");
    require(!d2::drawnAfterUnit(tree,3,3,19,19),"unit in front of tree covers it");
    d2::Tile edge;edge.orientation=6;
    require(same(d2::tileFootprint(edge,0,0),0,4,5,5),"bottom-left wall on the lower-left edge");
    d2::Tile unknown;unknown.orientation=99;
    require(same(d2::tileFootprint(unknown,1,2),5,10,10,15),"unknown types cover the whole cell");

    // Floors are always behind the unit, wherever it stands.
    d2::Tile floor;
    for(float p:{-50.f,0.f,20.f,500.f})require(!d2::drawnAfterUnit(floor,3,3,p,p),"floors are always behind");
    // Roofs follow the same depth rule as walls: a unit in front of a building is drawn over its
    // roof (the tent case), and a unit behind it is covered.
    d2::Tile roof;roof.orientation=15;
    require(same(d2::tileFootprint(roof,10,10),50,50,55,55),"roof covers its whole cell");
    require(!d2::drawnAfterUnit(roof,10,10,52,58),"unit in front of a roof covers it");
    require(!d2::drawnAfterUnit(roof,10,10,58,52),"unit to the front-right of a roof covers it");
    require(d2::drawnAfterUnit(roof,10,10,52,48),"unit behind a roof is covered");

    // The rule gives a consistent order along a line of walking: once past a tile, always past it.
    for(const d2::Tile* t:{&fence,&left,&tree}){
        bool wasBehind=true;
        for(float step=0;step<80;step+=.25f){
            const bool after=d2::drawnAfterUnit(*t,28,22,142-40+step,100+step*.5f);
            if(!wasBehind)require(!after,"no flip back to covered after passing a tile");
            wasBehind=after;
        }
    }
    std::cout<<"PASS: footprints from solid flags and type rules, unit behind/in front of walls and trees, roofs and floors, monotonic ordering\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
