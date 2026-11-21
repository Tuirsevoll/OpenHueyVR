/* The curtained mirrors a player draws by hand in VR (old-mansion-1f-11, b1-2 and 2f-13): each room
 * registers its placed morph "Curtain" on entry with three words of its own - whether it may be
 * pulled now, what a pull open does, what a pull shut does. The hand scrubs the morph's t (its
 * closure: 0 drawn back, bare mirror .. 1 drawn across) along its handle's path; the two thresholds
 * run the room's own branches, so the flag and the glass stay the place script's and the base game's
 * smash-due (the stalker's rage) reacts to them exactly as it does to the flat screen's prompt.
 *
 * The flat screen (`vr? 0=`) keeps `the-curtain`, the walk and the pulls untouched: nothing here is
 * registered, and nothing here runs.
 *
 * A curtain that is a hiding spot's part is never registered (the hiding spots stay as they are). */
#ifndef VRMIRROR_H
#define VRMIRROR_H

#include "../core/mathx.h"

struct Engine;

#define VRMIRROR_MAX 4
#define VRMIRROR_PATH_N 33
/* the grab: how near the handle's place the hand must be, horizontally (x/z), in world units */
#define VRMIRROR_EDGE_R 4.0f
/* the hand's height is asked of the curtain's own world height, grown by this */
#define VRMIRROR_HEIGHT 2.0f
/* the hand's scrub: how far from the current closure its target may ask in one tick */
#define VRMIRROR_WINDOW 0.25f
/* drawn back: c at or below this (80% or more drawn back) - the room's open branch runs, once */
#define VRMIRROR_BARE_AT 0.20f
/* drawn across: c at or above this - the room's shut branch runs, once */
#define VRMIRROR_COVER_AT 0.80f
/* a let-go curtain eases to its end over about 0.3 s (30 ticks a second) */
#define VRMIRROR_SPRING_TICKS 9
/* how much the handle must travel as c goes 0..1 to be a curtain (world units) */
#define VRMIRROR_TRAVEL 0.1f
/* c between these never rests: a let-go springs it to its flag's end */
#define VRMIRROR_AT_END 0.01f

/* forget the mirrors (a new game) */
void vrmirror_reset(void);
/* once a tick (before the hands): a room change, a script's takeover and its end, the spring a
 * let-go hand left, and the curtain posed from its closure */
void vrmirror_tick(struct Engine *e);
/* the room registers its curtain (in came-in, vr? only): the morph's name and three execution
 * tokens - `ok? ( loud? -- why )` (0 may be pulled; 1 no time, 2 broken, 3 smashing; `loud?` asks
 * for the no-time remark), `bare ( -- )`, `cover ( -- )`. 1 when registered; 0 when it was refused
 * (no such object here, not a morph, or a hiding spot's part - logged). */
int vrmirror_register(int room, const char *name, void *xt_ok, void *xt_bare, void *xt_cover);
/* 1 while its curtain can be scrubbed (registered, shown, no script playing it) */
int vrmirror_scrubbable(int id);
/* the mirror whose curtain's handle is within `radius` of `at`, horizontally, with the hand's
 * height inside the curtain's own extent grown by 2: the nearest, -1 none, and its distance */
int vrmirror_grab_edge(Vec3 at, float radius, float *dist);
/* take it: the offset from the handle to the hand (so nothing jumps). 0: cannot be taken (and the
 * room's `ok?` said why, logged) */
int vrmirror_take(int id, Vec3 at, Vec3 *offset);
/* the closure the hand's target asks for, from c_now within window (the handle's path) */
float vrmirror_scrub(int id, Vec3 target, float c_now, float window);
/* pose it from c; 0 none, 1 the room's bare branch ran, 2 its cover branch ran (once per crossing) */
int vrmirror_set(int id, float c);
/* the fire a set made, read once (the hand buzzes on it) */
int vrmirror_fire_take(int id);
/* a hand let go: it eases to the end its flag has (0 bare, 1 covered) over about 0.3 s */
void vrmirror_let_go(int id);
/* 1 when the room's own words may be asked now (0 = blocked; logs its reason when `loud`) */
int vrmirror_ok(int id, int loud);
float vrmirror_closure(int id);
int vrmirror_script_owned(int id);
const char *vrmirror_name(int id);
/* 1 when that placed object is a mirror's curtain (the hands never carry it as a prop) */
int vrmirror_placed(int placed);
/* the mirrors' debug lines (on unless turned off) */
void vrmirror_debug(int on);
int vrmirror_debug_on(void);
/* (DEBUG) the hand's own lines: the take (the hand's place) and the release (why) */
void vrmirror_log_take(int id, Vec3 at);
void vrmirror_log_release(int id, const char *why);

#endif
