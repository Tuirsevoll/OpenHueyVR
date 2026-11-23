# Rooms: the room being played, and going from room to room

## Purpose

The house is about 140 rooms joined by doors and doorways. One room is played at a time (it is
drawn, and its characters act fully); the others still exist (characters elsewhere move
through them off-screen). This subsystem owns which room is played and the change from one to
the next when the controlled character goes through an exit.

## API

| Message | Stack | To | Meaning |
|---|---|---|---|
| `go-through` | `( exit -- )` | rooms | The sender (the controlled character) has gone out by this exit of the played room. |
| `go-to-room` | `( room exit -- )` | rooms | Play `room`, arriving by `exit` (-1: no exit - a new game, a jump, the debug console). |
| `leaving-room` | `( room exit -- )` | broadcast | The played room is being left by `exit` (the story's phase 4). |
| `arrived` | `( room exit -- )` | broadcast | The new room is loaded and its characters arrive by `exit`, the one on this side (the story's phase 5; each character puts itself at that exit if it came with the player). |
| `entered-room` | `( room exit -- )` | broadcast | The room is in play: the camera starts on it, the story runs its entry (phase 0). |

**Facts:** `room-id` (the played room), the room graph (`room-exit-leads`, `route`...), whether
a room exists, its exits' spots (`exit-spot`, `exit-stand`).

An exit's spots are 4 inside the room from its door, 12 beyond it and 12 inside (`exit-spot` 0, 1,
2: `Doors_NavSpot`): found by walking the nav mesh from the door's own spot, then from each
triangle's centre, by the edge the step leaves through (`NavTri_ExitEdge` - a spot on a
triangle's edge is in it). Arriving, she stands on the first; without one, on the third
(`Fiona_Vt38`'s), else by the room's first camera.

Names: rooms are named in `scripts/room-names.fs` (`front-garden-2`).

## State

The played room; the exit it was entered by; a change in progress.

## Rules

From `SceneGame_LeaveRoom`, the play sub-state (`scene_game.c` ~1033-1095), `SceneGame_RoomIn`,
`SceneGame_EnterRoom`.

1. Only the controlled character's going out changes the played room. Others going out of it
   just go (their own off-screen travel).
2. Leaving by exit E: the room behind E is the next. The exit on its side (`rooms +0x14`, the
   other side's exit) is the arrival exit, which the next room's scripts read as "the exit
   taken".
3. The order of a change:
   1. `leaving-room` (the scripts' phase 4);
   2. the next room loads (synchronous here: the original kept running phase 3 while it
      streamed in);
   3. `arrived` (phase 5); every active character is told the arrival exit and forgets its
      camera setup;
   4. the camera's new room (no set, no path, no target);
   5. `entered-room`: sounds, then the scripts' phase 0. Then the characters in it enter, and
      the camera takes the room's sets and paths.
4. The room's sounds (bank 6) are swapped on leaving and entering, unless `keep-room-sounds`
   says to keep them (*to name*).
5. A forced exit ($80 and up: the scripts' cmd_exit with bit 7) goes to the room loaded ahead
   into the spare slot - `room-preload` (event 0x3A) or `exit-prepare` (0x01: the room behind an
   exit) - arriving by that same number (SceneGame_LeaveRoom). The room left stays in the spare
   slot (the original's two slots swap), so a forced exit with nothing else loaded ahead goes back
   to it; with no room left either (a new game, a jump) it takes exit 0's door.
   A save keeps the spare (`spare-was`, the rooms actor's line; put back as the save's people are):
   the hidden room $27 (`off-map-27`), in by a hole from any of six rooms, has only its forced way
   out. The original can't save there; OpenHuey's quick and bug saves can, and before 0.2.42 a load
   there left the spare as the room played before - its way out led elsewhere or nowhere.
   A save point's save (`save-slot`, SceneGame_Save) clears flag $18 (the stalkers kept where they
   are) in the game as it saves - so it is neither saved nor kept on; a quick save leaves it.
   The rooms' saved words (Rooms_SavedState, 13) are the closed-off doors: the progress's.

## Drawing

The room draws in the original's renderer layers, in order with the characters' (theirs $A;
`main.c` `draw_layer`, `room.c` `room_draw_layer`; `RoomMgr_Draw`, `Doors_Draw`,
`PlacedObjects_Draw`):

| Layer | What |
|---|---|
| 1 | the solid part (0), the doors, the plain placed objects |
| $19 (8 with the fog drawn early) | the see-through part (1), the flip books (4); "a_" objects always $19 |
| $1F | the glows (part 5, no depth writes) |
| $26 | the bloom mask (part 2) and the "g_" objects: they mark the mask only, no colour |

Part 3 is never drawn. Characters behind glass are drawn before it, so its depth writes don't
hide them. Room, door and placed draws drop a triangle with a corner outside the PS2's clip
volume, as their microprograms do (`D_003A7530` and the placed kinds': CLIP, FCAND, ADC -
`render_mesh_ps2_clip`). **In a headset that test is off entirely** (0.2.51): the eye is the
camera, so the PS2's two screens-wide bands, its near plane and its far plane are not the
picture's any more - and a polygon thrown away whole (the floor at her feet when she looks down
has a corner behind the eye) reads as the level de-rendering at the edge of vision. The eye's own
frustum cuts polygons where they cross its edge, and its near plane cuts the ones the eye is
inside (VR already brings that in, `vrbody_near`, so her own body does not clip away).

The room's lights (section 4): without one, no lights and the ambient 128 (`Lights_Reset`). A
character off the nav mesh takes light 0 alone, wherever it reaches (`Lights_ForModel`).

## Seamless room loading (VR option, test)

`Room loading: Original / Seamless (test)` (`vr.room-loading`, `scripts/vr-options.fs`, default
Original) lets one doorway be walked through with no fade and no jump. `PROPS/room_layout.txt`
(Sean's `export_layout.py`) gives each room's 3x4 place `W` in one shared world; `src/game/roomlayout.c`
reads it. `PROPS/seamless.txt` names the doorways (`ST_001 2 ST_002 2`). `src/game/roomseamless.c`
loads the other room draw-only into a second `Room` (`room_load_view` in `room.c`: the same mesh and
texture path, no global resets; its placed objects are left out, since only the room's own scripts
know which are shown, hidden or parked), draws it with `view_proj x T(other -> played)`, and suppresses the
scripts' old way out (`kit.fs exit-out`, the exit area's `take-exit`, walk-out and fade). Crossing
the doorway's line (its spots and event area, or its door) hands the played room over with the normal
`go-through`: Fiona and a nearby Hewie are put where they were converted by `T(from -> to)`, so the
world position does not change. Original mode does none of it.

**Seams (`PROPS/seams.txt`, `src/game/seam.c`).** A doorway between rooms that are not side by side
in the map is joined by measured frames (a point on the floor in the opening, the way out as a yaw,
the opening's width and height - the room data's y is up) instead of the layout: `theta = yawB + pi -
yawA`, `T_A->B(p) = pB + R(theta)(p - pA)`. A `layout` line uses `room_layout.txt` as before. A line in
`seams.txt` is also a seamless pair; the transform for a pair comes from `pair_T` in `roomseamless.c`.
The other room is drawn through the opening as a stencil portal (`render_portal_begin/end`,
`render_clip_plane`; main.c `draw_scene`): the opening's quad is marked in stencil bit 2 against the
played room's depth, the depth there is pushed back, the other room is drawn clipped at the plane
(`gl_ClipDistance` in the scene shader, the plane in clip space), and the plane's depth is put back
where it drew nothing. With the eye past the plane (a head through the doorway) the played room is
clipped to its side and the other drawn whole, clipped to its own. Debug words: `seam-outline`,
`seam-portal`, `seam-tint`, `seam-info`.

A room can have several seams (up to 8): each doorway a pair joins is an active seam with its own line,
transforms, neighbour (a draw-only room, one loaded a tick, nearest doorway first) and portal. A
measured seam's floor ends at its line, so it hands over as she reaches it (0.1 short); a layout pair
at 3 past the line. A door in the exit must be swung open (20 degrees) to cross and more than 4 to
show the room beyond. The doorways' void shells (always-drawn boxes of 6..24 triangles, untextured
or more than 30 across) are not drawn in a paired room. A follower near her is put at the converted
place only if that is on the new room's floor; otherwise he comes in by himself. `PROPS/seamless_all.txt`
(from the connected map's report) lists every doorway the layout already joins.


## Design notes

- The original loaded the next room into a second slot ahead of time (`exit-prepare`, event
  0x01). We load at once; the two-slot streaming is not needed on PC.
- The fade is not part of the change in the original: the door animation and the scripts do
  it. It is the same here.
- Room changes the story makes (a cutscene moving Fiona somewhere) are `go-to-room`.

## Status

**Built** (`scripts/rooms.fs`). Checked by `tests/rooms/test_rooms.fs` (26 tests: a new game in
the cage room; out by an exit - leaving / arrived / entered in order, with both exits; back
again; an exit to nowhere; a jump; the fog drawn early by its 26 bytes, past its section; no
lights section; light 0 off the mesh; an exit's spots on a triangle's edge, her arrival back from the kiln room). The placed objects' layers: `tests/graphics`. Listeners subscribe to `leaving-room`, `arrived` and
`entered-room`.

The rooms' own sounds come with their scripts (`room-sounds`). The sections where Hewie is the
one played (the mini-game: `hewie-controlled?` is `hewie-played?`) are built:
`docs/subsystems/hewie.md`, `docs/subsystems/story.md`.
