#pragma once
#include <cstdint>
namespace d2::native {
// Local Windows x64 helper protocol v20, fixed-width little-endian words.
inline constexpr uint32_t magic=0x44474d4B;
enum class Operation:uint32_t { Tick, Walk, Quit, Attack, Respawn, Pickup, Packet, Cheat };
// Packet: x = client packet header | size << 8, y..v = the packet body as little-endian words (inventory operations).
// Walk: x/y = world destination; z = 0 walk (0x01), 1 run (0x03).
struct Request {uint32_t signature=magic;Operation operation{};int32_t x{},y{},z{},w{},v{};};
// Another unit the native game knows about (monsters and town people), nearest first.
struct Unit {
    uint32_t guid{},classId{},mode{};
    uint32_t direction{},animSpeed{}; // path direction 0..63, animation speed (256 = one frame per tick)
    int32_t life{},maxLife{}; // whole life points
    int32_t x{},y{}; // absolute 16.16 subtile coordinates, copied from native path
    uint32_t flags{}; // bit 0: town person / interactive, bit 1: dead, bit 2: sells items, bit 3: gambles, bit 4: a missile (classId indexes missiles.txt). A missile's life is its remaining frame (counts down; 0 or 1 is the end of its range), maxLife its skill id, and name the owner's unit id.
    uint32_t states[6]{}; // the unit's states (states.txt rows 0..191) as bits: auras, curses, armours, chill...
    // Monsters: MONTYPEFLAG_* (2 superunique, 4 champion, 8 unique, 16 minion, 64 ghostly...), the colour transform (the
    // monster's palshift.dat map, 0 none, else index + 1), the special abilities (monumod.txt ids), the name (a random
    // unique's prefix, suffix and appellation string ids, a superunique's name string id first) and the COF components.
    uint8_t monsterType{},colorTransform{};
    uint8_t umods[10]{};
    uint16_t name[3]{};
    uint8_t components[16]{};
    // The last hit's class (D2UnitStrc dwLastHitClass): the high four bits the element (0x20 fire, 0x30 cold, 0x40
    // lightning...), the low four the weapon kind.
    uint8_t hitClass{};
    uint8_t reserved{};
    // A monster drawn as a player (states.txt gfxtype 2: Valkyrie, Decoy, Shadow Warrior and Master; D2Common #11013):
    // the player class (0xFFFFFFFF none), the player mode it shows, and its look as the player's (weapon class, then the
    // 16 component tokens), from its own gear.
    uint32_t lookClass{0xFFFFFFFF},lookMode{},look[17]{};
    // Another player (unitPlayer) in the sequence mode (18: Charge, Whirlwind, Leap...): the mode and frame D2Common's
    // sequence table draws now (255 none), the skill it is using, and its path type (9: Leap's flight), so it is drawn
    // the way this player's own character is.
    uint8_t sequenceMode{255},sequenceFrame{};uint16_t usedSkill{};
    uint32_t pathType{};
    // Where its path is going (whole absolute subtiles, the path's final target; 0 none): what the original client turns a
    // walking monster toward (the target of the move packet), not each step.
    uint16_t targetX{},targetY{};
};
// An item the native game knows: on the ground, in the inventory, equipped or in the belt (mode = native item mode).
// One stat of an item (itemstatcost.txt row, its parameter: a skill, a class..., and its value as the game holds it).
struct ItemStat {uint16_t id{},param{};int32_t value{};};
inline constexpr uint32_t maxItemStats=32;
struct Item {
    uint32_t guid{},classId{},code{},mode{};
    int32_t x{},y{}; // ground: absolute subtile; stored: grid cell; equipped: body location
    uint32_t owner{},quality{},flags{},invGraphic{};
    uint32_t prefix{},suffix{},rarePrefix{},rareSuffix{},fileIndex{};
    uint32_t price{}; // store items: what the game charges
    uint32_t page{},bodyLoc{}; // inventory page (0 inventory, 3 cube, 4 stash) and body location (1 head .. 10 gloves)
    int32_t minDamage{},maxDamage{},twoHandMinDamage{},twoHandMaxDamage{},throwMinDamage{},throwMaxDamage{};
    int32_t armorClass{},durability{},maxDurability{},quantity{};
    uint32_t traits{}; // itemTrait* bits, from D2Common's own tests (the checks D2Client 1.10f makes before its item packets)
    uint32_t sockets{}; // ITEMS_GetSockets: the item's sockets
    uint32_t parent{};  // an item in a socket (mode 6): the socketed item's guid, in the order the sockets were filled
    // Its magic properties: the stat list of state 0 and flag 0x40 (STATLIST_MAGIC), which D2Client 1.10f's tooltip prints
    // (0x6FAF31A9 → 0x6FAF19C0): affixes, a unique's or set item's own properties, runes and gems in it.
    uint32_t statCount{};
    ItemStat stats[maxItemStats]{};
    // What the tooltip's base lines show, by D2Common's rules: the defense with its magic (the full armorclass stat), the
    // requirements (ITEMS_CheckRequirements: items.txt reqstr/reqdex plus item_req_percent, -10 when ethereal;
    // ITEMS_GetLevelRequirement), and which of them the player meets (bit 0 strength, 1 dexterity, 2 level).
    int32_t armorTotal{},reqStr{},reqDex{},reqLevel{};
    uint32_t reqMet{};
    // A set item's bonuses in force: the lists of the item-set states (165-169, flags MAGIC | SET) the game has switched on
    // for the pieces worn (D2Common sub_6FDA4380), after the magic stats: stats[statCount .. statCount + setStatCount).
    uint32_t setStatCount{};
    // Its colour (D2Common ITEMS_GetColor): the palette shift file (items.txt InvTrans for the inventory picture, Transform
    // for the character's gear: 1 grey, 2 grey2, 5 greybrown, 6 invgrey, 7 invgrey2, 8 invgreybrown) and the colour in it
    // (colors.txt, 0..20); trans 0 none.
    uint8_t invTrans{},invColor{},chrTrans{},chrColor{};
};
// Item traits. Beltable: ITEMS_CheckIfBeltable. Socket filler: ITEMS_IsSocketFiller (gems, runes, jewels). Takes filler:
// socketed (IFLAG 0x800), ITEMS_CheckIfSocketable, max sockets > 0, identified, not broken, and fewer items inside than
// ITEMS_GetSockets (D2Client 6FAE17D0; an item the server has not yet given an inventory has none inside). Stacks with cursor: ITEMS_AreStackablesEqual(cursor item, this item). Usable:
// ITEMS_CheckRequirements(item, player, 0, ...) as D2Client asks before colouring the box behind an item (6FAE118A).
// Uses ammo: ITEMS_GetAmmoType (bows, crossbows). Quiver: ITEMS_GetQuiverType (arrows, bolts).
inline constexpr uint32_t itemTraitBeltable=1,itemTraitSocketFiller=2,itemTraitTakesFiller=4,itemTraitStacksWithCursor=8,itemTraitUsable=16,
    itemTraitUsesAmmo=32,itemTraitQuiver=64;
// A game object (chest, waypoint, portal, door, shrine...): class, animation mode and absolute subtile position.
struct Object {uint32_t guid{},classId{},mode{};int32_t x{},y{};uint32_t flags{};}; // flags bit 0: a level entrance/exit (UNIT_TILE), classId is its lvlwarp.txt row
// A skill of the player (only skills the player has a level or a quantity in).
struct Skill {uint32_t id{};int32_t level{},baseLevel{},quantity{};};
// A mercenary the NPC talked to offers (packet 0x4E: its name, a string id, and seed), as D2Common MONSTERS_HirelingInit
// (#11082) makes it for this player: level, life, defense, gold to hire, damage, resistances, and hireling.txt HireDesc
// (a string id: the special attack, "Fire Arrow").
struct Hireling {uint16_t name{};int16_t description{};int32_t level{},life{},defense{},cost{},minDamage{},maxDamage{},resist{};};
inline constexpr uint32_t maxHirelings=10;
// The player's own hireling as its panel shows it (D2Client 0x6FAEDA20 reads these stats of the unit): guid 0 when there is
// none. Life in whole points; resist: fire, cold, lightning, poison; the base strength and dexterity (blue when items raise them).
struct HirelingSheet {uint32_t guid{},name{};int32_t level{},experience{},nextExperience{},strength{},dexterity{},life{},maxLife{},defense{},minDamage{},maxDamage{},resist[4]{},baseStrength{},baseDexterity{};};
inline constexpr uint32_t unitTownPerson=1,unitDead=2,unitVendor=4,unitGambler=8,unitMissile=16,unitPlayer=32,maxUnits=192,maxItems=128,maxServerBytes=16384,maxSkills=64,statCount=32,maxObjects=192;
// What the character sheet shows beyond the plain stats, worked out by the game's rules (D2Common and D2Game's damage code).
struct Sheet {
    int32_t damageMin{},damageMax{}; // the weapon's (or bare hands') damage with strength/dexterity, mastery and item bonuses
    int32_t attackRating{},defense{};
    int32_t resist[4]{},resistMax[4]{}; // fire, cold, lightning, poison: the resistance and its cap (75 + max resist)
    int32_t baseAttribute[4]{};         // strength, energy, dexterity, vitality without items (a higher value is shown blue)
    int32_t skillDamageMin[2]{},skillDamageMax[2]{},skillAttack[2]{}; // with the left and right skill (0: not a weapon skill)
    int32_t baseMax[3]{};               // max life, mana, stamina without items (1/256 points; D2Client colours them against it)
};
// A set's bonuses on the player (D2Common gives them under an item-set state, stat 71 naming the set): the list D2Client
// 0x6FAF33C0 prints for that set's items.
struct SetBonus {int32_t set{-1};uint32_t statCount{};ItemStat stats[16]{};};
inline constexpr uint32_t maxOwnedSetItems=32;
struct Reply {
    uint32_t signature=magic,tick{},seed{},guid{},characterClass{},mode{};
    int32_t x{},y{}; // absolute 16.16 subtile coordinates, copied from native path
    uint32_t level{};
    uint32_t weaponClass{},components[16]{},coloredComponents{};
    uint32_t animationFrame{};
    uint32_t pathType{};
    int32_t pathTargetX{},pathTargetY{}; // whole absolute subtiles from the native dynamic path
    uint32_t sequenceMode{255},sequenceFrame{}; // in the sequence mode (18): the mode and frame D2Common's table draws now, 255 none
    uint32_t frameEvent{}; // the player's animation passed a frame with an AnimData event since the last reply (the client's skill do-functions run then)
    uint32_t shapeClass{0xFFFFFFFF},shapeMode{}; // a shapeshifted player (Werewolf...): the monster class and mode it is drawn as
    int32_t life{},maxLife{},mana{},maxMana{},experience{},characterLevel{},gold{},stashGold{}; // player vitals, whole points
    int32_t potionLife{},potionMana{}; // predicted potion recovery in 1/256 points, 0 when no potion effect is active
    uint32_t unitCount{};
    Unit units[maxUnits]{};
    uint32_t itemCount{};
    Item items[maxItems]{};
    uint32_t serverBytes{}; // what the game sent its client since the last reply: 16-bit length, then the packet bytes, repeated
    uint8_t server[maxServerBytes]{};
    int32_t stats[statCount]{}; // the player's stats 0..31 as the game holds them (life, mana and stamina in 1/256 points)
    int32_t leftSkill{},rightSkill{};
    uint32_t skillCount{};
    Skill skills[maxSkills]{};
    uint32_t objectCount{};
    Object objects[maxObjects]{};
    uint32_t playerStates[6]{}; // the player's states (as Unit::states)
    Sheet sheet;
    // For the item on the cursor, D2Common #10299 at each body location (index 1 head .. 10 gloves, 11/12 the swap hands),
    // which D2Client 1.10f switches on when the item is put there: 0 not allowed, 1 equip (0x1A), 2 the other hand's item
    // comes off (0x1B), 5 swap (0x1D), 6 stack (0x21), 7 two one-handers come off for a two-hander (0x1E).
    uint8_t bodyPlacement[13]{};
    int8_t wieldType{}; // D2Common INVENTORY_GetWieldType: 2 when one hand is empty and the other holds a two-handed weapon
    uint16_t bodyFits{}; // bit n: ITEMS_CheckBodyLocation(cursor's item, n), for D2Client's green/red box over a body slot
    // The colour of each COF component of the player's look (the worn item's Transform file and colour, as Item::chrTrans).
    uint8_t componentTrans[16]{},componentColor[16]{};
    // The time of day of the player's act (D2Common ENVIRONMENT_*FromAct): the cycle (0 sunrise, 1 morning, 2 noon,
    // 3 afternoon, 4 sunset, 5 night), the outdoor light's colour and its intensity (0-255).
    uint8_t envCycle{},envRed{255},envGreen{255},envBlue{255};
    int32_t envIntensity{255};
    uint32_t hirelingCount{};
    Hireling hirelings[maxHirelings]{};
    HirelingSheet hireling;
    SetBonus setBonuses[6]{}; // the player's lists for STATE_ITEMSET1..6 (set -1: none)
    // setitems.txt rows of the set items the player has, where D2Client 0x6FAE5990 counts them (green in a set's list): on
    // the body, in the backpack, the cube or the stash.
    uint32_t ownedSetItemCount{};uint16_t ownedSetItems[maxOwnedSetItems]{};
    int32_t goldLost{}; // stat 175 (goldlost): what the last death cost, shown under "You have died" (D2Client 0x6FB22461)
};
static_assert(sizeof(HirelingSheet)==72&&sizeof(Hireling)==32&&sizeof(Request)==28&&sizeof(Unit)==188&&sizeof(Item)==412&&sizeof(Sheet)==100&&sizeof(Reply)==196+maxUnits*188+maxItems*412+maxServerBytes+statCount*4+12+maxSkills*16+4+maxObjects*24+24+100+16+32+8+4+maxHirelings*32+72+6*136+4+maxOwnedSetItems*2+4);
}
