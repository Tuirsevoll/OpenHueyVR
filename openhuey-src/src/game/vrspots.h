/* The hiding spots a player walks into (the marker file PROPS/hidespots.txt, and a place's own
 * `vr-hiding-spot`): the boxes a spot's hand and body are in, one closure that poses every part of
 * it - a wardrobe's two bifold panels together, or the gallery curtain's morph - and the step a
 * player may take in and out.
 *
 * A spot comes from the mod's markers (vrmarkers.c): its GRAB box (where a hand takes it) and its
 * BOUND box (where she is hidden). Its parts are the placed objects that fold, slide or morph with
 * it; each is a placed animation's track (the frames its two flat ends give) or a morph (kind 1 / 3,
 * its two frames and blend). One closure c, 0 open .. 1 closed, poses all of them.
 *
 * A place may still type its own box with `vr-hiding-spot` (`vrspot_add`); when a marker spot is
 * there, the script's leaves are that spot's parts and its typed box is ignored. Without a marker
 * (old-mansion-1f-22) the typed box is the spot, as it was.
 *
 * While Fiona is in or at a spot her player-driven step is clamped to its footprint: the sides and
 * back always, the doorway only while it is open enough (VRSPOT_ENTER_MAX_CLOSURE). She may stand on
 * the room's solid nav there (the scripts turn noclip on for her), so no room needs new floor flags.
 * Scripted moves, stalkers and room doors never go through this. */
#ifndef VRSPOTS_H
#define VRSPOTS_H

#include "../core/mathx.h"

struct Engine;

#define VRSPOT_MAX 8
#define VRSPOT_LEAVES 4
#define VRSPOT_PARTS 4
#define VRSPOT_PATH_N 33

/* how near the spot's BOUND she may stand on the room's solid floor (world units) */
#define VRSPOT_NEAR 4.0f
/* the hip point above her feet, and how far she is kept from the footprint while it is shut */
#define VRSPOT_HIP 8.0f
#define VRSPOT_BODY_R 1.0f
/* how far into the GRAB box (grown) and how near a part's handle a hand takes the spot */
#define VRSPOT_GRAB_MARGIN 1.0f
#define VRSPOT_EDGE_R 3.0f
/* the hand's scrub: how far from the current closure its target may ask in one tick */
#define VRSPOT_WINDOW 0.25f
/* the doorway: a step across the BOUND's footprint edge is allowed only while c is this open */
#define VRSPOT_ENTER_MAX_CLOSURE 0.40f

void vrspot_reset(void);
/* let the spot go (a discovery has taken her out): no state, no noclip, no clamp until released
 * off again. The door's alpha returns to opaque. */
void vrspot_release(int on);
/* the place Fiona is now, from the scripts' own her-at and her feet (C's Actor.pos is taken at a
 * draw, not a headless tick): once a frame, before the spot's state is read */
void vrspot_here(Vec3 p);
/* register (or find again) a spot: two leaf names (the second may be ""), the doorway's origin
 * (ox, oz) and yaw in degrees, its half width and depth, and a point inside it. Returns its id,
 * -1 none. With a marker spot there, the leaves are its parts and the typed box is ignored; without
 * one, the typed box is the spot and the leaves are marked `hiding_leaf` (the hands' fallback). */
int vrspot_add(struct Engine *e, const char *leaf_a, const char *leaf_b, float ox, float oz, float yaw,
               float half_width, float depth, float inside_x, float inside_z);
/* once a tick, before the hands read it: the markers' spots, every part posed from its closure, and
 * the spot she is in: inside, full, half, enclosed, the door's alpha */
void vrspot_tick(struct Engine *e);
int vrspot_active(void);
int vrspot_inside(void);
int vrspot_full(void);
int vrspot_half(void);
int vrspot_enclosed(void);
int vrspot_noclip(void);   /* in or at the spot: her step may cross the room's solid floor */
float vrspot_closure(void);   /* 0..1, the spot's one closure */
/* the player's step (dx, dz), clamped to the active spot (unchanged when there is none or she is
 * nowhere near it) */
Vec3 vrspot_step(struct Engine *e, Vec3 at, Vec3 step);
/* the same from a given place, for a test or a probe: the spot there is asked, not the active one */
Vec3 vrspot_step_at(struct Engine *e, Vec3 at, Vec3 step);
/* the same for Fiona's own step (dx, dz), from where she is now */
Vec3 vrspot_step_here(struct Engine *e, float dx, float dz);
int vrspot_id_at(struct Engine *e);   /* the spot Fiona is in or nearest, -1 none */

/* ---- the hands' scrub of a marker spot (vrhands.c) ---- */
/* 1 when the spot is a marker spot with parts and no script is playing them */
int vrspot_scrubbable(int id);
/* 1 when that placed object is a part of a marker spot (the hands leave it to the spot) */
int vrspot_placed_part(int placed);
/* the spot whose GRAB box holds `at`, grown by `margin`: the nearest, -1 none, its distance */
int vrspot_grab_box(Vec3 at, float margin, float *dist);
/* the spot with a part handle within `radius` of `at`: the nearest (its distance and that part), a
 * curtain's or an open door's edge wherever it is */
int vrspot_grab_edge(Vec3 at, float radius, float *dist, int *part);
/* take it: the part whose handle is nearest the hand, and the offset from that handle to it (so
 * nothing jumps to the hand). 0 when the spot cannot be taken (script-owned, no handles) */
int vrspot_take(int id, Vec3 at, int *part, Vec3 *offset);
/* the closure the hand's target asks for: the handle polyline projected about `c_now`, within
 * `window`, clamped to 0..1 */
float vrspot_scrub(int id, int part, Vec3 target, float c_now, float window);
/* pose every part from c (0..1), the spot's one closure */
void vrspot_set_closure(int id, float c);
/* a hand let go of it: keep that speed (c a tick) to the end it was heading for */
void vrspot_let_go(int id, float speed);
/* 1 while a script owns the spot's parts: a hand takes nothing, and lets go */
int vrspot_script_owned(int id);
float vrspot_closure_of(int id);   /* 0..1 */
const char *vrspot_name(int id);
/* the spot's debug lines (the misses, the first fade): on unless turned off */
void vrspot_debug(int on);
int vrspot_debug_on(void);

#endif
