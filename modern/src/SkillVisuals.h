#pragma once
#include <cstdint>

namespace d2 {
constexpr int32_t skillLeap=132;
constexpr int32_t skillLeapAttack=143;

inline uint8_t nextLeapSequenceFrame(uint8_t previous,bool leapPath,uint8_t mode) {
    // S1 contains the complete vertical motion. Frame 5 launches, frame 11 is
    // held in flight, and frames 12..14 are the original three-frame landing.
    if(leapPath)return previous==255||previous>=12?5:uint8_t(previous<11?previous+1:11);
    // Off the leap path: the landing (12..14), then, while the game keeps the sequence mode, Leap Attack's strike
    // (sequence steps 15 on, A1 frames: playerSequenceFrame).
    if(previous<=11)return 12;
    if(previous<14||mode==18)return uint8_t(previous<254?previous+1:254);
    return 255;
}

// The player mode whose art a step of the Leap or Leap Attack sequence shows (D2Common gPlayerSequenceLeap 0x6FDDF570,
// gPlayerSequenceLeapAttack 0x6FDDF678): steps 0..14 are S1 (13), the jump, with frames 8..11 off the ground; Leap
// Attack's later steps are A1 (7). -1 for other skills. (skills.txt seqtrans names A1 for both, which drew the whole
// leap with attack frames standing on the ground.)
inline int leapSequenceMode(int32_t skill,uint64_t step) {
    if(skill==skillLeap)return 13;
    if(skill==skillLeapAttack)return step<15?13:7;
    return -1;
}

inline uint64_t playerSequenceFrame(int32_t skill,uint64_t frame,bool unarmed) {
    // Leap Attack's first 15 entries are the jump. Its remaining entries resume A1 at frame 5
    // unarmed and frame 6 for the weapon sequences, rather than continuing at A1 frame 15.
    if(skill==skillLeapAttack&&frame>=15)return frame-(unarmed?10u:9u);
    return frame;
}
}
