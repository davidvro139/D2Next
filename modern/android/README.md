# D2Next for Android

The same game as the Windows build: D2MOO's game libraries linked in, SDL3 for the window, input and sound, and the
OpenGL ES 3 renderer (`src/GlBackend.cpp`, which also runs as WebGL 2 in browsers). 64-bit ARM phones and tablets,
Android 8.0 or newer.

## Building

    bash modern/android/build-apk.sh

The result is `out/android/D2Next.apk`. The script uses only the Android SDK's own tools (no Gradle or Android
Studio). It needs:

- the Android SDK in `%LOCALAPPDATA%\Android\Sdk` (or `ANDROID_HOME`), with `platforms;android-36`, `build-tools;36.1.0`,
  `ndk;30.0.16248370` and `cmake;4.1.2`;
- a JDK (`JAVA_HOME`, or the newest one in `C:\Program Files\Java`).

If `out/android-arm64` contains a CMake cache from another checkout, set `ANDROID_BUILD_DIR` to a fresh build directory
before running the script; for example, `ANDROID_BUILD_DIR=out/android-arm64-local bash modern/android/build-apk.sh`.

The steps:

1. CMake and the NDK build the game as `libmain.so` and SDL as `libSDL3.so` (in `out/android-arm64`).
2. javac and d8 compile SDL's Java activity and D2Next's Android activity (`external/SDL/android-project`).
3. aapt2 packages them with `AndroidManifest.xml` and `res/`.
4. zipalign aligns the package, and apksigner signs it with the debug key (`~/.android/debug.keystore`, made on first
   use).

## Installing

1. On the phone, turn on developer options and USB debugging.
2. Connect the phone, then run:

       adb install -r out/android/D2Next.apk

## The game files

The app never contains Diablo II's files; you must supply them from your own installation. Each time you open D2Next,
a launcher appears **before any archives are opened**. It is a checklist of the game folder, each required MPQ, the
save folder, and the view zoom. Tap **Change** for the detailed folder and zoom controls, **Done** to return to the
list, and **Launch** to start the game.

The first time (or after you clear the folder), the launcher opens Android's folder picker. The suggested location is
`Documents/D2Next`. This grants D2Next access only to that folder, without broad storage permissions.

If you pick Documents (or Downloads) and tap Use this folder, D2Next creates a `D2Next` folder inside it. It then
creates `MPQ`, `Save`, and `Logs`, each with a `README.txt` that lists what to copy. Put the files there in this layout:

    Documents/D2Next/
        README.txt
        MPQ/       the game archives (see MPQ/README.txt)
        Save/      the characters (.d2s), settings.ini, and appearance files
        d2next.log
        d2next.prev.log
        Logs/      native game logs

Required MPQs: `d2data.mpq`, `d2exp.mpq`, `d2char.mpq`, `d2sfx.mpq`, and `Patch_D2.mpq`. Letter case doesn't matter.
Optional `d2speech.mpq`, `d2xtalk.mpq`, `d2music.mpq`, `d2xmusic.mpq`, `d2video.mpq`, and `d2xvideo.mpq` can also
go in `MPQ/`. MPQs placed directly in the selected `D2Next/` folder are accepted too. Leave the archives in that folder;
D2Next opens them from there and does not copy them. After you copy them, return to the launcher; the checklist
updates, then tap **Launch**. **Change** opens the detailed settings: **Change folder**, **Refresh**, a separate save
directory or **Use game folder**, and view zoom. OPTIONS also has **CHANGE GAME FOLDER**, which takes effect the next
time you open the app.

If you already have characters, copy their `.d2s` files into `Save/` (or into the separate save folder) before tapping
Launch. D2Next imports existing saves and settings when you launch. The game reads and writes its active save in
private app storage (the `/data/data/.../files/Save/` path in diagnostic logs); Android's folder picker grants access
to a shared folder as a SAF document tree, not as a normal filesystem path. D2Next copies saves, settings, and logs to
the selected folder every 10 seconds and again when the game exits, skipping a file that is already the same size.
Keep a backup of important saves. The current session's log is `d2next.log`; the previous session is kept as
`d2next.prev.log`.

## Controls

- **Joystick:** put a thumb down on the game in the left part of the screen and slide it. The hero walks that way, and
  runs when you push to the edge of the ring. Lifting the thumb stops.
- **Attack button (bottom right):** attacks the nearest enemy with the left skill, preferring the one the joystick
  points at. Hold it to keep fighting from one enemy to the next. The enemy being attacked is lit up and named.
- **Skill buttons (the four small ones around Attack):**
  - **Tap:** casts the button's skill on the nearest enemy, or ahead of you if none is near.
  - **Drag out:** aims, with a ring on the ground; let go to cast there. Drag back onto the button to cancel.
  - **Long press, or tap an empty button:** opens the skill list, and the skill you tap goes on that button.
  - **Dimmed:** not enough mana.
- **Potion buttons (the two small ones lower down):** drink from the belt's first and second columns.
- **Use button (with a hand, or the item's picture):** shows up when something is near. It picks up the nearest
  item, or talks to the nearest townsperson, or uses the nearest chest, door, waypoint or entrance.
- **NPC choices:** the initial Talk/Trade/Gamble/Leave menu uses enlarged text and tall, full-width touch targets,
  positioned within the display safe area.
- **Panel buttons (top right):** character, inventory, skills, map, quests, game menu.
- **Hosting a TCP/IP game:** the host-only **IP** button appears beside the panel buttons, in the same dark-and-gold
  style. Tap it to show the local network address and port for 30 seconds so other players can enter it on the TCP/IP
  join screen. Both devices need to be on a reachable network; some routers may also require port forwarding.
- **Close buttons:** the original close-button artwork has a 48-HUD-pixel touch target.
- **Store buy and sell:** a **BUYING** or **SELLING** banner sits under the store tabs. The Buy and Sell buttons are
  labeled, larger, and the one in use has a gold or red frame. Tapping one also shows a short message. In sell mode the
  inventory says **TAP AN ITEM TO SELL**.
- **Items in the panels:**
  - **Tap:** clicks the item (picks it up, places it, sells it in sell mode). In a store, a tap only shows the item.
  - **Hold:** shows its description and contextual actions: Use, To belt, Drop, Buy in a store, Sell in sell mode,
    Equip carried gear, Unequip worn gear into the first free backpack space, or Compare compatible gear.
  - **Compare:** shows base damage, defense, durability and requirements for items of the same type. Rolled affixes are
    not included because the client does not expose their full stat list. Tap, click, press a controller button, or use
    Back to close; the dismissing input is consumed.
- **Any other finger:** the mouse's left button. Tap to walk, attack, pick up, or press buttons.
- **A second finger while the first is down:** the right button at that finger, which casts the right-hand skill.
- **Back:** Esc (the game menu).
- **On-screen keyboard:** shown when typing a character's name, an amount of gold, or a TCP/IP join address after you tap the address box.
- **First game:** labels sit on the joystick, attack, skills and potions until you tap the screen.

**Game controller** (Bluetooth or USB):
- **Playing:**
  - left stick walks;
  - A attacks (hold to keep fighting);
  - B picks up, talks or uses;
  - X, Y, RB, RT are the four skill buttons (hold the right stick to aim);
  - LB and LT drink potions;
  - the D-pad opens the character sheet, inventory, skills and quests;
  - Back shows the map;
  - Start opens the game menu.
- **In panels and menus:** the left stick moves the cursor, A clicks, X right-clicks, and B goes back.

**Vibration** on hits and level-ups (the phone, or the controller's rumble) can be turned off in the options.

**Settings:** the pre-launch menu sets the game-files folder, save folder, view zoom (1x to 3x; phones start at 1.5x
so the world is readable), and cheats before archives open. In game, open the Esc menu (Back, or the last of the
top-right buttons), then OPTIONS. There you can set touch controls, left-handed (joystick on the right), button size,
how solid the buttons are, auto-target, view zoom, cheats, and change the shared game folder (takes effect the next
time you open D2Next). With cheats on, the Esc menu also has **CHEATS**: add or fill gold, fill life and mana,
give Town Portal or Identify scrolls, max all class skills, and gain a level. The choices are kept in `Save/settings.ini`. The Esc menu's BACK TO MAIN MENU
saves and goes back to the main menu.

How the controls are designed, and how each was tested, is in [../TOUCH-CONTROLS.md](../TOUCH-CONTROLS.md).

## Status

The first Android build. It compiles and packages, but hasn't yet been run on a device.

Known gaps:

- **Text before the fonts load:** on Windows this uses the system font; here it is left blank.
- **Logs:** the app writes `d2next.log` (and keeps `d2next.prev.log` from the last session); `adb logcat` shows SDL's messages and crashes.
- **Device validation:** the APK builds and packages, but touch controls, layout and lifecycle still need testing on
  physical Android devices with different screen sizes and cutouts.
