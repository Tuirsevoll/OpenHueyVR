# Hands (VR)

Fiona's hands on the Quest controllers (`src/game/vrhands.c`).

## Purpose

Her arms reach the grip poses, the hand shapes follow the triggers, and a grip takes a small
object, a door's knob or a wardrobe leaf. This page covers one part of that: **wall detection** of
the room's walls, its door leaves and its placed objects, so a controller can say when a hand has
swept through something solid.

## Rules

- **The wall test** (`hand_path_hits_wall` in `src/game/vrhands.c`): the straight path a hand took
  this frame - from where it was last tick to where it is now - is sampled, and a sample point
  within the skin of one of the room's mesh triangles makes it a hit. Each triangle is skipped when
  its bounding box is clear of the path's bounding box, expanded by the skin: the same answer,
  cheaper.
- **Every face of the room's mesh counts**, on whichever side it faces: a wall, the floor, the
  ceiling, a table's top. There is no look at the triangles' normals; only a sliver with no facing
  at all (its edges' cross product nothing) is left out.
- **Doors count** (`hand_path_hits_door`): every door the room has, with `present` set, is a leaf
  12 wide, 22 tall and 1 thick in its own frame (`door_local`, so its swing and its turn are
  taken). Every face counts, so a shut leaf's face and an open leaf's edge are hits alike.
- **Placed objects count** (`hand_path_hits_placed`): the room's placed objects' meshes are read as
  `placed_touch` reads them (shown, with a mesh, placed by `placed_world`). Every face counts - a
  table's top as much as its side.
- **The 0.40 skin**: the hands ask all three tests with 0.40 room units.
- **The sampling**: the path is sampled every 0.2 units - n = len / 0.2 + 1 points, capped at 32,
  evenly spaced, the first at the hand's last place and the last where it is now. A fast sweep
  therefore cannot step over a thin wall; the doors and the placed objects use the same points.
- **What the hand holds is not in its way**: the door or placed object that same hand holds
  (`HOLD_DOOR` its door; `HOLD_PROP`, `HOLD_WARD` its placed object) is the skipped index, so a
  leaf that follows the hand does not buzz it. Detection runs while holding anything else.
- The room's draws are read as `room_touch` reads them: a batch with fewer than three vertices, the
  bloom mask, and a batch whose visibility group is hidden are left out.
- A hand entering a wall, a leaf or a placed object buzzes its controller once
  (`vr_haptic(side, 0.15f, 0.03f)`); leaving it lets the next entry buzz again.
- **The arm stops at the room's mesh** (`path_stop`, `hand_stop` in `src/game/vrhands.c`): a wall,
  floor or ceiling of the room's mesh stops the hand's arm at `HAND_STOP`, the buzz's own 0.40 from
  the surface. The stop is the free point nearest the face, found by the 0.2 sampling and eight
  halvings, and it is always free, so asking again from it gives the same answer (the arm is posed
  once a tick and once a drawn frame and must not creep). A hand already at a surface, and a path
  clear of it, are let through: the hand is never trapped.
- **The stop slides along the surface** (`path_slide`): the stop is asked for, and where it met a
  face the rest of the motion has the part going into that face removed (`wall_normal_at` gives
  the face's normal, turned toward the side the hand came from) and the sideways part kept, so a
  hand pushing at an angle slides along the wall instead of sticking. That is asked again from the
  new place, up to three rounds, so a gentle corner can be turned. A hand pushing straight in has
  nothing sideways left and stops dead. Each round's answer is free of the surface, and the whole
  path is still swept, so a fast hand cannot pass through a thin wall.
- **The wrist and the fingertip both count**, for the stop and for the buzz. The hand's length is
  `hand_length` (`HAND_METRES` of hand, `HAND_SCALE` larger, fitted like `hand_scale`); the tip is
  that far along the controller's `fwd`. The stop is the wrist stop or the tip's stop taken back to
  the wrist, whichever is nearer the place the hand already had, and the buzz fires when either the
  wrist's path or the tip's path comes within 0.40 of a face - so a hand reaching fingers first is
  stopped and warned.
- **The stop is kept in world space** (`sHandStop`, a point in the room, not an offset from her
  head), so a hand pressed against a wall stays where it touched while she walks or turns. It is
  let go when the controller is more than `HAND_RELEASE` (temporarily 5.0, about an arm's length)
  from it; the first frame after the controller is found takes the hand as it is (no sweep, no
  pop), and losing the hand or changing room drops the stop.
- **The raw controller still does everything else**: the buzz's detection (`hand_path_hits_*` in
  `vrhands_tick`), the door push (`push_step`) and every grab and swing read the controller's own
  position, unchanged. Only the arm is stopped.

## Ladder grab

A free hand whose grip is held above `LADDER_GRAB_GRIP` (0.5) in the air beside a ladder takes that
ladder in the hand. The room's ladders are its nav links (`src/data/navmesh.h`: `spot[0]` the foot,
`spot[1]` the top; the game's y runs down, so the top is the smaller y), and a hand counts when

- its wrist (the `at` the other grabs use) is within `LADDER_GRAB_REACH` (8.0 world units) of the
  line the two spots make across the floor (x and z), and
- its height is between the top's y less `LADDER_GRAB_RISE` (5.0) and the foot's y plus it - the
  range worked out from the two spots either way round.

Both numbers are first values, to be tuned. It is asked only while Fiona is on a ladder: her model
is playing one of her climbing animations (`$700`..`$709`, scripts/fiona/anims.fs). Her mode is a
field of her Forth state and is not readable from C, so her model's motion says it.

The grip rules are the rest of the page's: the grab is a grip held above `LADDER_GRAB_GRIP` (0.5) -
softer than the door grab's 0.65, so the hand need not be squeezed hard or quickly - and a release is
a grip of 0.4 or under, exactly as everywhere else. The grab sets no hold (`sHold` stays
`HOLD_NONE`), so a door, a prop, a leaf or loot still takes the hand exactly as before; a ladder is
taken only when none of them did, which is checked after their grabs in the same frame. It keeps the
link (`vrhands_ladder_hold(side)` gives it, -1 none) and buzzes that controller once
(`vr_haptic(side, 0.15f, 0.03f)`); staying in the zone does not buzz again. Her body, her animations,
the stick and the arm's stop are untouched.

## Pull to climb

A hand holding a ladder (above) climbs her by being pulled (`vrhands.c`: `sLadderAnchor`,
`sLadderPull`, `ladder_pull_step`). Nothing else moves her on a ladder in the headset: the stick is
not read there and her root motion is not used (`scripts/fiona/ladder.fs`, `vr-climb`).

- **The pull is measured in play space**: the hand's place as an offset from her eyes, turned by her
  yaw (`play_from_world` with `vr_anchor_get`'s eye and yaw), not its world place. Her own move
  therefore cannot feed back into the measurement: a hand held still cannot move her, and a hand
  moving exactly with her moves her not at all.
- **Down the hand is up her**: pulling the hand down moves her up, so the body's move is the
  negative of the hand's y change. The game's y runs down, so up is negative; a hand pulled down 2
  gives the body -2.
- **Each tick** a hand on a ladder takes its play-space place, `ladder_pull_step` gives the body's
  move from the anchor and moves the anchor on with the hand, and that move is added to one pending
  total for both hands together (`vrhands_ladder_pull`). Counting the same motion twice is
  impossible, since the anchor has already moved.
- **On a grab** the anchor is taken from where the hand is, and nothing is pulled that tick. **On a
  release, a lost hand or a room change** the anchor is dropped and what was pulled and not yet
  taken with it.
- It moves her and nothing else: no hold is set (`sHold` stays `HOLD_NONE`), the buzz, the door, prop,
  ward and loot grabs, the arm stop and the wall code are exactly as they were. The arm still stops
  where it did (below).
- The scripts read it with two C words: `ladder-hands?` ( -- flag ), true while either hand holds a
  ladder, and `ladder-pull` ( F: -- dy ), the pending total in the game's y, taken (cleared) by the
  call.

## Status

Walls, floors and ceilings stop the arm at 0.40 and let it slide along them. Doors and placed
objects buzz only.

A hand also grabs a ladder in the air beside it while she climbs: the link is kept and that
controller buzzes once. The grab no longer needs the climb to be running: a new grab on a ladder
starts her getting on, from the nearer end, with no button (the button still works).
Pulling a hand on a ladder now climbs her (`docs/subsystems/fiona.md`, F5:
`vr-climb`), and an end is taken when her height has reached it. Letting go with one hand at the top
dismounts her; letting go with both hands at the bottom places her there (unchanged); the mid-ladder
both-hands fall is not built yet.

Both touch buzzes are off (WALL_BUZZ 0 and STOP_PULSE 0 in vrhands.c); detection and the arm stop
still run.

Checked by `tests/hands_sweep_check.c` (`hands_sweep_check`): a path across a wall, a path clear of
it, a fast sweep through a thin wall, a path along a floor (a flat face counts), a path coming down
onto a table top within the skin and one staying well above it, a path across a shut door's leaf
(and the same when that door is the one held, and well beside it), a path across a placed object's
face (the same when that object is the one held), a path over a placed object's flat top, a placed
face moved and turned by placed.c's own placement - found where it now is, not where it was - and
the arm's stop: a path into a wall stopping 0.40 short, the same stop asked again from there, and
a path clear of the wall (or a hand already at it) let through; the wrist and the fingertip; and
the slide: a hand pushing at an angle stopping 0.40 out and keeping its sideways motion, a hand
pushing straight in stopping dead with no sideways move, the slide asked again from its own point,
a hand moving parallel to the wall let through, and a fast hand crossing the wall in one move
stopped in front of it; and a ladder's grab zone: a hand beside the middle of a slanted ladder's
line, one 12 units away from it, one above the top spot's reach, and one at the foot spot's height.

## Left

- Sharp corners: the slide tries three rounds, so at a very sharp inside corner the hand may stop
  rather than turn.
- Doors as blockers: a hand still goes through a leaf; the stop must ride on the leaf's own frame.
- Placed objects as blockers: a hand still goes through a crate or a table.
- Held things still follow the raw hand (a prop, a leaf or loot can clip into a wall while the arm
  stops), because `HOLD_PROP`/`HOLD_WARD`/`HOLD_LOOT` move by the controller's position.
- Loot items: a hand's own pickup (`HOLD_LOOT`) is not a source of hits, so an item in the hand
  says nothing.
- The held object hitting other objects: the thing in a hand is not swept against the world.
- Both hands off: no fall yet (step 2b). A hand's pull climbs her (step 2); letting go of both hands
  leaves her hanging where she is, with nothing moving her.
