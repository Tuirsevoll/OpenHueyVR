# 3-D audio: binaural HRTF and occlusion

## Purpose

The sound driver places every effect the way the PS2 did: a distance curve and a pan (stereo,
or the game's Pro Logic II surround tables) worked out from its point seen from the camera. That
is enough to tell left from right, and nothing else: a stalker behind her, above her or in the
next room is the same sound as one in front.

This subsystem renders a *placed* sound as a mono source at its world point, binaurally through
Steam Audio's HRTF, with the listener being the headset's own pose - so the direction (behind,
above), the height and the distance are heard, and what a wall or a shut door is between is
muffled. The game's distance curves still decide its loudness: they are the same curves.

Music (the sequencer and the streamed tracks), the menus and a cutscene's movie sound stay
stereo, untouched.

## API

`src/platform/saudio.h` (the Steam Audio side; no-ops without the SDK):

| Function | Meaning |
|---|---|
| `saudio_available()` / `saudio_start()` / `saudio_stop()` | the SDK in this build / make the context, HRTF and 48 binaural effects / drop them |
| `saudio_set_enabled(on)` | 3-D audio on / off (occlusion comes with it), and a cutscene's bypass; ramped over 100 ms |
| `saudio_on()` | it is on now (the occlusion work is skipped when it isn't) |
| `saudio_listener(world_to_ear)` | the listener: the world -> ear matrix `engine_set_ear` is given (the headset's pose over Fiona's head, or the camera flat) |
| `saudio_listener_at(at)` | where the ears are, world units |
| `saudio_source(v, spatial, world, gain, gen)` | a voice's source: its world point, its amplitude (the original left/right pair's energy), and a key-on count (a new sound snaps, it does not glide from the last one's point) |
| `saudio_source_off(v)` | not a placed sound (a menu sound, a bank going) |
| `saudio_block(frames)` | the audio thread's 4 ms block: the ramp, the sources' occlusion/low-pass, and the 16 voices the HRTF runs for |
| `saudio_voice_weight(v)` / `saudio_voice_render(...)` | this voice's binaural share (0: the original pans) and its mono samples through the HRTF |
| `saudio_occlude(v, blocked)` | 0 clear .. 1 blocked (from `sound3d.c`, once a tick) |
| `saudio_sources(...)` | the spatial voices playing, and their points (for the occlusion rays) |

`src/game/sound3d.h`:

| Function | Meaning |
|---|---|
| `sound3d_tick(e)` | once a game tick: each spatial sound's occlusion, and a wardrobe hide (`fiona-hidden`) muffling the room |
| `sound3d_blocked(nav, from, to)` | 1 if the room's line of walk meets a wall or a shut door first (or leaves the mesh: another room) - the shared "is there a wall between" test, for a stalker's line of sight too later |

## State

Gathered by `snddrv.c` (its `VoiceRec`: `world[3]`, `spatial`, `gen`), `saudio.c` (per voice:
the world point it is at and where it is going, its gain, its occlusion and low-pass, the block's
binaural share; the listener matrix; the option's ramp), and `sound3d.c` (nothing: it recomputes
each tick from the room's nav mesh and `gProgress`'s `fiona-hidden`).

## Rules

1. **Which sounds.** A sound played with a point (`sound-at`, `bank-sound-at`, `engine_sound_at`,
   a door's own creak/latch) is spatial. A sound played plainly (`bank-sound`, `common-sound`) is
   not: that is the menus' sounds, the jingle, the slot machine's. Music (SPU core 0, the stream)
   and a movie's sound (slot 0) never are. Neither is a sound within 0.75 m of the ears (the
   original's `camera-sound`, a cutscene's voice at the camera, her own voice at her head).
2. **Loudness.** The driver still computes the game's placement - the distance curves, the pan
   tables - and the voice's two volumes are turned into one amplitude (their energy,
   `sqrt(L^2 + R^2) / sqrt(2)`, full scale 1.0). Turning 3-D audio on therefore changes where a
   sound is heard from, not how loud it is. The PS2 wet (reverb) send is fed exactly as before:
   no Steam Audio reverb, no reflections.
3. **The listener** is the same world -> ear matrix the eyes are drawn with
   (`engine_set_ear`): in a headset that is the headset's pose over Fiona's head - yaw, pitch,
   lean, duck, the seated offset - not the game camera. Steam Audio's axes are the view
   matrix's (x right, y up, z behind), so a point maps with no flip. Units do not matter to a
   direction, so the world's 12.5 units a metre never enter.
4. **Smoothing.** A source's gain and occlusion glide **a sample at a time** (30 ms and 120 / 350 ms
   one-poles) - a step once a block would be a 4 ms staircase, heard as a buzz. Its point glides a
   block at a time (45 ms), which is all the HRTF can use: it takes one direction a block. The
   low-pass's state is per voice and continuous; only its coefficient is refreshed a block. A new
   sound snaps. The HRTF is interpolated bilinearly, so a head turn is a glide too. The option
   itself fades over 100 ms.
5. **The effect's frame.** Steam Audio's binaural effect processes exactly `frameSize` samples a
   call and ignores `numSamples` (its overlap-add convolution reads and writes `mFrameSize`), so
   `saudio` never hands it a partial frame: each voice's samples are gathered until 192 are there,
   and its two ears' output waits in a FIFO. That is a constant **192-sample (4 ms) latency** on
   the binaural path, one effect per voice, never reset except on a true key-on. (Passing a short
   block straight through was the first version's bug: every 1024-frame SDL buffer is five
   192-sample slices and one of 64, so a quarter of the stream was convolved with stale data and
   discarded - a loud 250 Hz buzz.)
6. **Occlusion.** `sound3d_tick` walks the game's own line of walk from the ears to each spatial
   sound over the room's nav mesh (`navmesh_wall_masked`: navmesh.c), with the flags a sound
   cannot pass - a wall (0x4000) and a shut door's passage flags (0x60000, which `Doors_RoomIn`
   moves as the door swings). A wall, a shut door, or a sound off this room's mesh (another
   room) is blocked: its gain x 0.55 and a one-pole low-pass from 12 kHz to 900 Hz. Hidden in a
   wardrobe (`fiona-hidden`, `gProgress` bit 9) blocks everything but her own body's sounds
   (within 0.5 m). The ears off the floor (a ladder) leave a sound clear, not blocked.
7. **The cap.** The HRTF runs for 16 voices at once: the loudest and nearest
   (`gain * 1000 / (100 + distance)`). A voice outside those, or within the 0.75 m of the ears,
   keeps the driver's original left / right mix for that block - so nothing is lost, only less
   precisely placed. The choice is recomputed every 4 ms block.
8. **Cutscenes.** `engine_set_ear` turns 3-D audio off while `cutscene_heard` (a scene's shot or
   a movie playing): the scene's sound is the movie's, in stereo, and the engine mutes the
   effects anyway. The 100 ms ramp keeps it from clicking.
9. **Cost and level.** One HRTF filter per spatial voice per 4 ms block. Measured (Steam Audio
   4.8.1, a desktop CPU, 48 kHz, 192-sample blocks, bilinear): **16 voices = about 10.7 ms of CPU a
   second of audio - 1.1% of one core** (2.7 us a voice a block); nearest-neighbour interpolation
   is about a third of that. The occlusion rays run on the game thread at 30 Hz.
   `HGA_AUDIO3D_STATS=1` prints the real thing every five seconds of audio.
   The HRTF's own gain peaks about 1.37x for a full-scale sound (measured), so the binaural path
   is scaled by `SAUDIO_HEADROOM` (0.8, -1.9 dB): a 3-D sound is within 1 dB of the original pan's
   loudness and the mixer's clamp is not reached by one voice.
   `HGA_AUDIO3D_SELFTEST=<file.wav>` renders six seconds of a 440 Hz sine through one voice, in
   the game's own call pattern (five 192-sample slices and a 64 a callback), and prints the largest
   step inside a call and at a call's edge - equal means no discontinuity.

## Design notes

- The 16-voice cap is what keeps the cost flat: the effects voices are 24, and the game rarely
  has more than a handful of placed sounds at once. Raising `SAUDIO_MAX_BINAURAL` (saudio.c) is a
  one-line change; the measurement scales linearly.
- `sound3d_blocked` is deliberately a *shared* test whose name does not say "sound": it is the
  same question a stalker's line of sight asks, and the nav mesh's line of walk is the engine's
  existing answer. A triangle raycast against the room mesh can replace its body without the
  audio (or the caller) changing.
- The listener matrix is the one `engine_set_ear` already had for the original pans, so the two
  can never disagree about where the ears are.
- The 192-sample FIFO is the effect's own frame, not an arbitrary buffer: Steam Audio 4.8's
  `OverlapAddConvolutionEffect::apply` ignores `numSamples` and does `memcpy(..., in[i],
  mFrameSize * sizeof(float))` both ways, so a shorter buffer is read past its end and its output
  cut in half. Whatever calls `saudio_voice_render` may pass any number up to 192; the voice's
  FIFOs make that invisible (and cost 4 ms of latency, one frame, on the binaural path only).
- The driver's `VoiceRec` keeps the world point only while the sound is placed: `voices_sweep`
  and a bank going clear it.

## Status

**Built** (`src/platform/saudio.c`, `src/game/sound3d.c`; the driver and SPU changes in
`snddrv.c` and `spu.c`; the options in `scripts/subscreen/vr.fs` and `scripts/vr-options.fs`).
The SDK is optional - `HGA_STEAMAUDIO` is only defined when CMake finds it - so every existing
test builds and runs without it. Not spatialised: music, menus, a cutscene's movie sound, and
sounds within 0.75 m of the ears (by design). The wardrobe hide's flag does not currently turn
on (a separate bug): until it does, that rule never fires.


