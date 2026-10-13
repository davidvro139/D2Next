#pragma once
#include "Assets.h"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
namespace d2 {
// Tab separated game table (`data\global\excel\*.txt` inside the MPQs), addressed by column name.
class TextTable {
public:
    TextTable()=default;
    explicit TextTable(std::string_view text);
    size_t rows() const {return rows_.size();}
    std::string cell(size_t row,std::string_view column) const;
    bool hasColumn(std::string_view column) const {return columns_.count(std::string(column))!=0;}
private:
    std::map<std::string,size_t> columns_;
    std::vector<std::vector<std::string>> rows_;
};
// Monster modes in the order the game numbers them, with their file tokens.
inline constexpr const char* monsterModeTokens[16]={"dt","nu","wl","gh","a1","a2","bl","sc","s1","s2","s3","s4","dd","kb","sq","rn"};
// Player modes in PlrMode order, with their COF tokens. Death (0) and dead (17) are loaded unarmed (hth).
inline constexpr const char* playerModeTokens[20]={"dt","nu","wl","rn","gh","tn","tw","a1","a2","bl","sc","th","kk","s1","s2","s3","s4","dd","sq","kb"};
struct MonsterSprite {
    std::string id,code,baseWeapon,nameKey;                 // monstats id, two letter folder code, weapon class of the COF
    std::array<std::vector<std::string>,16> variants; // per component (hd tr lg ra la rh lh sh s1..s8): allowed variant codes
    std::array<bool,16> hasMode{};                  // per monster mode: has an animation
    std::array<uint8_t,16> directions{};            // per monster mode: directions in the animation
    bool npc=false;
    int transLvl=0; // monstats.txt TransLvl: the map of the monster's COF\palshift.dat it is drawn with (Carvers, Devilkin... are recoloured Fallen)
    int overlayHeight=2; // monstats2.txt OverlayHeight: which overlay.txt Height1-4 offset to use
    std::string monSound; // row of monsounds.txt
    int light=0;std::array<uint8_t,3> lightColor{255,255,255}; // monstats2.txt Light (radius in subtiles) and light-r/g/b
    bool small=false,large=false; // monstats2.txt small/large (a corpse explosion's size)
    bool selectable=true; // monstats2.txt isSel and not noSel: the mouse can point at it (named, lit up, clicked); camp Rogues, townsfolk and critters cannot
};
// The monster class number is the game's own (monstats.txt hcIdx), the same one D2Game reports.
class MonsterTable {
public:
    explicit MonsterTable(const Archives& archives);
    const MonsterSprite* find(uint32_t classId) const;
    size_t size() const {return sprites_.size();}
private:
    std::map<uint32_t,MonsterSprite> sprites_;
};
// Variant code of each component for one monster (chosen from its allowed list by `seed`), lower case, "" for none.
CharacterAppearance monsterAppearance(const MonsterSprite& sprite,uint32_t seed);
}
namespace d2 {
// The 128 palette remaps in an act's Pal.PL2 that states.txt colorshift indexes (0 unused, 73 revived grey,
// 104 poisoned green, 108 frozen blue). Loaded from data\global\palette\actN\pal.pl2.
class StateColors {
public:
    StateColors(const Archives& archives,int act);
    const std::array<uint8_t,256>* shift(int index) const {return loaded_&&index>0&&index<128?&shifts_[size_t(index)]:nullptr;}
private:
    bool loaded_=false;
    std::array<std::array<uint8_t,256>,128> shifts_{};
};
// Lazily loaded monster/NPC animations, one per (class, mode); a class or mode that cannot be loaded is remembered as missing.
class UnitArt {
public:
    UnitArt(const Archives& archives,const Palette& palette):archives_(archives),palette_(palette),table_(archives) {}
    void setStateColors(const StateColors* colors){stateColors_=colors;}
    // Nullptr when the class has no such animation. The pointer stays valid for the life of this object.
    const Animation* get(uint32_t classId,uint32_t mode,uint32_t variantSeed);
    // The same with the pieces the game chose for this monster, its colour transform (RandTransforms.dat map + 1, 0 none)
    // and the states.txt colorshift in force (0 none; it replaces the unique colour, the higher colorpri wins).
    const Animation* get(uint32_t classId,uint32_t mode,const std::array<uint8_t,16>& components,uint8_t colorTransform,uint8_t stateShift=0);
    const MonsterSprite* sprite(uint32_t classId) const {return table_.find(classId);}
    // Player art in a player mode for a look (a monster drawn as a player: Valkyrie, Decoy, Shadow Warrior); nullptr when
    // there is none. The pointer stays valid for the life of this object.
    const Animation* player(const CharacterAppearance& look,uint32_t mode);
private:
    std::map<std::string,std::unique_ptr<Animation>> players_;
    const Archives& archives_;
    Palette palette_;
    MonsterTable table_;
    const StateColors* stateColors_=nullptr;
    std::map<uint64_t,std::unique_ptr<Animation>> cache_;
    std::map<std::tuple<uint32_t,uint32_t,std::array<uint8_t,16>,uint8_t,uint8_t>,std::unique_ptr<Animation>> exact_;
    Bytes transforms_; // RandTransforms.dat: 30 maps of 256 palette indices
    std::map<std::string,Bytes> palshifts_; // per monster code: its palshift.dat (maps of 256 palette indices)
};
}
namespace d2 {
// `data\local\lng\eng\*.tbl`: string key to text (string.tbl, then expansionstring.tbl and patchstring.tbl override).
class StringTable {
public:
    explicit StringTable(const Archives& archives);
    // The text for a key; the key itself when it is not in the tables.
    std::string get(const std::string& key) const;
    bool has(const std::string& key) const {return strings_.count(key)!=0;}
    size_t size() const {return strings_.size();}
    // The text for a numeric string id as the game sends it (ids 10000 and up come from expansionstring.tbl, 20000 and up from patchstring.tbl).
    std::string byId(uint32_t id) const {auto it=byId_.find(id);return it==byId_.end()?std::string():it->second;}
    // The key of a numeric string id (e.g. the key of an NPC's spoken line, which names its sound).
    std::string keyById(uint32_t id) const {auto it=keyById_.find(id);return it==keyById_.end()?std::string():it->second;}
    const std::map<uint32_t,std::string>& keysById() const {return keyById_;}
private:
    std::map<std::string,std::string> strings_;
    std::map<uint32_t,std::string> byId_,keyById_;
};
struct ItemInfo {
    std::string code,name,nameKey,invFile,uniqueInvFile,setInvFile,type;
    uint8_t invWidth=1,invHeight=1;
    uint8_t beltRows=0; // belts: rows of four potion slots (armor.txt belt -> belts.txt numboxes / 4)
    bool equipment=false; // listed in weapons.txt or armor.txt rather than misc.txt
    bool useable=false; // items.txt useable: right-click may send the game's use-item packet
    int minDamage=0,maxDamage=0,twoHandMinDamage=0,twoHandMaxDamage=0,throwMinDamage=0,throwMaxDamage=0;
    int minArmor=0,maxArmor=0,durability=0,reqStr=0,reqDex=0,reqLevel=0,maxStack=0;
    std::string useSound,dropSound; // sounds.txt names: put in the inventory or worn (usesound), dropped on the ground (dropsound)
};
// Items by their four letter code (ItemsTxt dwCode) from weapons.txt, armor.txt and misc.txt, plus the affix and unique tables.
class ItemTable {
public:
    ItemTable(const Archives& archives,const StringTable& strings);
    const ItemInfo* find(uint32_t code) const;
    // Display name of an item as the game shows it (no stat text): quality words, affixes and unique/set names.
    std::string displayName(uint32_t code,uint32_t quality,uint32_t prefix,uint32_t suffix,uint32_t rarePrefix,uint32_t rareSuffix,uint32_t fileIndex) const;
    // The set a set item (setitems.txt row) belongs to: its sets.txt row, its name and its items (setitems.txt rows, in file
    // order). set -1 when the row names no set.
    struct SetInfo {int set=-1;std::string name;std::vector<uint32_t> items;};
    SetInfo setOf(uint32_t fileIndex) const;
    static std::string codeString(uint32_t code);
private:
    const StringTable& strings_;
    std::map<uint32_t,ItemInfo> items_;
    // magicAffix_: the magic suffixes, prefixes and automagic affixes in one list, rareAffix_: the rare suffixes and
    // prefixes, numbered as D2Common numbers them (its combined tables, #10604 and #10606; an item's affix number - 1).
    std::vector<std::string> magicAffix_,rareAffix_,unique_,set_,setOfItem_,setKeys_,setNames_; // setOfItem_: setitems.txt "set" per row; setKeys_/setNames_: sets.txt "index"/"name"
};
// Game objects as the client draws them: one row of objects.txt.
struct ObjectInfo {
    std::string name,token; // token: the two letters of the object's art folder
    bool selectable[8]{};   // per animation mode (neutral, operate, opened, special 1-5)
    unsigned frames[8]{};   // frames of the art the mode uses (FrameCnt)
    unsigned start[8]{};    // frame the mode starts on (Start)
    unsigned delta[8]{};    // 256 = one frame per game tick
    bool cycle[8]{};
    int sizeX=1,sizeY=1;
    int xOffset=0,yOffset=0; // screen pixels added to the object's position (objects.txt Xoffset/Yoffset)
    bool draw=true;
    bool door=false; // IsDoor: its opened mode has no art of its own, the neutral art's last frame is the open door
    uint8_t lit[8]{};                                // light radius in subtiles per animation mode (Lit0-7), 0 none
    std::array<uint8_t,3> lightColor{255,255,255};  // colour of that light (Red, Green, Blue)
    int automap=0; // objects.txt AutoMap: its icon on the automap (a frame of ui\automap\maximap), 0 none
};
// AutoMap.txt: the automap picture of a map tile. A level type (its automap name, "1 Wilderness"), the tile's kind code
// (from its orientation: fl floor, wl/wr walls, ...), its main index (Style) and a range of sub indices lead to up to four
// frames of ui\automap\maximap.dc6, one picked per tile.
class AutomapTable {
public:
    explicit AutomapTable(const Archives& archives);
    // The maximap frame of a tile, or -1 when the automap doesn't show it. pick chooses among the row's frames.
    int frame(const std::string& levelType,uint32_t orientation,uint32_t mainIndex,uint32_t subIndex,uint32_t pick) const;
private:
    struct Row {int first=0,last=0;std::vector<int> frames;};
    std::map<std::tuple<std::string,std::string,int>,std::vector<Row>> rows_;
};
class ObjectTable {
public:
    explicit ObjectTable(const Archives& archives);
    const ObjectInfo* find(uint32_t classId) const;
private:
    std::vector<ObjectInfo> objects_;
};
// Object animations by class and mode, decoded once (null when the object has no art).
class ObjectArt {
public:
    ObjectArt(const Archives& archives,const Palette& palette,const ObjectTable& table):archives_(archives),palette_(palette),table_(table) {}
    const Animation* get(uint32_t classId,uint32_t mode);
private:
    const Archives& archives_;
    Palette palette_;
    const ObjectTable& table_;
    std::map<uint64_t,std::unique_ptr<Animation>> cache_;
};
// Sound files of monsters: monstats MonSound -> monsounds.txt -> sounds.txt (paths relative to data\global\sfx, empty when none).
class SoundBank {
public:
    SoundBank(const Archives& archives,const MonsterTable& monsters);
    std::string monsterHit(uint32_t classId) const;
    std::string monsterDeath(uint32_t classId) const;
    std::string monsterAttack(uint32_t classId,int mode) const;
    std::string monsterWeapon(uint32_t classId,int mode) const;
    // The file (relative to data\global\sfx) of a sounds.txt sound, "" when there is none.
    std::string file(const std::string& soundName) const;
    // The same by sounds.txt Index (row number), as the client's tables name sounds (the NPCs' speech).
    std::string fileByIndex(int index) const {auto f=byIndex_.find(index);return f==byIndex_.end()||f->second=="none.wav"?std::string():f->second;}
    // sounds.txt Volume (0-255) and Group Size (the rows from this one that the game picks among at random; 0 just this one).
    int volumeByIndex(int index) const {auto f=volumes_.find(index);return f==volumes_.end()?255:f->second.first;}
    int groupByIndex(int index) const {auto f=volumes_.find(index);return f==volumes_.end()?0:f->second.second;}
private:
    const MonsterTable& monsters_;
    std::map<std::string,std::string> sounds_;
    std::map<int,std::string> byIndex_;
    std::map<int,std::pair<int,int>> volumes_;
    struct MonsterSounds {std::array<std::string,2> attack,weapon;std::string hit,death;};
    std::map<std::string,MonsterSounds> monsterSounds_;
};
// A missile (spell/arrow projectile or its impact effect) as the client draws it: one row of missiles.txt.
// light: radius in subtiles; trans (Trans): 0 drawn solid, 1 added onto the scene as a glow, 2 half see-through.
// travelSound / hitSound: sounds.txt rows played when the missile sets off and when it hits (TravelSound, HitSound).
// Also: its lifetime in ticks (Range), speed (Vel), frames per tick (animrate, 1024 = one), the frames a looping animation
// repeats (SubLoop: SubStart..SubStop, after playing the ones before once), and the effects the client itself adds where it
// hits (CltHitSubMissile1-4: explosions the server never sends) and the ones the server makes there (HitSubMissile1-4,
// sent like any missile: Fist of the Heavens' bolts, aimed at the undead near it).
struct MissileInfo {std::string celFile;bool loop=true;int light=0;std::array<uint8_t,3> lightColor{255,255,255};int trans=0;std::string travelSound,hitSound,progressSound,progressOverlay;
    int range=0,velocity=0,animRate=1024,animLen=0,explosion=-1,xOffset=0,yOffset=0,zOffset=0;bool randStart=false,subLoop=false,alwaysExplode=false;int subStart=0,subStop=0;std::array<int,3> clientSub{-1,-1,-1};std::array<int,4> clientHit{-1,-1,-1,-1},serverHit{-1,-1,-1,-1};
    // pCltDoFunc and CltParam1-3. 9 (meteorcenter) is a fall: frames, pixels of height per frame, pixels of sideways offset per frame.
    int clientDo=0;std::array<int,3> clientParam{};
    // pCltHitFunc and cHitPar1-3. The impact picture when this missile is removed (a ring count, a disc radius, and so on).
    int clientHitFunc=0;std::array<int,3> clientHitParam{};
    // pSrvDoFunc, and SubMissile1. Poison Javelin's server function 2 creates that sub on each step the missile changes tile.
    int serverDo=0,serverSub=-1,serverSub2=-1; // serverSub2: SubMissile2 (a corpse explosion's small blood)
    int initSteps=0; // missiles.txt InitSteps. Lightning and a bone spear wait this many frames before the client sub.
    // pCltDoFunc 1 with CltParam1 and CltParam2 set and no CltParam3 (blizzard shards, and the same columns on falling debris):
    // the client draws it falling straight down. CltParam1 is the pixel distance, CltParam2 the pixels per frame. Changing the
    // missile's Range does not change that fall.
    bool straightFall() const {return clientDo==1&&velocity==0&&clientParam[0]>0&&clientParam[1]>0&&clientParam[2]==0;}
    // How long the client shows an effect of its own: its Range, or for one that doesn't loop, until its frames (AnimLen) are shown.
    int effectTicks() const {const int frames=animLen>0?animLen*1024/std::max(animRate,1):range;return std::max(1,loop?range:std::min(range>0?range:frames,frames));}};
class MissileTable {
public:
    explicit MissileTable(const Archives& archives);
    const MissileInfo* find(uint32_t classId) const;
    int rowOf(const std::string& name) const {auto f=byName_.find(name);return f==byName_.end()?-1:f->second;} // by missiles.txt Missile (lower case)
private:
    std::vector<MissileInfo> missiles_;
    std::map<std::string,int> byName_;
};
// Missile animations by class, decoded once (null when the missile has no art of its own, e.g. purely server-side ones).
class MissileArt {
public:
    MissileArt(const Archives& archives,const Palette& palette,const MissileTable& table):archives_(archives),palette_(palette),table_(table) {}
    const Animation* get(uint32_t classId);
private:
    const Archives& archives_;
    Palette palette_;
    const MissileTable& table_;
    std::map<uint32_t,std::unique_ptr<Animation>> cache_;
};
// Overlays (overlay.txt): effects drawn on a unit, such as the glow while casting. frames advance rate/16 per game tick; trans is
// a COF draw effect (3: added onto the scene); radius and colour are the light it gives.
// overlay.txt: heights raise the overlay for units of each size (1 small .. 4 huge); preDraw puts it behind the unit.
struct OverlayInfo {std::string file;unsigned frames=1;int x=0,y=0,rate=16,trans=3,radius=0,loopWait=0,oneOfN=1;std::array<int,4> height{};bool preDraw=false;std::array<uint8_t,3> color{255,255,255};
    // Frame of a looping overlay, or -1 during overlay.txt LoopWaitTime (the pause between loops, in frames).
    int loopFrame(uint64_t ticks,unsigned count) const {if(!count)return 0;const uint64_t step=ticks*uint64_t(rate>0?rate:1)/16;const uint64_t span=uint64_t(count)+uint64_t(loopWait>0?loopWait:0);const uint64_t pos=step%span;return pos<count?int(pos):-1;}};
// states.txt: what a unit's state looks like. overlays: overlay.txt rows looped while the state lasts (overlay1-4, auras
// have a front and a back one); castOverlay / removeOverlay: played once when it starts / ends.
// colorPri / colorShift: the active state with the highest colorpri recolours the unit (Pal.PL2 index). light: that
// state's light-r/g/b replaces the colour of the light the unit already gives.
// sheetBlue/sheetRed: the states.txt columns that colour the character sheet's values (bit 0 attblue/attred, 1 dam, 2 arm, 3 rf,
// 4 rl, 5 rc, 6 rp), as D2Common's STATES_CheckStateMask*BlueOnUnit / RedOnUnit read them for D2Client.
struct StateInfo {std::string name,skill;int clientActive=0;std::array<std::string,4> overlays;std::string castOverlay,removeOverlay;int colorPri=0,colorShift=0;std::array<uint8_t,3> light{};uint8_t sheetBlue=0,sheetRed=0;};
struct StateVisual {uint8_t shift=0;bool light=false;std::array<uint8_t,3> lightColor{255,255,255};};
class StateTable {
public:
    explicit StateTable(const Archives& archives);
    const StateInfo* find(uint32_t id) const {return id<states_.size()?&states_[id]:nullptr;}
    int rowOf(const std::string& name) const {for(size_t i=0;i<states_.size();++i)if(states_[i].name==name)return int(i);return -1;}
    // The colour and light of the active state with the highest colorpri (none when nothing qualifies).
    StateVisual visual(const std::array<uint32_t,6>& states) const;
    // The sheet colour bits (StateInfo::sheetBlue / sheetRed) of all the active states together.
    std::pair<uint8_t,uint8_t> sheetColors(const std::array<uint32_t,6>& states) const {
        uint8_t blue=0,red=0;
        for(size_t s=0;s<states_.size()&&s<192;++s)if(states[s>>5]&(1u<<(s&31))){blue|=states_[s].sheetBlue;red|=states_[s].sheetRed;}
        return {blue,red};
    }
private:
    std::vector<StateInfo> states_;
};
class OverlayTable {
public:
    explicit OverlayTable(const Archives& archives);
    const OverlayInfo* find(const std::string& name) const;
    // The name of overlay.txt row `row` (the number the game sends, e.g. in packet 0x11), "" when out of range.
    std::string nameOfRow(size_t row) const {return row<rowNames_.size()?rowNames_[row]:std::string();}
private:
    std::map<std::string,OverlayInfo> overlays_; // by lower-case name
    std::vector<std::string> rowNames_;
};
// Overlay animations by name, decoded once (null when missing).
class OverlayArt {
public:
    OverlayArt(const Archives& archives,const Palette& palette,const OverlayTable& table):archives_(archives),palette_(palette),table_(table) {}
    const Animation* get(const std::string& name);
    // A whole DC6 file (archive path) as an animation: each frame with its offset (x left, y top from the anchor).
    const Animation* dc6(const std::string& path);
private:
    const Archives& archives_;
    Palette palette_;
    const OverlayTable& table_;
    std::map<std::string,std::unique_ptr<Animation>> cache_;
};
// Level entrances/exits (lvlwarp.txt): the screen rectangle around the tile that can be clicked.
struct WarpInfo {int selectX=0,selectY=0,selectWidth=0,selectHeight=0;};
class WarpTable {
public:
    explicit WarpTable(const Archives& archives);
    const WarpInfo* find(uint32_t row) const {return row<warps_.size()?&warps_[row]:nullptr;}
private:
    std::vector<WarpInfo> warps_;
};
// Levels as the game's waypoint packets refer to them: native level id, display name and waypoint number (255 = none).
// inside: IsInside, dark but for light sources. warpText: the entrance label leading here ("To The Blood Moor", a string key).
// warp/vis: the level's entrances (lvlwarp.txt ids) and the level each one leads to.
// automapName: the level type's name as AutoMap.txt writes it ("Act 1 - Wilderness" -> "1 Wilderness").
// layer: levels.txt Layer, the automap a level draws on (the outdoor areas of an act share one, so walking between them keeps the map).
// The sounds of the level (levels.txt SoundEnv, the row of soundenviron.txt that D2Client loads itself): song (its music,
// a sounds.txt row), ambience and event (day 0 / night 1: the looping scene sound, the first of a sounds.txt group of random
// events), eventDelay (in game frames). rain: levels.txt Rain (it can rain here; snow in Act V), mud: levels.txt Mud.
struct LevelSounds {int song=0;std::array<int,2> ambience{},event{};int eventDelay=0;};
struct LevelInfo {int32_t id=0;std::string name,warpText,automapName;int waypoint=255;bool inside=false;std::array<int,8> warp{},vis{};int layer=0;LevelSounds sounds;bool rain=false,mud=false;};
// pettype.txt: how the roster at the top left shows each kind of pet (D2Client 0x6FADAB00). name: a string key (the
// Valkyrie's "StrUI4"; a mercenary shows its own name instead), drawHp: a life bar above the icon, iconType: 1 one icon each,
// 2 one icon for all of them with their number; the icon is by monster class where classIcons names it (monstats row,
// then the file: golems, totems, each act's hirelings), baseIcon otherwise; a type without one has no portrait. Icons are data\global\ui\hireables\<icon>.dc6.
struct PetTypeInfo {std::string name;bool drawHp=false;int iconType=0;std::string baseIcon;std::vector<std::pair<uint32_t,std::string>> classIcons;};
class PetTypeTable {
public:
    explicit PetTypeTable(const Archives& archives);
    const PetTypeInfo* find(uint32_t type) const {return type<rows_.size()?&rows_[type]:nullptr;}
    // The icon a pet of this type and monster class shows.
    std::string icon(uint32_t type,uint32_t classId) const;
private:
    std::vector<PetTypeInfo> rows_;
};
class LevelTable {
public:
    explicit LevelTable(const Archives& archives);
    const LevelInfo* byId(int32_t id) const;
    const LevelInfo* byWaypoint(int waypoint) const;
private:
    std::map<int32_t,LevelInfo> byId_;
    std::map<int,int32_t> waypointToId_;
};
// Skills as the client shows them: skills.txt joined with skilldesc.txt.
struct SkillInfo {
    uint32_t id=0;
    std::string name,displayName; // internal name, name in the game's language
    int charClass=-1; // 0 amazon .. 6 assassin, -1 none
    int page=0,row=0,column=0,iconCel=-1; // tree position (1 based) and picture number
    int listRow=-1,listPool=0; // skilldesc.txt ListRow (the skill list's row: 0 general, 1-3 the class's pages, -1 never listed) and ListPool
    bool leftSkill=false,passive=false; // skills.txt leftskill (may go on the left button) and passive
    int requiredLevel=0,maxLevel=0; // skills.txt reqlevel/maxlvl
    std::array<uint32_t,3> prerequisites{}; // skills.txt reqskill1-3, as skill ids
    std::string castOverlay; // overlay.txt row played on the caster while casting (skills.txt castoverlay), empty none
    std::string startSound;  // sounds.txt row played when the cast starts (skills.txt stsound), empty none
    std::array<std::string,3> doSounds; // skills.txt dosound/a/b: extra cast/action sounds
    std::string targetSound,progressSound; // skills.txt tgtsound/prgsound
    int serverDoFunc=0;      // skills.txt srvdofunc (76 Whirlwind, 67 Charge: D2Client draws those units specially)
    int seqMode=-1;          // skills.txt seqtrans: the player mode whose frames the sequence mode (18) plays (Charge: A1), -1 none
    std::array<std::string,5> clientMissiles; // skills.txt cltmissilea/b/c/d (or cltmissile): missiles the client itself adds when used
    std::array<std::string,5> serverMissiles; // skills.txt srvmissilea/b/c/d (or srvmissile): missiles the server/gameplay creates
    std::array<std::string,2> clientOverlays; // skills.txt cltoverlaya/b: one-shot overlays on the caster
    std::string targetOverlay,progressOverlay,serverOverlay; // target/progress/server overlays named by skills.txt
    std::array<int,8> param{}; // skills.txt Param1-8
    std::string calc1,calc2; // skills.txt calc1/calc2. Blizzard's radius and frequency are par1 and par3.
    // skills.txt cltstfunc/cltdofunc: D2Client 1.10f's functions run when the skill starts and on its frame event (tables at
    // 0x6FB77488 and 0x6FB775A8). 18/30 are the curses. auraRangeCalc: skills.txt aurarangecalc (a curse's radius).
    int clientStart=0,clientDo=0;std::string auraRangeCalc;
    std::array<int,3> clientProgress{}; // skills.txt cltprgfunc1-3: a charge-up skill's client functions by charge (12 Tiger, 13 Cobra Strike)
    std::string auraState; // skills.txt aurastate (a charge-up skill's charges live in that state's stat)
    // A calc column whose value only depends on the skill level: parN, or lnXY (ParamX + ParamY per level above the first).
    int levelCalc(const std::string& calc,int level) const;
    // skills.txt range: whether the hero walks up to the target first. "h2h": yes, then strikes (melee skills); "rng",
    // "both": ranged attacks; "none": cast from where the hero stands (most spells, from Fire Bolt to Teleport).
    std::string range;
    bool aura=false; // skills.txt aura: works while chosen (Might, Concentration), or passive (the masteries)
    int mana=0,levelMana=0,manaShift=0,minMana=0; // skills.txt mana, lvlmana, manashift, minmana
    // Mana a cast takes at a skill level, in 1/256 points, as D2Game 1.10f charges it (0x6FD10C07): (mana + lvlmana per
    // level above the first) << manashift, at least minmana. Skills without mana or lvlmana cost nothing.
    int32_t manaCost(int level) const {
        if(!mana&&!levelMana)return 0;
        const int32_t cost=(mana+std::max(level-1,0)*levelMana)<<manaShift;
        return std::max(cost,minMana<<8);
    }
};
class SkillTable {
public:
    SkillTable(const Archives& archives,const StringTable& strings);
    const SkillInfo* find(uint32_t id) const;
    const SkillInfo* findByName(const std::string& name) const; // skills.txt skill column, any case
    const SkillInfo* withAuraState(const std::string& state) const; // the first skill whose aurastate is that state
    // The skills of one class that appear in the skill tree, by page, row and column.
    std::vector<const SkillInfo*> tree(int charClass) const;
    // Picture file of a class's skill icons (data\global\ui\spells\??skillicon.dc6).
    static std::string iconFile(int charClass);
private:
    std::map<uint32_t,SkillInfo> skills_;
};
// Skill icons of the classes, decoded once. The icon number is the picture of the skill itself, the next one is the pressed picture.
class SkillArt {
public:
    SkillArt(const Archives& archives,const Palette& palette):archives_(archives),palette_(palette) {}
    const Image* get(int charClass,int iconCel,bool pressed=false);
private:
    const Archives& archives_;
    Palette palette_;
    std::map<uint64_t,std::unique_ptr<Image>> cache_;
    std::map<int,Bytes> files_;
};
// Inventory pictures (data\global\items\*.dc6) of items, decoded once with the given palette.
class ItemArt {
public:
    ItemArt(const Archives& archives,const Palette& palette,const ItemTable& items):archives_(archives),palette_(palette),items_(items) {}
    // Null when the game data has no picture for the item. trans / color: the item's colour (itemspalette file 1..8 and
    // colour 0..20, trans 0 none): the picture is drawn through that file's colour map.
    const Image* get(uint32_t code,uint32_t quality,uint32_t invGraphic,uint8_t trans=0,uint8_t color=0);
private:
    const Archives& archives_;
    Palette palette_;
    const ItemTable& items_;
    std::map<std::string,std::unique_ptr<Image>> cache_;
    std::map<uint8_t,Bytes> maps_; // items/palette/*.dat by file number: 21 colour maps of 256 indices
};
}
