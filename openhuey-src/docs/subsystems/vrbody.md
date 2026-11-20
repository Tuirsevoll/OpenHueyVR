# VR body

Fiona's body under the headset, her elbow's own IK and the twist helpers of her model
(`src/game/vrbody.c`, `vrbody.h`).

## Purpose

The arms have always reached the controllers' grip poses (`docs/subsystems/hands.md`). The rest of
her did not follow the player: the chest never turned with the hands, the body did not sit under
the headset, the elbow's bend came from a fixed pole, and her forearm's twist was shared by three
bones with nothing to keep the skin from pinching - her model's own `vr ...` helper bones, added by
the `examples/mods/fiona-vr-rig` mod, exist exactly for that. This page is that work: a clean
reimplementation of the documented behaviour of **FRIK** (Fallout 4 VR:
`getNeckYaw`, `setBodyUnderHMD`, `setBodyPosture`, its shoulder IK, its elbow's limit window, its
`DampenHands`) and of **VRIK**'s first-person head ("shrink" and the near plane), on this game's
skeleton. FRIK is GPL and none of its source was read: the formulas come from its documented
behaviour and this project's notes. The repository it is named after:
<https://github.com/rollingrock/Fallout4-VR-FRIK>.

## Where it runs

`vrhands_pose` calls it, and `vrhands_pose` runs only where the arms do: `vrhands_wants` (she is
used, VR is active, she is `O_FI...`), she is visible, there is a world, and `fiona_view` has an
anchor. The anchor is dropped in a cutscene (`gCamDir.scene`), a scene and the free camera, and it
is not made without a tracked headset - so with the IK off not one matrix is touched: the `vr ...`
helpers keep their rest under their parents, and the motions (all 120, cutscenes included) are
bit-identical. `vrbody-check` proves the off case with the mod's own model.

## API

| Word (C) | What |
|---|---|
| `vrbody_bone(part, side)` | the bone this model's names gave that part (`VRBODY_*`), -1 none |
| `vrbody_karm()` / `vrbody_kbody()` | FRIK's two ratios, computed once a model |
| `vrbody_begin` | once a pose: the bones by name, the headset and the hands, the body's forward |
| `vrbody_body` | the chest's turn, the body under the headset, the feet kept |
| `vrbody_dampen` | one hand's frame smoothed for the body's pose only (`DampenHands`) |
| `vrbody_clavicle` | one clavicle's turn toward a far hand |
| `vrbody_shoulders` | her arm's root at the player's shoulder, reached by the clavicle's turn |
| `vrbody_arm` | one arm's elbow, its limit window and the wrist's twist (`VrArm`) |
| `vrbody_finish` | the forearm's roll, the upper arm's twist, the deltoid, the first-person head |
| `vrbody_near` | the near plane the eye views should use |
| `vrbody_eye_y` | the camera's head, read before the body moved it |
| `vrbody_forget_place` | her place jumped (a room change, a teleport, a snap turn): the smoothings start afresh |
| `vrbody_idle` | no pose this frame |

The first-person head is a VR option, `vr.body-fp-head` (default on; docs/subsystems/comfort.md).

## State

The names' indices (once a model: `vrbody_bone`), the two ratios, the bind lengths; the body's
forward and sideways (smoothed); the hip's offset and the spine's swing (smoothed); each hand's
smoothed frame and each wrist's twist; the forearm's roll last frame (its unwrap); the camera's
head. The helpers are rebuilt from their parent **every** frame, so nothing accumulates.

## Rules

- **Units.** FRIK's numbers are Fallout units. `kArm = L / 36.74` (L = |pos 30 - pos 27| at rest)
  multiplies a distance along the arm, `kBody = E / 120.48` (E = her rest eye height above bone 0,
  15.65 units) a body offset. Every number is a `#define` at the top of `vrbody.c`, named as FRIK or
  VRIK names it.
- **The arm's chain by name**: shoulder 27/43, elbow 28/44, wrist 30/46 (glTF's own names, never an
  index of a mod's bone). The hand is still *measured* on the geometric chain's wrist (bone 29), so
  the grip matrix - its size and its seat - is exactly what it was. Both answers are logged once a
  model.
- **Bone 0 is the hips and bone 1 is only the legs' parent**: the spine hangs from the root beside
  the pelvis. The body's move is therefore every bone under the root.
- **The body's forward** (FRIK's `getNeckYaw`): the hands' sum about the head's forward, weighted
  down by hands overhead and up by crossed hands, ±50 degrees, 0.7 of it turning the body; eased in
  with retention 0.75 a frame at 90 Hz. Its difference from her facing is spread over the waist
  (0.3), the chest (0.4) and the neck (0.3), each about its own origin and carrying its descendants.
- **The neck's target**: her neck, moved by the headset's motion from the neutral head, less 3
  kBody of head-in-front. The headset is at her **eyes**, not her neck, so a look up or down swings
  those eyes about the neck by the model's own eye-to-neck offset, and that swing is taken out of
  the move (read as the body's, it crouched her legs whenever she looked down). A drop the game's
  own crouch animation is already scrubbing her with (`scripts/story/crouch.fs`, from
  `vr.crouch-deadzone` to `vr.crouch-on`) is left to it: only what it leaves moves the hips - a
  look's few centimetres, or a duck below the deadzone. The look's own back (5 kBody·|pitch|) and
  down (6 kBody·pitch) offsets bend the **spine's aim** only; they never move the hips.
- **The hips**: back along F by tan(bodyPitch)·|n - pelvis|, pulled to the rest torso's length
  from n, two thirds of the vertical change dropped (FRIK's COM divided by 1.5). FRIK's crouch bend
  (`VR_CROUCH_BEND`) is **off** here: her crouch posture is the game's own crouch animation's, and
  bending her again would fold her twice. Never higher than the legs can reach: the move is scaled
  back until each leg is within its rest length plus 0.1 of its foot, so the feet stay on the floor.
- **The spine**: the pelvis -> neck line turned to the target, clamped forward 60, back 25, sideways
  35 degrees, spread over the waist (0.45), the chest (0.40) and the neck (0.15), parent first, each
  about its own origin. The swing and the hip's move are eased in with retention 0.6 a frame.
- **The feet** stay where the motion and `humanlegs` left them, and each leg is solved back to its
  ankle with the same law of cosines as `humanlegs.c`'s `leg` (which cannot be re-run here:
  it reads the feet from the skeleton this body has just moved). Two directions, as `humanlegs`
  has them: `bend` - **her pelvis's own forward** (`pelvis_forward`: the bind's +Z turned by how
  far her hips have turned since the bind - the animation's turn and ours, never the chest's
  swing) - places the knee, and `roll` (the hip's own Z) is what the bones' Z is aimed from.
  A leg aimed from the forward instead had its skin rolled a quarter turn, which read as the knees
  bending sideways. The knee target is then **projected onto the leg's own forward plane**
  (the plane through the hip that holds the leg and her pelvis's forward), so nothing can drift
  laterally; and **the legs are straight unless she is really down**: the pelvis keeps its
  calibrated height until the head is more than `BODY_DROP_DEAD` (4 cm) below it, so a look, a
  lean or a relaxed stance cannot fold a knee (a millimetre of drop is a whole knee's worth of
  bend in a leg this long). At that height the law of cosines gives h = 0.
- **The calibration** (`vr_calibrate`, the VR options' *Calibrate Height & Reach* row and the
  title's first-run step - one action, one set of saved values): the eye height as before, and then
  six seconds in which the furthest either controller is from the head is taken as the player's
  **reach** (a T-pose or the arms straight ahead; a full reach ends the window at once, and the row
  says "Arms out..." while it runs). It lives in `vr.arm-reach` (metres, 0 = not measured) beside
  `vr.eye-height`/`vr.body-scale`/`vr.calibrated`, and the scripts save it when the measurement
  lands (`vr-arm-save?` in `subscreen.fs`'s held frame and `opening.fs`'s calibration screen).
  From it, once a frame (one scale, the world's: her body is her own size, her head at the camera,
  and only her arms take a share):
  - **her drawn body is NOT scaled again.** `bodyScale` is **1.0** always: the height calibration
    already sets the VR world's scale (`vr.c`'s 1.6 / the player's height), and scaling her body by
    the same idea a second time buried her head and chest under the camera - the taller the player,
    the worse (`body_scale 1.077` at 1.74 m). Her actor's scale, collision, hitbox, position,
    movement, the world and the camera are untouched (`vrbody_body_scale` = 1).
  - **her head is put at the camera** (`vrbody_head_camera`, before the first-person head): the
    anchor is her head + 1.5 by the game's own camera, so her head bone is shifted to it and the
    neck stretches - her eyes are the player's, and looking straight down shows her chest instead
    of the inside of it. Logged once: the camera, her eye after posing, and how far apart they
    were (metres). **The camera sits a little inside her head**: the point that goes to the camera is
    `EYE_BACK_M` (0.06 m) *behind* her eyes - back along her forward, toward the centre of her head -
    and `EYE_UP_M` (0.01 m) *above* them (+Y is the ceiling), both in her metres. The player tunes
    them on the VR Enhancements page (*Body: Camera Back* 0..0.12 m, *Body: Camera Height*
    -0.05..+0.05 m, 0.01 a step, kept in `vr.cam-back`/`vr.cam-up`), clamped to those ranges.
    **The view must never read the seat back**: the camera keeps the head it found before any body
    pose (`vrbody_eye_y`) across cutscenes too - `vrbody_idle`/`vrbody_begin` no longer drop it - and
    the seat may move her head at most `HEAD_SEAT_MAX_M` (0.30 m) in a frame, warned once. Without
    that, a frame that lost the memory read the seated head and the seat chased it: the view flew
    upward after a cutscene until the room changed. Her place moving more than 2 m (another room)
    drops the memory, so a new room never uses the old one's height.
    The log says the seat and the reach: `vrbody: head at the camera - camera ... her eye ...; the
    camera 0.060 m behind her eyes and 0.010 m above them; reach_scale ...`.
  - **every offset and threshold of hers is in HER space**, converted with ONE constant metre's
    units (`units_per_m = VR_UNITS_PER_METER`, 12.5 - the value a 1.6 m calibration gives), never
    one that moves with the player's height: the shoulders' 0.20/0.075/0.18 m, the crouch dead
    zone, the body's move (her eye height over her model's 1.6 m = 0.783) and the clavicle's reach
    (a fraction of her own arm). The world's scale already maps the player onto her, so a
    height-dependent factor corrected everything twice - the logs read 12.48 at 1.60 m against
    13.53 at 1.73 m, and the taller the player the deeper the camera sat in her body. Now the
    shoulder and the eye come out identical at 1.60, 1.63 and 1.74 m (`hands-sweep-check` asserts
    it, to 0.00000 units).
  - **the calibrated reach, into her space**: `reach_m x (1.6 / the player's height) x 12.5`, so a
    taller player's longer reach lands at her arm too (a 1.74 m player's 0.86 m is 0.79 m of hers).
    The shoulder offset (|up, back, side| = 0.279 m, in her metres) comes off it, her arm is
    `arm1 + arm2` (her own, `bodyScale` 1), and the ratio is clamped to **0.90..1.25** - it comes
    out ~1.2 for a real reach, never pinned. Before a reach is measured the eye height sizes it
    (0.47 of it - a measured reach is about half). All the numbers are logged once:
    `fiona_arm_len`, `player_arm_len`, `reach_scale`, `units_per_m`, `body move`.
  - **the hand is the size the IK has always drawn it**, whatever the body's and the arm's
    scales are: at the wrist the inherited scale is cancelled (its local scale is 1 over what it
    inherited), so the wrist's world scale is the hand's own `hand_scale` (`HAND_METRES` x
    `HAND_SCALE`), and the palm's move under it is a similarity carrying that same size - the
    fingers keep their own bone-local frames and simply come out at it, as does a held item in
    that wrist frame. Logged once: `hands: body_scale ... arm_scale ... hand world scale ...`.
  - **the reach is never moved**: the target is the controller and an arm of her length can only
    bend or go straight, so the distance is clamped to `|l1 - l2| .. l1 + l2` (a millimetre's
    slack). At full stretch the arm is **dead straight** with the hand at l1 + l2 - at most a
    wrist gap short of the controller when the target is further than her arm reaches - and at the
    other end it folds; the old flexion window (145/10 degrees) **never touches the reach**, which
    is what used to pull her hand off the controller with the hands by her sides.
  - **the body's move** = her rest eye height over the player's, in the room's units (0.5..1.15):
    a head move is applied to her body in *her* proportions, so a taller player's crouch is the
    fraction of Fiona's height it really is, and standing tall is her standing straight.
- **The vertical is not a crouch by itself**: a rise is never followed (her legs are straight at her
  full height; following it would stretch them), and a drop smaller than `BODY_DROP_DEAD` (4 cm)
  is a relaxed stance, not a duck. Past it the whole drop moves her, in her sizes, less the part
  her crouch animation is scrubbing.
- **The clavicle** gives a far hand 0.15 L of reach (a downward offset counts 0.4, glTF is Y up),
  turned about the clavicle's origin by the smallest rotation.
- **FRIK's shoulders** (`vrbody_shoulders`, before the clavicle and the arm): her arm's root should
  be where the *player's* is, not where her drawn chest leaves it - **0.20 m below the eyes,
  0.075 m behind them, 0.18 m out to each side** (`SH_UP_M`, `SH_BACK_M`, `SH_SIDE_M`), in the
  player's metres (`units_per_m`) along her **body's yaw** (a look up or down never moves a
  shoulder). The offset is built in the **room** - the anchor's own space, +Y up, the same space
  and up the camera anchor uses - and brought into her model's space through **`unplace`**, the
  same conversion `vrbody_clavicle` makes; mixing the two once threw a shoulder metres from her
  body and dragged the arm into the floor. It is then **reached by turning the clavicle, never by
  moving or stretching it**: the shoulder lands on the clavicle's own arc with its length kept, and
  the extra give comes from `vrbody_clavicle`'s turn toward a far hand (`CLAV_REACH`, ~0.08 m).
  A **0.15 m clamp** (`SH_MAX_M`) holds the placed shoulder that close to her own posed one,
  warning once a side if the target is further. Logged once a side: the eye, the shoulder asked
  for, hers as posed, and the distance between them in metres. The torso is not pushed forward
  into the camera: the hips' target goes *back* from the neck (`BODY_HEAD_FRONT`, 3 kBody behind
  it) and a look's aim offsets go back and down. The **calibration measures the reach from that
  same point**: |up, back, side| = 0.279 m comes off the measured head-to-controller reach, so the
  two agree (a 1.73 m player's 0.86 m lands at an arm share of ~1.0).
- **The elbow** (FRIK's limit window): a wrist's twist from the hand's back and thumb-side axes;
  behind/crossed/lifted limits (they steer the elbow's *direction*, never the reach - a straight
  arm is the full `l1 + l2`, not a 145 degree cap); the pole as
  straight down turned about the body's sideways (`ELBOW_BEND_SIGN`, mirrored per side); the
  elbow's direction turned about up to come forward across the body and behind the head
  (`ELBOW_SWING_SIGN`); the elbow at the law of cosines, 27 -> 28 a pure hinge. A headset had the
  elbows folding **inward** across her chest with `ELBOW_BEND_SIGN` at +1: it is **-1** now (down
  leans out over the hand), and on top of it a floor of `ELBOW_OUTWARD` (0.2) of her own side is
  forced into the bend's direction, because the swing's sideways part turns over as the wrist's
  twist sweeps - that floor is what guarantees down-and-out in every pose, including the hands
  overhead, where the forward cut takes the direction down and forward. Both signs are logged once
  (`vrbody: signs - elbow bend ...`). A target more than 2.25 arms away is a tracker's glitch: no
  solve.
- **The forearm's roll** (what the hand did against a rigid follow of the elbow) is spread up the
  whole arm, FRIK's `armUpperTwistSplit` idea, and every joint's share is clamped. The hand's turn
  is read *unwrapped* (2·atan2, not folded into ±180 first), unwrapped again against last frame's,
  and clamped to ±170 degrees: a wrist turned past 180 used to fold to -170 and snap the forearm the
  other way. Then, as absolute turns about the forearm's axis, monotonic: `vr X upperarm twist` and
  the elbow bone take a quarter (`TWIST_UPPER`, clamped 45), bone 29 and `vr X forearm twist 1` a
  half (clamped 85), `vr X forearm twist 2` four fifths (clamped 140, `VR_TWIST_FRIK_CUFF` makes it
  all of it, clamped too), the hand all of it. The upper arm's and elbow's shares are *rolls of the
  bone* (about its own length, which moves nothing the solve placed), and the forearm's bones are
  rebuilt from the elbow's rolled follow with the rest, so a 180 degree wrist is
  spread over the arm instead of candy-wrapping one joint. At 170 degrees the steps along the arm
  are 42/42/51/34 degrees with `VR_TWIST_FRIK_CUFF 0` and 42/42/85/0 with 1: the same total with
  half of it across the cuff alone when it is 1, so 0.8 is kept.
- **The upper arm**: `vr X upperarm twist` takes back 0.5 of its roll, and `vr X shoulder` (the
  deltoid) turns halfway between the clavicle's rest and the arm.
- **The arms' bones are written last**, whichever solve made them (the VR body's elbow or the
  fallback's): `pose_arm` used to write them only in the fallback's block, which left a solved arm
  at the animation's pose - the shoulders by her sides while the hand reached the controller. The
  hand itself is the grip matrix exactly, its descendants carried with it.
- **The first-person head** (VRIK's shrink): `vr neck fp` shrunk to 0.3 on its own origin, and bone
  33 with every descendant (the eyes, the hair, a mod's bones) collapsed into the neck's top, with
  the near plane coming in to 0.8 of itself as she looks down 30..60 degrees. A skinned normal is
  made unit at the end (`vec3_norm` leaves a zero one zero): no NaN. The older whole-mesh collapse
  (`actor_hide_head`, main.c) is the **same option**: with "Body: Hide Head in First Person" off,
  neither runs and her head is there.
- **Order**: body's forward, body and posture (feet re-planted), the shoulder's placement, clavicle,
  arm, forearm's roll,
  upper arm and deltoid, head.

## Design notes

- **One owner per thing.** The body's pose is vrbody's alone. The crouch's posture is
  `scripts/story/crouch.fs`'s (its animation, its deadzone, its hysteresis) - vrbody adds none of
  it, and takes its own head-follow out of the part the animation is scrubbing. The leg IK that
  plants her on the floor is `humanlegs.c`'s, run before vrbody; vrbody re-solves only to keep the
  feet where that left them. The view's crouch anchor (`vr_crouch_anchor`) and the lean clamp
  (`vr_body_view`) are the camera's, not the body's. The first-person sample mod
  (`examples/mods/first-person`) moves only its own camera.
- The clipped-away head is the same skinned mesh the shadow and mirror passes draw (`actor_prepare`
  poses once), so they lose her head too while the option is on. A flag that skips the head for
  those passes needs a second pose; left as a TODO in `vrbody.c`.
- The helpers' own places are never moved: only their turns change, so the skin they share with
  their parents stays closed.
- `vrbody_idle` (a cutscene, a menu, non-VR) clears the smoothings, so the next posed frame starts
  afresh instead of easing from where the body was before the jump. `vrbody_forget_place` does the
  same without taking the IK off when her place jumps under it - a room change (the room-change
  branch of `vrhands_tick`, and `vrhands_pose`'s own room check for a drawn frame between two
  ticks), a teleport (`vrhands_drop_all`) or a snap turn (`vrhands_forget_stops`): a hand's
  smoothed frame, the hip's offset and the spine's swing are places in the room she left, and
  easing from them stretched her arms toward it for the first frames of the new one.

## Status

Built and checked: `vrbody-check` (the mod's model: the names, the ratios, the off case, the feet,
the helpers' seats, the collapsed head, the near plane, the arm's solves at the calibration's scale
(a target past the arm goes dead straight at `l1 + l2` with the elbow on the line, a target at the
shoulder folds to `|l1 - l2|`, and her drawn body takes the player's height while her actor's
scale stays 1),
a look down that must not crouch her **and leaves the legs dead straight**, a duck below the deadzone
 with the knees forward and none of it to the side, a lean, an elbow that sits down and outward
wrist turned 120..240 degrees through the seam - the arm's roll keeps its way and the twist is a
quarter at the elbow, a half at the forearm and four fifths at the cuff, and a room change's hand
smoothing - the next pose takes the controller whole instead of easing an arm out of the room she
left) and `hands-sweep-check`'s
posed-arm section (the shoulder turns, the elbow leaves the animation and is a forearm from the
hand; a bug that left the arms unposed fails it). Not yet on a headset: the sign conventions of the
elbow's two vertical turns (`ELBOW_BEND_SIGN`, `ELBOW_SWING_SIGN`) are the first thing to look at if
an elbow points the wrong way. What the calibration prints, once a change, to the console:
`vrbody: calibration - eyes 1.70 m, reach 0.66 m (measured); arm scale 1.36 (...); body move 0.63
(...)`, and `vr: body calibrated - eyes ... reach ...` when the measurement lands.
