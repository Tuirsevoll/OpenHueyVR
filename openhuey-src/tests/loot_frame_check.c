/* A thing in a hand, drawn between two ticks of the hand (src/game/loot.c, "a held thing's
 * frame"). Walked - her body under a hand held still, so the hand moves a tick and the eyes are
 * already drawn part way to the next - the thing is drawn between the hand's frame at the last
 * tick and its frame at the tick before, at the draw's share, rather than jumped on to the last
 * tick at once; at share 1 it is there, at 0 where it was. The game's own frame is left as the
 * hand gave it on every frame. A hand that jumped (a tracker coming back somewhere else) draws
 * its thing at once, and one put down is drawn where the world has it - its home - on the next
 * draw.
 *
 *   loot-frame-check */
#include <stdio.h>
#include <string.h>

#include "game/engine.h"
#include "game/loot.h"
#include "game/placed.h"
#include "game/progress.h"
#include "game/room.h"
#include "game/things.h"
#include "hactor/hactor.h"
#include "render/render.h"

/* ---- the little of the game loot.c reads when nothing is drawn or picked up ---- */
Engine gEngine;
Progress gProgress;

int vr_active(void) { return 1; }

/* what a thing is drawn with: none of it runs here, nothing is drawn */
unsigned render_texture(const uint8_t *rgba, int w, int h) {
    (void)rgba;
    (void)w;
    (void)h;
    return 0;
}

void render_mesh_upload(GpuMesh *m, const MeshVertex *v, int nv) {
    (void)m;
    (void)v;
    (void)nv;
}

void render_mesh_tint(float r, float g, float b, float a) {
    (void)r;
    (void)g;
    (void)b;
    (void)a;
}

void render_mesh(const GpuMesh *m, const Mat4 *mvp, const MeshDraw *d, int nd, const GpuTexture *tex, int nt,
                 const uint32_t *mask) {
    (void)m;
    (void)mvp;
    (void)d;
    (void)nd;
    (void)tex;
    (void)nt;
    (void)mask;
}

int things_model_draw(int model, int palette, const Mat4 *world, const Mat4 *view_proj) {
    (void)model;
    (void)palette;
    (void)world;
    (void)view_proj;
    return 0;
}

Placed *placed_named(PlacedSet *s, const char *name) {
    (void)s;
    (void)name;
    return NULL;
}

int room_load_prop(Room *r, int id) {
    (void)r;
    (void)id;
    return 0;
}

unsigned sprites_texture_of(int group, int id, int csa, int *w, int *h) {
    (void)group;
    (void)id;
    (void)csa;
    (void)w;
    (void)h;
    return 0;
}

HBody *hactor_body(int id) {
    (void)id;
    return NULL;
}

int hactor_kind(const char *name) {
    (void)name;
    return -1;
}

int main(void) {
    const Vec3 right = vec3(1.0f, 0.0f, 0.0f), up = vec3(0.0f, 1.0f, 0.0f), fwd = vec3(0.0f, 0.0f, 1.0f);
    Vec3 pos, r, u, f;
    int sItem;
    Mat4 gViewProj;
    int fails = 0;

#define CHECK(what, ok)                                  \
    do {                                                 \
        int _ok = (ok);                                  \
        printf("loot frame: %s -> %d (%s)\n", what, _ok, \
               _ok ? "pass" : "FAIL");                   \
        if (!_ok) {                                      \
            fails++;                                     \
        }                                                \
    } while (0)

    memset(&gEngine, 0, sizeof(gEngine));
    gEngine.room.id = 0x7;
    gViewProj = mat4_identity();

    /* an item of her own, at the origin: the hand takes it, and holds it there */
    loot_at(3, 0.0f, 0.0f, 0.0f);
    {
        float d2 = 0.0f;

        sItem = loot_nearest(vec3(0.0f, 0.0f, 0.0f), -1, &d2);
    }
    loot_grab(sItem, 1);
    loot_follow(sItem, vec3(0.0f, 0.0f, 0.0f), right, up, fwd);
    CHECK("a hand has the item", loot_held(sItem, NULL) == 1);

    /* first tick: the hand goes 10 ahead, and the thing with it */
    loot_follow(sItem, vec3(0.0f, 0.0f, 10.0f), right, up, fwd);
    loot_give_step(1, vec3(0.0f, 0.0f, 10.0f));   /* (the move just made) */
    CHECK("the game's frame is the hand's own", loot_held(sItem, NULL) == 1);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("the game's frame is where the hand is", pos.z > 9.999f && pos.z < 10.001f);

    /* a drawn frame part way: the eyes are 0.5 of the way past the tick, and a hand held still
     * moves with her: at 0.5 the thing is drawn halfway between where the hand was and is */
    loot_draw_between(0.5f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("halfway past the tick the thing is drawn halfway back", pos.z > 4.999f && pos.z < 5.001f);
    CHECK("and its axes are square", fabsf(vec3_dot(r, u)) < 1.0e-4f && fabsf(vec3_dot(r, f)) < 1.0e-4f);

    loot_draw_between(0.0f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("on the tick before it is drawn where the hand was", pos.z > -0.001f && pos.z < 0.001f);

    loot_draw_between(1.0f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("on the last tick it is drawn where the hand is", pos.z > 9.999f && pos.z < 10.001f);

    /* the frame is put back, every frame: the game's own place again */
    loot_draw_between(0.4f);
    loot_rebase(1);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("a frame leaves the game's own place", pos.z > 9.999f && pos.z < 10.001f);

    /* a second tick, the hand on another 10: the easing is between these two */
    loot_follow(sItem, vec3(0.0f, 0.0f, 20.0f), right, up, fwd);
    loot_give_step(1, vec3(0.0f, 0.0f, 10.0f));
    loot_draw_between(0.25f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("the next tick eases between the last two", pos.z > 12.499f && pos.z < 12.501f);

    /* one more tick, the hand on another 10, given as the loop gives it (the draw first, then the
     * tick's frame), so the frame just gone is the hand's own place */
    loot_draw_between(1.0f);
    loot_rebase(1);
    loot_follow(sItem, vec3(0.0f, 0.0f, 30.0f), right, up, fwd);
    loot_give_step(1, vec3(0.0f, 0.0f, 10.0f));
    loot_draw_between(0.0f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("the frame just gone leaves the hand's own place", pos.z > 19.999f && pos.z < 20.001f);

    /* a hand that jumped (further than a swing in a tick): drawn at once, no easing - a frame
     * part way past the tick is still the hand's own place, not where it came from */
    loot_draw_between(1.0f);
    loot_rebase(1);
    loot_follow(sItem, vec3(0.0f, 0.0f, 230.0f), right, up, fwd);
    loot_give_step(1, vec3(0.0f, 0.0f, 200.0f));
    loot_draw_between(0.99f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("a hand that jumped draws its thing at once", pos.z > 229.0f && pos.z < 231.0f);
    loot_draw_between(0.0f);
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("and on the tick before it, too", pos.z > 229.0f && pos.z < 231.0f);

    /* let go: the thing stays where the hand left it until the room's pickup resolves it (pending),
     * and is drawn there - no jump back to its home while that runs */
    loot_draw_between(1.0f);
    loot_rebase(1);
    loot_let_go(sItem, 1);
    CHECK("the hand has let go", loot_held(sItem, NULL) == 0);
    loot_draw(&gViewProj, 1);   /* (a drawn frame) */
    loot_drawn_frame(sItem, &pos, &r, &u, &f);
    CHECK("a thing let go is drawn where the hand left it", pos.z > 229.0f && pos.z < 231.0f);

    printf("loot frame: %d failed\n", fails);
    return fails != 0;
}
