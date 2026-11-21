# Hiding spots (VR)

## Purpose

The base game hides Fiona in a cupboard, wardrobe or under a bed with a canned animation: the room
walks her in, holds her and walks her out. In a headset the player walks herself, so the spots the
mod's Blender exporter marks (`PROPS/hidespots.txt`) get a small volume instead: their parts are
pulled by hand (a wardrobe's two bifold panels, the gallery curtain), one closure decides how well
hidden she is, and she may step through the spot's edge only while it is open enough. Beds and every
spot without a marker keep the base game's scripted hiding.

## Where a spot comes from

`PROPS/hidespots.txt` (a mod's library data, read by `game/vrmarkers.c`) gives each spot a GRAB box
(where a hand takes it) and a BOUND box (where she is hidden), in the room's own space, **+y up**.
`game/vrspots.c` registers one spot per marker on room entry and picks its parts:

- a place that still types its own box with `vr-hiding-spot` names its leaves; when a marker spot is
  near them (6 units of either box) they are that spot's parts and the typed box is ignored
  (`vrspots: <room> using marker box, script box ignored`);
- otherwise every shown placed object within 4 units of either box that is a door: a track 0 with two
  flat ends that moves some vertex more than 2 units, or a morph (kind 1 / 3). The nursery's chairs
  and its plain `right` / `left` are never parts.

The parts are logged once: `vrspots: <room> <spot> parts: <name>(fmt F, track 0 lo..hi) ...`.

A room with a `vr-hiding-spot` call and no marker (old-mansion-1f-22) keeps the typed box and the
hands' `ward_*` leaves, as before.

## One closure per spot

Every part is posed from the spot's one closure `c` (0 open .. 1 closed) every tick, so a wardrobe's
two panels always fold and slide together:

| Spot | Parts | Scrub source | c = 0 (open) | c = 1 (closed) |
|---|---|---|---|---|
| castle-2f-12 (nursery) | `right_door` (fmt 7), `left_door` (fmt 8) | track 0 | frame 31 | frame 54 (the default pose) |
| old-mansion-b1-4 (ST_050) | `l_door0` (fmt 7), `r_door0` (fmt 8) | track 0 | frame 31 | frame 54 |
| old-mansion-b1-5 (ST_051) | `l_door0` #18 (fmt 7), `r_door0` #19 (fmt 8) | track 0 | frame 31 | frame 54 |
| old-mansion-2f-8 (ST_05C) | `Curtain` (kind 3 morph) | its words | a 0, b 1, t 0 | a 0, b 1, t 1 |

The stops are the track's two flat ends (the closed one is the one that matches the object's defined
pose; a track whose ends do not match logs `vrspots: <room> closed end mismatch`). A rigid part is
posed at `frame = lo + c*(hi-lo)` through `placed_pose_at` / `placed_frame_set_f`; the curtain is
written as kit.fs's `morph` writes it (words `+0x74` 0, `+0x78` 1, `+0x7C` c). On room entry `c` is
read back from the parts' poses and words: the wardrobes start shut, the curtain open.

### A morph's model

A morph (kinds 1 / 3) is its two frames' s16 steps **from the model's own start** — `+0x60/0x64/0x68`,
s32 in 1/4096 units, exactly the vector the rigid kind (0) adds. Without it a curtain is drawn in the
wall beside its frame and half under the floor: old-mansion-2f-8's curtain sat at
`x −60.4..−56.3, y −14..14` instead of `x −65.1..−61.0, y 1.5..29.5` (open) → `x −70.3..−61.0`
(drawn), where its rail and its fixed left drape are. One decode does it for everything:
`placed_morph_start` / `placed_morph_vertex` in `game/placed.c` (drawing, the spot's handle and path,
the edge grab, the probe and the bounds), so the curtain seen and the edge grabbed cannot disagree.
Room entry logs, behind `vr-spot-debug!` (`0 vr-spot-debug!` off):

```
placed: morph 'Curtain' kind 3 def_pos(...) off30(...) start60*32/4096(...)
placed: morph 'Curtain' f0 without(lo)..(hi) | with(lo)..(hi)
placed: morph 'Curtain' f1 without(lo)..(hi) | with(lo)..(hi)
vrspots: <room> '<spot>' bound x(..) z(..) grab x(..) z(..) path x(..) z(..) overlap bound N grab N
```

The last line answers whether a marker's boxes still cover the door as drawn, and changes nothing.

A script that plays a part owns the spot: the hands let go at once, the spot is not posed, and when
its animation ends `c` is read back from the pose it left.

## Grabbing and scrubbing

`HOLD_SPOT` in `vrhands.c`: a rising grip takes the spot **by a part's edge first** - the hand within
`VRSPOT_EDGE_R` (4.0) of the part's handle **horizontally** (x/z only), with the hand's height inside
the part's own world bounds grown by 2 (the BOUND's height when the part has no bounds). The handle is
where it is at the current pose, so an opened door is grabbed at its open edge and a shut one at its
shut edge: the grab follows the door. The GRAB box grown by 1 unit stays as the fallback. The hand
keeps an offset from the handle, so nothing jumps; each tick the hand's target is projected on the
handle's path (`game/vrpath.c`, `vrpath_edge_gap` is the same x/z rule) within ±0.25 of the current
closure, and the result poses every part. A stop (0 or 1) buzzes once. Letting go faster than 0.02 a
tick keeps that speed to the stop. A spot's parts are never carried as props nor taken as exit doors.

A morph part (the curtain) is read and written through its own three words; a script's takeover is
judged by the **weight** they make (`w = (b == 1 ? t : 0) + (a == 1 ? 1 - t : 0)`, within 0.01), so
our own write - or a swap of a/b, or a wrapped t - never looks like a script's.

`vr-spot-closure` (0..100) reads the active spot's closure; `vr-spot-closure!` sets it (tests,
console); `vr-spot-let-go` makes any hand let go (a script about to play the spot); `vr-spot-debug!`
turns on the debug lines:

- `vrspots: take '<spot>' by box|edge c C hand (x y z)` and `vrspots: release '<spot>' reason R c C`
  (`let-go`, `script-anim`, `morph-words-changed`, `room`, `drop-all`, `other`);
- `vrspots: <room> '<spot>' state inside F full F half F enclosed F c C` on a change;
- `vrspots: script <word> started|ended`, `vrspots: flag fiona-hidden on|off`, `vrspots: avoid-prompt`;
- `vrspots: fade <part> idx I alpha A layer L shown S draws N` and, per draw, its blend / additive /
  mask / no_zwrite / solid_tex / group and its vertex-alpha range;
- `vrspots: edge '<spot>' part K <name> handle(x y z) c C` once per grip press near a spot;
- `vrspots: miss <spot> at(x,y,z) edge-dist D grab-dist G`.

## The hidden state

- **Inside**: her feet's footprint in the BOUND's own horizontal axes (0.3 margin) and her hip point
  (feet + 8 units, +y up) within the box's height grown by 2. The box's bottom is above her feet, so
  the hip is what the height is asked of.
- **Full** while inside and `c ≥ 0.85` (once full it holds down to 0.80); **half** while inside and
  `c > 0.40`; **enclosed** at `c ≥ 0.995`. Outside the BOUND all clear at once.
- `kit.fs` `vr-hiding-flags` sets `fiona-hidden` / `fiona-half-hidden`, the avoid prompts and
  `no-stalker-camera` from those, unchanged; a discovery (`found-her`, held) still owns them. The
  hiding prompt (`0 avoid-prompt`) and `vr-hiding-on` are set only on the tick she becomes **full**:
  inside with the doors or the curtain open is not hidden, and shows no prompt.
- Every headset watcher (`vr-wardrobe-watch` in castle-2f-12, `vr-cabinet-watch` in old-mansion-b1-4,
  b1-5 and 1f-22, `vr-curtain-watch` in old-mansion-2f-8) starts its hiding loop only on
  `vr-spot-full?`, and each loop leaves by its own exit path when `vr-spot-inside? 0=` **or**
  `vr-spot-full? 0=`; the C hysteresis keeps full down to 0.80. `vr-hidden-calm` runs only while full.
  The discoveries inside those loops (found, panicking, held) are as they were.

## Getting in and out

- A step across the BOUND's footprint edge, in or out, is allowed only while `c ≤ 0.40`
  (`VRSPOT_ENTER_MAX_CLOSURE`). Wardrobes start shut, so she must pull them 60% open to walk in; the
  curtain starts open. Once in and past 40% she cannot walk out until she opens it again.
- Inside with `c > 0.40` her step is kept a body's width (`VRSPOT_BODY_R` 1.0) from the footprint's
  sides; outside with `c > 0.40` a step into the footprint is stopped at the face she came to.
- `vrspot_noclip()` is true while her feet are within the footprint grown by 4 units and `c ≤ 0.40`
  (or she is already inside), so the room's solid nav stops counting there: she can step onto the
  wardrobe's floor or into the curtain alcove. `scripts/fiona/moving.fs`'s `vr-spot-move` uses it in
  every room. Her position is only ever moved there and by her animation's root motion (the scripts'
  `vr-head` is Hewie's); there is no separate head-driven move to route.

## The door seen through

`vr.hide-door` (default on): inside, full, the active spot's parts are drawn at alpha 0.5. Every part,
in every room - the old cabinet doors went fully invisible because a faded placed part blended with
its vertices' and texture's alpha (the PS2 never blended those, so their alpha is often 0):
`placed_draw` fades them through `render_mesh_alpha_solid`, whose solid draws use exactly 0.5 and
ignore the vertex **and texture** alpha (`U_SOLID_TEX` is forced on for them, as well as
`u_fade_solid`; cut-out, additive and bloom-mask draws keep their own). A fade line in the debug log
prints, for each part, its alpha, layer, `shown` and every draw's flags, so a part that is not drawn
like its room's siblings can be told at once.

## Scripts

The places keep their `vr-hiding-spot` calls (they name the leaves). castle-2f-12's
`wardrobe-floor-follows` / `wardrobe-noclip-follows` read `vr-spot-closure` ("open" at 40% or less).
old-mansion-2f-8 offers its scripted `hides` only on the flat screen (`vr? 0=`) and in the headset
watches the curtain spot (`vr-curtain-watch` / `vr-hides`, as the nursery's), opening the curtain at
`came-in`.

## API

| Word / function | Meaning |
|---|---|
| `vr-hiding-spot ( a1 n1 a2 n2 F: ox oz yaw half depth ix iz -- )` | a place registers its leaves and its typed box; a marker spot nearby takes the leaves as its parts |
| `vr-markers-line ( addr len levers -- flag )` | one hidespots / levers line, as `vrmarkers_parse_line` reads it (the tests) |
| `vr-spot-here ( F: x y z -- )` | Fiona's place from the scripts' own `her-at` |
| `vr-spot-inside?` / `-full?` / `-half?` / `-enclosed?` | her state at the active spot |
| `vr-spot-closure ( F: -- pct )` / `vr-spot-closure! ( pct -- )` | the active spot's one closure, read / set |
| `vr-spot-noclip?` / `vr-spot-step ( F: dx dz -- dx' dz' )` | her step at the spot: the doorway rule |
| `vr-spot-release ( on -- )` | a discovery has her: no state, noclip or clamp until she is outside again |
| `vr-spot-let-go ( -- )` | any hand on a spot lets go (a script plays it) |
| `vr-spot-debug! ( on -- )` | the spot's debug lines (take / release, state, script, flag, fade, edge, miss; on at start, `0` off) |
| `vr-hiding-flags` | once a frame: the flags, the prompt, `no-stalker-camera` |

## Status

Built and tested headless: `tests/story/test_vr_hiding.fs` (the nursery's marker spot: one closure for
both panels, the 85/80 hysteresis, inside with the doors open at nothing, 86% full, 81% still full,
79% not full while half, 90% full but not enclosed, outside with the doors shut at nothing, the
BOUND's footprint, the doorway rule in and out), `tests/story/test_vr_hiding_cabinets.fs` (the two
cabinets' marker spots, the old mansion's marker-less 1f-22 cabinet, and the curtain enterable at
entry) and `tests/vrspots_check.c` (the handle path's projection, the edge grab's x/z distance for a
track part and a morph part at c = 0 and c = 1, the height rule, and a fractional frame's pose). The
markers are injected with `vr-markers-line`, the same parser the file is read with.

