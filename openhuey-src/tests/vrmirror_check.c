/* The curtained mirrors a hand draws (src/game/vrmirror.c) with no room behind them: a morph of
 * two frames in a placed set, the grab's handle, a fire once per crossing, a let-go springing back
 * to the end its flag has, and a hiding spot's curtain never registered.
 *
 *   vrmirror-check */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/mathx.h"
#include "data/roommesh.h"
#include "game/engine.h"
#include "game/placed.h"
#include "game/vrmirror.h"

/* (the engine the module reads: the room's placed set, its Forth - none here) */
Engine gEngine;

/* (the renderer placed.c calls and this check never does) */
void render_mesh_upload(GpuMesh *g, const MeshVertex *v, int n) {
    (void)g;
    (void)v;
    (void)n;
}

void render_mesh_update(GpuMesh *g, const MeshVertex *v, int n) {
    (void)g;
    (void)v;
    (void)n;
}

void render_mesh_free(GpuMesh *g) { (void)g; }

void render_mesh(const GpuMesh *g, const Mat4 *mvp, const MeshDraw *d, int nd, const GpuTexture *textures,
                 int ntextures, const uint32_t groups[8]) {
    (void)g;
    (void)mvp;
    (void)d;
    (void)nd;
    (void)textures;
    (void)ntextures;
    (void)groups;
}

void render_mesh_alpha_solid(float a) { (void)a; }

/* (the hiding spots, read-only: a room with a spot marker, and the spot/part lookup) */
static int sSpotsHere, sSpotPart;

int vrmarkers_spots_here(int room_id, int *out, int max) {
    (void)room_id;
    if (!sSpotsHere || out == NULL || max < 1) {
        return 0;
    }
    out[0] = 0;
    return 1;
}

int vrspot_placed_part(int placed) {
    (void)placed;
    return sSpotPart;
}

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("vr mirror: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) { return fabsf(a - b) < 0.001f; }

static void wr32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }

static void wr16(uint8_t *p, int16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* the morph's words, as the places' `curtain` writes them (a 0, b 1, t c) */
static void set_t(Placed *p, float t) {
    uint32_t bits = 0;

    memcpy(&bits, &t, 4);
    p->words[0x74 / 4] = 0;
    p->words[0x78 / 4] = 1;
    p->words[0x7C / 4] = bits;
}

/* the ticks a let-go curtain needs to reach its end (about 0.3 s), with a margin */
static void tick(int n) {
    int i;

    for (i = 0; i < n; i++) {
        vrmirror_tick(&gEngine);
    }
}

int main(void) {
    uint8_t def[0x200];
    Placed *p;
    float d = 0.0f;

    memset(&gEngine, 0, sizeof(gEngine));
    memset(def, 0, sizeof(def));
    memcpy(def, "Curtain", 7);
    /* (three vertices: the first travels 3 model units as c goes 0..1, the others stand) */
    wr32(def + 0x40, 3);       /* (three vertices) */
    wr32(def + 0x4C, 0x100);   /* (their UVs) */
    wr32(def + 0x70, 0x110);
    wr32(def + 0x74, 0x120);   /* (frame 0's s16 positions) */
    wr32(def + 0x78, 0x130);
    wr32(def + 0x7C, 0x140);   /* (frame 1's) */
    wr16(def + 0x120, 4096);   /* (v0: 1, 0, 0) */
    wr16(def + 0x122, 0);
    wr16(def + 0x124, 0);
    wr16(def + 0x126, 0);      /* (v1: 0, 1, 0) */
    wr16(def + 0x128, 4096);
    wr16(def + 0x12A, 0);
    wr16(def + 0x12C, 0);      /* (v2: 0, 2, 0) */
    wr16(def + 0x12E, 8192);
    wr16(def + 0x130, 0);
    wr16(def + 0x140, 16384);  /* (v0 at frame 1: 4, 0, 0) */
    wr16(def + 0x142, 0);
    wr16(def + 0x144, 0);
    wr16(def + 0x146, 0);      /* (v1 stands) */
    wr16(def + 0x148, 4096);
    wr16(def + 0x14A, 0);
    wr16(def + 0x14C, 0);      /* (v2 stands) */
    wr16(def + 0x14E, 8192);
    wr16(def + 0x150, 0);

    gEngine.room.id = 1;
    gEngine.room.placed.n = 1;
    p = &gEngine.room.placed.p[0];
    memset(p, 0, sizeof(*p));
    memcpy(p->name, "Curtain", 8);
    p->shown = 1;
    p->kind = 1;
    p->scale = 32.0f;
    p->pos = p->def_pos = vec3(10.0f, 0.0f, 0.0f);
    p->def = def;
    p->def_end = def + sizeof(def);
    set_t(p, 1.0f);   /* (drawn across: the flag covered) */

    CHECK("the room registers its curtain", vrmirror_register(1, "Curtain", NULL, NULL, NULL) == 1);
    CHECK("its closure is the t the room left (1, covered)", near(vrmirror_closure(0), 1.0f));
    CHECK("it is a mirror's curtain (never carried as a prop)", vrmirror_placed(0) == 1);

    /* the handle: v0, the one that travels (model 1..4 -> world 42..138; the curtain 0..64 high) */
    CHECK("the grab finds the handle at its drawn edge",
          vrmirror_grab_edge(vec3(139.0f, 5.0f, 1.0f), 4.0f, &d) == 0 && d > 1.0f && d < 2.0f);
    CHECK("over the curtain's height there is nothing to take",
          vrmirror_grab_edge(vec3(139.0f, 200.0f, 1.0f), 4.0f, &d) < 0);

    /* a fire once per crossing: covered -> bare at 20%, bare -> covered at 80% */
    CHECK("30% drawn back is not a crossing", vrmirror_set(0, 0.30f) == 0 && vrmirror_fire_take(0) == 0);
    CHECK("20% drawn back bares the mirror once", vrmirror_set(0, 0.20f) == 1 && vrmirror_fire_take(0) == 1);
    CHECK("and not again while it is bare", vrmirror_set(0, 0.10f) == 0 && vrmirror_fire_take(0) == 0);
    CHECK("half drawn across is not a crossing", vrmirror_set(0, 0.50f) == 0 && vrmirror_fire_take(0) == 0);
    CHECK("80% drawn across covers it once", vrmirror_set(0, 0.80f) == 2 && vrmirror_fire_take(0) == 2);
    CHECK("and not again while it is covered", vrmirror_set(0, 0.95f) == 0 && vrmirror_fire_take(0) == 0);

    /* a let-go between the thresholds springs to the flag's end: covered -> 1 */
    vrmirror_let_go(0);
    tick(12);
    CHECK("a let-go from 95% comes to 1 (covered)", near(vrmirror_closure(0), 1.0f));

    /* ... and bare -> 0 */
    (void)vrmirror_set(0, 0.15f);   /* (a crossing: bare) */
    (void)vrmirror_fire_take(0);
    (void)vrmirror_set(0, 0.45f);   /* (between the thresholds) */
    vrmirror_let_go(0);
    tick(12);
    CHECK("a let-go from 45% comes to 0 (bare)", near(vrmirror_closure(0), 0.0f));

    /* the hard rule: a curtain a hiding spot owns is never registered */
    sSpotsHere = 1;
    sSpotPart = 1;
    CHECK("a hiding spot's curtain is refused", vrmirror_register(2, "Curtain", NULL, NULL, NULL) == 0);
    CHECK("... and nothing of it was kept", vrmirror_closure(0) == 0.0f && vrmirror_placed(0) == 0);

    printf("vr mirror: %d failed\n", fails);
    return fails != 0;
}

