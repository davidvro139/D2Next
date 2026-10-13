#include "Simulation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace d2 {
namespace {
int64_t isqrt(uint64_t v) {
    uint64_t r=uint64_t(std::sqrt(double(v)));
    while(r*r>v)--r;
    while((r+1)*(r+1)<=v)++r;
    return int64_t(r);
}
uint64_t mix(uint64_t h,uint64_t v) {return (h^v)*1099511628211ull;}
}
FixedPoint subtileCenter(GridPoint p) {return {p.x*fixedOne+fixedOne/2,p.y*fixedOne+fixedOne/2};}
GridPoint subtileOf(FixedPoint p) {
    // Arithmetic shift floors negative values too.
    return {p.x>>16,p.y>>16};
}
unsigned facingFor(int dx,int dy) {
    // Rows are dy -1..1, columns dx -1..1. Derived from atan2(dy,dx) rotated for the
    // isometric grid, the same mapping the previous marker prototype used.
    static constexpr unsigned table[3][3]={{8,10,12},{6,0,14},{4,2,0}};
    dx=std::clamp(dx,-1,1);dy=std::clamp(dy,-1,1);
    if(!dx&&!dy)return 0;
    return table[dy+1][dx+1];
}
World::World(const CollisionGrid& grid,GridPoint spawn):grid_(&grid) {
    if(!grid.walkable(spawn))throw std::runtime_error("Spawn subtile is not walkable");
    player_.position=player_.previous=subtileCenter(spawn);
}
bool World::apply(const Command& c) {
    switch(c.type) {
    case Command::Type::Hold:
        player_.held={std::clamp(c.x,-1,1),std::clamp(c.y,-1,1)};
        return true;
    case Command::Type::Stop:
        player_.held={};
        // Finish the current segment so the player always rests on a subtile centre.
        if(player_.routeIndex<player_.route.size())player_.route.resize(player_.routeIndex+1);
        return true;
    case Command::Type::MoveTo: {
        const GridPoint destination{c.x,c.y};
        const bool active=player_.routeIndex<player_.route.size();
        const GridPoint anchor=active?player_.route[player_.routeIndex]:subtileOf(player_.position);
        if(!grid_->walkable(destination))return false;
        std::vector<GridPoint> path;
        if(!(destination==anchor)) {
            path=findPath(*grid_,anchor,destination);
            if(path.empty())return false;
        }
        player_.held={};
        if(active) {
            // Commit to the segment already under way, then follow the new route.
            std::vector<GridPoint> route{anchor};
            route.insert(route.end(),path.begin(),path.end());
            player_.route=std::move(route);
            player_.routeIndex=0;
        } else {
            player_.route=std::move(path);
            player_.routeIndex=0;
            player_.segmentActive=false;
        }
        return true;
    }
    }
    return false;
}
void World::refillFromHeld() {
    player_.route.clear();player_.routeIndex=0;player_.segmentActive=false;
    if(!player_.held.x&&!player_.held.y)return;
    const auto here=subtileOf(player_.position);
    const GridPoint next{here.x+player_.held.x,here.y+player_.held.y};
    if(grid_->canStep(here,next))player_.route={next};
}
void World::tick() {
    ++ticks_;
    player_.previous=player_.position;
    int64_t budget=walkSpeed;
    bool moved=false;
    while(budget>0) {
        if(player_.routeIndex>=player_.route.size()) {
            refillFromHeld();
            if(player_.route.empty())break;
        }
        const auto target=subtileCenter(player_.route[player_.routeIndex]);
        if(!player_.segmentActive) {
            const auto here=subtileOf(player_.position);
            const auto& t=player_.route[player_.routeIndex];
            player_.facing=uint8_t(facingFor(t.x-here.x,t.y-here.y));
            player_.segmentActive=true;
        }
        const int64_t dx=int64_t(target.x)-player_.position.x,dy=int64_t(target.y)-player_.position.y;
        const int64_t distance=isqrt(uint64_t(dx*dx+dy*dy));
        moved=true;
        if(distance<=budget) {
            player_.position=target;budget-=distance;
            ++player_.routeIndex;player_.segmentActive=false;
        } else {
            player_.position.x+=int32_t(dx*budget/distance);
            player_.position.y+=int32_t(dy*budget/distance);
            budget=0;
        }
    }
    if(player_.routeIndex>=player_.route.size()) {player_.route.clear();player_.routeIndex=0;}
    if(moved!=player_.moving) {player_.moving=moved;player_.stateTicks=0;}
    else ++player_.stateTicks;
}
uint64_t World::hash() const {
    uint64_t h=14695981039346656037ull;
    h=mix(h,ticks_);
    h=mix(h,uint32_t(player_.position.x));h=mix(h,uint32_t(player_.position.y));
    h=mix(h,player_.facing);h=mix(h,player_.moving);h=mix(h,player_.stateTicks);
    h=mix(h,uint32_t(player_.held.x));h=mix(h,uint32_t(player_.held.y));
    h=mix(h,player_.routeIndex);h=mix(h,player_.segmentActive);
    for(const auto& p:player_.route){h=mix(h,uint32_t(p.x));h=mix(h,uint32_t(p.y));}
    return h;
}
std::array<float,2> renderPosition(const PlayerState& p,float alpha) {
    return interpolatePosition(p.previous,p.position,alpha);
}
std::array<float,2> interpolatePosition(FixedPoint previous,FixedPoint current,float alpha) {
    alpha=std::clamp(alpha,0.0f,1.0f);
    auto lerp=[&](int32_t a,int32_t b){return (float(a)+(float(b)-float(a))*alpha)/float(fixedOne);};
    return {lerp(previous.x,current.x),lerp(previous.y,current.y)};
}
}
