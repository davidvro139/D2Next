// In-process build: the D2MOO functions the level loader and the game session use, by the export number they have in
// the DLLs (source/*/definitions/*.1.10f.def), so NativeLink.h finds them the same way in both builds.
#include "NativeLink.h"
#include "ProgramPath.h"
#include <windows.h>
#include <Storm.h>
#include <FogHost.h>
#include <D2Collision.h>
#include <D2Composit.h>
#include <D2DataTbls.h>
#include <D2Dungeon.h>
#include <D2Monsters.h>
#include <D2Environment.h>
#include <D2Inventory.h>
#include <D2Items.h>
#include <D2QuestRecord.h>
#include <D2Skills.h>
#include <D2States.h>
#include <D2StatList.h>
#include <UselessOrdinals.h>
#include <D2Waypoints.h>
#include <DataTbls/ItemsTbls.h>
#include <Drlg/D2DrlgDrlg.h>
#include <Units/Units.h>
uint8_t* __stdcall D2CMP_MixPalette(uint8_t nTrans,int nColor); // D2CMP #10098 (src/x64/TileLibrary.cpp)
#include <GAME/Game.h>
#include <GAME/Task.h>
#include <PLAYER/PlrSave.h>
#include <cstring>
#include <mutex>
extern "C" {
void __cdecl D2GameHost_AllowTimeouts(int32_t bAllow);
void __cdecl D2GameHost_SetPacketTap(void(__cdecl* pfnTap)(int32_t,const void*,int32_t));
void __cdecl D2GameHost_AddExperience(D2UnitStrc* pPlayer,uint32_t nExperience);
void __cdecl D2GameHost_Kill(D2UnitStrc* pPlayer);
void __cdecl D2GameHost_KillHireling(D2UnitStrc* pPlayer);
D2UnitStrc* __cdecl D2GameHost_GetHireling(D2UnitStrc* pPlayer);
D2UnitStrc* __cdecl D2GameHost_GiveItem(D2UnitStrc* pPlayer,uint32_t dwCode,int32_t nQuality);
D2UnitStrc* __cdecl D2GameHost_GetUnit(uint16_t nGameId,int32_t nUnitType,int32_t nUnitGUID);
D2UnitStrc* __cdecl D2GameHost_GetClientPlayer(uint16_t nGameId,int32_t nClientId);
int32_t __cdecl D2GameHost_SaveClient(uint16_t nGameId,int32_t nClientId);
void __cdecl Mock_PushMessage(int list,const void* data,int size);
int __cdecl Mock_PopSent(int* list,int* client,void* buffer,int capacity);
void __cdecl Mock_Reset();
}
namespace d2::native {
namespace {
struct Export {Library library;int ordinal;void* address;};
template<class F> void* address(F* f) {return reinterpret_cast<void*>(f);}
const Export exports[]={
    {Library::Storm,252,address(&SFileCloseArchive)},
    {Library::Storm,266,address(&SFileOpenArchive)},
    {Library::D2CMP,10098,address(&D2CMP_MixPalette)},
    {Library::D2Common,10005,address(&DRLG_GetLevel)},
    {Library::D2Common,10006,address(&DRLG_InitLevel)},
    {Library::D2Common,10030,address(&DUNGEON_GetFloorTilesFromRoom)},
    {Library::D2Common,10031,address(&DUNGEON_GetWallTilesFromRoom)},
    {Library::D2Common,10032,address(&DUNGEON_GetRoofTilesFromRoom)},
    {Library::D2Common,10034,address(&DUNGEON_GetRoomCoordinates)},
    {Library::D2Common,10038,address(&DUNGEON_AllocAct)},
    {Library::D2Common,10039,address(&DUNGEON_FreeAct)},
    {Library::D2Common,10042,&sgptDataTables},
    {Library::D2Common,10057,address(&DUNGEON_GetLevelIdFromRoom)},
    {Library::D2Common,10065,address(&DUNGEON_StreamRoomAtCoords)},
    {Library::D2Common,10088,address(&DUNGEON_GetDrlgFromAct)},
    {Library::D2Common,10120,address(&COLLISION_CheckMaskWithSizeXY)},
    {Library::D2Common,10257,address(&INVENTORY_GetItemFromBodyLoc)},
    {Library::D2Common,10262,address(&INVENTORY_GetCursorItem)},
    {Library::D2Common,10277,address(&INVENTORY_GetFirstItem)},
    {Library::D2Common,10304,address(&INVENTORY_GetNextItem)},
    {Library::D2Common,10305,address(&INVENTORY_UnitIsItem)},
    {Library::D2Common,10424,address(&UNITS_GetPlayerData)},
    {Library::D2Common,10291,address(&INVENTORY_GetWieldType)},
    {Library::D2Common,10339,address(&UNITS_GetStashGoldLimit)},
    {Library::D2Common,10431,address(&UNITS_GetDefense)},
    {Library::D2Common,10432,address(&UNITS_GetAttackRate)},
    {Library::D2Common,10434,address(&D2Common_10434)},
    {Library::D2Common,10439,address(&UNITS_GetInventoryGoldLimit)},
    {Library::D2Common,10442,address(&UNITS_GetItemComponentId)},
    {Library::D2Common,10520,address(&STATLIST_UnitGetItemStatOrSkillStatValue)},
    {Library::D2Common,10521,address(&STATLIST_GetUnitBaseStat)},
    {Library::D2Common,10567,address(&STATLIST_GetMaxDurabilityFromUnit)},
    {Library::D2Common,10807,address(&ITEMS_GetStrengthBonus)},
    {Library::D2Common,10808,address(&ITEMS_GetDexBonus)},
    {Library::D2Common,11023,address(&SKILLS_GetWeaponMasteryBonus)},
    {Library::D2Common,10765,address(&ITEMS_CheckWeaponIfTwoHanded)},
    {Library::D2Common,10436,address(&UNITS_GetFrameBonus)},
    {Library::D2Common,10382,address(&UNITS_GetEventFrameInfo)},
    {Library::D2Common,11082,address(&MONSTERS_HirelingInit)},
    {Library::D2Common,11086,address(&MONSTERS_GetActFromHirelingTxt)},
    {Library::D2Common,10001,address(&DRLG_GetActNoFromLevelId)},
    {Library::D2Common,10923,address(&ENVIRONMENT_UpdatePeriodOfDay)},
    {Library::D2Common,10930,address(&ENVIRONMENT_GetCycleIndexFromAct)},
    {Library::D2Common,10926,address(&ENVIRONMENT_GetIntensityFromAct)},
    {Library::D2Common,10927,address(&ENVIRONMENT_GetLightColorFromAct)},
    {Library::D2Common,10932,address(&ENVIRONMENT_InitializeEnvironment)},
    {Library::D2Common,10934,address(&ENVIRONMENT_GetTimeRateFromAct)},
    {Library::D2Common,10484,address(&STATLIST_GetStatListFromUnitStateAndFlag)},
    {Library::D2Common,11243,address(&STATLIST_GetBaseStatsData)},
    {Library::D2Common,11013,address(&D2COMMON_11013_ConvertMode)},
    {Library::D2Common,10487,address(&STATES_CheckState)},
    {Library::D2Common,10517,address(&STATLIST_SetUnitStat)},
    {Library::D2Common,10519,address(&STATLIST_UnitGetStatValue)},
    {Library::D2Common,10473,address(&D2COMMON_10473)},
    {Library::D2Common,10480,address(&STATLIST_GetStatListFromUnitAndState)},
    {Library::D2Common,10575,address(&DATATBLS_UnloadAllBins)},
    {Library::D2Common,10576,address(&DATATBLS_LoadAllTxts)},
    {Library::D2Common,10593,address(&DATATBLS_GetCharstatsTxtTable)},
    {Library::D2Common,10600,address(&DATATBLS_GetItemsTxtRecord)},
    {Library::D2Common,10284,address(&INVENTORY_GetItemCount)},
    {Library::D2Common,10299,address(&D2Common_10299)},
    {Library::D2Common,10707,address(&ITEMS_CheckItemFlag)},
    {Library::D2Common,10756,address(&ITEMS_CheckRequirements)},
    {Library::D2Common,10757,address(&ITEMS_GetLevelRequirement)},
    {Library::D2Common,10758,address(&ITEMS_CheckBodyLocation)},
    {Library::D2Common,10604,address(&DATATBLS_GetMagicAffixTxtRecord)},
    {Library::D2Common,10606,address(&DATATBLS_GetRareAffixTxtRecord)},
    {Library::D2Common,10768,address(&ITEMS_CheckIfBeltable)},
    {Library::D2Common,10781,address(&ITEMS_GetAmmoType)},
    {Library::D2Common,10783,address(&ITEMS_GetQuiverType)},
    {Library::D2Common,10810,address(&ITEMS_CheckIfSocketable)},
    {Library::D2Common,10815,address(&ITEMS_GetMaxSockets)},
    {Library::D2Common,10816,address(&ITEMS_GetSockets)},
    {Library::D2Common,10817,address(&ITEMS_AddSockets)},
    {Library::D2Common,10821,address(&ITEMS_IsSocketFiller)},
    {Library::D2Common,10833,address(&ITEMS_AreStackablesEqual)},
    {Library::D2Common,10650,address(&DATATBLS_GetArmTypeTxtRecord)},
    {Library::D2Common,10747,address(&ITEMS_GetAltGfx)},
    {Library::D2Common,10775,address(&ITEMS_GetTransactionCost)},
    {Library::D2Common,10829,address(&ITEMS_GetColor)},
    {Library::D2Common,10887,address(&COMPOSIT_GetWeaponClassCode)},
    {Library::D2Common,10891,address(&COMPOSIT_GetArmorTypeFromComponent)},
    {Library::D2Common,10628,address(&DATATBLS_GetLevelThreshold)},
    {Library::D2Common,10629,address(&DATATBLS_GetMaxLevel)},
    {Library::D2Common,10941,address(&SKILLS_RefreshPassiveSkills)},
    {Library::D2Common,10947,address(&SKILLS_GetNextSkill)},
    {Library::D2Common,10948,address(&SKILLS_GetFirstSkillFromSkillList)},
    {Library::D2Common,10953,address(&SKILLS_AssignSkill)},
    {Library::D2Common,10963,address(&SKILLS_GetSkillIdFromSkill)},
    {Library::D2Common,10968,address(&SKILLS_GetSkillLevel)},
    {Library::D2Common,10950,address(&SKILLS_GetHighestLevelSkillFromUnitAndId)},
    {Library::D2Common,10999,address(&SKILLS_GetToHitFactor)},
    {Library::D2Common,11011,address(&SKILLS_GetQuantity)},
    {Library::D2Common,11041,address(&SKILLS_GetClassSkillId)},
    {Library::D2Common,11042,address(&SKILLS_GetPlayerSkillCount)},
    {Library::D2Common,11276,address(&SKILLS_EvaluateSkillFormula)},
    {Library::D2Common,11108,address(&QUESTRECORD_SetQuestState)},
    {Library::D2Common,11147,address(&WAYPOINTS_ActivateWaypoint)},
    {Library::D2Game,10002,address(&GAME_InitGameDataTable)},
    {Library::D2Game,10003,address(&GAME_ProcessNetworkMessages)},
    {Library::D2Game,10004,address(&GAME_UpdateGamesProgress)},
    {Library::D2Game,10005,address(&GAME_UpdateClients)},
    {Library::D2Game,10006,address(&GAME_CloseAllGames)},
    {Library::D2Game,10007,address(&GAME_ReceiveDatabaseCharacter)},
    {Library::D2Game,10009,address(&GAME_SetGlobalAct)},
    {Library::D2Game,10010,address(&GAME_SetInitSeed)},
    {Library::D2Game,10014,address(&GAME_GetGameInformation)},
    {Library::D2Game,10017,address(&GAME_GetPlayerUnitsInfo)},
    {Library::D2Game,10018,address(&D2Game_10018)},
    {Library::D2Game,10023,address(&GAME_SetServerCallbackFunctions)},
    {Library::D2Game,10036,address(&D2GAME_10036_PLRSAVE_EnableSaveFileWriting)},
    {Library::D2Game,10039,address(&TASK_InitializeClock)},
    {Library::D2Game,10046,address(&D2Game_10046)},
    {Library::D2Game,10047,address(&GAME_CreateNewEmptyGame)},
};
struct Named {Library library;const char* name;void* address;};
const Named names[]={
    {Library::Fog,"FogHost_Configure",address(&FogHost_Configure)},
    {Library::D2Game,"D2GameHost_AllowTimeouts",address(&D2GameHost_AllowTimeouts)},
    {Library::D2Game,"D2GameHost_SetPacketTap",address(&D2GameHost_SetPacketTap)},
    {Library::D2Game,"D2GameHost_AddExperience",address(&D2GameHost_AddExperience)},
    {Library::D2Game,"D2GameHost_GetUnit",address(&D2GameHost_GetUnit)},
    {Library::D2Game,"D2GameHost_GetClientPlayer",address(&D2GameHost_GetClientPlayer)},
    {Library::D2Game,"D2GameHost_SaveClient",address(&D2GameHost_SaveClient)},
    {Library::D2Game,"D2GameHost_Kill",address(&D2GameHost_Kill)},
    {Library::D2Game,"D2GameHost_KillHireling",address(&D2GameHost_KillHireling)},
    {Library::D2Game,"D2GameHost_GetHireling",address(&D2GameHost_GetHireling)},
    {Library::D2Game,"D2GameHost_GiveItem",address(&D2GameHost_GiveItem)},
    {Library::Net,"Mock_PushMessage",address(&Mock_PushMessage)},
    {Library::Net,"Mock_PopSent",address(&Mock_PopSent)},
    {Library::Net,"Mock_Reset",address(&Mock_Reset)},
};
}
void* linkedExport(Library library,int ordinal) {
    for(const auto& e:exports)if(e.library==library&&e.ordinal==ordinal)return e.address;
    return nullptr;
}
void* linkedNamed(Library library,const char* name) {
    for(const auto& n:names)if(n.library==library&&!std::strcmp(n.name,name))return n.address;
    return nullptr;
}
void useGameData(const std::filesystem::path& gameDirectory) {
    static std::once_flag once;
    std::call_once(once,[&] {
        namespace fs=std::filesystem;
#ifndef _WIN32
        // Fog finds its folders from the executable on Windows; here there is no such folder (Android): name them. The
        // other settings keep Fog's defaults.
        static const std::string install=programDirectory().string(),logs=(programDirectory()/"Logs").string();
        static const std::string saves=[] {
            const auto directory=environmentPath("D2NEXT_SAVE_DIR");
            return (directory.empty()?programDirectory()/"Save":directory).string();
        }();
        FogHostConfig config{};config.structSize=sizeof(config);
        config.installPath=install.c_str();config.savePath=saves.c_str();config.logDirectory=logs.c_str();
        config.isExpansion=1;config.haltTerminatesProcess=1;
        FogHost_Configure(&config);
#endif
        // The 1.10f data tables are only in the patch archive; without it D2Common reads a table that is not there and crashes.
        const auto core=gameFile(gameDirectory,"D2SE/CORES/1.10f/Patch_D2.mpq"),patch=gameFile(gameDirectory,"Patch_D2.mpq");
        if(!fs::exists(core)&&!fs::exists(patch))
            throw std::runtime_error("Patch_D2.mpq (the Diablo II 1.10f patch) is missing from "+gameDirectory.string()+". Copy it there from your Diablo II folder.");
        const fs::path paths[]={fs::exists(core)?core:patch,gameFile(gameDirectory,"d2exp.mpq"),gameFile(gameDirectory,"d2data.mpq"),gameFile(gameDirectory,"d2char.mpq"),gameFile(gameDirectory,"d2sfx.mpq")};
        int priority=5000;
        for(const auto& path:paths) {
            HSARCHIVE handle=nullptr;
            if(fs::exists(path)&&!SFileOpenArchive(fs::absolute(path).string().c_str(),priority,2,&handle))
                throw std::runtime_error("Cannot mount archive: "+path.string());
            priority-=1000;
        }
        DATATBLS_LoadAllTxts(nullptr,1,1);
    });
}
}
