#pragma once
// D2MOO_X64: the 64-bit port (pointers are 8 bytes, so the original 32-bit layouts and tricks cannot apply), on any
// system: Windows x64, Android arm64, Linux, the web (wasm64).
#if !defined(D2MOO_X64) && (defined(_WIN64) || defined(__LP64__) || defined(__wasm64__))
#define D2MOO_X64 1
#endif
