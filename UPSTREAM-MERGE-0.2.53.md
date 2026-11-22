# Merge: official OpenHuey 0.2.53 into the VR tree

Branch `upstream-update`, cut from `main` (`ec9d0dbc`, "Whisper Subtitles Fix").
Not pushed; Sean pushes it from GitHub Desktop when happy.

- **Merged commit:** `0455dcd76c99adc9e9065b7614f6147abc95fc95` - OpenHuey **0.2.53**,
  published 20 Nov 2026 as `openhuey-latest` (default branch `master`).
  `git fetch upstream master` and `git fetch upstream tag openhuey-latest` both resolved to that
  SHA, so it is also what the tag points at today; nothing newer was merged.
- **Merge commit:** `f492432f` "Merge OpenHuey 0.2.53 (0455dcd) into VR",
  parents `ec9d0dbc` (this repo's `main`) and `0455dcd` (official). `--no-ff`,
  `--allow-unrelated-histories`, no rebase, no squash.
- Version line now reads `project(hga VERSION 0.2.53 LANGUAGES C CXX)` - official's 0.2.53 with
  the VR tree's `CXX` (whisper.cpp).

## Why the conflict list is what it is

This repo's first commit (`115964ca`) has no parent: the tree was copied from official 0.2.32
(`11ed60cc`, "bedroom sheet remarks, looping sounds, AO banding, character shadows") and had VR
added already, so `git merge-base main <upstream>` finds nothing and a plain merge refuses.

The fetch brought 0.2.32's commit with official's history, so while resolving, `main` was
temporarily given a `refs/replace` graft onto `11ed60cc` - that let git do a real three-way merge
(base = 0.2.32) instead of the "both added, empty base" mess. That turned 209 raw conflicts into
**52 genuine ones**. The graft was deleted right after the merge commit was made
(`git replace -l` is empty, `git merge-base main 0455dcd` answers nothing again), so the recorded
history is exactly the two real parents.

## Conflicts and how each was resolved

`ours` = this VR tree, `theirs` = official 0.2.53, `mixed` = hand-merged both.

### C sources

| File | Resolution | Why |
|---|---|---|
| `openhuey-src/CMakeLists.txt` | mixed | official 0.2.53 version; `LANGUAGES C CXX`; every VR source (vr.c, voice.c, saudio.c, vrhands, vrbody, vrbones, gripposes, loot, itemprops, vrmirror, vrlevers, vrpath, vrspots, vrmarkers, triggerprobe, sound3d...), MSVC flags + ClankForth patch, Whisper/ggml/Steam Audio/OpenXR, the DLL copy rules and every VR test kept; official's new `title_languages` test kept |
| `src/main.c` | ours | VR's per-eye `draw_scene`/`draw_eye`/`prepare_shadows` and the pose blend replace official's single `draw`; official's `props`/`lights_for`/`actor_shown`/`forth_bye`/`world_rebuild_exits` work was already merged in around them (all six hunks were VR-only additions) |
| `src/forth/bind_engine.c` | ours + graft | VR prims and the VR field table stay; official's four new words (`language`, `language!`, `language-present?`, `language-code`, PR #128) were re-inserted into the merged word table (their `p_language*` prims had already merged) |
| `src/game/props.c`, `src/game/props.h` | ours | this tree's copy is official's props.c/.h plus the VR `props_load_own`/`props_free_own` (a held item's own model). Official has nothing extra |
| `src/game/particles.c` | ours | VR's `ps2_camera_culls` (no PS2 out-of-view dropping while an eye is the picture) |
| `src/game/sprites.c` | ours | VR's `vr_eye_active()` guard on `SPRITE_WHOLE` |
| `src/game/actor.c` | ours | VR's `actor_hide_head`/`on_head`/`bone_under` |
| `src/game/placed.c` | ours | VR's `Vec3 start` in `morph_fill` |
| `src/data/exetables.c` | **theirs** | official's table format gained `vaddr_eu` (the European `SLES_528.77`) plus `glyph-shapes`/`box-places`; this tree's two local additions (the sound-surround rows, the short-`exe/*.bin` guard) are already in official's version |

### Forth scripts

| File | Resolution | Why |
|---|---|---|
| `scripts/opening.fs` | mixed | VR's `VR OPTIONS` entry (`e-vr`, `on-calibrate`) **and** official's `LANGUAGE` entry (`e-language`, `on-language`): enum, entries, label, open and keys handling |
| `scripts/stalker/attack.fs` | **theirs** | official's `-1 blow-set !` in `strike`; the `blow-set @` test and the `blow-set` field arrived with the merge, and this tree had no `blow-set` at all - taking ours would have left the flag never set |
| `scripts/story/kit.fs` | ours | VR aim cone/`faced-offer?`/`note-fiona`/`act-fiona`/`pose-her!`/`loot-*` hooks; this tree already has a VR-aware `in-area?`, so official's duplicate definition was not inserted |
| `scripts/fiona.fs` | ours | VR `stand-past`, `hand-strike`, `try-ladder-grab`, `story-turned` and the headset `camera-cut` guard; official's 0.2.53 fiona changes (with-him-interrupted, exit-chosen, camera-relative turn, straining?/could) merged in cleanly around them |
| `scripts/items.fs` | ours | VR treats (`treat-id`/`treat-frames`/`treat-room`/`treat-at`, `hand-trigger`/`hand-mouth`/`hand-treat`/`treat-frame`), `vectors`, `game-loaded`; official's `dog-ready` work is in |
| `scripts/doors.fs` | ours | VR physics doors (`burst-way`/`door-burst`); official's `lock-flags`/`passage`/`let-go-all`/`shut-from-afar` and slam/barge through `door-set-opened` all merged in |
| `scripts/subscreen.fs` | ours | VR `vr-page-main`/`screen-face` on open and the arm save in `held-frame`; official's "item's tab gone" comment restored |
| `scripts/window.fs` | ours | VR item notices (`obtained?`, `notice-age`, `screen-face`) |
| `scripts/keys.fs` | ours | VR pad buttons and stick (`vr-down?`/`vr-pressed?`/`vr-stick`/nav repeat/`vr-gesture@`) |
| `scripts/hewie/mind.fs` | ours | VR petting (`pet-*`, the `vr-head` look, `vr-petting?`) |
| `scripts/hewie.fs` | ours | VR `his-treat` |
| `scripts/things.fs` | ours | VR treat logging (`treat-free-log` in gone / clear-things / thing-taken) |
| `scripts/fiona/controls.fs` | ours | VR `steer-yaw` |
| `scripts/fiona/moving.fs` | ours | VR `stick-len`, `vr-stride`, `vr-pace`, `kept-step`, `sneak-*` |
| `scripts/fiona/commands.fs` | ours | VR `go-spot` triangle fallback |
| `scripts/fiona/fear.fs` | ours | VR `panic-stumbles?` option |
| `scripts/subscreen/input.fs` | ours | VR `vr-stick` and the `pg-vr` page in `page-order` |
| `scripts/stalker.fs` | ours | VR `swept-by-door` |
| `scripts/story/common.fs` | ours | VR `vr? 0=` in `may-crouch?` |
| `scripts/scenes.fs` | ours | whitespace only |
| `scripts/story/places/castle-1f-2.fs` | ours | VR lever (`vr-lever-here?`) |
| `scripts/story/places/castle-2f-12.fs`, `old-mansion-1f-11`, `-1f-22`, `-2f-8`, `-2f-13`, `-b1-2`, `-b1-4`, `-b1-5` | ours | VR hiding-spot guards (`vr? 0= if ... hiding-spot offer`), `the-curtain-offer`, VR touch/aim notes |

### Tests, mods, docs

| File | Resolution | Why |
|---|---|---|
| `tests/places/test_castle_1f_7.fs` | mixed | this tree's headset let-go test kept, then official 0.2.53's rewritten "Hewie stays on the plate, the gate holds" test (the old one is replaced) |
| `tests/items/test_subscreen.fs` | ours | VR's four menu actions (Use, Equip, Discard, Cancel) |
| `tests/story/test_kit_rules.fs` | ours | VR `move-doing?`/`fiona-act?` tests |
| `examples/mods/item-props/mod.fs`, `.../scripts/item-props.fs` | ours | VR / Mod-API 1.5 comment blocks (grips, custom models); the code is identical to official's |
| `README.md` (root) | **theirs** | the VR tree's was a 2-byte placeholder; official's full README came in |
| `docs/library-and-mods.md` | **theirs** | official's 29 tables, `vaddr_eu`, glyph shapes / box places |
| `docs/modding.md` | ours | VR sections (`vr_bones.txt`, held items, `grip_poses.txt`) |
| `docs/subsystems/effects.md`, `fiona.md`, `hewie.md`, `items.md`, `story.md` | ours | VR paragraphs (headset culling, voice orders, petting/the paw, VR menu mode, aim-cone offers) |

Nothing VR-only was dropped: every file on `main` is still in the tree (3785 -> 3792 files - the
seven new official ones: `scripts/languages.fs` and the `tests/title/languages-mod` sample).

Hotspots the brief called out that merged **without** a conflict, for the record:
`src/render/post.c` (official unchanged since 0.2.32, this tree's VR fog/post kept),
`src/game/camdirector.c`, `scripts/rooms.fs` and `src/platform/update.c` are now byte-for-byte
official 0.2.53 (VR does not touch them); `src/game/cutscene.c` took official's `records()` bounds
fix and keeps VR's cue-parameter snapshot and negative-frame handling; `scripts/hewie/fetch.fs`
and `scripts/fiona/letgo.fs` took official's lines and keep the VR treat / unequip-on-use words.

## Submodules

| Submodule | State |
|---|---|
| `openhuey-src/external/clankforth` | `5925dbf4` - exactly what official 0.2.53 pins, and what this repo already had (no change needed). `cmake/clankforth-msvc.patch` is applied in the submodule's working tree (`src/forth.c`, `forth.h`, `forth_internal.h` modified; `.gitmodules` has `ignore = dirty`), so CMake's idempotent `git apply --check` skips re-applying on both configures. `git apply --check --reverse` applies cleanly, i.e. the patch is complete, not half-applied |
| `vcpkg` | `84297877` unchanged |

## Build

Both on this machine's toolchain: **Visual Studio 18 2026**, `-A x64`, vcpkg toolchain
`vcpkg/scripts/buildsystems/vcpkg.cmake` (the generator this machine's `CMakeCache.txt` names -
not "Visual Studio 17 2022", which is not installed here).

- **Debug** - `build/openhuey-src` (the everyday tree, `HGA_RELEASE=OFF`):
  `cmake --build build/openhuey-src --config Debug` -> **0 errors**, `openhuey.exe` plus all 20
  check/tool executables (incl. `loot-frame-check`, `itemprops-check`, `gripposes-check`,
  `vr-bones-check`, `vrbody-check`, `vrmarkers-check`, `vrspots-check`, `vrmirror-check`).
- **Release** - new `build/openhuey-src-release`, configured from scratch with `-DHGA_RELEASE=ON`:
  configure OK (ClankForth MSVC patch skipped as already applied, Whisper 1.9.5-dev, ggml CPU
  AVX512, Steam Audio found) and the build -> **0 errors**. The output folder carries
  `openhuey.exe`, `openxr_loader.dll`, `phonon.dll`, `whisper.dll`, `ggml*.dll` and `SDL3.dll`.

One extra commit was needed for that Release link, and it is **not** a merge regression: with
`WIN32_EXECUTABLE ON` official gets its `WinMain` from the MinGW SDL3 package's `SDL3main`
(`tools/hga_release.sh` builds Windows with MinGW); vcpkg's MSVC SDL3 ships only `SDL3.lib`, so
`HGA_RELEASE=ON` here failed at link with `LNK2019 WinMain` / `LNK1120`. This tree has never had a
`HGA_RELEASE=ON` configure before. Commit `0a35f392` adds, for MSVC only,
`/ENTRY:mainCRTStartup`: the no-console subsystem is kept and `main` is called as before; Debug and
the MinGW release path are untouched.

## Tests (`ctest --test-dir build/openhuey-src -C Debug`)

317 tests (was 316; `title_languages` is new). 31 pass, 286 fail - the same shape as before the
merge, because **this machine has no Haunting Ground data**, so every test that loads the game
fails with `openhuey: no game data`. A baseline run on `main` before touching the tree was kept in
`build/baseline-ctest2.txt`.

- **No test that passed before fails now** - the passing set is identical (`test_actors`,
  `test_core` and the other clankforth tests, `prop_check`, `spu_check`, `hands_sweep_check` and
  the `vr*_check`s, `mods_menu_folder`, `mods_menu_clean`, `mods_zip_sample`, ...).
- Known-old failures still failing **identically** (output compared line for line):
  `loot_frame_check`, `itemprops_check`, `gripposes_check` - these three are data-free programs and
  their output is byte-identical to `main`, same expectations failing (e.g. loot item 148's model
  on the wrong side of its spot; `vrbones: O_FIN/FIN_000 has no vr_bones.txt`).
- `story_test_title`, `mods_item_props`, `mods_menu`, `title_title`: the same three-line
  "no game data" as before.
- **New test `title_languages`** (official's European-disc sample mod) fails for the same reason -
  it needs the game's data/library. Nothing to fix here.

## In-game / VR smoke test

**Not run - there is no Haunting Ground data on this machine** (no `library/`, no
`Haunting Ground (USA)` folder), so this list is Sean's:

- VR boot, headset tracking, recenter; calibrate / standing height
- IK hands and body when looking down
- pick up a physical item; grips, including magnesia
- Fiona's outfits (the `fiona-vr-rig` mod on)
- grab, swing and slam a normal door; hiding spots
- mirror curtains in ST_06A / ST_04B / ST_04E
- remedies and Hewie treats: equip, use, eaten
- ladders

The Debug `openhuey.exe` does start and gets as far as the disc check (`--hidden --frames 3` ->
`openhuey: no game data. Import your disc first`), which is as far as it can go without the disc.
The void-fill check does **not** apply: `vr-sky-void-fill` (`ed09120`) is *not* merged into
`main`, and was deliberately not pulled in here.

## Follow-ups (not part of this merge)

1. **The title's UPDATE button still downloads official, non-VR OpenHuey.** `RELEASE_URL` in
   `src/platform/update.c` was left exactly as official has it (`.../ArnautDaniel/OpenHuey/
   releases/download/openhuey-latest/`), as instructed. A later change should hide or repoint it
   for VR builds - a VR player who presses it gets a non-VR `openhuey.exe` over their build.
2. The root `README.md` is now official's (it links official's releases); the fork's own README is
   `openhuey-src/README.md`, which merged cleanly with the VR sections kept.
3. `build/tmp/*.ps1`, `build/baseline-ctest*.txt`, `build/merged-ctest*.txt`,
   `build/debug-build*.txt` and `build/release-*` are scratch/evidence files in the (git-ignored)
   `build/` directory - safe to delete.

## Remote hygiene

- `upstream` was added **fetch-only**: `https://github.com/ArnautDaniel/OpenHuey.git` for fetch, and
  its push URL set to `https://invalid.invalid/DO-NOT-PUSH.git`, so no `git push upstream` can ever
  write to `ArnautDaniel/OpenHuey`.
- Nothing was pushed: `git push` was never run on this branch, and `main` is untouched at
  `ec9d0dbc`.





