/* Physical collectibles. A sparkle (or a vase's sparkle, once the vase breaks) gets the item's
 * mesh. Grip holds it; letting go runs the room's pickup. The action button is unchanged. */
#ifndef LOOT_H
#define LOOT_H

#include "../core/mathx.h"

struct Forth;

void loot_reset(void);
/* the place's effect k at this point (the sparkle's). No second copy if one is already there. */
void loot_sparkle(int fx, float x, float y, float z);
/* the same item, at a point the sparkle moved to (a key falling off a ladder). */
void loot_at(int item, float x, float y, float z);
void loot_move(int item, float x, float y, float z);
/* the garden key's own part, and the bisque doll's placed model. */
void loot_part(int item, int fact, int fx, int part);
void loot_doll(int item, int fact, int fx);
/* the room's pickup word, remembered on the prop just made or the one she is standing at. */
void loot_script(void *xt);
void loot_offer(struct Forth *f, void *xt);

int loot_spark_pop(int *fx);
int loot_waiting(void);
int loot_take(int *item, int *fact, int *fx, int *mode, void **xt);
void loot_idle(void);
int loot_fact(void); /* the prop the script just ran for; -2 already settled, -1 none */
void loot_consume(void);
/* back on its sparkle. 1: light the sparkle again (fx, and x y z). */
int loot_keep(float *x, float *y, float *z, int *fx);
void loot_got(int item);
/* room parts that are picked up with the action button (no sparkle of their own). */
void loot_maintain(void);
/* an equipped item, in the hand: its model, or the thrown sprite when that is its art. */
void loot_draw_held(int item, const Mat4 *world, const Mat4 *view_proj);

int loot_nearest(Vec3 hand, int skip, float *d2);
void loot_grab(int index, int side);
void loot_follow(int index, Vec3 at, Vec3 right, Vec3 up, Vec3 fwd);
void loot_let_go(int index, int side);
int loot_claims_placed(int placed_index);
void loot_draw(const Mat4 *view_proj, int layer);

#endif
