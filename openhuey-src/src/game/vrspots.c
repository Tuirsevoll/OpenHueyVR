/* The hiding spots a player walks into: see vrspots.h. */
#include "vrspots.h"

#include "engine.h"
#include "placed.h"

#include "../data/navmesh.h"
#include "../platform/vr.h"

#include <math.h>
#include <string.h>

typedef struct VrSpot {
    int used;
    int leaf[VRSPOT_LEAVES], nleaves;
    /* the box: u along the doorway, v behind it (the doorway is v = 0) */
    float ox, oz, yaw, half_width, depth, floor_y, height;
    float nx, nz;                  /* the interior's normal: v's direction */
    float seg_lo[VRSPOT_LEAVES], seg_hi[VRSPOT_LEAVES];   /* each leaf's shut width, along u */
    float open_ang[VRSPOT_LEAVES], shut_ang[VRSPOT_LEAVES];
    int have_ang[VRSPOT_LEAVES];
    int was_full;
    int inside, full, half, enclosed, near;
    float closure;
} VrSpot;

static VrSpot sSpots[VRSPOT_MAX];
static int sActive = -1;
static int sRoom = -2;
static int sReleased;
static Vec3 sHere;
static int sHereHave;

void vrspot_release(int on) {
    sReleased = on != 0;
}

void vrspot_here(Vec3 p) {
    sHere = p;
    sHereHave = 1;
}

static float rdf(const uint8_t *p) {
    float x;

    memcpy(&x, p, 4);
    return x;
}

static float wrap_rad(float a) {
    while (a > 3.14159265f) {
        a -= 6.2831853f;
    }
    while (a < -3.14159265f) {
        a += 6.2831853f;
    }
    return a;
}

/* a leaf track's turn about y at a frame (format 7 or 8; the doors that swing are y only) */
static float spot_rot(const Placed *p, int frame) {
    const uint8_t *k;

    if (p->keys == NULL || p->frames <= 0 || (p->format != 7 && p->format != 8)) {
        return p->rot.y;
    }
    if (frame < 0) {
        frame = 0;
    }
    if (frame >= p->frames) {
        frame = p->frames - 1;
    }
    k = p->keys + frame * (p->format == 7 ? 24 : 12);
    return rdf(k + 4);
}

/* the flat ends of a leaf's track: its open and shut stops */
static int spot_span(const Placed *p, int *lo, int *hi) {
    int a, b;

    if (p->keys == NULL || (p->format != 7 && p->format != 8) || p->frames < 3) {
        return 0;
    }
    for (a = 1; a < p->frames - 1 && fabsf(spot_rot(p, a) - spot_rot(p, 0)) < 1.0e-3f; a++) {
    }
    for (b = p->frames - 2; b > 0 && fabsf(spot_rot(p, b) - spot_rot(p, p->frames - 1)) < 1.0e-3f; b--) {
    }
    if (b - a < 2) {
        return 0;
    }
    *lo = a - 1;
    *hi = b + 1;
    return 1;
}

/* where a point is in the spot's box: u along the doorway, v behind it */
static void spot_local(const VrSpot *s, float x, float z, float *u, float *v) {
    float dx = x - s->ox, dz = z - s->oz;

    *u = dx * sinf(s->yaw) + dz * cosf(s->yaw);
    *v = dx * s->nx + dz * s->nz;
}

static void spot_world(const VrSpot *s, float u, float v, float *x, float *z) {
    *x = s->ox + u * sinf(s->yaw) + v * s->nx;
    *z = s->oz + u * cosf(s->yaw) + v * s->nz;
}

static int spot_near(const VrSpot *s, float x, float y, float z) {
    float u, v;

    spot_local(s, x, z, &u, &v);
    /* six units in front: the threshold's barred floor, which the base game's noclip box covered,
     * so she can reach the doorway on the wardrobe floor */
    return fabsf(u) <= s->half_width + 2.0f && v >= -6.0f && v <= s->depth + 2.0f &&
           y >= s->floor_y - 2.0f && y <= s->floor_y + s->height;
}

static int spot_inside(const VrSpot *s, float x, float y, float z) {
    float u, v;

    spot_local(s, x, z, &u, &v);
    return fabsf(u) <= s->half_width && v >= 0.0f && v <= s->depth &&
           y >= s->floor_y - 2.0f && y <= s->floor_y + s->height;
}

/* a leaf's closure, 0 open .. 1 shut, from its own turn */
static float spot_leaf_closure(const VrSpot *s, int i, const Placed *p) {
    float span;

    if (!s->have_ang[i]) {
        return 0.0f;
    }
    span = wrap_rad(s->shut_ang[i] - s->open_ang[i]);
    if (fabsf(span) < 1.0e-4f) {
        return fabsf(wrap_rad(p->rot.y - s->shut_ang[i])) < 0.02f ? 1.0f : 0.0f;
    }
    {
        float c = wrap_rad(p->rot.y - s->open_ang[i]) / span;

        return c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
    }
}

/* the leaf is at its shut stop and its shut width covers u */
static int spot_blocked(const VrSpot *s, const PlacedSet *set, float u) {
    int i;

    for (i = 0; i < s->nleaves; i++) {
        const Placed *p = &set->p[s->leaf[i]];
        float c = spot_leaf_closure(s, i, p);
        float lo = s->seg_lo[i], hi = s->seg_hi[i];

        if (c < 0.995f) {
            continue;
        }
        if (hi < lo) {
            float t = lo;
            lo = hi;
            hi = t;
        }
        if (u >= lo - 0.1f && u <= hi + 0.1f) {
            return 1;
        }
    }
    return 0;
}

/* each leaf's shut width along u, and the open and shut turns of its first track */
static void spot_measure(Engine *e, VrSpot *s) {
    PlacedSet *set = &e->room.placed;
    int i;

    for (i = 0; i < s->nleaves; i++) {
        Placed *p = &set->p[s->leaf[i]];
        Placed own = *p;
        int lo = 0, hi = 0;

        s->seg_lo[i] = s->seg_hi[i] = 0.0f;
        s->have_ang[i] = 0;
        if (p->mesh.nv > 0) {
            Placed at = *p;
            Mat4 w;
            int k;

            at.rot = p->def_rot;
            at.pos = p->def_pos;
            w = placed_world(&at);
            for (k = 0; k < p->mesh.nv; k++) {
                Vec3 v = mat4_point(&w, vec3(p->mesh.v[k].x, p->mesh.v[k].y, p->mesh.v[k].z));
                float u, vv;

                spot_local(s, v.x, v.z, &u, &vv);
                if (k == 0 || u < s->seg_lo[i]) {
                    s->seg_lo[i] = u;
                }
                if (k == 0 || u > s->seg_hi[i]) {
                    s->seg_hi[i] = u;
                }
            }
        } else {
            float u, vv;

            spot_local(s, p->def_pos.x, p->def_pos.z, &u, &vv);
            s->seg_lo[i] = s->seg_hi[i] = u;
        }
        placed_anim(set, &own, 0, 0);
        if (spot_span(&own, &lo, &hi)) {
            s->open_ang[i] = spot_rot(&own, lo);
            s->shut_ang[i] = spot_rot(&own, hi);
            s->have_ang[i] = 1;
            placed_anim(set, p, 0, 0);          /* (its own track, so a hand may take it) */
            placed_frame_set(set, p, hi);       /* (the shut pose it is defined with) */
        }
    }
}

static Actor *fiona_actor(Engine *e) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        if (e->actors[i].used && strncmp(e->actors[i].name, "O_FI", 4) == 0) {
            return &e->actors[i];
        }
    }
    return NULL;
}

void vrspot_reset(void) {
    memset(sSpots, 0, sizeof(sSpots));
    sActive = -1;
    sReleased = 0;
}

int vrspot_add(Engine *e, const char *leaf_a, const char *leaf_b, float ox, float oz, float yaw,
               float half_width, float depth, float inside_x, float inside_z) {
    PlacedSet *set;
    Placed *a, *b = NULL;
    VrSpot *s;
    Actor *f;
    int i, id = -1, na, nb = -1;
    float nx, nz, dot, h = 0.0f;

    if (e == NULL) {
        return -1;
    }
    yaw = yaw * 3.14159265f / 180.0f;   /* (the place gives degrees; the box is in radians) */
    if (sRoom != e->room.id) {
        vrspot_reset();
        sRoom = e->room.id;
    }
    set = &e->room.placed;
    a = placed_named(set, leaf_a);
    if (a == NULL) {
        return -1;
    }
    if (leaf_b != NULL && leaf_b[0] != '\0') {
        b = placed_named(set, leaf_b);
        if (b == NULL) {
            return -1;
        }
    }
    na = (int)(a - set->p);
    if (b != NULL) {
        nb = (int)(b - set->p);
    }
    for (i = 0; i < VRSPOT_MAX; i++) {
        if (sSpots[i].used && sSpots[i].nleaves > 0 && sSpots[i].leaf[0] == na &&
            (sSpots[i].nleaves < 2 || sSpots[i].leaf[1] == nb)) {
            return i;
        }
    }
    for (i = 0; i < VRSPOT_MAX; i++) {
        if (!sSpots[i].used) {
            id = i;
            break;
        }
    }
    if (id < 0) {
        return -1;
    }
    s = &sSpots[id];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->leaf[0] = na;
    s->nleaves = 1;
    if (b != NULL) {
        s->leaf[1] = nb;
        s->nleaves = 2;
    }
    a->hiding_leaf = 1;
    if (b != NULL) {
        b->hiding_leaf = 1;
    }
    s->ox = ox;
    s->oz = oz;
    s->yaw = yaw;
    s->half_width = half_width;
    s->depth = depth;
    s->height = 24.0f;
    nx = cosf(yaw);
    nz = -sinf(yaw);
    dot = (inside_x - ox) * nx + (inside_z - oz) * nz;
    if (dot < 0.0f) {
        nx = -nx;
        nz = -nz;
    }
    s->nx = nx;
    s->nz = nz;
    f = fiona_actor(e);
    if (navmesh_find(&e->room.nav, vec3(inside_x, f != NULL ? f->pos.y : 0.0f, inside_z), 1000.0f, &h) >= 0) {
        s->floor_y = h;
    } else {
        s->floor_y = f != NULL ? f->pos.y : 0.0f;
    }
    spot_measure(e, s);
    return id;
}


static int spot_pick(Engine *e, float x, float y, float z) {
    int i, best = -1;
    float best_d = 25.0f * 25.0f;

    for (i = 0; i < VRSPOT_MAX; i++) {
        VrSpot *s = &sSpots[i];
        float dx, dz, d;

        if (!s->used) {
            continue;
        }
        s->inside = spot_inside(s, x, y, z);
        s->near = spot_near(s, x, y, z);
        if (s->inside) {
            return i;
        }
        dx = x - s->ox;
        dz = z - s->oz;
        d = dx * dx + dz * dz;
        if (s->near && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void vrspot_tick(Engine *e) {
    PlacedSet *set;
    Actor *f;
    Vec3 at;
    int i, pick;

    if (e == NULL) {
        return;
    }
    if (sRoom != e->room.id) {
        vrspot_reset();
        sRoom = e->room.id;
    }
    set = &e->room.placed;
    if (sReleased) {
        Actor *rf = fiona_actor(e);
        int k;

        sActive = -1;
        if (rf != NULL) {
            Vec3 rat = sHereHave ? sHere : rf->pos;

            sActive = spot_pick(e, rat.x, rat.y, rat.z);
        }
        for (i = 0; i < VRSPOT_MAX; i++) {
            VrSpot *s = &sSpots[i];

            if (s->used) {
                s->full = s->half = s->enclosed = 0;
                s->was_full = 0;
                for (k = 0; k < s->nleaves; k++) {
                    set->p[s->leaf[k]].alpha = 1.0f;
                }
            }
        }
        return;
    }
    f = fiona_actor(e);
    if (f == NULL) {
        sActive = -1;
        return;
    }
    at = sHereHave ? sHere : f->pos;
    pick = spot_pick(e, at.x, at.y, at.z);
    sActive = pick;
    for (i = 0; i < VRSPOT_MAX; i++) {
        VrSpot *s = &sSpots[i];
        int k;
        float c = 1.0f, a;

        if (!s->used) {
            continue;
        }
        for (k = 0; k < s->nleaves; k++) {
            float lc = spot_leaf_closure(s, k, &set->p[s->leaf[k]]);

            if (k == 0 || lc < c) {
                c = lc;
            }
        }
        s->closure = c;
        if (i == pick) {
            if (s->inside && (c >= 0.85f || (s->was_full && c >= 0.80f))) {
                s->full = 1;
                s->half = 0;
                s->was_full = 1;
            } else {
                s->full = 0;
                s->half = s->inside && c > 0.40f;
                if (!s->inside || c < 0.80f) {
                    s->was_full = 0;
                }
            }
            s->enclosed = s->inside && c >= 0.995f;
        } else {
            s->full = s->half = s->enclosed = 0;
            s->was_full = 0;
        }
        a = (i == pick && s->inside && s->full && gVrOptions.hide_door_see_through) ? 0.5f : 1.0f;
        for (k = 0; k < s->nleaves; k++) {
            set->p[s->leaf[k]].alpha = a;
        }
    }
}


int vrspot_active(void) { return sActive >= 0; }
int vrspot_inside(void) { return sActive >= 0 && sSpots[sActive].inside; }
int vrspot_full(void) { return sActive >= 0 && sSpots[sActive].full; }
int vrspot_half(void) { return sActive >= 0 && sSpots[sActive].half; }
int vrspot_enclosed(void) { return sActive >= 0 && sSpots[sActive].enclosed; }
float vrspot_closure(void) { return sActive >= 0 ? sSpots[sActive].closure : 0.0f; }

int vrspot_noclip(void) {
    return !sReleased && sActive >= 0 && (sSpots[sActive].inside || sSpots[sActive].near);
}

int vrspot_id_at(Engine *e) {
    (void)e;
    return sActive;
}

static Vec3 spot_step_id(Engine *e, int id, Vec3 at, Vec3 step) {
    VrSpot *s;
    PlacedSet *set;
    float u, v, u2, v2, u_raw, x, z;

    s = &sSpots[id];
    set = &e->room.placed;
    spot_local(s, at.x, at.z, &u, &v);
    spot_local(s, at.x + step.x, at.z + step.z, &u2, &v2);
    u_raw = u2;
    if (!spot_near(s, at.x, at.y, at.z) && !spot_near(s, at.x + step.x, at.y, at.z + step.z)) {
        return step;
    }
    {
        int from_inside = fabsf(u) <= s->half_width && v >= 0.0f && v <= s->depth;

        if (from_inside) {
            /* inside: the side walls, the back wall, and the doorway where a leaf is fully shut */
            if (u2 > s->half_width) {
                u2 = s->half_width;
            } else if (u2 < -s->half_width) {
                u2 = -s->half_width;
            }
            if (v2 > s->depth) {
                v2 = s->depth;
            }
            if (v2 < 0.0f && spot_blocked(s, set, u2)) {
                v2 = 0.0f;
            }
        } else if (v > s->depth) {
            /* behind it: the back wall, never through it */
            if (v2 < s->depth) {
                v2 = s->depth;
            }
            if (u2 > s->half_width) {
                u2 = s->half_width;
            } else if (u2 < -s->half_width) {
                u2 = -s->half_width;
            }
        } else {
            /* in front or at a side: the doorway only, and only where it is open */
            if (v2 > s->depth) {
                v2 = s->depth;
            }
            if (v2 > 0.0f) {
                if (u2 > s->half_width) {
                    u2 = s->half_width;
                } else if (u2 < -s->half_width) {
                    u2 = -s->half_width;
                }
                if (fabsf(u_raw) > s->half_width || spot_blocked(s, set, u2)) {
                    v2 = 0.0f;
                }
            }
        }
    }
    spot_world(s, u2, v2, &x, &z);
    return vec3(x - at.x, step.y, z - at.z);
}

Vec3 vrspot_step(Engine *e, Vec3 at, Vec3 step) {
    if (sReleased || sActive < 0) {
        return step;
    }
    return spot_step_id(e, sActive, at, step);
}

Vec3 vrspot_step_at(Engine *e, Vec3 at, Vec3 step) {
    int id = spot_pick(e, at.x, at.y, at.z);

    if (id < 0) {
        return step;
    }
    return spot_step_id(e, id, at, step);
}

Vec3 vrspot_step_here(Engine *e, float dx, float dz) {
    Actor *f = fiona_actor(e);

    if (f == NULL && !sHereHave) {
        return vec3(dx, 0.0f, dz);
    }
    return vrspot_step(e, sHereHave ? sHere : f->pos, vec3(dx, 0.0f, dz));
}

