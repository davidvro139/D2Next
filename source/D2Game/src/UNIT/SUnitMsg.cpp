#include "UNIT/SUnitMsg.h"

#include <algorithm>

#include "D2PacketDef.h"

#include <D2BitManip.h>
#include <D2Math.h>

#include <D2Chat.h>
#include <D2DataTbls.h>
#include <D2Skills.h>
#include <D2States.h>
#include <D2Items.h>
#include <D2StatList.h>
#include <DataTbls/MonsterIds.h>
#include <DataTbls/ObjectsTbls.h>
#include <Units/UnitRoom.h>
#include <DataTbls/ObjectsIds.h>
#include <DataTbls/ItemsTbls.h>


#include "AI/AiGeneral.h"
#include "GAME/Clients.h"
#include "GAME/SCmd.h"
#include "ITEMS/ItemMode.h"
#include "MISSILES/Missiles.h"
#include "MONSTER/MonsterMode.h"
#include "MONSTER/MonsterMsg.h"
#include "PLAYER/Player.h"
#include "PLAYER/PlayerList.h"
#include "PLAYER/PlrMsg.h"
#include "SKILLS/Skills.h"
#include "UNIT/SUnit.h"

// D2UnitPacketListStrc is a variable-length node: { pNext, nHeader, <payload> }. The original 32-bit code
// addressed the payload by raw byte offsets counted from the start of the node, which assumes a 4-byte pNext.
// Every such offset is given here in its original (x86) form and shifted by the extra pointer bytes, so the
// payload never overlaps pNext or nHeader and allocations are big enough on any pointer size.
static constexpr size_t kPacketNodeShift = sizeof(void*) - 4;

template <typename T>
static T& PacketField(D2UnitPacketListStrc* pMsg, size_t nX86Offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(pMsg) + nX86Offset + kPacketNodeShift);
}


//D2Game.0x6FCC5520
//1.14d: 0x00571F90
void __fastcall SUNITMSG_SendUnitToClient(D2GameStrc* pGame, D2UnitStrc* pUnit, D2ClientStrc* pClient)
{
    if (!pUnit)
    {
        return;
    }

    D2CoordStrc coords = {};
    UNITS_GetCoords(pUnit, &coords);

    D2UnitStrc* pLocalPlayer = CLIENTS_GetPlayerFromClient(pClient, 0);
    switch (pUnit->dwUnitType)
    {
    case UNIT_PLAYER:
    {
        SCMD_SendPlayerSpawnAndPartyRoster(pClient, pUnit->dwUnitId, (uint8_t)pUnit->dwClassId, pUnit->pPlayerData->szName, coords.nX, coords.nY);
        break;
    }
    case UNIT_ITEM:
    {
        if (pUnit->dwFlags & UNITFLAG_INITSEEDSET)
        {
            if (pUnit->dwAnimMode == IMODE_ONGROUND && pUnit->dwFlags & UNITFLAG_SENDREFRESHMSG)
            {
                D2GAME_SendP0x9C_ItemAction_DropToGround_6FC3E8E0(pClient, pLocalPlayer, pUnit, 0);
            }
            else
            {
                D2GAME_SendP0x9C_ItemAction_AddToGround_6FC3E850(pClient, pUnit, 0);
            }
        }
        break;
    }
    case UNIT_OBJECT:
    {
        D2GAME_PACKETS_SendPacket0x51_6FC3D300(pClient, 0x51u, 2u, pUnit->dwUnitId, (uint16_t)pUnit->dwClassId, coords.nX, coords.nY, pUnit->dwAnimMode, pUnit->pObjectData->InteractType);
        if (pUnit->pObjectData->pObjectTxt->nSubClass & 4)
        {
            sub_6FC3F550(pClient, pUnit);
            if (pUnit->dwClassId == OBJECT_TOWN_PORTAL)
            {
                D2UnitStrc* pOwner = SUNIT_GetPortalOwner(pGame, pUnit);
                D2GAME_PACKETS_SendPacket0x82_6FC3F790(pGame, pClient, pUnit->dwUnitId, pOwner ? pOwner->dwUnitId : -1);
            }
        }
        break;
    }
    case UNIT_MONSTER:
    {
        if (!SUNIT_IsDead(pUnit) || !STATES_CheckStateMaskHideOnUnit(pUnit))
        {
            sub_6FC3FC80(pClient, pUnit);
            STATLIST_SetUnitStat(pUnit, STAT_PIERCE_IDX, (uint16_t)(coords.nY + coords.nX), 0);
            if (pUnit->dwClassId == MONSTER_EVILHUT)
            {
                sub_6FC3FAF0(pClient, pUnit->dwUnitId, AIGENERAL_GetMinionSpawnClassId(pUnit));
            }

            D2MonStatsTxt* pMonStatsTxtRecord = MONSTERMODE_GetMonStatsTxtRecord(pUnit->dwClassId);
            if (pMonStatsTxtRecord)
            {
                for (int32_t i = 0; i < 8; ++i)
                {
                    if (pMonStatsTxtRecord->nSendSkills[i >> 3] & gdwBitMasks[i & 7])
                    {
                        const int32_t nSkillId = pMonStatsTxtRecord->nSkill[i];
                        if (nSkillId >= 0 && nSkillId < sgptDataTables->nSkillsTxtRecordCount)
                        {
                            D2SkillStrc* pSkill = SKILLS_GetHighestLevelSkillFromUnitAndId(pUnit, nSkillId);
                            if (pSkill)
                            {
                                D2GAME_PACKETS_SendPacket0x21_UpdateSkills_6FC3DB50(pClient, pUnit, nSkillId, SKILLS_GetSkillLevel(pUnit, pSkill, 0), 0);
                            }
                        }
                    }
                }
            }

            D2GAME_STATES_SendUnitStates_6FCC58E0(pUnit, pClient);
            SUNITMSG_SendQueuedMessages(pUnit, pClient);
        }
        break;
    }
    case UNIT_MISSILE:
    {
        MISSILES_SyncToClient(pClient, pGame, pUnit, PATH_GetVelocity(pUnit->pDynamicPath));
        break;
    }
    default:
    {
        D2GAME_PACKETS_SendPacket0x09_6FC3D1A0(pClient, 9u, pUnit->dwUnitType, pUnit->dwUnitId, pUnit->dwClassId, coords.nX, coords.nY);
        break;
    }
    }

    switch (pUnit->dwUnitType)
    {
    case UNIT_PLAYER:
    {
        sub_6FC82830(pUnit, pClient);

        if (SUNIT_IsDead(pUnit) && STATES_CheckState(pUnit, STATE_PLAYERBODY))
        {
            D2UnitStrc* pPlayer = SUNIT_GetServerUnit(pGame, UNIT_PLAYER, INVENTORY_GetOwnerId(pUnit->pInventory));
            D2_ASSERT(pPlayer);
            D2GAME_PACKETS_SendPacket0x74_6FC3F640(pClient, pPlayer, pUnit, (pUnit->dwFlagEx & UNITFLAGEX_ISCORPSE) != 0);
        }
        else
        {
            PLAYER_SynchronizeItemsToClient(pUnit, pClient);
        }

        D2GAME_STATES_SendUnitStates_6FCC58E0(pUnit, pClient);
        sub_6FC82270(pGame, pUnit, pClient);
        SUNITMSG_SendQueuedMessages(pUnit, pClient);
        SUNITMSG_SendHoverText(pUnit, pClient);
        break;
    }
    case UNIT_MONSTER:
    {
        MONSTERMSG_SendMonsterModeUpdate(pGame, pUnit, pClient);

        if (STATES_CheckState(pUnit, STATE_VALKYRIE) || pUnit->dwClassId == MONSTER_IRONGOLEM || pUnit->dwClassId == MONSTER_SHADOWWARRIOR || pUnit->dwClassId == MONSTER_SHADOWMASTER)
        {
            PLAYER_SynchronizeItemsToClient(pUnit, pClient);
        }
        break;
    }
    case UNIT_ITEM:
    {
        ITEMMODE_SendGroundItemToClient(pUnit, pClient);
        break;
    }
    default:
        return;
    }
}

//D2Game.0x6FCC58E0
//1.14d: 0x00570E30
void __fastcall D2GAME_STATES_SendUnitStates_6FCC58E0(D2UnitStrc* pUnit, D2ClientStrc* pClient)
{
    D2BitBufferStrc bitBuffer = {};
    uint32_t nFlags[8] = {};
    D2StatStrc stats[16] = {};

    if (!pUnit || !pClient)
    {
        return;
    }

    const int32_t nTotalStatFlagCount = (sgptDataTables->nStatesTxtRecordCount + 31) / 32;
    memcpy(nFlags, D2COMMON_10494_STATES_GetStatFlags(pUnit), sizeof(uint32_t) * nTotalStatFlagCount);

    D2GSPacketSrvAA packetAA = {};
    packetAA.nHeader = 0xAAu;
    packetAA.nUnitType = pUnit->dwUnitType;
    packetAA.nUnitId = pUnit->dwUnitId;
    BITMANIP_Initialize(&bitBuffer, packetAA.pStream, 244);

    for (int32_t i = 0; i < nTotalStatFlagCount; ++i)
    {
        int32_t nBit = FOG_LeadingZeroesCount(nFlags[i]);
        while (nBit >= 0)
        {
            nFlags[i + (nBit >> 5)] &= gdwInvBitMasks[nBit & 31];

            const int32_t nStateId = nBit + 32 * i;

            D2StatesTxt* pStatesTxtRecord = SKILLS_GetStatesTxtRecord(nStateId);
            if (pStatesTxtRecord && !(pStatesTxtRecord->nStateFlags[0] & gdwBitMasks[0]))
            {
                BITMANIP_Write(&bitBuffer, nStateId, 8);

                D2StatListStrc* pStatList = STATLIST_GetStatListFromUnitAndState(pUnit, nStateId);
                if (pStatList)
                {
                    const int32_t nStatCount = STATLIST_GetBaseStatsData(pStatList, stats, std::size(stats));
                    if (nStatCount > 0)
                    {
                        BITMANIP_Write(&bitBuffer, 1, 1);

                        for (int32_t j = 0; j < nStatCount; ++j)
                        {
                            int32_t nValue = stats[j].nValue;
                            const int32_t nLayer = stats[j].nLayer;
                            const int32_t nStatId = *(&stats[0].nStat + 4 * j);

                            D2ItemStatCostTxt* pItemStatCostTxtRecord = SKILLS_GetItemStatCostTxtRecord(nStatId);
                            if (pItemStatCostTxtRecord && pItemStatCostTxtRecord->nSendBits)
                            {
                                if (pItemStatCostTxtRecord->nSendBits < 32u)
                                {
                                    if (pItemStatCostTxtRecord->dwItemStatFlags & gdwBitMasks[ITEMSTATCOSTFLAGINDEX_SIGNED])
                                    {
                                        const int32_t nMin = -(1 << (pItemStatCostTxtRecord->nSendBits - 1));
                                        const int32_t nMax = (1 << (pItemStatCostTxtRecord->nSendBits - 1)) - 1;
                                        nValue = D2Clamp(nValue, nMin, nMax);
                                    }
                                    else
                                    {
                                        const uint32_t nMax = (1 << pItemStatCostTxtRecord->nSendBits) - 1;
                                        nValue &= (nValue <= 0) - 1;
                                        nValue = std::min((uint32_t)nValue, nMax);
                                    }
                                }

                                BITMANIP_Write(&bitBuffer, nStatId, 9);

                                if (pItemStatCostTxtRecord->nSendParamBits)
                                {
                                    BITMANIP_Write(&bitBuffer, nLayer, pItemStatCostTxtRecord->nSendParamBits);
                                }

                                BITMANIP_Write(&bitBuffer, nValue, pItemStatCostTxtRecord->nSendBits);
                            }
                        }

                        BITMANIP_Write(&bitBuffer, 0x1FF, 9);
                    }
                    else
                    {
                        BITMANIP_Write(&bitBuffer, 0, 1);
                    }
                }
                else
                {
                    BITMANIP_Write(&bitBuffer, 0, 1);
                }
            }

            nBit = FOG_LeadingZeroesCount(nFlags[i]);
        }
    }

    BITMANIP_Write(&bitBuffer, 0xFFu, 8);

    packetAA.nSize = BITMANIP_GetSize(&bitBuffer) + 7;
    D2GAME_PACKETS_SendPacket_6FC3C710(pClient, &packetAA, packetAA.nSize);
}

//D2Game.0x6FCC5BE0
//1.14d: 0x005711D0
void __fastcall D2GAME_STATES_SendUnitStateUpdates_6FCC5BE0(D2UnitStrc* pUnit, D2ClientStrc* pClient)
{
    uint32_t nFlags[8] = {};
    D2StatStrc stats[16] = {};

    if (!pUnit || !pClient)
    {
        return;
    }
    
    const int32_t nTotalStatFlagCount = (sgptDataTables->nStatesTxtRecordCount + 31) / 32;
    memcpy(nFlags, STATES_GetGfxStateFlags(pUnit), sizeof(uint32_t) * nTotalStatFlagCount);

    D2BitBufferStrc bitBuffer = {};
    for (int32_t i = 0; i < nTotalStatFlagCount; ++i)
    {
        int32_t nBit = FOG_LeadingZeroesCount(nFlags[i]);
        while (nBit >= 0)
        {
            nFlags[i + (nBit >> 5)] &= gdwInvBitMasks[nBit & 31];

            const int32_t nStateId = nBit + 32 * i;
            D2StatesTxt* pStatesTxtRecord = SKILLS_GetStatesTxtRecord(nStateId);
            if (pStatesTxtRecord && !(pStatesTxtRecord->nStateFlags[0] & gdwBitMasks[0]))
            {
                if (STATES_CheckState(pUnit, nStateId))
                {
                    D2StatListStrc* pStatList = STATLIST_GetStatListFromUnitAndState(pUnit, nStateId);
                    if (pStatList)
                    {
                        const int32_t nStatCount = STATLIST_GetBaseStatsData(pStatList, stats, 16);
                        if (nStatCount > 0)
                        {
                            D2GSPacketSrvA8 packetA8 = {};

                            packetA8.nHeader = 0xA8u;
                            packetA8.nUnitType = pUnit->dwUnitType;
                            packetA8.nUnitId = pUnit->dwUnitId;
                            packetA8.nState = nStateId;
                            BITMANIP_Initialize(&bitBuffer, packetA8.pStream, 244);

                            for (int32_t j = 0; j < nStatCount; ++j)
                            {
                                int32_t nValue = stats[j].nValue;
                                const int32_t nLayer = stats[j].nLayer;
                                const int32_t nStatId = *(&stats[0].nStat + 4 * j);

                                D2ItemStatCostTxt* pItemStatCostTxtRecord = SKILLS_GetItemStatCostTxtRecord(nStatId);
                                if (pItemStatCostTxtRecord && pItemStatCostTxtRecord->nSendBits)
                                {
                                    if (pItemStatCostTxtRecord->nSendBits < 32u)
                                    {
                                        if (pItemStatCostTxtRecord->dwItemStatFlags & gdwBitMasks[ITEMSTATCOSTFLAGINDEX_SIGNED])
                                        {
                                            const int32_t nMin = -(1 << (pItemStatCostTxtRecord->nSendBits - 1));
                                            const int32_t nMax = (1 << (pItemStatCostTxtRecord->nSendBits - 1)) - 1;
                                            nValue = D2Clamp(nValue, nMin, nMax);
                                        }
                                        else
                                        {
                                            const uint32_t nMax = (1 << pItemStatCostTxtRecord->nSendBits) - 1;
                                            nValue &= (nValue <= 0) - 1;
                                            nValue = std::min((uint32_t)nValue, nMax);
                                        }
                                    }

                                    BITMANIP_Write(&bitBuffer, nStatId, 9);

                                    if (pItemStatCostTxtRecord->nSendParamBits)
                                    {
                                        BITMANIP_Write(&bitBuffer, nLayer, pItemStatCostTxtRecord->nSendParamBits);
                                    }

                                    BITMANIP_Write(&bitBuffer, nValue, pItemStatCostTxtRecord->nSendBits);
                                }
                            }

                            BITMANIP_Write(&bitBuffer, 0x1FFu, 9);

                            packetA8.nSize = BITMANIP_GetSize(&bitBuffer) + 8;
                            D2GAME_PACKETS_SendPacket_6FC3C710(pClient, &packetA8, packetA8.nSize);
                        }
                        else
                        {
                            D2GAME_PACKETS_SendPacket0xA7_StateOn_6FC3FC20(pClient, pUnit->dwUnitType, pUnit->dwUnitId, nStateId);
                        }
                    }
                    else
                    {
                        D2GAME_PACKETS_SendPacket0xA7_StateOn_6FC3FC20(pClient, pUnit->dwUnitType, pUnit->dwUnitId, nStateId);
                    }
                }
                else
                {
                    D2GAME_PACKETS_SendPacket0xA9_StateOff_6FC3FC50(pClient, pUnit->dwUnitType, pUnit->dwUnitId, nStateId);
                }
            }

            nBit = FOG_LeadingZeroesCount(nFlags[i]);
        }
    }
}

//D2Game.0x6FCC5F00
//1.14d: 0x00571580
void __fastcall D2GAME_STATES_SendStates_6FCC5F00(D2UnitStrc* pUnit, D2ClientStrc* pClient, int32_t bPlayer)
{
    if (bPlayer)
    {
        D2GAME_STATES_SendUnitStates_6FCC58E0(pUnit, pClient);
    }
    else
    {
        D2GAME_STATES_SendUnitStateUpdates_6FCC5BE0(pUnit, pClient);
    }
}

//D2Game.0x6FCC5F20
//1.14d: 0x005715A0
void __fastcall SUNITMSG_SendOverlay(D2UnitStrc* pItem, D2ClientStrc* pClient)
{
    D2StatListStrc* pStatList = STATLIST_GetStatListFromUnitAndFlag(pItem, 0x80);
    if (pStatList)
    {
        const int32_t nOverlay = STATLIST_GetTotalStatValue_Layer0((D2StatListExStrc*)pStatList, STAT_UNIT_DOOVERLAY);
        if (nOverlay >= 0 && nOverlay <= sgptDataTables->nOverlayTxtRecordCount)
        {
            if (pItem)
            {
                D2GAME_PACKETS_SendPacket0x11_6FC3F4A0(pClient, pItem->dwUnitType, pItem->dwUnitId, nOverlay);
            }
            else
            {
                D2GAME_PACKETS_SendPacket0x11_6FC3F4A0(pClient, 6, -1, nOverlay);
            }
        }
    }
}

//D2Game.0x6FCC5F80
//1.14d: 0x00571600
void __fastcall D2GAME_PACKETS_SendPacket0x0A_RemoveObject_6FCC5F80(D2UnitStrc* pUnit, D2ClientStrc* pClient)
{
    D2GAME_PACKETS_SendPacket0x0A_RemoveObject_6FC3D3A0(pClient, 0x0A, pUnit->dwUnitType, pUnit->dwUnitId);
}

//D2Game.0x6FCC5FA0
//1.14d: 0x00571620
void __fastcall SUNITMSG_SendHoverText(D2UnitStrc* pUnit, D2ClientStrc* pClient)
{
    if (!pUnit)
    {
        D2GAME_PACKETS_SendPacketSize06_6FC3C850(pClient, 0x76u, 6, -1);
        return;
    }

    if (!pUnit->pHoverText)
    {
        D2GAME_PACKETS_SendPacketSize06_6FC3C850(pClient, 0x76u, pUnit->dwUnitType, pUnit->dwUnitId);
        return;
    }

    //CLIENTS_GetGame(pClient);
    D2UnitStrc* pClientPlayer = CLIENTS_GetPlayerFromClient(pClient, 0);
    if (!pClientPlayer || pUnit->dwUnitType != UNIT_PLAYER || !PLAYERLIST_CheckFlag(pClientPlayer, pUnit, 4) && !PLAYERLIST_CheckFlag(pUnit, pClientPlayer, 2))
    {
        D2GSPacketSrv26 packet26 = {};
        packet26.nHeader = 0x26u;
        packet26.nMessageType = 5;
        packet26.nLang = CHAT_GetLangIdFromHoverMsg(pUnit->pHoverText);
        packet26.nUnitType = pUnit->dwUnitType;
        packet26.dwUnitGUID = pUnit->dwUnitId;
        packet26.szName[0] = 0;
        memset(packet26.szMessage, 0, sizeof(packet26.szMessage));
        CHAT_CopyHoverMsgToBuffer(pUnit->pHoverText, packet26.szMessage);
        D2GAME_PACKETS_SendPacket0x26_ServerMessage_6FC3DDF0(pClient, &packet26);
    }
}

//D2Game.0x6FCC6080
//1.14d: 0x00571740
void __fastcall D2GAME_UpdateUnit_6FCC6080(D2UnitStrc* pPlayer, D2ClientStrc* pClient)
{
    if (!pPlayer->pUpdateUnit || CLIENTS_GetPlayerFromClient(pClient, 0) == pPlayer->pUpdateUnit)
    {
        D2GSPacketSrv2C packet2C = {};
        packet2C.nHeader = 0x2Cu;
        packet2C.dwUnitGUID = pPlayer->dwUnitId;
        packet2C.nUnitType = pPlayer->dwUnitType;
        packet2C.nSoundEvent = pPlayer->nUpdateType;
        SCMD_SendPacket0x2C(pClient, &packet2C);
    }
}

//D2Game.0x6FCC60D0
//1.14d: 0x005717C0
void __fastcall SUNITMSG_QueueItemSkillOnUnit(D2UnitStrc* pUnit, int16_t nSkillId, uint8_t nSkillLevel, uint8_t nUnitType, int32_t nUnitGUID, uint8_t nSkillParam)
{
    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_ALLOC_POOL(pUnit->pMemoryPool, 20 + kPacketNodeShift);
    pMsg->pNext = nullptr;
    pMsg->nHeader = 0x99u;
    PacketField<int16_t>(pMsg, 8) = nSkillId;
    PacketField<uint8_t>(pMsg, 10) = nSkillLevel;
    PacketField<uint8_t>(pMsg, 11) = nUnitType;
    PacketField<int32_t>(pMsg, 12) = nUnitGUID;
    PacketField<uint8_t>(pMsg, 16) = nSkillParam;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC6150
//1.14d: 0x00571840
void __fastcall SUNITMSG_QueueItemSkillOnPosition(D2UnitStrc* pUnit, int16_t nSkillId, uint8_t nSkillLevel, int16_t nX, int16_t nY, uint8_t nSkillParam)
{
    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_ALLOC_POOL(pUnit->pMemoryPool, 20 + kPacketNodeShift);
    pMsg->pNext = nullptr;
    pMsg->nHeader = 0x9Au;
    PacketField<int16_t>(pMsg, 8) = nSkillId;
    PacketField<uint8_t>(pMsg, 10) = nSkillLevel;
    PacketField<int16_t>(pMsg, 12) = nX;
    PacketField<int16_t>(pMsg, 14) = nY;
    PacketField<uint8_t>(pMsg, 16) = nSkillParam;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC61D0
//1.14d: 0x005718C0
void __fastcall D2GAME_MERCS_SendStat_6FCC61D0(D2UnitStrc* pUnit, uint16_t nStatId, int32_t nValue)
{
    D2_ASSERT(nStatId < ((uint8_t)-1));

    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_ALLOC_POOL(pUnit->pMemoryPool, 20 + kPacketNodeShift);
    pMsg->pNext = nullptr;
    pMsg->nHeader = 0x9Eu;
    PacketField<int32_t>(pMsg, 8) = pUnit->dwUnitId;
    PacketField<uint8_t>(pMsg, 12) = nStatId;
    PacketField<int32_t>(pMsg, 16) = nValue;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC6270
//1.14d: 0x00571A10
void __fastcall SUNITMSG_QueueLifeUpdate(D2UnitStrc* pUnit, uint8_t nLifePercent)
{
    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_CALLOC_POOL(pUnit->pMemoryPool, 20 + kPacketNodeShift);

    pMsg->pNext = nullptr;
    pMsg->nHeader = 0xABu;
    PacketField<uint8_t>(pMsg, 8) = pUnit->dwUnitType;
    PacketField<int32_t>(pMsg, 12) = pUnit->dwUnitId;
    PacketField<uint8_t>(pMsg, 16) = nLifePercent;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC6300
//1.14d: 0x00571AA0
void __fastcall SUNITMSG_QueueSkillEvent(D2UnitStrc* pUnit, D2UnitStrc* pTargetUnit, int16_t nSkillId, int16_t nSkillLevel, int32_t nX, int32_t nY, uint8_t nSkillParam)
{
    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_CALLOC_POOL(pUnit->pMemoryPool, 40 + kPacketNodeShift);

    pMsg->pNext = nullptr;
    pMsg->nHeader = 0xA3u;
    PacketField<uint8_t>(pMsg, 8) = nSkillParam;
    PacketField<int16_t>(pMsg, 12) = nSkillLevel;
    PacketField<int16_t>(pMsg, 10) = nSkillId;
    PacketField<int32_t>(pMsg, 16) = pUnit->dwUnitType;
    PacketField<int32_t>(pMsg, 20) = pUnit->dwUnitId;
    PacketField<int32_t>(pMsg, 32) = nX;
    PacketField<int32_t>(pMsg, 36) = nY;

    if (pTargetUnit)
    {
        PacketField<int32_t>(pMsg, 24) = pTargetUnit->dwUnitType;
        PacketField<int32_t>(pMsg, 28) = pTargetUnit->dwUnitId;
    }
    else
    {
        PacketField<int32_t>(pMsg, 28) = -1;
    }

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC63D0
//1.14d: 0x00571B70
void __fastcall SUNITMSG_QueueSkillStop(D2UnitStrc* pUnit, int16_t nSkillId)
{
    STATES_ToggleState(pUnit, STATE_SKILL_MOVE, 0);

    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_CALLOC_POOL(pUnit->pMemoryPool, 20 + kPacketNodeShift);

    pMsg->pNext = nullptr;
    pMsg->nHeader = 0xA5u;
    PacketField<uint8_t>(pMsg, 8) = pUnit->dwUnitType;
    PacketField<uint32_t>(pMsg, 12) = pUnit->dwUnitId;
    PacketField<int16_t>(pMsg, 16) = nSkillId;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC6470
//1.14d: 0x00571C00
void __fastcall SUNITMSG_QueueMonsterPrecache(D2UnitStrc* pUnit, int16_t nMonsterClassId)
{
    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_ALLOC_POOL(pUnit->pMemoryPool, 12 + kPacketNodeShift);
    pMsg->pNext = nullptr;
    pMsg->nHeader = 0xA4u;
    PacketField<int16_t>(pMsg, 8) = nMonsterClassId;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC64D0
//1.14d: 0x00571C60
void __fastcall SUNITMSG_QueueSkillSelection(D2UnitStrc* pUnit, uint8_t bLeftSkill, int16_t nSkillId, int32_t nOwnerGUID)
{
    D2UnitPacketListStrc* pMsg = (D2UnitPacketListStrc*)D2_ALLOC_POOL(pUnit->pMemoryPool, 16 + kPacketNodeShift);
    pMsg->pNext = nullptr;
    pMsg->nHeader = 0x23u;
    PacketField<int32_t>(pMsg, 8) = nOwnerGUID;
    PacketField<int16_t>(pMsg, 12) = nSkillId;
    PacketField<uint8_t>(pMsg, 14) = bLeftSkill;

    if (!pUnit->pMsgFirst)
    {
        pUnit->pMsgFirst = pMsg;
    }

    if (pUnit->pMsgLast)
    {
        pUnit->pMsgLast->pNext = pMsg;
    }

    pUnit->pMsgLast = pMsg;
    UNITROOM_RefreshUnit(pUnit);
}

//D2Game.0x6FCC6540
//1.14d: 0x00571CD0
void __fastcall SUNITMSG_SendQueuedMessages(D2UnitStrc* pUnit, D2ClientStrc* pClient)
{
    for (D2UnitPacketListStrc* pMsg = pUnit->pMsgFirst; pMsg; pMsg = pMsg->pNext)
    {
        switch (pMsg->nHeader)
        {
        case 0x23:
            D2GAME_PACKETS_SendPacket0x23_6FC3DC60(pClient, pUnit->dwUnitType, pUnit->dwUnitId, PacketField<uint8_t>(pMsg, 14), PacketField<uint16_t>(pMsg, 12), PacketField<uint32_t>(pMsg, 8));
            break;

        case 0x99:
            SCMD_SendSkillUseOnUnit(pClient, pUnit->dwUnitType, pUnit->dwUnitId, PacketField<uint8_t>(pMsg, 11), PacketField<uint32_t>(pMsg, 12), PacketField<uint16_t>(pMsg, 8), PacketField<uint8_t>(pMsg, 16), PacketField<uint8_t>(pMsg, 10), 1);
            break;

        case 0x9A:
            SCMD_SendUnitActionAtPosition(pClient, pUnit->dwUnitType, pUnit->dwUnitId, PacketField<uint16_t>(pMsg, 12), PacketField<uint16_t>(pMsg, 14), PacketField<uint16_t>(pMsg, 8), PacketField<uint8_t>(pMsg, 16), PacketField<uint8_t>(pMsg, 10), 1);
            break;

        case 0x9E:
            D2GAME_PACKETS_SendPacket0x9E_9F_A0_6FC3D520(pClient, pUnit, PacketField<uint8_t>(pMsg, 12), PacketField<uint32_t>(pMsg, 16));
            break;

        case 0xA1:
            D2GAME_PACKETS_SendPacket0xA0_A1_A2_6FC3D610(pClient, pUnit, PacketField<uint8_t>(pMsg, 12), PacketField<uint32_t>(pMsg, 16), PacketField<uint32_t>(pMsg, 20));
            break;

        case 0xA3:
            D2GAME_PACKETS_SendPacket0xA3_6FC3D730(pClient, PacketField<uint8_t>(pMsg, 8), PacketField<uint16_t>(pMsg, 10), PacketField<uint16_t>(pMsg, 12), PacketField<uint32_t>(pMsg, 16), PacketField<uint32_t>(pMsg, 20), PacketField<uint32_t>(pMsg, 24), PacketField<uint32_t>(pMsg, 28), PacketField<uint32_t>(pMsg, 32), PacketField<uint32_t>(pMsg, 36));
            break;

        case 0xA4:
            D2GAME_PACKETS_SendPacket0xA4_6FC3FC00(pClient, PacketField<uint16_t>(pMsg, 8));
            break;

        case 0xA5:
            D2GAME_PACKETS_SendPacket0xA5_6FC3D7F0(pClient, PacketField<uint8_t>(pMsg, 8), PacketField<uint32_t>(pMsg, 12), PacketField<uint16_t>(pMsg, 16));
            break;

        case 0xAB:
            if (!(pUnit->dwFlags & UNITFLAG_UPGRLIFENHITCLASS))
            {
                D2UnitStrc* pPlayer = CLIENTS_GetPlayerFromClient(pClient, 0);
                if (pPlayer && SUNIT_AreUnitsHostile(pPlayer->pGame, pPlayer, pUnit))
                {
                    D2GAME_PACKETS_SendPacket0xAB_6FC3D7B0(pClient, PacketField<uint8_t>(pMsg, 8), PacketField<uint32_t>(pMsg, 12), PacketField<uint8_t>(pMsg, 16));
                }
            }
            break;

        default:
            break;
        }
    }
}

//D2Game.0x6FCC6790
//1.14d: 0x00571F40
void __fastcall D2GAME_SUNITMSG_FreeUnitMessages_6FCC6790(D2UnitStrc* pUnit)
{
    D2UnitPacketListStrc* pMsg = pUnit->pMsgFirst;
    if (pMsg)
    {
        do
        {
            D2UnitPacketListStrc* pNext = pMsg->pNext;
            D2_FREE_POOL(pUnit->pMemoryPool, pMsg);
            pMsg = pNext;
        }
        while (pMsg);

        pUnit->pMsgLast = nullptr;
        pUnit->pMsgFirst = nullptr;
    }
}
