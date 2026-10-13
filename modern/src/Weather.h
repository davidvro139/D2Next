#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <vector>
namespace d2 {
// Rain, and snow in Act V, as D2Client 1.10f makes it (0x6FAA6AE0-0x6FAA88B0): drops in screen pixels that fall along the
// wind and move with the camera, on levels whose levels.txt Rain is set, outside.
//
// The weather goes round four phases: clear (7500 + up to 7500 frames), building up (250 + up to 250), full (3000 + up to
// 3000; three times that in Acts III and V) and dying down (125 + up to 50). Building up and dying down move the number of
// drops between none and the storm's maximum (32-255 drops, drawn at random as the storm begins); strength() is that
// number / 256, which the rain's sound follows. Act V snows instead, and only some of its storms are clear: its levels
// set the maximum, the wind's range, and whether it snows all the time (the Rocky Summit and Nihlathak's Temple).
//
// A rain drop is a line 4-12 pixels long, longer and faster (15-30 pixels a frame) the lower on the screen it will land;
// it ends at a line chosen when it starts, stays there as a dot for 3 frames, and starts again at the top. Its colour is one
// of twelve greys-greens chosen by the period of the day (half see-through by day). A snowflake is a small shape (bigger
// lower on the screen) that sways from side to side as it falls.
//
// Not done: the splashes the original draws on the ground (a DC6 animation), mud, and the lightning of some storms.
class Weather {
public:
    struct Line {float x0,y0,x1,y1;std::array<uint8_t,3> color;float alpha;};
    // level / act (0-4) / rain (levels.txt Rain, and not inside) when the level changes; once a game frame.
    void update(int32_t level,int act,bool rain,int periodOfDay,float screenWidth,float screenHeight,float cameraX,float cameraY);
    // Test hook: straight to this phase (2: the full storm), when the level has weather.
    void forcePhase(int phase){if(active_)enterPhase(phase&3);}
    float strength() const {return float(count_)/256.f;}
    bool snowing() const {return snow_;}
    const std::vector<Line>& lines() const {return lines_;}
private:
    struct Drop {float x=0,y=0,ground=0;int length=0,speed=0,landed=0,dotFrames=0,shape=0,sway=0;std::array<uint8_t,3> color{};float alpha=1;};
    uint32_t random(uint32_t n){return n?uint32_t(random_()%n):0u;}
    void enterLevel(int32_t level,int act);
    void enterPhase(int phase);
    void spawn(Drop& drop,int periodOfDay);
    std::mt19937 random_{std::random_device{}()};
    int32_t level_=-1;int act_=0;bool active_=false;
    bool snow_=false,blizzard_=false,biggerFlakes_=false; // D2Client 0x6FBA7714, 0x6FBA771C, 0x6FBA7718
    int phase_=0,phaseTotal_=0,phaseLeft_=0,count_=0,maximum_=0;
    int wind_=127,windTarget_=127,windTimer_=0,windBase_=92,windRange_=71; // in 512ths of a turn
    std::array<int,4> phaseBase_{7500,250,3000,125},phaseRandom_{7500,250,3000,50};
    uint32_t frames_=0;
    float width_=800,height_=600,cameraX_=0,cameraY_=0;bool cameraKnown_=false;
    std::vector<Drop> drops_;
    std::vector<Line> lines_;
};
}
