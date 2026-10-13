# Modern client backend integration

The viewer uses `GameBackend` in `src/Backend.h`. It can wrap the walking prototype
or connect to a disposable x64 D2Game helper process. The optional native-town
scene loader uses x64 D2Common/D2CMP/Fog/Storm/D2Lang for map generation.

## Native game session (2026-09-20)

`Launch-Modern-NativeGame.cmd` discovers compatible `.d2s` files in the local
`Save` folder and opens character selection. Right-click walks and WASD sends
short native walk targets. Progress is written atomically back to the selected
local player file through the native host's save callback.

```powershell
out/modern/Release/D2Next.exe --native-town out/build/X64Dlls/runtime/Release
out/modern/Release/NativeGameTests.exe out/build/X64Dlls/runtime/Release "C:\games\Diablo II 1.10" "C:\games\Diablo II 1.10\save\dfsd.d2s"
```

The modern build produces `NativeGameHost.exe` and a separate
`native-host/D2Net.dll`, built from the existing mock transport. No native DLL
outputs or original game files are replaced. The helper owns the DLLs, tables,
game containers, callbacks, and player. It uses the tested headless host's join
sequence. A versioned fixed-width local pipe carries walk/tick requests and copied
16.16 player coordinates. D2Game's pathfinding and collision determine movement.
Native GUID and tick continuity are checked before publishing client entity ID 1.
The game seed must match the scene seed, and the initial player position must be
walkable in the generated scene. This does not compare every live room tile.

The viewer coalesces pending movement to the latest target per tick. Accepted
means queued locally, not acknowledged arrival: dynamic native collision may
still stop the player. Held input supplies targets up to three subtiles away;
release requests the current subtile. Outside/blocked static destinations are
rejected locally. Native unit/door collision is not drawn in the static overlay.

The helper runs without a console window. Startup has a 30-second timeout and
individual tick replies a 5-second timeout. Calls are currently synchronous;
background I/O is future work. A Windows job terminates the helper if the viewer
exits or initialization fails. Normal shutdown requests leave and then exits the
disposable process, allowing the OS to reclaim native global allocations without
cross-heap static destruction. Diagnostics go to `Release/native-game-host.log`.
Use one native session per build directory at a time (the diagnostic log is shared).

Scope: one tested paladin, normal difficulty, Act I town, movement only. NPCs and monsters are now shown as glyphs (see below), but
items are not rendered and nothing is interactive.
A snapshot outside level 1 ends this prototype with an error. Other saves can be
selected with `--native-save`, but acceptance by the native save loader is not
guaranteed. The helper uses debug unit lookup exports and this checkout's headers,
so it must be rebuilt alongside DLL layout changes. No network sockets are used.

Verification: native join, actual arrival, held movement/release, per-tick replay
equality across two sessions, invalid actor/destination rejection, missing-save
failure, retained snapshots and unchanged source save all pass. No helper process
remained after tests. All eight regular tests pass. Launcher smoke walking reached
its five-subtile target in 20 native ticks; hardware/WARP captures match at 535,050
non-background pixels. Only the headless held-input test has been performed;
extended interactive play has not been evaluated.

## Native units (2026-09-20)

Protocol v3: every reply also carries up to 160 monsters and town people (native GUID, class, mode, life, absolute 16.16
position, flags), nearest to the player first. `NativeGame` turns them into snapshot entities with ids `1000 + GUID`
(never a native pointer), `kind` (town person, enemy, corpse), class and life, and interpolates them like the player.
The viewer draws debug glyphs only: blue town person, red enemy, grey corpse; there is no monster artwork yet, and
nothing but the walking command is sent, so the player cannot yet attack or talk. In the Rogue Encampment the list holds
Gheed, Akara, Charsi, Kashya, Warriv and the rogues, who wander as they do in the game (`NativeGameTests` checks them).
Monsters, combat, death and drops in the D2Game backend itself are verified in `BUILD-X64.md`; this only exposes them.

## Native town scene bridge (2026-09-20)

Run `Launch-Modern-NativeTown.cmd` from the repository root, or:

```powershell
out/modern/Release/D2Next.exe --native-town out/build/X64Dlls/runtime/Release --walk --seed 0x12345678
out/modern/Release/NativeTownTests.exe out/build/X64Dlls/runtime/Release "C:\games\Diablo II 1.10"
```

`NativeTown.cpp` loads the native DLLs by absolute path, mounts separate archive
handles, loads the tables, creates Act I from a seed, and streams Rogue Encampment
rooms. Each selected native tile is matched against its complete DT1 header in
the same MPQs, retaining the actual variant rather than choosing the first tile
with a matching style. Native hidden tiles and exit markers are omitted.
Room collision is copied with the walk-blocking mask `0x3c01`; no room means blocked.
This is static dungeon collision, without D2Game units, doors or object interaction.

Native world coordinates are translated by `worldOrigin` to local scene subtiles.
Tile pixels, placements and collision all belong to the modern client when the
function returns. The native act, tables, archive handles and DLLs have already
been released. The loader requires exclusive access to the DLLs' global state;
a future persistent D2Game host must own scene generation itself.

This experimental bridge depends on this checkout's packed native tile/level
layouts and D2CMP's private 96-byte header plus library-name pointer. It is not
compatible with arbitrary DLL versions. Keep changes to those layouts coordinated
with this file; a versioned tile-description export would remove that dependency.

Without `--native-save`, the native scene uses prototype Sorceress movement,
spawn selection and animation. This scene-only mode has no D2Game session. Wall
ordering still uses the existing renderer's approximation; roofs and animated
tiles are not updated. Only Act I town is supported in this mode.

Verification: repeated generation after unload/reload produced identical tile,
pixel and collision hashes; seed `0xbeef` changed the generated scene. Snapshot
data survived later loads, tile bounds passed, the working directory stayed
unchanged and all native modules unloaded. All eight regular tests pass. Hardware
and WARP walking captures match byte for byte. Default seed: 35 rooms, 2,695
placements, 295 unique tiles, 14,922 walkable subtiles. This is integration
verification, not comparison against the original game's displayed frame.

## Implemented contract

- Input sends a `Command` and session-local `EntityId`. Zero is invalid. IDs must
  not be reused within a session, including after an entity disappears.
- Commands are applied in input order before the next 25 Hz tick. Results distinguish
  accepted input, rejected input, and unknown actors. Acceptance is not arrival.
- `tick()` advances one simulation step on the owning thread.
- `snapshot()` returns an owned copy of the tick, local player ID, and entity
  positions, facing and animation state. Copies survive later ticks and backend
  destruction. The renderer cannot access routes, native units or allocations.
- Positions are 16.16 fixed-point subtile centres in the current scene coordinate
  system. Previous and current positions describe adjacent completed ticks. A
  teleport or scene change must set both to the destination to avoid interpolation
  across the discontinuity. Facing uses the modern character's 0..15 direction order.
- The walking viewer requires a local player in every published snapshot. Loading,
  disconnection and scene transitions need explicit states before a DLL adapter is enabled.

`Movement.h` shares command and coordinate types without exposing `World` or
`PlayerState`. `Main.cpp` holds one copied snapshot, refreshed after a tick, and
uses it for rendering, the camera, status and movement smoke checks.

This is a C++ interface compiled into the modern executable. It is not a stable
cross-DLL ABI, save format or network protocol. Vectors and virtual objects must
not be passed into the game's ordinal exports. A future adapter translates these
values into the DLL's own calls and keeps DLL-owned data inside the adapter.

## Remaining native backend work

1. Startup, join and host-driven ticks now work in `NativeGameHost.cpp`. Keep the
   DLL implementation separate from the host and client code.
2. The local pipe and mock D2Net support movement. Extend explicit request results
   and session states before adding interactions; client-side acceptance does not
   imply that the server completed the action.
3. Extend native unit type/ID and lifetime mapping beyond the local player. Copy needed data
   into snapshots; never expose native pointers or assume IDs alone cannot be reused.
4. Act I town coordinates/tiles/static collision are bridged. For other levels,
   generate the scene from the live host and supply it before publishing its
   entities; independent generation is currently limited to the initial town.
5. Translate the native character class, equipment, action and animation timing.
   The current snapshot/viewer only supports the idle/walking Sorceress. A native
   paladin save must not be presented as a correctly rendered Sorceress.
6. Town join, walk, stop, leave and the marker launcher are tested. Continue
   comparing positions/ticks and inspecting rendered output as entity and scene
   coverage expands. Retain the prototype backend for renderer regression checks.

Archive handles from the modern static StormLib and the DLL's Storm wrapper are
separate. Each owner must close its own handles; do not exchange them. Preserve
the same archive precedence and never overwrite original saves or game assets.

## Verification

`BackendTests` compares a scripted walk, mid-route retarget, held movement and stop
against the existing `World` on every tick, including interpolation. It also checks
invalid actors, rejected movement, independent collision ownership, and snapshots
retained after backend destruction. Existing simulation tests continue to cover
movement rules. The viewer's `--walk --smoke-test` exercises input through this
interface and renders from its copied snapshot.

## Areas, fighting and HUD (2026-09-20)

`loadNativeTown(..., levels)` now builds one scene from several Act I levels; the viewer loads the town, Blood Moor,
Cold Plains and Stony Field together (about 24,000 tile placements, 11 s to start), and `NativeGame` accepts a player in
any of them. Right-click on an enemy sends `Command::attack`; the backend resends the left-skill packet every 10 ticks while
the target lives. Vitals (life, mana, experience, character level) and the native level id are part of `WorldSnapshot`.
`NativeAreaTests` walks the paladin out of the encampment into the Blood Moor, checks every step lies on a walkable
subtile of the scene, and fights an enemy. The HUD is placeholder: bars and text drawn with the system font.
Ground items are enumerated by the host (`Item` records in the pipe reply), named by `ItemTable`/`StringTable` (string.tbl, weapons/armor/misc and affix tables), drawn as coloured labels and picked up by right-click (`Command::pickup`: walk within 3 subtiles, then packet 0x16). `NativeAreaTests` kills enemies in the Blood Moor and loots a drop; `D2Next --smoke-test --autoplay N --capture f.bmp` scripts the same walk and fight and captures the frame.
Inventory: the host lists the player's items by walking the D2Common inventory (`INVENTORY_GetFirstItem`/`GetNextItem`, plus the cursor item; the unit info owner field is not set for carried items). `Item.mode` is the native item mode (0 stored, 1 equipped, 2 belt, 3/5 ground, 4 cursor), stored items carry their grid cell and page, equipped ones their body location. Inventory commands (`takeItem`, `placeItem`, `equipItem`, `swapBody`, `takeBody`, `useItem`, `useBelt`, `dropItem`) travel as generic `Operation::Packet` requests holding the client packet (0x19, 0x18, 0x1A, 0x1D, 0x1C, 0x20, 0x26, 0x17). The server treats a placement that does not fit as cheating and disconnects, so the viewer checks the fit first. In the viewer: I opens the panel, left click picks up/places/equips/swaps (click outside drops), right click drinks/reads a grid item, 1-4 or a click drinks the belt potion.
Not done: skills, NPC interaction, other acts, dungeons, audio.

## NPCs and stores (milestone E)
- The host taps the x64 D2Game (`D2GameHost_SetPacketTap`, exported by this build only): every packet the server queues for the player goes into the
  reply as `[16-bit length][bytes]...` and reaches the viewer as `WorldSnapshot::server` (one tick of packets). The viewer parses only what it needs:
  0x27 (a town person's menu: unit, quest speech string ids) and 0x2A (transaction result: code, gold).
- Commands: `interact(guid)` walks up to the person (hops along the scene path, the native pathfinder reaches about 14 subtiles) and sends 0x13. `raw(...)`
  sends any client packet. The dialog uses 0x2F (start talking/trading), 0x38 with action 1/2 (open the store / gamble), 0x32 (buy), 0x33 (sell), 0x30 (leave).
  Items can only be moved while the conversation is in the "talking" state that 0x2F sets.
- Store items are the NPC's inventory: the host lists them with `Item.owner` = the NPC number (`ItemSnapshot::vendor`), the store tab in `page`, the cell in
  `x,y` and the price the game charges (`ITEMS_GetTransactionCost`, difficulty 0 for now). `EntitySnapshot::services` says who sells (bit 0) or gambles (bit 1).
- Silent stub `sub_6FCC88B0` (buying) was written from the disassembly. `D2Next_TEST_GOLD=n` in the host's environment gives the character gold for tests.
- Tools: `NpcProbe DLL_DIR GAME_DIR SAVE [class]` dumps the packets of a conversation, `StringProbe GAME_DIR id...` prints string.tbl entries by id.

## Saving (milestone G)
- The default profile is the local `Save` folder beside the game. A `.d2s` file already in that folder is updated in place through a temporary file and
  atomic rename. `D2Next --native-save X.d2s` imports an external character into the local folder on its first save. `--profile DIR` selects another
  profile folder. Headless test runs (`--smoke-test`, the test programs) do not write a profile.
- The host enables the game's save code (`D2GAME_10036(1)`) only when it is given an output path; the server then hands each save to `cbSaveCharacter`
  (on leaving the game and whenever the game saves the character), and the host writes the bytes (minus the 2 byte realm length prefix) next to the profile
  file and renames them into place. `NativeSaveTests` buys an item, ends the session, starts again from the profile save and finds the same gold and items;
  the original save is compared byte for byte afterwards.
- Two upstream bugs had to be fixed for this to work (see `BUILD-X64.md`): the save writer left size and checksum 0, and the loader rejected every real
  save (only the header-only new-character file worked). The original `sfwsedf.d2s` (a barbarian with items, in `D2SE\CORES\1.10f\save`) now loads too.

## Stats, skills and controls (milestones D and G)
- Every reply carries the player's stats 0..31 (`WorldSnapshot::stats`: 0 strength, 1 energy, 2 dexterity, 3 vitality, 4 unspent stat points, 5 unspent
  skill points, 6-11 life/mana/stamina and maxima in 1/256, 12 level, 13 experience, 30 next level, 31 defense, 25 damage percent) and the skills the
  player has a level in, plus the selected left and right skill. The host reads them through D2Common (`SKILLS_GetFirstSkillFromSkillList` and friends).
- Packets: 0x3A add a stat point (stat id, count-1 in the high byte), 0x3B add a skill point, 0x3C select a skill (id, bit 31 = left button, item -1).
  `NativeLevelTests` levels a paladin with the host's `D2Next_TEST_EXP` hook (`D2GameHost_AddExperience`, an x64-only export used by tests), spends the
  points, learns Might and selects it. Selecting an aura switches it on (there is no cast animation).
- Controls (like the original): left button walks (hold to keep walking), attacks the enemy under it with the left skill (0x06, repeated while it lives),
  talks to a town person, picks up labelled items; shift + left button uses the left skill at the spot (0x05). Right button: right skill on the enemy (0x0D)
  or at the spot (0x0C). Middle button drags the camera. I inventory, C character, T skill tree, 1-4 belt, F3 collision overlay, Esc closes windows.
- Skill icons come from `data\global\ui\spells\??skillicon.dc6` (the icon number of skilldesc.txt is the picture number; the next picture is the pressed one).

## Game objects (milestone E, continued)
- The host publishes objects near the player (`WorldSnapshot::objects`, 90-subtile radius, nearest first): native object number, class (an objects.txt row),
  animation mode and absolute subtile position. `ObjectTable`/`ObjectArt` (`Tables.h`) load objects.txt and decode `data\global\objects\<token>\...` COF/DCC
  animations the same way character/monster art is loaded, with a fixed "lit" component and a `hth` weapon class token (objects have no equipment).
- The viewer draws objects through the same per-unit draw path as monsters/NPCs (kind 4 in the `OtherUnit` list), tracks each object's own animation clock
  (`objectClocks`, reset when its mode changes) and records screen boxes for the ones objects.txt marks selectable for their current mode (`objectPicks`).
  A left click on one of those boxes sends `Command::interactObject`, which walks the player close and then sends packet 0x13 with unit type 2 (`UNIT_OBJECT`);
  the server itself finishes the approach and opens the object (same as the original client relying on the server's own walk-to-interact).
- `ObjectProbe DLL_DIR GAME_DIR SAVE` lists the objects around the start position; `NativeObjectTests` opens the encampment waypoint and checks for the
  game's own waypoint-menu packet (0x63). The waypoint travel list itself, chest/door contents and quest-linked objects are not built yet.

## Waypoint menu (finishes milestone E)
- `LevelTable` (`Tables.h`) loads levels.txt: native level id, display name (`LevelName`, already plain text) and waypoint number (255 = none), with a
  reverse map from waypoint number to level. Opening a waypoint object (see above) eventually gets the game's own menu packet 0x63 (`D2GSPacketSrv63`:
  unit id of the waypoint, then 8 uint16 activation words — word 0 is a status marker, words 1..7 are one bit per waypoint number,
  `WAYPOINTS_IsActivated`'s own layout). The viewer keeps those flags (`WaypointMenu`), lists every activated waypoint by name and, on a click, sends the
  travel packet 0x49 (waypoint unit id, target level id) — the same packet and server-side approach/warp logic (`D2GAME_WAYPOINT_Unk_6FC79600`) a real
  client uses, so travel between any two of our currently loaded levels works; travelling to a level whose scene was never generated would not have
  anywhere to render (dungeons and other acts are not streamed yet, see milestone C).
- `NativeObjectTests` opens the encampment waypoint, decodes the flags (checks Rogue Encampment's own bit, always active from a fresh character) and sends
  a round-trip 0x49 travel packet, confirming the whole client-to-server path holds up without protocol errors.

## Missiles (first version of milestone H)
- Missiles (`UNIT_MISSILE`) are enumerated by the host exactly like monsters — same `native::Unit` record, distinguished by a new flag bit
  (`unitMissile`) — since they share the same `pDynamicPath`/`tGameCoords`/`wAnimSpeed`/`dwAnimMode` fields at the same struct offsets. They are filtered
  to within 120 subtiles of the player (there is no life/vendor data to publish, those fields are left 0) and share the monster/NPC array's budget
  (`maxUnits`, bumped from 160 to 192 for headroom). The client (`NativeGame.cpp`) reuses the existing unit-publishing code path unchanged apart from
  mapping the flag to `EntityKind::Missile`, so a missile gets the same movement-derived facing and mode-change animation clock as any other unit.
- `MissileTable`/`MissileArt` (`Tables.h`) read missiles.txt's `CelFile` and `LoopAnim` columns and decode `data\global\missiles\<CelFile>.dcc` directly
  with `decodeDcc` — missiles are a single DCC per class with no COF/layering, unlike characters/monsters/objects, and their direction/frame counts come
  straight from the DCC header. The viewer's `drawOther` treats missiles as a fourth sprite kind (5 = object, 4 = missile) alongside units and objects.
- `MissileProbe` walks a paladin to the Blood Moor, uses the level-up test hook to reach the level Holy Bolt requires (6), learns and selects it, casts it
  at the nearest monster and confirms a `EntityKind::Missile` entity appears with the expected class/art (`HolyBoltMissile`). `D2Next --autoplay N
  --cast-skill 101 --capture f.bmp` scripts the same thing for a visual capture (the blue Holy Bolt sprite mid-flight, confirmed).
- Not done: hit/death animation overlays for the target unit, sound, and purely-visual effects that are not their own missile unit (e.g. an aura's pulse
  ring, some explosion sequences that are separate missile classes already handled by the same mechanism above).

## Audio (second part of milestone H)
- `Audio` (`src/Audio.h/.cpp`) plays the game's 16-bit PCM WAVE files through winmm, one `waveOut` device per sound so Windows mixes them; music loops
  (`WHDR_BEGINLOOP`), effects are one-shot (at most 24 at once). d2music.mpq is mounted with the other archives.
- The viewer plays `data\global\music\Act1\town1.wav` in the Rogue Encampment and `...\wild.wav` elsewhere, and a one-hand swing effect when the player
  enters an attack mode. Audio is only created for real play: captures/smoke tests stay silent, and `--no-audio` turns it off.
- `AudioProbe GAME_DIR` (needs a sound device, so it is not in ctest) plays the town music and two effects and checks the devices open.
- Not done: monster/skill/UI sounds (sounds.txt maps names to files under data\global\sfx; the server's own sound packets are not parsed yet),
  other acts' music, volume settings, ambient scene loops.
- Monster sounds: `SoundBank` (Tables.h) resolves monstats `MonSound` -> monsounds.txt `HitSound`/`DeathSound` -> sounds.txt file. The viewer plays them when a
  nearby enemy's native mode changes to get-hit (3) or death (0). `TableTests` checks the zombie's files exist in the archives. Skill/UI/footstep sounds not done.

## Levels beyond the first scene (dungeons, the next wilderness areas)
- The scene is no longer fixed. `loadNativeTown(..., levels, withNeighbours)` generates the scene of any Act 1 level (id up to 39) with the game's seed; with
  `withNeighbours` it also covers the levels whose rectangles touch it (within 512 tiles), so walking across an outdoor border stays inside one scene.
  `NativeTown::levels` lists what it covers. `DungeonProbe DLL_DIR GAME_DIR SEED LEVEL...` reports generation of the levels (all tried, e.g. Den of Evil, Jail,
  Catacombs, generate in about a second).
- The backend no longer stops when the player leaves the loaded scene. `GameBackend::setScene(collision, origin, levels)` swaps it. The viewer watches
  `worldView.level`; when it is not in the loaded set it prints "Loading level N...", generates the new scene in-process (the game host is a separate
  process, so its DLLs are not disturbed), swaps collision/origin/GPU tiles and continues (a pause of a couple of seconds).
- `NativeGameHost` test hook D2Next_TEST_WAYPOINTS=1 discovers every Act I waypoint. `NativeDungeonTests` travels to the Jail (level 29) by the real 0x49
  packet, generates its scene, checks the player stands on walkable ground and walks there. `D2Next --travel 29` scripts the same for a capture (Jail
  Level 1 with the waypoint flames confirmed). The area name now comes from levels.txt, and the waypoint menu closes on a level change.
- Not done: other acts (the game host starts in Act I and the waypoint packet only travels within an act; the scene loader hard-codes the act 1 palette and
  act 0), dungeon music, walking through stairs/entrances by hand has not been exercised (only waypoints), and the scene swap blocks for the generation time.

## Level entrances and Act I coverage
- Level entrances/exits (cave mouths, stairs, crypt doors, the Underground Passage) are UNIT_TILE units. The host publishes those within 90 subtiles in the
  objects array with flag bit 0; the backend splits them into `WorldSnapshot::tiles` (`classId` = lvlwarp.txt row, `id` has `tileIdBit` set). Interacting is the
  same command as for objects: `InteractObject` sends 0x13 with unit type 5 and the server itself walks to the tile and warps the player.
- The viewer's `WarpTable` reads lvlwarp.txt; the clickable area of a tile is its SelectX/SelectY/SelectDX/SelectDY rectangle relative to the tile's screen
  position (checked: the box sits exactly on the Cold Plains cave mouth; F3 outlines it in gold, hovering shows "Entrance"). Clicking it walks up and enters;
  the viewer loads the new level's scene when the game reports it (`D2Next --travel 3 --walk-world X Y --click-after X Y` scripts it; capture: Cave Level 1).
- Findings: Act I's wilderness is not one continuous map. Blood Moor, Cold Plains, Stony Field (and the Burial Grounds) lie together; Dark Wood, Black Marsh,
  Tamoe Highland and the Outer Cloister lie together elsewhere, joined to the first group by the Underground Passage. All levels 1-39 generate (28 does not
  exist); `DungeonProbe DLL GAME SEED 1,2,3` (comma list) draws a coarse ground map.
- `NativeWalkTests DLL GAME SAVE LEVEL...` walks the real player through levels by pathfinding (town, Blood Moor, Cold Plains, Stony Field verified);
  D2WALK_ENTER=1 then takes the nearest entrance (Cold Plains -> Cave Level 1, scene swapped, player on walkable ground).
- x64 D2Game gained `D2GameHost_AllowTimeouts` (host calls it with 0): the server kicked a player who sent no packet for 45 s (heartbeat timeout), which
  also killed the game whenever the viewer stalled (scene generation takes 1-8 s, a big walk-path search took longer). Differential regression identical.
- Music per Act I area: town, wilderness, crypt (caves/crypts/other dungeons), monastery levels, Tristram.
