// The viewer's client state: what the player sees and does (panels, cursor, NPC dialogs, the world snapshot, the camera),
// shared by input, rendering and the game loop. The window and its input come through Platform.h.
#pragma once
#include "Assets.h"
#include "Navigation.h"
#include "Occlusion.h"
#include "Backend.h"
#include "NativeTown.h"
#include "NativeGame.h"
#include "Tables.h"
#include "Audio.h"
#include "UIAssets.h"
#include "FrontEnd.h"
#include "Weather.h"
#include "Platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
inline float zoom=4.0f;
inline float initialZoom=4.0f;
inline int frameStep=0;
inline float panX=0,panY=0;
inline int dragX=0,dragY=0;inline bool dragging=false; // panning the view with a held button (the map viewer, or the middle button)
inline bool showCollision=false,followPlayer=false,runEnabled=false;
inline const d2::CollisionGrid* walkGrid=nullptr;
inline const d2::MapScene* walkScene=nullptr;
inline std::unique_ptr<d2::GameBackend> world;
inline d2::WorldSnapshot worldView;
inline std::vector<d2::Command> pendingCommands;
// What the player last attacked or cast at: a unit (its game number, 0 none) or a subtile. The player faces it while the
// attack or cast plays, as the game turns the character toward its target.
inline int32_t actionTargetUnit=0;
inline bool actionTargetIsPoint=false;inline d2::GridPoint actionTargetPoint{};
inline int heldFacing=-1; // the facing of the last attack or cast, kept until the player moves (-1 none)
// The skill of the player's last cast and the tick its cast animation (mode 10) began: which cast overlay plays, and its frame.
inline int32_t castSkill=0;inline uint64_t castStart=0;inline uint8_t lastPlayerMode=255;
inline std::string walkStatus="Ready";
enum class GameMenuPage:uint8_t {Closed,Main,Options,Sound,Cheats};
struct GameMenuRegion {float x,y,w,h;int action;};
inline GameMenuPage gameMenuPage=GameMenuPage::Closed;
inline std::vector<GameMenuRegion> gameMenuRegions;
inline int gameMenuSelection=0,gameMenuPressed=0;
std::vector<std::pair<int,std::string>> gameMenuItems(); // the Esc menu page's lines (action, caption), Input.cpp
inline bool leaveToMainMenu=false; // the Esc menu's Back to Main Menu: the game loop ends and the menus start again
void resetClientState(); // everything above back to how a new game starts (after leaving one for the main menu)
inline d2::Audio* gameAudio=nullptr;
inline const d2::SoundBank* gameSounds=nullptr; // sounds.txt, for the NPCs' speech (null without audio)
inline const d2::StringTable* gameStrings=nullptr; // the string tables (native game only)
inline const d2::ItemTable* gameItems=nullptr; // item metadata (native game only)
inline const d2::MissileTable* gameMissiles=nullptr; // missiles.txt (native game only)
inline const d2::SkillTable* gameSkills=nullptr; // skills.txt (native game only)
inline const d2::UnitArt* gameUnitArt=nullptr; // monster art and monstats/monstats2 (native game only)
// Effects the client adds itself where a missile ends (missiles.txt CltHitSubMissile: Holy Bolt's and Fire Bolt's explosions):
// the missile row, where (scene subtiles) and the tick it began; each lasts its Range in ticks.
// vx, vy: subtiles a tick. fallFrames (pCltDoFunc 9): the effect starts that many frames away from (x, y) and reaches it.
// fallRate and slideRate are the screen pixels of height and of sideways offset lost each of those frames (CltParam2, CltParam3).
// A straight fall (blizzard ice) leaves slideRate at 0: fallFrames is CltParam1/CltParam2 and fallRate is CltParam2.
// facing: -1 derives it from vx, vy. life: 0 uses the missile's own duration; a lightning segment sets the subloop length.
// animRate: 0 uses the row. Ice break's melt is forced to 1.
struct ClientEffect {uint32_t classId=0;float x=0,y=0;uint64_t born=0;float vx=0,vy=0;uint32_t tickOffset=0;int fallFrames=0,fallRate=0,slideRate=0;int facing=-1,life=0,animRate=0;};
inline std::vector<ClientEffect> clientEffects;
// Lights the client keeps itself (not a missiles.txt row): a curse center's, D2Client 0x6FAA4280. Subtiles, radius in
// subtiles, colour 0..1. Rebuilt every tick.
struct ClientLight {float x=0,y=0,radius=0;std::array<float,3> color{1,1,1};};
inline std::vector<ClientLight> clientLights;
// The skill whose frame event (D2Client's do-function moment) passed since the audio last looked: its dosound and tgtsound
// play then (0x6FABC94E). 0 none.
inline uint32_t pendingDoSoundSkill=0;
// The character's "impossible" line is due (packet 0x2C, sound event 19: the server refused something).
inline bool pendingImpossibleSound=false;
inline const d2::OverlayTable* gameOverlays=nullptr; // overlay.txt (native game only)
inline const d2::StateTable* gameStates=nullptr; // states.txt (native game only)
inline const d2::LevelTable* gameLevels=nullptr; // levels.txt (native game only)
// Rain and snow (Weather.h), stepped each tick in the game screen's pixels: the size the renderer last showed.
inline d2::Weather gameWeather;
inline float weatherScreenWidth=800,weatherScreenHeight=600;
// Overlays the game plays once over a unit (packet 0x11: the lightning of Fist of the Heavens on its target...): the unit's
// native number, the overlay and the tick it began.
struct UnitOverlay {uint32_t unit=0;std::string name;uint64_t start=0;};
inline std::vector<UnitOverlay> unitOverlays;
// Renderer-side snapshot of the simulated character: continuous subtile position
// (interpolated between ticks), facing, and the animation state/tick.
struct CharacterView {float x=0,y=0;unsigned facing=0;bool moving=false;uint64_t tick=0;uint8_t mode=255,skillMoveFrame=255,sequenceMode=255,sequenceFrame=0;uint16_t shapeClass=0xFFFF;uint8_t shapeMode=0;};
// Ticks each object has spent in its current animation mode, and the screen boxes (scene pixels) of the selectable ones drawn last frame.
struct ObjectClock {uint32_t mode=0;uint64_t ticks=0;};
inline std::map<uint32_t,ObjectClock> objectClocks;
inline std::map<uint32_t,uint32_t> objectClasses;
inline std::map<uint32_t,uint32_t> openedObjectModes; // containers the game has reported as opened, kept open visually
struct ObjectPick {float x,y,w,h;uint32_t guid;float depth;};
inline std::vector<ObjectPick> objectPicks;
inline bool persistentOpenedContainer(uint32_t classId,const d2::ObjectInfo* info) {
    switch(classId) {
        case 1:case 3:case 4:case 5:case 6:case 9:case 28:case 50:case 51:case 52:case 53:case 79:case 87:case 88:case 89:case 94:case 95:
        case 139:case 140:case 141:case 144:case 146:case 147:case 148:case 176:case 177:case 181:case 183:case 198:case 208:case 209:
        case 240:case 241:case 242:case 243:case 284:case 329:case 330:case 331:case 332:case 333:case 334:case 335:case 336:
        case 354:case 355:case 356:case 365:case 371:case 372:case 387:case 388:case 389:case 390:case 391:case 397:case 405:case 406:case 407:
        case 413:case 420:case 424:case 425:case 430:case 431:case 432:case 433:case 454:case 455:case 485:case 486:case 487:
        case 501:case 502:case 504:case 505:case 524:case 525:case 526:case 529:case 530:case 531:case 532:case 533:case 534:case 541:case 544:
            return true;
        default:
            break;
    }
    if(!info)return false;
    std::string name=info->name;
    for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    for(const char* word:{"chest","casket","coffin","sarcophagus","urn","basket","tomb","grave"})
        if(name.find(word)!=std::string::npos)return true;
    return false;
}
inline uint32_t hoveredObject=0; // the game object under the cursor (0: none): lit up and named
struct UnitPick {float x,y,w,h;uint32_t guid,classId;uint8_t kind,monsterType;float depth;};
inline std::vector<UnitPick> unitPicks;
inline uint32_t hoveredUnit=0; // the town person or enemy under the cursor (0: none): lit up and named
inline std::function<void()> afterTick; // runs after every simulation step (level scene switching)
inline int mouseX=-1,mouseY=-1;
inline bool leftButtonDown=false; // the left mouse button is held (buttons are drawn pressed under it)
inline bool rightButtonDown=false;
// Milliseconds on a steady clock (the cursor's animation).
inline uint64_t nowMilliseconds(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
// Screen rectangles of the ground item labels drawn last frame (client pixels), for clicking them.
struct ItemLabelBox {float x,y,w,h;int32_t item;};
inline std::vector<ItemLabelBox> itemLabelBoxes;
inline std::map<uint32_t,uint64_t> itemLabelUntil; // ground items shown briefly after dropping, as if Alt were held
// Inventory panel (toggled with I): the regions drawn last frame, for clicking them, and the item on the cursor.
struct InventoryRegion {float x,y,w,h;int kind;int a,b;int32_t item;}; // kind 1: grid cell (a,b), 2: body slot a
inline std::vector<InventoryRegion> inventoryRegions;
inline bool inventoryOpen=false;
// Belt slots drawn last frame; the front potion of column 0..3 is drunk by the number keys 1..4 or a left click.
inline std::vector<InventoryRegion> beltRegions;
inline bool beltOpen=false; // the belt's upper rows shown above the control panel (~ key, or a click on an empty slot)
inline float panelX=0,panelY=0,panelW=0,panelH=0;
inline int32_t cursorItem=0,cursorItemW=1,cursorItemH=1;
inline bool beltPressed=false; // the left button went down on the belt; the belt acts when it comes up (Input.cpp)
inline bool scriptedShift=false; // test hooks: the next click counts as shift held (D2MODERN_CLICKS "s" points)
inline int32_t inventoryOccupied[4][10];
inline int32_t pendingIdentifyItem=0; // scroll or book the server armed (packet 0x3F); the next left click is the target
inline int spellCursor=-1; // cursor\buysell frame from that packet; -1 is the hand. Identify's books.txt spellicon is 0.
inline void clearIdentifyCursor(){pendingIdentifyItem=0;spellCursor=-1;}
// The store's sell mode (its Sell button): the NPC an item clicked in the inventory is sold to, 0 when not selling.
inline int32_t sellToNpc=0;
// The smith whose Repair mode is on (buysellbtn 6/7): a clicked carried or worn item is repaired alone (0x35), 0 off.
inline int32_t repairAtNpc=0;
// The store button last chosen, which is also the cursor's cursor\buysell frame (D2Client 0x6FAEACC2, from the button
// records at 0x6FAE91C3): 3 Buy, 4 Sell, 1 Repair; -1 none. A store opens with none chosen (0x6FAE95B0 clears them):
// the cursor stays the hand and a click on a store item still buys.
inline int storeCursor=-1;
inline bool expansionGame=true; // a Lord of Destruction character (d2s status 0x24 bit 0x20); a classic one has no weapon sets
inline bool characterHardcore=false; // the character played is hardcore (d2s status 0x24 bit 4): its death ends it
inline int gameDifficulty=0; // 0 Normal, 1 Nightmare, 2 Hell
inline uint32_t boughtItemCode=0; // the item last asked for in a store (packet 0x32): its drop sound when bought
inline bool secondWeaponSet=false; // weapon set II in use (packet 0x97 flips it)
// The stash (an object in town) and the Horadric Cube (used from the inventory). The game opens them (packet 0x77:
// 16 stash, 21 cube), their items are the player's items on page 4 (stash, 6 by 8) or 3 (cube, 3 by 4), and the left
// panel shows them. Regions: 50 a grid cell (a, b = cell), 51 close, 52 transmute.
inline bool stashOpen=false,cubeOpen=false;
inline std::vector<InventoryRegion> storageRegions;
inline float storageX=0,storageY=0,storageW=0,storageH=0;
// The quest log (Q, or the mini panel's quest button): the game sends it on request (client packet 0x40): 0x28 type 6
// the quest flags (16 bits per quest, QFLAG_*: bit 0 reward granted, 1 reward pending, 2 started, 15 completed in an
// earlier game), 0x52 each quest's log state (which qstsaXqYN line describes it), 0x50 the Den of Evil's monsters left.
// Quests are numbered as the game numbers them: Act I 1-6, II 9-14, III 17-22, IV 25-27, V 35-40.
struct QuestLog {
    bool open=false,known=false;
    int act=0,selected=0; // the act shown (0-4) and the chosen quest within it
    std::array<uint8_t,41> state{};
    std::array<uint16_t,48> flags{};
    int monstersLeft=0;
    uint64_t requestedAt=0;
    std::array<uint64_t,41> burnStart{}; // when each quest's completion burn began to play (0: not yet)
};
inline QuestLog questLog;
// Regions: 70 a quest (a = its place in the act), 71 an act tab (a), 72 close, 73 the panel.
inline std::vector<InventoryRegion> questRegions;
// The hireling's panel (npcinv, on the left; the O key or its portrait): its open state and click regions (80 a body slot, 81 close).
inline bool hirelingOpen=false;
inline std::vector<InventoryRegion> hirelingRegions;
void toggleQuestLog();
// The gold popup of the coin buttons: how much gold to drop (the inventory's coin), deposit (the inventory's coin while the
// stash is open) or withdraw (the stash's coin). It starts at the most allowed; digits type an amount. Regions: 60 the
// inventory's coin, 61 the stash's coin, 62 accept, 63 cancel, 64 the popup itself.
struct GoldPopup {enum class Kind:uint8_t {Drop,Deposit,Withdraw};bool open=false;Kind kind=Kind::Drop;int32_t amount=0,limit=0;bool typed=false;};
inline GoldPopup goldPopup;
inline std::vector<InventoryRegion> goldRegions;
void openGoldPopup(GoldPopup::Kind kind);
void closeGoldPopup(bool accept);
inline int32_t storageOccupied[8][6]{};
// NPC conversation and store. The game drives it with packets: 0x27 opens the menu of a town person, 0x2A reports the
// result of a purchase. The player answers with 0x38 (open the store), 0x32 (buy), 0x33 (sell) and 0x30 (walk away).
struct NpcDialog {
    enum class Page {Menu,Talk,Shop,Hire};
    bool open=false;
    int32_t guid=0;uint32_t classId=0;uint8_t services=0;
    std::vector<uint16_t> lines; // string ids of what the NPC has to say through Talk (packet 0x27 menu types 0 and 2)
    // Quest messages offered as their own menu entries (menu type 2), captioned by NpcTopics.h; and the line the Talk page
    // is saying now (0 none).
    std::vector<uint16_t> topics;uint16_t speaking=0;
    bool gossip=false; // the line being said is the NPC's own (NpcGossip.h): not reported to the game as heard
    Page page=Page::Menu;
    bool gamble=false;
    uint32_t tab=1;
    std::string message;uint64_t messageUntil=0;
    bool openedInventory=false;
    uint64_t talkStart=0;double talkSeconds=0; // the Talk page: when the speech began (ms) and how long it scrolls
};
inline NpcDialog npcDialog;
// What a unit says over its head (packet 0x27 with one entry of type 3, D2Client 0x6FB1DE35: a hireling's "I feel much stronger
// now"), by the unit's native id, until a tick.
struct OverheadText {uint32_t guid=0;std::string text;uint64_t until=0;};
inline std::vector<OverheadText> overheadTexts;
// The player's mercenary (packet 0x81: its native id, monstats class and name string), 0 none.
inline uint32_t mercGuid=0,mercClass=0,mercName=0;
// A dead hireling the hiring NPCs can bring back (packet 0x9B: its name string, -1 none, and the gold it costs).
inline int32_t deadMercName=-1,resurrectCost=0;
// The player's pets in the order they came (packets 0x7A add/remove, 0x81 a mercenary): pettype.txt row, monstats class, native
// id, and a mercenary's name string (0 others).
struct Pet {uint32_t type=0,classId=0,guid=0,name=0;};
inline std::vector<Pet> pets;
inline const d2::PetTypeTable* gamePetTypes=nullptr; // pettype.txt (native game only)
inline std::string gameMessage;inline uint64_t gameMessageUntil=0; // red center-screen feedback from D2Game or client-side checks
// The waypoint menu (packet 0x63): the waypoint object talked to and the game's own activation flags (nFlags[8], word 0 is
// a status marker, words 1..7 hold one bit per waypoint number). Travel sends packet 0x49 with the target level's id.
struct WaypointMenu {
    bool open=false;
    int32_t guid=0;
    uint16_t flags[8]{};
    int act=-1; // tab shown, -1 until the menu is first drawn (then the act of the waypoint in use)
    bool activated(int waypoint) const {return waypoint>=0&&waypoint<112&&(flags[waypoint/16+1]&(1u<<(waypoint&15)))!=0;}
};
inline WaypointMenu waypointMenu;
inline std::vector<InventoryRegion> waypointRegions; // kind 40: travel to level id `a`, 41: close, 42: act tab `a`, 36: panel
inline float waypointPanelX=0,waypointPanelY=0,waypointPanelW=0,waypointPanelH=0;
// Regions of the dialog drawn last frame: kind 20 menu button (a: 1 talk, 2 trade, 3 gamble, 4 leave, 5 back), 21 store tab (a),
// 22 store cell (a,b) holding item, 23 the panel itself, 24 store button (a: 1 buy, 2 sell, 3 repair all).
inline std::vector<InventoryRegion> dialogRegions;
inline float dialogX=0,dialogY=0,dialogW=0,dialogH=0;
// Skills and the character: the skill tree window (T), the character window (C), and the two skills of the mouse buttons.
inline bool skillsOpen=false,characterOpen=false;
inline std::array<int,8> skillHotkeys{};
inline bool miniPanelOpen=false; // the button bar opened by the arrow in the middle of the control panel
inline bool automapOpen=false;   // the automap over the game (Tab)
// The loaded scene's rooms (native game) and where the scene lies in the level's subtiles: the automap reveals rooms and
// remembers them in world cells, so a new scene of the same map keeps what was seen.
inline std::vector<d2::NativeTown::Room> sceneRooms;
inline d2::GridPoint sceneOrigin{};
inline int skillTab=2;    // page of the tree that is shown
inline int skillMenu=0;   // 0 closed, 1 choosing the left skill, 2 the right skill
inline int32_t characterClass=-1;
inline std::string characterName;
// Regions of these windows drawn last frame: kind 30 tree skill (a id), 31 tab (a page), 32 left skill box, 33 right skill box,
// 34 skill of the menu (a id), 35 stat button (a stat id), 36 a panel, 37 run button, 38 new stats button, 39 new skills button,
// 42 close the inventory, 43 close the skill tree.
inline std::vector<InventoryRegion> skillRegions;
inline float skillPanelX=0,skillPanelY=0,skillPanelW=0,skillPanelH=0,characterPanelX=0,characterPanelY=0,characterPanelW=0,characterPanelH=0;
inline bool leftHeld=false;
inline d2::Window* gameWindow=nullptr; // the game's window (Platform.h)
// The title screen, main menu and character select while they are up; input goes to them instead of the game.
inline d2::FrontEnd* frontEnd=nullptr;
inline uint64_t cursorPressed=0; // when a mouse button last went down: the cursor's hand closes, then turns while held
inline float menuScale=1,menuLeft=0,menuTop=0; // where the 800x600 front end sits in the window
// Test hook: message WM_APP+7 with N asks for the next frame to be saved as shotN.bmp in D2MODERN_SHOT_DIR.
inline int shotRequest=-1;


// Functions of the client state (ClientState.cpp) and of input (Input.cpp).
void buttonSound();
std::filesystem::path executableDirectory();
std::filesystem::path defaultGameDirectory();
std::filesystem::path defaultSaveDirectory();
std::array<float,2> characterAnchor(const d2::MapScene& scene,const CharacterView& view);
void runTick();
int32_t beltItem(int column);
void closeStorage();
bool storageClick(int mx,int my,bool right);
bool inventoryClick(int mx,int my,bool right);
void closeWaypointMenu();
void closeNpcDialog();
// Talk: the NPC speaks (their voice from the speech archives) while the text scrolls up; when it is over (or skipped with a
// click) the lines count as heard and the menu is back.
void startTalk(uint16_t line=0); // 0: the next of the NPC's lines, else its gossip (NpcGossip.h)
void startIntroduction(); // the NPC's introduction (NpcGossip.h entry 0, or 1 for its class)
bool hasIntroduction(uint32_t monsterClass);
// The gossip each NPC says this game, chosen for all of them at the first Talk (D2Client 0x6FAF44C0): the table row's line.
inline std::map<uint32_t,int> gossipChoice;inline bool gossipChosen=false;
inline std::set<uint32_t> greeted; // the NPCs (monster class) that have said their greeting this game
void greetOnce(uint32_t monsterClass);
void finishTalk();
// The rows of what the NPC says (the number heading each spoken line left out).
std::vector<std::string> talkRows();
void handleServerPackets();
bool npcClick(int mx,int my,bool shift);
bool waypointClick(int mx,int my);
void selectSkill(int skill,bool left);
void miniPanelButton(int button); // 0 character, 1 inventory, 2 skill tree, 3 party, 4 automap, 5 messages, 6 quests, 7 game menu
bool skillClick(int mx,int my,bool right);
const d2::EntitySnapshot* pickUnit(float x,float y,d2::EntityKind kind);
const d2::EntitySnapshot* pickEnemy(float x,float y);
const d2::EntitySnapshot* pickTownPerson(float x,float y);
const d2::EntitySnapshot* pickCorpse(float x,float y); // a player body lying dead
bool screenToSubtile(int mx,int my,d2::GridPoint& out);
void holdWalk(); // once a tick: while the left button is held, keep walking towards the cursor
bool uiClick(int mx,int my,bool right);
// An item picked up by a press, dropped where the button comes up (Input.cpp). waiting: released before the game had put the
// item on the cursor; the drop happens when it arrives (within a second).
struct ItemDrag {bool armed=false;int fromX=0,fromY=0;int dropX=0,dropY=0;uint64_t since=0;bool waiting=false;};
inline ItemDrag itemDrag;
void worldClick(int mx,int my,bool right);
void saveRequestedShot(const std::function<void(const std::filesystem::path&)>& save);
void activateGameMenu(int action);
void handleInput(const d2::InputEvent& event);
