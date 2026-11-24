# openhuey-src: OpenHuey, Haunting Ground as actors

The third codebase, and the one meant to ship: the program `openhuey`. (This folder was
`new-src-actors/` and the program `hga` until 0.1.0; the mods' `requires: hga` and the
`HGA_HOME` variable are still understood.)

- `src/` (top level) is the decompilation: the PS2 executable as faithful C. It is the
  **specification**: when we need to know what the game does, we read it there.
- `new-src/` was the first rewrite (`hg2`): the same C engine, with gameplay written in Forth.
  Its C is solid and was copied here. Its Forth scripts transliterated the decompiled code (raw
  field offsets, vtable slots, the original's control flow), which made them fragile. It is
  **retired and archived**: no longer in the tree, kept at the git tag `new-src-final`
  (`git show new-src-final:new-src/...`). For what the game does, run `hg` (the decompilation's
  native build) - `HG_EVLOG=1` traces the event scripts.
- `openhuey-src/` (here) keeps that engine and redesigns the gameplay as **actors that pass
  messages**. Each subsystem is an actor, or a family of them, with a small vocabulary of
  messages as its API. The game should feel the same; the code shouldn't look decompiled.

Start with `docs/design.md`, then `docs/actors.md` (the kernel) and `docs/subsystems.md` (the
map of the game). Each subsystem gets its own page in `docs/subsystems/` before it is built.
The Forth is ClankForth (`external/clankforth`, a submodule: clone with `--recursive`, or `git submodule update --init`; the language `external/clankforth/docs/forth.md`, OpenHuey's side `docs/forth.md`); the game library (the disc imported, its assets in common formats)
and mods are `docs/library-and-mods.md`; **making a mod: `docs/modding.md`**.

## Building and running

```
cmake -S openhuey-src -B build/openhuey-src -G Ninja
cmake --build build/openhuey-src
build/openhuey-src/openhuey                       # the game: from its library (the first run imports the disc)
build/openhuey-src/openhuey --import disc.iso     # import a disc into the library (library/ beside OpenHuey)
build/openhuey-src/openhuey --update            # a release: updated to the newest build (the title's UPDATE too)
build/openhuey-src/openhuey --mod path/to/mod     # with a mod on (mods/ beside OpenHuey holds the installed ones)
build/openhuey-src/openhuey --hidden --frames 300 --eval "..."   # headless, for tests
build/openhuey-src/openhuey --flat               # the window's picture, no headset (or $OPENHUEY_FLAT=1)
build/openhuey-src/openhuey --gl33                # drawn OpenGL 3.3's way even where 4.6 is there
ctest --test-dir build/openhuey-src    # the quick tests (OPENHUEY_FULL_TESTS=1: all of them)
build/openhuey-src/openhuey --test openhuey-src/tests/acoustics/test_hearing.fs   # one subsystem's
```

In the game: `` ` `` opens the Forth console (`scripts/debug.fs` has its helpers; Ctrl+V pastes,
each whole line run, Ctrl+C copies the line typed or, with none, what the last one printed,
Ctrl+Shift+C the whole log, Ctrl+X cuts), F1 the settings
(the picture - three presets: *enhanced*, the default, the game with some modern polish, *as on
the PS2*, the original's picture exactly, its characters lit by its own formula, and *low-power
computer*, the original's look with only the costly extras off - fullscreen, the controls - saved
in `user/`), F11 or Alt+Return fullscreen, F2 the debug
overlay (Fiona's meters - panic, breath, fear - the actors alive and their ids down the right, OpenHuey's
version; F4 hides or shows the actors' list), F3 the free camera,
F12 a bug report: the moment kept in `user/bugs/<date>_<time>/` (report.txt - where she is, every
actor's state, the last messages, the console's lines - screenshot.png and save.fs), the console
opened with `bug-note ` typed for what happened (into note.txt). All of it is zipped beside the
folder, `user/bugs/<date>_<time>.zip` (again with each note): drag that onto a GitHub issue. At
the console `bug <what happened>` does both at once; `s" <folder>/save.fs" bug-load` plays the moment again. The
version is CMake's `project(OpenHuey VERSION ...)`; the release builds carry it in their names.

The picture is OpenGL 4.6, else 3.3 (older and smaller GPUs: Intel's from Sandy Bridge, NVIDIA's
GeForce 8 on, AMD's Radeon HD 2000 on): the same renderer either way, `src/platform/gl.c` standing
in for 4.5's functions with 3.3's and rewriting the shaders for GLSL 3.30 (`src/platform/gl.h`).
On 3.3 a fresh start takes the low-power look. `--gl33` (or `OPENHUEY_GL=3.3`) draws 3.3's way
on any GPU - for checking it; a developer's build asks the driver to check every call (its debug
output: slower), a release only with `OPENHUEY_GL_DEBUG=1`.

Tests: `tests/test_*.fs` are the language and the kernel (the `forth` tool, no game data);
`tests/<subsystem>/test_*.fs` run inside the game headless (`openhuey --test`), with the game's data.

## 3-D audio (Steam Audio)

Placed sounds (footsteps, voices, doors, impacts) are rendered binaurally through Valve's Steam
Audio HRTF, from their world point to the headset's own pose, so a stalker can be heard behind,
above or through a wall (`docs/subsystems/audio3d.md`). Music, the menus and a cutscene's movie
sound stay stereo as they were.

The SDK is **optional**: without it the game builds exactly as before and plays its own left /
right placement.

1. Download `steamaudio_<version>.zip` from
   <https://github.com/ValveSoftware/steam-audio/releases> (4.8.1 is what this was built with).
2. From it copy `include/phonon.h`, `include/phonon_version.h` and
   `lib/windows-x64/phonon.lib`, `lib/windows-x64/phonon.dll` into
   `openhuey-src/external/steamaudio/`:
   ```
   external/steamaudio/include/phonon.h
   external/steamaudio/include/phonon_version.h
   external/steamaudio/lib/windows-x64/phonon.lib
   external/steamaudio/lib/windows-x64/phonon.dll
   ```
   (that folder is git-ignored). Or point CMake at it:
   `cmake -S openhuey-src -B build -DHGA_STEAMAUDIO_DIR=D:/steamaudio`.
3. Configure and build: CMake prints `Steam Audio: ... (binaural 3-D audio on)` and copies
   `phonon.dll` beside `openhuey.exe` (and `spu-check.exe`). `phonon.dll` must ship with the game.

The **VR options page** (the title's `VR OPTIONS`, or the sub screen's last page) has
**3D Audio** (On by default: the binaural HRTF; the occlusion's muffling is switched off in
`sound3d.c` for now), kept in `user/vr-options.fs`; off, the
game plays its original mono / stereo / Pro Logic mix (no environment variable is needed: the
option is the switch). `HGA_AUDIO3D_STATS=1` prints the HRTF's cost to stderr every five seconds
of audio (16 voices ≈ 11 ms of CPU a second, about 1% of a core), and
`HGA_AUDIO3D_SELFTEST=out.wav` writes a six-second 440 Hz tone through one spatial voice, in the
game's own block pattern, and prints the largest step between samples at a block's edge - equal to
one inside a block means no discontinuity (`docs/subsystems/audio3d.md`).

The VR side's own console lines - the headset and its session, the body and the hands, the grips,
the physical items, the hiding spots, the levers, the markers and the probes under them - are off by
default: a player needs none of them, and a release has no console to show them in. They all say
nothing when `OPENHUEY_VR_LOG` is unset; `OPENHUEY_VR_LOG=1` brings every one of them back (the
gate is `src/platform/vrlog.h`). The lines that say something failed - no headset, no runtime,
`openxr_loader.dll` missing, no microphone, a model that will not load - print either way.

