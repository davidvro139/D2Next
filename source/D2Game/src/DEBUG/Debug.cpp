#include "DEBUG/Debug.h"

#include <D2Dungeon.h>


#include "GAME/Game.h"
#include "UNIT/SUnit.h"
#include "GAME/Clients.h"
#include "PLAYER/PlrSave.h"


//D2Game.0x6FCCD2E0 (#10025)
D2UnitStrc* __stdcall DEBUG_GetUnitFromTypeAndGUID(int32_t nUnitType, int32_t nUnitGUID)
{
    D2GameStrc* pGame = sub_6FC3B160();
    if (!pGame)
    {
        return nullptr;
    }

    D2UnitStrc* pUnit = SUNIT_GetServerUnit(pGame, nUnitType, nUnitGUID);
    GAME_LeaveGlobalGamesCriticalSection();
    return pUnit;
}

#ifdef D2MOO_X64
// x64 hosts only: the function above always looks in the first game slot, so a host that closes its game and creates
// another (which gets the next slot) needs the unit from the game it names. Not part of the original DLL.
extern "C" __declspec(dllexport) D2UnitStrc* __cdecl D2GameHost_GetUnit(uint16_t nGameId, int32_t nUnitType, int32_t nUnitGUID)
{
    D2GameStrc* pGame = GAME_LockGame(GAME_GetGameGUIDFromGameId(nGameId));
    if (!pGame)
    {
        return nullptr;
    }

    D2UnitStrc* pUnit = SUNIT_GetServerUnit(pGame, nUnitType, nUnitGUID);
    GAME_LeaveGamesCriticalSection(pGame);
    return pUnit;
}

// x64 hosts only: the player unit of a client of the game (several players in one game: LAN). Not in the original DLL.
extern "C" __declspec(dllexport) D2UnitStrc* __cdecl D2GameHost_GetClientPlayer(uint16_t nGameId, int32_t nClientId)
{
    D2GameStrc* pGame = GAME_LockGame(GAME_GetGameGUIDFromGameId(nGameId));
    if (!pGame)
    {
        return nullptr;
    }

    D2ClientStrc* pClient = CLIENTS_GetClientFromClientId(pGame, nClientId);
    D2UnitStrc* pUnit = pClient ? CLIENTS_GetPlayerFromClient(pClient, 0) : nullptr;
    GAME_LeaveGamesCriticalSection(pGame);
    return pUnit;
}

// x64 hosts only: saves a client's character now (the save callback receives it), as D2Game does for every player
// when a multiplayer game (nGameType 1 or 2) ends for a client. Used before a LAN guest leaves. Not in the original DLL.
extern "C" __declspec(dllexport) int32_t __cdecl D2GameHost_SaveClient(uint16_t nGameId, int32_t nClientId)
{
    D2GameStrc* pGame = GAME_LockGame(GAME_GetGameGUIDFromGameId(nGameId));
    if (!pGame)
    {
        return 0;
    }

    int32_t bSaved = 0;
    D2ClientStrc* pClient = CLIENTS_GetClientFromClientId(pGame, nClientId);
    if (D2UnitStrc* pPlayer = pClient ? CLIENTS_GetPlayerFromClient(pClient, 0) : nullptr)
    {
        bSaved = D2GAME_SAVE_WriteFile_6FC8A500(pGame, pPlayer, CLIENTS_GetName(pClient), 0);
    }
    GAME_LeaveGamesCriticalSection(pGame);
    return bSaved;
}
#endif

//D2Game.0x6FCCD310 (#10026)
D2DrlgActStrc* __stdcall DEBUG_GetAct(uint8_t nAct)
{
    D2GameStrc* pGame = sub_6FC3B160();

    D2_ASSERT(pGame);

    D2DrlgActStrc* pAct = pGame->pAct[nAct];
    GAME_LeaveGlobalGamesCriticalSection();
    return pAct;
}

//D2Game.0x6FCCD350 (#10027)
int32_t __stdcall DEBUG_GetFreeMonsterIndex()
{
    D2GameStrc* pGame = sub_6FC3B160();

    D2_ASSERT(pGame);

    int32_t nIndex = 0;
    for (int32_t i = 0; i < 128; ++i)
    {
        for (D2UnitStrc* pUnit = pGame->pUnitList[1][i]; pUnit; pUnit = SUNIT_GetNextUnitFromList(pUnit))
        {
            ++nIndex;
        }
    }

    GAME_LeaveGlobalGamesCriticalSection();
    return nIndex;
}

//D2Game.0x6FCCD3B0 (#10028)
D2ActiveRoomStrc* __stdcall DEBUG_GetRoomBySubtileCoordinates(uint8_t nAct, int32_t nX, int32_t nY)
{
    D2GameStrc* pGame = sub_6FC3B160();

    D2_ASSERT(nAct < NUM_ACTS);

    D2ActiveRoomStrc* pRoom = DUNGEON_FindRoomBySubtileCoordinates(pGame->pAct[nAct], nX, nY);
    GAME_LeaveGlobalGamesCriticalSection();
    return pRoom;
}
