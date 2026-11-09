#pragma once
#include <cstdint>
namespace d2::native {
// MONMODE_SEQUENCE rows still name monster modes. Transform states (e.g.
// Shadow Warrior) must pass those modes through D2Common #11013 before drawing
// player art, just as the ordinary mode does. The frame remains authoritative.
template<class Unit,class Convert>
uint8_t monsterVisualSequenceMode(Unit* unit,uint8_t sequenceMode,Convert convert) {
    if(sequenceMode>=16)return 255;
    int type=int(unit->dwUnitType),classId=int(unit->dwClassId),mode=sequenceMode;
    convert(unit,&type,&classId,&mode,__FILE__,__LINE__);
    return mode>=0&&mode<255?uint8_t(mode):uint8_t(255);
}
}
