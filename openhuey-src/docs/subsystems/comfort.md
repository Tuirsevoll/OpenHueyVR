# Comfort and accessibility (VR)

## Purpose

The choices a player in a headset needs and the game had none of: how she is turned, a vignette
that narrows the view while she moves, teleporting instead of walking, seated play, the hands'
controls, the height calibration and a scene watched from her eyes. Every one of them is a choice
between the game as it always was and the option; nothing changes until a player picks otherwise.

## API

The options are C (`src/platform/vr.h` `VrOptions`, `gVrOptions`), read as the game runs. Forth
reaches them through `vr-settings` (the struct's address) and the `vr.` field words
(`forth/bind_engine.c`, as `gfx` and its fields are): flags are 32-bit (`l@ l!`, 0 or 1), the
rest single floats (`sf@ sf!`).

| Field | Meaning | Default |
|---|---|---|
| `vr.turn-snap` | 0 smooth turning, 1 a snap a push of the right stick | 0 |
| `vr.snap-degrees` | a snap's angle, 15 .. 90 | 45 |
| `vr.comfort` | the comfort vignette while she is moving | 0 |
| `vr.comfort-strength` | how dark its corners go: the page writes 0.25 Low, 0.5 Med, 0.75 High | 0.5 |
| `vr.teleport` | 0 the stick walks her, 1 a stick flick teleports | 0 |
| `vr.teleport-mode` | 0 fixed (the flick goes the reach), 1 dynamic (it grows with the hold) | 0 |
| `vr.teleport-reach` | how far a flick goes, or the furthest a dynamic one grows to, metres (not a row) | 2.5 |
| `vr.seated` | seated play: her eye height is the seated one, never the floor's | 0 |
| `vr.seated-height` | the eye height seated play is sized to, metres | 1.6 |
| `vr.seated-height` | the eye height seated play is sized to, metres (not a row: 1.6 stands for a seat) | 1.6 |
| `vr.left-handed` | the sticks and face buttons swapped between the hands | 0 |
| `vr.calibrate-start` | the start-up height calibration (a first run, or asked for) | 1 |
| `vr.cutscene-first-person` | an in-game scene watched from her eyes | 0 |
| `vr.eye-height` | her eyes above the floor, metres (what a calibration measured) | 1.6 |
| `vr.body-scale` | a metre of head motion in the world's units (1: as measured) | 1 |
| `vr.calibrated` | 1 once a calibration has been kept | 0 |

| Word | Stack | Meaning |
|---|---|---|
| `vr-settings` | `-- addr` | the options' struct; the `vr.` fields add to it |
| `vr-snap` | `F: -- radians` | the turn stick's snap waiting, taken (0: none) |
| `vr-tracking?` | `-- flag` | the headset's pose is found (a calibration can measure it) |
| `hand-stops-clear` | `--` | the arms' stops dropped (a snap turn: the hands' places moved with her) |
| `vr-defaults` | `--` | every option as the game was (`vr-options`) |
| `save-vr-options`, `load-vr-options` | `--` | the player's written to / read from `<user>/vr-options.fs` |

The player's are a Forth file in the player's directory, read at start-up (`scripts/vr-options.fs`,
loaded by `subsystems.fs`), as `graphics.fs` and `controls.fs` are. Nothing is written in a test
(`testing?`); a change writes them at once (a page's row changes and `save-vr-options` runs), so a
setting kept is a setting the next run has.

## The VR options page

`scripts/subscreen/vr.fs` (`subscreen.vr`, page `pg-vr` of the sub screen) - the menu they are set
in, reachable without a keyboard:

- **The title**: its menu has a row `VR OPTIONS` (`scripts/opening.fs`, entry `e-vr`), after
  OPTIONS; the cross opens the sub screen on the VR page (mode 6, `open-vr-options`, the options'
  page alone - the triangle returns to the title).
- **In a game**: Select (Y) opens the sub screen; R1 - the left trigger - turns the pages past the
  options, and the VR page is the last of them (`page-order` in `subscreen/input.fs`; the page
  after the options, as it should be). The arrows are read from the VR stick in a headset
  (`read-arrows`), so every page of the sub screen - the lists, the map, the files, the options -
  and this one are worked with the controllers: the left stick moves, the cross answers, the
  triangle backs out.
- **Its rows**, eight of them: Turning (Smooth, Snap 45, Snap 90), Left-handed (Off, On),
  Comfort vignette (Off, Low, Med, High), Teleport movement (Off, Fixed, Dynamic),
  First-person scenes (Off, On), Calibrate height (the measurement, or Not set), Seated mode
  (Off, On) and View height offset (5 cm a step). A label on the left, its value on the right, the
  row chosen drawn in a lit bar: up / down choose, left / right step the row (a choice round its
  words, a number by its step), and the cross steps it too - on Calibrate height the cross is the
  measurement itself (`vr-calibrate`; without a headset the buzzer says so, and the row reads
  Not set). A change is heard (a cursor sound) and kept (`save-vr-options`).
- It draws with the sub screen's own art and font (`subscreen/art.fs`: `page-art`, `centred`,
  `text`), so it reads as the game's menu - no other effect, as the PS2 look asks. The strip's
  last tab says `VR` on this page: the art's own `Options` word is covered with the paper it sits
  on, `VR` drawn in its place - the paper sized to the word, inside the tab's own frame (a tab of
  its own would need art the game has not).

## Rules

- **A snap turn is the smooth turn, once.** The push is read every headset frame
  (`vr.c` `sync_stick`): one snap when the turn stick's pull passes `SNAP_AT` (0.70 of its edge),
  re-armed under `SNAP_REARM` (0.35), so a held stick does not spin her. Reading it per frame
  catches a flick that begins and ends between two ticks; the turn waits for the tick
  (`vr_snap_take`). The tick applies it with the same three writes a smooth turn makes
  (`fiona/controls.fs` `turn-view`: `vr-turn!`, her yaw, her heading), so her body, the neck, the
  eyes and the hands' anchor stay together. `hands-off?` (a script, a scene) still takes and drops
  a waiting snap, so it cannot fire later.
- **The arm stops go with a snap** (`hand-stops-clear`): the arms' stops are places in the room,
  and a snap moves the hands' places a long way in one tick. Dropping them lets the next posed hand
  take the controller as it is (as a room change does).
- **The picture blends a snap if it is under `TURN_SNAP`** (`main.c`, 1.05 radians = 60 degrees),
  as it blends any turn; 60 degrees and more counts as a cut and is instant.
- **The comfort vignette** (`main.c` `comfort_measure`, `comfort_tick`; `render/post.c` `post_comfort`):
  how much her drawn movement darkens the corners - the step she took and the turn of her body and
  the camera as the last tick left them (`sPose`), so a walk, a run, a ladder and a script step all
  count and standing still counts nothing. Measured once a tick (a frame between two ticks would
  read 0 on most of them and never grow - the bug the first version had), eased a frame at a time.
  Speed runs from `COMFORT_SPEED_START` (14 units a second, about a slow walk) to
  `COMFORT_SPEED_FULL` (45, about a run); the turn from 0.8 to 3 radians a second (a smooth turn
  at full stick is about 1.8; a 45 degree snap is a spike). It darkens over 0.12 s and lets go
  over 0.45 s, and it is nothing while the world is held, a scene has the camera or the free
  camera does. `post_comfort` adds it to the picture own `gfx.vignette` (what is left multiplies),
  per eye, on the scene only - the UI is drawn after the post pass, so menus stay bright. The
  page writes `vr.comfort-strength` 0.25, 0.5 or 0.75 (Low, Med, High); off is off.
- **The height calibration** (`vr_calibrate`; `scripts/opening.fs` `calibrate-*`; the page row):
  she stands straight, the headset height above the floor becomes her eye height, and the world
  is scaled so that Fiona 1.6 m is that (1.6 / height, clamped 0.4 .. 2.0). The result lives in
  the options (`vr.eye-height`, `vr.body-scale`, `vr.calibrated`), so it is kept and read at
  start-up; `vr-defaults` leaves it - it is not a row. A first run (a headset, `vr.calibrate-start`
  on, nothing kept yet) opens the title on the calibration screen: stand straight and press A, or
  B to go on without it. What it measured is said there for about three seconds, then the screen
  leaves for the title menu by itself (`calibrate-frame`, ninety ticks - the wait is counted down
  by the title's own tick, so nothing has to be pressed to get past it); the page's
  `Calibrate height` row does it again later, and shows what it measured. A runtime with no floor
  space measures nothing: the screen says so and whatever was kept stands.
- **Seated play** (`vr.seated`): the world is sized to `vr.seated-height` instead of a floor
  measurement (1.6 / height, clamped), so the virtual floor stays at the player's own; the first
  run's calibration is not asked for (`scripts/opening.fs` `calibrate-wanted?`) and a standing
  calibration kept is left as it is. `vr.seated-offset` lifts (+) or dips (-) the view within her
  body - the camera and the hands' anchor together (`vr_seat_lift`, `main.c` `fiona_view`), so the
  first-person rig stays one piece; up to 0.3 m either way, and the drawn body stays where it is.
- **Teleport** (`vr.teleport`; `scripts/fiona.fs` `tp-*`, `teleport-step`; `fiona/controls.fs`
  `vr-move`): while it is on the stick no longer walks her. Pushed past its dead zone it aims a
  way out, in the stick direction turned by where she looks, and walks that way a stride at a
  time (`teleport-stride`, 2 room units - about 16 cm) as far as the flick goes: `vr.teleport-reach`
  (2.5 m) for a fixed one (`vr.teleport-mode` 0), or from 1.2 m (`teleport-near`) growing to the
  reach over about 0.9 s of holding for a dynamic one (`mode` 1). The walk stops at the last place
  she may stand - a wall, the edge of her floor, a place with no straight way to it from her
  triangle (`v-walk`) - and the marker rests there, against what held her back; the very first
  stride stopped leaves it at her feet. So a flick into a wall puts her against that wall, and a
  long one crosses a room and no further. A glow marks the place - white where she may stand, red
  where the first stride is already stopped. Letting go takes her there - her feet on that floor,
  facing the way the flick went - and what her hands held is let go (`hands-drop`), so nothing is
  dragged along. A place she cannot stand is not taken: letting go there leaves her where she is.
- **Left-handed play** (`vr.left-handed`; `vr.c` `bind_stick`, `sync_stick`): the sticks and the
  face buttons swap hands - the moving stick to the right, the turning stick to the left, use to
  X, back to Y, pause to B, menu to A - while the hands themselves keep their own sides: the grips,
  the triggers (kick, step back) and the drawn arms are her hands, whichever way the player holds
  them. OpenXR will not re-bind a live session, so both hands' parts are bound once when the session
  opens (`move_r`, `turn_l`, `interact_l`, ...) and `sync_stick` reads the pair the option names; a
  change is in effect on the next frame. Both sticks clicked is still the recentre, on whichever
  hands they are.
- **A scene from her eyes** (`vr.cutscene-first-person`; `main.c` `first_person_scene`,
  `fiona_view`; `scripts/scenes.fs` `picture-movie?`, `scene-picture!`): an in-game cutscene the
  game's own models play is watched from her head instead of on the projected screen - no screen is
  put up and the story's camera is not used at all. Her own animation turns her, and the view with
  her (her yaw is the view's, every frame), the headset's look on top; the player takes no part,
  she is the scene's. The 4:3 bars are left out of that picture (`engine_letterbox_off`, the panel
  keeps the text). A scene whose picture is its movie (keyed, alpha, strip, opaque: an SFD drawn
  over the room) keeps its screen: the models are not what is being acted, so there would be
  nothing to watch - and an FMV has no place to stand in either. Most of the game's scenes (79 of
  150) are the former, which is what this option is for.
- **The defaults are the game as it was**: smooth turning, no vignette, the stick walking her,
  neither seated nor left-handed, her eyes as the first pose found them, a scene on its screen.
  `vr.calibrate-start` is 1: a first run is asked to calibrate once (with no calibration kept, the
  first pose's height is used as before).

## Status

Built: the options' store (`VrOptions`, the `vr.` fields, `scripts/vr-options.fs`, read at start-up,
written on change), **turning** (Smooth, Snap 45, Snap 90), **the comfort vignette** (Off, Low,
Med, High - 25, 50, 75 per cent of the corners), **the height calibration** (kept in the options, a
first run's screen on the title, the page's row for later), **seated play** (`vr.seated`, with
`vr.seated-offset` for the view's height in the body), **teleporting** (`vr.teleport`: Off, Fixed,
Dynamic - the stick flick, its aim walking out until something stops it, facing the way it went, a
glow for the landing), **left-handed play** (`vr.left-handed`: the sticks and face buttons over,
the hands as they are), **a scene from her eyes** (`vr.cutscene-first-person`: no screen, her head
and her turning) and **the VR options page** (`scripts/subscreen/vr.fs`, eight rows): the title's
`VR OPTIONS` row and, in a game, the sub screen's page after the options - both worked with the
controllers, the sub screen's arrows read from the VR stick in a headset.

The options the game had are the defaults, so a run with none of them picked plays as it always did.
Each row that is a choice steps round its words (Smooth, Snap 45, Snap 90; Off, Low, Med, High;
Off, Fixed, Dynamic; On, Off) and the row's value is capitalised as the other menus' words are.
`vr.teleport-reach`, `vr.seated-height` and the calibration are kept in the options but are not
rows - the reach is what a Dynamic flick grows to, and seated play sizes the world itself.
The F1 settings menu (`scripts/graphics.fs`) is still the keyboard's: arrow keys only.
