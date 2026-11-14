# Hiding spots (VR)

## Purpose

The base game hides Fiona in a cupboard, wardrobe or under a bed with a canned animation: the room
walks her in, holds her and walks her out. In a headset the player walks herself, so the four
wardrobes/cabinets with real placed doors use a small volume instead: the doors are pulled by hand,
the closure decides how well hidden she is, and the step that leaves the volume is blocked only
while a door is fully shut. Beds and every hiding spot without a placed door keep the base game's
scripted hiding.

## API

`src/game/vrspots.h` (the geometry, closure and step; C) and `story/kit.fs` (the game's flags):

| Word / function | Meaning |
|---|---|
| `vr-hiding-spot ( a1 n1 a2 n2 F: ox oz yaw half depth ix iz -- )` | a place registers its two door leaves, the doorway's origin and yaw, its half width and depth, and a point inside it. The leaves are marked `hiding_leaf`. |
| `vr-spot-here ( F: x y z -- )` | Fiona's place from the scripts' own `her-at` (C's `Actor.pos` is taken at a draw, not a headless tick) |
| `vr-spot-inside?` / `vr-spot-full?` / `vr-spot-half?` / `vr-spot-enclosed?` | her state at the active spot |
| `vr-spot-closure ( F: -- pct )` | the least closed leaf, 0..100 |
| `vr-spot-noclip?` / `vr-spot-step ( F: dx dz -- dx' dz' )` | she may stand on the room's solid floor there; her player step is clamped to the box |
| `vr-spot-release ( on -- )` | a discovery has her: no state, noclip or clamp until she is outside again |
| `vr-hiding-flags` | once a frame (in `every-room`): sets `fiona-hidden` / `fiona-half-hidden`, the avoid prompt and `no-stalker-camera` from the spot |
| `vr-hides` / `vr-hides-in-cabinet` | the headset's watch: the place's own search and discovery take over; a discovery uses `hold-posed` for the canned animation and sets `vr-hiding-caught` until she leaves |

## Rules

1. **The box.** The place gives the doorway's origin, yaw, half width and depth; the inside is the
   normal side of the doorway line through the given inside point. The doorway is `v = 0`, the
   inside `v = 0..depth`; the sides are `u = ±half_width`, the back `v = depth`.
2. **The closure.** Each leaf's first track is loaded at registration and held at its shut end. Its
   current turn is compared with the flat open and shut ends of that track; the smallest closure of
   the leaves is the spot's. Format 7 (turn and place) and 8 (turn) both work. A leaf is **shut**
   at 99.5% or more.
3. **Full, half, exposed.** Inside the box:
   - full at 85% or more; once full it holds down to 80%;
   - half above 40% and below the full rule;
   - exposed at 40% or less.
   Outside the box both flags clear at once. A discovery (`found-her` or held) owns the flags until
   she leaves; `vr-hiding-caught` keeps the spot from hiding her again in that visit and
   `vr-spot-release` turns the spot's state, noclip and clamp off until she is outside again.
4. **The step.** While she is in or at the box her step is clamped to it: the sides and back always;
   the doorway only where a leaf is shut. She may stand on the room's solid nav there (the step
   uses mask 0), so no room needs new floor flags. Canned moves, stalkers, room doors and beds are
   untouched. Teleporting is left out while a spot is active.
5. **The crouch flag.** `fiona-half-hidden` is shared with the headset crouch (`fiona.sneak`); the
   spot clears it only when the crouch is not using it.
6. **The door seen through.** `vr.hide-door` (default on) draws the active spot's leaves at about
   half alpha in the see-through layer while she is inside and fully hidden. Off, they stay opaque.
7. **The prompts.** Entering the spot shows the “Hiding…” avoid row once (`0 avoid-prompt`); the
   avoid actor clears it after its second, and it is not re-sent while she stays. Coast clear
   (`1 avoid-prompt`) is shown only if `chased?` was true while she was in the spot and the stalker
   has since lost her and left (`vr-hiding-chased`); a spot entered out of danger never shows it.
8. **The audio.** Inside and every leaf shut (`vr-spot-enclosed?`), `sound3d.c` muffles the room and
   `saudio.c` lowers `spatialBlend` to about 0.4, so outside sounds are hard to place. Her own body's
   sounds stay clear. A base hiding (a bed) keeps the old `fiona-hidden` muffling.

## Spots

| Room | Leaves | Doorway origin, yaw | Half width, depth | Inside point |
|---|---|---|---|---|
| castle-2f-12 (nursery) | `left_door`, `right_door` | (19.28, 38.49), 135° | 6.8, 6.0 | (20.69, 39.91) |
| old-mansion-1f-22 | `l_door0`, `r_door0` | (149.9, 70.0), 0° | 3.5, 7.0 | (156.9, 70.0) |
| old-mansion-b1-4 | `l_door0`, `r_door0` | (14.5, 30.0), 90° | 3.5, 7.0 | (14.5, 37.0) |
| old-mansion-b1-5 | `l_door0`, `r_door0` | (21.5, -30.0), 90° | 3.5, 7.0 | (21.5, -37.0) |

The other non-bed hiding spots (curtains, gaps, alcoves, the cinematic lockers) have no door and
mostly no walkable interior; they stay scripted. Beds stay as they are. The place's `came-in`
registers a spot only when `vr?` is true, so the flat screen keeps the base game exactly (its
scripted `hides`/`hides-in-cabinet`).

## Status

Built and tested headless (`tests/story/test_vr_hiding.fs`: the nursery's thresholds, the hysteresis,
one door, leaving the box and the clamped step; `tests/story/test_vr_hiding_cabinets.fs`: the three
old-mansion cabinets). The nursery's box and the cabinets' were measured from their room data; a
spot whose geometry differs is given its own values in its place script.

The left/right leaves are both hand-driven; `placed_tick` and `placed_frame_set` now support the
format 7 tracks (the right leaf and one leaf of each cabinet), which also makes the flat screen's
scripted cabinet animations move those leaves. `old-mansion-b1-5`'s cabinet doors are objects 18
and 19 (the script used 8 and 9) - fixed.
