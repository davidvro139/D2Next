// Native x64 replacement for Storm's UTF-8 to UTF-16 converter (ordinal 904), which D2Lang uses to turn table text
// into Unicode. Transcribed from the disassembly of the 1.10f Storm.dll: the classic table driven converter, with
// the quirks the callers see (a NUL ends the conversion and is written, invalid lead bytes are not rejected,
// values above U+10FFFF become U+FFFD). Checked against the original by tools/x64/StormUnicodeTest.
#include <Windows.h>

#include <Storm.h>

#include <cstdint>

namespace
{
// Number of continuation bytes announced by each lead byte. Bytes 0x80..0xBF count as none, so they pass through as is.
uint32_t trailingBytes(uint8_t lead)
{
	if (lead < 0xC0)
		return 0;
	if (lead < 0xE0)
		return 1;
	if (lead < 0xF0)
		return 2;
	if (lead < 0xF8)
		return 3;
	if (lead < 0xFC)
		return 4;
	return 5;
}

// What the accumulated bytes carry above the code point, per number of continuation bytes.
constexpr uint32_t kOffsets[6] = { 0x00000000, 0x00003080, 0x000E2080, 0x03C82080, 0xFA082080, 0x82082080 };
}

// Returns 0 when a NUL was converted, 1 when the buffer is full, -1 when the source ended without a NUL and
// -1-n when it ended inside a sequence that needed n more bytes. *pUnitsWritten counts the UTF-16 units written
// (the NUL included), *pBytesConsumed the source bytes converted (the NUL excluded); either may be null.
int __stdcall SUniConvertUTF8to16(wchar_t* pBuffer, int nBufferCount, const char* szSrc, int nSrcCount, int* pUnitsWritten, int* pBytesConsumed)
{
	const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(szSrc);
	const uint8_t* const pSrcStart = pSrc;
	const uintptr_t nSrcEnd = nSrcCount == 0x7FFFFFFF ? ~uintptr_t(0) : uintptr_t(pSrc) + uintptr_t(int64_t(nSrcCount));
	uint16_t* pDst = reinterpret_cast<uint16_t*>(pBuffer);
	uint16_t* const pDstStart = pDst;
	uint16_t* const pDstEnd = pDst + nBufferCount;

	int nResult = 0;
	if (uintptr_t(pSrc) >= nSrcEnd)
	{
		nResult = -1;
	}
	else
	{
		for (;;)
		{
			const uint8_t nLead = *pSrc;
			const uint32_t nTrailing = trailingBytes(nLead);
			if (uintptr_t(pSrc) + nTrailing >= nSrcEnd)
			{
				nResult = -1 - int(nTrailing);
				break;
			}

			uint32_t nCode = nLead;
			for (uint32_t i = 1; i <= nTrailing; ++i)
			{
				nCode = (nCode << 6) + pSrc[i];
			}
			nCode -= kOffsets[nTrailing];

			if (pDst >= pDstEnd)
			{
				nResult = 1;
				break;
			}

			if (nCode <= 0xFFFF)
			{
				*pDst++ = uint16_t(nCode);
				if (nCode == 0)
				{
					break;
				}
			}
			else if (nCode > 0x10FFFF)
			{
				*pDst++ = 0xFFFD;
			}
			else
			{
				if (pDst + 1 >= pDstEnd)
				{
					nResult = 1;
					break;
				}
				nCode -= 0x10000;
				*pDst++ = uint16_t(0xD800 + (nCode >> 10));
				*pDst++ = uint16_t(0xDC00 + (nCode & 0x3FF));
			}

			pSrc += nTrailing + 1;
			if (uintptr_t(pSrc) >= nSrcEnd)
			{
				nResult = -1;
				break;
			}
		}
	}

	if (pBytesConsumed)
	{
		*pBytesConsumed = int(pSrc - pSrcStart);
	}
	if (pUnitsWritten)
	{
		*pUnitsWritten = int(pDst - pDstStart);
	}
	return nResult;
}
