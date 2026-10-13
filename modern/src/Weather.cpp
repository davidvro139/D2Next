#include "Weather.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace d2 {
namespace {
// Fog #10083 / #10084: cosine and sine by 512ths of a turn.
float turnCos(int a){return float(std::cos(double(a&511)*3.14159265358979/256.0));}
float turnSin(int a){return float(std::sin(double(a&511)*3.14159265358979/256.0));}
// A snowflake's line from where it is, by size (D2Client 0x6FB6E190; a settled flake draws the next one).
constexpr std::array<std::array<int,4>,9> flakes{{{0,0,0,1},{-1,-1,0,0},{0,-1,0,1},{1,0,-1,1},{1,0,0,1},{2,-1,-1,1},{1,1,-2,-1},{2,1,-2,-1},{2,-1,-2,2}}};
// Act V's levels from 109 (Harrogath) to 121 (Nihlathak's Temple), as D2Client 0x6FAA7040 sorts them: 0 Harrogath and the
// slopes below it, 1 the Tundra Wastelands, 2 the Rocky Summit and Nihlathak's Temple (snowing all the time), 3 the rest.
constexpr std::array<int,13> actVKinds{0,0,0,0,3,3,3,3,1,3,3,2,2};
int actVKind(int32_t level){return level>=109&&level<=121?actVKinds[size_t(level-109)]:3;}
}
void Weather::enterLevel(int32_t level,int act) {
    // D2Client 0x6FAA6D60: a new level starts clear with a new wind (aimed 92-162, changing in 125-500 frames).
    level_=level;act_=act;drops_.clear();count_=0;maximum_=0;
    windTimer_=int(random(375))+125;wind_=windTarget_=int(random(71))+92;windBase_=92;windRange_=71;
    snow_=act==4;blizzard_=false;biggerFlakes_=false;
    if(snow_) {
        // Some of Harrogath's visits have no snow at all (25%); the Tundra's and the summit's flakes are a size bigger.
        const int kind=actVKind(level);
        if(kind==0&&random(100)<25)snow_=false;
        if(kind==1||kind==2)biggerFlakes_=true;
        if(kind==2)blizzard_=true;
        if(!snow_){active_=false;return;}
    }
    phaseBase_={7500,250,3000,125};phaseRandom_={7500,250,3000,50};
    if(act==2||act==4)phaseBase_[2]*=3; // longer storms in Act III's jungle and Act V
    enterPhase(0);
}
void Weather::enterPhase(int phase) {
    // D2Client 0x6FAA6AE0: a snowing-all-the-time level stays in the full phase.
    if(blizzard_)phase=2;
    phase_=phase;phaseTotal_=phaseLeft_=phaseBase_[size_t(phase)]+int(random(uint32_t(phaseRandom_[size_t(phase)])));
    if(phase==0)count_=0;
    else if(phase==1)maximum_=int(random(224))+32;
    else if(phase==2) {
        if(snow_)switch(actVKind(level_)) {
            case 0:maximum_=int(random(56))+32;windBase_=42;windRange_=170;break;
            case 1:maximum_=int(random(112))+32;windBase_=170;windRange_=56;break;
            case 2:maximum_=256;wind_=windTarget_=windBase_=windRange_=28;break;
            default:break;
        }
        if(maximum_<=0)maximum_=int(random(224))+32;
        if(std::getenv("D2MODERN_WEATHER_LOG"))std::cout<<"Weather: full storm, "<<maximum_<<" drops"<<std::endl;
        count_=maximum_;
    }
}
void Weather::spawn(Drop& d,int period) {
    // D2Client 0x6FAA7D70. The lower the line it lands on, the nearer the drop: longer, faster, a bigger flake.
    const int span=std::max(1,int(height_)-87);
    d=Drop{};
    d.x=float(random(uint32_t(std::max(1.f,width_))));
    d.ground=float(40+int(random(uint32_t(span))));
    d.y=float(int(random(uint32_t(d.ground)+20))-20);
    d.sway=int(random(512));
    const float near=(d.ground-40)/float(span);
    if(snow_) {
        static constexpr std::array<uint8_t,4> alphas{200,160,80,160}; // 0x6FB6E189 by period: day, dusk, night, dawn
        d.alpha=alphas[size_t(period&3)]/255.f;d.dotFrames=1;
        d.speed=8+int(28*near);
        d.shape=std::clamp(int((d.ground-40)*7/float(span))+(biggerFlakes_?1:0),0,7);
        // A grey by size (0x6FBA767C: 120 + 13 each step, kept to a byte), dimmer steps at dusk, dawn and night.
        int index=d.shape*12/8;
        if(period==1||period==3)index/=2;else if(period==2)index/=4;
        const uint8_t v=uint8_t(120+index*80/6);d.color={v,v,v};
    } else {
        // One of twelve colours for the period of the day (0x6FAA71C9, the nearest palette entries to these): by day a light
        // grey-green, half see-through; at dusk and dawn darker; at night darkest. The steps wrap round a byte, as there.
        const int i=int(random(12));
        uint8_t v=0,g=0;
        if(period==0){v=uint8_t(98-i*80/6);g=uint8_t(v+25);d.alpha=127/255.f;}
        else if(period==2){v=uint8_t(25-i*24/6);g=uint8_t(v+5);}
        else {v=uint8_t(45-i*40/6);g=uint8_t(v+10);}
        d.color={v,g,v};
        d.length=4+int(8*near);d.speed=15+int(15*near);d.dotFrames=3;
    }
}
void Weather::update(int32_t level,int act,bool rain,int period,float screenWidth,float screenHeight,float cameraX,float cameraY) {
    width_=screenWidth;height_=screenHeight;
    const float cameraDx=cameraKnown_?cameraX-cameraX_:0,cameraDy=cameraKnown_?cameraY-cameraY_:0;
    cameraX_=cameraX;cameraY_=cameraY;cameraKnown_=true;
    if(level!=level_){active_=rain;enterLevel(level,act);if(!rain)active_=false;}
    lines_.clear();
    if(!active_){drops_.clear();count_=0;return;}
    // The phase and the number of drops (0x6FAA7CA7).
    if(--phaseLeft_<=0)enterPhase((phase_+1)%4);
    else if(phase_==1)count_=int(int64_t(phaseTotal_-phaseLeft_)*maximum_/std::max(1,phaseTotal_));
    else if(phase_==3)count_=int(int64_t(maximum_)*phaseLeft_/std::max(1,phaseTotal_));
    // The drops (0x6FAA7A77): rain falls faster in a heavier storm, snow slower the more sideways the wind blows it.
    const float factor=snow_?std::max(.25f,std::abs(turnCos(wind_))):strength()*.15f+.85f;
    for(size_t i=0;i<drops_.size();) {
        auto& d=drops_[i];
        int dx=0;
        if(!d.landed) {
            const int v=int(float(d.speed)*factor);
            dx=int(turnCos(wind_)*float(v));
            d.y+=float(int(turnSin(wind_)*float(v)));
        }
        d.y-=cameraDy;
        if(d.y>d.ground)d.landed=1;
        if(d.landed) {
            if(d.dotFrames==0) {
                // Gone; a new one starts while the storm still wants as many.
                if(int(drops_.size())<=count_){spawn(d,period);++i;}
                else {d=drops_.back();drops_.pop_back();}
                continue;
            }
            --d.dotFrames;d.speed=0;dx=0;
        }
        if(snow_&&!d.landed)dx+=int(2*turnSin(int((frames_*512/100+uint32_t(d.sway))&511)));
        d.x+=float(dx)-cameraDx;
        if(width_>0){d.x=std::fmod(d.x,width_);if(d.x<0)d.x+=width_;}
        ++i;
    }
    ++frames_;
    if(std::getenv("D2MODERN_WEATHER_LOG")&&frames_%50==0)std::cout<<"Weather: level "<<level_<<(snow_?" snow":" rain")<<", phase "<<phase_<<" ("<<phaseLeft_<<" left), drops "<<drops_.size()<<"/"<<count_<<" of "<<maximum_<<", wind "<<wind_<<std::endl;
    while(int(drops_.size())<count_){drops_.emplace_back();spawn(drops_.back(),period);}
    // The wind turns toward its aim, 2 a frame for rain and 1 for snow, and takes a new aim now and then (0x6FAA80FB).
    const int step=snow_?1:2;
    if(wind_<windTarget_)wind_=std::min(windTarget_,wind_+step);
    else if(wind_>windTarget_)wind_=std::max(windTarget_,wind_-step);
    if(--windTimer_<=0) {
        if(snow_){windTimer_=int(random(875))+250;windTarget_=windBase_+int(random(uint32_t(std::max(1,windRange_))));}
        else {windTimer_=int(random(375))+125;windTarget_=int(random(71))+92;}
    }
    // What to draw (0x6FAA8450): a falling drop's line along the wind, cut at its landing line; a landed drop's dot; a flake.
    for(const auto& d:drops_) {
        if(snow_) {
            const auto& s=flakes[size_t(std::min(8,d.shape+(d.landed?1:0)))];
            lines_.push_back({d.x+float(s[0]),d.y+float(s[1]),d.x+float(s[2]),d.y+float(s[3]),d.color,d.alpha});
        } else if(d.landed) lines_.push_back({d.x,d.y,d.x,d.y,d.color,d.alpha});
        else {
            float lx=float(int(turnCos(wind_)*float(d.length))),ly=float(int(turnSin(wind_)*float(d.length)));
            const float room=d.ground-d.y;
            if(ly>room&&ly>0){lx=float(int(room*lx/ly));ly=room;}
            lines_.push_back({d.x,d.y,d.x+lx,d.y+ly,d.color,d.alpha});
        }
    }
}
}
