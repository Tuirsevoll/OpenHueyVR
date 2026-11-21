# Hands (VR)

Fiona's hands on the Quest controllers (`src/game/vrhands.c`).

## Purpose

Her arms reach the grip poses, the hand shapes follow the triggers, and a grip takes a small
object, a door's knob or a hiding spot's leaf. A grip is also the pad's triangle - the way back out of
a menu or a dialogue - and the crouch trigger on the left hand drives the VR crouch
(docs/subsystems/comfort.md). This page covers one part of that: **wall detection** of
the room's walls, its door leaves and its placed objects, so a controller can say when a hand has
swept through something solid.

The arm's own chain is found by the model's names (shoulder 27/43, elbow 28/44, wrist 30/46), and
with it comes the rest of her: the VR body (`docs/subsystems/vrbody.md`) turns her chest, puts her
body under the headset, solves her elbow and rebuilds the twist helpers - all in `vrhands_pose`,
and all off where the arms are off. The arm's bones are written into the pose last, after the
animation, whichever solve made them (`hands-sweep-check`'s posed-arm section, which fails if they
are left at the animation's pose).

**Her arm's length is the calibration's** (`vrbody_arm_scale` = the body's uniform scale x the
arm's small share, `docs/subsystems/vrbody.md`): her *drawn* body is scaled to the player's height,
and only then do her two arm bones take a bounded share (0.90..1.15) so her full stretch is the
player's. Nothing is stretched past that, the target is always the controller, and the hand is
placed at the solved wrist - on the controller, with at most a wrist gap at full stretch.
`arm_lengths` (the fallback's lengths) takes the same multiplier, and remeasures if a
calibration changes it.

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
- **A hiding spot's leaves** are marked `hiding_leaf` by `vr-hiding-spot` (`docs/subsystems/hiding.md`);
  `nearest_ward` takes either leaf of the spot, one per hand. A leaf's track may be format 8 (the
  turn) or 7 (the turn and the place, the hinge moving as it swings); both are scrubbed the same way,
  so the right leaf of a pair is no longer inert. In the headset the marker spot's `HOLD_SPOT` takes
  the leaf first by its **edge** (x/z only, any height along the door) and by the GRAB box as the
  fallback, so the grab follows the door; with `vr-spot-debug!` on, the spot's hand prints
  `vrspots: take '<spot>' by edge|box ...` and `vrspots: release '<spot>' reason=... ...`.
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
  pop), and losing the hand, changing room, a snap turn or a teleport drops the stop
  (`vrhands_forget_stops`, `vrhands_drop_all`, the room-change branch of `vrhands_tick`, and
  `vrhands_pose`'s own room check for a drawn frame between two ticks) - and the body's own
  smoothed hand frames with it (`vrbody_forget_place`), so the arm is never posed from where it
  was in the old place.
- **The raw controller still does everything else**: the buzz's detection (`hand_path_hits_*` in
  `vrhands_tick`), the door push (`push_step`) and every grab and swing read the controller's own
  position, unchanged. Only the arm is stopped.

## Hewie's paw

The voice's "shake" holds his front paw (bone `0x1C`, the one animation `$1C05` lifts) at frame 45.
A hand within `PAW_REACH` (4.5) of it whose grip is above `PAW_GRIP` (0.40) has it
(`vrhands_paw`): the paw and the leg above it turn towards the hand (the petting's push, kind 5),
the grab buzzes once, and a side-to-side turn of `PAW_SHAKE_STEP` (1.0) back the other way counts a
shake and buzzes. `vrhands_paw_shake` gives the shakes, taken by the call;
`vrhands_paw_release` lets go when the action ends. The scripts are `scripts/hewie/states.fs`
(`st-shake-paw`), checked in `tests/hewie/test_commands.fs`; the hand's side is `vrhands_paw`.

## Doors

A grip on a door's knob takes the leaf (`HOLD_DOOR`), and a knob is a hand's reach away
(`DOOR_TOUCH`, 4.0 world units). A leaf swung away from the hand, its knob past the doorway's stand
line (`room_door_side`), is in a reach measured **over that line**, not at the knob: the knob's own
way past the line (`DOOR_KNOB sin swing`, 10 units from the hinge at most) is added to
`DOOR_TOUCH_AWAY` (6.5). So the hand may be 6.5 units on its own side of the opening whatever the
swing: a leaf wide open is as reachable as a shut one, and no step through the threshold comes
first. A leaf not swung at all is the plain reach from either side.

- **Held by the door, not by the room's exit.** A hold keeps the door's own id (`sHoldDoor`), not the
  exit index it was taken at: the two rooms a door joins need not number it alike (215 of the
  game's 303 doors have another exit on their far side), so an exit index is only the old room's
  once a room change has happened.
- **A room change carries the hold** (`doors_room_change` -> `door_carry`): the same door's exit in
  the room now in, its leaf present and free, the door joining the room she left to this one, and
  its knob under the hand (`knob_near` at `DOOR_CARRY_REACH`, the arrival's own move allowed for).
  driven from the next tick, not the one the room changed in (`sHoldFresh`): the hand's place is a
  point in the room and has moved with it. A grip that opened on the way through, a hand at no
  knob, a room without that leaf, a locked door or one a script holds: the hold ends instead.
- **A door the hands were still finishing ends at its stop** (`door_finish`), in the same tick the
  room changes: **the way it was going.** `sDoorVel` keeps each leaf's last tick of motion (degrees,
  `poses_save`), and `door_stop_for` finishes a leaf that moved more than a hand's tracking noise
  (`DOOR_MOVING`, 0.5° a tick) at the stop that way - a door dragged shut as she crosses shuts, one
  dragged open opens, even past the 45° rule's own answer - while a leaf let go still takes the 45°
  rule (`door_stop`). `sDoorSwing` is put at the stop, the state's open bit with it, and the leaf of
  the room now in (when it has one) put there too. The ease itself (5° a frame, `room.c` Door_Swing)
  runs in the played room only, so it dies with the room; without this a leaf stayed at the angle a
  hand last held it at, for good. Only the held bit this side set (`claim_for_letgo`, `sDoorClaim`)
  is taken back - never a script's own hold.
- **The per-exit timers go with the room** (`sSettle`, `sCoast`, `sVel`, the two hands' shove):
  they are the old room's exits', and an exit index means nothing in the room just left.
- **A load** (`door-poses-clear`) drops the hands' doors and those timers
  (`vrhands_forget_poses`): the save's doors are the save's.

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

## Pulling a lever

A lever from `PROPS/levers.txt` (`src/game/vrlevers.c`) is a placed object near its box whose track 0
is scrubbed from its rest pose (the end its own pose sits at) to its pulled one. A rising grip takes
it when the hand is inside the box grown by 1 unit, or within 3 units of its handle (the vertex that
travels furthest) as it is posed now - the same forgiving rules as a hiding spot's. `HOLD_LEVER`
keeps the offset from that handle to the hand, so nothing jumps; each tick the hand's target is
projected on the handle's path within ±0.25 of the closure and poses the object
(`placed_frame_set_f`). At 0.97 it snaps home, buzzes the controller strongly, is done (never taken
again) and queues the event the place script reads once (`vr-lever-pulled?`, `vr-lever-here?`); let go
before that, it springs back to rest in about 0.3 s. A script that plays the object takes it - the
hand lets go and the closure is read back when its animation ends - and a room change or
`hands-drop` drops the hold. A hand near a lever that misses is logged (`vrlevers: miss ...`) for the
box tuning.

The flat screen is untouched: its button prompt and the scripted pull still run (`vr? 0=`) and the
lever's object is left to the script. In VR the place scripts' `vr-pulls-it` / `vr-lowers-it` run the
very same event, in the same order, without Fiona's canned motion.

## Drawing a curtain across a mirror

Three rooms have a curtained mirror - old-mansion-1f-11 (ST_04E), b1-2 (ST_04B) and 2f-13 (ST_06A) -
and each registers its own `Curtain` morph in `came-in`, `vr?` only, with `vr-mirror-curtain
( addr len xt-ok xt-bare xt-cover -- ok )` (`src/game/vrmirror.c`). The curtain's t is the closure:
0 drawn back (the mirror bare), 1 drawn across. Nothing is registered on the flat screen, where
`the-curtain`, her walk and the `pulls` run as they always did.

- **The grab** is the curtain itself, no box: a rising grip takes it when the hand is within 4 units
  of its handle **horizontally** (x/z) with the hand's height inside the curtain's own world height
  grown by 2, and the room's `ok?` says 0. `HOLD_MIRROR` keeps the offset from the handle to the
  hand, so nothing jumps; each tick the hand's target is projected on the handle's path (33 samples,
  ±0.25 of the closure, the same `vrpath` as the levers) and written as a 0, b 1, t c - exactly the
  words `kit.fs`'s `morph` writes, so `mirror-shown` and the hand agree.
- **Firing** is once per crossing: covered, drawn back to 0.20 or less runs the room's `bare`; bare,
  drawn across to 0.80 or more runs its `cover`. Each buzzes the controller once. The flag therefore
  only changes at the thresholds, so `smash-due` (Daniella's rage at a bare mirror) always sees a
  clean state, and `drawing @` stays 0 throughout a VR pull.
- **Let go** between the thresholds and the curtain eases to the end its flag has (0 bare, 1 covered)
  in about 0.3 s - it never rests half-way. Let go at an end and it stays there.
- **The room's own words**: `vr-bare ( -- )` is the open branch of `the-curtain` without her walk or
  motion (`glass`, the struggle sound, the flag set); `vr-cover ( -- )` the shut one (the flag off,
  the glass out). `vr-mirror-ok? ( loud? -- why )` is the gate, the same rule as the prompt's
  `no-time-message` case: 0 may be pulled, 1 no time (a stalker in the mirror's area or within 40
  units), 2 broken, 3 smashing. `loud?` asks for the no-time remark, so a blocked press says why
  once, while a held pull asks silently.
- While a hand holds it the gate is asked each tick: if a smash starts (or the mirror breaks, or he
  comes) the hand lets go at once, and a script that plays the morph takes it the same way. A room
  change or `hands-drop` drops the hold.
- **The hiding spots are untouched.** A curtain that is a hiding spot's part is never registered
  (the spots' own `vrspot_placed_part`, read-only, asked only when the room has a spot marker):
  `vrmirror: <room> '<obj>' skipped (hiding spot)`. Neither ST_014 / ST_0E5 (`Cartain`, kept for a
  hiding spot) nor ST_045 (a room part, no mirror) is registered anywhere.
- **The prompt**: in VR each room's `the-curtain-offer` steps aside while its curtain is registered,
  so the physical pull replaces the button; `the-shards` and the no-time remark stay as they were.

The debug lines (`vr-mirror-debug!`, on by default) print, on entering each of the three rooms,
`vrmirror: <room> '<obj>' registered c=.. bare=.. broken=.. handle c0 world(..) c1 world(..)`, then
`take` / `release reason=..` / `fire bare|cover` / `spring-back` / `blocked: no time|broken|smashing`
and the registration refusals. Tests: `tests/vrmirror_check.c` (a fire once per crossing, the
spring to the flag's end, a spot's curtain refused) and `tests/story/test_vr_mirror.fs` (the room's
branches and its gate).

- **A held thing is drawn with the hand** (`src/game/loot.c`, "a held thing, drawn with the hand";
  `vrhands_hand_frame` in `src/game/vrhands.c`). The game moves at its 30 ticks a second, but a
  headset draws every frame it can, and her arms are posed afresh for each drawn frame (the pose
  hook, `actor.c`: `vrhands_pose`) from the controller's pose and the eyes as *that draw* has them.
  A held thing was placed by the tick instead, so between ticks it stood still while the eyes walked
  on and each new tick jumped it: the stutter - worst with her walking and the hand still. So a thing
  in a hand (`HOLD_LOOT`) is drawn from the very frame its hand is drawn from: `loot_draw_from_hands`
  (asked by `draw_frame`, once a frame, before an eye or an arm) takes `vrhands_hand_frame`'s raw
  hand frame - the wrist and the hand's axes, the frame the arm is posed from, before the wall stop -
  and that is what the thing is drawn with. It therefore sits in the hand on every drawn frame, as
  smoothly as the arm. Nothing of the game's own frame moves: what the hand gave at the tick is what
  the game keeps (`loot_follow`), which is what the pickup, `loot_nearest` and the placed-object code
  read. A hand not tracked (a menu, a scene with no anchor, no headset) leaves the thing where the
  tick has it.
- **A held item may be a mod's model** (`PROPS/item-<id>.gltf`, `PROPS/grips.txt`;
  docs/modding.md, "Held items"). It is placed at the holding hand's wrist bone (`Actor.bones` /
  `bone_axes`: 30 her left, 46 her right), turned by the grip's offset and quaternion; the game's
  own card / key / powder / ball is kept when the mod has no model. A line made for the other hand
  is mirrored across her body (offset x negated, quaternion `(x, -y, -z, w)`), and a second line
  for the same item overrides that mirror. The equipped magnesia/antimony follows the same rule.
  A `model=<file>` word names a file shared by several items; the held models' cache is by file,
  not by item, and outlives a grab: letting go and taking the item again reuses it, and a mod's
  `prop-free` never frees it. The model and debug axes are loaded off the render pass (on the grab,
  or on the equipped item's tick), not while drawing. A tint in the grip line multiplies the
  model's colours, and the `vr.item-prop-debug` option draws RGB axes at the model and the wrist.
- **A mod can shape the fingers too** (`PROPS/grip_poses.txt`, `src/game/gripposes.c`). A grip's
  optional pose name looks up a pose in that file; its lines turn and move Fiona's own
  `arm ... finger` bones relative to their rest (`local_rest * T(t) * R(q)`, the left hand
  mirrored as `(x, -y, -z, w)` and `(-tx, ty, tz)`). `vrgrip_apply` runs last in `vrhands_pose`,
  after `pose_arm`, `curl_hand` and `vrbody_finish`, and only in VR gameplay. It blends rotation
  and move in or out over 0.10 s on the real frame time, and while a pose is in it eases the hand
  morph to its rest shape (shape 0) because the poses were authored on the rest hand. It writes no
  bone that is not named `... finger ...`; an item with no pose, an empty hand, a door, a ladder,
  a wardrobe leaf or a mod prop leaves today's trigger/grip curl exactly as it was. A missing pose
  name logs once and falls back to that curl. The built-in check logs `[grip-poses] self-check
  PASS` with the right finger 2a / 3a / 5a / 2c positions, and the load line gives the pose,
  bone-line and knuckle-offset counts.
- **A grip lets an equipped thing go at once** (`scripts/fiona/letgo.fs`: `hand-defense`). With a
  magnesia, antimony (or a ball, the doll, the stone) in the equipment's slot 2, a grip pressed and
  released - `vrhands_defense_take`, armed by a close after the model is in the hand - makes the
  thing at the hand: a magnesia set down where it was let go (no speed), the aimed ones flicked along
  the hand's own swing (`defense_flick`). Her set-down / throw animation is **not** played and she
  keeps walking: no state of hers changes. The triangle it used to be bound to (see `vr.c`: a grip is
  the game's triangle in a headset) no longer starts that animation in a headset at all -
  `try-let-go` returns at once with `vr?` - so a grip taken to grab something does not drop into her
  throw either. The flat screen keeps the button's own let-go, animation and all.
- The equipped thing is drawn from the same live hand frame (`vrhands_defense_draw`), so it too stays
  in the hand while she walks.

## Status

Walls, floors and ceilings stop the arm at 0.40 and let it slide along them. Doors and placed
objects buzz only.

A held thing (loot) and the equipped magnesia/antimony are drawn from the hand's own frame at each
draw, so walking with one in a hand no longer stutters (above). Props and the hiding spots' leaves
are still placed by the tick.

The hiding spots' leaves are both driven by hand (format 7 or 8); a spot marks them and the generic
hiding-spot layer uses their closure (`docs/subsystems/hiding.md`). The right leaf of the nursery
wardrobe and one leaf of each old-mansion cabinet, silent before because their tracks are format 7,
now swing.

A hand also grabs a ladder in the air beside it while she climbs: the link is kept, that
controller buzzes once, and the base game's rung sound plays. The grab no longer needs the climb to be running: a new grab on a ladder
starts her getting on, from the nearer end, with no button (the button still works).
Pulling a hand on a ladder now climbs her (`docs/subsystems/fiona.md`, F5:
`vr-climb`), and an end is taken when her height has reached it. Letting go with one hand at the top
dismounts her; letting go with both hands at the bottom places her there (unchanged); the mid-ladder
both-hands fall is not built yet.

A held door is carried across a room change to the same door in the room now in, and a door left
mid-swing finishes **the way it was travelling** there - a door dragged shut as she crosses shuts -
rather than staying at the angle a hand last held it at (Doors, above). Checked by
`tests/hands_sweep_check.c` (`hands_sweep_check`): a door whose two sides number it apart, its hold
carried and driving on in the room now in; a door with no leaf in that room finished at its stop
with its state word, and found at that stop when its own room is entered again; a leaf dragged shut
at 50 degrees finishing shut and one dragged open at 40 finishing open (the 45° rule alone would do
the opposite); and the reach over the threshold taken where the plain one is not, a leaf wide open
included, with a leaf swung toward the hand let alone at the same distance.

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
