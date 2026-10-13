#include "Backend.h"
#include "Simulation.h"
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) {
    if(!ok) throw std::runtime_error(message);
}
static d2::CollisionGrid grid() { return {12,12,d2::Bytes(144,0),d2::Bytes(144,1)}; }
int main() { try {
    // Temporary grid is destroyed before any calls into the adapter.
    auto backend=d2::makePrototypeBackend(grid(),{2,2});
    auto initial=backend->snapshot();
    const auto id=initial.localPlayer;
    require(id!=0 && initial.player().id==id && initial.tick==0,"initial identity");
    require(backend->apply(0,d2::Command::stop())==d2::CommandResult::UnknownEntity,"zero actor rejected");
    require(backend->apply(id+1,d2::Command::moveTo({5,5}))==d2::CommandResult::UnknownEntity,"unknown actor rejected");
    require(backend->apply(id,d2::Command::moveTo({-1,0}))==d2::CommandResult::Rejected,"invalid destination rejected");
    require(backend->snapshot()==initial,"rejections leave snapshot unchanged");
    auto collision=grid();d2::World reference(collision,{2,2});
    for(unsigned tick=0;tick<100;++tick) {
        if(tick==0 || tick==7 || tick==30 || tick==60) {
            const auto command=tick==0 ? d2::Command::moveTo({9,2}) :
                tick==7 ? d2::Command::moveTo({3,9}) :
                tick==30 ? d2::Command::hold(1,0) : d2::Command::stop();
            require((backend->apply(id,command)==d2::CommandResult::Accepted)==reference.apply(command),"command parity");
        }
        backend->tick();reference.tick();
        const auto snapshot=backend->snapshot();const auto& p=snapshot.player();const auto& r=reference.player();
        require(snapshot.tick==reference.tickCount() && p.id==id,"tick and identity stable");
        require(p.position==r.position && p.previous==r.previous && p.facing==r.facing &&
            p.moving==r.moving && p.stateTicks==r.stateTicks,"adapter preserves movement and animation");
        for(float alpha:{-1.f,0.f,.5f,1.f,2.f})
            require(d2::renderPosition(p,alpha)==d2::renderPosition(r,alpha),"interpolation parity");
    }
    require(initial.tick==0 && initial.player().position==d2::subtileCenter({2,2}),"snapshot independent of ticks");
    auto retained=backend->snapshot();backend.reset();
    require(retained.player().id==id,"snapshot survives backend destruction");
    initial.entities.clear();
    bool rejected=false;try { initial.player(); } catch(const std::runtime_error&) { rejected=true; }
    require(rejected,"missing local player diagnosed");
    std::cout<<"PASS: backend ownership, identity, rejection, movement parity and interpolation\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
