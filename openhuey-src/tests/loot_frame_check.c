/* A thing a hand holds is drawn from the hand's own frame, taken at the draw (src/game/loot.c,
 * loot_draw_from_hands; src/game/vrhands.c, vrhands_hand_frame) - not from where the tick left it -
 * so with her walking and the hand still it sits in the hand on every drawn frame instead of
 * stepping at the 30 ticks. Checked here with a hand of its own: the drawn frame is the hand's, the
 * game's own frame is left as the tick gave it, a hand not tracked keeps the tick's frame, a thing
 * no hand has is drawn where the world has it, and a hand that moves draws the thing with it.
 *
 *   loot-frame-check */
#include <stdio.h>
#include <string.h>

#include "game/engine.h"
#include "game/itemprops.h"
#include "game/loot.h"
#include "game/placed.h"
#include "game/progress.h"
#include "game/props.h"
#include "game/room.h"
#include "game/things.h"
#include "hactor/hactor.h"
#include "platform/vr.h"
#include "render/render.h"
#include "core/files.h"

/* ---- the little of the game loot.c reads when nothing is picked up ---- */
Engine gEngine;
Progress gProgress;
VrOptions gVrOptions;   /* (the debug axes are off here) */

int vr_active(void) { return 1; }

static int sLightsAt;

void engine_lights_at(Vec3 at) {
    (void)at;
    sLightsAt = 1;
}

/* the hand, as the check puts it: `sHandOn` 0 no hand is tracked */
static int sHandOn = 1;
static Vec3 sHandAt = {0.0f, 0.0f, 0.0f};
static Vec3 sHandRight = {1.0f, 0.0f, 0.0f};
static Vec3 sHandUp = {0.0f, 1.0f, 0.0f};
static Vec3 sHandFwd = {0.0f, 0.0f, 1.0f};

int vrhands_hand_frame(int side, Vec3 *at, Vec3 *right, Vec3 *up, Vec3 *fwd) {
    (void)side;
    if (!sHandOn) {
        return 0;
    }
    *at = sHandAt;
    *right = sHandRight;
    *up = sHandUp;
    *fwd = sHandFwd;
    return 1;
}

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

void render_mesh_free(GpuMesh *m) {
    (void)m;
}

void render_texture_free(GpuTexture t) {
    (void)t;
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
    int item;
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

    /* an item of her own, at the origin: a hand takes it there */
    loot_at(3, 0.0f, 0.0f, 0.0f);
    {
        float d2 = 0.0f;

        item = loot_nearest(vec3(0.0f, 0.0f, 0.0f), -1, &d2);
    }
    loot_grab(item, 1);
    loot_follow(item, vec3(0.0f, 0.0f, 0.0f), right, up, fwd);
    CHECK("a hand has the item", loot_held(item, NULL) == 1);

    /* a draw: the thing is drawn from the hand's frame, wherever that hand is now */
    sHandAt = vec3(0.0f, 0.0f, 10.0f);
    loot_draw_from_hands();
    loot_drawn_frame(item, &pos, &r, &u, &f);
    CHECK("the drawn frame is the hand's", pos.z > 9.999f && pos.z < 10.001f);
    CHECK("with the hand's axes", r.z > -0.001f && r.z < 0.001f && f.z > 0.999f);

    /* a hand that moves draws the thing with it, on the same frame */
    sHandAt = vec3(4.0f, 2.0f, 30.0f);
    loot_draw_from_hands();
    loot_drawn_frame(item, &pos, &r, &u, &f);
    CHECK("a hand moving draws its thing with it", pos.z > 29.999f && pos.z < 30.001f && pos.x > 3.999f &&
                                                 pos.y > 1.999f);

    /* a hand not tracked (a menu, a scene, no headset): the tick's frame, not a stale hand's */
    sHandOn = 0;
    loot_draw_from_hands();
    loot_drawn_frame(item, &pos, &r, &u, &f);
    CHECK("an untracked hand keeps the tick's frame", pos.z > -0.001f && pos.z < 0.001f);

    /* ... and the game's own frame was never moved by any of those draws: a tick's frame given
     * after them is what a draw with no hand tracked shows */
    sHandOn = 1;
    loot_follow(item, vec3(1.0f, 1.0f, 1.0f), right, up, fwd);   /* (the tick: where the game has it now) */
    sHandOn = 0;
    loot_draw_from_hands();
    loot_drawn_frame(item, &pos, &r, &u, &f);
    CHECK("a draw leaves the game's own frame as the tick gave it", pos.z > 0.999f && pos.z < 1.001f);
    sHandOn = 1;

    /* let go: no hand has it, so it is drawn where the world has it */
    loot_let_go(item, 1);
    sHandAt = vec3(0.0f, 0.0f, 99.0f);
    loot_draw_from_hands();
    loot_drawn_frame(item, &pos, &r, &u, &f);
    CHECK("a thing no hand has is drawn where the world has it", pos.z > 0.999f && pos.z < 1.001f);

    /* a held item's own model (the sample mod's shared PROPS/antimony.gltf): loaded through the
     * files layer when the hand takes it, and drawn from the wrist bone on the next draw */
    {
        Actor *f = &gEngine.actors[0];
        Mat4 vp = mat4_identity();
        float d2 = 0.0f;
        int slot;

        files_layer_add(ITEMPROPS_SAMPLE_DIR);
        itemprops_reset();   /* (the earlier item's grab looked for a grips.txt before this layer) */
        memset(f, 0, sizeof(*f));
        f->used = 1;
        snprintf(f->name, sizeof(f->name), "O_FIN/FIN_000");
        f->model.nbones = 47;
        f->bones[30] = vec3(0.0f, 0.0f, 0.0f);
        f->bone_axes[30][0] = vec3(1.0f, 0.0f, 0.0f);
        f->bone_axes[30][1] = vec3(0.0f, 1.0f, 0.0f);
        f->bone_axes[30][2] = vec3(0.0f, 0.0f, 1.0f);
        f->bones[46] = vec3(0.0f, 0.0f, 0.0f);
        f->bone_axes[46][0] = vec3(1.0f, 0.0f, 0.0f);
        f->bone_axes[46][1] = vec3(0.0f, 1.0f, 0.0f);
        f->bone_axes[46][2] = vec3(0.0f, 0.0f, 1.0f);
        loot_at(148, 0.0f, 0.0f, 0.0f);
        CHECK("an item at rest preloads its model", loot_item_prop_loaded(148, 1));
        sLightsAt = 0;
        loot_draw(&vp, 1);
        CHECK("an item at rest draws its model", sLightsAt == 1);

        item = loot_nearest(vec3(0.0f, 0.0f, 0.0f), -1, &d2);
        loot_grab(item, 1);
        sLightsAt = 0;
        loot_draw(&vp, 1);
        CHECK("a held item's own model is loaded and drawn", loot_item_prop_loaded(148, 1) && sLightsAt == 1);

        /* the mod's own ground prop, loaded into the shared pool and freed as item-cleared does:
         * the held model must not share its storage */
        {
            int shared = props_load("PROPS/item.gltf");

            CHECK("the mod's ground prop loads in the shared pool", shared >= 0);
            if (shared >= 0) {
                props_free(shared);
            }
            sLightsAt = 0;
            loot_draw(&vp, 1);
            CHECK("the held model survives the ground prop's free",
                  loot_item_prop_loaded(148, 1) && sLightsAt == 1);
        }

        sLightsAt = 0;
        loot_draw_held(145, 1, &vp, &vp);   /* (the equipped antimony path) */
        CHECK("an equipped item's own model is drawn", loot_item_prop_loaded(145, 1) && sLightsAt == 1);

        slot = loot_item_prop_slot(148, 1);
        CHECK("two items naming one model share its cache slot",
              slot >= 0 && slot == loot_item_prop_slot(145, 1));

        sLightsAt = 0;
        loot_draw_held(148, 0, &vp, &vp);   /* (the other hand: the right hand's line mirrored) */
        CHECK("the mirrored hand uses the same model file",
              loot_item_prop_loaded(148, 0) && loot_item_prop_slot(148, 0) == slot && sLightsAt == 1);

        {
            int i, shared = 0;

            for (i = 0; i < MAX_PROPS; i++) {
                Prop *p = props_get(i);

                if (p != NULL && (strcmp(p->path, "PROPS/antimony.gltf") == 0 ||
                                  strcmp(p->path, "PROPS/item-148.gltf") == 0 ||
                                  strcmp(p->path, "PROPS/item-145.gltf") == 0)) {
                    shared = 1;
                }
            }
            CHECK("held item models are not in the shared props pool", shared == 0);
        }
    }

    /* grab -> let go -> the pickup's queue -> inventory, with the held model loaded; then the
     * same item again: the cache must survive and the release must not leave a dangling prop */
    {
        int got_item = -1, got_fact = -1, got_fx = -1, got_mode = -1;
        void *got_xt = NULL;
        Mat4 vp = mat4_identity();
        float d2 = 0.0f;
        int before = loot_item_prop_slot(148, 1);

        loot_let_go(item, 1);
        CHECK("a released modelled item is queued", loot_waiting() == 1);
        CHECK("the pickup queue has it", loot_take(&got_item, &got_fact, &got_fx, &got_mode, &got_xt) == 1);
        CHECK("a console-spawned item has no story fact", got_fact == -1);
        loot_consume();   /* (as takes-up: given, then loot-got retires it) */
        loot_idle();
        CHECK("it is gone after the pickup", loot_nearest(vec3(0.0f, 0.0f, 0.0f), -1, &d2) < 0);

        loot_at(148, 0.0f, 0.0f, 0.0f);
        item = loot_nearest(vec3(0.0f, 0.0f, 0.0f), -1, &d2);
        loot_grab(item, 1);
        sLightsAt = 0;
        loot_draw(&vp, 1);
        CHECK("the same item again draws its model", loot_item_prop_loaded(148, 1) && sLightsAt == 1);
        CHECK("the model cache slot is the same", loot_item_prop_slot(148, 1) == before);

        /* the equipped/defensive path, released and drawn again */
        loot_let_go(item, 1);
        (void)loot_waiting();
        if (loot_take(&got_item, &got_fact, &got_fx, &got_mode, &got_xt)) {
            loot_consume();
        }
        loot_idle();
        loot_at(145, 0.0f, 0.0f, 0.0f);
        item = loot_nearest(vec3(0.0f, 0.0f, 0.0f), -1, &d2);
        loot_grab(item, 1);
        sLightsAt = 0;
        loot_draw(&vp, 1);
        loot_draw_held(145, 1, &vp, &vp);
        CHECK("the equipped item still draws after a release", loot_item_prop_loaded(145, 1) && sLightsAt == 1);
    }

    printf("loot frame: %d failed\n", fails);
    return fails != 0;
}
