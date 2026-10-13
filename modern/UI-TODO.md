# Original interface: what is left

The HUD, front end (title, main menu, character select, loading) and all the panels (inventory, character, skill tree,
skill list, quest log, waypoints, stash and cube, stores, hireling) use the game's own art and fonts. Below, item by
item, what was done and what still differs from the original game.

## Still open (2026-10-11)

Needing the original game side by side (positions and looks judged from the art or by eye):
- Store: the Repair button's place; whether the original draws empty store tabs; the Gamble window's title.
- Gold popup and quest log: their places and the buttons inside them.
- Main menu button spacing; the inventory's and character sheet's close buttons (placed in the art's empty boxes).
- NPC speech box and menu box; the death screen's placement off 800x600.
- Shadows' lean, wall fading (40% / 40 pixels), the player's light radius (11) and the darkness (15%).
- Whether an Assassin's starting buckler is drawn with one weapon; champions' colours.

Needing more of the original client's code read:
- Floor tile animation speed (3 ticks a frame is an estimate).
- Hit cries by damage (soft/hard; here they take turns); skills.txt dosound/tgtsound.
- Weapon skills' damage bonuses kept in skills.txt Param (Bash, Sacrifice, Zeal...) on the character sheet.
- Act V NPCs' special introductions; the "welcome back" greeting; the quest log's "!" (questlast).
- Footsteps by armour and floor; lighting by +light radius items (the host doesn't send it).

Not built yet:
- Trade between players (LAN); other players' portraits in a party; the hireling roster's tooltips.
- Automap: the mini map, party, NPC and corpse markers, moving the map.
- Cain's identify was never tried (he joins the camp only after his quest).
- Hover lighting of monsters and town people (objects are lit).

## Missing game data in the panels

- [x] **Damage, Attack Rating, Defense and resistances** (2026-09-27): the host works them out by the server's own rules
  (`native::Sheet`): the damage of D2Game's melee roll without the dice (weapon or bare hands, two-handed damage when
  wielded with both hands, +damage, damage percent with the weapon's strength/dexterity bonus and mastery, min/max damage
  percent), D2Common's attack rating raised by the mastery's and items' to-hit percent, D2Common's defense, and the four
  resistances less 40 (Nightmare) or 100 (Hell), capped at 75 plus max resistance. Checked: a new Sorceress 1-5 (short
  staff, 10 strength), a 1023-strength Paladin 22-78 with a short sword.
- [x] **Damage and Attack Rating per skill** (2026-09-27): the first rows are the left skill's, the second the right
  skill's (labelled with its name): the weapon's share (skills.txt `SrcDam`), the skill's damage percent (`DmgSymPerCalc`,
  D2Common's formula evaluator) and its to-hit factor (`SKILLS_GetToHitFactor`). A skill that doesn't strike with the weapon
  leaves its rows empty. `D2MODERN_RIGHT_SKILL=ID` picks the right skill for a capture.
- [ ] **Skill damage bonuses in parameters**: many weapon skills keep their damage bonus in skills.txt `Param` columns that
  only their own server code applies (Bash, Sacrifice, Zeal...); the rows don't include those yet (the original client
  must know them per skill).
- [x] **Attribute colours** (2026-09-27): an attribute above its base value (items) is blue, below it red.
- [x] **Sheet colours by state** (2026-10-11): D2Client (0x6FAD0B99-0x6FAD0D3A) colours damage, attack rating, defense and
  the resistances by the player's states, not by items: blue while a state has the value's states.txt column attblue,
  damblue, armblue, rfblue, rlblue, rcblue or rpblue (an aura, a buff), red with the matching "red" column (a curse);
  defense also blue with a shield on and Holy Shield (state 101). A resistance at its cap shows the cap in gold (colour 4)
  unless blue, below zero red; the cap is 75 + max resistance but never above 95, the value never below -100 (the host
  now clamps both). Checked: the Paladin's Resist Fire aura turns Fire Resistance blue. Max life, mana and stamina are
  blue above and red below their value without items (fields 7, 9, 11; protocol v19 `Sheet::baseMax`).
- [x] **Experience past 2^31** (2026-09-27) was shown negative; it is read unsigned now. At level 99 Next Level and the
  experience bar are left empty (compare the original).

## Panel features drawn but not working

- [x] **Weapon swap** (2026-09-26): the W key and the I / II tabs above the weapon boxes send the swap packet (0x60).
- [x] **Active weapon tab** (2026-10-11): packet 0x97 flips the set in use (D2Client 0x6FAED280, flag 0x6FBB594C); on set
  II `panel\invchar6Tab` frames 1 and 0 are drawn over the weapon boxes (5 left, bottom 4 below; 6FADF670) with tab II
  raised. Empty hands show the `panel\inv_weapons` silhouette (bottom 2 above the box, 4 right and up on set II), not
  beside a two-handed weapon. The tab click areas (18 pixels above each weapon box) are estimated from the art.
  `D2MODERN_DUMP_DC6="archive path;prefix"` writes a DC6's frames as BMPs.
- [x] **Store buttons** (2026-09-26): Buy (buysellbtn 2/3) and Sell (4/5) in the boxes left of the close button, the
  mode in use shown pressed; in sell mode a click on an inventory item sells it (packet 0x33, item mode 0). The smiths
  (Charsi, Fara, Hratli, Halbu, Larzuk) also get Repair All (18/19, packet 0x35 with a negative flag). Deckard Cain's
  menu has "Identify Items" (packet 0x34).
- [x] **Store clicks** (2026-09-26): the panel's own click region came first and swallowed every click, so the menu
  buttons, tabs and buying by click did nothing. Fixed; selling a scroll to Charsi was checked.
- [x] **Repairing one item** (2026-10-10): the smiths' Repair button (buysellbtn 6/7, left of Buy) turns Repair mode on
  and off; a click on a carried or worn item then sends 0x35 with that item (Repair All sends -1). Checked with worn-down
  gear (host cheat 10, `D2MODERN_TEST_REPAIR=1` with `--talk 154 --shop`): one item, then the rest, each paid (0x2A code 2).
- [x] **Store cursors** (2026-10-10): while a store is open the cursor is `cursor\buysell` in the mode of its buttons,
  as D2Client's button records give it (0x6FAE91C3: Buy frame 3, Sell 4, Repair 1, the hammer; set through 0x6FB57580).
  Fixed 2026-10-10: a store opens with no button chosen and the normal hand (0x6FAE95B0 clears them; a click on an item
  still buys); the cursor and the pressed button change only once Buy, Sell or Repair is clicked.
  Hung from the pointer like the hand (top left at the hot spot). `D2MODERN_CURSOR_LOG=1` draws it into a capture.
- [x] **Store button hints** (2026-10-11): hovering Buy, Sell, Repair, Repair All or Close shows the name and what a
  click does over the button (not on touch screens, where the buttons are labeled). Repair All does not show its cost yet.
- [ ] **Store details**: the Repair button's place
  (x 143) is a guess; Cain's identify was not tried (he
  isn't in town until his quest is done).
- [ ] **Gamble** uses the same store panel but has no title (1.10 has no refresh button: buysellbtn holds buy, sell, repair,
  "?", cancel, arrows, a tick, repair all and a struck-out frame, nothing to refresh with).
- [ ] **Store tabs** for pages the NPC has no items on are hidden; check whether the original draws them.
- [x] **Gold** (2026-09-27): the coin (`panel\goldcoinbtn`) in the inventory's small box at (483,453) drops gold, or
  deposits it while the stash is open; the stash's coin at (153,82) withdraws. Both open the gold popup
  (`bigmenu\popupok` in the middle of the screen) with the game's question (strings 4033/4049/4050), the amount (the
  most allowed: 10000 per level carried, the stash limit of D2Common) and accept/cancel (`menu\goldbtn` 0/1, 2/3);
  digits type an amount, Backspace, Enter and Esc work. Dropping sends 0x50 (player number, amount), the stash 0x4F
  (button 20 deposit / 19 withdraw, amount's high and low 16 bits). Checked: 500 dropped (gold on the ground), deposited
  and withdrawn again.
- [ ] **Gold popup details**: its place and the button places inside it are estimates (the original may centre it over
  the panel); the original also has an up-counting button hold; the amount can't be edited with the mouse.
- [x] **Stash and Horadric Cube** (2026-09-26): the game opens them with packet 0x77 (16 stash, 17 closes it, 21 cube);
  clicking the town stash or using (right-clicking) the cube. The left panel shows `panel\tradestash` with its 6 by 8
  grid at (154,142) and the stash's gold, or `panel\supertransmogrifier` with its 3 by 4 grid at (198,199) and the
  Transmute button (`panel\miniconvert`, packet 0x4F button 24). Items are the player's items on page 4 (stash) or 3
  (cube), placed with packet 0x18 and that page. Close (and Esc) send button 18 or 23. Moving a scroll into the stash by
  clicking was checked. Transmuting checked 2026-10-10 with `D2MODERN_TEST_CUBE`: 3 chipped amethysts give a flawed
  amethyst, 3 El runes an Eld, a potion and a gem (no recipe) stay as they are. The server sends sound event 4 (packet
  0x2C) when a recipe makes something: `cursor\convert.wav`. D2MOO sent event 19 (the character's "impossible") for every
  used item that is not a quest item, so opening the cube said it; the original (D2Game 0x6FC483A9) returns quietly
  (fixed in ItemMode.cpp). Event 19 now plays `<class>_impossible_1/2` instead of questdone.
- [x] **Stash gold** (2026-09-27): see Gold above.
- [x] **Stash clicks** (2026-09-27): the stash's close button, its grid and the cube's transmute button did nothing: the
  panel's own click region came first in the list and took every click (the store had the same bug). Closing the stash now
  also closes the inventory it opened with (the cube leaves the inventory open). Checked: a scroll moved into the stash by
  clicking, the close button closing both panels.
- [x] **Which units the mouse selects** (2026-10-11): only monsters and NPCs with monstats2.txt isSel set and noSel clear are
  named, lit up and clickable, so the camp's wandering Rogues (rogue1/rogue3, named "Dummy" = "an evil force"), townsfolk and
  critters are not. By the data, the Act II hireling (noSel) and the Act III one (no isSel) cannot be pointed at either.
  `D2MODERN_PICK_LOG=1` lists the units in view and whether they can be selected.
- [x] **Objects under the cursor** (2026-09-27): the object the cursor is on (the one in front) is lit up (its picture added
  onto itself, dimmed) and named above it in the game's words (objects.txt Name through the string tables: "Your Private
  Stash"). `D2MODERN_HOVER_OBJECT=ROW` puts the cursor on an object for a capture.
- [ ] **Hover details**: the brightness of the lit-up object is an estimate; monsters and town people under the cursor are
  not lit up (the original outlines/brightens them too).
- [ ] **Trade** panel (player to player) is not built; there is only one player.

## Small layout issues

- [x] **Two-line labels** (2026-10-11): the resistance labels are two lines in the game's tables ("Lightning" /
  "Resistance", strings 4071-4074); they were joined into one, which ran past its box. Now the two lines in the largest font that fits
  the box (font6), 8 apart from the box's top. The classic character sheet uses `panel\invchar` too.
- [x] **String ids** (2026-10-02): the string table numbered expansionstring.tbl from 10000 and patchstring.tbl from
  20000; the game (D2Lang) numbers patch strings from 10000 and expansion strings from 20000. Ids the game sends (monster
  names, NPC lines, quest text) above 10000 found the wrong text. Fixed; the ids written in the code that had been looked
  up the old way moved up 10000 (monster abilities 22472-22475; front end 22731, 22744, 22518, 22519, 21882).
- [x] Druid and Assassin skill tab names (2026-10-02) come from expansionstring.tbl StrSklTree26-34 (22509-22517); they
  were missing only because of the id numbering above.
- [x] **The skill list** (2026-10-11), as D2Client 0x6FB19720 lays it out at 800x600: rows 0-4 from the bottom, each of the
  player's skills (in its own list order) in the row of its skilldesc.txt ListRow (0 general, 1-3 the class's pages; -1
  never), skills from items (charges, scrolls, books) in row 4; one per ListPool; the left list only skills.txt leftskill
  skills; no passives. The left list runs right from x 80, the right list left from x 672, 48 apart; the lowest row's icons
  stand from y 466 to 514 and each further row with a skill 48 higher. Checked on the Paladin: Attack/Throw/Unsummon, the
  three pages, the books on top.
- [ ] The main menu button spacing is estimated. Compare it against the original client.
- [x] **The character sheet's close button** (2026-10-11): it had none; now in the empty box at the bottom of its art (gold
  frame 207-240 by 449-482, the button at 208,449), closing the sheet. Every window with a close box has its button: the
  inventory, character sheet, skill tree, quest log, waypoints, stash and cube, stores, the hireling's panel.
- [ ] The inventory's close button position is inferred from the empty box in the art. Confirm it against the original.

## Still in the old plain style

- [x] **In-game text in the game's font** (2026-09-26): item names on the ground, tooltips, labels and the NPC menus use
  font16 (Segoe UI only if the fonts fail to load). Item names have the game's colours (grey low quality, blue magic,
  green set, yellow rare, gold unique, orange crafted) over a see-through black box.
- [x] **NPC talk menu** (2026-09-26): the name and Talk / Trade / Gamble / Cancel as text over a see-through box, the
  choice under the cursor in gold.
- [x] **NPC menu placement and speech** (2026-09-27): the menu is over the NPC's head, in the game's words (talk 3381,
  trade 3396, trade/repair 3334 at the smiths, gamble 3398, cancel 3400). Talk plays one message, as the game does: the
  NPC's voice (the client's own table from line to sounds.txt row, 864 pairs read from D2Client 1.10f, `NpcSpeech.h`; the
  files are in d2speech.mpq / d2xtalk.mpq, now mounted, with d2xmusic.mpq for Act V's music) while the text scrolls up
  through a box for as long as it speaks; then (or on a click) it counts as heard and the menu is back. The next Talk
  plays the next message.
- [x] **Hiring mercenaries** (2026-10-10): Kashya, Greiz, Asheara and Qual-Kehk get "hire" (string 3397). The game sends
  the offers as the talk begins (0x4F clears, 0x4E name string and seed each); the host works out what D2Client shows
  (0x6FAF5028: MONSTERS_HirelingInit #11082 in the act #11086 gives, for this player) and sends them (protocol v15). The
  Hire page lists each: the name, "Lvl / Life / Def / Cost" (3368, 3366, 3367, 3369; red when the player cannot pay) and
  the special (hiredesc through D2Common's table: "Fire Arrow"); "There are no Mercenaries left to hire." (3365) when
  none. A click sends 0x36 (9 bytes: the NPC, the name as a dword; D2Client 0x6FAF56D0). Checked: Paige hired for 195
  gold, she appears and follows. D2MOO rolled the level-ups signed (-4 to 4); the original divides unsigned (0x6FDA4CBA),
  fixed in Monsters.cpp, with the same fix for missiles' critical / deadly strike / mastery rolls (0x6FDBAE27) and
  SEED_RollPercentage (the low 32 bits). `D2MODERN_HIRE=N` with `--talk 150` lists the offers and hires the Nth.
- [x] **Overhead messages** (2026-10-10): a 0x27 with one entry of type 3 is a line the unit says over its head (D2Client
  0x6FB1DE35; the new hireling's "I feel much stronger now"), not a conversation; drawn in white over its head for 4 s.
  0x2A codes 0 / 2 / 5 (bought, repaired, hired) show no message.
- [x] **Pet roster** (2026-10-10): the player's pets (0x7A: 1 comes with its pettype row and class, 0 goes; 0x81 the
  hireling with its name) along the top left as D2Client 0x6FADAB00 draws them: from x 15, 56 apart, the
  `ui\hireables` icon (pettype.txt baseicon, or by monster class) standing on y 60, the life bar above (46 wide, y 14-19:
  green, yellow under half, red under a quarter, black the rest) and the name below (the hireling's own, or pettype.txt
  name; the number for a kind counted together, the skeletons). Hidden while a left panel is open. Checked: Paige, the
  Clay Golem, the Valkyrie.
- [x] **Resurrecting the hireling** (2026-10-10): when it dies the game sends 0x9B (its name, the gold) and takes it off
  the roster (0x7A 0); the hiring NPCs and Tyrael then offer "Resurrect %s: %d" (22696), which sends 0x62. Checked: Paige
  killed (host cheat 9, `D2MODERN_HIRE_KILL=1`) and brought back for 187 gold; 0x81 puts her back, 0x9B -1 ends the offer.
- [x] **The hireling's panel** (2026-10-10): the O key or a click on its portrait opens `panel\npcinv` at (80,60) with the
  inventory (D2Client 0x6FAED600). Its name and life, then the labels (table 0x6FB6F8D0: Experience, Level, Next Level,
  Strength, Dexterity, Damage, Defense, the four resistances, Life) and values (0x6FB795F8: right against their box,
  font6 when wider than it; blue when raised, red when lowered) at the original's places, which are the panel's own
  coordinates with D2Win's text baselines. The host sends the hireling's stats (protocol v16) and what it wears (its
  items carry it as owner); damage as D2Client 0x6FAEDB97 works it out (base plus secondary damage, the percent from
  damage%, the weapon's strength / dexterity bonuses and mastery, or dexterity for the rogues and strength for the others
  without one). What it wears sits in inventory.txt Hireling2's boxes (head, body, right and left arm); a click there
  sends 0x61 with the slot (an item onto it from the cursor, or its item to the cursor). Checked: a cap onto Paige's head,
  her sheet "level 5, 15750/26460, 37 str, 49 dex, 63/63 life, 36 def, 1-4 damage". `D2MODERN_HIRE_PANEL=1` with
  `D2MODERN_TEST_GIVE=cap` and `D2MODERN_HIRE=2 --talk 150`.
- [x] **Hireling item tooltips** (2026-10-10): what it wears shows its tooltip like any item (the panel is now drawn
  before the tooltips). `D2MODERN_HOVER_ITEM=#id` also finds the hireling's items.
- [x] **Potions for the hireling** (2026-10-11): shift with a belt slot's right button or with keys 1-4 sends 0x26 with its
  "on the hireling" word set (D2Client 0x6FB0183B passes the message's MK_SHIFT); a potion held over the hireling's
  portrait is given to it (0x61, which D2Game makes it drink). Checked: shift+1 with a hireling hired, the belt potion
  gone. `D2MODERN_CLICKS` takes keys as `kX` (`sk1` with shift).
- [ ] **Hireling details**: the roster's tooltips, other players' portraits in a LAN party (`Hireables\<Class>Icon`), and whether the
  hireling's portrait stays (greyed) while it is dead are not done.
- [x] **Quest topics** (2026-10-10): each 0x27 message carries its menu type (D2MessageStrc; the quests' message tables
  set it). Talk says types 0 and 2; type 2 is also its own menu entry under talk (D2Client 0x6FAF3FE8), captioned through
  the client's table from message to caption, usually the quest's name (0x6FB8D2A0, 527 pairs, `NpcTopics.h`; 3724
  otherwise). Choosing it plays that message, and hearing it sends 0x31 for it. Checked: after Akara's first message the
  next talk offers "Den of Evil" (line 65). `D2MODERN_TALK_LISTEN=1 D2MODERN_TALK_AGAIN=1` talks twice.
- [x] **NPC introduction, gossip and greetings** (2026-10-11), from D2Client's own tables (`NpcGossip.h`): the table at
  0x6FB7B978 (46 NPCs: class, lines of 15 bytes: string id, quest check, quest state, quest, player class 7 any; byte 0x14
  leaves out "Introduction"). The menu offers "Introduction" (3399) above Talk; it says line 1 when that is for the
  player's class, else line 0 (0x6FAF49C7). When the game has nothing for Talk (no 0x27 line), the NPC says its gossip:
  chosen once a game for every NPC at the first Talk (0x6FAF44C0: up to 10 random lines past the introductions, for the
  player's class, with the quest state the line needs; line 2 otherwise), not reported back as heard (0x31). The original
  draws from the player's seed, here from the game's own random. The quest states come from 0x28 (asked with 0x40 when a
  menu opens). Greetings (0x6FB464A0, records at 0x6FB91C54 by class): the first time an NPC's menu opens in a game, its
  greeting or half the time its "inactive" one, a time-of-day greeting one time in three (morning, day, evening by the
  act's cycle), then one of the sound's group (Akara: `aka_hello.wav`). The "welcome back" greeting (kind 2, set by a
  packet listing NPC classes, 0x6FAFC16B) is not used. `D2MODERN_TALK_LISTEN=1 D2MODERN_TALK_AGAIN=1 --talk N` prints the
  lines; `D2MODERN_STRINGS_GREP=regex` lists strings by key.
- [x] **Drag and drop** (2026-10-11): pressing on an item picks it up as a click does, and letting go more than 8 pixels away
  drops it there as a click would: onto the store (its goods too) sells it, onto the grid, the stash, the cube or a body
  slot places it; over the world it stays on the cursor (never dropped on the ground by a drag). Let go before the game
  has put it on the cursor, the drop waits for it (up to a second). Click and click still works as in the original. An
  item held over the store's goods now sells (it did nothing there before). `D2MODERN_CLICKS` takes drags as
  `dX1,Y1:X2,Y2`; the `--shop` hook now opens the store as the menu does (0x2F, then 0x38), without which the game
  refused to pick items up.
- [x] **Walking away from an NPC** (2026-10-11) closes the conversation (menu, speech or store) with the cancel the close
  button sends (0x30), 10 subtiles from the NPC or when the NPC leaves the client's view. The original has no distance
  check: D2Client's NPC handlers stop once the NPC is gone from its unit list (0x6FB269F0), D2Game keeps the interaction
  until the client cancels it and won't move a player busy in a store, and at 800x600 the store and speech cover the
  screen. With only the menu open the player can walk off (checked: Charsi's menu closes after a few steps away).
- [ ] **NPC details**: Cain's Identify caption is English; the speech box's place and the NPC menu's box are estimates; the
  Act V NPCs' special introductions (0x6FAF479A, classes 513-515 with quest states) are not done.
- [x] **Enemy name and life bar** (2026-09-26): the name in white over a see-through box at the top, its life filling the
  box in red.
- [x] **Champions and uniques** (2026-09-27): the host sends each monster's type (D2 flags: champions carry the unique flag
  too), special abilities, name seed and superunique number. A superunique shows its own name (SuperUniques.txt), a random
  unique the name its seed rolls from UniquePrefix/Suffix/Appellation.txt the way D2Common rolls ("Gloomtouch the
  Hammer"), both in gold; a champion its monster name in blue; below, the abilities in the game's words ("Lightning
  Enchanted, Extra Fast" for Rakanishu, "Champion"). Uniques are recoloured with their RandTransforms.dat map
  (SuperUniques.txt / monstats2.txt `Utrans`). Every monster and town person is now drawn with the pieces the game chose
  (its COF components) instead of a guess.
- [ ] **Champion/unique details**: whether the original client rolls a random unique's name and colour exactly as done here
  (seed = name seed, high 666; prefix, suffix, appellation, then the colour when monstats2 has none) is inferred; champions
  are not recoloured (check the original); minions' names are not shown differently. `UiDump GAME monster CLASS MODE OUT.bmp exact`
  renders a monster through the game's path (first pieces, palette shift).
- [x] **Death screen** (2026-09-27): once the death animation is over (lying dead, mode 17) the game's own "Press ESC to
  continue." (`data\local\ui\eng\youdiedinst`, a picture in two frames, found through D2Client's `%s\UI\%s\youdiedinst`) is
  shown in the middle of the screen, and Esc brings the player back in town (0x41). Checked with `D2MODERN_TEST_DIE`.
- [x] **Death screen** (2026-10-11), as D2Client 0x6FB22320 draws it while the player dies and lies dead (modes 0 and 17):
  "You have died" (`youdiedhardcore`, despite its name) with its bottom 94 above the middle, "Press ESC to continue."
  (`youdiedinst`) 48 lower, then in font30 red a hardcore character's "Your deeds of valor will be remembered" (5096), else
  "Death takes its toll of %d Gold" (5094, stat 175 goldlost, protocol v18 `Reply::goldLost`); in Nightmare and Hell
  (not hardcore) "You have lost experience" (5095) 48 lower. Esc respawns, or takes a hardcore character out of the game.
- [x] **Hardcore games** (2026-10-11): a hardcore save (status 0x24 bit 4) is put in a hardcore game (GAMEFLAG_ARENA_HARDCORE);
  D2Game disconnected hardcore characters from the softcore game before (SYSERROR_HARDCOREJOINSOFTCORE), so hardcore
  characters made in the front end could not be played.
- [x] **The player's corpse** (2026-10-11): D2Game leaves a dead body holding the gear the player wore (PlrModes 0x6FC7FBD0;
  a player unit lying dead, without UNITFLAGEX_ISCORPSE in this build). The host publishes such bodies that no client
  plays; they are drawn lying in that gear, and a click on one sends 0x13 with unit type 0, which gives the gear back
  (checked: two items back after respawning). `D2MODERN_DEATH=respawn` lists the bodies.
- [x] **The corpse's name** (2026-10-11) over it while the cursor is on it (D2Game names the body after the player), in a
  shaded box like a town person's. `D2MODERN_CLICKS` takes `mX,Y` to only move the mouse.
- [ ] **Death details**: the picture placement assumes 800x600 centring.
- [x] **Entrance names** (2026-09-26): hovering an entrance shows where it leads (levels.txt `LevelWarp` of the level that
  `Warp0-7`/`Vis0-7` connect to), above the entrance.

## Not built yet

- [x] **Esc game menu** darkens the live game and uses the original large gothic font plus animated red pentagram
  selectors. Options controls audio, Return resumes the paused simulation, and Save and Exit shuts down the native session so it saves the local character. Returning to
  character selection without closing the application remains future work.
- [x] **Mini panel** (2026-09-26): the arrow at (392,562) opens eight buttons above the control panel. Character,
  inventory, skill tree, automap, quests and game menu work; party and message log have no screens yet. The panel's place
  (312,527) is estimated.
- [x] **Automap** (2026-09-26): Tab or the mini panel's map button shows the level as seen so far over the game, a tenth
  of its size and centred on the player. Each tile's piece comes from AutoMap.txt (level type as "1 Wilderness", tile
  kind from its orientation: fl, wl, wr, wtlr, wtll, wtr, wbl, wbr, wld, wrd, wle, wre, co, ...; Style = main index,
  Start/EndSequence = sub index) and `ui\automap\maximap.dc6`, bottom-aligned on the tile's diamond. Objects with an
  objects.txt `AutoMap` icon (waypoints, shrines) are shown, the player is a white cross, and the area's name is in the top
  right. The player reveals map cells within 7 cells (35 subtiles) as they go.
- [x] **Automap rooms and memory** (2026-09-27): as the game does, the room the player stands in and the rooms touching it
  are revealed whole (the level loader keeps each game room's area and level). What was seen is kept per map, an act's
  levels.txt `Layer` (the outdoor areas of Act I share layer 0), in world cells, so it survives loading another scene;
  each tile is drawn with its own level's pieces (the camp's stream in town style from the Blood Moor), and only the map
  the player is on is shown.
- [ ] **Automap details**: the orientation order for end walls (wtr/wbl/wbr for 5/6/7) is inferred from the names; no mini
  map (maximaps.dc6), party markers, NPC or corpse markers yet; the original can also move the map (arrow keys) and fade
  it; whether the original reveals the touching rooms at once or a little later is not checked.
- [x] **Quest log** (2026-09-27): Q or the mini panel's quest button opens `menu\questbackground` on the left, with the act
  tabs (`expquesttabs`), the act's quests in sockets (`questsockets`, 1 the chosen one) and the chosen quest's name
  (`qstsaXqY`) and state line (`qstsaXqYN`, N from the game) or "Quest completed." / "...in a previous game." below. The
  game sends the log on request (client 0x40 when the log opens and every two seconds): 0x28 type 6 the quest flags,
  0x52 each quest's state, 0x50 the Den of Evil's monsters left. A quest not given yet is grey (`questdone`), one under way
  its gold picture (frame 0 of `aXqY`), a done one burnt stone (frame 26); one completed in this game burns once.
- [x] **Quests move on when an NPC is heard out** (2026-09-27): closing the Talk page sends each line back (0x31, the
  NPC and the line's string id). Before this no quest could start: Akara's line 64 now gives the Den of Evil.
- [ ] **Quest log details**: the socket and tab places are estimates; the "!" that tells of a quest update
  (`questlast`, `questicons`) is not shown; the other small box at the bottom is unused; which icon an ungiven quest has
  in the original is inferred from the art; the NPC speech is shown at once instead of scrolling, so lines count as heard
  when the page closes.
- [x] **Belt expansion** (2026-09-26): the ~ key or a click on an empty belt slot shows the worn belt's other rows
  (armor.txt `belt` -> belts.txt `numboxes`) above the belt. Not yet seen with a real belt: none of the test characters
  wears one. The upper rows looked for items by column and row, but a belt item's x is its slot in D2Common's 16 by 1
  belt grid (y is 0), row*4+column; fixed 2026-10-01, still not seen with a real belt.
- **Version note (2026-10-03)**: the D2Client/D2Common/D2Game in the game folder root are version 1.07 (D2SE copies its
  last-used core there; same hash as D2SE/CORES/1.07). The entries below from 2026-09-30 to 2026-10-02 were first read
  from those 1.07 files by mistake. All of them were re-checked against D2SE/CORES/1.10f and hold for 1.10f (the
  addresses quoted are now the 1.10f ones): the belt buttons, the hit overlay table, the hsh token, the five box colours
  and every background rule (1.10f tests items.txt nQuest where 1.07 tested another byte), the socket table (identical
  bytes), the grid drop order and helpers, the body-slot switch, the placement preview, the two-hander copy, the
  "Socketed (n)" line, and both D2MOO D2Game fixes (0x1B helper, 0x28 handler). The 1.10f body-slot switch also showed a
  missing case, now added: clicking an empty hand slot while the other hand holds a two-hander takes that two-hander
  (0x1C with the other slot). The property-description search was 1.07-only and has to be redone.
- [x] **Inventory clicks as the original** (2026-10-01), each from D2Client 1.10f's own code and checked against the server:
  - belt (D2Client's belt messages at 6FB6FC1C): right button uses the slot's item (0x26); left button takes it onto the
    cursor (0x24), puts the cursor's item in an empty slot (0x23 with the slot) or swaps (0x25). Before, a left click drank
    it, and no click reached the belt at all: the control panel's blocker region came first. Checked: drink, take, put back.
  - an item put down onto exactly one item in the backpack, stash or cube (6FAE7AB5): the two stack (0x21), a gem/rune/
    jewel goes into a free socket (0x28), a scroll goes into its tome (0x29), anything onto the Horadric Cube goes inside
    (0x2A), otherwise they swap (0x1F). Over two or more items nothing happens. Checked: swap, scroll into tome (4 to 5
    charges), scroll into the cube, quiver onto quiver (to 350, the rest stays on the cursor). Sockets not checked: no
    test item rolled sockets.
  - shift with a belt item in the backpack sends it to the belt (0x63); the game only takes potions there unless
    items.txt `autobelt` is set (D2Common INVENTORY_GetFreeBeltSlot), so scrolls stay. Checked with a potion.
  - body slots switch on D2Common #10299 for the cursor's item (host `bodyPlacement`): equip 0x1A, swap 0x1D, the other
    hand's item off 0x1B, two one-handers off for a two-hander 0x1E, stack 0x21, 0 refused. Checked: take, swap sword and
    buckler, equip, two-handed sword over sword and buckler (sword to the cursor, buckler to the backpack).
  - the host sends per item the answers D2Client asks D2Common for (`traits`: beltable, socket filler, takes a filler,
    stacks with the cursor's item, usable). Test hooks: `D2MODERN_TEST_GIVE="code[:quality] ..."` gives items as a
    quest reward does; `D2MODERN_CLICKS` points may start with `r` (right button) or `s` (shift);
    `D2MODERN_ITEMS_LOG=1` prints the player's items after each click; `D2MODERN_PACKET_LOG=1` the packets sent.
  - (2026-10-02) the belt's left button acts when it comes back up after a press on the belt (6FB00CD0 notes the press,
    6FB00E20 acts), as the original; the right button still uses on the way down.
  - (2026-10-02) a two-handed weapon in one hand with the other empty (D2Common INVENTORY_GetWieldType 2, host
    `wieldType`) is drawn again in the empty hand's slot with a red 25% box over it (6FAE02B4); a bow/crossbow or quiver
    with the other hand empty is red (6FAE0512). Checked: two-handed sword, short bow.
  - (2026-10-02) D2MOO bug fixed in D2Game ItemMode.cpp (0x1B helper, 0x6FC45550): it looked for the item to take off the
    other hand with a null pointer, but the original (D2Game 1.10f, 0x6FC456B6) passes in the other hand's item, which
    INVENTORY_GetSecondWieldingWeapon leaves alone. With D2MOO's version, putting a bow or two-hander into an empty hand
    while holding a shield disconnected the player as a cheat. Checked: the bow goes in, the buckler comes to the cursor.
  - (2026-10-02) body slot boxes: the equipped item under the mouse with nothing held gets a green 25% box instead of its
    usual one (6FAE047A); an item held over a slot puts a box over it, green when it may go there (ITEMS_CheckBodyLocation,
    host `bodyFits`) and the player can use it, or when it fits a free socket of the item there, else red (6FAE070F).
    Checked: an elite helm (red) and a cap (green) held over a level 1 Sorceress's head, her staff hovered (green).
  - (2026-10-02) sockets: the identified item under the mouse (backpack, stash, cube, store, body) gets a green box and
    shows its sockets (D2Client 6FAE0DC0): `panel\GemSocket` at the places of D2Client's table (6FB6EFD0, copied into
    RenderHud.cpp as `socketPlaces`) and the picture of what fills each, in filling order. The host sends `sockets` per
    item and the items in sockets (mode 6) with `parent` and their place as x. A socketed item's inventory exists on the
    server only once something is in it, so "takes a filler" counts a missing one as empty. Test hook:
    `D2MODERN_TEST_GIVE="cap:2:2"` (code:quality:sockets, ITEMS_AddSockets). Checked: a ruby and a sapphire into a
    2-socket cap, shown one above the other when hovered.
  - (2026-10-02) D2MOO bug fixed in PlrMsg.cpp (0x28, socketing): D2MOO placed the filler only while the player was
    interacting with something and otherwise returned 3, so every socketing disconnected the player. The original
    (D2Game 1.10f 0x6FC86405) refuses only while trading with a player and otherwise places it.
  - Not copied yet: quest items (`ass`, `xyz`, `tr2`) whose use is spent show red in a grid; the hover box is red instead
    of green in D2Client cursor mode 8 (seen in 1.07; meaning not worked out); the original only shows the held-item box
    while the mouse is in the right half of the screen; the tooltip has none of the magic properties (nor the fillers'
    bonuses; D2Client builds them from itemstatcost.txt descriptions).
  - (2026-10-02) an item held over the backpack, stash or cube shows where it would go (D2Client 6FAE15D2): green where
    it can be put down, the one item it would swap with or go into (the cube) yellow 128,128,0, red over two or more items;
    25% boxes cell by cell. Not copied: the original's own placement test (INVENTORY_CanItemBePlaced also refuses items a
    grid may not hold) and its position from the mouse (ours uses the cell under it, as the click does).
  - Magic properties (first version 2026-10-09, from the 1.10f D2Client). The host sends each item's stat list of state 0
    and flag 0x40 (STATLIST_MAGIC; D2Common #10484, #11243), up to 32 stats (protocol v10, `Item::stats`). The tooltip
    (0x6FAF31A9 → 0x6FAF19C0, one line by 0x6FAF21C0) writes them in blue under the base lines when the item is
    identified, highest descpriority on top (`ItemStats.cpp`): descfunc 1-28 (switch at 0x6FAF24CD, table 0x6FAF30E8),
    descval, positive/negative strings, ValShift, per-level ops, description groups (all resistances / attributes), and
    D2Client's combined lines: "Adds x-y damage" and the fire/lightning/magic/cold/poison ranges, "+n% Enhanced Damage"
    (0x6FAF37B8: strings 4002, 4001, 3995, 10023), the two-hand and throw damage copies left out (0x6FAF1FF7). Skill tab
    lines (descfunc 14, 0x6FAF2854) write nothing for a tab of 3 or more. `D2MODERN_ITEM_STATS_LOG=1` prints every carried
    item's stats and lines; `D2MODERN_HOVER_ITEM=N` (with `--open-inventory`) captures the Nth magic item's tooltip.
    Checked on magic, rare, set and unique rings, amulets, a cap, gloves, a short sword and an axe.
    Base lines (2026-10-09): the defense is the full armorclass stat (blue when the magic changed it: Biggin's Bonnet
    "Defense: 17"); the requirements are D2Common's (items.txt reqstr/reqdex + item_req_percent, -10 ethereal;
    ITEMS_GetLevelRequirement, shown above 1 as D2Client 0x6FAE2F73 does), red when ITEMS_CheckRequirements says unmet
    (protocol v11, `Item::armorTotal`, `reqStr`, `reqDex`, `reqLevel`, `reqMet`).
    Socket fillers (2026-10-09): a gem or rune keeps its own stat list (its bonus for that kind of item) linked to the
    item, so the tooltip adds each filler's stats to the item's (same stat and parameter summed). A perfect ruby in a Wrist
    Spike shows "Adds 15-20 fire damage" (`D2MODERN_TEST_SOCKET=gpr`, `D2MODERN_HOVER_ITEM=#id`).
    Set bonuses (2026-10-09): a set item's partial bonuses are lists of the item-set states 165-169 (D2Common ItemMods,
    flags MAGIC | SET), which the game gives the item when enough pieces are worn (the state bit stays off), so the host
    sends the lists that exist (protocol v12, `Item::setStatCount`) and the tooltip writes them green under the blue ones
    (D2Client 0x6FAF32B0 prints these states). Cleglaw's Tooth and Claw worn: "+123 to Maximum Damage (Based on Character
    Level)" and "All Resistances +15".
    Set-level bonuses and the set list (2026-10-11): D2Common gives the player each set's bonuses (sets.txt) as a stat list
    under an item-set state 165-170 with stat 71 = the set's sets.txt row; the host sends those lists and the setitems.txt
    rows of the set items the player has where D2Client 0x6FAE5990 counts them (body, backpack, cube, stash; protocol v17,
    `Reply::setBonuses`, `ownedSetItems`). Under the green bonuses the tooltip writes, as 6FAE2891-6FAE29F4 builds it: a
    worn item's set bonuses in gold (0x6FAF33C0), the set's name in gold and its items, green those the player has, red
    the others, a blank line before each part. The "Ethereal / Socketed" line moved up to end the blue block, where
    D2Client puts it. Cleglaw's Tooth and Claw worn: "+50 Defense" in gold, Pincers red. Damage lines show only the kinds
    the base item has (a +max damage bonus had added "Two-Hand" and "Throw Damage" lines to a one-handed sword).
    Not done yet: damage lines in blue when the magic changed them; a shield's "One-Hand Damage" line (Paladin smite: check the original);
    descfunc 22/23 (monster type and class names); equal-priority order (D2Common's qsort) unchecked.
  - D2MOO bug fixed (2026-10-09): the skill tab property (ItemMods.cpp PropertyFunc10) stored the tab as
    `layer % 3 + 8 * layer / 3`, so every "+n to <tab> Skills" affix got a parameter the game can't read (Fungal: 19
    instead of 17) and the bonus never applied. The original D2Common computes `layer % 3 + 8 * (layer / 3)` (0x6FD9729A).
  - (2026-10-02) the tooltip's bottom line as D2Client 6FAE3EE0 builds it, in blue (colour 3): "Ethereal (Cannot be
    Repaired)" (string 22745) for ethereal items, ", " between, "Socketed (n)" (3453, 3995, "(%i)") for socketed ones.
- [x] **Item backgrounds as the original** (2026-10-01): D2Client fills a box behind each item with D2GFX_DrawBox in draw
  mode 0 (TRANS25, read here as 25%), in colours made from RGB at start-up (6FADD2F0): red 128,0,0 when the player
  can't use it (ITEMS_CheckRequirements), it is broken or not equippable, or it is the armed identify scroll; 128,64,64
  unidentified; otherwise blue 0,0,128 in grids, and no box for a usable identified item on the body. This replaced the
  quality tints at 55% (they are kept only for the plain fallback store). Checked: blue scrolls, red two-handed sword
  and elite helm on a level 1 Sorceress.
- [x] **Difficulty** (2026-10-10): a character whose save's progression (byte 0x25) has opened Nightmare (5 for an
  expansion character, 4 classic) or Hell (10 / 8) chooses on OK: `charselect\difficultylevels` (sky palette) in the
  middle with SELECT DIFFICULTY (10019) and Normal / Nightmare / Hell (5156-5154) in its three bars, the closed ones grey,
  white or gold under the mouse; Escape cancels. The choice goes to the game in its create flags (GAMEFLAG_DIFFICULTY,
  through D2NEXT_DIFFICULTY); the host prints "Native game difficulty N". Checked: a save with progression 15 offers all
  three and plays each; one with 0 starts Normal directly (and the server refuses Hell for it). The original's text
  placement in the popup is a guess. `D2MODERN_FRONT_SHOT=file.bmp` (with `D2MODERN_FRONT_CLICK=x,y` or
  `D2MODERN_FRONT_CREDITS=seconds`) captures the front end.
- [x] **Credits** (2026-10-10): the game's own credits (data\local\ui\eng\ExpansionCredits.txt, UTF-16, then credits.txt;
  `*` lines are headings, in gold) rise over `charselect\creditsbckgexpand`. The scrolling speed (30 pixels a second) is a
  guess. Cinematics were already done.
- [x] **Classic (non-expansion) characters** (2026-10-11): a save without the expansion bit (status 0x24, 0x20) gets a classic
  game (no GAMEFLAG_ARENA_EXPANSION; D2Game disconnected it from the expansion game before). The creation screen's
  "Expansion Character" box starts ticked and can be cleared, except for the Druid and Assassin (greyed, always ticked).
  In a classic game the inventory is `panel\invchar` (no weapon set tabs; the tabs' click areas and W are off), as
  D2Client checks the expansion (0x6FAAC080). Checked: a classic copy of the test character plays, its panel without tabs.
  `D2MODERN_TABLE_GREP="archive path;regex"` prints a table's rows.
- [x] **Character creation sounds** (2026-10-11): a hero picked says `cursor\intro\<class> select.wav`, the one sent back to
  the fire `<class> deselect.wav`. Not checked: whether the original dims the heroes not chosen (hover lighting, nu2, is done).
- [x] **Mouse cursor** (2026-09-26): the game's hand (`cursor\ohand`), closing (`ppress`) when a button goes down and
  turning (`protate`) while held, in the menus and the game. An item on the cursor replaces the hand.
- [x] **Identify scroll** (2026-09-30): right-clicking a Scroll or Book of Identify sends use-item (packet 0x20, with the player's position). A scroll in the belt uses 0x26 on a left click, the same as a potion. The server arms it and answers 0x3F; the cursor becomes frame 0 of `cursor\buysell` (the question mark) and plays `cursor\identifyready.wav`. The next left-click on an unidentified item, in the inventory, on the body, in the stash or cube, or in the belt, sends 0x27 (the target, then the scroll). The server identifies it, plays `cursor\identify.wav` (packet 0x2C, sound 6), spends the scroll or one book charge, and clears the cursor. Escape sends 0x27 with the scroll as both ids, which cancels. A stack's count (books and throwing weapons, not gold piles) is drawn in font6 at the cell's lower right. Until that flag is set, a magic, rare, set, unique or crafted item keeps its base name in red and its base inventory picture; identifying reveals the real name, the quality colour, and `uniqueinvfile` / `setinvfile`. Not watched in a running game. The scroll and book pictures (`invrsc`, `invbsc`, `invrbk`, `invbbk`) and the skill-button frames (skilldesc IconCel 14 and 18) were already the files the game uses.
- [x] **Scroll and book skills** (2026-09-30): a scroll or book grants its skill with a quantity and no level (skills 217-220). Those skills are listed with the others when a skill box is opened, and the charge count is drawn in font6 at the icon's lower right, on the box and in the list. Casting one uses the same server function as right-clicking the item (identify arms the question-mark cursor; town portal opens the portal). Not watched in a running game.
- [x] **The hand's animation** (2026-10-11), D2Client's state machine (0x6FB57700; table at 0x6FB96B54: loops, 8 frames,
  step per drawn frame in 256ths): normally the pointing hand turning now and then (`protate`, 1/8 frame on a random
  quarter of the frames); a button held, `ppress` looping (1/4 frame) until it comes up, then `protate` from its first
  frame; 5 seconds without a mouse event, the hand opens (`ohand`) and turns open (`orotate`, looping); moved again, it
  closes (`ohand` backwards). It showed `ohand`'s first frame normally before. Stepped 25 times a second, the original's
  frame rate.
- [ ] **Other cursors**: Cain's identify was not tried.
- [x] **In-game sounds** (2026-09-26), all the game's own (sounds.txt, items.txt `usesound`/`dropsound`): the button
  click (`cursor\button.wav`) on the interface's buttons and tabs; an item picked up onto the cursor (`cursor\pickup.wav`);
  put away or worn (its usesound; potions their dropsound, since a potion's usesound is drinking); dropped on the ground
  (its dropsound); a potion drunk from the belt or inventory; gold picked up (`item\gold.wav`); a new level
  (`cursor\levelup.wav`). The files were checked to exist; the sounds were not listened to here.
- [x] **Spell and footstep sounds** (2026-09-26): a cast plays the skill's skills.txt `stsound` as the cast animation
  starts; each missile its missiles.txt `TravelSound` when it appears and `HitSound` when it ends within 30 subtiles of the
  player; footsteps (sounds.txt `<weight>_<walk|run>_<ground>_1-4`) twice per walk or run cycle. Checked with
  `D2MODERN_SOUND_LOG`: Fire Bolt gave firecast, firebolt1 and largefireimpact1; walking in town LightDirt1-4.
- [ ] **Footstep details**: the weight is chosen by class (Amazon, Sorceress, Assassin light; Necromancer, Druid medium;
  Barbarian, Paladin heavy) and the ground only by inside (istone) or outside (dirt); the original may use the armour and
  the floor tile (wood, sand, snow, outside stone). A missile that ends without hitting anything near the player also
  plays its hit sound.
- [x] **Drops and cries** (2026-09-26): an item appearing on the ground within 15 subtiles of the player plays its
  dropsound (the game reports drops already landed; a new area's items don't count); the player's get-hit (mode 4) and
  death (mode 0) play sounds.txt `<class>_hit_1-5` / `<class>_death_1-3` in turn. Checked: gold dropped by a monster.
- [x] **Music without pauses** (2026-09-26): music tracks (20-40 MB each) are read and decoded on a background thread with
  its own archive handles; the Act I town and wilderness tracks are read as the game starts, and a track not ready yet
  starts once it is. Leaving town used to stop the game about 1.2 seconds while the wilderness track loaded.
- [x] **Area loading pauses** (2026-09-27): entering a dungeon stopped the game about a second while the level was built.
  With D2MOO linked in the level loader shares the game's archives and tables instead of loading them each time: the Jail
  now loads in about 25 ms (1010 ms in the DLL build).
- [x] **Spell glitches near town** (2026-10-03): missiles and monsters are numbered separately by D2Game, and D2Game
  reuses missile numbers (a run of Fire Bolts cycles through 1-5), but the client gave both the entity id 1000 + number.
  Just outside the Rogue Encampment the townspeople (the lowest monster numbers) are still sent, so each new missile
  shared its id with one of them and the two swapped previous positions, facings and animation clocks (the bolt starting
  from the wrong place or facing, the townsperson jumping); further out they drop out of range and it stopped. Missiles
  now carry `missileIdBit` in their ids. `D2MODERN_MISSILE_LOG=1` notes a missile that shares a unit's number.
- [x] **A world per character** (2026-09-28): every game used seed 0x12345678, so every character had the same maps. The
  world now comes from the character's save (d2s 0xAB, the map seed D2Game writes each save); a new character (0 there)
  gets a random seed, which its first save keeps, as the original single player does. `--seed N` still overrides it, and
  test runs (`--smoke-test`) keep 0x12345678 for saves without one. Characters made before this all hold 0x12345678.
- [x] **Area sounds** (2026-10-10): each level's music, ambience and random events come from its soundenviron.txt row
  (levels.txt SoundEnv), which D2Client loads itself: Song; the day or night ambience loop (day is cycles 1-3); an event
  of the sounds.txt group every Event Delay frames plus up to a third more (0x6FB53860). Effects now play at their
  sounds.txt Volume. A loop path was broken (`"data\global\sfx\\"` had lost a backslash), so loops never played before.
  `D2MODERN_AMBIENCE_TICKS=N` runs N ticks with the audio following.
- [x] **Store sounds** (2026-10-11): the store's answer (0x2A, D2Client 0x6FAF58E4) plays `item_gold` for a sale and, for a
  purchase, the bought item's items.txt dropsound chosen as the request went out (0x6FAFA865). A dagger: smallmetalweapon.
- [ ] **More sounds**: skills.txt `dosound` and `tgtsound`; the original picks hit cries by damage (soft/hard), here they
  rotate; a unique or set item's own dropsound (uniqueitems/setitems) is not used for purchases.

## Character animation

Done (2026-09-25): the player has every mode the class has art for (attacks, block, cast, kick, specials, get hit,
death and lying dead). The animations are rebuilt when gear changes the weapon class or look.

- [x] **Throw** (TH) with one-handed swords (2026-09-26): a layer whose file has fewer frames than the COF holds its last
  frame. A layer with a different number of directions still fails to load, with the file named in the message.
- [x] **Walking and running directions** (2026-09-27): the player faced only 8 ways (the sign of each step's x and y) and,
  on a path at an angle between them, flipped back and forth. Now the facing uses all 16 directions of the animations and
  points toward where the player was sent (as the game's client does), not along each short segment the server moves on.
  Long walks are split into hops straight toward the goal on open ground (else the farthest point of the grid path a
  straight line reaches), instead of the grid path's zigzag. The walk hook prints the facings a walk went through.
  Walking up to an enemy to attack it (or to use a skill on it) faces the enemy, a skill at a spot faces the spot, a pickup
  the item; before, the player kept facing the last walk target and could walk backwards to the enemy.
  `D2MODERN_ATTACK_TEST=1` attacks the nearest enemy and prints the facing next to the enemy's direction.
- [x] **Monster facing** (2026-10-11): a walking monster or town person faces where its path goes (the host sends the path's
  final target, protocol v20 `Unit::targetX/Y`), as the original turns it to its move packet's target, not each step;
  near the target, the step's own direction. Checked (`D2MODERN_FACING_LOG=500`): the camp's walkers turned twice in
  70-250 walking ticks each.
- [x] **Facing the target** (2026-09-26): attacks, casts, throws and kicks turn the character toward the target, and it
  keeps that facing until it walks again.
- [x] **Missing layer files** (2026-09-26): a layer whose file doesn't exist is left out instead of stopping the load (a new
  Necromancer's `s3` armour layer, an Assassin's buckler). New Necromancers and Assassins failed to load before this.
- [ ] **Assassin (and Barbarian) shield with one weapon**: the game (D2Common `UNITS_GetItemComponentId`) puts the second
  hand's item on the left-hand weapon layer, and there is no `ai\lh\ailhbuc...` file, so an Assassin's starting buckler is
  not drawn. Check whether the original client shows it.
- [x] **Coloured gear** (2026-10-09): items are drawn in their colours, inventory pictures and the gear worn on the
  character. D2Common ITEMS_GetColor (#10829; type 1 inventory, 0 character) picks the colour (affix transformcolor,
  uniqueitems/setitems invtransform/chrtransform, colour states) and returns D2CMP_MixPalette(file, colour); the x64 D2CMP
  keeps those maps empty, so the host decodes the map number (file * 105 + colour) and sends the file and colour (protocol
  v13: Item::invTrans/invColor/chrTrans/chrColor, Reply::componentTrans/componentColor). The client draws the picture or
  layer through dataglobalitemspalette<file>.dat (21 maps of 256 indices: 1 grey, 2 grey2, 5 greybrown, 6 invgrey,
  7 invgrey2, 8 invgreybrown, by items.txt Transform/InvTrans). A component's colour rides in the top byte of its look
  code (trans * 21 + colour + 1), so a colour change reloads the layers like a gear change. Checked: Cleglaw's Claw,
  magic quilted armour and buckler in the inventory, the Max Paladin's own coloured armour and Cleglaw's shield worn.
  Not checked against the original side by side; other players (LAN) and monsters drawn as players get no colours yet.
- [x] **Transparent layers** in player animations are drawn (see "See-through and glowing drawing" below).
- [x] **Gear changes** (2026-10-11): the two looks worn last are kept, so changing back (the other weapon set, a helm taken off
  and put on) is at once; checked: the first swap to weapon set II 198 ms, the swaps back and forth 0 ms.
- [x] **A new look is built in the background** (2026-10-11): a thread with its own archives (StormLib handles are per thread;
  opened as the game starts) builds it while the old look is still drawn, and it is shown when ready; the newest change wins,
  and one finished after the gear changed again is kept for later. Checked: the new look shown 221 ms after the swap, the
  game's longest step meanwhile 0 ms. The web build (single-threaded) still builds it in place. `D2MODERN_LOOK_SYNC=1`
  builds it in place on the desktop too; `D2MODERN_CLICKS` takes `wN` (N ms of real time, reporting the longest step).
- [x] **Casting overlays** (2026-09-26): the spell's skills.txt `castoverlay` (overlay.txt: file, frames, `AnimRate`
  16 = one frame per game tick, `Trans`, light `Radius` and colour) plays at the caster's feet from the start of the cast
  to its last frame, added onto the scene, and lights its surroundings. `PreDraw` overlays go behind the character.
- [x] **Lasting overlays** (2026-09-26): the host reports every unit's states (D2Common `STATES_CheckState`, states
  0..191 as bits, for the player and each monster), and each active state's states.txt `overlay1-4` loop at the unit's
  feet (overlay.txt frames, AnimRate, Trans; PreDraw ones behind the unit). Checked: Frozen Armor's sparkles on the
  Sorceress, the Might aura's ring of rays under the Paladin.
- [x] **Skill effects** (2026-09-27, checked on all 30 Paladin skills):
  - the overlays the server plays once over a unit (packet 0x11, `UNITS_SetOverlay`: skills.txt srvoverlay, a missile's
    ProgOverlay, life leech) are drawn over it. A monster uses its native id. On the player the packet carries the native
    guid, which is drawn as unit 0; items, objects and the type-6 fallback are not drawn;
  - the effects the client itself adds where a missile ends (missiles.txt CltHitSubMissile: Holy Bolt's and Fire Bolt's
    explosions) are drawn. Moving ones the server also makes (HitSubMissile: Fist of the Heavens' holy bolts) are left to
    the server, which aims them at undead within range only (aura filter bit 4), so outside undead areas FoH shows just
    its lightning strike; the client used to add a wrong ring of 8 bolts;
  - missiles loop their SubStart..SubStop frames (SubLoop), and glowing ones (Trans 1) are drawn at full strength instead
    of being dimmed by the light where they are;
  - the sequence mode (18: Charge, Frenzy, Whirlwind, Lightning...) plays the frames of the mode its skill names
    (skills.txt seqtrans) instead of standing still;
  - a skill's own slow client missile (skills.txt cltmissilea, Vel 3 or less: Sacrifice's blood) sprays from the player
    on every swing; a swing that follows another keeps the mode, so the animation starting over marks it.
    `D2MODERN_LEFT_SKILL=96 D2MODERN_ATTACK_TEST=61` (ticks to keep attacking) with `--travel 3` shows it;
  - effects for units' states: Sanctuary (state 47) sparkles with sanctuarybolt Gleams around the unit, Open Wounds (62)
    drips blood1, and a corpse turning redeemed (99, states.txt `hide`) disappears as Redemption's ghost rises. The game's
    own client functions (states.txt cltactivefunc 85/86) place these; the spots and rates here are guesses;
  - elemental hits: when a monster falls into death, get-hit or knockback, its last hit class (sent in native::Unit) picks
    an overlay as D2Client 1.10f does (table at 6FB70EFC): fire fire_hit, cold ice_explode, lightning lightning, 0x10
    doubledamage1, 0x70 bash, 0x80 hit_thorns, 0x90 sanctuaryknockback. Vengeance shows its fire/cold/lightning this way.
    The player's own hits use the same table. PlrMsg sends the hit class on packet 0x0D for get-hit (mode graphic 0x06)
    and death (0x08), and on packet 0x0F for knockback (graphic 0x14); it is byte 0x0B (SCmd's nHitType, dwLastHitClass).
    Block (0x12) and the resting dead mode (0x09) carry a hit class and do not play the overlay.
  - Holy Shield (state 101) with a shield equipped draws the shield component from its own art, token `hsh`
    (pashhsh*.dcc), as D2Client 1.10f picks tokens at 6FB5E7FC. The same code also swaps tokens for bows and crossbows
    (a bow in the right hand shows `lit`; crossbows look at the ammo slot), which isn't copied yet.
- [x] **Skill effect details** (2026-09-30): a missile's explosion (`ExplosionMissile` and `CltHitSubMissile`) is added
  when the server removes it before its range runs out. `MissMode`'s collision step decrements `nCurrentFrame` and removes
  the missile at 0, so the last published frame of a miss is 0 or 1; `AlwaysExplode` (immolation arrow, frozen orb) still
  explodes then. One that only left the published area (further than 96 subtiles from the player; the host stops publishing
  past 120) is not a hit. That 96 is taken from the publish limit and the ~16 pixels per subtile, not from a client
  decompile. Fist of the Heavens' bolts at undead still haven't been seen: `D2MODERN_ATTACK_TEST` picks the nearest enemy
  in a straight line, which indoors is behind a wall, so it never casts there. The client submissile spray is still every
  3 ticks, except a meteor marker (`pCltDoFunc` 9), a blizzard (`pCltDoFunc` 13, monster blizzard 10), a poison javelin
  (`pCltDoFunc` 3), a poison cloud (`pCltDoFunc` 4), lightning and a bone spear (8 and 18), a firewall maker (6, molten boulder 47), and eruption (48). The meteor's rock and tail are spawned once and fall onto the
  marker. `CltParam1` frames, `CltParam2` pixels of height and `CltParam3` pixels of sideways offset per frame (59, 25
  and 15; each at least 1). The marker's `ProgSound` plays 2 frames before that fall ends. Blizzard's radius and
  frequency are the skill's `calc1` and `calc2` (`par1` 7, `par3` 4): one of the two ice pictures is dropped in that
  square on that clock, and each shard falls `CltParam1` pixels at `CltParam2` pixels a frame (120 and 5). The server's
  own shard falls the same way and outlives its Range. Its impact (`pCltHitFunc` 19) is one of the two explode missiles,
  when the picture lands. A poison cloud drops `CltParam2` puffs of `CltSubMissile1` every `CltParam1` frames, on a
  random spot in a square of half-width `CltParam3` (24, 1 and 6 on the javelin clouds). The guide does not say whether
  that area is a disc. The cloud graphic stays the published missile; `SrvDo03` does not spawn the puffs. Poison
  Javelin's trail is the server's cloud (`pSrvDoFunc` 2 creates the same `SubMissile1`). Plague Javelin and the poison
  trap balls do not get that server trail, so the client lays a cloud each frame the missile moves. `CltCalc1` would set
  the cloud's loop count, and the missile level is not published, so the cloud keeps its Range. Lightning (`pCltDoFunc` 8)
  lays one `CltSubMissile1` each frame after `InitSteps` (0 on Lightning Bolt, so from the first frame). The bolt's own cel
  is null; the trail is the picture, and each piece lasts the sub's Range. `CltParam1` is that piece's loop count, at least
  1, and a sub with `SubLoop` adds `loops*(SubStop-SubStart)` frames the way `MISSILES_CreateMissileFromParams` does.
  Bone Spear (`pCltDoFunc` 18) adds one `bonespeartrail` after `InitSteps`, moving on the spear's path. That is one second
  part of the projectile, not a piece dropped every frame. The trail has no `SubLoop`, so it keeps its own Range (8).
  A firewall maker (`pCltDoFunc` 6, and a molten boulder, 47, which runs that function) adds one of its three client fire
  pictures each time its subtile changes, the same step `SrvDo06` uses to lay the damaging patch. The big patch is still
  the published missile. `CltParam1` is the chance that picture has no light; the roll is not specified, so the light stays.
  The fire's own animation is the `SubLoop` window (`SubStart` 12 through `SubStop` 36).
  Eruption (`pCltDoFunc` 48) lays its smoke (`CltSubMissile2`) on the skill's `calc1` radius and `calc2` frequency
  (`par1` 7, `par2` 6), the same clock `SrvDo25` uses for the crack. The crack stays the published missile. The guide
  sets that crack to dead mode; a second crack is not added. The spot is the server helper's square, and a blocked
  tile is skipped with the walk grid rather than collision mask 0x45.
  A few impacts no longer use the ring of eight. Exploding arrow and fireball (`pCltHitFunc` 1) place `cHitPar2` copies
  of the client hit on a circle of radius `cHitPar1` (1 copy, 3 subtiles out). A fulminating potion (`pCltHitFunc` 3)
  keeps the burst and one of the two debris cels. Freezing arrow (`pCltHitFunc` 14) leaves the center facing the arrow
  and the ejecta on a random facing. Meteor (`pCltHitFunc` 18) lays rings of `cHitPar1`, `cHitPar2` and `cHitPar3`
  (5, 3 and 15) on the 18 spots the server uses for its ground fire, plus the white light for the skill's `Param3`
  frames (30; `Param4` per level is not applied, the missile level is not published, and the light radius stays the
  row's 5 rather than 12). Bone spear, grim ward, the catapult bombs and vine wither (`pCltHitFunc` 24, 29, 44, 54)
  leave one picture facing the way the missile flew. Ice break (`pCltHitFunc` 31) becomes the melt cel at animation
  rate 1; the medium break uses the large melt. Andariel's queen glob (`pCltHitFunc` 39) becomes one of the next two
  missile rows. Duriel's death rock (`pCltHitFunc` 41) throws three debris inside 6 subtiles and leaves the smoke.
  Molten boulder (`pCltHitFunc` 52) bursts, throws `cHitPar1` rocks (10) outward at velocity `cHitPar2` (15) toward a
  distance of twice that count, and lays 18 small fires on those same meteor spots. A missile with `pCltDoFunc` 11
  stays on its last frame and loses its light once that frame is up. The server's own ground fire (meteor, immolation
  arrow) is still the published missile.
  `D2MODERN_OVERLAY_LOG=1` and `D2MODERN_MISSILE_LOG=1` print the overlays and missiles.
- [x] **State details** (2026-09-30): `castoverlay` / `removerlay` play once when a state starts or ends. The first report
  only records what is already on, so loading in does not replay them. The active state with the highest `colorpri` recolours
  the unit through that act's `Pal.PL2` (128 maps of 256 indices at byte 341248; 108 frozen blue, 104 poisoned green, 73
  revived grey). `light-r/g/b` replaces the colour of a light the unit already gives. `LoopWaitTime` pauses a looping overlay.
  A monster's cast overlay plays when MonsterMsg sends the skill-use packet (0x4C at a target, 0x4D at a point; 0x99 and
  0x9A are the same layouts). That includes a melee cast that launches no missile. State on/off sounds are not played. The player's
  tint is applied when that mode is decoded: each sprite index reads the act palette through the Pal.PL2 map, the same
  composition UnitArt uses for a monster after its palshift (the player has none). That shifted mode is kept until the gear
  changes. `1ofN` is rolled once when the overlay is created.
- [x] **Overlay heights**: `Height1-4` shift the overlay (screen Y, so a negative height raises it) and `Xoffset`/`Yoffset`
  are applied. The player uses height 2 (medium). Monsters use monstats2.txt `OverlayHeight`.
- [x] **Repeated casts** (2026-09-30): the cast overlay restarts when the cast mode begins, and when a following cast stays
  in mode 10 but its animation frame goes backwards.
- [x] **Curses** (2026-10-09), from D2Client 1.10f's code (the skills.txt cltstfunc table is at 0x6FB77488, cltdofunc at
  0x6FB775A8, missiles.txt pCltDoFunc at 0x6FB91260). The curse start (18) puts one cltmissilec at the path's target unit
  or point (0x6FABABC0). The curse do (30) runs on the cast's AnimData frame event, which the host now reports (D2Common
  #10382, `Reply::frameEvent`). It creates `cursecenter` (row 191) with the curse's cltmissilea as its stream missile
  (#11131) and aurarangecalc − 3, at least 2, as its stream range (#11133). cursecenter's pCltDoFunc 17 calls 0x6FB35D10
  while its remaining frames are below CltParam1 (7) and divisible by 3. That function walks dx, dy from −r to r in steps
  of 2 (radius above 4) or 1, and for each spot inside the radius places the picture on a 1-in-r/2 (or 1-in-r) roll, in a
  loaded room. `SkillInfo::levelCalc` evaluates the level-only calc expressions (parN, lnXY, dmXY, lvl, + − * /). Not
  copied: the light 0x6FAA4280 gives the center, and the room test (any spot is drawn).
- [x] **Corpse spells and Mind Blast** (2026-10-09), from the same tables: Corpse Explosion (21/32), Poison Explosion
  (21/33), Revive (24) and Mind Blast (8); see SKILLS-PROGRESS.md's log for what each lays. The corpse's size comes from
  monstats2 small/large (`MonsterSprite::small`/`large`, flags 10 and 11 of byte 5) and OverlayHeight; the blood is
  the guts' SubMissile1/2 (`MissileInfo::serverSub`/`serverSub2`). Skills not listed here keep the start-of-cast rule
  in ClientState (a client missile the server doesn't also make, at the target when it is slow).
  Later the same day: Raise Skeleton (20/31), Teeth/Bone Spear's bonecast (19), the Inferno/Arctic Blast flame mix (24,
  done in Main where missiles are handed to the renderer), and `clientLights` for the curse center's light.

## World rendering

Done (2026-09-26): lighting on indoor levels (levels.txt `IsInside`). Floors are shaded smoothly and walls from end to
end. Characters and objects are lit where they stand, by the player's light and by lit objects (objects.txt `Lit0-7` and
colour) and missiles (missiles.txt `Light` and colour). The level surroundings are black.

- [ ] **Light tuning**: the player's light radius is a fixed 11 subtiles and the unlit darkness is 15%. Both are
  estimates; compare them with the original. Items with +light radius (stat `item_lightradius`) are not counted yet,
  because the host doesn't send that stat.
- [x] **Monster light** (2026-09-26): living monsters with monstats2.txt `Light` (Fallen Shamans, Will-o'-wisps,
  Vampires...) light the dark levels around them in their `light-r/g/b` colour.
- [x] **Day and night outdoors** (2026-10-10): the host sends the act's time of day (protocol v14: the cycle, the light's
  colour and intensity). The server only moves the clock (packet 0x53); the light is worked out as D2Client does it
  every frame, ENVIRONMENT_UpdatePeriodOfDay (#10923) on a copy of the act's environment, keeping the intensity, which
  D2Common moves a step a frame (Act IV's areas settle at 16-128). Outside, the scene's light is that colour times
  intensity / 128 (128 the art's full light: the Pandemonium Fortress's fixed value; brighter is not drawn), with the
  player's and objects' lights added at dusk and night: night is about half as bright and blue, Act IV red.
  `D2MODERN_TIME_OF_DAY=cycle` (host cheat 8, ENVIRONMENT_InitializeEnvironment) sets the time; `D2MODERN_ENV_LOG=1`.
  Check against the original: the 128 scale, and whether the original's 3D modes brighten above it.
- [x] The game's **gamma/brightness** setting (2026-10-11): BRIGHTNESS 0-10 in the options (settings.ini `brightness`, 5 as
  drawn). The whole picture, the interface too, is scaled 0.6x-1.4x by a last full-window pass (blend `Scale`: what is
  below times twice a grey) before the cursor. A brightness scale, not the original's gamma ramp (darks are not lifted).
  `D2MODERN_BRIGHTNESS=N` puts it in a capture.
- [x] **See-through and glowing drawing** (2026-09-26): missiles with missiles.txt `Trans` 1 are added onto the scene as
  a glow, and `Trans` 2 is half see-through. The see-through layers of every animation (COF transparent flag) are drawn
  over it by their effect: 3 and 6 add on, 0/1/2 are 75/50/25% opaque. This covers wraiths, glowing weapons, the
  waypoint flames and the Amazon's block.
- [ ] **Finer see-through details**: a solid layer drawn after a see-through one in the same frame simply hides it
  (the original blends each layer in order); the effect values are inferred from where they appear (compare with the
  original). objects.txt `Trans` (7 on the river dummies R1-R5 and 1R-4R) needs nothing: D2Common loads it (field at 0x13E) but
  nothing in 1.10 reads it for objects, so they are drawn by their COF layers like any object (checked 2026-10-10).
- [x] **Monster colours** (2026-09-27): each monster is drawn through its palette shift, map `TransLvl` (monstats.txt) of its
  `COF\palshift.dat` (map 0 too: it greys the Act I skeleton, which is tan without it; for most monsters map 0 changes
  nothing), then a unique's RandTransforms.dat map. Ghostly champions (type flag 0x40) are half see-through. The piece lists
  of monstats2.txt keep their "nil" places and lose their quotes, so the game's piece numbers point at the right pieces (a
  Blood Moor zombie's head was wrong before).
- [x] **Colour variations**: dyed and tinted gear is drawn in its colours ("Coloured gear"); frozen, poisoned and the other
  states.txt tints are drawn (see "State details").
- [x] **Overlays** from unit states: auras, curses and armour overlays loop from the states the host reports (see
  "Character animation").
- [x] **Character, monster and object shadows** (2026-09-26): every COF layer with its shadow flag (layer byte 1) casts a
  shadow: the frame pressed to half height and leaning up-left at 45 degrees on screen, drawn 50% black on the ground
  after the floors and before walls and units, like the map's shadow tiles. Missiles and overlays cast none.
- [ ] **Shadow shape**: the 45-degree lean was judged by eye. The map's pre-drawn fence shadows lean only about half as
  much (`UiDump GAME tilepair`), but with that little lean a unit's shadow hides behind the unit. Compare it with the
  original client. Overlapping shadows darken twice; the original may not.
- [x] **Stairs and cave mouths** (2026-09-27) were black holes: the level loader left out every special tile (orientation
  10/11), but only main index 30 and up are the game's invisible markers; below 30 they are the pictures of the ways to
  other levels (the stairs up in the Jail and the Catacombs, the Den of Evil's mouth in the Blood Moor cliff), now drawn
  standing like walls. `D2MODERN_DEBUG_SPECIAL=1` lists them as a scene loads; `D2MODERN_LOOK_AT=cellX,cellY` centres a
  capture on a map cell.
- [x] **Units inside buildings** (2026-10-11): a unit was drawn over the roofs when any roof piece lay behind it, so a cow
  in a barn stood on the roof. Now only when every roof piece its picture overlaps lies behind it (one in front of the
  building); units inside stay under the roof, as in the original. The roof still fades for the player walking in.
- [x] **Blue squares by the Rogue Encampment river** (2026-10-11): River.dt1 has one "wall" (orientation 1, main 5, sub 0,
  rarity 0) that is a floor-sized diamond of a single colour, the only tile with those keys, so the riverbanks get it in 17
  places. Walls of one flat colour (not shadows) are left out; `D2MODERN_SCAN_FLAT_WALLS=1` scans every lvltypes.txt tile
  file and finds only this one. Not yet traced in D2Client how the original hides it.
- [x] **Animated floor tiles** (2026-09-27): a tile whose DT1 header marks it animated (byte 7) cycles through the tiles of
  its file with the same orientation, main and sub index, by rarity (the frame number; Act IV's lava floor has sets of 10),
  a frame every 3 game ticks. No Act I area has any (the camp's stream is still in 1.10); checked 2026-10-10 in the River of
  Flame (lava, 10 frames). Kurast's water has none. `D2MODERN_DEBUG_ANIMATED=1` lists them as a scene loads.
- [ ] **Animation speed of floor tiles**: 3 ticks a frame is an estimate.
- [x] **Weather** (2026-10-10, `modern/src/Weather.h`): rain on levels.txt Rain levels outside, snow in Act V, from D2Client
  0x6FAA6AE0-0x6FAA88B0: the four phases (clear 300-600 s, building up, full 2-4 minutes, three times that in Acts III and
  V, dying down), 32-255 drops, the wind turning, drops as lines in the screen's pixels that move with the camera, their
  colours by period of day through the act palette's nearest entries, flakes swaying; Act V's levels set the snow (none on
  a quarter of Harrogath's visits, always on the Rocky Summit and in Nihlathak's Temple). The rain's sound (scene_rain)
  follows the storm's strength. `D2MODERN_WEATHER=2` starts the full storm; `D2MODERN_WEATHER_LOG=1`.
- [x] **Lightning** (checked 2026-10-10): needs nothing. D2Client has it (a white screen two frames 3 apart, then
  event_thunder, every 500-2000 frames: 0x6FAA8240), but a storm's chance of it is compiled to 0 (0x6FAA6CDA) and the only
  other switch, skills' client do-function 84 (0x6FACB070), is used by no skill in 1.10's skills.txt.
- [x] **Weather splashes** (2026-10-11): while the floors are drawn (D2Client 0x6FAA31BB), a floor tile whose DT1 header
  byte 6 has the water bit (2) gets a rain splash when rand(1000) < the rain's strength x 1000 (its drops / 256,
  0x6FAA7D36), and in a muddy level (levels.txt Mud) a bubble when rand(1000) < 100: at a random point of the tile's
  diamond (0x6FAA7590), `data\global\UncompOverlays\Rain3` or `Rain4.dc6` (kind 2 + rand(2)), the bubble `bubble3`
  30 pixels higher (0x6FAA7760), a frame every 2 ticks. The original rolls every drawn frame (rain at most every 3 ms,
  bubbles every 25 ms); here rain once per 40 ms (its 25 fps), so a faster screen doesn't rain harder. Checked in a storm
  on the Rogue Encampment's river (`D2MODERN_WEATHER=2 D2MODERN_LOOK_AT=207,8`, `D2MODERN_WEATHER_LOG=1` counts them).
- [x] **Walls in front of the player** (2026-09-26): a wall piece standing in front of the player (drawn after it) whose
  pixels cover at least 40 of the player's pixels is drawn 40% opaque, so the player shows through it. Checked in the Jail
  behind its south wall; pieces that only come near the player stay solid.
- [ ] **Wall fading details**: the 40% and the 40-pixel threshold are estimates; each wall piece fades as a whole, so
  a faded piece next to a solid one shows a straight edge (compare with the original); only the player's own sprite is
  tested (not monsters or the cursor); trees (orientation 14) fade like walls.
- [x] **Automap** (Tab): see "Not built yet" above for what is done and what is left.

## Useful tools

- `UiDump GAME list [filter]` lists interface files with their frame sizes.
- `UiDump GAME dump data/global/ui/... PREFIX [palette]` writes a frame sheet, or a whole 800x600 screen.
- `UiDump GAME panel FILE FIRST_FRAME PALETTE OUT.bmp` assembles a 320x432 panel.
- `UiDump GAME frontend title|main|select|loading OUT.bmp` renders a front-end screen offline.
- `StringProbe GAME find TEXT` searches the game's string tables for a label.
- `D2MODERN_TEST_DIE=N` kills the player on game tick N (D2Game `D2GameHost_Kill`); with `D2MODERN_DEATH=show` a capture
  waits until the player lies dead, with `D2MODERN_DEATH=respawn` it then presses Esc and waits until they live again.
- `D2MODERN_OPEN_QUESTS=ACT` on a capture opens the quest log at that act (1-5) and prints the first quests' flags/states;
  `D2MODERN_TEST_QUESTS="1:0x1 2:0x4"` sets quest flag bits at the start; `D2MODERN_TALK_LISTEN=1` with `--talk CLASS`
  chooses Talk and closes the conversation (the message counts as heard); `D2MODERN_TALK_LISTEN=show` captures the speech
  scrolling 8 seconds in. `StringProbe GAME ID...` also prints each string's key.
- `StringProbe GAME key KEY...` prints strings by key; `UiDump GAME cat` writes the exact bytes now (binary stdout).
- `D2MODERN_LIST_MONSTERS=1` prints the champions, uniques and minions in the scene with their map cells (aim
  `D2MODERN_LOOK_AT` and `D2MODERN_HOVER_ENEMY` at them); `D2MODERN_TEST_TRANSFORM=N` draws every monster in colour map N-1.
- `D2MODERN_SOUND_LOG=1` prints every sound effect asked for ("Sound: ..."); in a capture run the audio is created muted,
  so nothing is heard.
- `D2MODERN_OPEN_STASH=1` walks to the town stash and opens it; `D2MODERN_SHOW_CUBE=1` draws the cube panel;
  `D2MODERN_TEST_CUBE="code code ..."` opens a carried cube (give it and the items with `D2MODERN_TEST_GIVE`), puts those
  items in, presses Transmute and prints the cube before and after; `D2MODERN_SOUND_EVENT_LOG=1` prints packet 0x2C events;
  `D2MODERN_CLICKS="x,y x,y"` clicks those window points in turn after the other hooks (15 ticks each).
- `D2MODERN_WALK_TO=dx,dy` on a capture walks the player that many subtiles first (with `--travel LEVEL` and
  `D2MODERN_TEST_WAYPOINTS=1` to start in a dungeon, for example the Jail, 29).
- `D2MODERN_AUTOMAP=1` on a capture shows the automap. `UiDump GAME automapcheck LVLTYPE_ROW` counts which AutoMap.txt
  tile codes cover each orientation's tiles of a level type.
- `D2MODERN_HOVER_ENEMY=1` on a capture puts the cursor on the visible enemy nearest the middle (or an empty spot) and
  draws the cursor into the capture.
- `SaveInit DLL_DIR GAME_DIR SAVE OUT` plays a character for a moment and saves it to OUT, turning a header-only new
  character into a full save (starting gear, stats and skills blocks).
- `UiDump GAME cat ARCHIVE_PATH` prints a file from the archives (for example `data\global\excel\overlay.txt`).
- `UiDump GAME dccframes ARCHIVE_PATH` lists a DCC's frames: place, size and the rows that have pixels.
- `UiDump GAME charshadow TOKEN MODE` says whether a character animation casts a shadow.
- `UiDump GAME tilepair DT1 MAIN SUB OUT.bmp` draws the walls of one tile index with their shadow tile in red.
- Casting test: `D2MODERN_TEST_EXP=20000 D2MODERN_CAST_TICKS=600 D2MODERN_STOP_AT_MODE=10 D2MODERN_STOP_DELAY=N` with
  `--autoplay 400 --cast-skill 36` (Fire Bolt) captures the Sorceress N ticks into the cast and prints the overlay's
  table values.
- For captures of the live window, set `D2MODERN_SHOT_DIR` and send `WM_APP+7` to the window; the game saves its next
  frame there. Don't grab the screen, because that captures whatever else is on it.

## The "Expansion" rows in the tables (fixed 2026-10-03)

Sixteen of the game's .txt tables have a row named "Expansion" that divides the classic rows from Lord of
Destruction's. The game's compiled tables (.bin) leave it out; each .bin has one record fewer than its .txt has data
rows (objects 574 → 573, levels 134 → 133, and so on).

The client read the .txt files and counted that row. So every table it indexes by row number was one off from the
divider on:
- objects (from 408): every Act V object was drawn as its neighbour. In Harrogath the invisible "Larzuk Greeting"
  trigger (542) was drawn as a ground tomb (541) and could be clicked.
- uniqueitems (from 129) and setitems (from 62): Lord of Destruction unique and set item names.
- overlay (from 190) and LvlTypes (from 29).

The click crashed the game. Operating an object with no operate function (OperateFn 0) is fatal in the original
too (1.10f 0x6FC78AEC: assertion, then exit). The original client never offers such an object, because it isn't
selectable.

`TextTable` now drops the "Expansion" rows, as the compiler does. Tools: `Ds1ObjectProbe` lists a DS1's preset units;
`BinCountProbe` prints the record counts of compiled tables; `D2MODERN_LIST_OBJECTS=1` lists the objects near the
player.

Magic and rare names were wrong too (fixed 2026-10-03). An item's affix numbers are 1-based positions in D2Common's
combined tables:
- magic: magicsuffix, then magicprefix, then automagic (#10604);
- rare and crafted: raresuffix, then rareprefix (#10606).
Every row counts, empty-name rows included. `ItemTable` had counted named rows only and kept prefixes and suffixes
apart, so for example "Steel Long Sword of Quality" showed as "Long Sword of Butchery", and rare items had no name.

Checked with `D2MODERN_AFFIX_LOG=1 D2MODERN_TEST_GIVE="lsd:4 rin:4 cap:6 ..."`, which prints D2Common's own record
names beside the client's. All 19 affixes of the ten test items agree, and crafted items (quality 8) take rare names.

## Travelling out of Act IV or V (fixed 2026-10-03)

The player moved to another act's waypoint but the old map stayed. The cause was D2Common `PATH_RecacheRoom`: when it
found no room at a unit's position, it kept the old room, whereas 1.10f (0x6FDADBC2) sets the room to null for
anything that isn't a missile. `LEVEL_ChangeAct` relies on that: its first `D2Common_10229` call (to 0,0) empties
the room, so the second call finds the new act's room through its hint room. With the old room kept, the second call
found a room of the old act at the same coordinates. Harrogath and Lut Gholein overlap, so trips from Act V landed in
Harrogath at Lut Gholein's coordinates.

Checked with `--travel` from an Act V save and from an Act I save to levels 1, 40, 75, 103 and 109. Every trip now
reaches its level.

## Waypoints that stopped opening (fixed 2026-10-03)

After the first use of a waypoint, clicking a waypoint walked the player to it, but the menu didn't open. The original
client tells the game when the waypoint menu closes without travelling: packet 0x49 with the waypoint's id and level 0
(D2Client 0x6FB26299, 0x6FB26307, 0x6FB26674, 0x6FB269B9). That makes D2Game end the player's interaction
(`SUNIT_ResetInteractInfo`). Ours didn't send it, so `PLAYER_IsBusy` refused the next waypoint. `closeWaypointMenu`
now sends it; a travel already ends the interaction with its own 0x49.

Test: `D2MODERN_WAYPOINT_CLICK_TEST=<ticks held>` presses on the waypoint from eight spots around it, holds the button,
lets go and reports whether the menu opened. Before the fix 1/8 opened; after it 8/8, in Rogue Encampment and
Harrogath.

## Leap and Leap Attack (fixed 2026-10-03)

The player slid along the ground during a leap. The art came from skills.txt `seqtrans`, which is A1 for Leap, so
the whole leap drew attack frames standing on the ground. D2Common's own sequences (gPlayerSequenceLeap 0x6FDDF570,
gPlayerSequenceLeapAttack 0x6FDDF678) draw S1 for steps 0..14. In S1, frames 8..11 are in the air (feet 23-26 px up)
and 12..14 are the landing. Leap Attack goes on in A1 for its strike (A1 frame 5 unarmed, 6 with a weapon).
`leapSequenceMode` (SkillVisuals.h) gives that mode.

While in the air, the server rewinds the sequence to frame 10 at each frame-11 event until the player lands
(D2Game SKILLS_Leap → `D2GAME_SKILLS_RewindSkillEx(…,10)`), so the client holds frame 11 during the flight. After
landing, Leap Attack stays in the sequence mode (18) for the strike, so the steps now keep counting through the strike
instead of freezing on frame 11. Checked with a copy of MaxBarbarian: `D2MODERN_LEAP_LOG=1 --travel 3 --cast-skill
132` (or 143), and `SequenceArtProbe <game dir> ba s1 hth 4`, which prints where each frame's pixels sit.

## Skill survey of all seven classes (2026-10-04)

The per-skill checklist and its log are in [SKILLS-PROGRESS.md](SKILLS-PROGRESS.md).

Every skill of every class was cast with the class's Max save in the Cold Plains (`--cast-skill N --travel 3`, 80 ticks).
Skills that didn't cast there are passives, auras, skills that need corpses (Raise Skeleton, Revive, Corpse Explosion,
Find Item…), or skills that need other gear or a Druid form (bow skills on a spear Amazon, Double Swing, Maul…).
Found and fixed:

- **Sequence skills draw the game's own table.** The snapshot now carries the sequence step D2Game is on: elapsed game
  frames since `nSeqCurrentFramePrecise >> 8` (the mode's start, moved by rewinds) times `dwSeqSpeed`, plus the frame
  bonus (`UNITS_GetFrameBonus`), as SUnit 0x6FCBCE70 schedules it. The client draws the mode and frame of that row of
  the D2Common sequence table instead of skills.txt `seqtrans`. Jab alternates A1/A2, Charge runs then strikes, Arctic
  Blast uses S1, and Fists of Fire, Claws of Thunder and Blades of Ice use A2. `D2MODERN_SEQUENCE_LOG=1` prints it.
  The protocol is v5.
- **Whirlwind** (srvdofunc 76) also draws the unit turned 21/64 and 42/64 of a circle, before and after it, as
  D2Client 0x6FB4BA32/0x6FB4BB12 → 0x6FAC23F0 do. The game holds A1 frame 3 while spinning (rewinds to 3 on each strike).
- **4-direction art** (Twister, Blades of Ice and others): `fileDirection(4,d)` gave 2,2,3,3, so reordering moved one
  frame twice and drew an emptied picture (D3D11 "Upload asset texture failed", E_INVALIDARG). D2Client's 64-to-4 table
  (file offset 0xCEAD0) centres the four on 64-directions 0, 16, 32, 48, so file direction d is our quarter d.
- **Bone Wall and Bone Prison crashed the server** when a new piece replaced the oldest: D2MOO's
  `AIGENERAL_ExecuteCallbackOnMinions` and `AIGENERAL_SetOwnerData` lacked 1.10f's "owner/unit is a monster" checks
  (0x6FCCF620, 0x6FCCF273), and a summon's owner is the player.
- **Charge's trail** (srvdofunc 67): while running (the client's skill flag 1, i.e. the sequence's RN steps), D2Client
  0x6FAC9620 draws the skill's param3 earlier copies. Charge's start function (0x6FAC9AF0 area) sets param3 to 0 and
  its do function (0x6FAC9B50, cltdofunc table entry 36 at 0x6FB7763C) adds 1 on every running step, up to 2. Copy i
  stands i steps back along the unit's client-coordinate (screen) velocity, the step being that velocity times 15 over
  its approximate length (max + min/2), and shows one frame earlier per copy, wrapping within 8. Moving down the screen
  the copies are drawn before the unit, farthest first; otherwise after it. The unit drawer's fourth argument is 1
  there, the same value Whirlwind's copies pass for a player, so the copies are drawn like the unit itself.

- **Monsters drawn as players** (Decoy, Valkyrie, Shadow Warrior, Shadow Master) were invisible: their monstats2 art
  (code `vk`) has no pieces. Their states (states.txt `dopplezon`, `valkyrie`, `shadowwarrior`) have gfxtype 2, and
  D2Common #11013 then draws the monster as a player of the state's gfxclass in its own gear. The snapshot sends each
  such monster's player class, mode and look (`native::Unit::look*`, protocol v7, computed with the same composite
  functions as the player's), and `UnitArt::player` loads that class's art.
- **A layer with fewer directions than its COF** (the Raven: COF 16, body 8) made the whole animation fail to load;
  each layer now uses its own nearest direction.
- **Shapeshifted Druid** (Werewolf, Werebear): the player stayed drawn in human form. The snapshot now runs the mode it
  shows through D2Common #11013 (`D2COMMON_11013_ConvertMode`), which for a shapeshifted unit gives the form's monstats
  class (states.txt gfxclass) and a monster mode it has art for (#11014); the client draws that monster's art. The
  protocol is v6.
- The skills the first pass couldn't reach were tested with new hooks: `D2MODERN_CORPSE_SKILL=N` (after the cast,
  use N on the nearest corpse: Raise Skeleton raises drawn skeletons, Corpse Explosion, Poison Explosion, Revive and
  Raise Skeletal Mage cast), `D2MODERN_NEXT_SKILL=N` with `D2MODERN_NEXT_APPROACH=1` (a Druid form skill after the
  form, or a ranged skill after walking within 20 subtiles: Dragon Flight's range is 27 and it works), and
  `D2MODERN_TEST_WIELD="right,left"` with `D2MODERN_TEST_GIVE` (all ten bow skills with a short bow and arrows;
  Double Swing, Frenzy, Double Throw, Dragon Claw and Poison Dagger with their weapons).

## Bought gear that vanished (fixed 2026-10-05)

Buying armor (or any gear) whose place on the body was free took the gold and the store's copy, and the item showed
nowhere. 1.10f wears a bought item at once when its body slot is free (the buy handler calls 0x6FC4A9B0 with body
location 0, as D2MOO does). 0x6FC4A9B0 passes the address of its own body-location argument to the fit check
(0x6FC42F20), which writes back the slot the item fits, and places the item there; D2MOO's version gave the check a
separate variable and then placed the item at the location passed in, 0, which is no slot. Fixed in
source/D2Game/src/ITEMS/ItemMode.cpp. The same function also wears an item picked up from the ground into a free slot
and gear taken back from the corpse, which had the same fault.

A change made earlier the same day that skipped wearing for a stored-mode buy (SUnitNpc.cpp) is undone: 1.10f checks no
mode there. The signed-shift fix of the buy packet's multibuy bit (Input.cpp) stays.

Test: `--talk 154 --shop` with `D2MODERN_TEST_GOLD=50000 D2MODERN_SHOP_BUY=0,0,0` buys the cheapest item of Charsi's
armor page three times and prints where each went (a Paladin with only a sword and shield: cap on the head, buckler
in the backpack as the shield hand is taken, leather gloves on the hands). NativeShopTests now requires the armor it
buys to be in the backpack or on a real body location.
