# Touch controls: plan

How D2Next should play on phones and tablets (Android now, mobile browsers later), modelled on the layout Diablo
Immortal made standard for action RPGs on touch screens. The game rules stay Diablo II 1.10f's. Only the way the player
gives orders changes, and every control below turns into a command the client already sends today (see
"What already exists").

## What we have now

- SDL turns the first finger into the mouse (left button). A second finger, while the first is down, is a right click
  where it touches (`src/PlatformSdl.cpp`).
- Back is Esc. The on-screen keyboard appears only when typing.

That's enough to test, but not to play. You walk by holding a finger on the ground, which covers the hero. Casting
needs two fingers. The 800×600 interface is tiny on a phone. Belt, hotkeys and Alt (item names) can't be reached.

## The target layout

Landscape, the game full screen. Controls are drawn over the game, semi-transparent, using the game's own art where it
has some (skill icons, belt items, panel buttons).

```
 ┌──────────────────────────────────────────────────────────────────┐
 │ [life]                                    [map] [inv] [char] [≡] │
 │                                                                  │
 │                                                                  │
 │                           (the game)                    [S3] [S4]│
 │                                                       [S2]       │
 │   ╭───╮                                            [S1]  ╭─────╮ │
 │   │ ● │  ← joystick (appears where                       │ ATK │ │
 │   ╰───╯     the left thumb lands)          [P1] [P2]     ╰─────╯ │
 │ [mana]                            [pick up / talk]               │
 └──────────────────────────────────────────────────────────────────┘
```

| Control | What it does | Becomes (existing command) |
|---|---|---|
| **Joystick** (left half, floating) | Walk or run in the thumb's direction while held; let go to stop | `Command::moveTo` a point ahead in that direction, re-sent as the thumb moves (see Movement) |
| **Attack** (big button, bottom right) | Use the left skill on the best target (see Targeting); hold to keep attacking | `Command::attack` / `castOn(false, unit)` |
| **Skill buttons S1–S4** (arc around Attack) | Tap: cast that skill on the best target. Drag: aim it, release to cast. Drag back onto the button: cancel | select the skill as the right skill (as the F1–F8 hotkeys do), then `castOn(true, unit)` or `castAt(true, point)` |
| **Potions P1, P2** | Drink from the belt's first and second columns | `Command::useBelt` (as keys 1–4) |
| **Pick up / Talk** (appears when something is near) | Pick up the nearest item, talk to the nearest townsperson, use the nearest object (chest, door, waypoint) | `pickup` / `interact` / `interactObject` |
| **Top-right buttons** | Automap, inventory, character, skill tree, game menu | what the keys Tab, I, C, T, Esc do now |
| **Tap on the game** | As today: walk there, attack or talk to what's tapped, pick up the tapped item | today's left-click path in `Input.cpp` |
| **Long press on an item or skill** | Its tooltip (the mouse-over text) | today's hover code, at the finger |
| **Pinch** | Zoom (the existing zoom) | `zoom` |

Diablo II has two active skills (left and right), not five. The skill buttons use what the original already offers, the
F1–F8 hotkeys. Each button holds one hotkey skill, and pressing it makes that skill the right skill and casts it, which
is what pressing F1 and right-clicking does in the original. A button's skill is chosen in the skill-select popup, just
as hotkeys are assigned now. The Attack button is the left skill.

## Movement

The joystick gives an angle and a strength.

- **Direction:** every few ticks, the client sends `moveTo` to the point about four subtiles ahead of the hero in that
  direction. That is exactly what holding the mouse button on the ground does today (`holdWalk` in `Main.cpp`). With 4
  subtiles and a refresh every 3 ticks (120 ms), walking never stalls, and turning stays smooth instead of snapping to the
  eight directions `Command::hold` has.
- **Strength:** pushing past about 80% of the ring runs; less walks, so the run/walk toggle isn't needed. Both are
  `Command::setRun`.
- **Release:** sends `Command::stop`.
- **Blocked paths:** the joystick point is clamped to the nearest walkable subtile (the collision grid is already in
  the client), so the hero slides along walls instead of standing still.

## Targeting (Attack and quick-cast skills)

The best target is chosen in this order:

1. the enemy picked last, while it lives and is in the skill's range;
2. the enemy nearest the direction the joystick points (within ±45°) and in range;
3. otherwise the nearest enemy in range.

"In reach" is 18 subtiles, about half the 800×600 screen across, for every skill. skills.txt's `range` column turned
out not to be a reach. It says whether the hero walks up to the target first (`h2h`: melee) or casts from where it
stands (`none`, the value of nearly every spell, Fire Bolt and Teleport included). A skill that isn't melee, used with
no enemy near, goes to the point straight ahead of the hero, so the player can still clear doors and barrels.

The chosen target gets the game's own hover highlight and name bar (the code that does this for the mouse already
exists), so the player sees what will be hit. Tapping an enemy on screen makes it the target.

## Picking up and talking

- **Pick up / Talk button:** shows the icon for the nearest thing within reach. In order: an item on the ground (its
  name, coloured by quality, is the label), a townsperson, an object.
- **Hosting a TCP/IP game:** while hosting, a touch-only **IP** button appears beside the top-right panel controls in
  the same dark-and-gold style. Tap it to show the host's local IPv4 address and port (4000) in a larger, high-contrast
  message for 30 seconds. Network reachability depends on the Wi-Fi network and router.
- **NPC choices:** with touch controls on, the initial Talk/Trade/Gamble/Leave menu uses larger text and taller,
  full-width touch rows with high-contrast button backgrounds. It stays inside the screen's safe area. Desktop without
  touch controls keeps the original-sized menu.
- **NPC dialogue:** spoken lines and the NPC name are scaled up and centered within the safe area when touch controls
  are enabled. Tap anywhere in the dialogue panel to skip the remaining lines; desktop retains its original sizing.
- **Item names:** ground items show their names all the time, as if Alt were held, so they can be tapped. The original
  lets players hold Alt for this, so the information shown doesn't change.
- **Gold:** isn't picked up automatically. The original doesn't do it, and that rule belongs to the game. The button and
  tapping cover it.

## The panels (inventory, stash, character, skills, shops)

These stay the original 800×600 panels, scaled up to the screen's height (`hudScale` already does this).

| Mouse action | Touch equivalent |
|---|---|
| Click | Tap |
| Drag an item | Tap to pick up, tap to place (how the original's cursor already works, so nothing changes) |
| Right-click (use, or move to the belt) | Long press, then choose an applicable action |
| Hover tooltip | Long press shows it; it goes away when the finger lifts |
| Shift-click | The "To belt" and "Sell" popup items |

Close (X) buttons retain their original artwork and have a 48-HUD-pixel touch target.

On carried gear, the long-press menu also offers **Equip**; the native game validates the destination and requirements.
On equipped gear, **Unequip** moves it into the first open backpack space. **Compare** shows damage, defense, durability
and requirements against a compatible item of the same item type; tap, click, press a controller button, or use
Back/Escape to dismiss it. The comparison consumes that input so it cannot activate a panel behind the overlay.
Affix-by-affix comparison is not available yet because the client does not expose the full rolled-stat list.

## Settings (stored with the profile)

- Left-handed: mirror the layout.
- Five button sizes (×0.65, ×0.8, ×1, ×1.25, ×1.5) and transparency. The HUD and buttons fit the device's safe area;
  button size changes independently without enlarging the Diablo II panels beyond the screen.
- Joystick: floating (appears where the thumb lands) or fixed.
- Auto-target on/off. With it off, Attack and skills only use the tapped target or the joystick direction.
- Show item names always on/off.
- Vibration on hits and level-ups (needs Android's VIBRATE permission).

On Android, SDL reports the safe area after accounting for display cutouts and system gesture insets. The game HUD and
on-screen controls use that area for their layout and touch targets; the world remains fullscreen behind it. Other
platforms use the full window as their safe area.

## Game controllers

SDL already reads gamepads on Android, Windows and in browsers, so a controller maps onto the same actions:

| Controller | Action |
|---|---|
| Left stick | Joystick |
| A | Attack |
| X, Y, B, RB | S1–S4 |
| LB, LT | Potions |
| Right stick | Aim |
| Start | Menu |

Built on the same action layer, it costs little.

## How it fits in the code

```
PlatformSdl ──finger and gamepad events──▶ TouchControls ──actions──▶ the existing Input.cpp paths and d2::Command
                                                 │
                                                 └──▶ HudSprites (drawn after the HUD by RenderHud)
```

- **`Platform.h`:** new `InputEvent` types `TouchDown`, `TouchMove`, `TouchUp` (finger id, position in window pixels)
  and gamepad axes and buttons. With touch controls on, the window stops turning fingers into the mouse
  (`SDL_HINT_TOUCH_MOUSE_EVENTS=0`).
- **New `src/TouchControls.h/.cpp`:** the layout (in HUD coordinates, so it scales with `hudScale`), each finger's
  owner (joystick, a button, the game, a panel), and targeting. It turns fingers into the same actions keys and clicks
  produce. It doesn't talk to the game itself; it calls the functions `Input.cpp` uses, so there is one path for keys,
  mouse, fingers and pads.
- **Drawing:** the controls are `HudSprite`s added at the end of `Renderer::buildHud`, so they go through both
  backends (D3D11 and GL) unchanged. They use the skill icons and belt pictures the HUD already loads.
- **Which build has it:** on by default on Android and in mobile browsers. On desktop, `--touch` (or
  `D2Next_TOUCH=1`) turns it on for testing with a touch screen or the mouse (SDL can turn mouse presses into
  touches).
- **Tests:** a test hook `D2Next_TOUCHES="down 0 120 500; move 0 160 480; up 0"` scripts fingers the way
  `D2Next_CLICKS` scripts clicks. Scene captures of each step guard the layout, the same way the other HUD captures do.

## Steps

Each step is playable on its own.

1. **Touch plumbing and the joystick.** Touch events, `TouchControls` with finger ownership, the floating joystick
   (moveTo ahead, run past 80%, stop on release), and the HUD and panels unchanged under it. The finish line: walk
   around town with one thumb.

   *Done (2026-10-03), in `src/TouchControls.cpp`:*
   - **Starting the joystick:** a finger that lands in the left 45% of the screen, above the control panel, with no
     left-side panel or menu open, becomes the joystick. Its ring follows a thumb that slides past it, and it waits
     faintly above the life globe when not in use.
   - **Walking:** a `moveTo` 96 scene pixels ahead, re-sent every 3 ticks or as soon as the thumb turns by 20°. The
     target is shortened until it can be reached and turned up to 67° to slide along walls.
   - **Running and stopping:** past 80% runs; lifting the thumb or returning to the middle stops.
   - **Other fingers:** the first is the left mouse button, a second the right one.
   - **On desktop:** `--touch` or `D2Next_TOUCH=1` turns it on.
   - **Test hook:** `D2Next_TOUCHES="d 1 150 300; m 1 230 300; t 40; u 1"` scripts fingers and prints where the
     player ends up.
2. **Attack and targeting.** The Attack button, best-target choice, highlight of the target, hold to repeat.

   *Done (2026-10-03):*
   - **The button:** 44 HUD pixels across, right of the control panel above the mana globe, showing the left skill's
     icon. It is hidden while the inventory, skill tree or a store covers the right half.
   - **Pressing it:** sends the same attack order as tapping that enemy. The target is the current one while it lives
     and is within 18 subtiles, otherwise the nearest enemy within 45° of the joystick's direction, otherwise the nearest
     enemy.
   - **Holding it:** moves on to the next enemy when one falls. The same enemy is never re-ordered, because NativeGame
     already repeats the attack every 10 ticks and a fresh order restarts the swing before it lands. The joystick's
     walking orders pause while it is held, since a walk order would call the attack off.
   - **The target:** lit and named like the mouse-over (`touch::target()` overrides `hoveredUnit`).
   - **Orientation:** the game is landscape only (either way up) and full screen on phones (`SDL_HINT_ORIENTATIONS`).
   - **Tested:** in the Cold Plains with `D2Next_TOUCHES`, holding the button killed three Dark Hunters in 40 ticks.
3. **Skill buttons.** S1–S4 bound to the F1–F4 hotkey skills, quick cast, drag to aim with a ground marker and cancel,
   dimmed when there isn't enough mana, assignment through the skill popup.

   *Done (2026-10-03):*
   - **The buttons:** four, 28 HUD pixels across, in an arc 110 pixels from the attack button (left of it round to
     above it), each holding a hotkey skill (`skillHotkeys[0..3]`, what F1-F4 hold).
   - **Pressing one:** makes its skill the right skill, as the F key does, then acts as a right click would:
     - an aura (skills.txt `aura`) is only selected;
     - a tap goes on the best enemy;
     - with no enemy near, a melee skill (`range` `h2h`) does nothing and the others go to the point ahead.
     Skills centred on the caster (warcries, Holy Shield) take effect around the hero wherever they are aimed, as in
     the game.
   - **Dragging out:** aims at the middle of the screen (the hero) plus three times the drag, with a ring on the
     ground. Releasing casts on the enemy under the ring, or at that place. Dragged back onto the button, nothing is
     cast.
   - **Choosing a button's skill:** a long press (0.6 s) or a tap on an empty button opens the game's right-skill
     popup, and the skill tapped there becomes the button's (`touch::assign`).
   - **Mana:** a button is dimmed while the mana is below the cost. The cost is computed as D2Game 1.10f charges it
     (0x6FD10C07: `(mana + lvlmana × (level−1)) << manashift`, at least `minmana`). That check found a D2MOO bug,
     now fixed in `SKILLS/Skills.cpp`: level 1 skills cost only their `minmana`, because D2MOO skipped the formula
     below level 2.
   - **Test hooks:** `D2Next_HOTKEYS="101,98,97,121"` sets the hotkeys; `D2Next_TOUCH_LOG=1` prints each
     button's cast.
   - **Tested:** in the Cold Plains, Smite from a button killed two Dark Hunters, Might was selected, Fist of the
     Heavens went where it was aimed, and a popup choice became the button's skill.
   - **Not yet:** the hotkeys aren't saved with the character (the original keeps them in the .d2s). They last only
     for the session.
4. **Potions, pick up and talk.** P1/P2, the contextual button, item names always shown.

   *Done (2026-10-03):*
   - **The potion buttons:** P1 and P2, 22 HUD pixels across, below-left of the skill arc. They drink from the belt's
     first two columns (`Command::useBelt`, as keys 1 and 2) and show the potion that's there.
   - **The use button:** 30 HUD pixels across, left of the skill arc. It appears only when something is within 10
     subtiles: an item on the ground first (the nearest), then a town person, then an object the mouse could use
     (`objectPicks`: objects.txt selectable, and level entrances). It shows the item's picture or the game's hand
     cursor (`cursor\ohand`), and lights up the person or object, as the mouse-over does.
   - **Item names:** this client already shows every ground item's name all the time.
   - **Tested:** in town, the use button talked to Warriv. P1 and P2 each drank a potion. In the Cold Plains, three taps
     picked up three of the six drops of a fight.
5. **Panels by touch.** Long-press tooltips and the action popup, bigger close buttons, the top-right panel buttons.

   *Done (2026-10-03):*
   - **Panel buttons:** six at the top right, using the mini panel's own pictures (`panel\minipanelbtn`): character,
     inventory, skill tree, automap, quests, game menu. They act through `miniPanelButton`, the same function the mini
     panel uses, and hide while the inventory, skill tree or a store covers the right half.
   - **A finger on an item** (backpack, worn, stash or cube, belt, a store's goods) moves the cursor there, so the
     game's description shows. Then:
     - **lifted soon:** a left click. On a store's goods it only shows the description, because a click buys in this
       client and a stray tap shouldn't;
     - **moved:** it becomes the mouse, pressed where it landed;
     - **held 0.4 s:** a menu beside the finger, in the game's font:
       - "Buy" on a store's goods (the click);
       - "Sell" on a backpack item while the store is in sell mode;
       - "Equip" on wearable backpack items, using the native game's slot and requirement checks;
       - "Unequip" on worn gear, moving it to the first open backpack cells;
       - "Compare" against another item of the same game item type (base damage, defense, durability and requirements);
       - "Use" for usable items and belt items (the right click: packet 0x20, or the belt's use);
       - "To belt" for potions and scrolls in the backpack (shift and click: packet 0x63);
       - "Drop" for what is in a grid (take, then drop).
       The next touch picks a line or closes the menu.
   - **Close buttons:** keep the original artwork and expand the touch target to 48 HUD pixels.
   - **Tested:** the inventory button opened the inventory. A long press on a Town Portal scroll showed its
     description and the menu. "Drop" put it on the ground. "Use" on an Identify scroll sent 0x20. "To belt" sent
     0x63, which the game refused as it refuses the PC's Shift-click, because the test character's belt was full. A
     quick tap took the scroll to the cursor.
6. **Settings and controllers.** Left-handed mirror, sizes, transparency, auto-target switch, gamepad mapping,
   vibration.

   *Settings done (2026-10-03):*
   - **Where:** the Esc menu's OPTIONS page, in the game's big lettering, one line per setting; a tap or Enter steps
     through the values. The lines are:
     - SOUND
     - TOUCH CONTROLS (on and off, also on PC)
     - LEFT-HANDED: the joystick on the right; the buttons on the left, mirrored around the attack button. Which
       panels hide the joystick and the buttons swaps sides with them.
     - BUTTON SIZE: very small, small, normal, large or very large (×0.65, ×0.8, ×1, ×1.25, ×1.5)
     - BUTTONS: faint, normal or solid (×0.6, ×1, ×1.4 on their transparency)
     - AUTO-TARGET: with it off, the attack and skill buttons only go for the enemy being fought or the one the
       joystick points at
     - VIEW ZOOM: 1x, 1.25x, 1.5x, 2x, 2.5x or 3x (phones start at 1.5x)
     - PREVIOUS MENU
     Both pages' lines come from one list (`gameMenuItems`), so the mouse, the keyboard and the drawing agree.
   - **Saved:** in `settings.ini` in the profile folder (the Save folder; on Android the app's `Save`), one
     `name=value` line each, written at each change. The previous three size settings are migrated to the matching
     choices. Test runs keep the defaults; `D2Next_SETTINGS=<file>` gives them one. `--touch` still turns the controls
     on whatever the file says.
   - **Tested:** settings changed through the menu were written, then read back by a second run (left-handed: a
     finger on the right walked the hero). A capture shows the mirrored, large, solid layout.
   - *Game controllers and vibration, done (2026-10-03).* They share the touch controls' actions (`TouchControls.cpp`),
     and work on PC and Android whether the touch controls are on or off. The SDL window opens controllers as they're
     plugged in and passes `PadButton` and `PadAxis` events. Nothing happens until a controller has been used, so mouse
     and keyboard play is untouched.
     - **Playing:**
       - left stick: walks (the joystick's walking and steering; runs from 68%);
       - A: the attack button (hold to fight on);
       - B: the use button;
       - X, Y, RB, RT: the four skill buttons, at the best target, or with the right stick held at the point that way;
       - LB, LT: the potions;
       - D-pad: character, inventory, skills, quests;
       - Back: the automap;
       - Start: Esc.
     - **Cursor, whenever the screen wants the mouse** (the menus before the game, a panel, a popup, an item on the
       cursor, the death screen): the left stick moves the game's cursor (`touch::update()`, every frame), slowly near
       the middle and up to 900 pixels a second at the edge. A is the left button, X the right one, B is Esc.
     - **The game menu:** the D-pad or the left stick steps through the lines, A chooses, B and Start go back.
     - **Vibration:** a hit taking at least 3% of life (stronger and longer for bigger hits), and a level gained. It
       uses the controller's rumble, or the phone's vibrator (Android VIBRATE permission; SDL's haptic device). The
       options have VIBRATION ON/OFF.
     - **Test hook:** `D2Next_TOUCHES` takes `p BUTTON 1|0`, `a AXIS VALUE` and `f FRAMES` steps.
     - **Tested:** the left stick walked the same path as the touch joystick. Holding A killed three Dark Hunters in 40
       ticks; B picked up two drops; LB drank a potion. The D-pad opened the inventory, the stick took the cursor to a
       scroll, and A took it (0x19) and put it back (0x18). Start, Down, Down, A chose Back to Main Menu.

   *Also (2026-10-03), on every build: BACK TO MAIN MENU in the Esc menu*, between SAVE AND EXIT GAME and RETURN TO
   GAME. The four lines are centred where the original's three were.
   - **Leaving:** the game loop ends. `gameMain` closes the game, and D2Game saves the character as the player leaves.
     `resetClientState()` puts the client's globals back.
   - **Starting again:** `run()` runs again in the same window (it is the one thing kept). The menus open on the main
     menu, with the character just played chosen first. A new renderer, art and game are made; the server is set up
     once per process, and each later session only creates its game.
   - **Test hook:** `D2Next_MAIN_MENU_TEST=N` leaves and comes back N times.
   - **Tested:** three games in one process, on the GL and the Direct3D builds. The third game's picture is
     byte-identical to the reference town scene. Clicking the menu line round and round started 11 games in two
     minutes without a fault.
7. **The web.** The same layer in mobile browsers (SDL delivers browser touch events the same way).

## Questions to settle while building

- Should the Attack button also pick up items and talk when nothing hostile is near, as some ports do? That would make
  the contextual button optional.
- Button positions in the original 800×600 HUD space or in screen space? Screen space is better on very wide phones;
  HUD space keeps captures simple.
- How many skill buttons: four (as Immortal) or eight (every hotkey)?
