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
  within the skin of one of the room's mesh triangles makes it a hit.
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
  floor or ceiling of the room's mesh stops the hand's arm at `HAND_STOP` from the surface - the
  buzz's own 0.40 (temporarily 2.0, a diagnostic, so the stop can be seen). The stop is the free
  point nearest the face, found by the 0.2 sampling and eight halvings, and it is always free, so
  asking again from it gives the same answer (the arm is posed once a tick and once a drawn frame
  and must not creep). A hand already at a surface, and a path clear of it, are let through: the
  hand is never trapped.
- **The stop is kept in world space** (`sHandStop`, a point in the room, not an offset from her
  head), so a hand pressed against a wall stays where it touched while she walks or turns. It is
  let go when the controller is more than `HAND_RELEASE` (temporarily 5.0, about an arm's length)
  from it; the first frame after the controller is found takes the hand as it is (no sweep, no
  pop), and losing the hand or changing room drops the stop.
- **The raw controller still does everything else**: the buzz's detection (`hand_path_hits_*` in
  `vrhands_tick`), the door push (`push_step`) and every grab and swing read the controller's own
  position, unchanged. Only the arm is stopped.
- **Sticking is expected**: a hand stopped at a wall does not slide along it yet.

## Status

Walls, floors and ceilings stop the arm. Doors and placed objects buzz only.

Checked by `tests/hands_sweep_check.c` (`hands_sweep_check`): a path across a wall, a path clear of
it, a fast sweep through a thin wall, a path along a floor (a flat face counts), a path coming down
onto a table top within the skin and one staying well above it, a path across a shut door's leaf
(and the same when that door is the one held, and well beside it), a path across a placed object's
face (the same when that object is the one held), a path over a placed object's flat top, a placed
face moved and turned by placed.c's own placement - found where it now is, not where it was - and
the arm's stop: a path into a wall stopping 0.40 short, the same stop asked again from there, and
a path clear of the wall (or a hand already at it) let through.

## Left

- Doors as blockers: a hand still goes through a leaf; the stop must ride on the leaf's own frame.
- Placed objects as blockers: a hand still goes through a crate or a table.
- Sliding along walls: a stopped hand sticks where it met the surface.
- Held things still follow the raw hand (a prop, a leaf or loot can clip into a wall while the arm
  stops), because `HOLD_PROP`/`HOLD_WARD`/`HOLD_LOOT` move by the controller's position.
- Loot items: a hand's own pickup (`HOLD_LOOT`) is not a source of hits, so an item in the hand
  says nothing.
- The held object hitting other objects: the thing in a hand is not swept against the world.
