/* Binaural 3D audio on Steam Audio (Valve's phonon, its C API): see saudio.c and
 * docs/subsystems/audio3d.md.
 *
 * The sound driver (snddrv.c) still places every effect exactly as the game did - the distance
 * curves and the pan / Pro Logic tables - and the sound processor's voices (spu.c) still play
 * their samples. What changes is how a *placed* voice reaches the ears: instead of its two
 * volumes it is rendered as a mono source at its world point through an HRTF, so a stalker
 * behind her, above her or through a wall is heard there. The listener is the headset (the same
 * world -> ear matrix the eyes are drawn with).
 *
 * Nothing here runs without the SDK (a build with HGA_STEAMAUDIO undefined is the game as it
 * was), nor with 3D audio off (vr.h VrOptions.audio3d), nor during a cutscene (whose sound is
 * the movie's, mixed in stereo). Every function is safe to call in those cases.
 */
#ifndef SAUDIO_H
#define SAUDIO_H

#include <stdint.h>

#include "../core/mathx.h"

/* The effect's frame: Steam Audio's binaural effect processes exactly this many samples a call
 * and ignores numSamples (its overlap-add convolution reads and writes mFrameSize), so saudio
 * hands it whole frames from a per-voice FIFO -- a shorter call's samples wait for the next one,
 * and the effect's output waits for the caller. 192 samples (4 ms) of constant latency, and no
 * sample ever read twice or dropped. It is the SPU's slice (spu.c SLICE). */
#define SAUDIO_FRAME 192

/* 1: a binaural path is possible (the SDK is in this build); 0: everything below does nothing */
int saudio_available(void);
/* start the context, the HRTF and a binaural effect a voice (once; on the main thread). 0 if it
 * can't (no SDK, no memory): the game stays on its original placement */
int saudio_start(void);
void saudio_stop(void);
/* the options (vr.audio3d, vr.occlusion) and the cutscene's bypass: read live, ramped over 100 ms.
 * start() is tried the first time it is switched on. */
void saudio_set_enabled(int on, int occlusion);
int saudio_on(void);

/* the listener: the world -> ear matrix (engine_set_ear's), every frame. Its axes are Steam
 * Audio's already: +x right, +y up, +z behind the head (that is how mat4_look's view matrix is
 * built, and how Steam Audio defines a listener). */
void saudio_listener(const Mat4 *world_to_ear);
int saudio_listener_at(Vec3 *out);   /* where the ears are, in the world */

/* a voice's source, told by the driver while it holds the SPU's lock (lock order: SPU, then
 * this): `world` its point in world units, `gain` the placement's amplitude (the original
 * left/right pair's energy: the game's distance curves, untouched), `gen` bumped each time the
 * voice is keyed on (a new sound snaps to its point instead of gliding from the last one's).
 * spatial 0: not placed. */
void saudio_source(int voice, int spatial, const float world[3], float gain, uint32_t gen);
void saudio_source_off(int voice);

/* the audio thread: the block's ramp and the 16 loudest/nearest voices (call before
 * saudio_voice_weight), then, for a voice with a weight, its raw mono samples (the envelope
 * applied, no volumes) through the HRTF into interleaved stereo `out` (added). `gain` is the
 * placement's amplitude (the original left/right pair's energy): the distance curves, untouched.
 * `weight` is the share saudio_voice_weight gave for this block, so the caller's dry mix and the
 * binaural one are always the same number even if a source changes in between. */
void saudio_block(int frames);
float saudio_voice_weight(int voice);
void saudio_voice_render(int voice, const float *mono, int frames, float gain, float *out, float weight);

/* occlusion, 0 clear .. 1 blocked (sound3d.c, once a tick): muffled and attenuated, eased over
 * time here */
void saudio_occlude(int voice, float blocked);

/* the voices that are spatial now (sound3d.c casts its rays for these): how many, into `voices`;
 * `world` their points, world units */
int saudio_sources(int *voices, float (*world)[3], int max);

#endif
