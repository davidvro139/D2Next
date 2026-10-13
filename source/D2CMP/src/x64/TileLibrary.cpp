// Native x64 tile library for D2CMP: the part of the original D2CMP.dll that D2Common's level generator uses.
//
// A tile library is a .dt1 file. Only its header and the tile headers are kept (the drawing data is not needed
// on the server side): a 0x114 byte file header followed by 0x60 bytes per tile. The tiles of a library are
// indexed in a hash keyed on (style, sequence, type), and a library that was loaded once is kept and shared by
// name. Transcribed from the disassembly of the 1.10f D2CMP.dll, including the order the tiles come back in
// (the most recently inserted first), because the level generator picks between tiles of the same kind by
// walking that list. Checked against the original by tools/x64/D2CmpTileTest.
//
// D2Common treats a tile as an opaque pointer that it only reads through the accessors below, so a tile here
// is the 0x60 byte file header followed by a native pointer, not the 32-bit layout the original patched in place.
#include <Windows.h>

#include <D2CMP.h>
#include <Fog.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr int kHashBuckets = 128;    // D2TileLibraryHashStrc::pNodes
constexpr int kMaxLibraries = 32;    // Slots of a D2TileLibraryHashStrc* array
constexpr uint32_t kFileHeaderSize = 0x114;
constexpr uint32_t kTileHeaderSize = 0x60;
constexpr int32_t kSupportedVersion = 7;
constexpr int32_t kNeedsSetupFlag = 2; // Bit of the minor version that is set in shipped files and cleared once loaded

#pragma pack(push, 1)
struct Tile
{
	int32_t nLightDirection;   // 0x00
	uint16_t nRoofHeight;      // 0x04
	uint16_t nFlags;           // 0x06 D2TileMaterialFlags
	int32_t nTotalHeight;      // 0x08
	int32_t nWidth;            // 0x0C
	int32_t nHeightToBottom;   // 0x10
	int32_t nType;             // 0x14
	int32_t nStyle;            // 0x18
	int32_t nSequence;         // 0x1C
	int32_t nRarity;           // 0x20
	int32_t nTransparentColor; // 0x24
	uint8_t subtileFlags[25];  // 0x28 collision flags of the 5x5 subtiles
	uint8_t pad0x41[7];        // 0x41
	int32_t nBlockHeadersOffset; // 0x48
	int32_t nBlockHeadersSize;   // 0x4C
	int32_t nBlocks;             // 0x50
	uint32_t unused0x54;         // 0x54 zero in every file
	uint32_t unused0x58;         // 0x58
	uint32_t unused0x5C;         // 0x5C zero in every file
	const char* szLibraryName;   // native pointer, after the file's 0x60 bytes
};
#pragma pack(pop)
static_assert(offsetof(Tile, nType) == 0x14 && offsetof(Tile, nRarity) == 0x20 && offsetof(Tile, subtileFlags) == 0x28, "DT1 tile header layout");
static_assert(offsetof(Tile, szLibraryName) == kTileHeaderSize, "tile header size");

struct Library
{
	std::string szName;
	std::vector<Tile> tiles;
	D2TileLibraryHashStrc* pHash = nullptr;
};

std::mutex gMutex;
std::vector<std::unique_ptr<Library>> gLibraries; // Loaded libraries, looked up by exact name

int bucketOf(const Tile& tile)
{
	return (2 * tile.nSequence - tile.nType + tile.nStyle) & (kHashBuckets - 1);
}

// Adds a tile to the hash. A node is per (style, sequence, type) and lists its tiles, newest first.
void insertTile(D2TileLibraryHashStrc* pHash, Tile* pTile)
{
	D2TileLibraryHashNodeStrc** ppBucket = &pHash->pNodes[bucketOf(*pTile)];

	D2TileLibraryHashNodeStrc* pNode = *ppBucket;
	while (pNode && !(pNode->nSequence == pTile->nSequence && pNode->nStyle == pTile->nStyle && pNode->nType == pTile->nType))
	{
		pNode = pNode->pPrev;
	}

	if (!pNode)
	{
		pNode = new D2TileLibraryHashNodeStrc{};
		pNode->nStyle = pTile->nStyle;
		pNode->nSequence = pTile->nSequence;
		pNode->nType = pTile->nType;
		pNode->pPrev = *ppBucket;
		*ppBucket = pNode;
		pNode->pRef = new D2TileLibraryHashRefStrc{ reinterpret_cast<D2TileLibraryEntryStrc*>(pTile), nullptr };
		return;
	}

	for (D2TileLibraryHashRefStrc* pRef = pNode->pRef; pRef; pRef = pRef->pPrev)
	{
		if (pRef->pTile == reinterpret_cast<D2TileLibraryEntryStrc*>(pTile))
		{
			return;
		}
	}
	pNode->pRef = new D2TileLibraryHashRefStrc{ reinterpret_cast<D2TileLibraryEntryStrc*>(pTile), pNode->pRef };
}

bool readAll(HSFILE hFile, void* pBuffer, size_t nSize)
{
	int nRead = 0;
	return FOG_FReadFile(hFile, pBuffer, nSize, &nRead, 0, 0, 0) && size_t(nRead) == nSize;
}

// Reads a .dt1 and builds its hash. Returns null (after reporting it) when the file cannot be used.
Library* loadLibrary(const char* szFileName)
{
	HSFILE hFile = nullptr;
	if (!FOG_FOpenFile(szFileName, &hFile))
	{
		return nullptr;
	}

	struct Closer
	{
		HSFILE h;
		~Closer() { FOG_FCloseFile(h); }
	} closer{ hFile };

	uint8_t header[kFileHeaderSize];
	if (!readAll(hFile, header, sizeof(header)))
	{
		return nullptr;
	}

	int32_t nVersion, nMinor, nTiles;
	memcpy(&nVersion, header + 0, 4);
	memcpy(&nMinor, header + 4, 4);
	memcpy(&nTiles, header + 0x10C, 4);
	if (nVersion != kSupportedVersion)
	{
		FOG_DisplayAssert("pLibrary->dwVersion == 7", __FILE__, __LINE__);
		return nullptr;
	}
	if (!(nMinor & kNeedsSetupFlag))
	{
		FOG_DisplayAssert("pLibrary->dwFlags & 2", __FILE__, __LINE__);
		return nullptr;
	}
	if (nTiles < 0 || nTiles > 0x100000)
	{
		FOG_DisplayAssert("pLibrary->nTiles", __FILE__, __LINE__);
		return nullptr;
	}

	auto pLibrary = std::make_unique<Library>();
	pLibrary->szName = szFileName;
	pLibrary->tiles.resize(size_t(nTiles));

	// The tile headers follow the file header directly; the original read exactly that much of the file.
	std::vector<uint8_t> raw(size_t(nTiles) * kTileHeaderSize);
	if (!raw.empty() && !readAll(hFile, raw.data(), raw.size()))
	{
		return nullptr;
	}

	pLibrary->pHash = new D2TileLibraryHashStrc{};
	for (int32_t i = 0; i < nTiles; ++i)
	{
		Tile& tile = pLibrary->tiles[size_t(i)];
		memcpy(&tile, raw.data() + size_t(i) * kTileHeaderSize, kTileHeaderSize);
		if (tile.unused0x54 != 0)
		{
			FOG_DisplayAssert("!pTile->pBlock", __FILE__, __LINE__);
		}
		if (tile.unused0x5C != 0)
		{
			FOG_DisplayAssert("!pTile->pCache", __FILE__, __LINE__);
		}
		tile.szLibraryName = pLibrary->szName.c_str();
		insertTile(pLibrary->pHash, &tile);
	}
	return pLibrary.release();
}

const Tile* asTile(const D2TileLibraryEntryStrc* pEntry, int nLine)
{
	if (!pEntry)
	{
		FOG_DisplayAssert("pTile", __FILE__, nLine);
	}
	return reinterpret_cast<const Tile*>(pEntry);
}
}

// Loads a tile library into the first free slot of an array of 32 hashes, or reuses a library that was already
// loaded with the same name.
void __stdcall D2CMP_10087_LoadTileLibrarySlot(D2TileLibraryHashStrc** ppTileLibraryHash, const char* szFileName)
{
	if (!ppTileLibraryHash)
	{
		FOG_DisplayAssert("ppTileHash", __FILE__, __LINE__);
		return;
	}
	if (!szFileName)
	{
		FOG_DisplayAssert("szFileName", __FILE__, __LINE__);
		return;
	}

	int nSlot = 0;
	while (nSlot < kMaxLibraries && ppTileLibraryHash[nSlot])
	{
		++nSlot;
	}
	if (nSlot >= kMaxLibraries)
	{
		FOG_DisplayAssert("nSlot < MAX_TILE_LIBRARIES", __FILE__, __LINE__);
		return;
	}

	std::lock_guard<std::mutex> lock(gMutex);
	for (const auto& pLibrary : gLibraries)
	{
		if (pLibrary->szName == szFileName)
		{
			ppTileLibraryHash[nSlot] = pLibrary->pHash;
			return;
		}
	}

	if (Library* pLibrary = loadLibrary(szFileName))
	{
		gLibraries.emplace_back(pLibrary);
		ppTileLibraryHash[nSlot] = pLibrary->pHash;
	}
}

// Collects the tiles of a (type, style, sequence) from every loaded library of the array, in slot order, up to
// nTileListSize of them. Returns how many.
int __stdcall D2CMP_10088_GetTiles(D2TileLibraryHashStrc** ppTileLibraryHash, int nType, int nStyle, int nSequence, D2TileLibraryEntryStrc** pTileList, int nTileListSize)
{
	if (!ppTileLibraryHash)
	{
		FOG_DisplayAssert("ppTileHash", __FILE__, __LINE__);
		return 0;
	}
	if (!pTileList || nTileListSize <= 0)
	{
		return 0;
	}

	int nFound = 0;
	for (int i = 0; i < kMaxLibraries && ppTileLibraryHash[i]; ++i)
	{
		const int nBucket = (2 * nSequence - nType + nStyle) & (kHashBuckets - 1);
		const D2TileLibraryHashNodeStrc* pNode = ppTileLibraryHash[i]->pNodes[nBucket];
		while (pNode && !(pNode->nSequence == nSequence && pNode->nStyle == nStyle && pNode->nType == nType))
		{
			pNode = pNode->pPrev;
		}
		for (const D2TileLibraryHashRefStrc* pRef = pNode ? pNode->pRef : nullptr; pRef && nFound < nTileListSize; pRef = pRef->pPrev)
		{
			pTileList[nFound++] = pRef->pTile;
		}
		if (nFound >= nTileListSize)
		{
			break;
		}
	}
	return nFound;
}

int __stdcall D2CMP_10077_GetTileType(D2TileLibraryEntryStrc* pTile)
{
	return asTile(pTile, __LINE__)->nType;
}

int __stdcall D2CMP_10078_GetTileStyle(D2TileLibraryEntryStrc* pTile)
{
	return asTile(pTile, __LINE__)->nStyle;
}

int __stdcall D2CMP_10079_GetTileFlags(D2TileLibraryEntryStrc* pTile)
{
	return asTile(pTile, __LINE__)->nFlags;
}

int __stdcall D2CMP_10081_GetTileRarity(D2TileLibraryEntryStrc* pTile)
{
	return asTile(pTile, __LINE__)->nRarity;
}

int __stdcall D2CMP_10082_GetTileSequence(D2TileLibraryEntryStrc* pTile)
{
	return asTile(pTile, __LINE__)->nSequence;
}

uint8_t* __stdcall D2CMP_10085_GetTileFlagArray(D2TileLibraryEntryStrc* pTile)
{
	return const_cast<uint8_t*>(asTile(pTile, __LINE__)->subtileFlags);
}

namespace
{
// The colour maps for item transformations, 256 bytes each: 105 per transformation type (21 colours, five
// shades), indexed by MixPalette. The original fills them when the palettes are loaded for drawing. Nothing on
// the server side reads their contents, only whether a map exists, so they stay zero until a renderer owns them.
constexpr size_t kMixedMapSize = 256;
constexpr int kMixedMapsPerTransform = 105;
constexpr int kTransformTypes = 9;
uint8_t gMixedMaps[size_t(kMixedMapsPerTransform) * kTransformTypes * kMixedMapSize];
}

// The colour map for a transformation type (1, 2 and 5..8 exist) and colour (0..20), or null.
uint8_t* __stdcall D2CMP_MixPalette(uint8_t nTrans, int nColor)
{
	if (nTrans >= kTransformTypes || nColor >= 21 || nTrans == 0 || (nTrans > 2 && nTrans <= 4))
	{
		return nullptr;
	}
	return gMixedMaps + (size_t(kMixedMapsPerTransform) * nTrans + size_t(nColor)) * kMixedMapSize;
}
