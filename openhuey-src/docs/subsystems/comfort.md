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
- **Its rows**, seven of them: Turning (Smooth, Snap 45, Snap 90), Left-handed (Off, On),
  Comfort vignette (Off, Low, Med, High), Teleport movement (Off, Fixed, Dynamic),
  Calibrate height (the measurement, or Not set), Seated mode (Off, On) and View height offset
  (5 cm a step). A label on the left, its value on the right, the row chosen drawn in a lit bar:
  up / down choose, left / right step the row (a choice round its words, a number by its step),
  and the cross steps it too - on Calibrate height the cross is the measurement itself
  (`vr-calibrate`; without a headset the buzzer says so, and the row reads Not set). A change is
  heard (a cursor sound) and kept (`save-vr-options`).
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
- **The comfort vignette** (`main.c` `comfort_measure`, `comfort_tick`; `render.c` `render_comfort`,
  its own ramp texture and shader): how much her drawn movement darkens the corners - the step she
  took and the turn of her body and the camera as the last tick left them (`sPose`), so a walk, a
  run, a ladder and a script step all count and standing still counts nothing. Measured once a
  tick (a frame between two ticks would read 0 on most of them and never grow), eased a frame at
  a time. A walk is about 13 room units a second: `COMFORT_SPEED_START` 4 (a crawl, under it) to
  `COMFORT_SPEED_FULL` 15 (a walk is nearly all of it, a run past it), the turn 0.4 to 1.6 radians
  a second (a smooth turn at full stick is about 1.8; a 45 degree snap is a spike). It darkens
  over 0.12 s and lets go over 0.45 s, and it is nothing while the world is held, a scene has the
  camera or the free camera does. The share is `sComfort x vr.comfort-strength`. The ring is drawn
  by the renderer over the finished picture - `render_post`, window or eye the same, before the
  menus, so the UI stays bright - from a radial ramp (clear to 0.02 of the half-width, at its full
  by 0.55, the wide axis stretched 1.25: the outside of a headset's visible picture is well
  inside the texture's corner, so a ramp that only reached full at the corner was a smudge at the
  very edge of vision and nothing else) times the share; the picture's own vignette setting and
  shader are untouched. The page's Low, Med and High are 0.25, 0.5 and 0.75 of it - the corners
  about half at High on a walk, the middle clear - and off is off.
  straight, the headset height above the floor becomes her eye height, the world is sized to her
  (see below), and the play space's origin is taken again where she stands, so the new size
  applies from there and the view does not shift by her offset from the old one. The result lives
  in the options (`vr.eye-height`, `vr.body-scale`, `vr.calibrated`), so it is kept and read at
  start-up; `vr-defaults` leaves it - it is not a row. A first run (a headset, `vr.calibrate-start`
  on, nothing kept yet) opens the title on its own screen: stand straight and press A, or B to go
  on without it; what it measured is said for about three seconds, then the screen leaves for the
  menu by itself (`calibrate-frame`, ninety ticks, counted by the title's own tick); the page's
  `Calibrate height` row does it again later, and shows what it measured. A runtime with no floor
  space measures nothing: the screen says so and whatever was kept stands. The height alone is
  enough - an arm span (a T-pose) says the same thing about a body to within a few per cent, and
  this game's body is Fiona's, not the player's; what the number is needed for here is the world's
  size, not a skeleton's.
- **Seated play** (`vr.seated`): the world is sized to `vr.seated-height` instead of a floor
  measurement (1.6 / height, clamped), so the virtual floor stays at the player's own; the first
  run's calibration is not asked for (`scripts/opening.fs` `calibrate-wanted?`) and a standing
  calibration kept is left as it is. `vr.seated-offset` lifts (+) or dips (-) the view within her
  body - the camera and the hands' anchor together (`vr_seat_lift`, `main.c` `fiona_view`), so the
  first-person rig stays one piece; up to 0.3 m either way, and the drawn body stays where it is.
- **The world's size, and why the view must not warp** (`vr.c` `body_scale`, `vr_body_view`,
  `vr_eye_body`, `proj_fov`): a measured height reads as a *scale*, `s = 1.6 / height` - what a
  metre of head motion is worth in the world, and what the eyes' separation is worth. Those two
  are the whole of it, and they agree: the same motion and the same stereo both say the world is
  `1/s` times its size. The headset's field of view is never touched by it. The first version also
  multiplied the focal length by `s` - the opposite way from the motion - so an object's apparent
  size and its parallax disagreed, and the picture warped as the head moved (nausea, and only
  when the scale was not 1, which is why seated play with its default 1.6 m felt right and a
  calibration did not). A scale change (a calibration, or seated play on or off) takes the play
  space's origin again where the player stands, so the camera does not jump by their offset from
  the old one.
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
  change is in effect on the next frame. Nothing else is on the sticks: the both-clicks recentre
  is gone, so the calibration is the menu's one way and a scene is not interrupted by it.
- **A cutscene keeps its screen** (`main.c` `cutscene_on_screen`, `fiona_view`): the scene is
  drawn once, at 4:3, and put on a screen in front of the player, her model hidden; her eyes are
  not used and the story's camera stays the picture's. (An option to watch the game's scenes from
  her head was tried and taken out: `vr.cutscene-first-person` is still a field, kept so an older
  options file loads, but nothing reads it and no row writes it.)
- **A menu opens in front of the look, then stays in the room** (`vr.c` `place_screen`, `vr_screen_face`;
  `main.c` `draw_frame`; `scripts/subscreen.fs` `opened`): the panel a menu is drawn on is placed
  once, 1.8 m ahead of the head and level, when the menu opens (`vr_screen_face`: the sub screen,
  the pause, a cutscene, the title's screens; and again after the play space is taken again, since
  a panel placed in the old space would be somewhere else in the new). The look's forward and
  right in play space are `(-sin yaw, 0, -cos yaw)` and `(cos yaw, 0, -sin yaw)`: their x was
  taken positive at first, which put the panel at the opposite yaw - a menu opened as far to one
  side as the player had turned to the other. After the placement it is part of the room: turning
  the head looks around it, it does not come with you.
- **The sub screen's panel is a page, and it holds the room out** (`scripts/subscreen.fs`
  `page-ground`, `panel-opaque`; `render.c` `render_panel_opaque`): the menu is composed the way the
  PS2 composed it, its pieces and words blended by the GS's ALPHA register - arithmetic on the
  screen, whose alpha nobody read. On a headset's panel the alpha is what puts the picture in front
  of the room, so that blending (and every translucent piece: the highlight's band too) took the
  panel's alpha away with it, and the room showed through the words, the game's font, the button
  symbols and the highlighted row (the tab headings, from the art's own picture, were opaque and
  stayed right). Two things: the page's paper on layer 9 under the art, so the ink blends with
  paper rather than with the room; and `panel-opaque`, after everything has drawn, which puts the
  panel's own alpha back over all of it - a full-screen quad of the font's solid texel, alpha
  only, the colour untouched, so the picture stays exactly as drawn (a quad that sampled the
  atlas's corner, as the first try did, wrote no alpha at all and changed nothing). The pause menu
  asks for it too, at its dim's own alpha, so the world is still seen through the dim and not
  through its letters.
- **The defaults are the game as it was**: smooth turning, no vignette, the stick walking her,
  neither seated nor left-handed, her eyes as the first pose found them, a scene on its screen.
  `vr.calibrate-start` is 1: a first run is asked to calibrate once (with no calibration kept, the
  first pose's height is used as before).

## Status

Built: the options' store (`VrOptions`, the `vr.` fields, `scripts/vr-options.fs`, read at start-up,
written on change), **turning** (Smooth, Snap 45, Snap 90), **the comfort vignette** (Off, Low,
Med, High - 25, 50, 75 per cent of the corners, its own ring over the picture), **the height
calibration** (kept in the options, a first run's screen on the title, the page's row for later,
one action: the height, the world's size and the view's origin together), **seated play**
(`vr.seated`, with `vr.seated-offset` for the view's height in the body), **teleporting**
(`vr.teleport`: Off, Fixed, Dynamic - the stick flick, its aim walking out until something stops
it, facing the way it went, a glow for the landing), **left-handed play** (`vr.left-handed`: the
sticks and face buttons over, the hands as they are), and **the VR options page**
(`scripts/subscreen/vr.fs`, seven rows): the title's `VR OPTIONS` row and, in a game, the sub
screen's page after the options - both worked with the controllers, the sub screen's arrows read
from the VR stick in a headset.

The options the game had are the defaults, so a run with none of them picked plays as it always did.
Each row that is a choice steps round its words (Smooth, Snap 45, Snap 90; Off, Low, Med, High;
Off, Fixed, Dynamic; On, Off) and the row's value is capitalised as the other menus' words are.
`vr.teleport-reach`, `vr.seated-height` and the calibration are kept in the options but are not
rows - the reach is what a Dynamic flick grows to, and seated play sizes the world itself.
The F1 settings menu (`scripts/graphics.fs`) is still the keyboard's: arrow keys only.
A headset session prints one line to the console as it opens - `vr: comfort 1 (0.75), scenes from
her eyes 0, world scale 0.894 (eye 1.79 m), seated 0` - so a setting that never reached the
engine can be told from one that did nothing (vr.c, the first frame).
