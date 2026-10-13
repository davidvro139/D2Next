#include "PLAYER/PlrSave.h"

#include <Fog.h>
#include <File.h>
#include <CRC.h>
#include <D2BitManip.h>

#include <D2Items.h>
#include <D2Monsters.h>
#include <D2QuestRecord.h>
#include <D2Skills.h>
#include <D2States.h>
#include <D2StatList.h>
#include <D2Waypoints.h>

#include "GAME/Arena.h"
#include "GAME/Clients.h"
#include "GAME/Game.h"
#include "GAME/SCmd.h"
#include "ITEMS/ItemMode.h"
#include "ITEMS/Items.h"
#include "MONSTER/MonsterAI.h"
#include "MONSTER/Monster.h"
#include "PLAYER/Player.h"
#include "PLAYER/PlayerPets.h"
#include "PLAYER/PlayerStats.h"
#include "PLAYER/PlrIntro.h"
#include "PLAYER/PlrSave2.h"
#include "PLAYER/PlrTrade.h"
#include "QUESTS/Quests.h"
#include "SKILLS/Skills.h"
#include "UNIT/SUnit.h"
#include "UNIT/SUnitMsg.h"
#include "UNIT/SUnitNpc.h"


int32_t gbWriteSaveFile_6FD30E08;
int32_t dword_6FD4DC28;


//D2Game.0x6FC895D0 (#10036)
void __fastcall D2GAME_10036_PLRSAVE_EnableSaveFileWriting(int32_t bWriteSaveFile)
{
    gbWriteSaveFile_6FD30E08 = bWriteSaveFile;
}

//D2Game.0x6FC895E0
int32_t __fastcall sub_6FC895E0(D2GameStrc* pGame, D2UnitStrc* pPlayer, D2SavedItemStrc* pSavedItem, D2UnitStrc** ppItem, int32_t* a5)
{
    if (a5)
    {
        *a5 = 0;
    }

    D2UnitStrc* pItem = sub_6FC897F0(pGame, pSavedItem);
    if (!pItem)
    {
        return 12;
    }

    switch (pItem->dwAnimMode)
    {
    case IMODE_STORED:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        UNITS_SetXForStaticUnit(pItem, pSavedItem->nX);
        UNITS_SetYForStaticUnit(pItem, pSavedItem->nY);

        if (!D2GAME_PlaceItem_6FC44410(__FILE__, __LINE__, pGame, pPlayer, pItem->dwUnitId, CLIENTS_GetUnitX(pItem), CLIENTS_GetUnitY(pItem), 0, 1, 0))
        {
            return 12;
        }

        break;
    }
    case IMODE_EQUIP:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        int32_t nUnused = 0;
        if (!sub_6FC45060(pGame, pPlayer, pItem->dwUnitId, pSavedItem->nBodyLoc, 1, &nUnused))
        {
            return 12;
        }

        break;
    }
    case IMODE_INBELT:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        UNITS_SetXForStaticUnit(pItem, pSavedItem->nX);
        UNITS_SetYForStaticUnit(pItem, pSavedItem->nY);
        int32_t nUnused = 0;
        if (!sub_6FC48940(pGame, pPlayer, pItem->dwUnitId, pSavedItem->nX, 0, &nUnused))
        {
            return 12;
        }

        break;
    }
    case IMODE_ONCURSOR:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        INVENTORY_SetCursorItem(pPlayer->pInventory, pItem);
        if (!sub_6FC4B9D0(pGame, pPlayer, pItem))
        {
            return 12;
        }

        break;
    }
    case IMODE_SOCKETED:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        int32_t nUnused = 0;
        if (!*ppItem || !D2GAME_ITEMSOCKET_PlaceItem_6FC497E0(pGame, pPlayer, pItem->dwUnitId, (*ppItem)->dwUnitId, &nUnused, 0, 1u, 0))
        {
            return 12;
        }
        break;
    }
    default:
        return 12;
    }

    if (pSavedItem->unk0x01)
    {
        *ppItem = pItem;
    }

    if (a5)
    {
        *a5 = pSavedItem->unk0x01;
    }

    return 0;
}

//D2Game.0x6FC897F0
D2UnitStrc* __fastcall sub_6FC897F0(D2GameStrc* pGame, D2SavedItemStrc* pSavedItem)
{
    D2ItemDropStrc itemDrop = {};

    itemDrop.nId = pSavedItem->nItemId;
    itemDrop.nItemLvl = pSavedItem->nItemLevel;
    itemDrop.nX = pSavedItem->nX;
    itemDrop.nY = pSavedItem->nY;
    itemDrop.nQuantity = pSavedItem->nQuantity;
    itemDrop.wItemFormat = pSavedItem->nItemFormat;
    itemDrop.nQuality = pSavedItem->nItemQuality;
    itemDrop.nMinDur = pSavedItem->nMinDurability;
    itemDrop.nMaxDur = pSavedItem->nMaxDurability;
    itemDrop.nSpawnType = pSavedItem->nSpawnType;
    itemDrop.dwItemSeed = pSavedItem->nItemSeed;
    itemDrop.wUnitInitFlags = 1;
    itemDrop.dwSeed = pSavedItem->nSeed;
    itemDrop.nItemIndex = pSavedItem->nItemIndex;
    itemDrop.bForce = 1;
    itemDrop.pGame = pGame;
    itemDrop.pRoom = 0;
    itemDrop.dwFlags1 = pSavedItem->nItemFlags;
    
    strcpy_s(itemDrop.szName, pSavedItem->szName);

    itemDrop.eEarLvl = pSavedItem->nEarLevel;

    D2UnitStrc* pItem = D2GAME_CreateItemEx_6FC4ED80(pGame, &itemDrop, 0);
    if (pItem)
    {
        ITEMS_SetFileIndex(pItem, pSavedItem->nItemIndex);
        ITEMS_SetInvPage(pItem, pSavedItem->nInvPage);
    }

    return pItem;
}

//D2Game.0x6FC898F0
int32_t __fastcall sub_6FC898F0(D2GameStrc* pGame, D2UnitStrc* pPlayer, D2UnitStrc* pItem, D2UnitStrc* a4)
{
    if (!pItem)
    {
        return 12;
    }

    switch (pItem->dwAnimMode)
    {
    case IMODE_STORED:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        if (!D2GAME_PlaceItem_6FC44410(__FILE__, __LINE__, pGame, pPlayer, pItem->dwUnitId, CLIENTS_GetUnitX(pItem), CLIENTS_GetUnitY(pItem), 0, 1, 0))
        {
            return 12;
        }
        return 0;
    }
    case IMODE_EQUIP:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        int32_t nItemGUID = -1;
        if (pItem)
        {
            nItemGUID = pItem->dwUnitId;
        }

        int32_t nUnused = 0;
        if (!sub_6FC45060(pGame, pPlayer, nItemGUID, ITEMS_GetBodyLocation(pItem), 1, &nUnused))
        {
            return 12;
        }
        return 0;
    }
    case IMODE_INBELT:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        int32_t nUnused = 0;
        if (!sub_6FC48940(pGame, pPlayer, pItem->dwUnitId, CLIENTS_GetUnitX(pItem), 0, &nUnused))
        {
            return 12;
        }
        return 0;
    }
    case IMODE_ONCURSOR:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);
        INVENTORY_SetCursorItem(pPlayer->pInventory, pItem);
        if (!sub_6FC4B9D0(pGame, pPlayer, pItem))
        {
            return 12;
        }
        return 0;
    }
    case IMODE_SOCKETED:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);

        int32_t nUnused = 0;
        if (!a4 || !D2GAME_ITEMSOCKET_PlaceItem_6FC497E0(pGame, pPlayer, pItem ? pItem->dwUnitId : -1, a4->dwUnitId, &nUnused, 0, 0, 0))
        {
            return 12;
        }

        return 0;
    }
    default:
        return 12;
    }
}

//D2Game.0x6FC89AB0 (#10029)
int32_t __stdcall D2GAME_10029_SAVE_WriteFileInterface(D2GameStrc* pGame, D2UnitStrc* pPlayer, const char* szName)
{
    return D2GAME_SAVE_WriteFile_6FC8A500(pGame, pPlayer, szName, 0);
}

//D2Game.0x6FC89AD0
int32_t __stdcall D2GAME_SAVE_SerializeItem_6FC89AD0(D2UnitStrc* pItem, uint8_t* pBitstream, uint32_t nBitstreamSize, int32_t n0, int32_t a5)
{
    int32_t nBitsLeft = 0;

    if (pItem && !(pItem->dwFlagEx & 0x8000)/* && (n0 || !a5 || D2GAME_Return1_6FC41900())*/)
    {
        nBitsLeft = ITEMS_SerializeItemToBitstream(pItem, pBitstream, nBitstreamSize, 1, 1, 0);

        if (!nBitsLeft)
        {
            FOG_DisplayAssert("!\"Character has too many items\"", __FILE__, __LINE__);
            exit(-1);
        }
    }

    return nBitsLeft;
}

//D2Game.0x6FC89B50
int32_t __stdcall PLRSAVE_WriteItems_6FC89B50(D2InventoryStrc* pInventory, uint8_t* pBitstream, uint32_t nBitstreamSize, int32_t a4, int32_t a5)
{
    if (nBitstreamSize < 2)
    {
        return 0;
    }

    *(WORD*)pBitstream = 'MJ';

    int32_t nItemCount = 0;
    if (pInventory)
    {
        if (INVENTORY_GetCursorItem(pInventory))
        {
            nItemCount = 1;
        }

        for (D2UnitStrc* pItem = INVENTORY_GetFirstItem(pInventory); pItem; pItem = INVENTORY_GetNextItem(pItem))
        {
            if (INVENTORY_UnitIsItem(pItem))
            {
                ++nItemCount;
            }
        }
    }

    if (nBitstreamSize < 4)
    {
        return 2;
    }

    uint8_t* pItemCount = pBitstream + 2;
    *(uint16_t*)pItemCount = nItemCount;

    uint8_t* pData = pItemCount + 2;
    if (!pInventory)
    {
        return pData - pBitstream;
    }

    int32_t nSize = 0;
    for (D2UnitStrc* pItem = INVENTORY_GetFirstItem(pInventory); pItem; pItem = INVENTORY_GetNextItem(pItem))
    {
        D2UnitStrc* pCheckedItem = INVENTORY_UnitIsItem(pItem);

        const uint8_t nBodyLoc = ITEMS_GetBodyLocation(pCheckedItem);
        if (INVENTORY_GetItemNodePage(pItem) != 3 || nBodyLoc != BODYLOC_RARM && nBodyLoc != BODYLOC_LARM)
        {
            nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pCheckedItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
            if (nSize < 0)
            {
                return -1;
            }

            pData += nSize;

            if (!nSize && (uint16_t)nItemCount > 0u)
            {
                nItemCount += UINT16_MAX;
            }
        }
    }

    D2UnitStrc* pWeapon = INVENTORY_GetLeftHandWeapon(pInventory);
    D2UnitStrc* pRightHandItem = INVENTORY_GetItemFromBodyLoc(pInventory, BODYLOC_RARM);
    D2UnitStrc* pLeftHandItem = INVENTORY_GetItemFromBodyLoc(pInventory, BODYLOC_LARM);
    if (pWeapon)
    {
        if (pRightHandItem == pWeapon)
        {
            nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pRightHandItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
            if (nSize < 0)
            {
                return -1;
            }

            pData += nSize;

            if (!nSize && (uint16_t)nItemCount > 0u)
            {
                nItemCount += UINT16_MAX;
            }

            if (pLeftHandItem)
            {
                nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pLeftHandItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
                if (nSize < 0)
                {
                    return -1;
                }

                pData += nSize;

                if (!nSize && (uint16_t)nItemCount > 0u)
                {
                    nItemCount += UINT16_MAX;
                }
            }
        }
        else
        {
            nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pLeftHandItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
            if (nSize < 0)
            {
                return -1;
            }

            pData += nSize;

            if (!nSize && (uint16_t)nItemCount > 0u)
            {
                nItemCount += UINT16_MAX;
            }

            if (pRightHandItem)
            {
                nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pRightHandItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
                if (nSize < 0)
                {
                    return -1;
                }

                pData += nSize;

                if (!nSize && (uint16_t)nItemCount > 0u)
                {
                    nItemCount += UINT16_MAX;
                }
            }
        }
    }
    else
    {
        if (pRightHandItem)
        {
            nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pRightHandItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
            if (nSize < 0)
            {
                return -1;
            }

            pData += nSize;

            if (!nSize && (uint16_t)nItemCount > 0u)
            {
                nItemCount += UINT16_MAX;
            }
        }

        if (pLeftHandItem)
        {
            nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pLeftHandItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
            if (nSize < 0)
            {
                return -1;
            }

            pData += nSize;

            if (!nSize && (uint16_t)nItemCount > 0u)
            {
                nItemCount += UINT16_MAX;
            }
        }
    }

    D2UnitStrc* pCursorItem = INVENTORY_GetCursorItem(pInventory);
    if (pCursorItem && pCursorItem->dwUnitType == UNIT_ITEM)
    {
        nSize = D2GAME_SAVE_SerializeItem_6FC89AD0(pCursorItem, pData, nBitstreamSize + pBitstream - pData, a4, a5);
        if (nSize < 0)
        {
            return -1;
        }

        pData += nSize;

        if (!nSize && (uint16_t)nItemCount > 0u)
        {
            nItemCount += UINT16_MAX;
        }
    }

    *(WORD*)pItemCount = nItemCount;

    return pData - pBitstream;
}

//D2Game.0x6FC8A0F0
int32_t __fastcall sub_6FC8A0F0(D2GameStrc* pGame, D2UnitStrc* pUnit, uint8_t* pSection, int32_t nSize, int32_t a5, int32_t a6)
{
    if (a5 && pUnit && pUnit->dwUnitType == UNIT_PLAYER)
    {
        const int32_t result = PLRTRADE_CopyTradeSaveDataToBuffer(pUnit, pSection, nSize);
        if (result != 0)
        {
            return result;
        }
    }

    return PLRSAVE_WriteItems_6FC89B50(pUnit->pInventory, pSection, nSize, a5, a6);
}

//D2Game.0x6FC8A140
int32_t __fastcall D2GAME_SAVE_CalculateChecksum_6FC8A140(D2SaveHeaderStrc* pSaveHeader, int32_t nSize)
{
    int32_t nCheckSum = 0;
    int32_t nLastTime = 0;
    int32_t nPlayTime = 0;
    int32_t nMapSeed = 0;
    if (pSaveHeader->dwVersion > 91)
    {
        nCheckSum = pSaveHeader->dwChecksum;
        nLastTime = pSaveHeader->dwLasTime;
        nPlayTime = pSaveHeader->dwPlayTime;
        nMapSeed = pSaveHeader->dwMapSeed;

        pSaveHeader->dwChecksum = 0;
        pSaveHeader->dwLasTime = 0;
        pSaveHeader->dwPlayTime = 0;
        pSaveHeader->dwMapSeed = 0;
    }

    const int32_t result = CRC32_Compute(pSaveHeader, nSize);

    if (pSaveHeader->dwVersion > 91)
    {
        pSaveHeader->dwChecksum = nCheckSum;
        pSaveHeader->dwLasTime = nLastTime;
        pSaveHeader->dwPlayTime = nPlayTime;
        pSaveHeader->dwMapSeed = nMapSeed;
    }

    return result;
}

//D2Game.0x6FC8A1B0
int32_t __fastcall D2GAME_SAVE_WriteFileOnRealm_6FC8A1B0(D2GameStrc* pGame, D2UnitStrc* pPlayer, const char* szCharName, char* szAccountName, int32_t bInteractsWithPlayer, int32_t nCharSaveTransactionToken, int32_t a7, D2ClientInfoStrc* pClientInfo)
{
    if (!gbWriteSaveFile_6FD30E08)
    {
        return 0;
    }

    uint8_t pSaveData[8192] = {};
    uint32_t nFileSize = 0;
    PLRSAVE2_CreateSaveFile(pGame, pPlayer, &pSaveData[2], &nFileSize, 8192, bInteractsWithPlayer, a7);

    if (nFileSize <= 0)
    {
        FOG_DisplayHalt("Unable to save player to stream.\n", __FILE__, __LINE__);
        exit(-1);
    }

    *(uint16_t*)pSaveData = nFileSize + 2;

    D2ClientStrc* pClient = SUNIT_GetClientFromPlayer(pPlayer, __FILE__, __LINE__);
    
    const uint32_t nCalculatedChecksum = D2GAME_SAVE_CalculateChecksum_6FC8A140((D2SaveHeaderStrc*)&pSaveData[2], nFileSize);
    if (nCalculatedChecksum == D2GAME_GetSaveChecksumFromClient_6FC33940(pClient))
    {
        D2SaveHeaderStrc* pOldSaveData = CLIENTS_GetSaveHeader(pClient);
        const uint32_t nOldFileSize = CLIENTS_GetSaveHeaderSize(pClient);

        D2_ASSERT(pOldSaveData);

        if (nOldFileSize == nFileSize)
        {
            const uint32_t nOldVersion = pOldSaveData->dwVersion;
            uint32_t nOldChecksum = 0;
            uint32_t nOldLastTime = 0;
            uint32_t nOldPlayTime = 0;
            uint32_t nOldMapSeed = 0;
            if (nOldVersion > 91)
            {
                nOldChecksum = pOldSaveData->dwChecksum;
                nOldLastTime = pOldSaveData->dwLasTime;
                nOldPlayTime = pOldSaveData->dwPlayTime;
                nOldMapSeed = pOldSaveData->dwMapSeed;
                pOldSaveData->dwChecksum = 0;
                pOldSaveData->dwLasTime = 0;
                pOldSaveData->dwPlayTime = 0;
                pOldSaveData->dwMapSeed = 0;
            }

            const uint32_t nNewVersion = *(uint32_t*)&pSaveData[6];
            uint32_t nNewChecksum = 0;
            uint32_t nNewLastTime = 0;
            uint32_t nNewPlayTime = 0;
            uint32_t nNewMapSeed = 0;
            if (nNewVersion > 91u)
            {
                nNewChecksum = *(uint32_t*)&pSaveData[14];
                nNewLastTime = *(uint32_t*)&pSaveData[50];
                nNewPlayTime = *(uint32_t*)&pSaveData[54];
                nNewMapSeed = *(uint32_t*)&pSaveData[173];
                *(uint32_t*)&pSaveData[14] = 0;
                *(uint32_t*)&pSaveData[50] = 0;
                *(uint32_t*)&pSaveData[54] = 0;
                *(uint32_t*)&pSaveData[173] = 0;
            }

            bool bDataSameAsPreviousSave = true;
            for (int32_t i = 0; i < nOldFileSize; ++i)
            {
                if (((uint8_t*)pOldSaveData)[i] != pSaveData[i + 2])
                {
					bDataSameAsPreviousSave = false;
                    break;
                }
            }

            if (nOldVersion > 91)
            {
                pOldSaveData->dwChecksum = nOldChecksum;
                pOldSaveData->dwLasTime = nOldLastTime;
                pOldSaveData->dwPlayTime = nOldPlayTime;
                pOldSaveData->dwMapSeed = nOldMapSeed;
            }

            if (nNewVersion > 91)
            {
                *(uint32_t*)&pSaveData[14] = nNewChecksum;
                *(uint32_t*)&pSaveData[50] = nNewLastTime;
                *(uint32_t*)&pSaveData[54] = nNewPlayTime;
                *(uint32_t*)&pSaveData[173] = nNewMapSeed;
            }

            if (bDataSameAsPreviousSave)
            {
				// Since we have the same save file, relock instead of saving
                ++dword_6FD4DC28;

                if (gpD2EventCallbackTable_6FD45830 && gpD2EventCallbackTable_6FD45830->pfRelockDatabaseCharacter)
                {
                    gpD2EventCallbackTable_6FD45830->pfRelockDatabaseCharacter(&pClientInfo, szCharName, szAccountName);
                }

                D2GAME_SetClientsRealmId_6FC346C0(pClient, pClientInfo);
                return FALSE;
            }
        }

        GAME_LogMessage(7, "[SAVE] *** %s len:%d checksums matched but data didn't ***", szCharName, nFileSize);
    }

    D2GAME_SetClientsRealmId_6FC346C0(pClient, pClientInfo);
    D2GAME_SetSaveFileChecksum_6FC33970(pClient, nCalculatedChecksum);

    if (gpD2EventCallbackTable_6FD45830 && gpD2EventCallbackTable_6FD45830->pfSaveDatabaseCharacter)
    {
        if (IsBadCodePtr((FARPROC)gpD2EventCallbackTable_6FD45830->pfSaveDatabaseCharacter))
        {
            FOG_DisplayAssert("ptEventCallbackTable->fpSaveDatabaseCharacter", __FILE__, __LINE__);
            exit(-1);
        }

        gpD2EventCallbackTable_6FD45830->pfSaveDatabaseCharacter(&pClientInfo, szCharName, szAccountName, pSaveData, nFileSize + 2, nCharSaveTransactionToken);
        CLIENTS_CopySaveDataToClient(pClient, &pSaveData[2], nFileSize);
    }

    return TRUE;
}

//D2Game.0x6FC8A500
int32_t __fastcall D2GAME_SAVE_WriteFile_6FC8A500(D2GameStrc* pGame, D2UnitStrc* pPlayer, const char* szName, DWORD dwArg)
{
    if (!pPlayer)
    {
        return 0;
    }

    pPlayer->dwFlagEx &= ~UNITFLAGEX_UPDATEINV;

    if (STATES_CheckState(pPlayer, STATE_PLAYERBODY))
    {
        FOG_Trace("[SAVE ERROR] Tried to save a dead body!\n");
    }

    int32_t bInteractsWithPlayer = 0;
    int32_t nUnitType = 6;
    int32_t nUnitGUID = 0;
    if (SUNIT_GetInteractInfo(pPlayer, &nUnitType, &nUnitGUID) && nUnitType == UNIT_PLAYER)
    {
        bInteractsWithPlayer = 1;
    }

    D2ClientStrc* pClient = SUNIT_GetClientFromPlayer(pPlayer, __FILE__, __LINE__);
    char szAccountName[50] = {};
    CLIENTS_CopyAccountNameToBuffer(pClient, szAccountName);
    int32_t nCharSaveTransactionToken = 0;
    D2GAME_GetCharSaveTransactionToken_6FC346A0(pClient, &nCharSaveTransactionToken);
    D2ClientInfoStrc* pClientInfo = nullptr;
    D2GAME_GetRealmIdFromClient_6FC346B0(pClient, &pClientInfo);

    uint8_t pSaveData[8192] = {};
    uint32_t nFileSize = 0;

    if (pGame->nGameType == 1 || pGame->nGameType == 2)
    {
        if (CLIENTS_GetSaveHeader(pClient))
        {
            CLIENTS_FreeSaveHeader(pClient);
        }

        PLRSAVE2_CreateSaveFile(pGame, pPlayer, pSaveData, &nFileSize, 0x2000u, bInteractsWithPlayer, 0);

        D2_ASSERT(nFileSize > 0);

        CLIENTS_AttachSaveFile(CLIENTS_GetClientId(pClient), pSaveData, nFileSize, nFileSize, 0, 0, 0);
        D2GAME_SetSaveLoadComplete_6FC34300(pClient);
        return 1;
    }

    if (gpD2EventCallbackTable_6FD45830)
    {
        return D2GAME_SAVE_WriteFileOnRealm_6FC8A1B0(pGame, pPlayer, szName, szAccountName, bInteractsWithPlayer, nCharSaveTransactionToken, dwArg, pClientInfo);
    }

    if (!gbWriteSaveFile_6FD30E08)
    {
        return 1;
    }

    char szDirectory[1024] = {};
    FOG_GetSavePath(szDirectory, 1024);
    char szFileName[500] = {};
    sprintf(szFileName, "%s%s.d2s", szDirectory, szName);

    PLRSAVE2_CreateSaveFile(pGame, pPlayer, pSaveData, &nFileSize, 0x2000u, bInteractsWithPlayer, dwArg);
    
    if (nFileSize <= 0)
    {
        return 1;
    }

    FILE* pFile = fopen(szFileName, "wb"); // NOLINT(clang-diagnostic-deprecated-declarations)
    if (pFile)
    {
        FileLockAndWrite(pSaveData, nFileSize, 1u, pFile);
        fclose(pFile);
    }
    else
    {
        FOG_Trace("Unable to open player save file %s", szFileName);
    }

    return 1;
}

//D2Game.0x6FC8A780
int32_t __fastcall sub_6FC8A780(D2GameStrc* pGame, D2ClientStrc* pClient, uint8_t* pSavefile, D2UnitStrc** ppPlayer, int32_t* pValid, int32_t* pVersion, int32_t* a7, int32_t* a8, int32_t* a9, int32_t* a10, int32_t* a11)
{
    // TODO: Names
    *a11 = 0;

    uint8_t saveHeader[130] = {};
    memcpy(saveHeader, pSavefile, sizeof(saveHeader));

    CLIENTS_SetFlags(pClient, 0);

    if (*(uint32_t*)saveHeader != D2SMAGIC_HEADER)
    {
        if (gpD2EventCallbackTable_6FD45830)
        {
            *pValid = 1;
            return 0;
        }

        return SYSERROR_BAD_INPUT;
    }

    if (*(uint16_t*)&saveHeader[32] != 0x82)
    {
        if (gpD2EventCallbackTable_6FD45830)
        {
            *pValid = 1;
            return 0;
        }

        return SYSERROR_UNK_14;
    }

    const uint32_t nVersion = *(uint32_t*)&saveHeader[4];
    *pVersion = nVersion;

    if (nVersion < 71 || nVersion > 96)
    {
        if (gpD2EventCallbackTable_6FD45830)
        {
            *pValid = 1;
            return 9;
        }

        return SYSERROR_BAD_INPUT;
    }

    saveHeader[23] = 0;

    char* szClientName = CLIENTS_GetName(pClient);
    szClientName[15] = 0;

    if (_strcmpi(szClientName, (const char*)&saveHeader[8]))
    {
        *pValid = 0;
        return SYSERROR_BAD_INPUT;
    }

    const uint32_t nFlags = *(uint32_t*)&saveHeader[24];

    if (nFlags & CLIENTSAVEFLAG_EXPANSION)
    {
        if (!pGame->bExpansion)
        {
            FOG_Trace("[PLAYER LOAD]  Error Loading:%s  Error:SYSERROR_NOTEXPANSIONGAME\n", CLIENTS_GetName(pClient));
            return SYSERROR_NOTEXPANSIONGAME;
        }
    }
    else
    {
        if (pGame->bExpansion)
        {
            FOG_Trace("[PLAYER LOAD]  Error Loading:%s  Error:SYSERROR_EXPANSIONGAME\n", CLIENTS_GetName(pClient));
            return SYSERROR_EXPANSIONGAME;
        }
    }

    if (nFlags & CLIENTSAVEFLAG_LADDER)
    {
        if (!pGame->dwGameType)
        {
            FOG_Trace("[PLAYER LOAD]  ClientAddPlayerToGame()  Error Loading:%s  Error:SYSERROR_NOTLADDERGAME\n", CLIENTS_GetName(pClient));
            CLIENTS_SetPlayerInClient(pClient, 0);
            return SYSERROR_NOTLADDERGAME;
        }
    }
    else
    {
        if (pGame->dwGameType)
        {
            FOG_Trace("[PLAYER LOAD]  ClientAddPlayerToGame()  Error Loading:%s  Error:SYSERROR_LADDERGAME\n", CLIENTS_GetName(pClient));
            CLIENTS_SetPlayerInClient(pClient, 0);
            return SYSERROR_LADDERGAME;
        }
    }

    if (nFlags & CLIENTSAVEFLAG_HARDCORE)
    {
        if (nFlags & CLIENTSAVEFLAG_DEAD)
        {
            return SYSERROR_DEADHARDCORE;
        }

        if (!(ARENA_GetFlags(pGame) & GAMEFLAG_ARENA_HARDCORE))
        {
            return SYSERROR_HARDCOREJOINSOFTCORE;
        }
    }
    else
    {
        if (ARENA_GetFlags(pGame) & GAMEFLAG_ARENA_HARDCORE)
        {
            return SYSERROR_SOFTCOREJOINHARDCORE;
        }
    }

    const uint8_t nCharacterProgression = (nFlags & CLIENTSAVEFLAG_CHARACTER_PROGRESSION_MASK) >> CLIENTSAVEFLAG_CHARACTER_PROGRESSION_BIT;
    if (nCharacterProgression < 8 && pGame->nDifficulty >= DIFFMODE_HELL)
    {
        return SYSERROR_HELL_NOT_UNLOCKED;
    }

    if ((nFlags & CLIENTSAVEFLAG_EXPANSION) && nCharacterProgression < NUM_ACTS && pGame->nDifficulty != DIFFMODE_NORMAL)
    {
        return SYSERROR_NIGHTMARE_NOT_UNLOCKED;
    }

    if (nCharacterProgression < 4 && pGame->nDifficulty != DIFFMODE_NORMAL)
    {
        return SYSERROR_NIGHTMARE_NOT_UNLOCKED;
    }

    CLIENTS_SetGuildFlags(pClient, *(int32_t*)&saveHeader[89]);

    uint8_t v44[4] = {};
    *(uint32_t*)v44 = *(uint32_t*)&saveHeader[91];
    char v47[28] = {};
    memcpy(v47, &saveHeader[95], sizeof(v47));

    v44[3] = 0;
    v47[27] = 0;

    CLIENTS_SetGuildTag(pClient, (int32_t*)v44);
    CLIENTS_SetGuildName(pClient, v47);
    CLIENTS_SetGuildEmblem(pClient, saveHeader[123], saveHeader[124], saveHeader[125]);

    if (nFlags & CLIENTSAVEFLAG_INIT)
    {
        const uint32_t nFlagsCopy = nFlags & (~CLIENTSAVEFLAG_INIT);
        *pValid = 1;
        CLIENTS_SetFlags(pClient, nFlagsCopy);
        return 0;
    }

    CLIENTS_SetWeaponSwitch(pClient, nFlags & CLIENTSAVEFLAG_WEAPON_SWITCH);

    const int32_t nClassId = *(uint16_t*)&saveHeader[34];
    *pValid = 0;
    CLIENTS_SetClassId(pClient, nClassId);
    CLIENTS_SetFlags(pClient, nFlags);

    uint8_t nAct = saveHeader[88] & 0xF;
    const uint8_t nDifficulty = saveHeader[88] >> 4;
    if (nAct >= 5 || nDifficulty >= 3u)
    {
        return SYSERROR_UNK_9;
    }

    if (pGame->nGameType == 3 && !pGame->InitSeed)
    {
        if (pGame->nDifficulty != nDifficulty)
        {
            nAct = 0;
        }
        else
        {
            pGame->dwInitSeed = *(uint32_t*)&saveHeader[126];
        }
    }
    else
    {
        if (pGame->nDifficulty != nDifficulty)
        {
            nAct = 0;
        }
    }

    CLIENTS_SetActNo(pClient, nAct);

    D2UnitStrc* pPlayer = SUNIT_AllocUnitData(UNIT_PLAYER, nClassId, 0, 0, pGame, nullptr, 0, 0, 0);
    *ppPlayer = pPlayer;
    CLIENTS_SetPlayerInClient(pClient, pPlayer);
    SUNIT_InitClientInPlayerData(pPlayer, pClient);
    UNITS_SetNameInPlayerData(pPlayer, CLIENTS_GetName(pClient));
    SUNIT_InitSeed(pPlayer, &pGame->pGameSeed);
    UNITS_ChangeAnimMode(pPlayer, 1);

    D2PlayerDataStrc* pPlayerData = UNITS_GetPlayerData(pPlayer);
    pPlayerData->pTrade = nullptr;
    pPlayerData->dwTradeState = 0;
    pPlayerData->dwAcceptTradeTick = 0;
    pPlayerData->unk0x54 = 0;

    D2GAME_SUNITMSG_FirstFn_6FCC5520(CLIENTS_GetGame(pClient), *ppPlayer, pClient);

    *a7 = 16;
    *a8 = sgptDataTables->nSkillsTxtRecordCount;

    const uint16_t v37 = *(uint16_t*)&saveHeader[30];
    const uint16_t v39 = *(uint16_t*)&saveHeader[28];
    if (v37 > 16 || v39 > sgptDataTables->nSkillsTxtRecordCount)
    {
        if (gpD2EventCallbackTable_6FD45830)
        {
            *pValid = 1;
            return 0;
        }

        return SYSERROR_BAD_INPUT;
    }

    if (v37 != 16)
    {
        *a7 = v37;
    }

    if (v39 != sgptDataTables->nSkillsTxtRecordCount)
    {
        *a8 = v39;
    }

    uint8_t* v40 = &saveHeader[71];
    for (int32_t i = 0; i < 8; ++i)
    {
        int32_t v41 = *(v40 - 1);
        if (v41 < 156 || v41 > 216)
        {
            if (v41 == 255)
            {
                v41 = -1;
            }
        }
        else
        {
            v41 = v41 + 65;
        }
        CLIENTS_SetSkillHotKey(pClient, i, v41, *v40, -1);
        v40 += 2;
    }

    int32_t v42 = saveHeader[86];
    if (v42 < 156 || v42 > 216)
    {
        if (v42 == 255)
        {
            v42 = -1;
        }
    }
    else
    {
        v42 = saveHeader[86] + 65;
    }
    *a9 = v42;

    int32_t v43 = saveHeader[87];
    if (v43 < 156 || v43 > 216)
    {
        if (v43 == 255)
        {
            v43 = -1;
        }
    }
    else
    {
        v43 = saveHeader[87] + 65;
    }
    *a10 = v43;

    *a11 = *(uint16_t*)&saveHeader[32];
    return 0;
}

//D2Game.0x6FC8AD50
int32_t __fastcall D2GAME_SAVE_ReadWaypointData_6FC8AD50(D2UnitStrc* pUnit, uint8_t* pSection, int32_t nUnused, int32_t* pSize)
{
    constexpr uint32_t WAYPOINT_DATA_SIZE = 80;

    *pSize = 0;

    uint8_t pSource[WAYPOINT_DATA_SIZE] = {};
    memcpy(pSource, pSection, sizeof(pSource));

    D2PlayerDataStrc* pPlayerData = UNITS_GetPlayerData(pUnit);
    if (*(uint16_t*)pSource != 'SW')
    {
        return SYSERROR_BAD_WAYPOINT;
    }

    uint8_t* pData = &pSource[8];
    for (int32_t i = 0; i < 3; ++i)
    {
        D2WaypointDataStrc* pWaypointData = (D2WaypointDataStrc*)pData;
        if (pWaypointData->nFlags[0] != 0x102 && pWaypointData->nFlags[0] != 0x101 && pWaypointData->nFlags[0] != 0)
        {
            return SYSERROR_BAD_WAYPOINT;
        }

        WAYPOINTS_CopyAndValidateWaypointData(pPlayerData->pWaypointData[i], pWaypointData);
        pData = &pData[24];
    }

    *pSize = WAYPOINT_DATA_SIZE;
    return 0;
}

//D2Game.0x6FC8ADE0
int32_t __fastcall sub_6FC8ADE0(D2GameStrc* pGame, D2UnitStrc* pUnit, uint8_t* pSection, int32_t a4, int32_t nUnused, int32_t* pSize)
{
    *pSize = 0;

    if (*(uint16_t*)pSection == 'fg')
    {
        const uint8_t* pData = pSection + 2;
        int32_t nSize = 0;
        for (int32_t i = 0; i < a4; ++i)
        {
            if ((uint8_t)(1 << (i & 7)) & pData[i / 8])
            {
                STATLIST_SetUnitStat(pUnit, i, *(uint32_t*)&pData[4 * nSize + 3], 0);
                ++nSize;
            }
            else
            {
                STATLIST_SetUnitStat(pUnit, i, 0, 0);
            }
        }

        if (ARENA_IsInArenaMode(pGame))
        {
            STATLIST_SetUnitStat(pUnit, STAT_HITPOINTS, STATLIST_GetMaxLifeFromUnit(pUnit), 0);
            STATLIST_SetUnitStat(pUnit, STAT_MANA, STATLIST_GetMaxManaFromUnit(pUnit), 0);
        }

        STATLIST_SetUnitStat(pUnit, STAT_STAMINA, STATLIST_GetMaxStaminaFromUnit(pUnit), 0);
        *pSize = 4 * nSize + 5;

        return 0;
    }

    return SYSERROR_BAD_STATS;
}

//D2Game.0x6FC8AEC0
int32_t __fastcall sub_6FC8AEC0(D2GameStrc* pGame, D2ClientStrc* pClient, D2UnitStrc* pUnit, uint8_t* pSection, int32_t a5, int32_t a6, int32_t* pSkillCount)
{
    *pSkillCount = 0;

    if (*(uint16_t*)pSection == 'fi')
    {
        const int32_t nClassId = CLIENTS_GetClassId(pClient);
        for (int32_t i = 0; i < SKILLS_GetPlayerSkillCount(nClassId); ++i)
        {
            const int32_t nSkillId = SKILLS_GetClassSkillId(nClassId, i);
            if (pSection[i + 2])
            {
                D2GAME_SetSkills_6FD14C60(pUnit, nSkillId, pSection[i + 2], 1);
            }
        }

        sub_6FC3DCA0(pClient, pUnit);

        *pSkillCount = SKILLS_GetPlayerSkillCount(nClassId) + 2;
        
        sub_6FD14C30(pUnit);
        
        return 0;
    }

    return SYSERROR_BAD_SKILLS;
}

//D2Game.0x6FC8AF70
int32_t __fastcall sub_6FC8AF70(D2SavedItemStrc* pSavedItem, uint8_t* pData)
{
    memset(pSavedItem, 0, sizeof(D2SavedItemStrc));

    D2BitBufferStrc bitBuffer = {};
    BITMANIP_Initialize(&bitBuffer, pData, 0x40u);

    pSavedItem->nItemFlags = BITMANIP_Read(&bitBuffer, 32);

    if (ITEMS_CheckIfFlagIsSet(pSavedItem->nItemFlags, IFLAG_UNK3))
    {
        if (ITEMS_CheckIfFlagIsSet(pSavedItem->nItemFlags, IFLAG_COMPACTSAVE))
        {
            pSavedItem->nItemFormat = (uint8_t)BITMANIP_Read(&bitBuffer, 10);
            pSavedItem->nBodyLoc = BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->nItemLevel = 1;
            pSavedItem->nSpawnType = BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nX = (uint8_t)BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->nY = (uint8_t)BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nInvPage = BITMANIP_Read(&bitBuffer, 8);
            
            if (ITEMS_CheckIfFlagIsSet(pSavedItem->nItemFlags, IFLAG_ISEAR))
            {
                pSavedItem->nItemCode = ' rae';
                pSavedItem->nItemIndex = BITMANIP_Read(&bitBuffer, 3);
                pSavedItem->nEarLevel = BITMANIP_Read(&bitBuffer, 8);

                for (int32_t i = 0; i < 16; ++i)
                {
                    pSavedItem->szName[i] = BITMANIP_Read(&bitBuffer, 7);
                }
            }
            else
            {
                pSavedItem->nItemCode = BITMANIP_Read(&bitBuffer, 32);
            }
        }
        else
        {
            pSavedItem->nItemFormat = (uint8_t)BITMANIP_Read(&bitBuffer, 10);
            pSavedItem->nItemCode = BITMANIP_Read(&bitBuffer, 32);
            pSavedItem->nBodyLoc = BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->unk0x01 = BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemLevel = (uint8_t)BITMANIP_Read(&bitBuffer, 12);
            pSavedItem->nSpawnType = BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemQuality = BITMANIP_Read(&bitBuffer, 4);
            pSavedItem->nQuantity = BITMANIP_ReadSigned(&bitBuffer, 20);
            pSavedItem->nMinDurability = (uint8_t)BITMANIP_Read(&bitBuffer, 8);
            pSavedItem->nMaxDurability = (uint8_t)BITMANIP_Read(&bitBuffer, 8);
            pSavedItem->nX = (uint8_t)BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->nY = (uint8_t)BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemIndex = BITMANIP_ReadSigned(&bitBuffer, 10);
            pSavedItem->nSeed = BITMANIP_Read(&bitBuffer, 32);
            pSavedItem->nItemSeed = BITMANIP_Read(&bitBuffer, 32);
            pSavedItem->nInvPage = BITMANIP_Read(&bitBuffer, 8);
        }

        pSavedItem->nItemId = DATATBLS_GetItemIdFromItemCode(pSavedItem->nItemCode);
        return BITMANIP_GetSize(&bitBuffer);
    }
    else
    {
        pSavedItem->nItemFormat = 0;
        
        if (ITEMS_CheckIfFlagIsSet(pSavedItem->nItemFlags, IFLAG_ISEAR))
        {
            pSavedItem->nBodyLoc = BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->nItemLevel = 1;
            BITMANIP_ReadSigned(&bitBuffer, 10);
            pSavedItem->nItemId = DATATBLS_GetItemIdFromItemCode(' rae');
            pSavedItem->nSpawnType = BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nX = (uint8_t)BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->nY = (uint8_t)BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemIndex = BITMANIP_ReadSigned(&bitBuffer, 10);
            pSavedItem->nInvPage = BITMANIP_Read(&bitBuffer, 8);
            pSavedItem->nEarLevel = BITMANIP_Read(&bitBuffer, 8);

            for (int32_t i = 0; i < 16; ++i)
            {
                pSavedItem->szName[i] = BITMANIP_Read(&bitBuffer, 7);
                if (!pSavedItem->szName[i])
                {
                    break;
                }
            }

            pSavedItem->nItemCode = ' rae';
        }
        else
        {
            pSavedItem->nBodyLoc = BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->unk0x01 = BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemLevel = (uint8_t)BITMANIP_Read(&bitBuffer, 12);
            const int32_t nItemId = BITMANIP_ReadSigned(&bitBuffer, 10);
            pSavedItem->nSpawnType = BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemQuality = BITMANIP_Read(&bitBuffer, 4);
            pSavedItem->nQuantity = BITMANIP_ReadSigned(&bitBuffer, 20);
            pSavedItem->nMinDurability = (uint8_t)BITMANIP_Read(&bitBuffer, 8);
            pSavedItem->nMaxDurability = (uint8_t)BITMANIP_Read(&bitBuffer, 8);
            pSavedItem->nX = (uint8_t)BITMANIP_Read(&bitBuffer, 5);
            pSavedItem->nY = (uint8_t)BITMANIP_Read(&bitBuffer, 3);
            pSavedItem->nItemIndex = BITMANIP_ReadSigned(&bitBuffer, 10);
            pSavedItem->nSeed = BITMANIP_Read(&bitBuffer, 32);
            pSavedItem->nItemSeed = BITMANIP_Read(&bitBuffer, 32);
            pSavedItem->nInvPage = BITMANIP_Read(&bitBuffer, 8);

            if (nItemId >= 0)
            {
                pSavedItem->nItemId = DATATBLS_MapOldItemIndexToCurrent(nItemId);

                D2ItemsTxt* pItemsTxtRecord = DATATBLS_GetItemsTxtRecord(pSavedItem->nItemId);
                if (pItemsTxtRecord)
                {
                    pSavedItem->nItemCode = pItemsTxtRecord->dwCode;
                }
                else
                {
                    pSavedItem->nItemId = -1;
                    pSavedItem->nItemCode = 0;
                }
            }
            else
            {
                pSavedItem->nItemId = -1;
                pSavedItem->nItemCode = 0;
            }
        }

        return 25;
    }
}

//D2Game.0x6FC8B3D0
int32_t __fastcall sub_6FC8B3D0(D2GameStrc* pGame, D2UnitStrc* pPlayer, uint8_t* pSection, uint32_t dwVersion, int32_t nSize, int32_t a6, int32_t* pSize)
{
    // Original game computes the remaining size but does nothing with it.
    D2_MAYBE_UNUSED(nSize);

    if (*(uint16_t*)pSection != 'MJ')
    {
        return 14;
    }

    int32_t nTotalSize = 4;
    uint8_t* pData = pSection + 4;
    const int32_t nTotal = *((uint16_t*)pSection + 1);
    for (int32_t j = 0; j < nTotal; ++j)
    {
        D2SavedItemStrc savedItem = {};

        if (*(uint16_t*)pData != 'MJ')
        {
            return SYSERROR_UNK_14;
        }

        pData += 2;
        int32_t nDataSize = sub_6FC8AF70(&savedItem, pData);
        pData += nDataSize;
        nTotalSize += nDataSize + 2;

        int32_t nCount = 0;
        D2UnitStrc* pItem = nullptr;
        if (a6)
        {
            const int32_t nResult = sub_6FC8B680(pGame, pPlayer, &savedItem, &pItem, &nCount);
            if (nResult)
            {
                return nResult;
            }
        }
        else
        {
            if (sub_6FC895E0(pGame, pPlayer, &savedItem, &pItem, &nCount))
            {
                continue;
            }
        }

        for (int32_t i = 0; i < nCount; ++i)
        {
            memset(&savedItem, 0, sizeof(savedItem));

            if (*(uint16_t*)pData != 'MJ')
            {
                return SYSERROR_UNK_14;
            }

            pData += 2;
            nDataSize = sub_6FC8AF70(&savedItem, pData);
            pData += nDataSize;
            nTotalSize += nDataSize + 2;

            int32_t nResult = 0;
            if (a6)
            {
                int32_t nUnused = 0;
                nResult = sub_6FC8B680(pGame, pPlayer, &savedItem, &pItem, &nUnused);
            }
            else
            {
                int32_t nUnused = 0;
                nResult = sub_6FC895E0(pGame, pPlayer, &savedItem, &pItem, &nUnused);
            }

            if (nResult)
            {
                return nResult;
            }
        }
    }

    *pSize = nTotalSize;
    return 0;
}

//D2Game.0x6FC8B680
int32_t __fastcall sub_6FC8B680(D2GameStrc* pGame, D2UnitStrc* pUnit, D2SavedItemStrc* pSavedItem, D2UnitStrc** ppItem, int32_t* a5)
{
    D2UnitStrc* pItem = sub_6FC897F0(pGame, pSavedItem);
    if (!pItem)
    {
        return SYSERROR_INVENTORY;
    }

    switch (pItem->dwAnimMode)
    {
    case IMODE_STORED:
    {
        UNITS_ChangeAnimMode(pItem, 4);
        UNITS_SetXForStaticUnit(pItem, pSavedItem->nX);
        UNITS_SetYForStaticUnit(pItem, pSavedItem->nY);

        if (!D2GAME_PlaceItem_6FC44410(__FILE__, __LINE__, pGame, pUnit, pItem->dwUnitId, CLIENTS_GetUnitX(pItem), CLIENTS_GetUnitY(pItem), 0, 0, 0))
        {
            return SYSERROR_INVENTORY;
        }
        break;
    }
    case IMODE_EQUIP:
    {
        const int32_t nBodyLoc = ITEMS_GetBodyLocation(pItem);
        if (!nBodyLoc || !INVENTORY_PlaceItemInBodyLoc(pUnit->pInventory, pItem, nBodyLoc))
        {
            return SYSERROR_INVENTORY;
        }

        int32_t nUnused = 3;
        if (nBodyLoc == 11 || nBodyLoc == 12)
        {
            nUnused = 4;
        }

        if (!INVENTORY_PlaceItemInSocket(pUnit->pInventory, pItem, nUnused))
        {
            return SYSERROR_INVENTORY;
        }

        ITEMS_SetBodyLocation(pItem, nBodyLoc);
        pItem->dwFlags &= 0xFFFFFFFDu;
        UNITS_ChangeAnimMode(pItem, 1);
        ITEMS_SetInvPage(pItem, uint8_t(-1));
        ITEMS_SetItemCMDFlag(pItem, 8, 1);
        pItem->dwFlags &= 0xFDFFFFFFu;
        INVENTORY_AddItemToTradeInventory(pUnit->pInventory, pItem);
        UNITS_RefreshInventory(pUnit, 1);
        QUESTS_ItemPickedUp(pGame, pUnit, pItem);
        break;
    }
    case IMODE_SOCKETED:
    {
        if (!*ppItem)
        {
            return 13;
        }

        UNITS_ChangeAnimMode(pItem, 4);

        int32_t nUnused = 0;
        if (!D2GAME_ITEMSOCKET_PlaceItem_6FC497E0(pGame, pUnit, pItem->dwUnitId, (*ppItem) ? (*ppItem)->dwUnitId : -1, &nUnused, 0, 1u, 0))
        {
            return SYSERROR_INVENTORY;
        }

        break;
    }
    default:
        return SYSERROR_INVENTORY;
    }

    if (pSavedItem->unk0x01)
    {
        *ppItem = pItem;
    }

    *a5 = pSavedItem->unk0x01;
    return 0;
}

//D2Game.0x6FC8B8A0
int32_t __fastcall PLRSAVE_ReadItems_6FC8B8A0(D2GameStrc* pGame, D2UnitStrc* pPlayer, uint8_t* pSection, uint32_t dwVersion, int32_t nSize, int32_t a6, int32_t* pSize)
{
    if (*(uint16_t*)pSection != 'MJ')
    {
        return 14;
    }

    const uint16_t nItemCount = *((uint16_t*)pSection + 1);

    uint8_t* pItemBitstream = pSection + 4;
    for (int32_t i = 0; i < nItemCount; ++i)
    {
        const uint32_t nBitstreamSize = pSection - pItemBitstream + nSize;
        D2ItemSaveStrc itemSave = {};
        ITEMS_GetCompactItemDataFromBitstream(pItemBitstream, nBitstreamSize, 1, &itemSave);
        if (itemSave.nClassId == -1)
        {
            return SYSERROR_UNK_14;
        }

        uint32_t nRemainingSize = 0;
        D2UnitStrc* pItem = sub_6FC4EC10(pGame, 0, pItemBitstream, nBitstreamSize, 1, &itemSave, &nRemainingSize, dwVersion);
        if (!pItem && nRemainingSize <= 0)
        {
            return SYSERROR_UNK_14;
        }

        pItemBitstream += nRemainingSize;
        
        int32_t bRemoveItem = 0;
        if (pItem)
        {
            if (a6)
            {
                bRemoveItem = sub_6FC8BAA0(pGame, pPlayer, pItem, 0);
            }
            else
            {
                bRemoveItem = sub_6FC898F0(pGame, pPlayer, pItem, 0);
            }
        }

        D2UnitStrc* pParentItem = pItem;
        if (bRemoveItem)
        {
            if (pItem)
            {
                SUNIT_RemoveUnit(pGame, pItem);
            }

            pParentItem = nullptr;
        }

        for (int32_t j = 0; j < itemSave.nItemFileIndex; ++j)
        {
            const uint32_t nSocketableBitstreamSize = pSection - pItemBitstream + nSize;
            ITEMS_GetCompactItemDataFromBitstream(pItemBitstream, nSocketableBitstreamSize, 1, &itemSave);
            
            D2UnitStrc* pSocketable = sub_6FC4EC10(pGame, 0, pItemBitstream, nSocketableBitstreamSize, 1, &itemSave, &nRemainingSize, dwVersion);
            if (!pSocketable && nRemainingSize <= 0)
            {
                return SYSERROR_UNK_14;
            }

            pItemBitstream += nRemainingSize;
            
            if (pParentItem)
            {
                if (pSocketable)
                {
                    if (a6)
                    {
                        sub_6FC8BAA0(pGame, pPlayer, pSocketable, pParentItem);
                    }
                    else
                    {
                        sub_6FC898F0(pGame, pPlayer, pSocketable, pParentItem);
                    }
                }
            }
            else
            {
                if (pSocketable)
                {
                    SUNIT_RemoveUnit(pGame, pSocketable);
                }
            }
        }

        if (pParentItem && ITEMS_GetItemFlags(pParentItem) & IFLAG_RUNEWORD && !ITEMS_GetRunesTxtRecordFromItem(pParentItem))
        {
            D2GAME_ITEMMODE_Unk_6FC4BC10(pGame, pPlayer, pParentItem);
        }
    }

    if (pSize)
    {
        *pSize = pItemBitstream - pSection;
    }

    return 0;
}

//D2Game.0x6FC8BAA0
int32_t __fastcall sub_6FC8BAA0(D2GameStrc* pGame, D2UnitStrc* pPlayer, D2UnitStrc* pItem, D2UnitStrc* a4)
{
    if (!pItem)
    {
        return SYSERROR_INVENTORY;
    }

    switch (pItem->dwAnimMode)
    {
    case IMODE_STORED:
    {
        UNITS_ChangeAnimMode(pItem, IMODE_ONCURSOR);

        if (D2GAME_PlaceItem_6FC44410(__FILE__, __LINE__, pGame, pPlayer, pItem->dwUnitId, CLIENTS_GetUnitX(pItem), CLIENTS_GetUnitY(pItem), 0, 0, 0))
        {
            return 0;
        }

        return SYSERROR_INVENTORY;
    }
    case IMODE_EQUIP:
    {
        const int32_t nBodyLoc = ITEMS_GetBodyLocation(pItem);
        if (!nBodyLoc || !INVENTORY_PlaceItemInBodyLoc(pPlayer->pInventory, pItem, nBodyLoc))
        {
            return 13;
        }

        int32_t nUnused = 3;
        if (nBodyLoc == 11 || nBodyLoc == 12)
        {
            nUnused = 4;
        }

        if (!INVENTORY_PlaceItemInSocket(pPlayer->pInventory, pItem, nUnused))
        {
            return SYSERROR_INVENTORY;
        }

        ITEMS_SetBodyLocation(pItem, nBodyLoc);

        if (pItem)
        {
            pItem->dwFlags &= 0xFFFFFFFD;
        }

        UNITS_ChangeAnimMode(pItem, 1);
        ITEMS_SetInvPage(pItem, 0xFFu);
        ITEMS_SetItemCMDFlag(pItem, 8, 1);

        if (pItem)
        {
            pItem->dwFlags &= 0xFDFFFFFFu;
        }

        INVENTORY_AddItemToTradeInventory(pPlayer->pInventory, pItem);
        UNITS_RefreshInventory(pPlayer, 1);
        QUESTS_ItemPickedUp(pGame, pPlayer, pItem);
        return 0;
    }
    case IMODE_SOCKETED:
    {
        if (!a4)
        {
            return SYSERROR_INVENTORY;
        }

        UNITS_ChangeAnimMode(pItem, 4);

        int32_t bPlaced = 0;
        if (!D2GAME_ITEMSOCKET_PlaceItem_6FC497E0(pGame, pPlayer, pItem ? pItem->dwUnitId : -1, a4->dwUnitId, &bPlaced, 0, 1u, 0))
        {
            return SYSERROR_INVENTORY;
        }

        return 0;
    }
    default:
        return SYSERROR_INVENTORY;
    }
}

//D2Game.0x6FC8BC70
int32_t __fastcall sub_6FC8BC70(D2GameStrc* pGame, D2UnitStrc* pUnit, uint8_t* pSection, uint32_t dwVersion, int32_t nSize, int32_t a6, int32_t* pSize)
{
    if (dwVersion == 71)
    {
        return sub_6FC8B3D0(pGame, pUnit, pSection, 71u, nSize, a6, pSize);
    }
    else if (dwVersion >= 72 && dwVersion <= 96)
    {
        return PLRSAVE_ReadItems_6FC8B8A0(pGame, pUnit, pSection, dwVersion, nSize, a6, pSize);
    }
    else
    {
        return 1;
    }
}

//D2Game.0x6FC8BCC0
int32_t __fastcall sub_6FC8BCC0(D2GameStrc* pGame, D2UnitStrc* pPlayer, uint8_t* pSection, uint32_t dwVersion, int32_t nRemainingSize, int32_t* pSize)
{
    *pSize = 0;

    if (dwVersion < 71 || dwVersion > 96)
    {
        return 1;
    }

    if (*(uint16_t*)pSection != 'MJ')
    {
        return SYSERROR_UNK_14;
    }

    uint8_t* pData = pSection + 4;
    nRemainingSize -= 4;
    int32_t nSize = 4;

    for (int32_t i = 0; i < *(uint16_t*)(pSection + 2); ++i)
    {
        D2UnitStrc* pPlayerBody = SUNIT_AllocUnitData(UNIT_PLAYER, pPlayer ? pPlayer->dwClassId : -1, 0, 0, pGame, nullptr, 1, PLRMODE_DEAD, 0);
        if (!pPlayerBody)
        {
            return SYSERROR_CORPSES;
        }

        pPlayerBody->dwFlags &= 0xFFFFFFFB;

        D2ClientStrc* pClient = SUNIT_GetClientFromPlayer(pPlayer, __FILE__, __LINE__);
        SUNIT_InitClientInPlayerData(pPlayerBody, pClient);
        UNITS_SetNameInPlayerData(pPlayerBody, CLIENTS_GetName(pClient));
        SUNIT_InitSeed(pPlayerBody, &pGame->pGameSeed);
        STATES_ToggleState(pPlayerBody, STATE_PLAYERBODY, 1);
        pPlayerBody->dwFlags &= 0xFFFFFFEFu;
        INVENTORY_SetOwnerId(pPlayerBody->pInventory, pPlayer ? pPlayer->dwUnitId : -1);

        int32_t nReadSize = 0;
        const int32_t nErrorCode = sub_6FC8BC70(pGame, pPlayer, pData + 12, dwVersion, nRemainingSize - 12, 1, &nReadSize);
        if (nErrorCode)
        {
            return nErrorCode;
        }

        INVENTORY_CreateCorpseForPlayer(pPlayer->pInventory, pPlayerBody->dwUnitId, 0, 1);
        D2GAME_SendPacket0x8E_CorpseAssign_ToAllPlayers_6FC3F9B0(pGame, pPlayer->dwUnitId, pPlayerBody->dwUnitId, 1u);

        const int32_t nSectionSize = nReadSize + 12;
        nRemainingSize -= nSectionSize;
        pData += nSectionSize;
        nSize += nSectionSize;
    }

    *pSize = nSize;
    return 0;
}

//D2Game.0x6FC8BEE0
int32_t __fastcall sub_6FC8BEE0(int16_t nHirelingId, int32_t nLevel)
{
    switch (nHirelingId)
    {
    case 0:
    case 1:
        if (nLevel < 49)
        {
            return (((nLevel < 25) - 1) & 100) + 100;
        }

        return 400;
    case 2:
    case 3:
        return (((nLevel < 49) - 1) & 220) + 220;
    case 4:
    case 5:
    case 12:
    case 13:
    case 14:
    case 21:
    case 23:
        return 480;
    case 6:
    case 7:
    case 8:
        if (nLevel < 55)
        {
            return (((nLevel < 31) - 1) & 105) + 105;
        }

        return 420;
    case 9:
    case 10:
    case 11:
        return (((nLevel < 55) - 1) & 230) + 230;
    case 15:
    case 17:
        if (nLevel < 61)
        {
            return (((nLevel < 37) - 1) & 110) + 110;
        }

        return 440;
    case 16:
        if (nLevel < 61)
        {
            return (((nLevel < 37) - 1) & 115) + 115;
        }

        return 260;
    case 18:
    case 20:
        return (((nLevel < 61) - 1) & 230) + 230;
    case 19:
        return (((nLevel < 61) - 1) & 240) + 240;
    case 22:
        return 500;
    case 24:
    case 25:
        if (nLevel < 75)
        {
            return (((nLevel < 42) - 1) & 220) + 220;
        }

        return 880;
    case 26:
    case 27:
        return (((nLevel < 75) - 1) & 460) + 460;
    default:
        return 960;
    }
}

//D2Game.0x6FC8C050
uint32_t __fastcall sub_6FC8C050(D2GameStrc* pGame, int16_t nHirelingId, uint32_t a3)
{
    int32_t nLevel = 1;
    while (nLevel < 99)
    {
        if (3 * nLevel * (nLevel + 1) * sub_6FC8BEE0(nHirelingId, nLevel + 1) > a3)
        {
            break;
        }
        ++nLevel;
    }

    D2HirelingTxt* pHirelingTxtRecord = DATATBLS_GetHirelingTxtRecordFromIdAndLevel(pGame->bExpansion, nHirelingId, nLevel);
    if (!pHirelingTxtRecord)
    {
        return a3;
    }
    
    const uint32_t nExperience = MONSTERS_GetHirelingExpForNextLevel(nLevel, pHirelingTxtRecord->dwExpPerLvl);
    if (nExperience <= a3)
    {
        return a3;
    }

    return nExperience;
}

//D2Game.0x6FC8C0C0
// Builds a player from a character file: header, quests, waypoints, NPC introductions, stats, skills, items, the mercenary and the
// skill hotkeys. Written from the 1.10f disassembly. Trouble after the stats and skills (items, mercenary) ends the load early but
// still returns 0, as in the original; the errors of the earlier steps are returned.
int32_t __fastcall D2GAME_SAVE_ProcessSaveFile_6FC8C0C0(D2GameStrc* pGame, D2ClientStrc* pClient, uint8_t* pSaveFile, int32_t nSize, D2UnitStrc** ppPlayer, D2ActiveRoomStrc* pRoomArg, int32_t nXArg, int32_t nYArg)
{
    D2UnitStrc* pPlayer = nullptr;
    *ppPlayer = nullptr;

    if (nSize < 130)
    {
        return 9;
    }

    int32_t bValid = 0;
    int32_t nVersion = 0;
    int32_t aUnused[3] = {};
    int32_t nSkillsInHeader = 0;
    int32_t nLeftSkill = 0;
    int32_t nRightSkill = 0;
    int32_t nConsumed = 0;
    int32_t nResult = sub_6FC8A780(pGame, pClient, pSaveFile, &pPlayer, &bValid, &nVersion, aUnused, &nSkillsInHeader, &nLeftSkill, &nRightSkill, &nConsumed);
    if (nResult)
    {
        return nResult;
    }

    if (bValid)
    {
        return sub_6FC8C890(pGame, pClient, ppPlayer, pRoomArg, nXArg, nYArg);
    }

    int32_t nRemaining = nSize - nConsumed;
    *ppPlayer = pPlayer;
    uint8_t* pData = pSaveFile + nConsumed;

    // Quest records of the three difficulties.
    if (nRemaining < 298)
    {
        return 2;
    }

    uint8_t aQuestSection[298] = {};
    memcpy(aQuestSection, pData, sizeof(aQuestSection));
    if (*(uint32_t*)aQuestSection != '!ooW' || *(uint32_t*)&aQuestSection[4] != 6)
    {
        return 2;
    }

    D2PlayerDataStrc* pPlayerData = UNITS_GetPlayerData(pPlayer);
    for (int32_t i = 0; i < 3; ++i)
    {
        QUESTRECORD_CopyBufferToRecord(pPlayerData->pQuestData[i], &aQuestSection[10 + 96 * i], 96, 1);
    }

    nConsumed = 298;
    pData += 298;
    nRemaining -= 298;

    nResult = D2GAME_SAVE_ReadWaypointData_6FC8AD50(pPlayer, pData, nVersion, &nConsumed);
    if (nResult)
    {
        return nResult;
    }

    pData += nConsumed;
    nRemaining -= nConsumed;

    // What the character has been told by the NPCs.
    if (nRemaining < 52)
    {
        return 11;
    }

    uint8_t aIntroSection[52] = {};
    memcpy(aIntroSection, pData, sizeof(aIntroSection));
    if (*(uint16_t*)aIntroSection != 0x7701)
    {
        return 11;
    }

    for (int32_t i = 0; i < 3; ++i)
    {
        PLRINTRO_CopyBufferToQuestIntroFlags(UNITS_GetPlayerData(pPlayer)->pPlayerIntro[i], &aIntroSection[4 + 8 * i]);
        PLRINTRO_CopyBufferToNpcIntroFlags(UNITS_GetPlayerData(pPlayer)->pPlayerIntro[i], &aIntroSection[28 + 8 * i]);
    }

    pData += 52;
    nRemaining -= 52;

    nResult = sub_6FC8ADE0(pGame, pPlayer, pData, 16, nVersion, &nConsumed);
    if (nResult)
    {
        return nResult;
    }

    // Life and mana are put back at the end; the items may have changed them.
    const int32_t nSavedLife = STATLIST_UnitGetStatValue(pPlayer, STAT_HITPOINTS, 0);
    const int32_t nSavedMana = STATLIST_UnitGetStatValue(pPlayer, STAT_MANA, 0);
    const int32_t nGold = STATLIST_UnitGetStatValue(pPlayer, STAT_GOLD, 0);
    if (nGold < 0 || nGold > (int32_t)UNITS_GetInventoryGoldLimit(pPlayer))
    {
        STATLIST_SetUnitStat(pPlayer, STAT_GOLD, 0, 0);
    }

    const int32_t nGoldBank = STATLIST_UnitGetStatValue(pPlayer, STAT_GOLDBANK, 0);
    if (nGoldBank < 0 || nGoldBank > (int32_t)UNITS_GetStashGoldLimit(pPlayer))
    {
        STATLIST_SetUnitStat(pPlayer, STAT_GOLDBANK, 0, 0);
    }

    pData += nConsumed;
    nRemaining -= nConsumed;

    nResult = sub_6FC8AEC0(pGame, pClient, pPlayer, pData, nSkillsInHeader, nVersion, &nConsumed);
    if (nResult)
    {
        return nResult;
    }

    if (nLeftSkill)
    {
        D2GAME_AssignSkill_6FD13800(pPlayer, 1, nLeftSkill, -1);
    }

    if (nRightSkill)
    {
        D2GAME_AssignSkill_6FD13800(pPlayer, 0, nRightSkill, -1);
    }

    UNITS_GetPlayerData(pPlayer)->nLeftSkillId = nLeftSkill;
    UNITS_GetPlayerData(pPlayer)->nRightSkillId = nRightSkill;

    pData += nConsumed;
    nRemaining -= nConsumed;

    // Everything from here on is allowed to fail: the character is used as far as it has been read.
    do
    {
        if (nVersion == 71)
        {
            nResult = sub_6FC8B3D0(pGame, pPlayer, pData, 71, nRemaining, 0, &nConsumed);
        }
        else if (nVersion >= 72 && nVersion <= 96)
        {
            nResult = PLRSAVE_ReadItems_6FC8B8A0(pGame, pPlayer, pData, nVersion, nRemaining, 0, &nConsumed);
        }
        else
        {
            break;
        }

        if (nResult)
        {
            break;
        }

        pData += nConsumed;
        nRemaining -= nConsumed;

        nResult = sub_6FC8BCC0(pGame, pPlayer, pData, nVersion, nRemaining, &nConsumed);
        if (nResult)
        {
            break;
        }

        pData += nConsumed;
        nRemaining -= nConsumed;

        int32_t nMercSectionSize = 0;
        if (nVersion == 71)
        {
            if (*(uint16_t*)pData != 'MJ')
            {
                break;
            }

            const uint32_t nMercSeed = *(uint32_t*)&pData[2];
            const uint16_t nMercName = *(uint16_t*)&pData[6];
            if (nMercSeed && nMercName)
            {
                D2GAME_MERCS_Create_6FCC8630(pGame, pPlayer, nMercName, nMercSeed, 0xFFFF, 0, 0);
            }

            nMercSectionSize = 8;
        }
        else
        {
            if (nRemaining < 1)
            {
                FOG_Trace("Premature end of save data.");
                break;
            }

            if (!pData[0])
            {
                nMercSectionSize = 1;
            }
            else
            {
                if (nRemaining < 18)
                {
                    FOG_Trace("Premature end of save data.");
                    break;
                }

                const int32_t bDead = *(uint32_t*)&pData[2] & 0x10000;
                const uint16_t nHirelingId = *(uint16_t*)&pData[12];
                uint8_t* pMercItems = pData + 18;
                D2HirelingTxt* pHirelingTxt = DATATBLS_GetHirelingTxtRecordFromIdAndLevel(pGame->bExpansion, nHirelingId, 1);
                if (!pHirelingTxt)
                {
                    FOG_Trace("Unable to load merc.");
                    break;
                }

                if (nVersion <= 89)
                {
                    *(uint32_t*)&pData[14] = sub_6FC8C050(pGame, *(int16_t*)&pData[12], *(uint32_t*)&pData[14]);
                }

                *(uint16_t*)&pData[10] += pHirelingTxt->wNameFirst;
                if (*(uint16_t*)&pData[10] > pHirelingTxt->wNameLast)
                {
                    *(uint16_t*)&pData[10] = pHirelingTxt->wNameFirst;
                }

                D2UnitStrc* pMerc = D2GAME_MERCS_Create_6FCC8630(pGame, pPlayer, *(uint16_t*)&pData[10], *(uint32_t*)&pData[6], *(int16_t*)&pData[12], pHirelingTxt->dwClass, bDead);
                if (!pMerc)
                {
                    nMercSectionSize = 18;
                }
                else
                {
                    D2PetInfoStrc petInfo = {};
                    petInfo.nSeed = *(uint32_t*)&pData[6];
                    petInfo.wName = *(uint16_t*)&pData[10];
                    petInfo.nHirelingId = *(uint16_t*)&pData[12];
                    PLAYERPETS_UpdatePetInfo(pPlayer, 7, pMerc->dwUnitId, &petInfo);

                    const int32_t nMaxLevel = DATATBLS_GetMaxLevel(0);
                    const uint32_t nMercExperience = *(uint32_t*)&pData[14];
                    if ((uint32_t)STATLIST_UnitGetStatValue(pMerc, STAT_EXPERIENCE, 0) < nMercExperience)
                    {
                        STATLIST_SetUnitStat(pMerc, STAT_EXPERIENCE, nMercExperience, 0);
                    }

                    D2GAME_MERCS_SendStat_6FCC61D0(pMerc, STAT_EXPERIENCE, nMercExperience);

                    int32_t nMercLevel = 1;
                    while (true)
                    {
                        D2HirelingTxt* pLevelTxt = DATATBLS_GetHirelingTxtRecordFromIdAndLevel(pGame->bExpansion, nHirelingId, nMercLevel);
                        if (!pLevelTxt)
                        {
                            FOG_DisplayAssert("ptMercStats", __FILE__, __LINE__);
                            exit(-1);
                        }

                        if (nMercLevel + 1 > nMaxLevel || nMercExperience < (uint32_t)MONSTERS_GetHirelingExpForNextLevel(nMercLevel + 1, pLevelTxt->dwExpPerLvl))
                        {
                            break;
                        }

                        ++nMercLevel;
                    }

                    MONSTERAI_UpdateMercStatsAndSkills(pGame, pPlayer, pMerc, nMercLevel);
                    if (!pMerc->pInventory)
                    {
                        INVENTORY_AllocInventory(nullptr, pMerc);
                    }

                    if (bDead)
                    {
                        sub_6FC7D470(pGame, pPlayer, pMerc);
                        pMerc->dwFlags |= UNITFLAG_ISDEAD;
                        SUNIT_SetCombatMode(pGame, pMerc, MONMODE_DEAD);
                        MONSTER_DeleteEvents(pGame, pMerc);
                    }

                    int32_t nMercItemsSize = 0;
                    if (sub_6FC8BC70(pGame, pMerc, pMercItems, nVersion, nRemaining - 18, 0, &nMercItemsSize))
                    {
                        break;
                    }

                    D2GAME_ITEMS_UpdateInventoryItems_6FC45050(pGame, pMerc, 0, 0);
                    STATLIST_SetUnitStat(pMerc, STAT_HITPOINTS, STATLIST_GetMaxLifeFromUnit(pMerc), 0);
                    MONSTERAI_SendMercStats(pGame, pPlayer, 1);
                    nMercSectionSize = 18 + nMercItemsSize;
                }
            }
        }

        // The skill hotkeys of the second page and the switch-weapon skills.
        pData += nMercSectionSize;
        D2ClientStrc* pOwnClient = SUNIT_GetClientFromPlayer(pPlayer, __FILE__, __LINE__);
        if (pOwnClient && nVersion > 72)
        {
            uint8_t* pHotkeys = pData;
            for (int32_t nHotkey = 8; nHotkey < 16; ++nHotkey)
            {
                int32_t nSkill = pHotkeys[0];
                if (nSkill >= 156 && nSkill <= 216)
                {
                    nSkill += 65;
                }
                else if (nSkill == 255)
                {
                    nSkill = -1;
                }

                CLIENTS_SetSkillHotKey(pOwnClient, nHotkey, (int16_t)nSkill, pHotkeys[1], -1);
                pHotkeys += 6;
            }

            if (nVersion > 81)
            {
                UNITS_SetSwitchLeftSkill(pPlayer, *(int32_t*)&pData[48], -1);
                UNITS_SetSwitchRightSkill(pPlayer, *(int32_t*)&pData[52], -1);
            }
        }
    }
    while (false);

    STATLIST_SetUnitStat(pPlayer, STAT_HITPOINTS, nSavedLife, 0);
    STATLIST_SetUnitStat(pPlayer, STAT_MANA, nSavedMana, 0);
    STATLIST_SetUnitStat(pPlayer, STAT_ATTACKRATE, 100, 0);
    STATLIST_SetUnitStat(pPlayer, STAT_VELOCITYPERCENT, 100, 0);
    STATLIST_SetUnitStat(pPlayer, STAT_OTHER_ANIMRATE, 100, 0);
    const int32_t nClassId = pPlayer ? pPlayer->dwClassId : -1;
    STATLIST_SetUnitStat(pPlayer, STAT_NEXTEXP, DATATBLS_GetLevelThreshold(nClassId, STATLIST_GetUnitBaseStat(pPlayer, STAT_LEVEL, 0)), 0);

    if (CLIENTS_GetWeaponSwitch(pClient))
    {
        D2GAME_PACKETS_SendPacket0x97_6FC3FB60(pClient);
    }

    return 0;
}

//D2Game.0x6FC8C890
int32_t __fastcall sub_6FC8C890(D2GameStrc* pGame, D2ClientStrc* pClient, D2UnitStrc** ppPlayer, D2ActiveRoomStrc* pRoomArg, int32_t nXArg, int32_t nYArg)
{
    sub_6FC32220(pClient);
    D2UnitStrc* pPlayer = SUNIT_AllocUnitData(UNIT_PLAYER, CLIENTS_GetClassId(pClient), 0, 0, pGame, nullptr, 0, 0, 0);
    CLIENTS_SetPlayerInClient(pClient, pPlayer);
    SUNIT_InitClientInPlayerData(pPlayer, pClient);
    UNITS_SetNameInPlayerData(pPlayer, CLIENTS_GetName(pClient));
    SUNIT_InitSeed(pPlayer, &pGame->pGameSeed);
    UNITS_ChangeAnimMode(pPlayer, PLRMODE_NEUTRAL);

    D2PlayerDataStrc* pPlayerData = UNITS_GetPlayerData(pPlayer);
    pPlayerData->pTrade = nullptr;
    pPlayerData->dwTradeState = 0;
    pPlayerData->dwAcceptTradeTick = 0;
    pPlayerData->unk0x54 = 0;

    D2GAME_SUNITMSG_FirstFn_6FCC5520(pGame, pPlayer, pClient);

    const uint8_t nAct = CLIENTS_GetActNo(pClient);
    PLAYERSTATS_SetStatsForStartingAct(pGame, pPlayer, nAct);
    PLAYER_CreateStartItemsFromCharStatsTxt(pPlayer, pGame, (void*)(uintptr_t)nAct);

    pPlayerData->nLeftSkillId = 0;
    pPlayerData->nRightSkillId = 0;

    if (!nAct && pPlayer)
    {
        D2CharStatsTxt* pCharStatsTxtRecord = PLRSAVE2_GetCharStatsTxtRecord(pPlayer->dwClassId);
        if (pCharStatsTxtRecord && pCharStatsTxtRecord->wStartSkill && SKILLS_GetHighestLevelSkillFromUnitAndId(pPlayer, pCharStatsTxtRecord->wStartSkill))
        {
            D2GAME_AssignSkill_6FD13800(pPlayer, 0, pCharStatsTxtRecord->wStartSkill, -1);
            pPlayerData->nRightSkillId = pCharStatsTxtRecord->wStartSkill;
        }
    }

    QUESTS_SequenceCycler(pGame, pPlayer, 1);
    *ppPlayer = pPlayer;

    return 0;
}

//D2Game.0x6FC8C9D0
int32_t __fastcall D2GAME_SAVE_ReadFile_6FC8C9D0(D2GameStrc* pGame, D2ClientStrc* pClient, const char* szName, D2UnitStrc** ppPlayer, D2ActiveRoomStrc* pRoomArg, int32_t nXArg, int32_t nYArg)
{
    *ppPlayer = nullptr;

    char szPath[1024] = {};
    FOG_GetSavePath(szPath, sizeof(szPath));

    char szFileName[500] = {};
    sprintf_s(szFileName, "%s%s.d2s", szPath, szName);

    FILE* pSaveFile = fopen(szFileName, "rb"); // NOLINT(clang-diagnostic-deprecated-declarations)
    if (!pSaveFile)
    {
        return SYSERROR_UNK_14;
    }

    uint8_t saveFile[0x2000] = {};
    const uint32_t nFileSize = FileLockAndRead(saveFile, 1u, 0x2000u, pSaveFile);
    fclose(pSaveFile);
    
    if (!nFileSize)
    {
        return SYSERROR_UNK_14;
    }

    if (nFileSize < 8 || *(uint32_t*)saveFile != D2SMAGIC_HEADER)
    {
        return SYSERROR_UNK_9;
    }

    D2UnitStrc* pPlayer = nullptr;
    int32_t nResult = 0;
    if (*(uint32_t*)&saveFile[4] > 91u)
    {
        nResult = PLRSAVE2_ProcessSaveFile(pGame, pClient, saveFile, nFileSize, &pPlayer, pRoomArg, nXArg, nYArg);
    }
    else
    {
        nResult = D2GAME_SAVE_ProcessSaveFile_6FC8C0C0(pGame, pClient, saveFile, nFileSize, &pPlayer, pRoomArg, nXArg, nYArg);
    }

    if (!nResult)
    {
        *ppPlayer = pPlayer;
        if (!pPlayer)
        {
            nResult = SYSERROR_UNK_14;
        }
    }

    return nResult;
}

//D2Game.0x6FC8CB40
int32_t __fastcall D2GAME_SAVE_GetUnitDataFromFile_6FC8CB40(D2GameStrc* pGame, D2ClientStrc* pClient, const char* szName, int32_t a4, D2UnitStrc** ppPlayer, D2ActiveRoomStrc* pRoomArg, int32_t nXArg, int32_t nYArg)
{
    if (pGame->nGameType != 1 && pGame->nGameType != 2 && !gpD2EventCallbackTable_6FD45830)
    {
        if (a4)
        {
            D2UnitStrc* pPlayer = nullptr;
            const int32_t nErrorCode = D2GAME_SAVE_ReadFile_6FC8C9D0(pGame, pClient, szName, &pPlayer, pRoomArg, nXArg, nYArg);
            if (pPlayer)
            {
                QUESTS_SequenceCycler(pGame, pPlayer, 0);
            }

            *ppPlayer = pPlayer;
            if (nErrorCode)
            {
                return nErrorCode;
            }
        }
        else
        {
            const int32_t nErrorCode = sub_6FC8C890(pGame, pClient, ppPlayer, pRoomArg, nXArg, nYArg);
            if (nErrorCode)
            {
                return nErrorCode;
            }
        }
    }
    else
    {
        if (!a4)
        {
            const int32_t nErrorCode = sub_6FC8C890(pGame, pClient, ppPlayer, pRoomArg, nXArg, nYArg);
            if (nErrorCode)
            {
                return nErrorCode;
            }
        }
        else
        {
            *ppPlayer = nullptr;
            D2SaveHeaderStrc* pSaveHeader = CLIENTS_GetSaveHeader(pClient);
            const uint32_t nSaveHeaderSize = CLIENTS_GetSaveHeaderSize(pClient);
            if (!nSaveHeaderSize)
            {
                return SYSERROR_UNK_14;
            }

            if (nSaveHeaderSize < 8)
            {
                *ppPlayer = nullptr;
                return SYSERROR_UNK_9;
            }

            if (pSaveHeader->dwHeaderMagic != D2SMAGIC_HEADER)
            {
                *ppPlayer = nullptr;
                return SYSERROR_UNK_9;
            }

            int32_t nErrorCode = 0;
            if (pSaveHeader->dwVersion > 91)
            {
                nErrorCode = PLRSAVE2_ProcessSaveFile(pGame, pClient, (uint8_t*)pSaveHeader, nSaveHeaderSize, ppPlayer, pRoomArg, nXArg, nYArg);
            }
            else
            {
                nErrorCode = D2GAME_SAVE_ProcessSaveFile_6FC8C0C0(pGame, pClient, (uint8_t*)pSaveHeader, nSaveHeaderSize, ppPlayer, pRoomArg, nXArg, nYArg);
            }

            if (nErrorCode)
            {
                *ppPlayer = nullptr;
                return nErrorCode;
            }

            if (!*ppPlayer)
            {
                return SYSERROR_UNK_14;
            }

            if (pGame->nGameType == 1 || pGame->nGameType == 2)
            {
                CLIENTS_FreeSaveHeader(pClient);
            }

            QUESTS_SequenceCycler(pGame, *ppPlayer, 0);
        }
    }

    if (!*ppPlayer)
    {
        return SYSERROR_UNK_14;
    }

    sub_6FD14C30(*ppPlayer);
    return 0;
}
