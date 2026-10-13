// The phone controls (TouchControls.h, ../TOUCH-CONTROLS.md): the joystick, the attack button, and fingers working the
// mouse.
#include "TouchControls.h"
#include "ClientState.h"
#include "NativeProtocol.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
namespace touch {
namespace pad=d2::pad;
namespace {
#ifdef __ANDROID__
bool on=true;
#else
bool on=std::getenv("D2MODERN_TOUCH")!=nullptr;
#endif
constexpr float pi=3.14159265f;
// The joystick, in window pixels: where the thumb landed (the ring's centre) and where it is now.
struct Stick {int64_t finger=0;float cx=0,cy=0,x=0,y=0;};
std::optional<Stick> stick;
bool walking=false,runningByStick=false;
uint32_t ticksSinceOrder=0;
float orderedAngle=0;
// The attack button: the finger on it, the enemy aimed at (native unit number), ticks since the last attack order.
std::optional<int64_t> attackFinger;
uint32_t aimed=0,ticksSinceAttack=0;
// A skill button under a finger: where the finger is, whether it has been dragged out (aiming), how long it has been
// held still (a long press chooses the button's skill) and whether that happened (the lift then casts nothing).
struct SkillFinger {int64_t finger=0;int slot=0;float x=0,y=0;bool dragged=false;uint32_t heldTicks=0;bool choosing=false;};
std::optional<SkillFinger> skillFinger;
int assigningSlot=-1; // the skill button the skill popup was opened for (-1: none)
constexpr int skillButtons=4;
// The fingers working the mouse: the first is the left button, a second one at the same time the right button.
std::optional<int64_t> leftFinger,rightFinger;

Settings chosen;
std::filesystem::path settingsFile;
struct SafeArea {float left,top,right,bottom;};
SafeArea safeArea(int width,int height) {
    int x=0,y=0,w=width,h=height;
    if(gameWindow)gameWindow->safeArea(x,y,w,h);
    const int left=std::clamp(x,0,width),top=std::clamp(y,0,height);
    const int right=std::clamp(x+w,left,width),bottom=std::clamp(y+h,top,height);
    if(right<=left||bottom<=top)return {0,0,float(width),float(height)};
    return {float(left),float(top),float(right),float(bottom)};
}
float hudScale() {
    int w=0,h=0;if(gameWindow)gameWindow->clientSize(w,h);
    const auto safe=safeArea(w,h);
    return std::max(.5f,std::min((safe.right-safe.left)/800.f,(safe.bottom-safe.top)/600.f));
}
// The controls' size: the interface's scale, times the size chosen in the options.
float controlScale() {
    constexpr float sizes[]={.65f,.8f,1.f,1.25f,1.5f};
    return hudScale()*sizes[std::clamp(chosen.size,0,4)];
}
// How solid the controls are drawn (multiplies their transparency), as chosen in the options.
float opacity() {
    constexpr float amounts[]={.6f,1.f,1.4f};
    return amounts[std::clamp(chosen.opacity,0,2)];
}
// The layout is drawn for the right hand; left-handed, it is mirrored (offsets to the left go to the right).
float side(){return chosen.leftHanded?-1.f:1.f;}
float radius(){return 64*controlScale();} // the joystick's ring, in window pixels
// The attack button: right of the control panel's right end, above the mana globe (mirrored: above the life globe).
struct Circle {float x,y,r;};
Circle attackButton() {
    int w=0,h=0;if(gameWindow)gameWindow->clientSize(w,h);
    const auto safe=safeArea(w,h);
    const float s=controlScale();
    return {chosen.leftHanded?safe.left+96*s:safe.right-96*s,safe.bottom-160*s,44*s};
}
// The skill buttons: an arc around the attack button, from its inner side round to above it.
Circle skillButton(int slot) {
    const auto attack=attackButton();
    const float s=controlScale(),angle=pi+float(slot)*pi/6; // 180, 210, 240, 270 degrees (screen y points down)
    return {attack.x+side()*std::cos(angle)*110*s,attack.y+std::sin(angle)*110*s,28*s};
}
int hotkeySkill(int slot){return skillHotkeys[size_t(slot)];} // F1-F4; 0: none chosen yet
int skillLevelOf(int id) {
    for(const auto& skill:worldView.skills)if(int(skill.id)==id)return skill.level;
    return 0;
}
// Whether there is mana for the skill (worldView.stats[8]: mana in 1/256 points).
bool affordable(int skill) {
    const auto* info=gameSkills?gameSkills->find(uint32_t(skill)):nullptr;
    return !info||info->manaCost(skillLevelOf(skill))<=worldView.stats[8];
}
// In the game, alive, no menu or popup up and nothing on the cursor.
bool inGame() {
    if(!walkGrid||frontEnd||gameMenuPage!=GameMenuPage::Closed||goldPopup.open||skillMenu)return false;
    return !(cursorItem||pendingIdentifyItem||worldView.vitals.maxLife<=0||worldView.vitals.life<=0);
}
bool playing(){return on&&inGame();} // the touch controls (a game controller works with or without them)
// The joystick also needs its half of the screen free of panels, the buttons theirs. On the left: the character sheet,
// stash, cube, quest log, a store, the waypoints; on the right: the inventory, the skill tree (and a store's inventory).
bool leftHalfCovered(){return characterOpen||stashOpen||cubeOpen||questLog.open||npcDialog.open||waypointMenu.open;}
bool rightHalfCovered(){return inventoryOpen||skillsOpen||npcDialog.open;}
bool joystickUsable(){return playing()&&!(chosen.leftHanded?rightHalfCovered():leftHalfCovered());}
bool attackUsable(){return playing()&&!(chosen.leftHanded?leftHalfCovered():rightHalfCovered());}
// Where a finger may start the joystick: its side of the screen (the left 45%, or the right 45% left-handed), above
// the globes and the control panel.
bool joystickMayStart(int x,int y) {
    if(!joystickUsable())return false;
    int w=0,h=0;gameWindow->clientSize(w,h);
    const auto safe=safeArea(w,h);
    const float controlPanelTop=safe.bottom-60*hudScale(); // the life globe and the control panel
    const bool onItsSide=chosen.leftHanded?float(x)>safe.left+(safe.right-safe.left)*.55f:
        float(x)<safe.left+(safe.right-safe.left)*.45f;
    return onItsSide&&float(x)>=safe.left&&float(x)<=safe.right&&float(y)>=safe.top&&float(y)<controlPanelTop;
}
bool onAttackButton(int x,int y) {
    if(!attackUsable())return false;
    const auto b=attackButton();
    return std::hypot(float(x)-b.x,float(y)-b.y)<=b.r*1.15f; // a little extra room: thumbs land wide
}
// A finger working the mouse: the events a mouse would give.
void mouse(d2::InputEvent::Type type,int x,int y,int button) {
    d2::InputEvent e;e.type=d2::InputEvent::Type::MouseMove;e.x=x;e.y=y;
    if(type!=d2::InputEvent::Type::MouseMove){handleInput(e);e.type=type;e.button=button;}
    mouseX=x;mouseY=y;
    handleInput(e);
}
void setRunning(bool run) {
    if(run==runningByStick)return;
    runningByStick=run;
    pendingCommands.push_back(d2::Command::setRun(run||runEnabled));
}
void release() {
    if(walking)pendingCommands.push_back(d2::Command::stop());
    walking=false;
    if(runningByStick){runningByStick=false;pendingCommands.push_back(d2::Command::setRun(runEnabled));}
    stick.reset();
}
// A game controller: its axes (sticks -1..1, triggers 0..1) and buttons (pad::, then the triggers as buttons 15 LT and
// 16 RT, pressed past half way). padAttack: A is held (keeps attacking, as a finger on the attack button does).
constexpr int padLT=pad::Buttons,padRT=pad::Buttons+1;
float padAxis[pad::Axes]{};
bool padAttack=false;
bool padUsed=false; // a controller has been used: from then on its walking, targets and use button take part
// The direction the hero is steered: the touch joystick, or a controller's left stick. Strength 1 is the ring's edge
// (or the stick's edge, reached at 85%: diagonals rarely reach the corner).
struct Steer {float angle,strength;};
std::optional<Steer> steering() {
    if(stick) {
        const float dx=stick->x-stick->cx,dy=stick->y-stick->cy;
        return Steer{std::atan2(dy,dx),std::hypot(dx,dy)/radius()};
    }
    const float x=padAxis[pad::LeftX],y=padAxis[pad::LeftY],m=std::hypot(x,y);
    if(m<.25f)return std::nullopt;
    return Steer{std::atan2(y,x),m/.85f};
}
const d2::EntitySnapshot* playerEntity() {
    for(const auto& e:worldView.entities)if(e.id&&e.id==worldView.localPlayer)return &e;
    return nullptr;
}
// The subtile the hero should walk to: 96 scene pixels ahead in the thumb's direction (a screen direction turned into the
// isometric grid, where a subtile is 32x16 pixels: 3 subtiles sideways, 6 up or down), moved back towards the hero until
// it can be reached, and turned along walls when straight ahead is blocked.
std::optional<d2::GridPoint> walkTarget(float angle) {
    const auto& grid=*walkGrid;
    const auto* player=playerEntity();
    if(!player)return std::nullopt;
    const d2::GridPoint here=d2::subtileOf(player->position);
    auto reach=[&](float a)->std::optional<d2::GridPoint> {
        const float sx=std::cos(a)*96.f,sy=std::sin(a)*96.f;
        const float gx=(sx/16.f+sy/8.f)*.5f,gy=(sy/8.f-sx/16.f)*.5f;
        const int steps=int(std::ceil(std::max(std::abs(gx),std::abs(gy))));
        d2::GridPoint last=here;
        for(int i=1;i<=steps;++i) {
            const d2::GridPoint p{here.x+int(std::lround(gx*float(i)/float(steps))),here.y+int(std::lround(gy*float(i)/float(steps)))};
            if(p==last)continue;
            if(!grid.canStep(last,p))break;
            last=p;
        }
        if(std::max(std::abs(last.x-here.x),std::abs(last.y-here.y))<2)return std::nullopt;
        return last;
    };
    for(float turn:{0.f,pi/8,-pi/8,pi/4,-pi/4,3*pi/8,-3*pi/8})if(auto p=reach(angle+turn))return p;
    return std::nullopt;
}
// How far the attack button looks for enemies: 18 subtiles, about half the 800x600 screen across.
constexpr float reach=18;
// An enemy that can be attacked: alive (the game removes the dead from the enemies: they become corpses).
bool attackable(const d2::EntitySnapshot& e){return e.kind==d2::EntityKind::Enemy&&e.life!=0;}
// The enemy to attack: the one aimed at while it lives and is within reach; otherwise the nearest one the joystick points
// at (within 45 degrees on screen); otherwise the nearest one.
uint32_t chooseTarget() {
    const auto* player=playerEntity();
    if(!player)return 0;
    const float px=float(player->position.x)/float(d2::fixedOne),py=float(player->position.y)/float(d2::fixedOne);
    const auto steer=steering();
    const bool pointing=steer&&steer->strength>=.25f;
    const float pointed=pointing?steer->angle:0;
    uint32_t nearest=0,nearestPointed=0;float best=reach,bestPointed=reach;
    for(const auto& e:worldView.entities) {
        if(!attackable(e)||e.id<1000)continue;
        const uint32_t unit=uint32_t(e.id-1000);
        const float gx=float(e.position.x)/float(d2::fixedOne)-px,gy=float(e.position.y)/float(d2::fixedOne)-py;
        const float distance=std::hypot(gx,gy);
        if(unit==aimed&&distance<=reach*1.2f)return unit;
        if(distance<best){best=distance;nearest=unit;}
        if(pointing) {
            // The enemy's direction on screen (a subtile step is 16 pixels across and 8 down per grid unit).
            float turned=std::abs(std::atan2((gx+gy)*8.f,(gx-gy)*16.f)-pointed);if(turned>pi)turned=2*pi-turned;
            if(turned<=pi/4&&distance<bestPointed){bestPointed=distance;nearestPointed=unit;}
        }
    }
    // Auto-target off (the options): only the enemy the joystick points at.
    return nearestPointed?nearestPointed:chosen.autoTarget?nearest:0;
}
// The left skill on the target, as clicking the enemy does.
void attack() {
    ticksSinceAttack=0;
    aimed=chooseTarget();
    if(aimed)pendingCommands.push_back(d2::Command::attack(int32_t(aimed)));
}
// A window point in scene pixels (what pickEnemy takes).
std::array<float,2> toScene(float x,float y) {
    int w=0,h=0;gameWindow->clientSize(w,h);
    return {(x-float(w)*.5f-panX)/zoom+float(walkScene->width)*.5f,(y-float(h)*.5f-panY)/zoom+float(walkScene->height)*.5f};
}
// Where a dragged skill button aims: the hero (the middle of the screen, which follows the hero) plus three times the
// drag, kept inside the window.
std::array<float,2> aimPoint(const SkillFinger& f) {
    const auto b=skillButton(f.slot);
    int w=0,h=0;gameWindow->clientSize(w,h);
    const float x=float(w)*.5f+(f.x-b.x)*3.f,y=float(h)*.5f+(f.y-b.y)*3.f;
    return {std::clamp(x,0.f,float(w-1)),std::clamp(y,0.f,float(h-1))};
}
// The point 96 scene pixels ahead of the hero, where the joystick points (or straight down the screen when it rests).
std::optional<d2::GridPoint> pointAhead() {
    float angle=pi/2;
    if(const auto steer=steering();steer&&steer->strength>=.25f)angle=steer->angle;
    return walkTarget(angle);
}
// A skill button's skill, made the right skill (as its F key does) and cast as a right click would be:
// - an aura only needs choosing: it works while it is the right skill;
// - aimed (dragged): on the enemy under the aim point, or at the place;
// - tapped: on the best enemy (a right click on it); with no enemy near, a melee skill (range h2h) does nothing and the
//   others go to the point ahead of the hero (a right click on the ground). Skills centred on the caster (warcries,
//   Holy Shield) take effect around the hero wherever they are aimed, as in the game.
void castSkill(int slot,std::optional<std::array<float,2>> aim) {
    const int skill=hotkeySkill(slot);
    const auto* info=gameSkills?gameSkills->find(uint32_t(skill)):nullptr;
    const auto* player=playerEntity();
    if(!skill||!info||!player)return;
    if(worldView.rightSkill!=skill)selectSkill(skill,false);
    const std::string& range=info->range;
    static const bool log=std::getenv("D2MODERN_TOUCH_LOG")!=nullptr; // test hook: what the skill buttons do
    if(log)std::printf("Touch: skill button %d casts %d (range '%s'%s)%s\n",slot,skill,range.c_str(),info->aura?", aura":"",aim?" where aimed":"");
    if(info->aura)return;
    if(aim) {
        const auto scene=toScene((*aim)[0],(*aim)[1]);
        if(const auto* enemy=pickEnemy(scene[0],scene[1])){aimed=uint32_t(enemy->id-1000);pendingCommands.push_back(d2::Command::castOn(true,int32_t(aimed)));return;}
        d2::GridPoint place;
        if(screenToSubtile(int((*aim)[0]),int((*aim)[1]),place))pendingCommands.push_back(d2::Command::castAt(true,place));
        return;
    }
    if(const uint32_t enemy=chooseTarget()) {
        aimed=enemy;
        pendingCommands.push_back(d2::Command::castOn(true,int32_t(enemy)));
        return;
    }
    if(range!="h2h")if(const auto ahead=pointAhead())pendingCommands.push_back(d2::Command::castAt(true,*ahead));
}
// Opens the skill popup (the right skill's list) to choose a skill button's skill.
void chooseSkillFor(int slot) {
    assigningSlot=slot;
    skillMenu=2;
}
// The potion buttons, below and left of the skill arc: the belt's first two columns (what keys 1 and 2 drink).
constexpr int potionButtons=2;
Circle potionButton(int column) {
    const auto a=attackButton();const float s=controlScale();
    return column==0?Circle{a.x-side()*200*s,a.y+62*s,22*s}:Circle{a.x-side()*145*s,a.y+72*s,22*s};
}
// What the use button would pick up, talk to or use, chosen each tick: within 10 subtiles of the hero, an item on the
// ground first (the nearest), then a town person, then an object the cursor could use (objects.txt selectable, as the
// mouse uses them: objectPicks) or a level entrance.
struct Nearby {enum class Kind:uint8_t {None,Item,Person,Object} kind=Kind::None;uint32_t id=0;};
Nearby nearby;
constexpr float useReach=10;
Circle useButton() {
    const auto a=attackButton();const float s=controlScale();
    return {a.x-side()*215*s,a.y-25*s,30*s};
}
Nearby findNearby() {
    const auto* player=playerEntity();
    if(!player)return {};
    const float px=float(player->position.x)/float(d2::fixedOne),py=float(player->position.y)/float(d2::fixedOne);
    auto distance=[&](float x,float y){return std::hypot(x-px,y-py);};
    Nearby best;float bestDistance=useReach;
    for(const auto& item:worldView.items) {
        if(item.mode!=3&&item.mode!=5)continue;
        const float d=distance(float(item.x)+.5f,float(item.y)+.5f);
        if(d<bestDistance){bestDistance=d;best={Nearby::Kind::Item,item.id};}
    }
    if(best.kind!=Nearby::Kind::None)return best;
    for(const auto& e:worldView.entities) {
        if(e.kind!=d2::EntityKind::TownPerson||e.id<1000)continue;
        const float d=distance(float(e.position.x)/float(d2::fixedOne),float(e.position.y)/float(d2::fixedOne));
        if(d<bestDistance){bestDistance=d;best={Nearby::Kind::Person,uint32_t(e.id-1000)};}
    }
    if(best.kind!=Nearby::Kind::None)return best;
    auto usable=[](uint32_t guid){for(const auto& pick:objectPicks)if(pick.guid==guid)return true;return false;};
    for(const auto* list:{&worldView.objects,&worldView.tiles})for(const auto& o:*list) {
        if(!usable(o.id))continue;
        const float d=distance(float(o.x)+.5f,float(o.y)+.5f);
        if(d<bestDistance){bestDistance=d;best={Nearby::Kind::Object,o.id};}
    }
    return best;
}
void use() {
    switch(nearby.kind) {
    case Nearby::Kind::Item:pendingCommands.push_back(d2::Command::pickup(int32_t(nearby.id)));break;
    case Nearby::Kind::Person:pendingCommands.push_back(d2::Command::interact(int32_t(nearby.id)));break;
    case Nearby::Kind::Object:pendingCommands.push_back(d2::Command::interactObject(int32_t(nearby.id)));break;
    default:break;
    }
}
bool onCircle(Circle b,int x,int y){return std::hypot(float(x)-b.x,float(y)-b.y)<=b.r*1.15f;}
// The panel buttons at the top right: the mini panel's character, inventory, skill tree, automap, quests and game menu
// buttons. Hidden while a panel covers the right half (its own close button closes it).
constexpr int panelButtons[]={0,1,2,4,6,7};
constexpr int standardPanelButtonCount=int(std::size(panelButtons));
bool hostingLanGame() {
    const char* port=std::getenv("D2MODERN_LAN_HOST");
    return d2::lanAvailable()&&port&&*port;
}
int panelButtonCount(){return standardPanelButtonCount+(hostingLanGame()?1:0);}
bool panelButtonsShown() {
    if(!on||!walkGrid||frontEnd||gameMenuPage!=GameMenuPage::Closed||goldPopup.open||skillMenu)return false;
    return !(inventoryOpen||skillsOpen||npcDialog.open);
}
std::array<float,4> panelButtonRect(int index) { // x, y, w, h in window pixels
    int w=0,h=0;if(gameWindow)gameWindow->clientSize(w,h);
    const auto safe=safeArea(w,h);
    const float s=hudScale(),size=30*s,gap=4*s;
    return {safe.right-8*s-float(panelButtonCount()-index)*(size+gap)+gap,safe.top+8*s,size,size};
}
int panelButtonAt(int x,int y) {
    if(!panelButtonsShown())return -1;
    for(int i=0;i<panelButtonCount();++i) {
        const auto r=panelButtonRect(i);
        if(float(x)>=r[0]-4&&float(x)<=r[0]+r[2]+4&&float(y)>=r[1]-4&&float(y)<=r[1]+r[3]+4)return i;
    }
    return -1;
}
// Items in the panels. A finger that lands on one shows its description (the cursor is moved there) and waits: lifted
// soon it is a tap (a left click; on a store's goods only the description, so that a stray tap buys nothing), moved it
// becomes the mouse, held still for 0.4 s it opens a menu of what a right click or shift click would do.
enum class ItemPlace:uint8_t {Grid,Body,Storage,Belt,Store};
struct ItemPress {int64_t finger=0;float x=0,y=0;int32_t item=0;ItemPlace place=ItemPlace::Grid;uint32_t ticks=0;};
std::optional<ItemPress> itemPress;
enum class MenuAction:uint8_t {Use,ToBelt,Drop,Buy,Sell,Equip,Unequip,Compare};
struct MenuOption {std::string label;MenuAction action;int32_t target=0;};
struct ItemMenu {float x=0,y=0;int32_t item=0;std::vector<MenuOption> options;std::vector<std::array<float,4>> rows;};
std::optional<ItemMenu> menu;
struct ItemAction {
    enum class Kind:uint8_t {Equip,Unequip} kind=Kind::Equip;
    int32_t item=0,x=0,y=0,page=0,bodyLoc=0;
    uint32_t ticks=0;
};
std::optional<ItemAction> itemAction;
struct ItemComparison {int32_t first=0,second=0;};
std::optional<ItemComparison> comparison;
std::optional<std::pair<int32_t,ItemPlace>> itemAt(int x,int y) {
    auto inside=[&](const InventoryRegion& r){return float(x)>=r.x&&float(x)<=r.x+r.w&&float(y)>=r.y&&float(y)<=r.y+r.h;};
    if(npcDialog.open&&npcDialog.page==NpcDialog::Page::Shop)
        for(const auto& r:dialogRegions)if(r.kind==22&&r.item&&inside(r))return std::pair{r.item,ItemPlace::Store};
    if(inventoryOpen)for(const auto& r:inventoryRegions)if((r.kind==1||r.kind==2)&&r.item&&inside(r))return std::pair{r.item,r.kind==1?ItemPlace::Grid:ItemPlace::Body};
    if(stashOpen||cubeOpen)for(const auto& r:storageRegions)if(r.kind==50&&r.item&&inside(r))return std::pair{r.item,ItemPlace::Storage};
    for(const auto& r:beltRegions)if(r.item&&inside(r))return std::pair{r.item,ItemPlace::Belt};
    return std::nullopt;
}
// What the menu offers: buying for a store's goods; using (what a right click does) for what can be used and for belt
// items; equipping or comparing compatible carried gear; moving potions and scrolls to the belt; and dropping items from
// a grid or storage. Worn items can be unequipped or compared with compatible carried gear.
const d2::ItemSnapshot* carried(int32_t id) {
    for(const auto& item:worldView.items)if(int32_t(item.id)==id)return &item;
    return nullptr;
}
std::optional<std::pair<int,int>> freeBackpackCell(const d2::ItemSnapshot& item) {
    if(!gameItems)return std::nullopt;
    const auto* info=gameItems->find(item.code);
    if(!info||info->invWidth>10||info->invHeight>4)return std::nullopt;
    for(int y=0;y<=4-int(info->invHeight);++y)for(int x=0;x<=10-int(info->invWidth);++x) {
        bool occupied=false;
        for(const auto& other:worldView.items) {
            if(other.mode!=0||other.page!=0||other.vendor||int32_t(other.id)==int32_t(item.id))continue;
            const auto* otherInfo=gameItems->find(other.code);
            if(!otherInfo)continue;
            if(other.x<x+info->invWidth&&x<other.x+otherInfo->invWidth&&
               other.y<y+info->invHeight&&y<other.y+otherInfo->invHeight) {
                occupied=true;break;
            }
        }
        if(!occupied)return std::pair{x,y};
    }
    return std::nullopt;
}
std::vector<MenuOption> menuFor(int32_t id,ItemPlace place) {
    const auto* item=carried(id);
    if(!item)return {};
    std::vector<MenuOption> options;
    if(place==ItemPlace::Store){options.push_back({"Buy",MenuAction::Buy});return options;}
    if(place==ItemPlace::Grid&&sellToNpc)options.push_back({"Sell",MenuAction::Sell});
    if(place==ItemPlace::Belt||(item->traits&d2::native::itemTraitUsable))options.push_back({"Use",MenuAction::Use});
    if(place==ItemPlace::Grid&&item->page==0) {
        const auto* info=gameItems?gameItems->find(item->code):nullptr;
        if(info&&info->equipment)options.push_back({"Equip",MenuAction::Equip});
        if(info&&!info->type.empty())for(const auto& other:worldView.items) {
            if(other.mode!=1||int32_t(other.id)==id)continue;
            const auto* worn=gameItems?gameItems->find(other.code):nullptr;
            if(worn&&worn->type==info->type) {
                options.push_back({"Compare",MenuAction::Compare,int32_t(other.id)});
                break;
            }
        }
        if(item->traits&d2::native::itemTraitBeltable)options.push_back({"To belt",MenuAction::ToBelt});
    }
    if(place==ItemPlace::Grid||place==ItemPlace::Storage)options.push_back({"Drop",MenuAction::Drop});
    if(place==ItemPlace::Body) {
        if(freeBackpackCell(*item))options.push_back({"Unequip",MenuAction::Unequip});
        const auto* info=gameItems?gameItems->find(item->code):nullptr;
        if(info&&!info->type.empty())for(const auto& other:worldView.items) {
            if(other.mode!=0||other.page!=0||other.vendor||int32_t(other.id)==id)continue;
            const auto* carriedInfo=gameItems?gameItems->find(other.code):nullptr;
            if(carriedInfo&&carriedInfo->type==info->type) {
                options.push_back({"Compare",MenuAction::Compare,int32_t(other.id)});
                break;
            }
        }
    }
    return options;
}
void click(float x,float y,int button) {
    mouse(d2::InputEvent::Type::ButtonDown,int(x),int(y),button);
    mouse(d2::InputEvent::Type::ButtonUp,int(x),int(y),button);
}
void act(const ItemMenu& m,const MenuOption& option) {
    const auto* item=carried(m.item);
    switch(option.action) {
    case MenuAction::Use:click(m.x,m.y,1);break;
    case MenuAction::ToBelt:scriptedShift=true;click(m.x,m.y,0);scriptedShift=false;break;
    case MenuAction::Drop:pendingCommands.push_back(d2::Command::takeItem(m.item));pendingCommands.push_back(d2::Command::dropItem(m.item));break;
    case MenuAction::Buy:click(m.x,m.y,0);break;
    case MenuAction::Sell:if(sellToNpc)pendingCommands.push_back(d2::Command::raw(0x33|(17<<8),sellToNpc,m.item,0,0));break;
    case MenuAction::Equip:
        if(item&&item->mode==0&&item->page==0) {
            itemAction=ItemAction{ItemAction::Kind::Equip,m.item,item->x,item->y,int32_t(item->page)};
            pendingCommands.push_back(d2::Command::takeItem(m.item));
        }
        break;
    case MenuAction::Unequip:
        if(item&&item->mode==1)if(const auto cell=freeBackpackCell(*item)) {
            itemAction=ItemAction{ItemAction::Kind::Unequip,m.item,cell->first,cell->second,0,int32_t(item->bodyLoc)};
            pendingCommands.push_back(d2::Command::takeBody(int32_t(item->bodyLoc)));
        }
        break;
    case MenuAction::Compare:
        if(carried(option.target))comparison=ItemComparison{m.item,option.target};
        break;
    }
}
void quickItemActionTick() {
    if(!itemAction)return;
    const auto action=*itemAction;
    const auto* item=carried(action.item);
    if(!item) {
        itemAction.reset();
        return;
    }
    if(item->mode==4) {
        if(action.kind==ItemAction::Kind::Unequip) {
            if(action.bodyLoc>0) {
                pendingCommands.push_back(d2::Command::placeItem(action.item,action.x,action.y,action.page));
                itemAction->ticks=0;
                itemAction->bodyLoc=-action.bodyLoc;
            } else if(++itemAction->ticks>10) {
                gameMessage="Could not place item in inventory.";
                gameMessageUntil=worldView.tick+100;
                itemAction.reset();
            }
            return;
        }
        int selectedLoc=0,selectedAction=0;
        constexpr int placementPriority[]={1,5,2,7};
        for(const int desired:placementPriority) {
            for(int loc=1;loc<=10;++loc)if(worldView.bodyPlacement[size_t(loc)]==desired) {
                selectedLoc=loc;selectedAction=desired;break;
            }
            if(selectedLoc)break;
        }
        if(selectedLoc) {
            switch(selectedAction) {
            case 1:pendingCommands.push_back(d2::Command::equipItem(action.item,selectedLoc));break;
            case 5:pendingCommands.push_back(d2::Command::swapBody(action.item,selectedLoc));break;
            case 2:pendingCommands.push_back(d2::Command::raw(0x1B|(9<<8),action.item,selectedLoc,0,0));break;
            case 7:pendingCommands.push_back(d2::Command::raw(0x1E|(9<<8),action.item,selectedLoc,0,0));break;
            default:break;
            }
            itemAction.reset();
        } else {
            pendingCommands.push_back(d2::Command::placeItem(action.item,action.x,action.y,action.page));
            gameMessage="This item cannot be equipped.";
            gameMessageUntil=worldView.tick+100;
            itemAction.reset();
        }
        return;
    }
    if(action.kind==ItemAction::Kind::Unequip&&action.bodyLoc<0) {
        if(item->mode==0&&item->page==0){itemAction.reset();return;}
        if(++itemAction->ticks>10) {
            gameMessage="Could not place item in inventory.";
            gameMessageUntil=worldView.tick+100;
            itemAction.reset();
        }
        return;
    }
    if(++itemAction->ticks>10) {
        gameMessage=action.kind==ItemAction::Kind::Equip?"Could not equip item.":"Could not unequip item.";
        gameMessageUntil=worldView.tick+100;
        itemAction.reset();
    }
}
int skillButtonAt(int x,int y) {
    if(!attackUsable())return -1;
    for(int slot=0;slot<skillButtons;++slot) {
        const auto b=skillButton(slot);
        if(std::hypot(float(x)-b.x,float(y)-b.y)<=b.r*1.15f)return slot;
    }
    return -1;
}
// A game controller has three ways of working, by what is on screen:
// - the game menu (Esc): the D-pad or the left stick steps through the lines, A chooses, B or Start goes back;
// - the cursor, whenever the mouse is what the screen wants (the title and character screens, a panel, the skill or
//   gold popup, an item on the cursor, the death screen, an item menu): the left stick moves the game's cursor, A is
//   the left button, X the right one, B is Esc;
// - playing: the left stick walks, A attacks, B picks up, talks or uses, X, Y, RB and RT are the four skill buttons
//   (aimed with the right stick when it is held), LB and LT drink from the belt's first two columns.
// Anywhere in the game Start is Esc, Back the automap and the D-pad the panels (left character, right inventory, up
// skills, down quests).
bool panelUp(){return leftHalfCovered()||rightHalfCovered()||menu.has_value()||comparison.has_value();}
bool padPlaying(){return padUsed&&inGame()&&!panelUp();}
bool cursorMode() {
    if(frontEnd)return true;
    if(!walkGrid||gameMenuPage!=GameMenuPage::Closed)return false;
    return panelUp()||skillMenu||goldPopup.open||cursorItem||pendingIdentifyItem||worldView.vitals.life<=0;
}
void key(unsigned k) {
    d2::InputEvent e;e.type=d2::InputEvent::Type::Key;e.key=k;
    handleInput(e);
}
float cursorX=-1,cursorY=-1; // the cursor the left stick moves, in fractions of pixels
void padCast(int slot) {
    if(!hotkeySkill(slot)){chooseSkillFor(slot);return;} // an empty button asks for its skill (the popup: the cursor)
    // The right stick held: at the point that way (as a dragged skill button aims); otherwise at the best target.
    const float x=padAxis[pad::RightX],y=padAxis[pad::RightY],m=std::hypot(x,y);
    if(m<.5f){castSkill(slot,std::nullopt);return;}
    int w=0,h=0;gameWindow->clientSize(w,h);
    const float reachPixels=180*hudScale();
    castSkill(slot,std::array<float,2>{std::clamp(float(w)*.5f+x/m*reachPixels,0.f,float(w-1)),std::clamp(float(h)*.5f+y/m*reachPixels,0.f,float(h-1))});
}
void padButton(int button,bool down) {
    using T=d2::InputEvent::Type;
    if(comparison) {
        if(down)comparison.reset();
        return;
    }
    if(gameMenuPage!=GameMenuPage::Closed) {
        if(!down)return;
        if(button==pad::Up)key(d2::key::Up);
        else if(button==pad::Down)key(d2::key::Down);
        else if(button==pad::A)key(d2::key::Enter);
        else if(button==pad::B||button==pad::Start)key(d2::key::Escape);
        return;
    }
    if(button==pad::Start){if(down)key(d2::key::Escape);return;}
    const bool inWorld=walkGrid&&!frontEnd;
    if(down&&inWorld) {
        // The panels, the same buttons in every mode of the game.
        constexpr std::pair<int,int> panels[]={{pad::Left,0},{pad::Right,1},{pad::Up,2},{pad::Down,6},{pad::Back,4}};
        for(const auto& [padKey,panel]:panels)if(button==padKey){miniPanelButton(panel);return;}
    }
    if(cursorMode()) {
        if(mouseX<0){int w=0,h=0;gameWindow->clientSize(w,h);mouse(T::MouseMove,w/2,h/2,0);}
        if(button==pad::A)mouse(down?T::ButtonDown:T::ButtonUp,mouseX,mouseY,0);
        else if(button==pad::X)mouse(down?T::ButtonDown:T::ButtonUp,mouseX,mouseY,1);
        else if(button==pad::B&&down)key(d2::key::Escape);
        return;
    }
    if(!down){if(button==pad::A)padAttack=false;return;}
    if(!padPlaying())return;
    switch(button) {
    case pad::A:padAttack=true;attack();break;
    case pad::B:use();break;
    case pad::X:padCast(0);break;
    case pad::Y:padCast(1);break;
    case pad::RB:padCast(2);break;
    case padRT:padCast(3);break;
    case pad::LB:if(const int32_t item=beltItem(0))pendingCommands.push_back(d2::Command::useBelt(item));break;
    case padLT:if(const int32_t item=beltItem(1))pendingCommands.push_back(d2::Command::useBelt(item));break;
    default:break;
    }
}
void padEvent(const d2::InputEvent& e) {
    padUsed=true;
    if(e.type==d2::InputEvent::Type::PadButton){padButton(e.button,e.pressed);return;}
    if(e.button<0||e.button>=pad::Axes)return;
    const float before=padAxis[e.button];
    padAxis[e.button]=e.value;
    // The triggers work as buttons, pressed past half way.
    if(e.button==pad::LT||e.button==pad::RT)if((before>.5f)!=(e.value>.5f))padButton(e.button==pad::LT?padLT:padRT,e.value>.5f);
    // In the game menu the left stick steps through the lines, a line per push.
    if(gameMenuPage!=GameMenuPage::Closed&&e.button==pad::LeftY) {
        if(before>-.6f&&e.value<=-.6f)key(d2::key::Up);
        else if(before<.6f&&e.value>=.6f)key(d2::key::Down);
    }
}
}
Settings& settings(){return chosen;}
// The file: one "name=value" line per setting (touch, lefthanded, size, opacity, autotarget).
void loadSettings(const std::filesystem::path& file) {
    settingsFile=file;
    std::ifstream in(file);
    int storedSize=-1;
    bool hasControlSizeVersion=false;
    for(std::string line;std::getline(in,line);) {
        const auto equals=line.find('=');
        if(equals==std::string::npos)continue;
        const std::string name=line.substr(0,equals);
        const int value=std::atoi(line.c_str()+equals+1);
        if(name=="touch")on=value!=0;
        else if(name=="lefthanded")chosen.leftHanded=value!=0;
        else if(name=="size")storedSize=value;
        else if(name=="controlsizeversion")hasControlSizeVersion=value>=2;
        else if(name=="opacity")chosen.opacity=std::clamp(value,0,2);
        else if(name=="autotarget")chosen.autoTarget=value!=0;
        else if(name=="vibration")chosen.vibration=value!=0;
        else if(name=="alwaysrun")chosen.alwaysRun=value!=0;
        else if(name=="music")chosen.music=value!=0;
        else if(name=="effects")chosen.effects=value!=0;
        else if(name=="musicvolume")chosen.musicVolume=std::clamp(value,0,10);
        else if(name=="brightness")chosen.brightness=std::clamp(value,0,10);
        else if(name=="effectsvolume")chosen.effectsVolume=std::clamp(value,0,10);
        else if(name=="controlhint")chosen.controlHintSeen=value!=0;
        else if(name=="viewzoom")chosen.viewZoom=std::clamp(value,0,5);
        else if(name=="cheats")chosen.cheats=value!=0;
    }
    if(storedSize>=0) {
        // Older profiles used 0..2 for small/normal/large. Keep those choices when upgrading the range.
        chosen.size=hasControlSizeVersion?std::clamp(storedSize,0,4):std::clamp(storedSize,0,2)+1;
    }
}
void saveSettings() {
    if(settingsFile.empty())return;
    std::ofstream out(settingsFile,std::ios::trunc);
    out<<"touch="<<(on?1:0)<<"\nlefthanded="<<(chosen.leftHanded?1:0)<<"\nsize="<<chosen.size<<"\ncontrolsizeversion=2\nopacity="<<chosen.opacity
       <<"\nautotarget="<<(chosen.autoTarget?1:0)<<"\nvibration="<<(chosen.vibration?1:0)<<"\nalwaysrun="<<(chosen.alwaysRun?1:0)
       <<"\nmusic="<<(chosen.music?1:0)<<"\neffects="<<(chosen.effects?1:0)<<"\nmusicvolume="<<chosen.musicVolume<<"\neffectsvolume="<<chosen.effectsVolume<<"\nbrightness="<<chosen.brightness
       <<"\ncontrolhint="<<(chosen.controlHintSeen?1:0)<<"\nviewzoom="<<chosen.viewZoom<<"\ncheats="<<(chosen.cheats?1:0)<<"\n";
}
float viewZoomScale() {
    static constexpr float steps[]={1.f,1.25f,1.5f,2.f,2.5f,3.f};
    return steps[std::clamp(chosen.viewZoom,0,5)];
}
bool enabled(){return on;}
void setEnabled(bool enable){if(!enable)reset();on=enable;}
uint32_t target() {
    if(!(on||padUsed)||!aimed)return 0;
    for(const auto& e:worldView.entities)if(e.id==1000+d2::EntityId(aimed)&&attackable(e))return aimed;
    aimed=0;
    return 0;
}
bool assign(int skill) {
    if(assigningSlot<0)return false;
    skillHotkeys[size_t(assigningSlot)]=skill;
    assigningSlot=-1;
    return true;
}
bool dismissComparison() {
    if(!comparison)return false;
    comparison.reset();
    return true;
}
void reset() {
    release();
    attackFinger.reset();skillFinger.reset();aimed=0;assigningSlot=-1;itemPress.reset();menu.reset();comparison.reset();
    if(rightFinger){mouse(d2::InputEvent::Type::ButtonUp,mouseX,mouseY,1);rightFinger.reset();}
    if(leftFinger){mouse(d2::InputEvent::Type::ButtonUp,mouseX,mouseY,0);leftFinger.reset();}
}
void update() {
    static auto last=std::chrono::steady_clock::now();
    const auto now=std::chrono::steady_clock::now();
    const float seconds=std::clamp(std::chrono::duration<float>(now-last).count(),0.f,.1f);
    last=now;
    if(!gameWindow||!cursorMode())return;
    // The left stick moves the cursor: slowly near the middle (for aiming at an inventory cell), fast at the edge.
    const float x=padAxis[pad::LeftX],y=padAxis[pad::LeftY],m=std::hypot(x,y);
    if(m<.2f)return;
    int w=0,h=0;gameWindow->clientSize(w,h);
    if(cursorX<0||int(cursorX)!=mouseX||int(cursorY)!=mouseY){cursorX=mouseX<0?float(w)*.5f:float(mouseX);cursorY=mouseY<0?float(h)*.5f:float(mouseY);}
    const float push=std::min(1.f,(m-.2f)/.65f),speed=900*hudScale()*push*push;
    cursorX=std::clamp(cursorX+x/m*speed*seconds,0.f,float(w-1));cursorY=std::clamp(cursorY+y/m*speed*seconds,0.f,float(h-1));
    mouse(d2::InputEvent::Type::MouseMove,int(cursorX),int(cursorY),0);
}
void handle(const d2::InputEvent& e) {
    using T=d2::InputEvent::Type;
    if(e.type==T::PadButton||e.type==T::PadAxis){padEvent(e);return;}
    if(!chosen.controlHintSeen&&e.type==T::TouchDown&&on&&!frontEnd) {
        chosen.controlHintSeen=true;
        saveSettings();
    }
    if(comparison&&e.type==T::TouchDown){comparison.reset();return;}
    // An item menu takes the next touch: on one of its lines that is done, anywhere else it just closes.
    if(menu&&e.type==T::TouchDown) {
        const ItemMenu open=*menu;menu.reset();
        for(size_t i=0;i<open.rows.size()&&i<open.options.size();++i) {
            const auto& r=open.rows[i];
            if(float(e.x)>=r[0]&&float(e.x)<=r[0]+r[2]&&float(e.y)>=r[1]&&float(e.y)<=r[1]+r[3]){buttonSound();act(open,open.options[i]);break;}
        }
        return;
    }
    if(itemPress&&e.finger==itemPress->finger) {
        auto& p=*itemPress;
        if(e.type==T::TouchMove) {
            // Moved off: the finger is the mouse after all (pressed where it landed).
            if(std::hypot(float(e.x)-p.x,float(e.y)-p.y)>12*hudScale()) {
                const auto started=p;itemPress.reset();
                if(started.place!=ItemPlace::Store){leftFinger=e.finger;mouse(T::ButtonDown,int(started.x),int(started.y),0);}
                mouse(T::MouseMove,e.x,e.y,0);
            }
            return;
        }
        if(e.type==T::TouchUp) {
            const auto finished=p;itemPress.reset();
            if(finished.place!=ItemPlace::Store)click(finished.x,finished.y,0); // a tap: a click (a store's goods: only shown)
        }
        return;
    }
    if(e.type==T::TouchDown)if(const int index=panelButtonAt(e.x,e.y);index>=0){
        buttonSound();
        miniPanelButton(index<standardPanelButtonCount?panelButtons[index]:8);
        return;
    }
    if(e.type==T::TouchDown&&!leftFinger&&!rightFinger&&!cursorItem&&!pendingIdentifyItem)if(const auto found=itemAt(e.x,e.y)) {
        itemPress=ItemPress{e.finger,float(e.x),float(e.y),found->first,found->second};
        mouse(T::MouseMove,e.x,e.y,0); // the description shows while the finger is down
        return;
    }
    if(stick&&e.finger==stick->finger) {
        if(e.type==T::TouchUp){release();return;}
        stick->x=float(e.x);stick->y=float(e.y);
        // A thumb that slides past the ring drags the ring along, so it never has to come back to where it started.
        const float dx=stick->x-stick->cx,dy=stick->y-stick->cy,d=std::hypot(dx,dy),r=radius();
        if(d>r*1.5f){stick->cx=stick->x-dx/d*r*1.5f;stick->cy=stick->y-dy/d*r*1.5f;}
        return;
    }
    if(attackFinger&&e.finger==*attackFinger) {
        if(e.type==T::TouchUp)attackFinger.reset(); // the target stays lit until it dies or another is chosen
        return;
    }
    if(skillFinger&&e.finger==skillFinger->finger) {
        auto& f=*skillFinger;
        f.x=float(e.x);f.y=float(e.y);
        const auto b=skillButton(f.slot);
        const bool onButton=std::hypot(f.x-b.x,f.y-b.y)<=b.r*1.3f;
        if(!onButton)f.dragged=true;
        if(e.type!=T::TouchUp)return;
        // Lifted: a tap casts at the best target, a drag at where it aims; dragged back onto the button it is called off.
        const auto finished=f;skillFinger.reset();
        if(finished.choosing||!attackUsable())return;
        if(!finished.dragged)castSkill(finished.slot,std::nullopt);
        else if(!onButton)castSkill(finished.slot,aimPoint(finished));
        return;
    }
    if(e.type==T::TouchDown&&attackUsable()) {
        // The potion buttons and the use button act at once; their finger does nothing more.
        for(int column=0;column<potionButtons;++column)if(onCircle(potionButton(column),e.x,e.y)) {
            if(const int32_t item=beltItem(column))pendingCommands.push_back(d2::Command::useBelt(item));
            return;
        }
        if(nearby.kind!=Nearby::Kind::None&&onCircle(useButton(),e.x,e.y)){use();return;}
    }
    if(e.type==T::TouchDown&&!skillFinger)if(const int slot=skillButtonAt(e.x,e.y);slot>=0) {
        skillFinger=SkillFinger{e.finger,slot,float(e.x),float(e.y)};
        // A button with no skill yet asks for one straight away.
        if(!hotkeySkill(slot)){skillFinger->choosing=true;chooseSkillFor(slot);}
        return;
    }
    if(e.type==T::TouchDown&&!attackFinger&&onAttackButton(e.x,e.y)) {
        attackFinger=e.finger;
        attack();
        return;
    }
    if(e.type==T::TouchDown&&!stick&&!leftFinger&&joystickMayStart(e.x,e.y)) {
        stick=Stick{e.finger,float(e.x),float(e.y),float(e.x),float(e.y)};
        ticksSinceOrder=0;
        return;
    }
    // The mouse.
    if(leftFinger&&e.finger==*leftFinger) {
        if(e.type==T::TouchMove)mouse(T::MouseMove,e.x,e.y,0);
        else if(e.type==T::TouchUp){mouse(T::ButtonUp,e.x,e.y,0);leftFinger.reset();}
        return;
    }
    if(rightFinger&&e.finger==*rightFinger) {
        if(e.type==T::TouchMove)mouse(T::MouseMove,e.x,e.y,0);
        else if(e.type==T::TouchUp){mouse(T::ButtonUp,e.x,e.y,1);rightFinger.reset();}
        return;
    }
    if(e.type!=T::TouchDown)return;
    // The first finger off the controls is the left button (tapping an enemy attacks that one); a second one is the right
    // button where it touches (casts the right skill).
    if(!leftFinger&&!rightFinger){leftFinger=e.finger;mouse(T::ButtonDown,e.x,e.y,0);return;}
    if(!rightFinger){rightFinger=e.finger;mouse(T::ButtonDown,e.x,e.y,1);}
}
void highlight(uint32_t& unit,uint32_t& object) {
    if(!(on||padUsed))return;
    if(const uint32_t aimedAt=target())unit=aimedAt;
    else if(nearby.kind==Nearby::Kind::Person)unit=nearby.id;
    if(nearby.kind==Nearby::Kind::Object)object=nearby.id;
}
void tick() {
    quickItemActionTick();
    nearby=attackUsable()||padPlaying()?findNearby():Nearby{};
    // Vibration (the options): a hit that takes at least 3% of life, harder the more it takes; a level gained.
    if(chosen.vibration&&gameWindow&&worldView.vitals.maxLife>0) {
        static int32_t lastLife=0,lastMax=0,lastLevel=0;
        const int32_t life=worldView.vitals.life,maxLife=worldView.vitals.maxLife,level=worldView.stats[12];
        if(lastLife>0&&life<lastLife&&maxLife==lastMax) {
            const float lost=float(lastLife-life)/float(maxLife);
            if(lost>=.03f)gameWindow->vibrate(std::min(1.f,.3f+lost*3.f),std::clamp(int(80+lost*600),80,300));
        }
        if(lastLevel>0&&level>lastLevel)gameWindow->vibrate(.8f,400);
        lastLife=life;lastMax=maxLife;lastLevel=level;
    }
    if(itemPress&&++itemPress->ticks>=10) {
        // Held still for 0.4 s: the menu (the description stays shown, the cursor being over the item).
        const auto held=*itemPress;itemPress.reset();
        auto options=menuFor(held.item,held.place);
        if(!options.empty())menu=ItemMenu{held.x,held.y,held.item,std::move(options),{}};
    }
    if(assigningSlot>=0&&!skillMenu)assigningSlot=-1; // the popup was closed without a choice
    if(skillFinger&&!skillFinger->dragged&&!skillFinger->choosing&&++skillFinger->heldTicks>=15) {
        // Held still on the button for 0.6 s: choose its skill.
        skillFinger->choosing=true;
        chooseSkillFor(skillFinger->slot);
    }
    if(attackFinger||padAttack) {
        // Held: the game keeps attacking an enemy until it falls (NativeGame repeats the order), so only a new target
        // needs an order: the next enemy once this one is dead or out of reach. Ordering the same one again would start
        // the swing over and it would never land.
        if(attackFinger?!attackUsable():!padPlaying()){attackFinger.reset();padAttack=false;}
        else if(++ticksSinceAttack>=6) {
            ticksSinceAttack=0;
            const uint32_t previous=target(),next=chooseTarget();
            if(next&&next!=previous){aimed=next;pendingCommands.push_back(d2::Command::attack(int32_t(next)));}
        }
    }
    // Walking: the touch joystick, or a controller's left stick while no panel is up.
    if(!(stick?joystickUsable():padPlaying())) {
        if(stick||walking||runningByStick)release(); // a panel or menu opened, or the hero died
        return;
    }
    const auto steer=steering();
    ++ticksSinceOrder;
    if((attackFinger||padAttack)&&aimed) {
        // Attacking: the joystick only aims (walking orders would call the attack off); it walks again when the button
        // is let go.
        walking=false;
        return;
    }
    if(!steer||steer->strength<.25f) {
        // Back in the middle: stand still.
        if(walking){pendingCommands.push_back(d2::Command::stop());walking=false;}
        setRunning(false);
        return;
    }
    setRunning(steer->strength>.8f);
    const float angle=steer->angle;
    float turned=std::abs(angle-orderedAngle);if(turned>pi)turned=2*pi-turned;
    // A new order every three ticks (120 ms), or at once when the thumb turns: the hero never reaches the point ahead.
    if(walking&&ticksSinceOrder<3&&turned<.35f)return;
    if(const auto destination=walkTarget(angle)) {
        pendingCommands.push_back(d2::Command::moveTo(*destination));
        walking=true;followPlayer=true;
    }
    ticksSinceOrder=0;orderedAngle=angle;
}
void draw(std::vector<d2::HudSprite>& hud,int width,int height,const Art& art) {
    if(!on||!walkGrid||frontEnd||gameMenuPage!=GameMenuPage::Closed)return;
    // Round shapes for the rings, buttons and the knob (pixels below half alpha are not drawn, so the edges are hard; the
    // transparency comes from the Mix blend).
    auto disc=[](float inner,float outer,std::array<uint8_t,3> color) {
        d2::Image image;image.width=image.height=128;image.rgba.assign(128*128*4,0);
        for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
            const float d=std::hypot(float(x)-63.5f,float(y)-63.5f)/63.5f;
            if(d>outer||d<inner)continue;
            auto* p=&image.rgba[size_t(y*128+x)*4];p[0]=color[0];p[1]=color[1];p[2]=color[2];p[3]=255;
        }
        return image;
    };
    // Dark glass, a gold rim (the colour of the game's gold text) and a pale knob.
    static const d2::Image fill=disc(0,1,{12,10,8}),ring=disc(.9f,1,{196,168,104}),knob=disc(0,1,{216,204,180});
    auto put=[&](const d2::Image& image,float x,float y,float size,float amount) {
        d2::HudSprite s;s.image=&image;s.x=x-size;s.y=y-size;s.w=size*2;s.h=size*2;s.blend=d2::HudSprite::Blend::Mix;s.amount=std::min(1.f,amount*opacity());
        hud.push_back(s);
    };
    const float s=hudScale();
    // The joystick. At rest the ring waits faintly where the thumb usually goes.
    if(joystickUsable()||stick) {
        const auto safe=safeArea(width,height);
        const float r=radius();
        const float restX=safe.left+(safe.right-safe.left)*(chosen.leftHanded?.86f:.14f),restY=safe.bottom-170*hudScale();
        const float cx=stick?stick->cx:restX,cy=stick?stick->cy:restY;
        const float kx=stick?stick->x:cx,ky=stick?stick->y:cy;
        float ox=kx-cx,oy=ky-cy;const float d=std::hypot(ox,oy);if(d>r){ox*=r/d;oy*=r/d;}
        const float active=stick?1.f:.5f;
        put(fill,cx,cy,r,.35f*active);
        put(ring,cx,cy,r,.6f*active);
        put(knob,cx+ox,cy+oy,r*.42f,.6f*active);
        if(!chosen.controlHintSeen&&art.text) {
            const auto* picture=art.text("MOVE",{199,179,119});
            if(picture&&picture->width&&picture->height) {
                const float tw=float(picture->width)*s,th=float(picture->height)*s;
                d2::HudSprite sprite;sprite.image=picture;sprite.x=cx-tw*.5f;sprite.y=cy-r-26*s;sprite.w=tw;sprite.h=th;
                hud.push_back(sprite);
            }
        }
    }
    // A round button with a picture in it (fitted, its proportions kept); brighter while pressed.
    auto button=[&](Circle b,const d2::Image* icon,bool held,float dim) {
        const float pressed=(held?1.f:.7f)*dim;
        put(fill,b.x,b.y,b.r,.45f*pressed);
        if(icon&&icon->width&&icon->height) {
            const float side=b.r*1.25f,fit=side/float(std::max(icon->width,icon->height));
            const float w=float(icon->width)*fit,h=float(icon->height)*fit;
            d2::HudSprite s;s.image=icon;s.x=b.x-w*.5f;s.y=b.y-h*.5f;s.w=w;s.h=h;
            s.blend=d2::HudSprite::Blend::Mix;s.amount=std::min(1.f,pressed*opacity());
            hud.push_back(s);
        }
        put(ring,b.x,b.y,b.r,.8f*pressed);
    };
    auto skillPicture=[&](int skill){return art.skill?art.skill(skill):nullptr;};
    auto itemPicture=[&](int32_t item){return item&&art.item?art.item(item):nullptr;};
    if(attackUsable()) {
        button(attackButton(),skillPicture(worldView.leftSkill),attackFinger.has_value(),1);
        // The skill buttons: dimmed without the mana for the skill; an empty one is a faint ring (tap it to choose).
        for(int slot=0;slot<skillButtons;++slot) {
            const int skill=hotkeySkill(slot);
            const bool held=skillFinger&&skillFinger->slot==slot;
            button(skillButton(slot),skill?skillPicture(skill):nullptr,held,!skill?.5f:affordable(skill)?1.f:.4f);
        }
        // The potion buttons: what is in the belt's first two columns (faint when a column is empty).
        for(int column=0;column<potionButtons;++column) {
            const int32_t item=beltItem(column);
            button(potionButton(column),itemPicture(item),false,item?1.f:.5f);
        }
        // The use button, only when something is near: the item it would pick up, or the hand.
        if(nearby.kind!=Nearby::Kind::None)
            button(useButton(),nearby.kind==Nearby::Kind::Item?itemPicture(int32_t(nearby.id)):art.hand,false,1);
        if(!chosen.controlHintSeen&&art.text) {
            auto caption=[&](float x,float y,const char* text) {
                const auto* picture=art.text(text,{199,179,119});
                if(!picture||!picture->width||!picture->height)return;
                const float tw=float(picture->width)*s,th=float(picture->height)*s;
                d2::HudSprite sprite;sprite.image=picture;sprite.x=x-tw*.5f;sprite.y=y;sprite.w=tw;sprite.h=th;
                hud.push_back(sprite);
            };
            const auto safe=safeArea(width,height);
            caption((safe.left+safe.right)*.5f,safe.top+8*s,"TAP TO HIDE LABELS");
            const auto attack=attackButton();
            caption(attack.x,attack.y-attack.r-26*s,"ATTACK");
            const auto skill=skillButton(skillButtons-1);
            caption(skill.x,skill.y-skill.r-22*s,"SKILLS");
            const auto potion=potionButton(0);
            caption((potionButton(0).x+potionButton(1).x)*.5f,potion.y+potion.r+6*s,"POTIONS");
            if(nearby.kind!=Nearby::Kind::None) {
                const auto use=useButton();
                caption(use.x,use.y-use.r-22*s,"USE");
            }
        }
        // Aiming: a ring on the ground where the skill will go.
        if(skillFinger&&skillFinger->dragged&&!skillFinger->choosing) {
            const auto b=skillButton(skillFinger->slot);
            const bool cancel=std::hypot(skillFinger->x-b.x,skillFinger->y-b.y)<=b.r*1.3f;
            if(!cancel) {
                const auto at=aimPoint(*skillFinger);
                put(ring,at[0],at[1],24*hudScale(),.9f);
                put(knob,at[0],at[1],6*hudScale(),.9f);
            }
        }
    }
    auto plain=[&](float x,float y,float w,float h,std::array<uint8_t,3> color,float amount) {
        d2::HudSprite sprite;sprite.color=color;sprite.x=x;sprite.y=y;sprite.w=w;sprite.h=h;sprite.blend=d2::HudSprite::Blend::Mix;sprite.amount=amount;
        hud.push_back(sprite);
    };
    // The panel buttons: the mini panel's own pictures, on a dark backing.
    if(panelButtonsShown())for(int i=0;i<panelButtonCount();++i) {
        const auto r=panelButtonRect(i);
        if(i==standardPanelButtonCount) {
            plain(r[0]-2*s,r[1]-2*s,r[2]+4*s,r[3]+4*s,{146,121,73},.85f);
            plain(r[0],r[1],r[2],r[3],{12,10,8},.6f);
            if(art.text)if(const auto* picture=art.text("IP",{199,179,119});picture&&picture->width&&picture->height) {
                const float textScale=std::min({1.4f*s,(r[2]-6*s)/float(picture->width),(r[3]-4*s)/float(picture->height)});
                d2::HudSprite sprite;sprite.image=picture;sprite.x=r[0]+(r[2]-float(picture->width)*textScale)*.5f;
                sprite.y=r[1]+(r[3]-float(picture->height)*textScale)*.5f;sprite.w=float(picture->width)*textScale;sprite.h=float(picture->height)*textScale;
                hud.push_back(sprite);
            }
        } else if(art.panelButton)if(const auto* picture=art.panelButton(panelButtons[i],false)) {
            plain(r[0]-2*s,r[1]-2*s,r[2]+4*s,r[3]+4*s,{12,10,8},.6f);
            d2::HudSprite sprite;sprite.image=picture;sprite.x=r[0];sprite.y=r[1];sprite.w=r[2];sprite.h=r[3];hud.push_back(sprite);
        }
    }
    // The item menu: a line per choice beside where the finger held the item (on the left of it, or the right when
    // there is no room), in the game's font on a dark backing with a gold edge.
    if(menu&&art.text) {
        const float rowHeight=28*s,pad=14*s;
        float textWidth=0;
        for(const auto& option:menu->options)if(const auto* t=art.text(option.label,{199,179,119}))textWidth=std::max(textWidth,float(t->width)*s);
        const float w=textWidth+pad*2,h=rowHeight*float(menu->options.size());
        const auto safe=safeArea(width,height);
        float x=menu->x-w-16*s;if(x<safe.left)x=menu->x+16*s;
        x=std::clamp(x,safe.left,std::max(safe.left,safe.right-w));
        const float y=std::clamp(menu->y-h*.5f,safe.top,std::max(safe.top,safe.bottom-h));
        plain(x-2*s,y-2*s,w+4*s,h+4*s,{196,168,104},.9f);
        plain(x,y,w,h,{12,10,8},.92f);
        menu->rows.clear();
        for(size_t i=0;i<menu->options.size();++i) {
            const float ry=y+rowHeight*float(i);
            menu->rows.push_back({x,ry,w,rowHeight});
            if(i)plain(x+pad*.5f,ry,w-pad,std::max(1.f,s),{96,82,52},.9f);
            if(const auto* t=art.text(menu->options[i].label,{199,179,119})) {
                d2::HudSprite sprite;sprite.image=t;sprite.w=float(t->width)*s;sprite.h=float(t->height)*s;
                sprite.x=x+pad;sprite.y=ry+(rowHeight-sprite.h)*.5f;hud.push_back(sprite);
            }
        }
    }
    if(comparison&&art.text) {
        const auto* first=carried(comparison->first);
        const auto* second=carried(comparison->second);
        const auto* firstInfo=first&&gameItems?gameItems->find(first->code):nullptr;
        const auto* secondInfo=second&&gameItems?gameItems->find(second->code):nullptr;
        if(first&&second&&firstInfo&&secondInfo) {
            struct ComparisonRow {std::string label,left,right,delta;bool favorableIncrease=true;};
            std::vector<ComparisonRow> rows;
            auto number=[&](int value){return value>0?std::to_string(value):std::string("-");};
            auto addRow=[&](const char* label,int left,int right,bool favorableIncrease=true) {
                if(left==0&&right==0)return;
                const int delta=right-left;
                rows.push_back({label,number(left),number(right),delta>0?"+"+std::to_string(delta):std::to_string(delta),favorableIncrease});
            };
            const int leftMinDamage=first->minDamage>0?first->minDamage:firstInfo->minDamage;
            const int leftMaxDamage=first->maxDamage>0?first->maxDamage:firstInfo->maxDamage;
            const int rightMinDamage=second->minDamage>0?second->minDamage:secondInfo->minDamage;
            const int rightMaxDamage=second->maxDamage>0?second->maxDamage:secondInfo->maxDamage;
            if(leftMinDamage||leftMaxDamage||rightMinDamage||rightMaxDamage) {
                rows.push_back({"Damage",number(leftMinDamage)+"-"+number(leftMaxDamage),number(rightMinDamage)+"-"+number(rightMaxDamage),"",true});
            }
            addRow("Defense",first->armorClass>0?first->armorClass:firstInfo->maxArmor,
                   second->armorClass>0?second->armorClass:secondInfo->maxArmor);
            addRow("Durability",first->maxDurability>0?first->durability:firstInfo->durability,
                   second->maxDurability>0?second->durability:secondInfo->durability);
            addRow("Req. level",firstInfo->reqLevel,secondInfo->reqLevel,false);
            addRow("Req. strength",firstInfo->reqStr,secondInfo->reqStr,false);
            addRow("Req. dexterity",firstInfo->reqDex,secondInfo->reqDex,false);
            const auto safe=safeArea(width,height);
            const float pad=12*s,rowHeight=23*s;
            const float panelWidth=std::min(500*s,safe.right-safe.left-16*s);
            const float panelHeight=(62+float(rows.size())*23+28)*s;
            const float x=safe.left+(safe.right-safe.left-panelWidth)*.5f;
            const float y=safe.top+(safe.bottom-safe.top-panelHeight)*.5f;
            plain(x-2*s,y-2*s,panelWidth+4*s,panelHeight+4*s,{196,168,104},.95f);
            plain(x,y,panelWidth,panelHeight,{12,10,8},.96f);
            auto text=[&](const std::string& value,float left,float top,float maxWidth,std::array<uint8_t,3> color) {
                if(value.empty())return;
                const auto* image=art.text(value,color);
                if(!image||!image->width||!image->height)return;
                const float fit=std::min({s,maxWidth/float(image->width),rowHeight/float(image->height)});
                d2::HudSprite sprite;sprite.image=image;sprite.x=left;sprite.y=top+(rowHeight-float(image->height)*fit)*.5f;
                sprite.w=float(image->width)*fit;sprite.h=float(image->height)*fit;sprite.blend=d2::HudSprite::Blend::Mix;
                sprite.amount=1;hud.push_back(sprite);
            };
            const float labelWidth=92*s,valueWidth=(panelWidth-2*pad-labelWidth-12*s)*.5f;
            const float firstX=x+pad+labelWidth,secondX=firstX+valueWidth+6*s;
            text("COMPARE ITEMS",x+pad,y+5*s,panelWidth-2*pad,{199,179,119});
            const auto itemName=[&](const d2::ItemSnapshot& item) {
                return gameItems->displayName(item.code,item.quality,item.prefix,item.suffix,item.rarePrefix,item.rareSuffix,item.fileIndex);
            };
            text(itemName(*first),firstX,y+29*s,valueWidth,{235,215,140});
            text(itemName(*second),secondX,y+29*s,valueWidth,{235,215,140});
            for(size_t i=0;i<rows.size();++i) {
                const float rowY=y+(57+float(i)*23)*s;
                text(rows[i].label,x+pad,rowY,labelWidth,{190,185,165});
                text(rows[i].left,firstX,rowY,valueWidth,{225,225,225});
                text(rows[i].right,secondX,rowY,valueWidth-42*s,{225,225,225});
                if(!rows[i].delta.empty()) {
                    const int leftValue=rows[i].left.empty()?0:std::atoi(rows[i].left.c_str());
                    const int rightValue=rows[i].right.empty()?0:std::atoi(rows[i].right.c_str());
                    const bool improved=rows[i].favorableIncrease?rightValue>=leftValue:rightValue<=leftValue;
                    text(rows[i].delta,secondX+valueWidth-36*s,rowY,36*s,improved?std::array<uint8_t,3>{110,220,120}:std::array<uint8_t,3>{255,100,90});
                }
            }
            text("Tap anywhere to close",x+pad,y+panelHeight-22*s,panelWidth-2*pad,{160,150,130});
        } else comparison.reset();
    }
}
}
