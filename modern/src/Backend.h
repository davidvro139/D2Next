#pragma once
#include "Movement.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace d2 {
// A TCP/IP game whose host is gone (or could not be reached): the game goes back to the menus with what() rather than
// ending the program.
struct ConnectionLost:std::runtime_error {using std::runtime_error::runtime_error;};
// IDs are nonzero, unique for an entity lifetime within one backend session,
// and never reused within that session. They are not native unit pointers.
using EntityId = uint64_t;
enum class EntityKind:uint8_t { Player, TownPerson, Enemy, Corpse, Missile };
struct EntitySnapshot {
    EntityId id{};
    FixedPoint position{}, previous{};
    uint8_t facing{};
    bool moving{};
    uint32_t stateTicks{};
    // UINT32_MAX: use prototype stateTicks. Otherwise an authoritative sprite frame.
    uint32_t animationFrame=UINT32_MAX;
    // Filled by native backends for units other than the local player.
    EntityKind kind=EntityKind::Player;
    uint32_t classId=0;
    int32_t life=-1,maxLife=-1;
    uint8_t mode=0,direction=0; // native animation mode and path direction (0..63)
    uint16_t animSpeed=256;
    uint8_t services=0; // town people: bit 0 sells items, bit 1 gambles
    std::array<uint32_t,6> states{}; // native backends: the unit's states (states.txt rows) as bits
    // Native monsters: MONTYPEFLAG_* (2 superunique, 4 champion, 8 unique, 16 minion, 64 ghostly), the colour transform
    // (RandTransforms.dat map + 1, 0 none), special abilities (monumod.txt ids), the name's string ids (see native::Unit) and
    // the COF components.
    uint8_t monsterType=0,colorTransform=0;
    uint8_t hitClass=0; // native monsters: the last hit's class (see native::Unit)
    std::array<uint8_t,10> umods{};
    std::array<uint16_t,3> name{};
    std::array<uint8_t,16> components{};
    uint8_t lookClass=255,lookMode=0;std::array<uint32_t,17> look{}; // native monster drawn as a player (Valkyrie, Decoy): class, mode, look codes
    // Native player skill movement: the exact straight path used by Leap/Leap Attack, retained through landing.
    bool skillMovePath=false;
    FixedPoint skillMoveOrigin{},skillMoveTarget{};
    uint8_t skillMoveFrame=255; // Leap S1 frame; retained briefly for the three-frame landing
    uint8_t sequenceMode=255,sequenceFrame=0; // native player in the sequence mode: the mode and frame its table draws (255 none)
    bool frameEvent=false; // native player: an AnimData event frame passed this tick (a skill's client do-function runs)
    uint16_t usedSkill=0; // another native player (LAN): the skill it is using (this player's own is the client's castSkill)
    uint16_t shapeClass=0xFFFF;uint8_t shapeMode=0; // native player shapeshifted: the monster class and mode it is drawn as (0xFFFF none)
    bool operator==(const EntitySnapshot&) const = default;
};
// An item as the game reports it. Ground items (mode 3) have a scene subtile position; carried ones a grid cell or body location.
struct ItemSnapshot {
    uint32_t id{},classId{},code{},mode{};
    int32_t x{},y{};
    uint32_t quality{},flags{},invGraphic{},prefix{},suffix{},rarePrefix{},rareSuffix{},fileIndex{},page{},bodyLoc{},vendor{},price{}; // vendor: native unit number of the NPC whose store holds the item (0 = not for sale)
    int32_t minDamage{},maxDamage{},twoHandMinDamage{},twoHandMaxDamage{},throwMinDamage{},throwMaxDamage{};
    int32_t armorClass{},durability{},maxDurability{},quantity{};
    uint32_t traits{}; // native::itemTrait* bits
    uint32_t sockets{},parent{}; // the item's sockets; for an item in a socket (mode 6) the socketed item, x its place
    struct Stat {uint16_t id{},param{};int32_t value{};bool operator==(const Stat&) const=default;};
    std::vector<Stat> stats; // native: its magic properties (itemstatcost.txt row, parameter, value)
    int32_t armorTotal{},reqStr{},reqDex{},reqLevel{};uint32_t reqMet{}; // native: see native::Item
    std::vector<Stat> setStats; // native: a set item's bonuses in force (green in the tooltip)
    uint8_t invTrans{},invColor{},chrTrans{},chrColor{}; // native: its colour (see native::Item)
    bool operator==(const ItemSnapshot&) const=default;
};
struct SkillSnapshot {uint32_t id{};int32_t level{},baseLevel{},quantity{};bool operator==(const SkillSnapshot&) const=default;};
inline constexpr uint32_t tileIdBit=0x40000000u;
// Missiles number from 1 like monsters do (separate D2Game unit lists), so their entity ids carry this bit; without it a
// missile and a monster with the same number shared one id and swapped previous positions, facings and animation clocks.
inline constexpr uint32_t missileIdBit=0x20000000u;
inline constexpr uint32_t playerIdBit=0x10000000u; // another player in the game (LAN): its own number space
struct ObjectSnapshot {
    uint32_t id{},classId{},mode{}; // native object number, class (objects.txt row) and animation mode
    int32_t x{},y{}; // scene subtile
    uint32_t flags{};
    bool operator==(const ObjectSnapshot&) const=default;
};
struct Vitals {
    int32_t life=0,maxLife=0,mana=0,maxMana=0,experience=0,level=0,gold=0,stashGold=0;
    // Native backends: predicted potion recovery in 1/256 points, or 0 when no potion effect is active.
    int32_t potionLife=0,potionMana=0;
    bool operator==(const Vitals&) const=default;
};
// Native backends only: what the character sheet shows beyond the plain stats (see native::Sheet).
struct CharacterSheet {
    int32_t damageMin=0,damageMax=0,attackRating=0,defense=0;
    std::array<int32_t,4> resist{},resistMax{}; // fire, cold, lightning, poison
    std::array<int32_t,4> baseAttribute{};      // strength, energy, dexterity, vitality without items
    std::array<int32_t,3> baseMax{};            // max life, mana, stamina without items (1/256 points)
    std::array<int32_t,2> skillDamageMin{},skillDamageMax{},skillAttack{}; // with the left and right skill (0: not a weapon skill)
    bool operator==(const CharacterSheet&) const=default;
};
struct WorldSnapshot {
    uint64_t tick{};
    EntityId localPlayer{};
    uint32_t level{}; // native level id of the player (0 for the prototype backend)
    Vitals vitals; // native backends only
    std::vector<EntitySnapshot> entities;
    std::vector<ItemSnapshot> items; // native backends only: ground items and the player's belongings
    std::vector<uint8_t> server; // native backends only: the client packets the game sent during the last tick (16-bit length, bytes, repeated)
    // Native backends only: the player's stats by id (0 strength, 1 energy, 2 dexterity, 3 vitality, 4 unspent stat points, 5 unspent skill points, 6-11 life, mana,
    // stamina and their maxima in 1/256 points, 12 level, 13 experience, 30 experience of the next level, 31 defense), the skills with a level, and the selected skills.
    std::vector<ObjectSnapshot> objects; // native backends only: game objects near the player, nearest first
    // Native backends only: level entrances/exits (caves, stairs, crypt doors: UNIT_TILE) near the player. classId is the lvlwarp.txt row, id has bit 30 set
    // (tileIdBit) so it never collides with an object number; interact with them like objects.
    std::vector<ObjectSnapshot> tiles;
    std::array<int32_t,32> stats{};
    CharacterSheet sheet;
    // Native backends only: how the player looks now, as the game's three letter codes (packed, 0 none): the weapon class, then the
    // 16 animation components (head, torso, legs, arms, hands, shield, specials). Changes when gear is put on or taken off.
    std::array<uint32_t,17> appearance{};
    std::vector<SkillSnapshot> skills;
    int32_t leftSkill=0,rightSkill=0;
    std::array<uint8_t,13> bodyPlacement{}; // native backends only: native::Reply::bodyPlacement for the item on the cursor
    int8_t wieldType=-1; // native backends only: native::Reply::wieldType (2: a two-handed weapon in one hand, the other empty)
    // Native: the sets' bonuses on the player (sets.txt row and its stats, see native::SetBonus) and the setitems.txt rows of
    // the set items the player has (native::Reply::ownedSetItems).
    struct SetBonus {int32_t set=-1;std::vector<ItemSnapshot::Stat> stats;bool operator==(const SetBonus&) const=default;};
    std::vector<SetBonus> setBonuses;std::vector<uint16_t> ownedSetItems;
    int32_t goldLost=0; // native: stat 175, the gold the last death cost
    uint16_t bodyFits=0; // native backends only: native::Reply::bodyFits
    uint32_t nativeGuid=0; // the player's native unit id, so a missile's owner can be told from the player
    // native backends only: the act's time of day (native::Reply::envCycle...): the cycle 0-5 and the outdoor light (RGB, intensity 0-255)
    // native backends only: the mercenaries the NPC talked to offers (native::Hireling): name and HireDesc string ids, level,
    // life, defense, cost, damage, resistances.
    struct Hireling {uint16_t name=0;int16_t description=0;int32_t level=0,life=0,defense=0,cost=0,minDamage=0,maxDamage=0,resist=0;bool operator==(const Hireling&) const=default;};
    std::vector<Hireling> hirelings;
    // native backends only: the player's own hireling as its panel shows it (native::HirelingSheet; guid 0 none).
    struct HirelingSheet {uint32_t guid=0;int32_t level=0,experience=0,nextExperience=0,strength=0,dexterity=0,life=0,maxLife=0,defense=0,minDamage=0,maxDamage=0;std::array<int32_t,4> resist{};int32_t baseStrength=0,baseDexterity=0;bool operator==(const HirelingSheet&) const=default;};
    HirelingSheet hireling;
    uint8_t envCycle=2;std::array<uint8_t,3> envColor{255,255,255};int32_t envIntensity=255;
    const EntitySnapshot& player() const;
    bool operator==(const WorldSnapshot&) const = default;
};
enum class CommandResult { Accepted, Rejected, UnknownEntity };

// In-process C++ interface, NOT a DLL ABI or serialized network protocol.
// Calls run on the owning simulation thread. apply() precedes tick(); tick()
// advances exactly one 25 Hz step. snapshot() returns independently owned data.
// DLL handles, native structs and allocations must stay inside a future adapter.
class GameBackend {
public:
    virtual ~GameBackend() = default;
    virtual CommandResult apply(EntityId actor, const Command& command) = 0;
    virtual void tick() = 0;
    virtual WorldSnapshot snapshot() const = 0;
    // Native backends: the player walked into a level outside the scene the backend was given. Hands over the scene of the new level(s); false when unsupported.
    virtual bool setScene(const CollisionGrid&, GridPoint, std::vector<int>) { return false; }
};
// Owns a copy of collision data; callers may destroy their source grid.
std::unique_ptr<GameBackend> makePrototypeBackend(const CollisionGrid& grid, GridPoint spawn);
std::array<float,2> renderPosition(const EntitySnapshot& entity, float alpha);
}
