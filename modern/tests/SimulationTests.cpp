#include "Simulation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
static void require(bool b,const char* what){if(!b)throw std::runtime_error(std::string("Simulation test failed: ")+what);}
static int64_t distance(d2::FixedPoint a,d2::FixedPoint b){
    const double dx=double(a.x)-b.x,dy=double(a.y)-b.y;return int64_t(std::llround(std::sqrt(dx*dx+dy*dy)));
}
static d2::CollisionGrid open(uint32_t w,uint32_t h){return {w,h,d2::Bytes(size_t(w)*h,0),d2::Bytes(size_t(w)*h,1)};}
static size_t runUntilIdle(d2::World& w,size_t limit=1000){
    size_t n=0;while(w.player().moving&&n<limit){w.tick();++n;}return n;
}
int main(){try{
    using d2::Command;using d2::GridPoint;
    // Speed: 8 straight subtiles at ~6 subtiles/s take ~1.33 s (33 ticks).
    {
        auto g=open(20,20);d2::World w(g,{2,2});
        require(w.apply(Command::moveTo({10,2})),"straight order accepted");
        w.tick();require(w.player().moving,"moves on first tick");
        require(w.player().facing==d2::facingFor(1,0)&&w.player().facing==14,"east facing");
        const size_t ticks=1+runUntilIdle(w);
        require(ticks>=33&&ticks<=35,"straight walk duration");
        require(w.player().position==d2::subtileCenter({10,2}),"rests on the destination centre");
        require(!w.player().moving&&w.player().route.empty(),"idle at destination");
    }
    // Diagonals cover sqrt(2) times the distance per step, so they take proportionally longer.
    {
        auto g=open(20,20);d2::World w(g,{2,2});
        require(w.apply(Command::moveTo({7,7})),"diagonal order accepted");
        w.tick();require(w.player().facing==0,"south-east screen diagonal facing");
        const size_t ticks=1+runUntilIdle(w);
        require(ticks>=29&&ticks<=31,"diagonal walk duration");
    }
    // Every facing table entry agrees with the previous atan2-based mapping.
    {
        constexpr double pi=3.14159265358979323846;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            if(!dx&&!dy)continue;
            const unsigned expected=unsigned((int(std::lround((std::atan2(double(dy),double(dx))-pi/4)*8/pi))+16)%16);
            require(d2::facingFor(dx,dy)==expected,"facing table matches atan2 mapping");
        }
    }
    // A wall with a gap: the walker routes through the gap and never stands on a blocked subtile.
    {
        auto g=open(12,12);for(int y=0;y<11;++y)g.flags[size_t(y)*12+6]=1;
        d2::World w(g,{2,2});
        require(w.apply(Command::moveTo({10,2})),"routed order accepted");
        bool throughGap=false;
        for(int i=0;i<400&&(i==0||w.player().moving);++i){
            w.tick();const auto here=d2::subtileOf(w.player().position);
            require(g.walkable(here),"never inside a blocked subtile");
            throughGap|=here.y==11;
        }
        require(throughGap&&d2::subtileOf(w.player().position)==GridPoint{10,2},"went around the wall");
    }
    // Changing destination mid-walk never teleports: the current segment finishes first.
    {
        auto g=open(20,20);d2::World w(g,{2,2});
        w.apply(Command::moveTo({12,2}));
        for(int i=0;i<5;++i)w.tick();
        w.apply(Command::moveTo({5,12}));
        for(int i=0;i<300&&(i==0||w.player().moving);++i){
            w.tick();
            require(distance(w.player().previous,w.player().position)<=d2::walkSpeed+1,"step length bounded by walk speed");
        }
        require(d2::subtileOf(w.player().position)==GridPoint{5,12},"reached the second destination");
    }
    // Rejected commands leave the state exactly as it was.
    {
        auto g=open(10,10);g.flags[5*10+5]=1;d2::World w(g,{1,1});
        const auto before=w.hash();
        require(!w.apply(Command::moveTo({5,5})),"blocked destination rejected");
        require(!w.apply(Command::moveTo({-4,3})),"outside map rejected");
        require(!w.apply(Command::moveTo({100,100})),"far outside map rejected");
        require(w.hash()==before,"rejection leaves state unchanged");
        require(w.apply(Command::moveTo({1,1})),"already there is accepted");
        w.tick();require(!w.player().moving,"already there does not move");
    }
    // Held direction: continuous motion without idle frames between subtiles, stops on release and at walls.
    {
        auto g=open(12,12);d2::World w(g,{2,5});
        w.apply(Command::hold(1,0));
        for(int i=0;i<20;++i){w.tick();require(w.player().moving,"no idle gap while held");}
        require(w.player().facing==14,"held facing");
        w.apply(Command::hold(0,0));
        runUntilIdle(w);
        const auto rest=w.player().position;
        require(rest==d2::subtileCenter(d2::subtileOf(rest)),"release rests on a subtile centre");
        require(d2::subtileOf(rest).x>=5&&d2::subtileOf(rest).x<=8,"released after about 3.3 subtiles");
        w.apply(Command::hold(1,0));
        for(int i=0;i<200;++i)w.tick();
        require(d2::subtileOf(w.player().position)==GridPoint{11,5}&&!w.player().moving,"held walker stops at the map edge");
        w.apply(Command::hold(1,1)); // Diagonal into the edge is blocked too.
        w.tick();require(!w.player().moving,"blocked diagonal does not move");
    }
    // Replay: the same recorded commands reproduce identical state on every tick.
    {
        auto g=open(30,30);for(int y=0;y<25;++y)g.flags[size_t(y)*30+15]=1;
        const std::vector<std::pair<uint64_t,Command>> script={
            {0,Command::moveTo({5,5})},{10,Command::moveTo({25,4})},{40,Command::hold(0,1)},
            {60,Command::stop()},{70,Command::moveTo({27,27})},{90,Command::moveTo({-3,4})},{120,Command::hold(-1,0)},{130,Command::hold(0,0)}};
        auto run=[&](std::vector<uint64_t>& hashes){
            d2::World w(g,{2,2});size_t next=0;
            for(uint64_t t=0;t<220;++t){
                while(next<script.size()&&script[next].first==t)w.apply(script[next++].second);
                w.tick();hashes.push_back(w.hash());
            }
            return w.player().position;
        };
        std::vector<uint64_t> a,b;
        const auto pa=run(a),pb=run(b);
        require(a==b&&pa==pb,"replay reproduces every tick hash");
        auto altered=script;altered[1].second=Command::moveTo({25,20});
        d2::World w(g,{2,2});size_t next=0;bool differs=false;
        for(uint64_t t=0;t<220;++t){
            while(next<altered.size()&&altered[next].first==t)w.apply(altered[next++].second);
            w.tick();differs|=w.hash()!=a[size_t(t)];
        }
        require(differs,"different inputs produce different state");
    }
    // Interpolation: render positions are continuous between ticks and independent of render rate.
    {
        auto g=open(20,20);d2::World w(g,{2,2});w.apply(Command::moveTo({12,2}));
        w.tick();w.tick();
        const auto& p=w.player();
        const auto start=d2::renderPosition(p,0),end=d2::renderPosition(p,1),mid=d2::renderPosition(p,.5f);
        require(std::abs(start[0]-float(p.previous.x)/d2::fixedOne)<1e-5f,"alpha 0 is the previous tick");
        require(std::abs(end[0]-float(p.position.x)/d2::fixedOne)<1e-5f,"alpha 1 is the current tick");
        require(std::abs(mid[0]-(start[0]+end[0])*.5f)<1e-5f&&std::abs(mid[1]-start[1])<1e-5f,"alpha 0.5 is halfway");
        // One render frame at 30, 60 or 144 fps advances alpha by 25/fps of a tick, so the
        // interpolated speed is 6 subtiles per second at every frame rate.
        for(int fps:{30,60,144}){
            const float dt=1.0f/fps,alpha=dt*d2::simulationHz;
            const float perSecond=(d2::renderPosition(p,alpha)[0]-d2::renderPosition(p,0)[0])/dt;
            require(std::abs(perSecond-6.0f)<.02f,"render speed is the same at every frame rate");
        }
    }
    std::cout<<"PASS: walk speed, facing, wall avoidance, mid-route retargeting, held movement, rejected commands, replay determinism and interpolation\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
