// Native x64 replacement for the "linker" functions of the original Fog.dll: the lookup tables the game builds
// while loading its data tables so that one table can refer to a row of another by code or by name.
//
// A linker (D2TxtLinkStrc) is in one of two modes and never both:
//   * code table: a sorted array of {4-character code, row index}, filled by FOG_10215 and read by FOG_GetLinkIndex
//   * name tree : a binary search tree of lower-cased names (32 characters at most), filled by
//                 FOG_10216_AddRecordToLinkingTable and read by FOG_GetRowFromTxt / FOG_10255
//
// Transcribed from the disassembly of the 1.10f Fog.dll, including its quirks: a duplicate code is bumped to the
// next free value, and a duplicate name still consumes a row index. Checked against the original by the
// differential test in tools/x64 (FogDiffTest, linker section).
#include <Windows.h>

#include <Fog.h>

#include <cstring>

namespace
{
constexpr int kNameLength = 32;               // Size of D2TxtLinkNodeStrc::szText, including the terminator
constexpr int kTableGrowth = 0x40;            // Entries added each time the code table is full
constexpr uint32_t kPrintableCodeLimit = 0x202020A0; // The original only logs failures for codes above this value

// Returned by FOG_10255 when a row has no name. Static, as in the original ("???").
char gNotFoundName[] = "???";

char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c + 0x20) : c;
}

void lowerCase(char* p, int nMax)
{
	for (int i = 0; i < nMax && p[i]; ++i)
	{
		p[i] = lower(p[i]);
	}
}

// The code as text for log messages: four characters, spaces shown as string terminators.
void codeToText(uint32_t dwCode, char* pOut)
{
	for (int i = 0; i < 4; ++i)
	{
		const char c = char((dwCode >> (8 * i)) & 0xFF);
		pOut[i] = c == ' ' ? 0 : c;
	}
	pOut[4] = 0;
}

void freeTree(D2TxtLinkNodeStrc* pNode)
{
	if (!pNode)
	{
		return;
	}
	freeTree(pNode->pPrevious);
	freeTree(pNode->pNext);
	FOG_Free(pNode, __FILE__, __LINE__, 0);
}

// Inserts a node keyed on its text. On a duplicate the node is logged and freed, as the original did.
void insertNode(D2TxtLinkNodeStrc** ppRoot, D2TxtLinkNodeStrc* pNew)
{
	D2TxtLinkNodeStrc* pCurrent = *ppRoot;
	while (pCurrent)
	{
		const int nCompare = strcmp(pNew->szText, pCurrent->szText);
		if (nCompare == 0)
		{
			FOG_Trace("Duplicate string index '%s'", pNew->szText);
			FOG_Free(pNew, __FILE__, __LINE__, 0);
			return;
		}
		ppRoot = nCompare < 0 ? &pCurrent->pPrevious : &pCurrent->pNext;
		pCurrent = *ppRoot;
	}
	*ppRoot = pNew;
}

// Pre-order search for the node holding a row index, or null.
D2TxtLinkNodeStrc* findByIndex(D2TxtLinkNodeStrc* pNode, int nIndex)
{
	if (!pNode)
	{
		return nullptr;
	}
	if (pNode->nLinkIndex == nIndex)
	{
		return pNode;
	}
	if (D2TxtLinkNodeStrc* pFound = findByIndex(pNode->pPrevious, nIndex))
	{
		return pFound;
	}
	return findByIndex(pNode->pNext, nIndex);
}
}

void* __stdcall FOG_AllocLinker(const char* szFile, int nLine)
{
	return FOG_Alloc(int(sizeof(D2TxtLinkStrc)), szFile, nLine, 0); // Memory is zero-filled
}

void __stdcall FOG_FreeLinker(void* pLinker)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);
	if (!p)
	{
		return;
	}
	freeTree(p->pFirstNode);
	if (p->pTbl)
	{
		FOG_Free(p->pTbl, __FILE__, __LINE__, 0);
	}
	FOG_Free(p, __FILE__, __LINE__, 0);
}

// Returns the row index of a code, or -1. bLogError makes a miss visible in the log (only for printable codes).
int __stdcall FOG_GetLinkIndex(void* pLinker, uint32_t dwCode, BOOL bLogError)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);
	if (!p || p->nRecords == 0 || !p->pTbl)
	{
		FOG_Trace("Invalid index for linked field");
	}

	if (p && p->pTbl)
	{
		int nLow = 0, nHigh = p->nRecords;
		while (nLow < nHigh)
		{
			const int nMid = nLow + (nHigh - nLow) / 2;
			const uint32_t dwEntry = p->pTbl[nMid].dwCode;
			if (dwCode > dwEntry)
			{
				nLow = nMid + 1;
			}
			else if (dwCode < dwEntry)
			{
				nHigh = nMid;
			}
			else
			{
				return p->pTbl[nMid].nLinkIndex;
			}
		}
	}

	if (bLogError && dwCode > kPrintableCodeLimit)
	{
		char szCode[5];
		codeToText(dwCode, szCode);
		FOG_Trace("Failed to find code '%s' in index", szCode);
	}
	return -1;
}

// Writes the code of a row index as text (four characters, trailing spaces removed). Returns 0 when there is none.
int __stdcall FOG_GetStringFromLinkIndex(void* pLinker, int nIndex, char* szString)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);
	if (!p || !p->pTbl || nIndex < 0 || nIndex >= p->nRecords)
	{
		return 0;
	}

	for (int i = 0; i < p->nRecords; ++i)
	{
		if (p->pTbl[i].nLinkIndex == nIndex)
		{
			codeToText(p->pTbl[i].dwCode, szString);
			return 1;
		}
	}
	return 0;
}

// Adds a code to the table (kept sorted) and returns the row index it was given, which is the number of rows
// added before it. A code that is already present is logged and bumped to the next free value.
int __stdcall FOG_10215(void* pLinker, int nCode)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);
	if (!p)
	{
		FOG_DisplayAssert("ptIndex", __FILE__, __LINE__);
		return -1;
	}
	if (p->pFirstNode)
	{
		FOG_DisplayAssert("!ptIndex->pStrTbl", __FILE__, __LINE__);
		return -1;
	}

	if (p->nAllocatedCells <= p->nRecords)
	{
		const int nOldCells = p->nAllocatedCells;
		const int nNewCells = nOldCells + kTableGrowth;
		p->pTbl = static_cast<D2TxtLinkTblStrc*>(FOG_ReallocPool(nullptr, p->pTbl, nNewCells * int(sizeof(D2TxtLinkTblStrc)), __FILE__, __LINE__, 0));
		memset(p->pTbl + nOldCells, 0, kTableGrowth * sizeof(D2TxtLinkTblStrc));
		p->nAllocatedCells = nNewCells;
	}

	const uint32_t dwOriginal = uint32_t(nCode);
	uint32_t dwCode = dwOriginal;
	int nLow = 0;
	for (;;)
	{
		nLow = 0;
		int nHigh = p->nRecords;
		bool bDuplicate = false;
		while (nLow < nHigh)
		{
			const int nMid = nLow + (nHigh - nLow) / 2;
			const uint32_t dwEntry = p->pTbl[nMid].dwCode;
			if (dwCode > dwEntry)
			{
				nLow = nMid + 1;
			}
			else if (dwCode < dwEntry)
			{
				nHigh = nMid;
			}
			else
			{
				bDuplicate = true;
				break;
			}
		}

		if (!bDuplicate)
		{
			break;
		}

		if (dwCode == dwOriginal && dwCode > kPrintableCodeLimit)
		{
			char szCode[5];
			codeToText(dwCode, szCode);
			FOG_Trace("Duplicate code index '%s'", szCode);
		}
		++dwCode;
	}

	if (nLow < p->nRecords)
	{
		memmove(p->pTbl + nLow + 1, p->pTbl + nLow, size_t(p->nRecords - nLow) * sizeof(D2TxtLinkTblStrc));
	}
	p->pTbl[nLow].dwCode = dwCode;
	p->pTbl[nLow].nLinkIndex = p->nRecords;
	++p->nRecords;
	return p->pTbl[nLow].nLinkIndex;
}

// Adds a name to the tree and returns the row index it was given. A duplicate name is logged and dropped, but it
// still uses up a row index so later rows keep their position.
int __stdcall FOG_10216_AddRecordToLinkingTable(void* pLinker, const char* szName)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);
	if (!p)
	{
		FOG_DisplayAssert("ptIndex", __FILE__, __LINE__);
		return -1;
	}
	if (p->pTbl)
	{
		FOG_DisplayAssert("!ptIndex->pCodeTbl", __FILE__, __LINE__);
		return -1;
	}

	auto* pNode = static_cast<D2TxtLinkNodeStrc*>(FOG_Alloc(int(sizeof(D2TxtLinkNodeStrc)), __FILE__, __LINE__, 0));
	strncpy_s(pNode->szText, kNameLength, szName ? szName : "", _TRUNCATE);
	lowerCase(pNode->szText, kNameLength);
	pNode->nLinkIndex = p->nRecords;

	const int nIndex = p->nRecords;
	insertNode(&p->pFirstNode, pNode);
	++p->nRecords;
	return nIndex;
}

// Row index of a name, or -1. nColumn nonzero makes a miss visible in the log (names starting with '*' never log).
int __stdcall FOG_GetRowFromTxt(void* pLinker, char* szText, int nColumn)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);

	char szKey[kNameLength];
	strncpy_s(szKey, kNameLength, szText ? szText : "", _TRUNCATE);
	lowerCase(szKey, kNameLength);

	if (p && p->nRecords != 0 && !p->pFirstNode)
	{
		FOG_DisplayAssert("ptIndex->pStrTbl", __FILE__, __LINE__);
	}

	if (p)
	{
		for (D2TxtLinkNodeStrc* pNode = p->pFirstNode; pNode;)
		{
			const int nCompare = strcmp(szKey, pNode->szText);
			if (nCompare == 0)
			{
				return pNode->nLinkIndex;
			}
			pNode = nCompare < 0 ? pNode->pPrevious : pNode->pNext;
		}
	}

	if (nColumn != 0 && szText && szText[0] && szText[0] != '*')
	{
		FOG_Trace("Failed to find string '%s' in index", szText);
	}
	return -1;
}

// Name (lower case) of a row index, or "???". a3 nonzero makes a failure visible in the log.
char* __stdcall FOG_10255(void* pLinker, int nId, int a3)
{
	auto* p = static_cast<D2TxtLinkStrc*>(pLinker);
	if (!p)
	{
		if (a3)
		{
			FOG_Trace("null index");
		}
		return gNotFoundName;
	}

	if (nId >= 0 && nId < p->nRecords)
	{
		if (D2TxtLinkNodeStrc* pNode = findByIndex(p->pFirstNode, nId))
		{
			return pNode->szText;
		}
	}

	if (a3)
	{
		FOG_Trace("index out of bounds");
	}
	return gNotFoundName;
}
