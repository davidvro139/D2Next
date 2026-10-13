# LAN multiplayer

Started 2026-10-04. Several players in one game over a local network: one machine hosts the game (D2MOO's server runs
there), the others join it with their own characters.

## Playing

From the menus, as in the original game (`Launch-Modern-SDL.cmd` or `Launch-Modern-NativeGame.cmd`):

1. Main menu: **OTHER MULTIPLAYER**, then **TCP/IP GAME**. The TCP/IP screen shows this computer's address
   ("Your IP Address is:").
2. Host: **HOST GAME**, then pick a character. The game is open to others on TCP port 4000, the port Diablo II's own
   TCP/IP games use (1.10f D2Net.dll passes 4000 to `htons`).
3. Guest: **JOIN GAME**, type the host's address (as its TCP/IP screen shows it), **OK**, then pick one of your own
   characters. The address is remembered for next time (`LastTcpIp.txt` in the save folder; the original kept it in the
   registry). `address:port` reaches a host on another port.
4. A host that cannot be reached brings back the TCP/IP screen with the game's "Cannot Connect to Server".

The same from the command line (no menus with `--skip-menu` and the scripted runs):

```
Launch-Modern-SDL.cmd --lan-host          (port 4000)
Launch-Modern-SDL.cmd --lan-host 4001     (another port)
Launch-Modern-SDL.cmd --lan-join 192.168.1.20
Launch-Modern-SDL.cmd --lan-join 192.168.1.20:4001
```

Windows asks the host to let D2Next through the firewall the first time it listens; it must be allowed on the
private network. A TCP/IP game never pauses, not in the background and not behind the game menu (single player pauses
in both). When a guest quits, its character is saved by the host's game and sent back to the guest's own save folder;
nothing of the guest's is written on the host.

Tested on one machine (two copies of the game, see Testing); a game between two computers has not been played yet.

## Android

The APK has the same menus and asks for the network at install (`INTERNET`, `ACCESS_NETWORK_STATE` in
android/AndroidManifest.xml; no prompt). A phone can host or join, with a PC or another phone, on the same Wi-Fi (not
mobile data, and not a guest network that keeps devices apart). The join box opens the phone's keyboard; Back is
CANCEL.

During a TCP/IP game the phone holds Android's low-latency Wi-Fi lock (`WAKE_LOCK` permission; Window::
lowLatencyNetwork in src/PlatformSdl.cpp): without it the Wi-Fi's power saving holds packets back and delivers them in
bursts.

Every 10 seconds a guest writes a line to D2Next.log (in the app's folder on a phone), for example
`LAN: last 10 s: 0 ticks waited for the host, 0 snapshots caught up, at most 1 waiting, 0 now`:

- **ticks waited**: ticks with no snapshot to play (the game held still). Many = the Wi-Fi delivers late or in bursts.
- **caught up**: snapshots played faster to get back to the host's present (after joining, or a pause).
- **at most / now waiting**: each snapshot waiting is 40 ms behind the host. Steady at 0-1 is as close as it gets.

First phone test (2026-10-04): slow or laggy in multiplayer. Changed after it: a guest used to stop each tick until the
host's next snapshot came (on Wi-Fi that froze it until each burst, then jumped); it now plays from what has arrived,
the Wi-Fi lock is held, and a bug that could fail a good connection on Windows and Android (the "in progress" error
read after it had been overwritten) is fixed.

Things to watch for on a phone:

- A phone that leaves the game app (home screen, a call, the screen off) stops running it, as Android does. If it was
  the host, the guests' games stop and, after 4 seconds without a snapshot, go back to the TCP/IP screen with "The
  connection to the host was lost". If it was a guest, the host goes on; the guest catches up when it comes back, or,
  if it was away long enough to fill the connection, the host lets it go after 10 seconds without taking anything.
- The PC and phone builds must be the same version (the hello is "D2L2"; an older build is refused).

## The screens

Laid out from D2Launch.dll (1.10f), whose screens are tables of controls (type, x, bottom y, width, height, hotkey,
string id, image, handler...; 6FA36670-6FA369F0 for these):

- OTHER MULTIPLAYER: OPEN BATTLE.NET (disabled here), TCP/IP GAME and CANCEL on the main menu's background. The main
  menu's OTHER MULTIPLAYER button moved to where D2Launch puts it (below the Battle.net gateway button, which is not
  drawn).
- TCP/IP screen (`tcpipscreen`): "TCP/IP Options" (string 5117, font42), "Your IP Address is:" (5121) and the address
  (font16), HOST GAME and JOIN GAME, and while one of them is under the mouse its description (5122/5123,
  fontformal11). With no address the text is "Cannot detect a valid TCP/IP address." (5124) and both buttons are off,
  as in the original. CANCEL (Escape) goes back to OTHER MULTIPLAYER.
- JOIN GAME's popup (`popupokcancel2`, `ipaddressbox`, `cancelbuttonblank`): "Enter Host IP Address to Join Game"
  (5120) over the address box; OK is Enter, CANCEL Escape. An address that names no computer leaves the popup open, as
  D2Launch does.
- Character selection's EXIT goes back to the TCP/IP screen for a TCP/IP game.

The address shown is the one of the network card that carries the computer's traffic (found with a UDP socket
"connected" to an outside address; nothing is sent), so virtual adapters (VPNs, virtual machines) are not shown.

## How it works

- **One game, several clients.** The host's `NativeSession` owns the D2Game game. Its own player is client 1; each
  guest joins as client 2, 3... the way D2Game takes a Battle.net or TCP/IP player: a join packet (0x67), then the
  character's save through the database callback (#10007), then the enter-game packet (0x6a). The mock D2Net carries a
  client id with each packet so each client's messages stay its own.
- **Requests and snapshots.** A guest sends the same `native::Request` the single-player game uses (move, skill,
  item...) and gets the same `native::Reply` snapshot back, described for its own player
  (`D2GameHost_GetClientPlayer`). The host's tick steps the game for everyone (25 per second), then sends each guest
  its snapshot. Other players appear in the snapshot's units with the `unitPlayer` flag and their class, look, mode,
  sequence frame and shape-shift form, and are drawn with player art.
- **Connection** (`modern/src/Lan.cpp`, TCP, Winsock or POSIX sockets): the guest sends "D2L2", the protocol magic and
  its save; the host answers with the client number. After that the host sends frames `{kind, size}`: 1 = a snapshot
  as its change from the one before (XOR with the previous snapshot, runs of unchanged and changed bytes; standing in
  town that is 73 bytes a snapshot instead of 72 KB), 2 = the guest's saved character as it leaves.
- **A guest never waits on the network.** It takes in whatever has arrived and plays one snapshot a tick; a tick with
  none holds still (which leaves one more waiting from then on: the delay grows by itself to what the bursts need),
  and a cushion that went unused for 2 seconds is played through at once. On one PC over 30 seconds: no waits, at most
  1 snapshot waiting.
- **Never waiting on a guest.** The host queues what each guest is sent and hands it to the connection as it takes it;
  a guest that takes nothing for 10 seconds is let go. A guest connects in the background while its loading screen is
  drawn (a phone that draws nothing for 5 seconds is reported as not responding), and a host that sends nothing for 4
  seconds is treated as gone: back to the TCP/IP screen with "The connection to the host was lost" (this message is
  D2Next's own; the game has none for it). A guest that closes without leaving is taken out of the game; its
  progress since joining is not sent back.
- **Saving.** Each character is saved only to its own file: the host's save callback writes the host's character to
  the host's profile, and keeps a guest's save in memory for that guest. As a guest leaves, the host saves it with
  D2Game's own writer (`D2GameHost_SaveClient` → `D2GAME_SAVE_WriteFile_6FC8A500`, what D2Game does in multiplayer
  games when a player leaves). D2Game compares the new save with the last one and relocks instead of saving when the
  character did not change; the host then sends back that last save (the one D2Game found unchanged). The guest
  writes it beside its save and moves it over, so a failure leaves the old save whole.

## Testing

The menus: two copies of the SDL build on one machine, driven by a script (`D2Next_SHOT_DIR` and window messages:
WM_APP+7 a screenshot, WM_APP+8 a left button event, WM_APP+9 a key, WM_APP+10 a typed character; SDL ignores posted
mouse messages). Checked 2026-10-04: host and join through the screens, both players see each other walking, closing
the guest's window sends its character back to its save folder, and a join with no host gives "Cannot Connect to
Server". Also checked: a guest frozen for 15 seconds (process suspended) catches up and plays on while the host never
stops, and stopping the host brings the guest back to the TCP/IP screen with "The connection to the host was lost".

Two headless processes on one machine (`out/modern-gl/Release`, copies of saves only):

```
D2Next_LAN_TICKS=450 ./D2Next.exe --native --smoke-test --no-audio --native-save host.d2s --lan-host 6115 --capture host.bmp
D2Next_TEST_SAVE=1 D2Next_RIGHT_SKILL=149 D2Next_WALK_TO=8,0 D2Next_LAN_TICKS=50 ./D2Next.exe --native --smoke-test --no-audio --native-save guest.d2s --profile gp --lan-join 127.0.0.1:6115 --capture guest.bmp
```

`D2Next_LAN_TICKS` keeps a smoke test in the game that many ticks (40 ms each) and reports the other players it saw;
`D2Next_TEST_SAVE` with `--profile` lets a smoke test save into that folder.

Checked 2026-10-04:

- the guest joins as client 2; each player sees the other (the guest's capture shows the host's Paladin) and the
  guest's walking is seen on the host;
- an unchanged guest gets back its save byte for byte (D2Game relocks);
- a changed guest (right skill set to Battle Orders) gets back a new save from D2Game's writer: byte 0x7C is 149, the
  checksum and time changed, and it loads and plays in single player;
- single player is unchanged: the regression scenes match, ctest passes, the waypoint click test opens 8/8, and the
  APK builds (Lan.cpp's POSIX branch compiles for Android).

## Not done yet

- Player names over heads, the party screen, trading, chat.
- OPEN BATTLE.NET and finding games on the network (the original TCP/IP games are joined by address only too).
- Smaller snapshots (each is a full `native::Reply`, sent 25 times a second; fine on a LAN, not over the internet).
- A WebSocket transport for the web build (the wasm page hides TCP/IP; browsers have no raw TCP); the out-of-process game host (`NativeGameHost.exe`) does not host LAN games.
- Interactive testing on two machines, and with three or more players.

## After the second phone test (2026-10-04)

- **Your own character answers at once (a guest).** The original client moved the player's unit itself before the
  server answered. A guest now shows its character walking the moment a walk is asked for (NativeGame::updateShown),
  at the speed the host has been moving it, at most as far ahead as the host's answers take (both measured), then
  drawn back to (or put at) where the host has it. Only what is drawn moves ahead; requests and pathing use the host's
  position. The first walk after joining is not shown ahead (the speed is not known yet). `D2Next_SHOWN_LOG=1`
  prints the shown and host positions each tick; on one PC the shown character started a tick before the host's.
- **Other players' Charge, Leap, Whirlwind...** Each player in a snapshot now carries what its sequence draws (the
  table's mode and frame), the skill it uses and its path type (native::Unit, protocol v8), and is drawn as the
  player's own character is: Leap's jump and landing frames, Charge's run with its trail, Whirlwind's turned copies.
  Checked with a guest leaping and charging while the host watched (`D2Next_LAN_STOP_SEQUENCE=N` stops the host's
  `D2Next_LAN_TICKS` run N ticks into another player's sequence, for the capture).
- **Sounds near this player only.** Monster hit and death sounds and missiles' travel, hit and impact sounds are
  heard only within 30 subtiles of this player's character, so another player's fight across the level is not heard.
- **Waypoints.** The default "every Normal waypoint known" (`--no-waypoints` turns it off) now applies to every player
  in the host's game, not only the host's own character.
- Scripted runs (smoke tests) set `D2Next_LAN_WAIT`: a scripted guest waits for each host tick, as its loops step
  the game faster than real time.
