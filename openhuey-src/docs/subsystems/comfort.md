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
| `vr.left-handed` | the sticks, face buttons and triggers swapped between the hands (the pause button stays on the left) | 0 |
| `vr.whisper-open` | the microphone: 0 B holds it (PTT), 1 open mic | 0 |
| `vr.whisper-debug` | the speech subtitles: 0 off, 1 the command alone, 2 the command and the words heard | 0 |
| `vr.calibrate-start` | the start-up height calibration (a first run, or asked for) | 1 |
| `vr.cutscene-first-person` | an in-game scene watched from her eyes | 0 |
| `vr.eye-height` | her eyes above the floor, metres (what a calibration measured) | 1.6 |
| `vr.body-scale` | a metre of head motion in the world's units (1: as measured) | 1 |
| `vr.calibrated` | 1 once a calibration has been kept | 0 |
| `vr.crouch-deadzone` | the headset drop or crouch trigger (a share of the standing height) below which no crouch animation plays | 0.13 |
| `vr.crouch-on` | ... and the share at which the hide flag comes on | 0.25 |
| `vr.crouch-off` | ... and the share it goes off at, rising (the hysteresis) | 0.18 |
| `vr.sneak-speed` | the crouch walk's share of a walk, and its animation rate (45%: 55% slower) | 0.45 |
| `vr.panic-tint` | the panic's grey / darkening tint at each heartbeat (the VR page's Reduced preset) | 1 |
| `vr.panic-smear` | the heartbeat's frame blend, held and blended in again (photosensitive: off) | 0 |
| `vr.panic-zoom` | the first frames drawn 16 pixels bigger over themselves (photosensitive: off) | 0 |
| `vr.panic-negative` | the negative as her panic starts (photosensitive: off) | 0 |
| `vr.panic-shake` | the camera shaken by her panic | 0 |
| `vr.panic-stumbles` | she stumbles, and one time in five falls, running blindly into things | 1 |
| `vr.panic-haptics` | the heartbeat on the controllers: 0 off, 1 half, 2 full | 2 |

| Word | Stack | Meaning |
|---|---|---|
| `vr-settings` | `-- addr` | the options' struct; the `vr.` fields add to it |
| `vr-snap` | `F: -- radians` | the turn stick's snap waiting, taken (0: none) |
| `vr-gesture` | `-- g` | the turn stick's Hewie gesture this tick: 0 up (go / attack), 1 down (come), 2 its click (stay), -1 none |
| `vr-menu?` | `-- flag` | a menu is up: the triggers page the tabs, they do not crouch / kick |
| `vr-tracking?` | `-- flag` | the headset's pose is found (a calibration can measure it) |
| `hand-stops-clear` | `--` | the arms' stops dropped (a snap turn: the hands' places moved with her) |
| `vr-defaults` | `--` | every option as the game was (`vr-options`) |
| `save-vr-options`, `load-vr-options` | `--` | the player's written to / read from `<user>/vr-options.fs` |
| `panic-preset`, `panic-preset$` | `-- p` / `-- addr len` | the panic set they add up to: 0 Full, 1 Reduced, 2 Minimal, -1 Custom (its name) |
| `panic-preset!` | `p --` | that preset's seven settings, written over the individual ones |
| `panic-preset-step` | `dir p -- p'` | the preset a step of the row goes to (a Custom set: Full, or Minimal, either way) |
| `panic-tint?`, `panic-smear?`, `panic-zoom?`, `panic-negative?`, `panic-shake?`, `panic-stumbles?` | `-- flag` | one of the panic settings |
| `panic-haptics`, `panic-haptics-scale` | `-- level` / `F: -- k` | the beat's choice (0..2) and its strength (0, 0.5, 1) |

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
- **In a game**: Select (Y) opens the sub screen; the left trigger (L1) turns the pages back
  and the right trigger (or X, R1) turns them on, and the VR page is the last of them
  (`page-order` in `subscreen/input.fs`; the page after the options, as it should be). The arrows
  are read from the VR stick in a headset (`read-arrows`), so every page of the sub screen - the
  lists, the map, the files, the options - and this one are worked with the controllers: the
  movement stick moves, A answers, a grip backs out.
- **Its rows**, ten of them: Turning (Smooth, Snap 45, Snap 90), Left-handed (Off, On),
  Comfort vignette (Off, Low, Med, High), Teleport movement (Off, Fixed, Dynamic),
  Calibrate height (the measurement, or Not set), Seated mode (Off, On), Whisper (PTT, Open Mic),
  Whisper Subtitles (Off, On, Debug) and 3D Audio (Off, On) - the last the binaural HRTF, its
  occlusion coming with it (docs/subsystems/audio3d.md). A label on the
  left, its value on the right, the row chosen drawn in a lit bar:
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

## The controllers

The Quest 3 (Touch Plus) layout, in the right hand's play (`vr.c` `sync_stick`). The hands
themselves do not move when left-handed play is on: the sticks, face buttons and triggers swap,
and each grip still grabs with its own hand.

| Left hand | Action | Right hand | Action |
|---|---|---|---|
| Stick | move, click runs | Stick | turn; a quick straight flick up is Hewie's "go / attack", down is "come"; its click is stay / stop |
| Y | status screen, or the movie pause in a cutscene | B | hold for Whisper (PTT) |
| X | backstep | A | interact, and accept in a menu |
| Option (the menu button) | pause | Trigger | kick / tackle; a page right in a menu |
| Trigger | hold to crouch (the VR crouch rules, slow sneak walk, and the view lowers with it); a page left in a menu | Grip | grab |
| Grip | grab, and back / close a dialogue or a menu | | |

The two grips are the triangle everywhere as well as the grab: a grip backs out of a menu or
closes a dialogue, and (as the triangle always has) lets go of a held throw.

In a menu the triggers are the tabs by their **physical** side - the left pages left, the right
pages right - even in left-handed play; in the game they are crouch and kick, and those swap with
the hands as the rest does. The crouch trigger lowers the view with her body and raises it as she
comes up: the player does not have to duck, and the physical and trigger crouches do not add.

The turn stick's gesture is strict, so turning or resting the stick cannot call Hewie: it must
leave the centre as a mostly vertical push (`FLICK_STRAIGHT` 0.35 across, and up two and a half
times its sideways), reach `FLICK_PULL` (0.85) within 180 ms, and centre again before another. Its
click is its own button and counts wherever the stick is. The gesture is read for the whole tick
(`vr_gesture_now`) and dropped when the tick ends (`vr_pad_clear`), exactly as a key press is.

**Left-handed play** swaps the functions above between the hands - movement to the right stick,
turning and Hewie's gestures to the left, interact to X, status to A, backstep to B, Whisper to
Y, crouch to the right trigger and kick to the left - except the pause button, which the right
Touch controller has no bindable button for (its system button belongs to the runtime). The
pause stays on the left controller's menu button. OpenXR will not re-bind a live session, so both
hands' actions are bound once when the session opens (`move_r`, `turn_l`, `interact_l`, ...) and
`sync_stick` reads the pair the option names; a change is in effect on the next frame.

Other headsets: the Touch layouts (Quest 2 / 3 / Pro's Touch and Touch Plus, Pico 4 / Neo 3,
Vive Focus 3) get the same mapping. Index controllers get the sticks, grips, both triggers and
the A / B face buttons, with the left trackpad's click as pause. WMR, HP Reverb and Vive Cosmos
get the sticks, grips, triggers and the left menu button; the Vive wands get the same on their
trackpads. A controller without a face button cannot give the face-button actions, so the
keyboard remains beside them.

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
- **The height calibration** (`vr.c` `vr_calibrate`, `scripts/opening.fs`, `scripts/vr-options.fs`,
  `subscreen/vr.fs`): stand straight, the headset height above the floor becomes her eye height,
  the world is sized to her (see below), and the play space's origin is taken again where she
  stands, so the new size applies from there and the view does not shift by her offset from the
  old one. The result lives in the options (`vr.eye-height`, `vr.body-scale`, `vr.calibrated`), so
  it is kept and read at start-up; `vr-defaults` leaves it - it is not a row. A first run (a headset,
  `vr.calibrate-start` on, nothing kept yet) opens the title on its own screen: stand straight and
  press A, or B to go on without it; what it measured is said for about three seconds, then the
  screen leaves for the menu by itself (`calibrate-frame`, ninety ticks, counted by the title's own
  tick); the page's `Calibrate height` row does it again later, and shows what it measured. A
  save's load does it at once, before the world comes back (`vr-calibrate-now`, `saves.fs`): no
  menu, no button, so a game loaded in a headset is sized to the player standing there. A runtime
  with no floor space measures nothing: the screen says so and whatever was kept stands. The
  height alone is enough - an arm span (a T-pose) says the same thing about a body to within a few
  per cent, and this game's body is Fiona's, not the player's; what the number is needed for here
  is the world's size, not a skeleton's.
- **Seated play** (`vr.seated`): the world is sized to `vr.seated-height` instead of a floor
  measurement (1.6 / height, clamped), so the virtual floor stays at the player's own; the first
  run's calibration is not asked for (`scripts/opening.fs` `calibrate-wanted?`) and a standing
  calibration kept is left as it is. The view sits at her own eyes: there is no manual height
  offset any more (`vr.seated-offset` is retired - an older options file may still set it and
  nothing reads it), so the camera and the hands' anchor stay at the calibrated height.
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
- **Left-handed play** (`vr.left-handed`; `vr.c` `bind_stick`, `sync_stick`): the sticks, face
  buttons and triggers swap hands - the moving stick to the right, the turning stick (and Hewie's
  flick gestures and stop click) to the left, use to X, status to A, backstep to B, Whisper to Y,
  crouch to the right trigger and kick to the left - while the hands themselves keep their own
  sides: the grips, the drawn arms and the grab are her hands, whichever way the player holds them.
  The pause button is the exception: it stays on the left controller's menu button, because the
  right Touch controller has no bindable button of its own there (its system button is the
  runtime's). OpenXR will not re-bind a live session, so both hands' parts are bound once when the
  session opens (`move_r`, `turn_l`, `interact_l`, ...) and `sync_stick` reads the pair the option
  names; a change is in effect on the next frame. Nothing else is on the sticks: the both-clicks
  recentre is gone, so the calibration is the menu's one way and a scene is not interrupted by it.
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
  atlas's corner, as the first try did, wrote no alpha at all and changed nothing). A menu asks
  for it once a tick and the value holds until the next (`sUiOpaque`, cleared at the tick's
  start): the headset draws several frames a tick, and a request kept for one frame only left the
  panel see-through on two frames in three - a fast flicker over every UI. The pause menu asks
  for it too, at its dim's own alpha, so the world is still seen through the dim and not through
  its letters.
- **The game's pixel font is drawn point-sampled** (`render.c` `render_nearest_2d`;
  `bind_engine.c` `game_text`): the menu's words come from the game's own 16 x 21 font, drawn at
  two to three times its size on the page. A smooth filter turns a one-pixel stroke into a ramp
  that never reaches full ink, so every letter read as a wash the paper showed through - in the
  window as much as on a panel, which is why it looked like an old fault. The font's quads are
  sampled with `GL_NEAREST` (a sampler of their own, only while the game's font draws), so a
  stroke is a stroke; the art around them keeps its smooth filtering.
- **The defaults are the game as it was**: smooth turning, no vignette, the stick walking her,
  neither seated nor left-handed, her eyes as the first pose found them, a scene on its screen,
  B holding the microphone and the subtitles off. The panic's effects are the page's Reduced
  preset (the grey tint, the stumbles, the full beat) - the one set whose default is a choice
  rather than the original, the screen effects' photosensitivity the reason.
  `vr.calibrate-start` is 1: a first run is asked to calibrate once (with no calibration kept, the
  first pose's height is used as before).

## Status

Built: the options' store (`VrOptions`, the `vr.` fields, `scripts/vr-options.fs`, read at start-up,
written on change), **turning** (Smooth, Snap 45, Snap 90), **the comfort vignette** (Off, Low,
Med, High - 25, 50, 75 per cent of the corners, its own ring over the picture), **the height
calibration** (kept in the options, a first run's screen on the title, the page's row for later,
a save's load measuring without a prompt, one action: the height, the world's size and the view's
origin together), **seated play** (`vr.seated`), **teleporting**
(`vr.teleport`: Off, Fixed, Dynamic - the stick flick, its aim walking out until something stops
it, facing the way it went, a glow for the landing), **left-handed play** (`vr.left-handed`: the
sticks, face buttons and triggers over, the hands and the pause button as they are), **the
microphone's mode** (`vr.whisper-open`:
PTT, Open Mic) and **its subtitles** (`vr.whisper-debug`: Off, On, Debug - Off none; On the command
Hewie heard, capitalised and with an exclamation mark, and nothing when the words made no command;
Debug the command and the words Whisper heard in brackets, or `UNRECOGNISED:` and the words, in the
message window's panel at the bottom of the screen), and **the VR options page**
(`scripts/subscreen/vr.fs`, eleven rows and the panic effects' own page): the title's `VR OPTIONS`
row and, in a game, the sub screen's page after the options - both worked with the controllers, the
sub screen's arrows read from the VR stick in a headset. Its last two rows are the panic's
**preset** (Full, Reduced, Minimal; Custom shown, never stepped to) and the `Panic effects` button
that opens the seven individual settings in the status screen's own look (grey/darkening tint,
heartbeat smear, zoom blur, negative flash, camera shake, panic stumbles, heartbeat haptics Off /
Half / Full). A preset writes its settings over the individual ones; any other set shows as
Custom. The keyboard's `P` opens the same seven in the F1 menu's panel (`scripts/panic-menu.fs`):
arrows, Return or right to change, left back, P to close. Both write the same saved values.

The options the game had are the defaults, so a run with none of them picked plays as it always did.
Each row that is a choice steps round its words (Smooth, Snap 45, Snap 90; Off, Low, Med, High;
Off, Fixed, Dynamic; On, Off; Off, On, Debug) and the row's value is capitalised as the other
menus' words are.
`vr.teleport-reach`, `vr.seated-height` and the calibration are kept in the options but are not
rows - the reach is what a Dynamic flick grows to, and seated play sizes the world itself.
The F1 settings menu (`scripts/graphics.fs`) is still the keyboard's: arrow keys only.
A headset session prints one line to the console as it opens - `vr: comfort 1 (0.75), scenes from
her eyes 0, world scale 0.894 (eye 1.79 m), seated 0` - so a setting that never reached the
engine can be told from one that did nothing (vr.c, the first frame).
