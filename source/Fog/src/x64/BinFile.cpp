// Native x64 replacement for Fog's tab-separated text table reader (#10207-#10210): what the game uses to build
// its data tables from the .txt files of DATA\GLOBAL\EXCEL when no .bin is used (as a mod does).
//
// Transcribed from the disassembly of the original Fog.dll, including its quirks: numbers are read digit by digit
// without checking for non-digits, an ASCII field that fills its length writes its terminator one byte past it, a
// row whose first cell is "Expansion" is dropped, and columns the table does not know are skipped.
#include <Windows.h>

#include <Fog.h>

#include <cstring>

namespace
{
constexpr int kMaxCells = 0x118;     // Columns of a text file, and fields of a table, at most
constexpr uint16_t kUnused = 0x7fff; // Column with no field

void halt(const char* szMessage)
{
	FOG_DisplayHalt(szMessage, __FILE__, __LINE__);
	exit(-1);
}

// Skips the rest of a cell and its terminator.
const char* skipCell(const char* p)
{
	while (*p)
	{
		++p;
	}
	return p + 1;
}

// Copies a cell into a buffer of nSize bytes (at most nSize - 1 characters and a terminator) and skips past it.
const char* copyCell(const char* p, char* szOut, int nSize)
{
	char* pOut = szOut;
	while (*p && pOut < szOut + nSize - 1)
	{
		*pOut++ = *p++;
	}
	*pOut = 0;
	return skipCell(p);
}

// The first four characters of a cell as a code, padded with spaces. Skips past the cell.
uint32_t readCode(const char*& p)
{
	uint8_t c[4] = {};
	int n = 0;
	while (*p && n < 4)
	{
		c[n++] = uint8_t(*p++);
	}
	p = skipCell(p);
	uint8_t code[4] = {' ', ' ', ' ', ' '};
	for (int i = 0; i < 4 && c[i]; ++i)
	{
		code[i] = c[i];
	}
	uint32_t dwCode;
	memcpy(&dwCode, code, 4);
	return dwCode;
}

// Row index of a code in a linker's code table, or -1 (the inlined search of the original: no log on a miss).
int findCode(const D2TxtLinkStrc* pLinker, uint32_t dwCode)
{
	if (!pLinker || !pLinker->pTbl)
	{
		return -1; // A linker D2MOO has not built (the original Fog would read through it)
	}
	int nLow = 0, nHigh = pLinker->nRecords;
	while (nLow < nHigh)
	{
		const int nMid = nLow + (nHigh - nLow) / 2;
		const uint32_t dwEntry = pLinker->pTbl[nMid].dwCode;
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
			return pLinker->pTbl[nMid].nLinkIndex;
		}
	}
	return -1;
}

// The row index of a name, added to the linker when it is not there yet.
int findOrAddName(void* pLinker, char* szName)
{
	if (!pLinker)
	{
		halt("ptIndex");
	}
	if (static_cast<D2TxtLinkStrc*>(pLinker)->pTbl)
	{
		halt("!ptIndex->pCodeTbl");
	}
	const int nIndex = FOG_GetRowFromTxt(pLinker, szName, 0);
	return nIndex >= 0 ? nIndex : FOG_10216_AddRecordToLinkingTable(pLinker, szName);
}

// For each column of the file, the index of the field it fills (case-insensitive name), or kUnused. The fields no
// column names follow, so that they get their defaults. Returns the number of entries.
int mapColumns(uint16_t* pMap, const D2BinFileStrc* pBinFile, const D2BinFieldStrc* pFields)
{
	int nFields = 0;
	bool used[kMaxCells] = {};
	for (; pFields[nFields].nFieldType != TXTFIELD_NONE; ++nFields)
	{
		if (nFields >= kMaxCells)
		{
			halt("Too many fields");
		}
	}

	int nEntries = 0;
	const char* p = reinterpret_cast<const char*>(pBinFile->pDataBuffer);
	for (int nColumn = 0; nColumn < pBinFile->nCellCount; ++nColumn)
	{
		// The original keeps the fields in hash buckets, the last added first: a name given twice matches the later one.
		int nMatch = -1;
		for (int i = nFields - 1; i >= 0; --i)
		{
			if (!used[i] && _stricmp(p, pFields[i].szFieldName) == 0)
			{
				nMatch = i;
				break;
			}
		}
		if (nMatch >= 0)
		{
			used[nMatch] = true;
			pMap[nEntries++] = uint16_t(nMatch);
		}
		else
		{
			pMap[nEntries++] = kUnused;
		}
		p = skipCell(p);
	}
	for (int i = 0; i < nFields; ++i)
	{
		if (!used[i])
		{
			pMap[nEntries++] = uint16_t(i);
		}
	}
	return nEntries;
}

void* linkerOf(const D2BinFieldStrc& field)
{
	return field.pLinkField ? *static_cast<void**>(field.pLinkField) : nullptr;
}
}

// Splits a text file in place: cells become strings, rows end with '\n'. The first row is the header. Rows whose
// first cell is "Expansion" (the separator between the classic and the expansion rows) are removed. Takes over
// the buffer (freed by FOG_FreeBinFile). Returns nullptr for a file with no rows after the header.
D2BinFileStrc* __stdcall FOG_CreateBinFile(void* pDataBuffer, int nBufferSize)
{
	auto* pBinFile = static_cast<D2BinFileStrc*>(FOG_Alloc(int(sizeof(D2BinFileStrc)), __FILE__, __LINE__, 0));
	pBinFile->pDataBuffer = static_cast<uint8_t*>(pDataBuffer);
	pBinFile->pData = nullptr;
	pBinFile->nRecordCount = 0;
	pBinFile->nCellCount = 0;

	char* p = static_cast<char*>(pDataBuffer);
	char* pEnd = p + nBufferSize;

	// Header
	bool bHeaderEnded = false;
	while (p < pEnd)
	{
		if (*p == '\t')
		{
			*p++ = 0;
			++pBinFile->nCellCount;
		}
		else if (*p == '\r')
		{
			if (p + 1 >= pEnd || p[1] != '\n')
			{
				halt("Text file line without a line feed");
			}
			*p = 0;
			p += 2;
			++pBinFile->nCellCount;
			bHeaderEnded = true;
			break;
		}
		else
		{
			++p;
		}
	}
	if (!bHeaderEnded || p >= pEnd)
	{
		FOG_Free(pBinFile, __FILE__, __LINE__, 0);
		return nullptr;
	}
	if (pBinFile->nCellCount > kMaxCells)
	{
		halt("Too many columns in text file");
	}
	pBinFile->pData = reinterpret_cast<uint8_t*>(p);

	// Rows
	char* pRow = p;
	while (p < pEnd)
	{
		if (*p == '\t')
		{
			*p++ = 0;
		}
		else if (*p == '\r')
		{
			if (p + 1 >= pEnd || p[1] != '\n')
			{
				halt("Text file line without a line feed");
			}
			*p = 0;
			p += 2;
			++pBinFile->nRecordCount;
			if (strncmp(pRow, "Expansion", 10) == 0)
			{
				--pBinFile->nRecordCount;
				memmove(pRow, p, size_t(pEnd - p));
				pEnd -= p - pRow;
				p = pRow;
			}
			pRow = p;
		}
		else
		{
			++p;
		}
	}
	return pBinFile;
}

void __stdcall FOG_FreeBinFile(D2BinFileStrc* pBinFile)
{
	void* pDataBuffer = pBinFile->pDataBuffer;
	FOG_Free(pDataBuffer, __FILE__, __LINE__, 0);
	FOG_Free(pBinFile, __FILE__, __LINE__, 0);
}

int __stdcall FOG_GetRecordCountFromBinFile(D2BinFileStrc* pBinFile)
{
	return pBinFile->nRecordCount;
}

// Fills nRecordCount records of nRecordSize bytes from the rows of a text file, as the fields describe them.
// A first pass adds the rows' own codes and names to their linkers (so that the second can look up rows of the
// same table), the second converts the cells; fields no column names get their default.
void __stdcall FOG_10207(D2BinFileStrc* pBinFile, D2BinFieldStrc* pBinField, void* pTxt, int nRecordCount, int nRecordSize)
{
	(void)nRecordCount; // The original walks the rows of the file
	uint16_t map[2 * kMaxCells];
	const int nEntries = mapColumns(map, pBinFile, pBinField);
	auto* pRecords = static_cast<uint8_t*>(pTxt);

	bool bLinkPass = false;
	for (int nColumn = 0; nColumn < pBinFile->nCellCount; ++nColumn)
	{
		if (map[nColumn] == kUnused)
		{
			continue;
		}
		switch (pBinField[map[nColumn]].nFieldType)
		{
		case 10: case 12: case 14: case 16: case 17: case 18:
			bLinkPass = true;
			break;
		default:
			break;
		}
	}

	if (bLinkPass)
	{
		const char* p = reinterpret_cast<const char*>(pBinFile->pData);
		uint8_t* pRecord = pRecords;
		for (int nRow = 0; nRow < pBinFile->nRecordCount; ++nRow)
		{
			for (int nColumn = 0; nColumn < pBinFile->nCellCount; ++nColumn)
			{
				if (map[nColumn] == kUnused)
				{
					p = skipCell(p);
					continue;
				}
				const D2BinFieldStrc& field = pBinField[map[nColumn]];
				uint8_t* pOut = pRecord + field.nFieldOffset;
				switch (field.nFieldType)
				{
				case 10: case 12: case 14:
				{
					const uint32_t dwCode = readCode(p);
					const int nIndex = FOG_10215(linkerOf(field), int(dwCode));
					if (field.nFieldType == 10)
					{
						memcpy(pOut, &dwCode, 4);
					}
					else if (field.nFieldType == 12)
					{
						if (nIndex > 0xff)
						{
							halt("Too many codes for a byte field");
						}
						*pOut = uint8_t(dwCode);
					}
					else
					{
						if (nIndex > 0xffff)
						{
							halt("Too many codes for a word field");
						}
						const uint16_t wCode = uint16_t(dwCode);
						memcpy(pOut, &wCode, 2);
					}
					break;
				}
				case 16:
					p = copyCell(p, reinterpret_cast<char*>(pOut), field.nFieldLength);
					FOG_10216_AddRecordToLinkingTable(linkerOf(field), reinterpret_cast<char*>(pOut));
					break;
				case 17: case 18:
				{
					char szName[32];
					p = copyCell(p, szName, sizeof(szName));
					const int nIndex = findOrAddName(linkerOf(field), szName);
					uint8_t* pIndexOut = pRecord + field.nFieldOffset;
					if (field.nFieldType == 17)
					{
						const uint16_t wIndex = uint16_t(nIndex);
						memcpy(pIndexOut, &wIndex, 2);
					}
					else
					{
						memcpy(pIndexOut, &nIndex, 4);
					}
					break;
				}
				default:
					p = skipCell(p);
					break;
				}
			}
			if (*p != '\n')
			{
				halt("Text file row with too many cells");
			}
			++p;
			pRecord += nRecordSize;
		}
	}

	const char* p = reinterpret_cast<const char*>(pBinFile->pData);
	uint8_t* pRecord = pRecords;
	for (int nRow = 0; nRow < pBinFile->nRecordCount; ++nRow)
	{
		int nColumn = 0;
		for (; nColumn < pBinFile->nCellCount; ++nColumn)
		{
			if (map[nColumn] == kUnused)
			{
				p = skipCell(p);
				continue;
			}
			const D2BinFieldStrc& field = pBinField[map[nColumn]];
			uint8_t* pOut = pRecord + field.nFieldOffset;
			const char* pCell = p;
			switch (field.nFieldType)
			{
			case 1: case 7: // ASCII: the terminator lands one past the field when the text fills it
			{
				uint8_t* pLimit = pOut + field.nFieldLength;
				while (*p && pOut < pLimit)
				{
					*pOut++ = uint8_t(*p++);
				}
				*pOut = 0;
				p = skipCell(p);
				break;
			}
			case 2: case 3: case 4: case 5: case 6: case 8: case 26:
			{
				if (*p == '-')
				{
					++p;
				}
				int nValue = 0;
				while (*p)
				{
					nValue = nValue * 10 + (*p++ - '0');
				}
				if (*pCell == '-')
				{
					nValue = -nValue;
				}
				++p;
				switch (field.nFieldType)
				{
				case 2: case 8:
					memcpy(pOut, &nValue, 4);
					break;
				case 3:
				{
					const uint16_t w = uint16_t(nValue);
					memcpy(pOut, &w, 2);
					break;
				}
				case 5: // Left alone when the cell is empty
					if (pCell + 1 != p)
					{
						*pOut = uint8_t(nValue);
					}
					break;
				case 4: case 6:
					*pOut = uint8_t(nValue);
					break;
				case 26: // Bit field: the length is the bit index
				{
					uint8_t* pByte = pOut + (field.nFieldLength >> 3);
					const uint8_t mask = uint8_t(1u << (field.nFieldLength & 7));
					*pByte = nValue ? uint8_t(*pByte | mask) : uint8_t(*pByte & ~mask);
					break;
				}
				}
				break;
			}
			case 9: // Raw code
			{
				const uint32_t dwCode = readCode(p);
				memcpy(pOut, &dwCode, 4);
				break;
			}
			case 11: case 13: case 15:
			{
				const uint32_t dwCode = readCode(p);
				const int nIndex = findCode(static_cast<D2TxtLinkStrc*>(linkerOf(field)), dwCode);
				if (field.nFieldType == 11)
				{
					memcpy(pOut, &nIndex, 4);
				}
				else if (field.nFieldType == 13)
				{
					*pOut = uint8_t(nIndex);
				}
				else
				{
					const uint16_t w = uint16_t(nIndex);
					memcpy(pOut, &w, 2);
				}
				break;
			}
			case 19: case 20: case 21:
			{
				char szName[32];
				p = copyCell(p, szName, sizeof(szName));
				const int nIndex = FOG_GetRowFromTxt(linkerOf(field), szName, 1);
				if (field.nFieldType == 19)
				{
					memcpy(pOut, &nIndex, 4);
				}
				else if (field.nFieldType == 20)
				{
					const uint16_t w = uint16_t(nIndex);
					memcpy(pOut, &w, 2);
				}
				else
				{
					*pOut = uint8_t(nIndex);
				}
				break;
			}
			case 22: // Key: the callback turns the text into a word (a string id)
			{
				char szText[256];
				p = copyCell(p, szText, sizeof(szText));
				if (field.pLinkField)
				{
					const uint16_t w = reinterpret_cast<uint16_t(__fastcall*)(char*)>(field.pLinkField)(szText);
					memcpy(pOut, &w, 2);
				}
				break;
			}
			case 23: case 24: case 25: // Custom: the callback fills the record
			{
				char szText[256];
				p = copyCell(p, szText, sizeof(szText));
				if (field.pLinkField)
				{
					reinterpret_cast<void(__fastcall*)(char*, void*, int, int, int, int)>(field.pLinkField)(szText, pRecord, field.nFieldOffset, field.nFieldLength, nRow, nColumn);
				}
				break;
			}
			default: // 10, 12, 14, 16-18: filled by the first pass
				p = skipCell(p);
				break;
			}
		}

		// Fields no column names
		for (int nEntry = nColumn; nEntry < nEntries; ++nEntry)
		{
			const D2BinFieldStrc& field = pBinField[map[nEntry]];
			uint8_t* pOut = pRecord + field.nFieldOffset;
			auto put = [pOut](auto value) { memcpy(pOut, &value, sizeof(value)); };
			switch (field.nFieldType)
			{
			case 2: case 8: case 9: case 10: case 18:
				put(uint32_t(0));
				break;
			case 11: case 19:
				put(uint32_t(0xffffffff));
				break;
			case 3: case 14: case 17: case 22:
				put(uint16_t(0));
				break;
			case 15: case 20:
				put(uint16_t(0xffff));
				break;
			case 13: case 21:
				put(uint8_t(0xff));
				break;
			case 23: case 24: case 25:
				if (field.pLinkField)
				{
					reinterpret_cast<void(__fastcall*)(char*, void*, int, int, int, int)>(field.pLinkField)(nullptr, pRecord, field.nFieldOffset, field.nFieldLength, nRow, nEntry);
				}
				break;
			case 26:
				break;
			default: // 1, 4-7, 12, 16
				put(uint8_t(0));
				break;
			}
		}

		if (*p != '\n')
		{
			halt("Text file row with too many cells");
		}
		++p;
		pRecord += nRecordSize;
	}
}
