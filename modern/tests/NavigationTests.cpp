#include "Navigation.h"
#include <iostream>
#include <stdexcept>
static void require(bool b){if(!b)throw std::runtime_error("Navigation test failed");}
int main(){try {
    d2::MapScene s;s.cellsX=2;s.cellsY=1;s.originX=123;s.originY=45;
    d2::Tile floor;floor.orientation=0;floor.collision[20]=1;floor.collision[4]=8;
    d2::Tile wall;wall.orientation=1;wall.collision[21]=1;
    d2::Tile shadow;shadow.collision.fill(255);
    s.tiles={floor,wall,shadow};s.placements={{0,0,0,0,0,false},{1,0,0,0,0,false},{2,0,0,0,0,true}};
    auto g=d2::buildCollision(s);
    require(!g.walkable({0,0})&&!g.walkable({4,4})&&!g.walkable({1,0}));
    require(g.walkable({0,4})&&g.walkable({4,0}));
    require(!g.walkable({5,0})&&!g.walkable({-1,0})&&!g.walkable({10,5}));
    require(g.walkable(d2::findSpawn(g)));
    for(int y=-5;y<10;++y)for(int x=-5;x<10;++x) {
        const auto p=d2::projectSubtile(s,{x,y});require(d2::pickSubtile(s,p[0],p[1])==d2::GridPoint{x,y});
    }
    d2::CollisionGrid maze{7,7,d2::Bytes(49,0),d2::Bytes(49,1)};
    for(int y=0;y<6;++y)maze.flags[y*7+3]=1;
    auto path=d2::findPath(maze,{1,1},{5,1});require(!path.empty());
    d2::GridPoint last{1,1};bool throughGap=false;
    for(auto p:path){require(maze.canStep(last,p));throughGap|=p.y==6;last=p;}
    require(throughGap&&last==d2::GridPoint{5,1});
    maze.flags[6*7+3]=1;require(d2::findPath(maze,{1,1},{5,1}).empty());
    require(d2::findPath(maze,{1,1},{-1,1}).empty());
    require(!maze.canStep({1,1},{5,1})); // Cannot tunnel through walls.
    maze.flags[1]=1;maze.flags[7]=1;
    require(!maze.canStep({0,0},{1,1})); // Diagonal corner blocked.
    maze.flags[1]=0;require(!maze.canStep({0,0},{1,1}));
    maze.flags[7]=0;require(maze.canStep({0,0},{1,1}));
    std::cout<<"PASS: DT1 row direction, merged flags, shadows, missing floors, coordinate round trips, routing, unreachable destinations and corners\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
