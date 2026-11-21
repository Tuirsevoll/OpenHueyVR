/* The hiding spots' geometry that has no room behind it: the handle path a hand's target is
 * projected on (src/game/vrpath.c: a straight path, an L, the window and the clamps at its ends)
 * and a track's pose at a fractional frame (src/game/placed.c, placed_pose_at: the shortest way
 * round, between the two keys either side).
 *
 *   vrspots-check */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/mathx.h"
#include "data/roommesh.h"
#include "game/placed.h"
#include "game/vrpath.h"

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

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("vr spots: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) { return fabsf(a - b) < 0.001f; }

int main(void) {
    Vec3 line[33], ell[33];
    Vec3 rot, pos;
    Placed p;
    static uint8_t keys[44 * 12];
    int i;

    for (i = 0; i < 33; i++) {
        float t = (float)i / 32.0f;

        line[i] = vec3(32.0f * t, 0.0f, 0.0f);                    /* (a straight path, 32 long) */
        ell[i] = i <= 16 ? vec3(16.0f * t, 0.0f, 0.0f)            /* (an L: +x, then +z) */
                         : vec3(16.0f, 0.0f, 16.0f * (t - 0.5f) * 2.0f);
    }

    CHECK("a straight path projects a target onto its middle", near(vrpath_project(line, 33, vec3(8, 0, 0), 0.0f, 0.25f), 0.25f));
    CHECK("the window keeps it near the hand", near(vrpath_project(line, 33, vec3(20, 0, 0), 0.0f, 0.25f), 0.25f));
    CHECK("the L's corner and its far leg", near(vrpath_project(ell, 33, vec3(16, 0, 8), 0.5f, 0.25f), 0.75f));
    CHECK("an L near the corner, from a narrow window",
          near(vrpath_project(ell, 33, vec3(16, 0, 8), 0.5f, 0.05f), 0.5625f));
    CHECK("both ends clamp", near(vrpath_project(line, 33, vec3(-9, 0, 0), 0.0f, 0.25f), 0.0f) &&
                                 near(vrpath_project(line, 33, vec3(99, 0, 0), 1.0f, 0.25f), 1.0f));

    {   /* the edge grab's distance: x/z only, any height along the door (a morph's path as well as a
         * track's) - never -1 when the hand is in front of the door's own height */
        Vec3 morph[33], track[33];
        Vec3 at;

        for (i = 0; i < 33; i++) {
            float t = (float)i / 32.0f;

            track[i] = vec3(23.0f * t, 10.0f, 0.0f);                     /* (a swing along x) */
            morph[i] = vec3(-60.438f - 5.234f * t, 12.984f, -123.686f);  /* (the curtain's own line) */
        }
        at = vec3(1.0f, 0.0f, 1.0f);   /* (a hand at chest height, 1 and 1 away in x/z) */
        CHECK("the edge grab's distance at c = 0 and c = 1, a track part",
              vrpath_edge_gap(track, 33, 0.0f, at, -14.0f, 26.0f) >= 0.0f &&
                  vrpath_edge_gap(track, 33, 1.0f, vec3(22.0f, 0.0f, 1.0f), -14.0f, 26.0f) >= 0.0f);
        CHECK("the edge grab's distance at c = 0 and c = 1, a morph part",
              fabsf(vrpath_edge_gap(morph, 33, 0.0f, vec3(-59.438f, 0.0f, -123.686f), -16.0f, 30.0f) - 1.0f) < 0.001f &&
                  fabsf(vrpath_edge_gap(morph, 33, 1.0f, vec3(-64.672f, 20.0f, -123.686f), -16.0f, 30.0f) - 1.0f) <
                      0.001f);
        CHECK("out of the door's height the edge grab gives none",
              vrpath_edge_gap(track, 33, 0.5f, vec3(11.0f, 40.0f, 0.0f), -14.0f, 26.0f) < 0.0f);
    }

    memset(&p, 0, sizeof(p));
    for (i = 0; i < 44; i++) {   /* (a track of 44 frames, its turn y = 0.1 a frame) */
        float zero = 0.0f, y = 0.1f * (float)i;

        memcpy(keys + i * 12, &zero, 4);
        memcpy(keys + i * 12 + 4, &y, 4);
        memcpy(keys + i * 12 + 8, &zero, 4);
    }
    p.keys = keys;
    p.frames = 44;
    p.format = 8;
    p.def_rot = vec3(9.0f, 9.0f, 9.0f);
    p.def_pos = vec3(1.0f, 2.0f, 3.0f);
    placed_pose_at(&p, 42.5f, &rot, &pos);
    CHECK("frame 42.5's turn lies between frames 42 and 43",
          rot.y > 4.2f && rot.y < 4.3f && near(rot.y, 4.25f));
    CHECK("a turn-only track leaves the defined place", near(pos.x, 1.0f) && near(pos.y, 2.0f) && near(pos.z, 3.0f));
    placed_pose_at(&p, 0.0f, &rot, &pos);
    CHECK("frame 0 is the track's first key", near(rot.y, 0.0f));
    placed_pose_at(&p, 43.0f, &rot, &pos);
    CHECK("its last frame is the last key", near(rot.y, 4.3f));
    {
        float minus = -3.1f, plus = 3.1f, zero = 0.0f;

        memcpy(keys, &zero, 4);   /* (the shortest way round: through pi, not through 0) */
        memcpy(keys + 4, &plus, 4);
        memcpy(keys + 8, &zero, 4);
        memcpy(keys + 12, &zero, 4);
        memcpy(keys + 16, &minus, 4);
        memcpy(keys + 20, &zero, 4);
        p.frames = 2;
        placed_pose_at(&p, 0.5f, &rot, &pos);
        CHECK("an angle wraps the shortest way round", rot.y > 3.1f && rot.y < 3.15f);
    }

    printf("vr spots: %d failed\n", fails);
    return fails != 0;
}
