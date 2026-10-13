# Skills progress

Where each class skill stands against the original 1.10f game: whether the game server runs it, whether the client draws
the character right, and whether its effects (missiles, overlays, sounds) match. Spells ("magic"), weapon skills, auras,
summons and passives are all here, one row per skill, grouped by class. The log at the end records what changed and when.

Details of how things were fixed stay in [UI-TODO.md](UI-TODO.md); this file is the checklist.

## Legend

| Mark | Meaning |
|:-:|---|
| ✅ | works and checked (how is in the Tested column) |
| ⚠️ | works partly, or differs from 1.10f (see Notes) |
| ❌ | broken |
| 👁 | seen drawn and plausible (right art, frames and effects from the game's own data), not yet compared side by side with 1.10f |
| ⬜ | not checked yet |
| — | doesn't apply (passives have nothing to cast or draw) |

Columns:

- **Type**: from skills.txt. *spell* is magic cast from where the hero stands; *weapon* is a melee, bow or throwing skill,
  or a sequence the weapon performs (Charge, Whirlwind, Leap); *aura*, *summon* and *passive* as in the game.
- **Server**: the game server accepts the skill and it does what it should (damage, summons, states, movement).
- **Look**: the character's animation (and a summon's or wall's art) compared by eye with the original.
- **Effects**: missiles, explosions, overlays, light and sounds compared with the original.
- **Tested**: when, and how (the test hooks are listed below).

## Overview (2026-10-09)

| Class | Skills | Passive | Server ✅ | Server ⬜ | Look ✅ | Look 👁 | Effects ✅ | Effects 👁 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Amazon | 30 | 6 | 24 | 0 | 4 | 20 | 0 | 20 |
| Sorceress | 30 | 4 | 26 | 0 | 3 | 22 | 0 | 22 |
| Necromancer | 30 | 3 | 27 | 0 | 2 | 19 | 15 | 5 |
| Paladin | 30 | 0 | 30 | 0 | 1 | 29 | 30 | 0 |
| Barbarian | 30 | 10 | 20 | 0 | 6 | 11 | 0 | 2 |
| Druid | 30 | 1 | 29 | 0 | 6 | 18 | 0 | 9 |
| Assassin | 30 | 2 | 28 | 0 | 5 | 20 | 1 | 9 |

Every active skill has been seen working on the server (the Paladin auras on 2026-09-27). Most skills have not yet been compared by eye with
the original; that is the bulk of the remaining work.

## Open work

- [ ] **Compare looks and effects by eye**, class by class, against the real 1.10f game (installed under D2SE). All
      sequence skills (skills.txt anim SQ) but Dragon Flight now have a frame strip checked against D2Common's table
      (see "How to test"). Next: the plain-mode skills (A1, SC, TH...) and every
      skill's effects.
- [x] **Untested on the server** (done 2026-10-04): Telekinesis, Iron Golem, Find Potion, Find Item, Grim Ward.
- [x] Telekinesis on objects (opens a chest) and Dragon Flight's frame strip (all 23 steps) — 2026-10-04.
- [x] Shadow Warrior and Master: not darkened in 1.10f as far as its client code and data show (see their rows).
- [x] Blaze: its flames use the same art as Fire Wall (groundFireBig), so tall flames are right.
- [x] Summon placement: where a summon appears is decided by the game server (D2MOO's 1.10f code), not the client.
- [x] **Curses' pictures** (2026-10-09): drawn by D2Client 1.10f's own curse functions (see the log).
- [x] **Buffs' stats on the player** (2026-10-09, `D2MODERN_STAT_LOG=1`): Battle Orders, Shout, Battle Command, Frozen,
      Shiver and Chilling Armor, Enchant, Fade, Werewolf and Werebear change the character sheet by their skills.txt
      formulas exactly (see the log). Paladin auras were checked on 2026-09-27.
- [x] **Curses' and warcries' stats on monsters** (2026-10-09): with `D2MODERN_STAT_LOG=1` the host prints a monster's
      stats whenever its states change. Amplify Damage, Weaken, Decrepify, Lower Resist, Taunt, Battle Cry, Cloak of
      Shadows and Inner Sight change them by their formulas exactly (see the log). The other curses (Dim Vision, Iron
      Maiden, Terror, Confuse, Life Tap, Attract) change behaviour, not stats.
- [x] **Behaviour curses** (2026-10-09, `D2MODERN_WATCH_TICKS=N`): Terror, Confuse, Attract, Iron Maiden and Dim Vision
      seen working on Fallen (see the log).
- [x] **Life Tap, Burst of Speed, Bone Armor** (2026-10-09): seen working (see the log).
- [x] **Energy Shield** (2026-10-09): takes 20% of the damage at level 1, as D2Game's EventFunc24 with min(EMin,95) gives.
- [x] **Cyclone Armor** (2026-10-09): absorbs the Fallen Shamans' fire, not melee hits (see the log).
- [x] **Client functions of the skills whose pictures are client-only** (skills.txt cltstfunc/cltdofunc, D2Client tables
      at 0x6FB77488 and 0x6FB775A8), 2026-10-09: curses, Corpse/Poison Explosion, Revive, Mind Blast. Most other
      skills' pictures are the server's missiles, which the host publishes (warcries, Shock Web, Shock Wave), and Cloak
      of Shadows' start function draws nothing.
- [x] **Curse center's light** (2026-10-09): a red light of the curse's radius (0x6FAA4280, `clientLights`). Seen in the
      Jail (`D2MODERN_CAST_AT=6,0`, Weaken): the floor around the spot turns faintly red against a capture without the cast.
- [x] **Corpse/Poison Explosion's lights** (2026-10-09): seen in Jail Level 1. Against a capture 6 ticks earlier the
      floor around the corpse warms (Corpse Explosion: the burst's and the light missile's 255,178,64) or turns green
      (Poison Explosion: greenlightmissile's 128,255,128); floor further away is unchanged.
- [x] **Audit of client functions** (2026-10-09): every player skill whose client functions add pictures the server
      doesn't make is now copied or shown not to need it (see the log). Sacrifice's blood (34) keeps the spray rule
      checked with the Paladin on 2026-09-27.
- [x] **Druid forms' colours** (2026-10-09): nothing tints them in 1.10f's data. The wolf and bear states (139, 140) have no
      colorpri/colorshift, and the form monsters (wolf 430 "40", bear 431 "TG") have TransLvl 0 and no COF\palshift.dat
      to shift with, so the default palette is right.
- [x] **Charge's trail** (2026-10-09): D2Client draws each copy with 0x6FB5A720(unit, x, y, 1, 0); Whirlwind's copies pass
      (x, y, caster is a player, 0), the same for a player, and our client draws both alike (drawTile, same blending).
- [x] **Skill sounds' timing** (2026-10-09): stsound when the action begins, dosound and tgtsound on the frame event
      (D2Client 0x6FABC475, 0x6FABC94E), Jab's dosound a on its sound frame (do-function 16); all used to play at the start.
- [x] **prgsound** (2026-10-09) where D2Client plays it: Tiger and Cobra Strike when a finishing move releases their
      charges, Hurricane looped while its state lasts, Arctic Blast when its stream starts (see the log).
- [ ] **Sounds by ear**: no skill's sounds are compared by ear with 1.10f yet.

## How to test

All hooks run in the smoke test (`--smoke-test --no-audio --capture FILE.bmp`) with a copy of a save. The level 99
characters are in `save/` (`MaxAmazon.d2s` … `MaxAssassin.d2s`): copy them, never test on the originals.

| What | Command (environment variables and arguments) |
|---|---|
| Cast skill N at the nearest monster | `D2MODERN_TEST_WAYPOINTS=1 D2MODERN_CAST_TICKS=80 ... --travel 3 --cast-skill N` |
| Centre the capture on the target | `D2MODERN_LOOK_AT=target` |
| Print the sequence step each tick | `D2MODERN_SEQUENCE_LOG=1` |
| Capture N ticks into the sequence mode | `D2MODERN_STOP_AT_MODE=18 D2MODERN_STOP_DELAY=N`; one run per N = 0, 1, 2... gives a frame strip to set beside the table (`source/D2Common/src/DataTbls/SequenceTbls.cpp`) |
| Give and hold weapons | `D2MODERN_TEST_GIVE="sbw aqv" D2MODERN_TEST_WIELD="sbw,aqv"` (right hand, left hand) |
| Use a skill on a corpse afterwards | `D2MODERN_CORPSE_SKILL=N` (after a killing skill, e.g. `--cast-skill 84` Bone Spear); `D2MODERN_CORPSE_TICKS=N` stops after N ticks. To find a corpse at once, give the killing skill `D2MODERN_CAST_TICKS=60` |
| Use a skill on a dropped item | `D2MODERN_TEST_GIVE=hax D2MODERN_ITEM_SKILL=90 D2MODERN_ITEM_SKILL_DROP=hax` (reports where the item went) |
| Cast at a spot from the player instead of at an enemy | `D2MODERN_CAST_AT=dx,dy` (a curse indoors, where no enemy is near the waypoint) |
| Use a second skill on the nearest enemy | `D2MODERN_NEXT_SKILL=N`, with `D2MODERN_NEXT_APPROACH=1` to walk within 20 subtiles and a clear line, opening doors on the way (also for `--cast-skill` and `CORPSE_SKILL`) |
| Server packets as the client receives them | `D2MODERN_SERVER_LOG=1` |
| Missiles, overlays, frame events, curse centers | `D2MODERN_MISSILE_LOG=1`, `D2MODERN_OVERLAY_LOG=1` |
| The player's vitals, sheet, strength and states before and after the cast; a monster's stats when its states change | `D2MODERN_STAT_LOG=1` |
| After the cast, keep going N ticks and report each enemy's distance, life, modes and states | `D2MODERN_WATCH_TICKS=N` |
| Aim only at one monster class (a caster, for elemental damage) | `D2MODERN_TARGET_CLASS=N` (monstats hcIdx) |
| Make the player easy to hit (level 1, defense 0; cheat 7) | `D2MODERN_TEST_NODEFENSE=1` (with `WATCH_TICKS`) |
| Meanwhile: life to pct% first; attack the nearest enemy; walk to an offset and report the speed | `D2MODERN_TEST_HURT=pct`, `D2MODERN_WATCH_ATTACK=1`, `D2MODERN_WATCH_WALK=dx,dy` (with `WATCH_TICKS`) |
| Walk closer before casting a skill centred on the caster | `D2MODERN_NEXT_APPROACH=1 D2MODERN_APPROACH_RANGE=5` |
| Leap frames | `D2MODERN_LEAP_LOG=1` |
| Where each frame of an animation sits | `SequenceArtProbe <game dir> ba s1 hth 4` |
| The skill list with its skills.txt columns | `SkillListProbe <game dir>` |

Skill ids: Amazon 6-35, Sorceress 36-65, Necromancer 66-95, Paladin 96-125, Barbarian 126-155, Druid 221-250,
Assassin 251-280. A crash with a named stack: run `out/modern/RelWithDebInfo/D2Next.exe` (build it with
`cmake --build out/modern --config RelWithDebInfo --target D2Next`).

## Log

Newest first. Add an entry for every change to a skill, and update its row.

- **2026-10-09**
  - Curses (66, 71, 72, 76, 77, 81, 82, 86, 87, 91) drawn as D2Client 1.10f does, read from its code. At the cast's
    start (cltstfunc 18, 0x6FAC7F90) one cltmissilec (cursecast) where the target is. On the cast's frame event
    (cltdofunc 30, 0x6FAC7FF0) an invisible cursecenter there, with the curse's cltmissilea as its picture and
    aurarangecalc − 3 (at least 2) as its radius. Its pCltDoFunc 17 (0x6FB32310 → 0x6FB35D10) fills that disc at 3 and 6
    frames before it ends: spots 2 subtiles apart with a 1-in-radius/2 chance (radius above 4), else every subtile with a
    1-in-radius chance. Before, three copies of both pictures were stacked on the target at the cast's start.
  - The host now reports the player's AnimData frame event (D2Common #10382; protocol v9), which is when D2Client runs a
    skill's cltdofunc. It comes one tick after the server applies the curse.
  - Warcries (cltdofunc 25, 0x6FACA570) would add 64 copies of their cltmissilea from the caster; the server already
    makes that ring (srvmissilea) and it is drawn, so the client adds nothing.
  - `D2MODERN_LOOK_AT=target` centres a capture on the unit last targeted.
  - Corpse Explosion (74, cltstfunc 21 / cltdofunc 32, 0x6FAC8530 / 0x6FAC85F0): the start keeps the corpse's subtile
    and class; on the frame event 0x6FABD8A0 lays the guts by the corpse's monstats2 size (small: 1; large: 3 within 3
    subtiles and 4 of the guts' SubMissile1 within 2; otherwise 2 within 2 and 3 of SubMissile2 within 1), facing the
    four diagonals in turn, then cltmissileb (the burst). Poison Explosion (83, cltdofunc 33, 0x6FAC8940): one
    cltmissilea. Before, the start-of-cast rule stacked copies of all three pictures at the cast's start.
  - Revive (95, cltstfunc 24, 0x6FAC8D00): one picture on the corpse by its monstats2 OverlayHeight (1 revivesmall, 3
    revivelarge, else revivemedium); was three copies of each.
  - Mind Blast (273, cltdofunc 8, 0x6FABFD40): the curse's disc with a visible center (mindblast center carrying
    mindblast hit, radius aurarangecalc).
  - `D2MODERN_CORPSE_TICKS=N` ends the corpse-skill hook after N ticks (for a capture mid-effect).
  - Raise Skeleton and Raise Skeletal Mage (70, 80; cltstfunc 20 / cltdofunc 31, 0x6FAC8370 / 0x6FAC8440): one
    corpseexplosion on the corpse at the frame event; was three at the cast's start.
  - Corpse/Poison Explosion's light missiles and the curse center's red light (not yet seen, see Open work).
  - Teeth and Bone Spear (67, 84; cltstfunc 19, 0x6FAC8150): one bonecast 2 subtiles from the caster toward the target,
    facing it; was three copies on the target.
  - Inferno and Arctic Blast (41, 230; cltdofunc 24, 0x6FACA1D0): the client's stream tosses a coin between
    cltmissilea and cltmissileb for each flame; the server's published stream is all cltmissilea, so half its flames (by
    number) are drawn as cltmissileb (Flamethrower2, ArcticBlastIce_2).
  - `D2MODERN_CAST_AT=dx,dy` casts at a spot; `D2MODERN_NEXT_APPROACH` logs the distance at each step ("Approach: ...").
  - `D2MODERN_NEXT_APPROACH` opens closed doors on the route (objects.txt IsDoor, mode 0; the collision grid doesn't
    hold them and the server blocks them), keeps walking until the line to the target is clear, and the cast and corpse
    hooks skip enemies inside walls (gargoyletrap). The corpse hook's summary gives the tick it ends at. With these the
    corpse skills run indoors: `--travel 29` (Jail Level 1), `D2MODERN_CAST_TICKS=600 D2MODERN_CORPSE_SKILL=74`.
  - Buffs' stats (`D2MODERN_STAT_LOG=1`, each at level 1 on the Max saves):
    - Battle Orders: max life and mana 8191 → 11057 (+35%, Param3). Shout: defense 339 → 600, +100% (Param1) on top of Iron
      Skin's +30% (UNITS_GetDefense: (dex/4 + armour class) × (1 + percent/100)). Battle Command: its own level 1 → 2
      (+1 all skills), and Iron Skin and Natural Resistance with it.
    - Frozen Armor 255 → 331 (+30%), Shiver Armor and Chilling Armor 255 → 369 (+45%), Enchant attack 5065 → 6078 (+20%,
      ToHit), Fade all resistances 0 → 20 (dm12: 10 toward 75).
    - Werewolf life +45% (Param2 25 + Lycanthropy's 20), attack +50%. Werebear life +70% (Param5 50 + 20), defense +25%,
      damage +50% on top of the +1023% bare hands get from strength 1023 (11-67 → 11-70).
  - Curses' and warcries' stats on monsters (Fallen, level 1, `D2MODERN_STAT_LOG=1`):
    - Amplify Damage physical resistance 0 → −100. Weaken damage −33%. Decrepify physical resistance −50, damage −50%,
      velocity 75 → 25%, attack rate 100 → 50. Lower Resist fire, lightning, cold and poison −32 (dm56: 25 toward 70).
    - Taunt damage and to-hit −5%. Battle Cry defense 10 → 5 (−50%), damage −25% (with `D2MODERN_APPROACH_RANGE=5`).
      Cloak of Shadows defense 10 → 9 (−15%). Inner Sight defense 10 → −30 (armour class −40, its EMin).
  - Behaviour curses (`D2MODERN_WATCH_TICKS`, Fallen in the Cold Plains, level 1):
    - Terror: from 6-18 subtiles the four cursed Fallen run (mode 15) to 27-39, about the AI's flee distance 30 (D2Game
      0x6FCE7E80, AiParam1 or 30). From 45 they stand idle, which is that function's rule too, not a fault.
    - Confuse: the cursed Fallen attack (mode 4) and lose life (9 → 5, 6 → 3) with the player out of reach.
    - Attract: the attracted Fallen loses life (6 → 4) while its neighbours attack.
    - Iron Maiden: the player loses 2 life and the cursed attacker 3 (200% reflected, in whole points).
    - Dim Vision: the cursed Fallen further away wander off instead of closing in; the uncursed one walks up and attacks.
    - Life Tap: at 50% life (`D2MODERN_TEST_HURT=50`, cheat 6) and attacking three cursed Fallen, the player heals 4095 →
      4142; the same fight after Dim Vision heals nothing.
    - Burst of Speed: a 32-subtile walk at 0.459 subtiles a tick against 0.372 after Fade, +23.3% (dm12 at level 1: +23%).
    - Bone Armor: with the player easy to hit (`D2MODERN_TEST_NODEFENSE=1`) the Dark Hunters' blows drain its absorb pool
      12800 → 6656 (1/256 points, 50 at the start) in 130 ticks while life stays 8191.
    - Monsters' hits: their chance is 2·AR/(AR+defense)·alvl/(alvl+dlvl), at least 5%, so a level 1 monster hits a level
      99 player 5% of the time whatever the defense. `D2MODERN_TEST_NODEFENSE=1` (cheat 7) makes the player level 1 with
      defense 0. (The Cold Plains' class 43 is the Dark Hunter, corruptrogue1, not a Fallen as written above.)
    - Energy Shield: 800 ticks among the Dark Hunters cost 164 life against 206 after Enchant, 79.6% (20% absorbed at
      level 1). The mana it costs (about 2 a point) is lost in the Max save's mana regeneration.
    - Cyclone Armor, in the Outer Cloister (`--travel 27`, the Fallen Shamans fallenshaman3 are class 60 near its
      waypoint; `D2MODERN_TARGET_CLASS=60 D2MODERN_APPROACH_RANGE=14`). With `D2MODERN_STAT_LOG` the host prints the
      absorb pool (bonearmor) when it changes: 48 at level 1 (12288/256), and each fire bolt takes 2-3 off it while life
      stays 8191. Next to a shaman (range 8) its melee hits cost 79 life and the pool stays full: physical isn't absorbed.
  - Skill sounds as D2Client times them: stsound at the action's start, dosound and tgtsound on the frame event (Frozen
    Armor: coldcast.wav, then frozenarmor.wav 7 ticks later; Psychic Hammer: hammer.wav, then vengeanceimpact1.wav on the
    target), Jab's dosound a for a player. A skill whose sound comes later no longer also gets the plain swing sound.
  - Druid forms' colours and Charge's trail settled from the data and D2Client (see Open work); nothing to change.
  - prgsound and prgoverlay from D2Client 1.10f:
    - Tiger Strike and Cobra Strike (cltprgfunc 12, 13: 0x6FAC08E0, 0x6FAC09F0): packet 0xA3 (D2Game sub_6FCC6300, sent by
      SkillAss sub_6FCF77E0 when a finishing move hits with charges up, byte 1 the charge count; D2Client's handler
      0x6FAB2D60, packet table row 0xA3 at 0x6FB76A14) plays prgsound + (n - 1) on the caster and prgoverlay + (n - 1) on
      the target. Seen with Dragon Talon after one charge: assassin_chargeup_tiger_1 (tigera.wav) and tigerstrike1.
    - Hurricane: its state has cltactivefunc 90 (0x6FAC2C90), which keeps prgsound (druid_hurricane) going while the
      state lasts; the client loops it (`Audio::startLoop`) from the state's start to its end.
    - Arctic Blast (cltstfunc 15, 0x6FACA050): prgoverlay and prgsound once when the stream starts (not while already
      streaming, state 12), which is when the client's action starts: unchanged.
  - Needed nothing: Strafe (do 20 picks the bolt without a bow; the server already sends strafebolt then), Blade Fury
    (do 6 uses cltmissilea, the server's bladefragment1, unless an aurastate stat picks another).
- **2026-10-04**
  - Any skill used while walking somewhere far away was cut off: the client re-sends a walk every 10 ticks until the
    destination is reached, and a skill didn't cancel that destination (Dragon Flight became a walk after 3 of its 23
    steps). A skill used on a spot or a unit (packets 0x05-0x10) now ends the walk, as in the original client.
  - Telekinesis opens a closed chest. A waypoint just arrived through can't be targeted (it goes from mode 1 to 2
    without regaining TARGETABLE); 1.10f's D2Game does the same (0x6FC74AC0).
  - After a waypoint travel the client no longer also sends "waypoint closed" (0x49, level 0).
  - Every active skill captured twice, a few ticks into its animation mode and 4 ticks after its first missile, and the
    captures looked through class by class (the 👁 marks). Found and fixed:
    - Decoy, Valkyrie, Shadow Warrior and Shadow Master were not drawn at all. states.txt gives their states gfxtype 2:
      D2Common #11013 draws such a monster as a player of the state's gfxclass (Amazon, Assassin) in its own gear. The
      snapshot now sends that look for each such monster (protocol v7) and the client draws the class's player art.
    - The Raven was not drawn: its COF has 16 directions and its body file 8, which the loader refused. A layer with
      fewer directions now uses its nearest direction, as the game reads each layer with its own count.
  - Last five server checks: Find Potion drops a potion and Find Item drops items from corpses, Grim Ward raises its
    ward, Iron Golem uses up a dropped axe and the golem appears, Telekinesis picks a dropped potion up. Every active
    skill has now been seen working on the server.
  - Sequence skills checked frame by frame: Jab, Impale, Inferno, Lightning, Chain Lightning, Frenzy, Arctic Blast,
    Fists of Fire, Blade Fury, Claws of Thunder, Blades of Ice. Each capture (one per tick from the start of mode 18,
    `D2MODERN_STOP_AT_MODE=18 D2MODERN_STOP_DELAY=n`) shows the mode and frame of D2Common's table row for that step,
    drawn with the class's art for that weapon; no step falls back to standing or walking art. Not compared with the
    original game itself yet.
  - Charge (107): the running trail of up to two earlier copies, as D2Client 0x6FAC9620 draws it.
  - Werewolf / Werebear (223, 228): the Druid is drawn as the form's monster (D2Common #11013). The form skills (232,
    233, 238, 239, 242, 243, 248) work on the server.
  - Corpse skills (70, 74, 80, 83, 95), bow skills (6-31), Double Swing, Frenzy, Double Throw, Dragon Claw, Poison
    Dagger and Dragon Flight tested with the right gear, corpses or range.
  - Whirlwind (151): the two turned copies (D2Client 0x6FAC23F0).
  - All sequence skills draw the mode and frame of D2Common's sequence table for the step the game is on (was
    skills.txt seqtrans: Jab now alternates A1/A2, Charge runs then strikes, Arctic Blast uses S1, Fists of Fire,
    Claws of Thunder and Blades of Ice use A2).
  - Bone Wall, Bone Prison (78, 88): server crash fixed (D2Game AiGeneral owner checks from 1.10f).
  - Twister, Blades of Ice (240, 274) and all 4-direction art: crash fixed (direction order from D2Client's table).
  - Survey: every skill of every class cast with the Max saves in the Cold Plains.
- **2026-10-03**
  - Leap, Leap Attack (132, 143): S1 jump art (was A1, the character slid); Leap Attack's strike after landing. Speed
    and take-off timing checked against D2Game 1.10f (run speed, flight starts at frame 11).
- **2026-09-30**
  - Missile details: explosions on early removal, Meteor, Blizzard, poison clouds, Lightning, Bone Spear trail,
    firewalls, Eruption, and the impact pictures of many missiles (see UI-TODO, "Skill effect details").
- **2026-09-27**
  - All 30 Paladin skills checked: server overlays, client hit missiles, SubLoop, sequence modes, Sacrifice's blood,
    Sanctuary / Open Wounds / Redemption state effects, elemental hit overlays, Holy Shield's shield art.
- **2026-09-26**
  - Casting overlays and lasting state overlays.

## Skills

### Amazon

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 6 | Magic Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 7 | Fire Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 8 | Inner Sight | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 9 | Critical Strike | passive | — | — | — | — | passive: works through stats |
| 10 | Jab | weapon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | A1/A2 thrusts; strip checked against the table 2026-10-04 |
| 11 | Cold Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 12 | Multiple Shot | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 13 | Dodge | passive | — | — | — | — | passive: works through stats |
| 14 | Power Strike | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 15 | Poison Javelin | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 16 | Exploding Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 17 | Slow Missiles | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 18 | Avoid | passive | — | — | — | — | passive: works through stats |
| 19 | Impale | weapon | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | slow A1 thrust; strip checked against the table 2026-10-04 |
| 20 | Lightning Bolt | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 21 | Ice Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 22 | Guided Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 23 | Penetrate | passive | — | — | — | — | passive: works through stats |
| 24 | Charged Strike | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 25 | Plague Javelin | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 26 | Strafe | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) | skills.txt anim A1, no sequence: the plain attack is right |
| 27 | Immolation Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 28 | Decoy | summon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | drawn as the Amazon (states.txt dopplezon gfxtype 2 → player art, fixed 2026-10-04; was invisible) |
| 29 | Evade | passive | — | — | — | — | passive: works through stats |
| 30 | Fend | weapon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 31 | Freezing Arrow | weapon | ✅ | 👁 | 👁 | 2026-10-04 with a short bow and arrows (`TEST_WIELD=sbw,aqv`) |  |
| 32 | Valkyrie | summon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | drawn as a gold-armoured Amazon with her gear (state valkyrie → player art, fixed 2026-10-04; was invisible) |
| 33 | Pierce | passive | — | — | — | — | passive: works through stats |
| 34 | Lightning Strike | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 35 | Lightning Fury | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |

### Sorceress

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 36 | Fire Bolt | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 37 | Warmth | passive | — | — | — | — | passive: works through stats |
| 38 | Charged Bolt | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 39 | Ice Bolt | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 40 | Frozen Armor | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 41 | Inferno | spell | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | SC raise, held at 9 while the flames stream; strip checked against the table 2026-10-04 |
| 42 | Static Field | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 43 | Telekinesis | spell | ✅ | ⬜ | ⬜ | 2026-10-04 on a dropped potion (`ITEM_SKILL`) | picks up a dropped potion; opens a closed chest (objects, 2026-10-04). A waypoint just arrived through is not targetable: it goes 1→2 without regaining TARGETABLE, the same in 1.10f (D2Game 0x6FC74AC0) |
| 44 | Frost Nova | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 45 | Ice Blast | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 46 | Blaze | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) | flames are groundFireBig, the same art as Fire Wall (missiles.txt), and the skill adds Fire Wall pictures client-side: tall flames are right |
| 47 | Fire Ball | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 48 | Nova | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 49 | Lightning | spell | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | SC cast, bolt on release; strip checked against the table 2026-10-04 |
| 50 | Shiver Armor | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 51 | Fire Wall | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 52 | Enchant | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 53 | Chain Lightning | spell | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | SC cast, bolt on release; strip checked against the table 2026-10-04 |
| 54 | Teleport | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 55 | Glacial Spike | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 56 | Meteor | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 57 | Thunder Storm | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 58 | Energy Shield | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 59 | Blizzard | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 60 | Chilling Armor | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 61 | Fire Mastery | passive | — | — | — | — | passive: works through stats |
| 62 | Hydra | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 63 | Lightning Mastery | passive | — | — | — | — | passive: works through stats |
| 64 | Frozen Orb | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 65 | Cold Mastery | passive | — | — | — | — | passive: works through stats |

### Necromancer

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 66 | Amplify Damage | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 67 | Teeth | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) | bonecast placed as D2Client's cltstfunc 19 (2026-10-09) |
| 68 | Bone Armor | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 69 | Skeleton Mastery | passive | — | — | — | — | passive: works through stats |
| 70 | Raise Skeleton | summon | ✅ | ✅ | ✅ | 2026-10-09 against D2Client 1.10f's code, captured | skeletons drawn; one corpseexplosion on the corpse at the frame event (cltstfunc 20 / cltdofunc 31) |
| 71 | Dim Vision | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 72 | Weaken | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 73 | Poison Dagger | weapon | ✅ | ⬜ | ⬜ | 2026-10-04 with a dagger |  |
| 74 | Corpse Explosion | spell | ✅ | ⬜ | ✅ | 2026-10-09 against D2Client 1.10f's code, captured on a Fallen corpse | guts and blood by the corpse's monstats2 size, then the burst (cltstfunc 21 / cltdofunc 32) |
| 75 | Clay Golem | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 76 | Iron Maiden | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 77 | Terror | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 78 | Bone Wall | summon | ✅ | ✅ | ⬜ | 2026-10-04 after the crash fix, repeated casts | bone pile drawn; server crash fixed 2026-10-04 (AiGeneral owner checks) |
| 79 | Golem Mastery | passive | — | — | — | — | passive: works through stats |
| 80 | Raise Skeletal Mage | summon | ✅ | ⬜ | ✅ | 2026-10-09 against D2Client 1.10f's code | the same functions as Raise Skeleton |
| 81 | Confuse | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 82 | Life Tap | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 83 | Poison Explosion | spell | ✅ | ⬜ | ✅ | 2026-10-09 against D2Client 1.10f's code, captured | poisoned guts (cltdofunc 33); the green clouds are the server's |
| 84 | Bone Spear | spell | ✅ | 👁 | 👁 | 2026-10-09 capture | bonecast placed as D2Client's cltstfunc 19, in front of the caster |
| 85 | Blood Golem | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 86 | Attract | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 87 | Decrepify | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 88 | Bone Prison | summon | ✅ | 👁 | ⬜ | 2026-10-04 after the crash fix, repeated casts | server crash fixed 2026-10-04 |
| 89 | Summon Resist | passive | — | — | — | — | passive: works through stats |
| 90 | Iron Golem | summon | ✅ | ⬜ | ⬜ | 2026-10-04 on a dropped hand axe (`ITEM_SKILL`) | the axe is used up and the golem appears |
| 91 | Lower Resist | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's curse code, captured on Fallen | cursecast on the target, then the curse's CurseArea sparks over a disc of aurarangecalc − 3 (cltstfunc 18 / cltdofunc 30) |
| 92 | Poison Nova | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 93 | Bone Spirit | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 94 | Fire Golem | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 95 | Revive | spell | ✅ | ⬜ | ✅ | 2026-10-09 against D2Client 1.10f's code, captured | one revive picture on the corpse by its monstats2 OverlayHeight (cltstfunc 24) |

### Paladin

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 96 | Sacrifice | weapon | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 97 | Smite | weapon | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 98 | Might | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 99 | Prayer | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 100 | Resist Fire | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 101 | Holy Bolt | spell | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 102 | Holy Fire | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 103 | Thorns | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 104 | Defiance | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 105 | Resist Cold | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 106 | Zeal | weapon | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 107 | Charge | weapon | ✅ | ✅ | ✅ | 2026-09-27 (all 30 Paladin skills) | runs (RN) with trail, then A1; trail from D2Client 0x6FAC9620 |
| 108 | Blessed Aim | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 109 | Cleansing | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 110 | Resist Lightning | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 111 | Vengeance | weapon | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 112 | Blessed Hammer | spell | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 113 | Concentration | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 114 | Holy Freeze | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 115 | Vigor | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 116 | Conversion | weapon | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 117 | Holy Shield | spell | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 118 | Holy Shock | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 119 | Sanctuary | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 120 | Meditation | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 121 | Fist of the Heavens | spell | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 122 | Fanaticism | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 123 | Conviction | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 124 | Redemption | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |
| 125 | Salvation | aura | ✅ | 👁 | ✅ | 2026-09-27 (all 30 Paladin skills) |  |

### Barbarian

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 126 | Bash | weapon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 127 | Sword Mastery | passive | — | — | — | — | passive: works through stats |
| 128 | Axe Mastery | passive | — | — | — | — | passive: works through stats |
| 129 | Mace Mastery | passive | — | — | — | — | passive: works through stats |
| 130 | Howl | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 131 | Find Potion | spell | ✅ | ⬜ | ⬜ | 2026-10-04 on a corpse (`CORPSE_SKILL` after Bash) | a potion drops; skills.txt range h2h only means walking up to the corpse |
| 132 | Leap | weapon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | S1 jump: crouch, airborne 8-11, landing; flight held at frame 11 (server rewinds to 10) |
| 133 | Double Swing | weapon | ✅ | ✅ | ⬜ | 2026-10-04 with two hand axes (`TEST_WIELD=hax,hax`) | dual-wield strike |
| 134 | Pole Arm Mastery | passive | — | — | — | — | passive: works through stats |
| 135 | Throwing Mastery | passive | — | — | — | — | passive: works through stats |
| 136 | Spear Mastery | passive | — | — | — | — | passive: works through stats |
| 137 | Taunt | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 138 | Shout | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 139 | Stun | weapon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 140 | Double Throw | weapon | ✅ | ✅ | ⬜ | 2026-10-04 with two throwing axes | throw |
| 141 | Increased Stamina | passive | — | — | — | — | passive: works through stats |
| 142 | Find Item | spell | ✅ | ⬜ | ⬜ | 2026-10-04 on a corpse (`CORPSE_SKILL` after Bash) | items drop; skills.txt range h2h only means walking up to the corpse |
| 143 | Leap Attack | weapon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | S1 jump then A1 strike; strike resumes A1 5 (hand-to-hand) / 6 |
| 144 | Concentrate | weapon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 145 | Iron Skin | passive | — | — | — | — | passive: works through stats |
| 146 | Battle Cry | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 147 | Frenzy | weapon | ✅ | ✅ | ⬜ | 2026-10-04 with two hand axes (`TEST_WIELD=hax,hax`) | two-axe A1 swings; strip checked against the table 2026-10-04 |
| 148 | Increased Speed | passive | — | — | — | — | passive: works through stats |
| 149 | Battle Orders | spell | ✅ | 👁 | 👁 | 2026-10-09 capture | the 64-missile ring is the server's (srvmissilea), published like any missile; D2Client's cltdofunc 25 would add the same ring, so the client adds none |
| 150 | Grim Ward | spell | ✅ | ⬜ | ⬜ | 2026-10-04 on a corpse (`CORPSE_SKILL` after Bash) | the ward appears; skills.txt range h2h only means walking up to the corpse |
| 151 | Whirlwind | weapon | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | A1 frame 3 + two turned copies; spin copies from D2Client 0x6FAC23F0 |
| 152 | Berserk | weapon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 153 | Natural Resistance | passive | — | — | — | — | passive: works through stats |
| 154 | War Cry | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 155 | Battle Command | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |

### Druid

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 221 | Raven | summon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | art fixed 2026-10-04 (COF 16 directions, body file 8); its attack (S1) is an additive white flash by its COF |
| 222 | Werewolf | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 223 | Werewolf | spell | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | drawn as the wolf; form art via D2Common #11013 |
| 224 | Lycanthropy | passive | — | — | — | — | passive: works through stats |
| 225 | Firestorm | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 226 | Oak Sage | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 227 | Summon Spirit Wolf | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 228 | Werebear | spell | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | drawn as the bear; form art via D2Common #11013 |
| 229 | Molten Boulder | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 230 | Arctic Blast | spell | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | S1 with the cold stream; strip checked against the table 2026-10-04 |
| 231 | Carrion Vine | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 232 | Feral Rage | weapon | ✅ | ✅ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) | wolf form |
| 233 | Maul | weapon | ✅ | ✅ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) | bear form |
| 234 | Fissure | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 235 | Cyclone Armor | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 236 | Heart of Wolverine | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 237 | Summon Dire Wolf | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 238 | Rabies | weapon | ✅ | ⬜ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) |  |
| 239 | Fire Claws | weapon | ✅ | ⬜ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) |  |
| 240 | Twister | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) | texture crash fixed 2026-10-04 (4-direction art) |
| 241 | Solar Creeper | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 242 | Hunger | weapon | ✅ | ⬜ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) |  |
| 243 | Shock Wave | spell | ✅ | ⬜ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) |  |
| 244 | Volcano | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 245 | Tornado | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 246 | Spirit of Barbs | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 247 | Summon Grizzly | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 248 | Fury | weapon | ✅ | ⬜ | ⬜ | 2026-10-04 after Werewolf/Werebear (`NEXT_SKILL`) |  |
| 249 | Armageddon | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 250 | Hurricane | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |

### Assassin

| ID | Skill | Type | Server | Look | Effects | Tested | Notes |
|---:|---|---|:-:|:-:|:-:|---|---|
| 251 | Fire Blast | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 252 | Claw Mastery | passive | — | — | — | — | passive: works through stats |
| 253 | Psychic Hammer | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 254 | Tiger Strike | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 255 | Dragon Talon | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 256 | Shock Web | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 257 | Blade Sentinel | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 258 | Burst of Speed | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 259 | Fists of Fire | weapon | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | A2 strikes then S4, burning ground; strip checked against the table 2026-10-04 |
| 260 | Dragon Claw | weapon | ✅ | ✅ | ⬜ | 2026-10-04 with two katars | claw strikes |
| 261 | Charged Bolt Sentry | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 262 | Wake of Fire | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 263 | Weapon Block | passive | — | — | — | — | passive: works through stats |
| 264 | Cloak of Shadows | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) | its cltstfunc 7 (0x6FABF910) only checks the caster's aurastate: no picture of its own |
| 265 | Cobra Strike | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 266 | Blade Fury | spell | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | SC throws; strip checked against the table 2026-10-04 |
| 267 | Fade | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 268 | Shadow Warrior | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) | drawn as an Assassin in her gear (fixed 2026-10-04; was invisible). Not darkened: 1.10f's D2Client only gives these classes an inventory (0x6FB40F50) and skips one call for them (0x6FB43C32); states.txt, monstats2 and their umod (42 lightningdeath) give no colour |
| 269 | Claws of Thunder | weapon | ✅ | ✅ | ⬜ | 2026-10-04 survey (Cold Plains) | A2 strikes then S4; strip checked against the table 2026-10-04 |
| 270 | Dragon Tail | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 271 | Lightning Sentry | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 272 | Wake of Inferno | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 273 | Mind Blast | spell | ✅ | 👁 | ✅ | 2026-10-09 against D2Client 1.10f's code, captured | mindblast center carrying mindblast hit over a disc of aurarangecalc (cltdofunc 8) |
| 274 | Blades of Ice | weapon | ✅ | ✅ | 👁 | 2026-10-04 survey (Cold Plains) | A2 strikes then S4, ice bursts; strip checked against the table 2026-10-04; texture crash fixed 2026-10-04 (4-direction art) |
| 275 | Dragon Flight | weapon | ✅ | ✅ | ⬜ | 2026-10-04 within range 27 (`NEXT_APPROACH`) | full 23-step sequence (SC 0-9, teleport, KK 0-12) checked 2026-10-04; was cut off after 3 steps when cast during a long walk (the client's walk hops interrupted any skill; fixed); range 27 (aurarangecalc), out of range the game refuses, like 1.10f |
| 276 | Death Sentry | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 277 | Blade Shield | spell | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
| 278 | Venom | spell | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) |  |
| 279 | Shadow Master | summon | ✅ | 👁 | ⬜ | 2026-10-04 survey (Cold Plains) | drawn as an Assassin in her gear (fixed 2026-10-04; was invisible). Not darkened: 1.10f's D2Client only gives these classes an inventory (0x6FB40F50) and skips one call for them (0x6FB43C32); states.txt, monstats2 and their umod (42 lightningdeath) give no colour |
| 280 | Phoenix Strike | weapon | ✅ | 👁 | 👁 | 2026-10-04 survey (Cold Plains) |  |
