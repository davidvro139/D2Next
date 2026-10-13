// Native x64 replacements for the small utility functions of the original Fog.dll.
// Each one was derived from the disassembly of the 1.10f Fog.dll and is checked against it by the
// differential test in tools/x64 (see BUILD-X64.md).
#include <Windows.h>

#include <Fog.h>

#include "FogState.h"

#include <bit>
#include <ctime>
#include <filesystem>

namespace
{
// Sine of one full turn split in 512 steps. The values are bit-exact copies of the original table (they are
// not exactly sinf()), so simulation results that use them match the 32-bit game.
constexpr uint32_t kSinTableBits[512] =
{
#include "SinTable.inc"
};

float sinTable(int index)
{
	return std::bit_cast<float>(kSinTableBits[index & 0x1FF]);
}

// CRC-16/CCITT table, MSB first, polynomial 0x1021 (identical to the table in the original DLL).
struct Crc16Table
{
	uint16_t entries[256];
	constexpr Crc16Table() : entries()
	{
		for (int i = 0; i < 256; ++i)
		{
			uint32_t crc = uint32_t(i) << 8;
			for (int bit = 0; bit < 8; ++bit)
			{
				crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
			}
			entries[i] = uint16_t(crc);
		}
	}
};
constexpr Crc16Table kCrc16;
}

float __stdcall FOG_10083_Cos_LUT(int16_t index)
{
	return sinTable(int(index) + 0x80);
}

float __stdcall FOG_10084_Sin_LUT(int16_t index)
{
	return sinTable(int(index));
}

// checksum = rotl(checksum, 1) + byte, as used by the character save format
uint32_t __stdcall FOG_ComputeChecksum(void* pData, size_t dwSize)
{
	const auto* pBytes = static_cast<const uint8_t*>(pData);
	uint32_t nChecksum = 0;
	for (size_t i = 0; i < dwSize; ++i)
	{
		nChecksum = std::rotl(nChecksum, 1) + pBytes[i];
	}
	return nChecksum;
}

// Number of set bits in the buffer
uint32_t __stdcall FOG_PopCount(void* pData, size_t dwSize)
{
	const auto* pBytes = static_cast<const uint8_t*>(pData);
	uint32_t nCount = 0;
	for (size_t i = 0; i < dwSize; ++i)
	{
		nCount += uint32_t(std::popcount(pBytes[i]));
	}
	return nCount;
}

// Despite the name, this is the index of the lowest set bit (0 to 31), or -1 for zero.
uint32_t __stdcall FOG_LeadingZeroesCount(uint32_t nValue)
{
	return nValue ? uint32_t(std::countr_zero(nValue)) : 0xFFFFFFFFu;
}

// CRC over the lower-cased characters: crc = table[(crc & 0xFF) ^ c] ^ (crc >> 2), starting from 0xFFFF
uint16_t __stdcall FOG_ComputeStringCRC16(const char* szString)
{
	uint16_t nCrc = 0xFFFF;
	for (const auto* p = reinterpret_cast<const uint8_t*>(szString); *p; ++p)
	{
		const uint8_t c = (*p >= 'A' && *p <= 'Z') ? uint8_t(*p + 0x20) : *p;
		nCrc = uint16_t(kCrc16.entries[(nCrc & 0xFF) ^ c] ^ (nCrc >> 2));
	}
	return nCrc;
}

unsigned __stdcall FOG_Decode14BitsFromString(uint16_t* p2Characters)
{
	const auto* p = reinterpret_cast<const uint8_t*>(p2Characters);
	return (unsigned(p[1] & 0x7F) << 7) + (p[0] & 0x7F);
}

void __stdcall FOG_Encode14BitsToString(uint16_t* p2Characters, uint32_t value)
{
	auto* p = reinterpret_cast<uint8_t*>(p2Characters);
	p[0] = uint8_t(value | 0x80);
	p[1] = uint8_t((value >> 7) | 0x80);
}

// Four base-255 digits, each stored as digit + 1
unsigned __stdcall FOG_Decode32BitsFromString(uint32_t* p4Characters)
{
	const auto* p = reinterpret_cast<const uint8_t*>(p4Characters);
	uint32_t nValue = p[3];
	nValue = nValue * 255 + p[2];
	nValue = nValue * 255 + p[1];
	nValue = nValue * 255 + p[0];
	return nValue - 0x00FE0200u; // 1 + 255 + 255^2 + 255^3, the sum of the +1 offsets
}

void __stdcall FOG_Encode32BitsToString(uint32_t* p4Characters, uint32_t value)
{
	auto* p = reinterpret_cast<uint8_t*>(p4Characters);
	for (int i = 0; i < 4; ++i)
	{
		p[i] = uint8_t(value % 255 + 1);
		value /= 255;
	}
}

// The original returns wall-clock seconds, kept smooth using the millisecond tick count.
int32_t __fastcall FOG_10055_GetSyncTime()
{
	return int32_t(time(nullptr));
}

void __fastcall FOG_10050_EnterCriticalSection(CRITICAL_SECTION* pCriticalSection, int nLine)
{
	(void)nLine; // The original used the line to report deadlocks
	EnterCriticalSection(pCriticalSection);
}

int __fastcall FOG_IsExpansion()
{
	std::lock_guard<std::mutex> lock(fogx64::state().mutex);
	return fogx64::state().isExpansion ? 1 : 0;
}

BOOL __fastcall FOG_GetInstallPath(char* pPathBuffer, size_t nBufferSize)
{
	auto& s = fogx64::state();
	std::lock_guard<std::mutex> lock(s.mutex);
	fogx64::resolveDefaultsLocked(s);
	if (!pPathBuffer || s.installPath.size() + 1 > nBufferSize)
	{
		return FALSE;
	}
	memcpy(pPathBuffer, s.installPath.c_str(), s.installPath.size() + 1);
	return TRUE;
}

// Copies the save folder (with a trailing backslash) into the buffer, creating the folder if needed,
// and returns its length. Returns 0 when the buffer is too small.
size_t __fastcall FOG_GetSavePath(char* pPathBuffer, size_t nBufferSize)
{
	auto& s = fogx64::state();
	std::string savePath;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		fogx64::resolveDefaultsLocked(s);
		savePath = s.savePath;
	}

	if (!pPathBuffer || savePath.size() + 1 > nBufferSize)
	{
		return 0;
	}

	std::error_code ignored;
	std::filesystem::create_directories(savePath, ignored);
	memcpy(pPathBuffer, savePath.c_str(), savePath.size() + 1);
	return savePath.size();
}
