#include "Navigation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace d2 {
bool CollisionGrid::walkable(GridPoint p) const {
    if(p.x<0||p.y<0||p.x>=int(width)||p.y>=int(height))return false;
    const size_t i=size_t(p.y)*width+p.x;
    return ground.at(i)&&!(flags.at(i)&collideSolid);
}
bool CollisionGrid::canStep(GridPoint a,GridPoint b) const {
    const int dx=b.x-a.x,dy=b.y-a.y;
    return std::abs(dx)<=1&&std::abs(dy)<=1&&(dx||dy)&&walkable(a)&&walkable(b)
        &&(!dx||!dy||(walkable({a.x+dx,a.y})&&walkable({a.x,a.y+dy})));
}
CollisionGrid buildCollision(const MapScene& scene) {
    if(!scene.cellsX||!scene.cellsY||scene.cellsX>128||scene.cellsY>128)throw std::runtime_error("Invalid collision grid dimensions");
    CollisionGrid g;g.width=scene.cellsX*5;g.height=scene.cellsY*5;
    g.flags.resize(size_t(g.width)*g.height);g.ground.resize(g.flags.size());
    for(const auto& p:scene.placements) {
        if(p.shadow)continue;
        if(p.cellX>=scene.cellsX||p.cellY>=scene.cellsY)throw std::runtime_error("Collision placement outside grid");
        const auto& tile=scene.tiles.at(p.tile);
        for(uint32_t y=0;y<5;++y)for(uint32_t x=0;x<5;++x) {
            const size_t i=size_t(p.cellY*5+y)*g.width+p.cellX*5+x;
            // D2Common sub_6FD411F0 traverses the DT1 flag rows bottom to top.
            g.flags[i]|=tile.collision[(4-y)*5+x];
            if(tile.orientation==0)g.ground[i]=1;
        }
    }
    return g;
}
GridPoint findSpawn(const CollisionGrid& g) {
    GridPoint best{-1,-1};int distance=std::numeric_limits<int>::max();
    for(int y=0;y<int(g.height);++y)for(int x=0;x<int(g.width);++x) {
        int dx=x-int(g.width/2),dy=y-int(g.height/2),d=dx*dx+dy*dy;
        if(d<distance&&g.walkable({x,y})) {best={x,y};distance=d;}
    }
    if(best.x<0)throw std::runtime_error("Map has no walkable floor for marker spawn");
    return best;
}
std::vector<GridPoint> findPath(const CollisionGrid& g,GridPoint from,GridPoint to) {
    if(!g.walkable(from)||!g.walkable(to)||from==to)return {};
    auto index=[&](GridPoint p){return p.y*int(g.width)+p.x;};
    std::vector<int> parent(size_t(g.width)*g.height,-1),queue;
    int start=index(from),end=index(to);parent[start]=start;queue.push_back(start);
    constexpr GridPoint steps[]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for(size_t head=0;head<queue.size()&&parent[end]<0;++head) {
        int n=queue[head];GridPoint p{n%int(g.width),n/int(g.width)};
        for(auto s:steps) {GridPoint next{p.x+s.x,p.y+s.y};
            if(!g.canStep(p,next))continue;int i=index(next);
            if(parent[i]<0){parent[i]=n;queue.push_back(i);}
        }
    }
    if(parent[end]<0)return {};
    std::vector<GridPoint> path;
    for(int n=end;n!=start;n=parent[n])path.push_back({n%int(g.width),n/int(g.width)});
    std::reverse(path.begin(),path.end());return path;
}
std::array<float,2> projectPoint(const MapScene& s,float x,float y) {
    return {float(s.originX)+16*(x-y),float(s.originY)+8*(x+y)};
}
std::array<float,2> projectSubtile(const MapScene& s,GridPoint p) {
    return projectPoint(s,float(p.x)+.5f,float(p.y)+.5f);
}
GridPoint pickSubtile(const MapScene& s,float x,float y) {
    x=(x-s.originX)/16;y=(y-s.originY)/8;
    return {int(std::floor((x+y)*.5f)),int(std::floor((y-x)*.5f))};
}
}
