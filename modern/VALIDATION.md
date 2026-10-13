# Modern client and native DLL validation

From the repository root:

```powershell
.\tools\modern\Test-Modern-Native.ps1 -IncludeCombat
```

The script builds the modern client in `out/modern-review`, independently of the
normal launcher build. It uses the already-built DLLs in
`out/build/X64Dlls/runtime/Release`; it does not rebuild or replace them.
The default game directory is `C:\games\Diablo II 1.10` and the default save is
`save\dfsd.d2s` there. Override them with `-GameDir`, `-Save` and `-DllDir`.

Each run creates `out/modern-validation/<timestamp>-<id>/` with a copied save,
per-check output/error logs, sprite and scene BMPs, and `report.json`. Tests only
receive the copy. Both the original and copied save hashes must remain unchanged.
Logs may contain the character name and local file paths; review before sharing.

Checks run sequentially because native sessions share a diagnostic log:

- Fresh configure/build, followed by the regular CTest suite.
- Real MPQ character animation decoding and NPC/monster sprite loading.
- Native town generation, repeatability and resource cleanup.
- Native session startup, appearance, movement, held input and replay.
- With `-IncludeCombat`: walk into the Blood Moor and hurt an enemy.
- WARP and hardware render smoke tests with byte-for-byte capture comparison.
  The hardware test must report an actual hardware device; WARP fallback does not
  satisfy it.

A failed executable, timeout, render mismatch, modified save, or changed source/
DLL fingerprint makes the overall script exit nonzero. Source fingerprints cover
the modern sources/tests, shared native headers, squall, StormLib sources, build
definition, mock transport, and the runner. They are checked again after the run
to catch concurrent edits. A consistent fingerprint is not original-game parity:
it makes the tested inputs identifiable. The combat check exercises one scripted
route and encounter; it is not exhaustive gameplay coverage.

`-TimeoutSeconds` controls the per-process limit (default 600). A timeout terminates
that check's process and fails it. Native helpers belong to their parent's Windows
job and are terminated when that parent exits.

## Review fixes accompanying the runner

- `UnitSpriteTests` now counts failed animation loads and exits with failure. It
  previously caught them, printed their errors, and still reported PASS.
- HUD labels retain a value/ComPtr copy of their text texture before requesting
  the shadow texture. That request can clear the cache, invalidating a reference.
- `NativeAreaTests` copies the target ID/life before replacing its world snapshot.
  The old pointer into the snapshot's entity vector became invalid on replacement.

These are client/test fixes; no DLL gameplay implementations were changed.

## Inventory item-use fidelity fix (2026-09-20)

`NativeGame.cpp` now supplies the player's absolute world position when dispatching
`Command::useItem`. Previously packet 0x20 contained X=0, Y=0. The original
`D2GAME_PACKETCALLBACK_Rcv0x20_UseGridItem_6FC85990` in
`source/D2Game/src/PLAYER/PlrMsg.cpp` rejects positions more than 50 subtiles away
on either axis through `sub_6FC83C60`. Position is resolved at dispatch, after any
earlier queued packets. Explicit raw packet coordinates are preserved.

`NativeShopTests` now buys an antidote from Akara, closes the store, uses it from
inventory, and checks that native D2Game consumed it. This failed before the fix
and passed afterward with the local x64 DLLs and a copy of `dfsd.d2s`.
Logs: `out/item-use-validation/before.log` and `after.log`. The full x64 client
build and eight regular CTest checks passed. Original and copied saves retained
SHA256 `4ABE864C415B1DA8B31EE6F350B4837E1F61400E308D4941F6B64E0AC330B9BC`.
This verifies one native inventory consumable, not every item-use effect.

Run the integration regression explicitly (the general runner does not include it):

```powershell
& .\out\modern\Release\NativeShopTests.exe out/build/X64Dlls/runtime/Release 'C:\games\Diablo II 1.10' out/item-use-validation/character.d2s
```

## Recorded run

`out/modern-validation/20260920-161012-ee7b4cce/report.json`: all checks passed,
including eight regular tests, 22 NPC/monster animations, native session replay,
Blood Moor movement/combat, actual hardware/WARP capture equality, unchanged saves,
and stable source/DLL fingerprints. No native helper remained after completion.
