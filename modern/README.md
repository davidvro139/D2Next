# D2Next: first 64-bit engine slice

## Source layout (2026-09-26)

| File | What it holds | Platform |
|---|---|---|
| `Assets`, `Dcc`, `Dt1`, `Ds1`, `Character` | the game's file formats (MPQ via StormLib, pictures, maps, tiles, animations) | portable |
| `Tables`, `Font`, `UIAssets` | the game's text tables, fonts and interface pictures | portable |
| `Simulation`, `Navigation`, `Occlusion`, `Movement`, `Backend` | the prototype world, pathing, draw order, and the `GameBackend` interface | portable |
| `FrontEnd` | title, main menu, character select and creation (produces a sprite list) | portable |
| `NativeGame`, `NativeTown`, `NativeProtocol` | the real game logic (D2MOO) behind the `GameBackend` interface, and the level loader | Windows |
| `NativeSession` | the game server: D2Game running one game with the player, a tick per request | Windows |
| `NativeLink.h`, `NativeExports.cpp` | how the native code finds D2MOO's functions: linked in (the table in `NativeExports.cpp`) or by export number in the x64 DLLs | Windows |
| `NativeGameHost` | the helper process of the DLL build: `NativeSession` behind pipes | Windows |
| `ClientState.h/.cpp` | the client's shared state (panels, cursor, NPC dialogs, the world snapshot, camera) and the session logic (ticking, the game's packets) | Windows types still in use |
| `Platform.h` | the window and its input as the game sees them: `InputEvent`s, held keys, size, key numbers (`d2::key`) | portable interface |
| `PlatformWin32.cpp` | the Win32 window (messages to `InputEvent`s) and system-font text (GDI) | Win32 |
| `PlatformSdl.cpp` | the SDL3 window (events to `InputEvent`s); SDL is vendored in `external/SDL` (3.4.16, built static) | SDL3 |
| `Input.cpp` | `handleInput`: clicks on the interface and the world, keys | portable |
| `DrawList.h` | `DrawCommand`: one picture or colour on a quad, lit at its corners, blended (normal, added, see-through, darken, shadow) | portable |
| `Hud.h` | `HudSprite`: one piece of the interface in window pixels (becomes a `DrawCommand`) | portable |
| `Renderer.h`, `RenderWorld.cpp`, `RenderHud.cpp` | what the game looks like: the world in painter order, lights, shadows, the interface, the menus and the cursor, all as draw lists | portable code (reaches Win32 types only through `ClientState.h`) |
| `GpuBackend.h` | what a graphics API must do: frames, draw lists, captures, texture caching by picture | portable interface |
| `D3D11Backend.cpp` | the Direct3D 11 backend (shaders, blend and stencil states, textures, capture) | Direct3D 11 |
| `PlatformText.h`, `PlatformWin32.cpp` | text in the system font before the game's fonts load (GDI) | Win32 |
| `Audio` | sounds and music (music read on a background thread), played through winmm or SDL3 audio streams | winmm / SDL3 |
| `Main.cpp` | startup, the game loop, and the scripted test runs | Win32 |

`Main.cpp` used to hold everything from `ClientState` to `Renderer`; it was split mechanically on 2026-09-26, checked by
comparing twelve captured scenes byte for byte before and after. The interface was then made a sprite list, and the
renderer split into the portable `Renderer` (draw lists) and `D3D11Backend` (same check each time; the title screen,
map viewer and asset viewer were also captured through the game's shot hook). Then the window, input and sound moved
behind `Platform.h`, with a Win32 and an SDL3 implementation: `cmake -S modern -B out/modern-sdl -A x64
-DD2Next_PLATFORM=SDL` builds the SDL version (`Launch-Modern-SDL.cmd` runs it); it draws the twelve reference scenes
byte for byte like the Win32 build. Win32 is still the default. Test hooks: `D2Next_AUDIO_SELFTEST=1` plays a click and
the town music and reports what is playing; `D2Next_AUDIO_SILENT=1` (SDL) plays at zero volume. Linux uses the same SDL + OpenGL ES path (`Build-Linux.sh`, [LINUX.md](LINUX.md)). The web build is wasm64
WebGL 2 through Emscripten (`modern/web/build-web.sh`, [web/README.md](web/README.md)): the HTML page mounts MPQs
into the virtual filesystem, and `Main.cpp` yields with `emscripten_sleep` so the browser stays responsive.

### The game is linked in (2026-09-27)

D2MOO's game code (D2Game, D2Common, Fog, Storm, D2Lang, D2CMP, D2Hell) is built as static libraries with the viewer and
linked into `D2Next.exe`: no DLLs, no `NativeGameHost.exe`, no pipes. `Build-Modern.cmd` builds it (the first build
compiles D2MOO too), and `--native` starts the game with it (`Launch-Modern-NativeGame.cmd`, `Launch-Modern-NativeTown.cmd`
and `Launch-Modern-SDL.cmd` use it; `Build-X64.cmd` is no longer needed to play). D2Net is replaced by the stand-in
transport of the tests (`tools/x64/MockD2Net.cpp`, built static with `MOCKD2NET_STATIC`).

- The game server is `NativeSession`, called directly each tick. The level loader shares the archives and data tables
  the game loads (`useGameData`, once per program) and only allocates and frees its own act.
- Leaving the game closes it (`GAME_CloseAllGames`), so a later session in the same program starts clean; the save test
  runs two sessions one after the other this way.
- The program ends without global destructors (`endProgram`), after the player has left the game and the character is
  saved: D2MOO keeps its state for the whole program, as it did in the helper process, which also ended with `ExitProcess`.
- A crash prints its call stack (`installCrashReport`, names where the build has symbols; build `RelWithDebInfo` for them).
- The DLL build stays for checking the x64 DLLs against the original ones: configure with `-DD2Next_INPROCESS=OFF`
  (e.g. `out/modern-dev`) and run with `--native-town DLL_DIR`. Both builds draw the twelve reference scenes byte for
  byte alike, the native tests pass in both (the level test needs a new level-1 Paladin), and a session saves the same
  character (only the checksum and the time played differ). Startup is about a second faster linked in.
- Two D2Game bugs showed up in-process and are fixed for the DLLs too: `GAME_UpdateProgress` read a debug switch past
  the end of the game (the original's task block, which x64 does not allocate), and values 1, 2 or 4 there crashed on
  purpose (in-process that happened in about one run in ten); and the unit lookup the host used (#10025) always looks
  in the first game slot, so the host now uses `D2GameHost_GetUnit(gameId, type, guid)`.

This is a map, asset, and tile viewer with a movement prototype, not a playable remake. It reads real
game archives and displays DS1 maps, DC6 frames, or DT1 tile atlases using Direct3D 11.
No original game DLLs, injection,
DirectDraw wrapper, or administrator access is required to run it.

Run `Build-Modern.cmd` from the project root, then `Launch-Modern.cmd`.
The launcher opens the Rogue Encampment's `TownN1.ds1` layout at native pixel scale
with an animated Sorceress. Right-click a reachable floor to walk there, or hold
WASD to move in screen directions. The camera follows her; C toggles collision (red
blocked, green walkable), and F re-enables following after you drag to pan. Mouse
wheel zooms, left-button dragging pans, and R resets the camera. Esc opens the
original-style game menu with Options, Save and Exit Game, and Return to Game.
Maps use shared GPU tile textures and camera culling; the title reports visible
tile placements. `--zoom 1` starts at native pixel scale. `--reference-map` selects
the earlier CPU compositor for comparison.
Direct executable runs enable movement with `--map --walk`; `--collision` also
starts with the collision overlay visible. `--map` alone retains the static view.

Movement runs in `d2sim` (`src/Simulation.h`), which has no graphics or Windows
dependencies. It ticks at a fixed 25 Hz with 16.16 fixed-point positions and takes
serialisable commands (`MoveTo`, `Hold`, `Stop`), so the same command list always
gives the same state; `SimulationTests` verifies this with per-tick state hashes.
The viewer runs whole ticks from a time accumulator and interpolates the remainder,
so speed does not depend on frame rate, and the simulation pauses while the window
is unfocused. Collision uses DT1 wall/no-player flags and an eight-neighbor
breadth-first route search; missing floors and map boundaries are blocked and
diagonal steps cannot cut blocked corners. Walk speed (6 subtiles/second) is an
approximation. There is no unit footprint, dynamic objects/doors, DS1 special
collision overrides, or original-game pathfinding parity yet. The character is
drawn among the tiles by footprint depth ordering (`src/Occlusion.h`): tiles in
front of her are drawn over her, tiles behind her under her. Footprints come from
each tile's solid collision subtiles, with type-based rules for trees, roofs and
tiles that have none; these rules have not been compared with the original game.
Roofs are not hidden when she is inside a building, and she has no shadow. The flat
green diamonds are special marker tiles that are still drawn. `--marker` uses the
yellow diamond instead of the sprite; the collision overlay draws above scenery.
`--place X Y` starts on a given walkable subtile and `--map-stats` prints tile
orientation counts and solid footprints, both for inspecting depth ordering.

Pass `--tiles` to return to the Act I floor atlas, with 16 tiles per page.
Left/Right select atlas pages or individual frames/tiles. The window title shows
the current page. Atlas cells are separate previews; `--map` displays map placement.

Running the executable without arguments still opens the hand-axe inventory
sprite. Use `--asset` for another DC6 or DT1 file. DT1 files default to atlas mode;
`--single-tile` lets Left/Right inspect individual tiles with their
main/sub/orientation IDs in the title.

```powershell
cmake -S modern -B out/modern -G "Visual Studio 18 2026" -A x64
cmake --build out/modern --config Release --parallel 6
ctest --test-dir out/modern -C Release --output-on-failure
out/modern/Release/D2Next.exe --verify-assets
out/modern/Release/D2Next.exe --software --smoke-test --capture out/modern/smoke.bmp
out/modern/Release/D2Next.exe --asset 'data\global\items\invcap.dc6'
out/modern/Release/D2Next.exe --tiles
out/modern/Release/D2Next.exe --tiles --verify-assets
out/modern/Release/D2Next.exe --tiles --software --smoke-test --capture out/modern/tiles.bmp
out/modern/Release/D2Next.exe --asset 'data\global\tiles\act1\town\fence.dt1' --single-tile
out/modern/Release/D2Next.exe --map
out/modern/Release/D2Next.exe --map --verify-assets
out/modern/Release/D2Next.exe --map --software --smoke-test --capture out/modern/map.bmp
out/modern/Release/D2Next.exe --asset 'data\global\tiles\act1\town\towne1.ds1'
```

Put the user's compatible Diablo II 1.10f archives in the `MPQ` folder beside
`D2Next.exe`. Development launches also check the project root's `MPQ` folder,
so `Launch-Modern.cmd` needs no asset-directory argument. Use `--game-dir PATH`
to explicitly select another installation. Archive precedence is the D2SE 1.10f
patch (or the root patch when no such core exists), expansion, base data,
characters, sound. The caller is responsible for supplying compatible archives.

Player `.d2s` files live in the local `Save` folder. Copy an existing compatible
character there to import it. `Launch-Modern-NativeGame.cmd` discovers local
characters automatically, shows them on character selection, and saves progress
back to the local copy. Use `--profile PATH` to select a different save folder.
`--lan-host [PORT]` opens the game to other players on the network and `--lan-join ADDRESS[:PORT]` joins one;
see [LAN.md](LAN.md).

The implementation supports DC6 version 6, DT1 version 7.6 (raw isometric and
RLE blocks), and the Act I PAL.DAT palette. DT1 tile IDs, rarity, 25 collision flags,
and signed block-coordinate origins are retained for future map placement. Tile
preview bounds come from the block coordinates; they are not DS1 scene anchors.
Files, tile counts, block ranges, RLE runs, decoded image sizes, and atlas sizes
are checked before use. Atlas pages exceeding the size budget require individual
tile mode.

DS1 layout versions 4–18 are parsed with checked dimensions, dependency strings,
layer counts, and object-record ranges. Editor `.tg1` dependencies are normalized
to archive `.dt1` paths. Maps select the act palette automatically. Floors,
shadows, and walls are composited in passes with diagonal ordering for walls.
The viewer uses the first matching tile variant deterministically, not the original
seeded rarity selection. The console reports resolved and missing references;
`--verify-assets` fails if a referenced tile cannot be resolved.

This is a static inspection implementation: each unique tile is uploaded once,
and visible placements are drawn in their original order. Camera culling tests
tile image bounds, including tall walls, rather than only their cell anchors.
Cell coordinates and tile collision flags remain available for future movement.
Shadows use stencil coverage to darken only previously drawn map pixels.
Limits are 128x128 cells and 256 MiB decoded tile cache. Only `--reference-map`
allocates the composed map image (limited to 128 MiB). Rendering currently uses
one draw per visible placement; atlas instancing and spatial chunk indexing
are future optimizations. NPCs, object sprites, water/palette animation, DS1 groups and NPC
paths, exact roof/wall occlusion, and gameplay are not implemented. Some marker
tiles and unanimated water colors remain visible. Object records are counted and
range-checked but not spawned. Optional data after the object section is ignored.

The viewer decodes DCC/COF Sorceress animations and runs the walking simulation
through the command/snapshot interface described in [BACKEND-INTEGRATION.md](BACKEND-INTEGRATION.md).
`Launch-Modern-NativeGame.cmd` now discovers characters in the local `Save`
folder and joins the selected character in a separate x64 D2Game host.
Right-click and WASD use native walking; character/equipment rendering is pending.
Progress is saved atomically to the local player file. Only Act I town is supported.
See [BACKEND-INTEGRATION.md](BACKEND-INTEGRATION.md) for commands and limits.
Tiled menus are not implemented.
`Launch-Modern-NativeTown.cmd` now previews a seeded Act I town generated by the
repository's x64 DLLs, with native static collision and prototype walking.
Use `--seed 0xbeef` to try another seed. (Linked-in build: `--native`; the DLL build takes `--native-town DLL_DIR`.)
DC6/DT1 inspection still uses the Act I palette.
Frame placement offsets are intentionally unused in the centered image inspector;
they must be retained when an animation/scene format is introduced.

`--smoke-test` renders in a hidden window, reads back the output, and fails if the
frame contains only background. `--software` selects WARP for repeatable tests.
With `--walk`, the smoke test also routes from a synthetic screen click, runs the
simulation until the character arrives, and verifies that an outside-map click is
rejected and leaves her still.
Ordinary startup attempts hardware and falls back to WARP if device creation fails.
Device removal produces a clear error; automatic device recreation is future work.

StormLib is statically built from the source under `external/StormLib`, pinned to
commit `44ebfbfc109d76e2a85bbd5d8b0c949df7e65c6f` from
https://github.com/ladislav-zezula/StormLib. Its license is retained there. To restore:

```powershell
git clone https://github.com/ladislav-zezula/StormLib.git modern/external/StormLib
git -C modern/external/StormLib checkout 44ebfbfc109d76e2a85bbd5d8b0c949df7e65c6f
```

The modernization roadmap and acceptance gates are in `../MODERNIZATION-PLAN.md`.
