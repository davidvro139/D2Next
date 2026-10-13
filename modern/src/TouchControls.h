#pragma once
// The phone controls (see ../TOUCH-CONTROLS.md). Every finger on the screen comes here first. A finger that lands on the
// game in the left part of the screen becomes the joystick, which walks the hero in the thumb's direction (runs when
// pushed to the edge). A finger on the attack button (bottom right) attacks the best enemy near the hero with the left
// skill, as clicking it would, and keeps attacking while held. The four skill buttons around it hold the F1-F4 hotkey
// skills: a tap casts the skill on the best target, a drag aims it, a long press chooses the button's skill. Two potion
// buttons drink from the belt's first two columns, and a use button appears when something is near to pick up, talk
// to or use. A row of buttons at the top right opens the panels (as the mini panel's buttons do). In the panels, a tap
// on an item clicks it, and a long press shows its description with contextual actions (use, equip, compare, to the belt,
// drop, buy). Every other finger works the mouse: the first one is the
// left button, a second one at the same time is the right button. So the menus, the panels and tapping in the game
// behave as clicks do.
#include "Hud.h"
#include "Platform.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace touch {
// The joystick (and later the buttons): on by default on phones, --touch or D2MODERN_TOUCH=1 elsewhere. Off, fingers only
// work the mouse.
bool enabled();
void setEnabled(bool on);
// The player's choices (the Esc menu's options), kept in a file in the profile folder (loadSettings/saveSettings).
struct Settings {
    bool leftHanded=false; // the joystick on the right, the buttons on the left
    int size=2;            // the buttons and the joystick: 0 very small, 1 small, 2 normal, 3 large, 4 very large
    int opacity=1;         // how solid they are drawn: 0 faint, 1 normal, 2 solid
    bool autoTarget=true;  // off: the attack and skill buttons only go for the enemy the joystick points at (or the one being fought)
    bool vibration=true;   // a shake on hits and level-ups (the controller's rumble, or the phone's vibrator)
    // Run on (the run/walk toggle), for every character and every game: 1.10f D2Client keeps it in the registry (Storm
    // SRegLoadValue/SRegSaveValue, "Diablo II\Always Run", 0x6FB08D90/0x6FB08DC0), loaded as a game starts.
    bool alwaysRun=false;
    bool music=true;
    bool effects=true;
    int musicVolume=10;    // 0..10
    int effectsVolume=10;  // 0..10
    int brightness=5;      // 0..10, 5 as drawn (the game's gamma setting; Renderer::present scales the picture 0.6x-1.4x)
    bool controlHintSeen=false; // first-run labels over the touch controls
#ifdef __ANDROID__
    int viewZoom=2;        // 0=1x .. 5=3x; 1.5x default on phones so the world is readable
#else
    int viewZoom=0;        // 1x, matching the desktop game
#endif
    bool cheats=false;     // Esc menu Cheats: gold, items, skills, levels, and other save edits
};
// World scale from VIEW ZOOM: 1, 1.25, 1.5, 2, 2.5 or 3.
float viewZoomScale();
Settings& settings();
// Reads the settings and whether the controls are on (when the file says); missing or unreadable, the defaults stay.
void loadSettings(const std::filesystem::path& file);
void saveSettings(); // to the file last loaded
// A TouchDown, TouchMove or TouchUp event, or a game controller's PadButton or PadAxis.
void handle(const d2::InputEvent& event);
// Once per frame (menus too): a game controller's left stick moving the cursor in the panels and menus.
void update();
// Once per game tick, before the tick: the joystick's walking orders.
void tick();
// The game's pictures the buttons show (each null when there is none: the button is drawn plain).
struct Art {
    std::function<const d2::Image*(int)> skill;    // a skill's icon, by skill id
    std::function<const d2::Image*(int32_t)> item; // an item's inventory picture, by native item number
    const d2::Image* hand=nullptr;                  // the game's hand cursor (the use button)
    std::function<const d2::Image*(int,bool)> panelButton; // a mini panel button's picture (0-7), pressed or not
    std::function<const d2::Image*(const std::string&,std::array<uint8_t,3>)> text; // a line of text in the game's font
};
// The controls, drawn over the interface (window pixels; width and height of the window).
void draw(std::vector<d2::HudSprite>& hud,int width,int height,const Art& art);
// The unit and object the controls point out (the attack target; what the use button would talk to or use), lit up
// and named as if the cursor were over them: replaces what the mouse picked when there is one.
void highlight(uint32_t& unit,uint32_t& object);
// A skill was chosen in the skill popup. True when a skill button opened the popup to be given a skill (the skill is now
// that button's), false when the popup was opened to choose the left or right skill as usual.
bool assign(int skill);
// Dismiss the touch comparison overlay when an Escape/Back key is pressed.
bool dismissComparison();
// The enemy the attack button aims at (native unit number, 0: none): lit up and named as if the cursor were over it.
uint32_t target();
// Lets go of everything (leaving the game, a menu opening over it).
void reset();
}
