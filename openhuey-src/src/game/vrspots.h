/* The hiding spots a player walks into (scripts/story/vr-hiding.fs): a small local box at a
 * wardrobe/cabinet's doorway, its doors' closure, and the step a player may take inside it.
 *
 * The box is given by the place (origin on the doorway, yaw along the doorway, half width, depth
 * behind it, and a point clearly inside it). The spot's door leaves are ordinary placed objects
 * marked `hiding_leaf`; a hand turns them (vrhands.c). The closure is the smallest of the leaves'
 * own turn, from the flat open and shut ends of their first track.
 *
 * While Fiona is in or at the box her player-driven step is clamped to it: the sides and back
 * always, the doorway only where a leaf is 100% shut. She may stand on the room's solid nav there
 * (the scripts turn noclip on for her), so no room needs new floor flags. Scripted moves, stalkers
 * and room doors never go through this.
 */
#ifndef VRSPOTS_H
#define VRSPOTS_H

#include "../core/mathx.h"

struct Engine;

#define VRSPOT_MAX 4
#define VRSPOT_LEAVES 4

void vrspot_reset(void);
/* let the spot go (a discovery has taken her out): no state, no noclip, no clamp until released
 * off again. The door's alpha returns to opaque. */
void vrspot_release(int on);
/* the place Fiona is now, from the scripts' own her-at (C's Actor.pos is taken at a draw, not a
 * headless tick): once a frame, before the spot's state is read */
void vrspot_here(Vec3 p);
/* register (or find again) a spot: two leaf names (the second may be ""), the doorway's origin
 * (ox, oz) and yaw in degrees, its half width and depth, and a point inside it. Returns its id,
 * -1 none. The leaves are marked `hiding_leaf`. */
int vrspot_add(struct Engine *e, const char *leaf_a, const char *leaf_b, float ox, float oz, float yaw,
               float half_width, float depth, float inside_x, float inside_z);
/* once a tick, after everything moved: inside, closure, the full/half/enclosed state, and the
 * see-through alpha on the active spot's leaves */
void vrspot_tick(struct Engine *e);
int vrspot_active(void);
int vrspot_inside(void);
int vrspot_full(void);
int vrspot_half(void);
int vrspot_enclosed(void);
int vrspot_noclip(void);   /* in or at the spot: her step may cross the room's solid floor */
float vrspot_closure(void);   /* 0..1, the least closed leaf */
/* the player's step (dx, dz), clamped to the active spot (unchanged when there is none or she is
 * nowhere near it) */
Vec3 vrspot_step(struct Engine *e, Vec3 at, Vec3 step);
/* the same from a given place, for a test or a probe: the spot there is asked, not the active one */
Vec3 vrspot_step_at(struct Engine *e, Vec3 at, Vec3 step);
/* the same for Fiona's own step (dx, dz), from where she is now */
Vec3 vrspot_step_here(struct Engine *e, float dx, float dz);
int vrspot_id_at(struct Engine *e);   /* the spot Fiona is in or nearest, -1 none */

#endif