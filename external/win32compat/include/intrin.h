#pragma once
// <intrin.h>: the MSVC intrinsics the game libraries use, from the compiler's builtins.
#include <Windows.h>
template<class I,class M> inline unsigned char _BitScanReverse(I* index,M mask) {
    if(!mask)return 0;
    *index=I(31-__builtin_clz(uint32_t(mask)));return 1;
}
template<class I,class M> inline unsigned char _BitScanForward(I* index,M mask) {
    if(!mask)return 0;
    *index=I(__builtin_ctz(uint32_t(mask)));return 1;
}
