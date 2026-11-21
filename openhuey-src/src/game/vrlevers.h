/* The levers a player pulls by hand in VR (PROPS/levers.txt, a mod's library data): each box names
 * a lever - a placed object near it whose track 0 it scrubs from its rest pose to its pulled pose.
 * Pulling is the hands' (vrhands.c HOLD_LEVER); the *event* stays the place script's: it asks
 * `vr-lever-pulled?` once and runs the same gate/breaker code the flat screen's prompt runs.
 *
 * The flat screen (`vr? 0=`) keeps the button prompt and the scripted pull exactly as they were. */
#ifndef VRLEVERS_H
#define VRLEVERS_H

#include "../core/mathx.h"

struct Engine;

#define VRLEVER_MAX 8
#define VRLEVER_PATH_N 33
/* how near an object's bounds must be to the lever's box to be its object (world units) */
#define VRLEVER_REACH 3.0f
/* how much the lever's handle must travel as c goes 0..1 to be a lever (world units) */
#define VRLEVER_TRAVEL 1.0f
/* the grab: the box grown by this, or the handle's place within this of the hand (world units) */
#define VRLEVER_GRAB_MARGIN 1.0f
#define VRLEVER_EDGE_R 3.0f
/* the hand's scrub: how far from the current closure its target may ask in one tick */
#define VRLEVER_WINDOW 0.25f
/* c at which the lever has been pulled home (snapped to 1, the event queued) */
#define VRLEVER_DONE 0.97f
/* a let-go lever eases back to rest over about 0.3 s (30 ticks a second) */
#define VRLEVER_SPRING_TICKS 9

/* forget the levers (a new game) */
void vrlevers_reset(void);
/* once a tick, before the hands: the current room's lever boxes and their objects on a room change,
 * the spring a let-go hand left, a script's takeover, and every lever posed from its closure */
void vrlevers_tick(struct Engine *e);
/* the spot whose box holds `at`, grown by margin: the nearest lever, -1 none, and its distance */
int vrlever_grab_box(Vec3 at, float margin, float *dist);
/* the lever whose handle is within `radius` of `at`: the nearest, -1 none, and its distance */
int vrlever_grab_edge(Vec3 at, float radius, float *dist);
/* 1 while the lever can be pulled (an object found, not done, no script playing it) */
int vrlever_scrubbable(int id);
/* take it: the offset from the lever's handle to the hand (so nothing jumps). 0: cannot be taken */
int vrlever_take(int id, Vec3 at, Vec3 *offset);
/* the closure the hand's target asks for, from c_now within window (the handle's path) */
float vrlever_scrub(int id, Vec3 target, float c_now, float window);
/* pose it from c (0 rest .. 1 pulled) */
void vrlever_set(int id, float c);
/* a hand let go before it was home: it eases back to rest over about 0.3 s */
void vrlever_let_go(int id);
/* pulled home: c = 1, done, the event queued for the place script (`vr-lever-pulled?`) */
void vrlever_complete(int id);
int vrlever_done_q(int id);
int vrlever_script_owned(int id);
float vrlever_closure(int id);
const char *vrlever_name(int id);
/* 1 when that placed object is a lever's (the hands never carry it as a prop) */
int vrlever_placed(int placed);
/* the place scripts' words: this room has that lever box and its object was found; and once, after
 * it completes, the event (reading it clears it) */
int vrlever_here_q(const char *name, int len);
int vrlever_pulled_take(const char *name, int len);
/* a console/test tool: the lever pulled home as a hand would (its event queued). 1 when it was */
int vrlever_pull(const char *name, int len);
/* the lever's debug lines (the misses): on unless turned off */
void vrlever_debug(int on);
int vrlever_debug_on(void);

#endif
