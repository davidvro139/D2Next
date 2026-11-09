#include "SkillVisuals.h"
#include "HeroCryState.h"
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(std::string("Skill visual test failed: ")+message);}

int main(){try{
    d2::HeroCryState cries;
    require(cries.observe(1,false)==d2::HeroCry::None,"incomplete snapshot emitted a cry");
    require(cries.observe(0,true)==d2::HeroCry::None,"first valid dead snapshot emitted stale death cry");
    require(cries.observe(0,true)==d2::HeroCry::None,"unchanged death repeated cry");
    require(cries.observe(1,true)==d2::HeroCry::None,"standing emitted cry");
    require(cries.observe(4,true)==d2::HeroCry::Hit,"real hit lost its cry");
    require(cries.observe(4,true)==d2::HeroCry::None,"held hit repeated cry");
    require(cries.observe(0,true)==d2::HeroCry::Death,"real death lost its cry");
    require(cries.observe(1,false)==d2::HeroCry::None&&cries.observe(0,true)==d2::HeroCry::None,"invalid snapshot retained old mode");
    cries=d2::HeroCryState{};
    require(cries.observe(0,true)==d2::HeroCry::None,"new session retained old mode");
    uint8_t frame=255;
    frame=d2::nextLeapSequenceFrame(frame,true,18);require(frame==5,"Leap begins with original takeoff frame 6");
    for(int i=0;i<10;++i)frame=d2::nextLeapSequenceFrame(frame,true,18);
    require(frame==11,"Leap holds original airborne frame 12 while moving");
    frame=d2::nextLeapSequenceFrame(frame,false,18);require(frame==12,"landing begins at original frame 13 when the leap path ends");
    frame=d2::nextLeapSequenceFrame(frame,false,1);require(frame==13,"landing plays original frame 14");
    frame=d2::nextLeapSequenceFrame(frame,false,1);require(frame==14,"landing plays original frame 15");
    frame=d2::nextLeapSequenceFrame(frame,false,1);require(frame==255,"Leap returns to the server animation after landing");
    require(d2::playerSequenceFrame(d2::skillLeapAttack,15,true)==5,"unarmed Leap Attack resumes A1 frame 5");
    require(d2::playerSequenceFrame(d2::skillLeapAttack,15,false)==6,"armed Leap Attack resumes A1 frame 6");
    require(d2::playerSequenceFrame(d2::skillLeap,10,false)==10,"Leap keeps its sequence frame");
    // Leap Attack: the game stays in the sequence mode (18) after landing for the strike, so the steps go on past 14.
    frame=11;for(int i=0;i<4;++i)frame=d2::nextLeapSequenceFrame(frame,false,18);
    require(frame==15,"Leap Attack goes on from the landing into its strike");
    require(d2::playerSequenceFrame(d2::skillLeapAttack,frame,false)==6,"the strike starts at A1 frame 6 with a weapon");
    frame=d2::nextLeapSequenceFrame(frame,false,1);require(frame==255,"the strike ends with the sequence mode");
    // The art: S1 (13) for the jump, A1 (7) for Leap Attack's strike (D2Common's Leap sequences).
    require(d2::leapSequenceMode(d2::skillLeap,11)==13&&d2::leapSequenceMode(d2::skillLeapAttack,11)==13,"the jump draws S1");
    require(d2::leapSequenceMode(d2::skillLeapAttack,15)==7,"Leap Attack's strike draws A1");
    require(d2::leapSequenceMode(d2::skillLeap-1,3)==-1,"other skills keep skills.txt's mode");
    std::cout<<"PASS: hero cry initial/death/hit/reset transitions; Leap arc, Leap Attack landing and strike frames\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
