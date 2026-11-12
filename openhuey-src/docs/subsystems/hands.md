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

## Status

Walls, floors and ceilings stop the arm at 0.40 and let it slide along them. Doors and placed
objects buzz only.

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
stopped in front of it.

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
