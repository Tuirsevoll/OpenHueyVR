# Modding OpenHuey

How to make a mod for OpenHuey (the Haunting Ground engine in this folder): what a mod can replace,
how its scripts plug into the game, and how to test one. Written so that someone new to the
project - or an AI assistant starting a fresh session - can pick it up from here. The design and
the reasons behind it are in `docs/library-and-mods.md`; the Forth language is `docs/forth.md`;
the actor kernel is `docs/actors.md`.

## In one minute

A mod is a folder (or a `.zip` of one) in `mods/` beside OpenHuey:

```
mods/my-mod/
  mod.fs                 the manifest: its name, version, what it needs
  library/data/...       files that replace the game's (same paths as the library's)
  scripts/my-mod.fs      its Forth: an actor of its own, a hook, words
```

Samples to copy, in `examples/mods/` (in a release: `mod-examples/`):

| Sample | Shows |
|---|---|
| `room-tally` | an actor of its own: hears the game's broadcasts, keeps its own state, draws a line on screen, answers a message |
| `flower-scent` | a hook: changes what using an item does, keeping the game's rule for everything else |
| `free-play-entry` | a hook: adds an entry to the title menu |
| `first-person` | the mods' camera: the game through her eyes |
| `item-props` | props and the items lying about: each item a model where it lies, not a sparkle |

Try one without installing it: `openhuey --mod examples/mods/room-tally`. Install one by copying its
folder into `mods/`; the title's MODS menu then turns it on or off and orders it.

## The manifest: mod.fs

OpenHuey reads it (it isn't run): a word per line, `\` and `( )` comments.

```forth
mod: my-mod              \ its name: letters, digits, - ; also its scripts' vocabulary
version: 1 0             \ major, minor
requires: openhuey 1 3        \ the mod API it was written for (below)
requires: other-mod 1 0  \ another mod it needs, at least this version
after: other-mod         \ load after that one, if it's on
```

- **The mod API** (`requires: openhuey M m`): the game's messages, broadcasts and hooks a mod uses.
  OpenHuey has 1.5 (`mod-api` at the console: 105). A mod needs the same major and a minor no newer.
  1.1 added `item-use-rule`, 1.2 `title-menu-hook`, 1.3 the mods' camera, 1.4 a model's bones,
  1.5 props and the items lying about.
- A mod is **turned off** (and the reason shown in the MODS menu and on stderr) when: a
  `requires:` isn't met; its name is one of the game's vocabularies (`fiona`, `items` ...); its
  scripts fail to load - an error in them, or code run as they load that never ends (stopped after
  50 million steps: "ran too long"). Nothing else stops the game.
- **Order**: `mods/order.txt` (`on: name` / `off: name` lines, written by the MODS menu). Mods it
  doesn't name are on, after those it names, by name. `after:` moves a mod below another. A later
  mod's files win over an earlier one's; hooks chain in load order.
- **A zip** works the same: `mods/my-mod.zip` with `mod.fs` at its top or inside one folder.
- **No sandbox**: a mod's scripts can do anything the game's can. OpenHuey says so when it loads one.

## Replacing the game's files (the files layer)

Every asset the engine reads goes through `files_read` (`src/core/files.c`), which looks in each
mod's `library/` first (the last mod in the order first), then in the library. So a mod replaces
a file by having one at the **same path**, under `library/data/` for the game's data (or
`library/` for the few files beside it, e.g. `library/exe/`). Folder names keep the game's case
(`SYSTEM`, not `system`); a file's own name may be any case (in a zip, case never matters).

The library holds the game's files converted to common formats (an import of the player's disc;
`docs/library-and-mods.md`). What a mod can replace, and in what form:

| What | Path (under `library/data/`) | Format |
|---|---|---|
| a texture | `<folder>/<NAME>.texbank/<i>.png` (texture i of bank NAME.TEX); a room's: `ST_xxx/ST_yyy.room/textures.texbank/<i>.png` (effects: `effects.texbank/`) | PNG, RGBA, any size up to 8192 a side: a bigger one (an HD pack: 2x, 4x) is drawn whole over the same surface. A 16-colour texture's other palettes are `<i>-<k>.png`, mapped in the bank's `bank.txt` (`src/data/texbank.h`) |
| a music track | `ADX00/AD_01.flac` (for `ADX00/AD_01.ADX`) | FLAC, 16-bit; loop with Vorbis comments `LOOPSTART` / `LOOPEND` (samples), or none: it loops end to start (`src/data/musicsrc.h`) |
| a sound effect | `<bank>.samples/<i>.wav` (sample i of the bank `<bank>.BD`; `bank.txt` lists them) | WAV, mono, 16-bit PCM; a loop as a `smpl` chunk; its pitch is the game's (the HD's), not the file's rate (`src/data/samples.h`) |
| text | `SUBSCR/MSG_BASE.txt`, `MSG_SUB.txt`, `MSG_END.txt`; a room's `ST_xxx/ST_yyy.room/messages.txt` | UTF-8 text: `#id` then the message's lines; the game's control codes as `{page}`, `{colour 5}` ...; buttons as `{cross}`, `{triangle}` ... (the full list: `src/data/msgtext.h`). A translation is these files |
| a movie | `X.mpg` + `X.flac` (for `X.SFD`) | an MPEG-1 / 2 program stream (video only) and its sound (none: silent) (`src/data/sfd.h`) |
| a room's mesh | `ST_xxx/ST_yyy.room/mesh.gltf` + `mesh.bin` | glTF 2.0 as OpenHuey writes it: the engine's draw words in `extras` "hga" (`src/data/roomgltf.h`) |
| a room's lights, cameras | `ST_xxx/ST_yyy.room/lights.txt`, `cameras.txt` | text, a record a line (`src/data/roomtext.h`) |
| a character model | `O_XXX/XXX_000.model/model.gltf` + `model.bin` | glTF 2.0, skinned, as OpenHuey writes it (`src/data/modelgltf.h`); the motions (`motions.bin`) stay the game's. Bones of its own may be added ("A model's bones" below) |
| one of the executable's tables | `library/exe/<name>.bin` (not under data/) | the table's bytes (`src/data/exetables.c` names them); one shorter than the game's table isn't used (stderr says so: the executable's is) |

**An HD texture pack made for PCSX2** (a `replacements/` folder of `.dds` / `.png` named by
PCSX2's hashes) becomes a mod with `tools/hdpack.py`: it matches the pack's pictures to the
library's textures by what they show, tile by tile (a PCSX2 picture is the PS2's memory at the
texture's place, so it can hold other textures in a room's unused parts), and writes the mod with
a report of what it matched. It needs Pillow and numpy:

```sh
python3 -m venv venv && venv/bin/pip install pillow numpy
venv/bin/python -I tools/hdpack.py <library> <pack>/SLUS-21075/replacements mods/hd-textures
```

`--max N` sets the largest a texture is written (default 2048 a side: 8x a room's 256).

The best start for any of these: import a disc (`openhuey --import disc.iso`), open the file in the
library, copy it into the mod at the same path, change it. For glTF, keep what Blender or another
tool round-trips: the `extras` and the accessor layout OpenHuey wrote (each accessor's data packed,
not interleaved); a mesh must be valid glTF (cgltf's checks: every accessor within its buffer) and
still one OpenHuey reads (it says so on stderr if not).

What can't be replaced yet: the rooms' other data (navigation, events, doors, placed objects,
effects, obstacles: still inside each room's `.PAC`), the motion banks, the sound banks'
programs (`.HD` / `.SDT`).

## Scripts: an actor of your own

`scripts/<name>.fs` is loaded after the game's scripts, as the vocabulary `<name>`. Its folder is
also a root for `USING:`, so a bigger mod splits into `scripts/<name>/part.fs`
(`USING: <name>.part ;`).

```forth
\ scripts/my-mod.fs
IN: my-mod
USING: engine actors messages mods ;     \ the game's vocabularies it uses (see below)

state: my-state                        \ its own fields (an actor's state)
  cell field rooms-seen
end-state

behaviour my-behaviour
  on spawned ( -- )  self subscribe entered-room ;
  on entered-room ( room exit -- )  2drop  1 rooms-seen +! ;
end-behaviour

my-behaviour my-state s" my-mod" spawn constant me   \ spawned as the scripts load
```

The rules (they are the game's own: `docs/design.md`, `docs/actors.md`):

- **Everything is actors passing messages.** A mod's actor hears the game's broadcasts
  (`subscribe`) and sends the game's messages (`send`). It never writes another actor's state.
  To ask an actor something, send it a message and handle its answer (`room-tally` asks
  nothing; its test asks it, see below).
- **Messages are declared once** (`message name ( args -- )`), the stack comment is the
  declaration: `scripts/messages.fs` holds the game's, each with what it means. A mod declares its
  own the same way in its own file.
- **A handler takes exactly its message's arguments** off the stack and leaves nothing: its stack comment must say
  so, and its code is checked against it as it loads (a warning); the kernel reports one that doesn't anyway.
- **Every word's stack comment is checked** against its code as it loads (ClankForth's "Stack effects"): the game
  loads with no warnings, and a mod should too.
- **Facts** (the engine's C words: `room-id`, `body-pos`, the progress' flags `story-flag?`,
  `state-flag?` ...) may be read by anyone; they are `engine`, `facts`, `game-state`.
- **A sequence over time is a task** (`spawn` a word; `yield`, `n wait`) - or a handler that waits (`await` a
  message, `n wait`): its actor busy meanwhile, its messages kept for after (external/clankforth/docs/actors.md).

### The game's vocabularies a mod usually uses

| Vocabulary | Has |
|---|---|
| `engine` | the C words: rooms, actors' bodies, input (`key:`, `key-pressed?`), drawing (`ui-text`, `ui-rect`, `ui-clear`), sounds, files |
| `actors` | the kernel: `message`, `behaviour` / `on` / `end-behaviour`, `state:` / `field` / `end-state`, `spawn`, `send`, `broadcast`, `subscribe`, `self`, `sender`, `kill`, `actor-named`, `enter` / `leave-actor` |
| `messages` | every message of the game's (`scripts/messages.fs`), and the actors by name (`fiona-id`, `items-id` ...) |
| `facts` | the progress' flags: `story-flag?`, `state-flag?`, `resident-flag?` |
| `flag-names`, `story-names`, `room-names` | names for flags, story flags, rooms (`front-garden-3` ...) |
| `keys` | the pad's buttons on the keyboard: `cross button-pressed?` ...; `stick@` |
| `mods` | what the game gives mods: `mod-layer` (the UI layer for mods) |
| `items`, `items.names` | the inventory's words and the items' names (`fresh-flowers` ...) |
| `game` | the actors' ids (`game:fiona`, `game:hewie`, `game:items` ...), `free-play` |
| `opening` | the title: `title-menu-hook`, `title-entry`, `title-gone` |

`vocab-words <name>` at the console lists a vocabulary; `.actors` lists the actors.

### Broadcasts worth hearing (subscribe to them)

| Message | Stack | When |
|---|---|---|
| `tick` | `--` | every game tick (30 a second), while play runs |
| `frame-end` | `--` | after each frame's messages: draw here |
| `held-tick` | `--` | instead of `tick` while the world is held (a menu up) |
| `entered-room` | `room exit --` | a room is in play (`leaving-room`, `arrived`: before it) |
| `danger` | `level --` | calm, followed, chased |
| `panic` | `stage level --` | Fiona's panic, each frame |
| `fiona-doing` | `mode sub cond cmd doing --` | what Fiona does, each frame |
| `hewie-doing` | `here action mode sub cond mood group commandable near reachable breakable way trust --` | Hewie, each frame |
| `stalker-doing` | `room mode sub cond hp hp-max kind alert came --` | the stalker in play, each frame |
| `noise-made` | `loud room door --` | Fiona's loudest noise this frame |
| `game-loaded` | `--` | a save was loaded: rebuild anything kept from the progress |

### Messages worth sending

| Message | To | Stack | Does |
|---|---|---|---|
| `go-to-room` | `s" rooms" actor-named` | `room exit --` | play that room (exit -1: no exit) |
| `give-item` / `take-item` | `game:items` | `id n --` / `id --` | into / out of the inventory |
| `remedy` | `game:fiona` | `fear recovery --` | her fear (0..100) and recovery changed by these |
| `fright` | the panic (`s" panic" actor-named`) | `amount --` | a fright (a float as a cell: `f>cell`) |
| `costume` | `game:fiona` | `n --` | her costume (0 her clothes, 1 the slip) |
| `do-action` | `game:hewie` | `action arg --` | Hewie's action |
| `stalker-order` | the stalker | `mode --` | 0 after her, 2 to her room, 3 searching |

All of them: `scripts/messages.fs`. Before sending one, read its handler (`grep -rn "on <name>"
scripts/`) to see what it really does.

### Drawing

UI layers are drawn in order over the picture; a mod draws on `mod-layer` (from `mods`): clear
it and draw each `frame-end` (as `room-tally` does). `ui-text ( layer addr len x y rgba scale -- )`
(the game's own font; bytes from `$C0` are the pad's buttons: `$C0` circle, `$C1` triangle,
`$C2` square, `$C3` cross, `$C4` L1, `$C6` R1), `ui-rect ( layer x y w h rgba -- )`,
`ui-blend ( layer mode fix -- )` (the layer's last text or picture blended as the PS2 does with a
fixed alpha, 128 = 1: mode 0 by its own alpha - the default -, 1 subtracted, 2 mixed, 3 added),
`screen-size ( -- w h )`, `char-size ( -- w h )` at the pen's scale (`pen-scale ( n -- )`).

### The mods' camera (API 1.3)

A mod can take the camera: `mod-camera ( -- addr )` is its own camera (set its fields each tick:
`cam.x cam.y cam.z` the eye, `cam.yaw` - 0 looks along -z, positive turns right, as the
characters' yaw - `cam.pitch` positive up, `cam.fov` the vertical view in radians, `cam.near`),
`mod-camera! ( on -- )` gives it the view (from the camera as it is) or gives it back, and
`mod-camera? ( -- flag )` says whether it has it now: a cutscene's or an event's shot keeps the
game's camera and the mod's has it again after. Fiona walks by the camera, so she goes where the
mod's camera looks. `mouse-capture ( on -- )` holds the mouse in the window, its motion all look
(`mouse-dx`, `mouse-dy`: pixels this tick). The sample: `examples/mods/first-person` (through her
eyes - `body-head` her head, `body-yaw` her facing - the mouse or the arrows looking; F4 off).

### A model's bones (API 1.4)

**Adding bones** (finger bones on Fiona's hands, say): open the library's `model.gltf` in Blender,
add the bones under the game's, weight the mesh to them, export glTF with **Custom Properties** on
(the `extras` carry the game's bones' exact rest and the model's bone table). The rules:

- The game's bones stay as they are: named `bone 0`, `bone 1` ... with their extras. Their order in
  the file may change (Blender reorders them) - OpenHuey finds them by name, as the motions name
  them. Renaming, removing or moving one breaks the motions: the model isn't read (stderr says so).
- New bones may have any other names. They go after the game's (`bone-count` counts both), each
  hanging from the joint above it, at rest as the file has it. 256 bones at most.
- No motion moves a new bone: it keeps its rest under its parent (a finger bone goes with the hand),
  so the game's motions and scenes look as they did. A mod's script moves it (below).
- The mesh may be weighted anew (JOINTS_0 as bytes or shorts, WEIGHTS_0 as floats or normalised
  bytes / shorts; positions, normals and UVs floats - Blender's defaults); its vertices stay the same in
  number and order (the draws' words in their extras say where each goes).

**Moving bones** from a script, any model's - the game's bones or added ones. `id` is the model:
a character's is `cast-model ( char -- model | -1 )` - `0 cast-model` Fiona's, `1` Hewie's (the
characters' numbers as the story's scripts have them):

| Word | Stack | What |
|---|---|---|
| `bone-count` | `( id -- n )` | its bones |
| `bone-named` | `( id addr len -- bone )` | the bone of that name (`s" bone 12"`, a mod's own), -1 none |
| `bone-parent` | `( id bone -- parent )` | the bone it hangs from, -1 a root (or no such bone) |
| `bone-rest` | `( id bone -- ) ( F: -- rx ry rz )` | its own turn at rest: radians about x, then y, then z, as the motions turn it |
| `bone-set` | `( id bone -- ) ( F: rx ry rz -- )` | its own turn from now on, in place of the motions' (its children with it) - in scenes too - till it is freed; up to 64 bones a body |
| `bone-free` | `( id bone -- )` | back to the motions' turn |
| `bones-free` | `( id -- )` | all of them back |
| `bone-pos` | `( id bone -- ) ( F: -- x y z )` | where it is in the room, as last drawn |
| `bone-point` | `( id bone -- ) ( F: ox oy oz -- x y z )` | a point in its own axes, in the room |

A setting holds until freed or the model is loaded again (a costume change: `cast-model` gives
the new one - find its bones again by name). Set them each tick for a hand that follows something (a VR controller).

### VR bone roles (`vr_bones.txt`)

Fiona's outfits number their game bones differently: cloth and skirt bones come before the body,
so `bone 30` is her left wrist in `O_FIN/FIN_000` but a shoulder pad in the slip. The VR arm IK and
body read **`library/data/<model>.model/vr_bones.txt`** (through the files layer, so a mod's copy
wins). The fiona-vr-rig mod writes one beside every outfit:

```
# comment lines start with '#'
model = O_FIS/FIS_000
game_bones = 68
bones = 107
floor_y = 0.0
root = bone 0   [bone 0]
pelvis = bone 1   [bone 1]
L thigh = bone 2   [bone 2]
waist = bone 43   [bone 23]
chest = bone 44   [bone 24]
L clavicle = bone 46   [bone 26]
L upperarm = bone 47   [bone 27]
L elbow = bone 48   [bone 28]
L forearm = bone 49   [bone 29]
L wrist = bone 50   [bone 30]
neck = bone 52   [bone 32]
head = bone 53   [bone 33]
L eye = bone 54   [bone 34]
R eye = bone 55   [bone 35]
R clavicle = bone 62   [bone 42]
R upperarm = bone 63   [bone 43]
R elbow = bone 64   [bone 44]
R forearm = bone 65   [bone 45]
R wrist = bone 66   [bone 46]
```

- Each line is `<role> = <bone name>`, with FIN_000's number in `[...]` for reference (ignored).
  Roles may contain spaces. Unknown keys and roles (`bust`, the shoulder pads, `model`,
  `game_bones`, `bones`) are ignored. A role that is missing where the VR code needs it disables
  the table.
- `floor_y` is the model-space floor height. In `O_FIS` and `O_FIC` `bone 0` binds at the hip
  (10.75), not at the floor, so the eye height is measured from `floor_y`, not from bone 0.
- The loader resolves each bone by name and checks the skeleton: the wrist's chain through
  forearm, elbow, upper arm and clavicle, and (when the mod's finger bones are there) that
  `arm left finger 1a` / `arm right finger 1a` hang from the named wrists. On a mismatch, or with
  no file and a model that is not FIN_000's exact layout, the arm IK and the VR body are skipped
  for that model (her own animation plays, nothing is twisted); the grip poses still run, because
  they find the finger bones by name. One `vrbones: ... (ok)` line is logged per model.
- From Forth (the checks and the console): `vr-bone ( id addr len -- bone )` gives the bone a role
  names (`s" L wrist" vr-bone`), `vr-bones-ok?`, `vr-bones-from-file?` and `vr-bones-floor` read
  the rest of the table.

### Props (API 1.5)

A **prop** is a mod's own model: any glTF 2.0 (`.gltf` with its `.bin` and images beside it or as
`data:` URIs - Blender's "glTF Separate" or "glTF Embedded" - or a `.glb`), read as authored. It
goes in the mod's `library/data/` (say `library/data/PROPS/tube.gltf`), loaded by that path; its
files beside it are read through the files layer too (a zipped mod's as well, nothing outside the
mod's folders). The model:

- **Y up, one unit a game unit** (Fiona stands 15 tall; the floor's height differs room to room -
  `body-pos` says where she stands, `item-placed` where an item lies). Its nodes' transforms are applied; skins and animations aren't read
  (move the whole prop from a script).
- **Its look**: each triangle primitive a draw - its material's base colour texture (a PNG) and
  factor, its vertex colours; `OPAQUE`, `MASK` (the texture's alpha cuts it) or `BLEND` as the
  material says. Lit by the room's lights as the characters are (their nearest three about its
  middle), normals as the file has them (made from the triangles if it has none).
- 64 props at most at once; they go with a new game (the mod's scripts load them again). A held
  item's own model is outside this pool ("Held items" below).

| Word | Stack | What |
|---|---|---|
| `prop-load` | `( addr len -- id )` | the prop at that path (`s" PROPS/tube.gltf"`), -1 if it can't be read (stderr says why) - not shown yet |
| `prop-place` | `( id room -- ) ( F: x y z -- )` | where it is, in that room (-1: whichever is played - one in her hand), and shown |
| `prop-turn` | `( id -- ) ( F: rx ry rz -- )` | turned about x, then y, then z (radians), as the room's own objects |
| `prop-scale` | `( id -- ) ( F: s -- )` | its size (1 as authored) |
| `prop-show` | `( id on -- )` | shown or not |
| `prop-at` | `( id -- ) ( F: -- x y z )` | where it is placed |
| `prop-bounds` | `( id -- ) ( F: -- x0 y0 z0 x1 y1 z1 )` | its bounds as authored (not turned or scaled) |
| `prop-free` | `( id -- )` | gone |
| `data-file?` | `( addr len -- flag )` | a file of the data as the files layer has it (a mod's over the library) |

### Items lying about (API 1.5)

Every item lying in a room - shown by a sparkle until she picks it up (a Medallion, an Antimony
Tube, one Hewie sniffed out, one from a broken vase) - is told to mods by the room as it shows
and as it goes, so a mod can show it its own way (a prop of it where it lies) and take it for her
(a VR hand picking it up). The rooms' own rules are kept: what she may take, and when, is theirs.

| Message | From / to | Stack | When |
|---|---|---|---|
| `item-placed` | broadcast by the room | `room k item x y z --` | item `item` lies at x y z (floats as cells: `cell>f`), the room's sparkle k |
| `item-cleared` | broadcast by the room | `room k --` | ... no more: taken (by her or for her), put away, or the room left |
| `take-item-at` | to the room (the sender of `item-placed`) | `k --` | taken for her: the room's own doing, as her action button by it would (she turns to it and picks it up, the item given, its fact, "You've obtained ...") |
| `take-answer` | back to the sender | `k started? --` | whether her taking it started: not where there's none (or one only Hewie fetches), not while she can't (held, a message up, the room being left, panicking); `item-cleared` tells it went |

`item-sparkles! ( on -- )` (`mods`): the game's sparkles drawn or not (told either way); `item-sparkles?`.
A room's item ids are `items.names`' (`medallion`, `antimony-tube` ...). The sample,
`examples/mods/item-props`, shows each one as a turning model - `PROPS/item-<id>.gltf` if the mod
has one, else its gold cube - instead of its sparkle (`openhuey --mod examples/mods/item-props`).

### Held items (API 1.5, `PROPS/grips.txt`)

In a headset, a hand that has taken an item draws the model its grip names, or its own
`library/data/PROPS/item-<id>.gltf` when it names none (the id decimal: `medallion` is `$70`, 112;
`antimony-powder` is `$91`, 145). Without either file the item keeps the game's own card / key /
powder / ball, as before. Models are cached by file, not by item: several items can name one file
and give it different tints. The cache is the held-item cache (its own, not the mod props' 64-slot
pool); a file is loaded once, the first time a hand takes an item that names it. A mod's
`prop-free` (or `item-cleared`) never frees these models: taking, letting go and taking an item
again keeps the same loaded model.

Its place comes only from `library/data/PROPS/grips.txt`. An item may have up to two lines, one
for each hand:

```
item  bone  ox oy oz  qx qy qz qw  [grip-pose-name]  [model=<file>]  [r g b]
148   46    1 0.5 0   0 0 0 1      SampleGrip        model=antimony.gltf  1 0.75 0.75
145   46    0 0 0     0 0 0 1                        model=antimony.gltf  0.55 0.55 0.55
145   30    0 0.2 0   0 0.70710678 0 0.70710678      model=antimony.gltf  0.3 0.3 0.3
```

- `item`: the item's id, decimal, 0 .. 191.
- `bone`: 30 (her left hand, +x) or 46 (her right, -x).
- The hand holding the item uses its own line when there is one. With only the other hand's line,
  that line is mirrored across her body: the offset's x is negated and the quaternion becomes
  `(x, -y, -z, w)`. A second line for the same item and the other bone overrides the mirror. This
  is the same for a thing her hand takes and the equipped magnesia / antimony.
- `ox oy oz`: the model's origin from the wrist, in the wrist bone's own axes, game units (Fiona is
  about 15 tall, Y up).
- `qx qy qz qw`: the model's turn about that origin, a unit quaternion (x y z w).
- `grip-pose-name`: the pose `PROPS/grip_poses.txt` gives this hand's finger bones (API 1.6,
  "Finger grip poses" below). Without that file, or with a name it does not have, the hand keeps
  its animation/trigger shape as before.
- `model=<file>`: a model in `PROPS/` (for example `model=antimony.gltf`), shared by every item
  that names it. A name already starting with `PROPS/` is used as it is. Without this the item's
  own `PROPS/item-<id>.gltf` is used.
- `r g b`: multiplies the model's colours (1 1 1: as authored), so one model can serve recolour
  variants.
- The optional words may be in any order. A comment starts with `#`, `;` or `//`. A line with too
  few or too many words is skipped. A second line for the same item and the same hand is ignored
  (the first is kept).
- An item with a model but no line sits at the wrist as it is: the model's origin at the wrist, no
  offset and no turn.

### Finger grip poses (API 1.6, `PROPS/grip_poses.txt`)

A mod can also shape the fingers around the item. `library/data/PROPS/grip_poses.txt` is read
through the same files layer as `grips.txt`:

```
# pose <name>, then one line a finger bone: <bone name> qx qy qz qw [tx ty tz]
pose grip_orb
arm right finger 2a -0.487758 0.029052 0.000203 0.872496 -0.000669 -0.011448 0.031618
arm right finger 2b -0.064588 -0.305268 0.049872 0.948764
...
```

- `pose <name>` starts a pose; the name matches the optional word in `grips.txt` (up to 31
  characters). A following line is `<bone name> qx qy qz qw [tx ty tz]`: the numbers are read from
  the end, and everything before them is the bone name (so names with spaces work). Four numbers
  are the old format, with `t = (0, 0, 0)`. Comments start `#`; blank lines and malformed lines
  are ignored.
- `q` is the bone's pose rotation **relative to its rest, in the bone's own axes**. `t` is the
  optional bone-local move in model units, in the same rest axes (`t` is omitted when it is ~0):
  `local_posed = local_rest * T(t) * R(q)`, exactly Blender's pose basis order. Adding `t` to the
  parent-space rest translation is wrong; it must be turned by the rest rotation first. Bones a
  pose does not list keep the hand's normal shape.
- The left hand uses the matching `arm left finger ...` bone with the mirrored turn
  `(qx, -qy, -qz, qw)` and move `(-tx, ty, tz)` - the same mirror as `grips.txt`. Model X is
  left-right; the finger bones' local X is the odd axis under that reflection. If a pose lists
  `arm left ...` lines itself, those are used for the left hand as they are.
- A hand blends into the pose over 0.10 s when the item starts being held or shown, and back out
  over 0.10 s when it lets go. Switching straight from one item to another blends between the two
  poses, rotation and move together. The hand morph (fist/point) eases to its rest shape while a
  pose is in, because the poses were authored on the rest hand.
- The game writes only bones named `... finger ...`; its own bones 0-48, the wrist, palm and the
  `vr ...` helpers are never touched. A missing pose name logs once and leaves the hand as it was.
  The built-in check runs once on Fiona's VR model at load and logs `self-check PASS` with the
  right finger 2a / 3a / 5a / 2c positions; `[grip-poses]` lines on stderr report what is applied,
  including the loaded pose and knuckle-offset counts.
- Poses apply only in VR gameplay, after the arms and body are posed - never to a cutscene, a
  canned animation, a menu or the flat screen.

The model keeps its authored size and shading and is drawn in the room's 3D pass with the things
lying about. The `vr.item-prop-debug` option (VR Options, VR Enhancements, off by default) draws
small RGB axes at the model's origin and the wrist bone while a held item has one.

An item **lying about** draws the same model too, before the game's fallback card / key / powder /
ball / doll / magnesia: the right hand's grip line (so its `model=` and tint) at the item's spot,
upright and with its floor side on the spot. The fallback is used only when no model file exists.
The engine logs one line per item the first time it draws its resting model - the item, the spot's
y and the model's drawn y range - so a bad up/down sign can be seen (`room up is -y`).

A typical VR pickup: on `item-placed` load and place the item's prop (remember k and the room's
id: the sender); as the hand grabs it, carry the prop (`-1` room: it follows her through doors);
when it's "put away", send `take-item-at` - on `item-cleared` free the prop; on a refused
`take-answer`, put it back where it lay.

## Hooks: when the game must ask

Where a mod needs to change what the game decides (not just react), the game offers a hook: a
deferred word with the game's own behaviour as its default. A mod keeps what it replaces, sets
its own, and calls the kept one for everything it doesn't handle - so several mods setting the
same hook chain in load order:

```forth
action-of item-use-rule constant game-rule         \ what was there (the game's, or an earlier mod's)
: my-rule ( place -- result )
    dup game-rule execute  dup no-use <> if  nip exit  then  drop   \ the game's answer first
    ( place ) ...  ;                                               \ then the mod's
' my-rule is item-use-rule
```

| Hook | In | Since | What |
|---|---|---|---|
| `item-use-rule ( place -- result )` | `items` | 1.1 | what using the item at `place` does (`id@` its item); the result as the game's rules': `no-use`, `used-up` (spends one), `marked`, `event-started`, `done-quietly` (something happened, nothing spent) |
| `title-menu-hook ( -- )` | `opening` | 1.2 | the title's entries from mods: call the kept one, then `title-entry ( addr len xt -- )` for each; picking one runs xt in the title's actor (`title-gone` first, to leave the title) |

New hooks are added when a mod needs one (and the mod API's minor goes up). Ask for one - or add
it: a `defer` in the subsystem's file with the game's word as its default, the call site going
through it, the table above and `MODS_API_MINOR` (`src/game/mods.h`) updated, and a sample.

## Testing a mod

OpenHuey runs Forth tests headless (`--test`): a test file is a vocabulary using `tester`, with
`T{ ... -> ... }T` checks, ending in `test-summary`.

```forth
\ tests/my-mod.fs
IN: test-my-mod
USING: tester engine actors messages room-names game my-mod ;

: frames ( n -- )  0 ?do  game-tick  loop ;
2 frames
testing the mod is on and its actor there
T{ s" my-mod" mod-on?  s" my-mod" actor-named 0< -> -1 0 }T
test-summary
```

```sh
SDL_AUDIO_DRIVER=dummy openhuey --mod path/to/my-mod --test tests/my-mod.fs
```

- In `--test` runs only `--mod` folders are on (never `mods/`), or the folder `--mods DIR` names.
- A headless run starts a new game at once (no title). `free-play` jumps past the opening
  (Hewie with her, the grate open); `<room> room!` (debug) moves her to a room.
- `game-tick` runs a tick; `deliver` delivers the messages queued now; `key: X true key-hold`
  holds a key (`cross`'s is X, `triangle`'s V; `scripts/keys.fs`).
- To read an actor's own fields in a test: `game:fiona enter  her-fear f@  leave-actor` (tests
  only: an actor never does this to another).
- To see it: `openhuey --mod my-mod --hidden --frames 3 --eval ": w 300 0 do game-tick loop ;  w  s\" /tmp/shot.png\" screenshot"`
  writes the frame after 300 ticks as a PNG. Use `OPENHUEY_HOME=/some/folder` for such runs: it keeps
  OpenHuey's saves and settings there instead of beside hga.
- The repository's own tests of the samples: `tests/mods/*.fs`, run by ctest
  (`CMakeLists.txt`: `mods_room_tally`, `mods_flower_scent`, `mods_free_play_entry`,
  `mods_menu`).

## Pitfalls (each one bit someone)

- **Your own words win over the game's.** A file's own definitions shadow every vocabulary it
  uses - the core too. A mod that defines `move` breaks the core `move` (copying bytes) for the
  rest of its file. Name things after what they mean in your mod (`reorder`, `shift-mod`).
- **Two used vocabularies with the same word**: in a file, the later in `USING:` wins; `vocab:word`
  reaches the other. At the console and in `--eval` the order is kept from line to line, and a
  vocabulary already in it keeps its place - say `vocab:word` there (`first-person` has a `pitch`,
  `game` a `camera`: `first-person:pitch`, `engine:camera`). If a word does something unexpected,
  check `see word` and which vocabulary has it.
- **Control words only inside a definition.** `if`, `?do`, `do`, `case` can't run at the top level
  of a file or at `--eval`: wrap them in a `: word ... ;`.
- **Locals**: the first `{: :}` of a definition can't be inside an `if` or a loop (later ones
  can); a local can't be named like an immediate word (`to`, `if`, `loop` ...).
- **`r@` inside a `do` loop is the loop's index**, not what you `>r`'d before it. Use a local.
- **Floats are on their own stack**: `f@ f! sf@ sf!`, `fdup`; a float in a message travels as a
  cell (`f>cell`, `cell>f`).
- **A message's arity is its declaration's stack comment.** A handler must take all of them.
- **Mods load after the game's scripts**: the game's actors already exist, and a mod spawns its
  own as its file loads; the first messages arrive at the first tick. A soft reset (the game
  over's return to the title) loads every script again, mods too.
- **Never write another actor's state** (its fields): send it a message. The game's rules depend
  on each actor being the only one changing its state.
- **Folder case** in `library/`: the game's (`SYSTEM/`, `ST_000/`); file names any case.
- **Exactness**: the game's data reads back exactly in the library's formats; a mod's replacement
  needn't, but keep the shape the reader expects (a WAV mono 16-bit, a texture the same shape (bigger is fine),
  message ids in order ...). OpenHuey says on stderr what it couldn't read.

## For an assistant starting here

- **Map**: `docs/subsystems.md` (which actor does what, in what order) and
  `docs/subsystems/<name>.md` (each one's messages, rules, status). `docs/forth.md` (the
  language), `docs/actors.md` (the kernel), `docs/library-and-mods.md` (the library and mods'
  design), this file.
- **Find a word**: `grep -rn ": word\b" scripts/`; a message: `grep -n "message name" scripts/messages.fs`
  and its handlers `grep -rn "on name" scripts/`; a C word: `src/forth/bind_engine.c` (the table
  at its end lists them by name).
- **The original game** is decompiled in `../src/` (C) - the reference for what the game does; the
  scripts cite its function names (`Panic_Breath`, `Items_Use` ...).
- **Build and check**: `cmake --build build`; `cd build && SDL_AUDIO_DRIVER=dummy ctest` (the
  tests need the game's data: the library, or the developer's extracted folder). Run any
  headless OpenHuey with `SDL_AUDIO_DRIVER=dummy` and `--hidden`.
- **The mods' code**: `src/game/mods.c` (finding, ordering, loading, the menu's list),
  `src/core/files.c` (the files layer), `src/core/zip.c`, `scripts/mods.fs`,
  `scripts/mods/menu.fs`; the hooks in `scripts/items.fs` and `scripts/opening.fs`.
