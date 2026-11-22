/* The hiding spots a player walks into: see vrspots.h. A spot's parts - the placed objects that
 * fold, slide or morph with it - are posed from one closure every tick, so a wardrobe's two bifold
 * panels always move together and the gallery curtain's morph follows the same value. */
#include "vrspots.h"
#include "../platform/vrlog.h"

#include "../data/navmesh.h"
#include "../data/pac.h"
#include "../platform/vr.h"
#include "engine.h"
#include "placed.h"
#include "vrmarkers.h"
#include "vrpath.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* the closure a hand lets go of faster than this (c a tick) keeps its speed to the stop */
#define VRSPOT_COAST 0.02f
/* a part whose flat end matches the object's defined pose: radians and room units */
#define VRSPOT_SAME 0.05f
/* how near a place's typed leaves must be to a marker spot to be its parts (world units) */
#define VRSPOT_LEAF_REACH 6.0f
/* how near a placed object must be to a spot's boxes to be one of its parts (world units) */
#define VRSPOT_PART_REACH 4.0f
/* a part's handle must travel at least this far as the spot closes to be a door (world units) */
#define VRSPOT_PART_TRAVEL 2.0f

typedef struct VrPart {
    int id;            /* the placed object's index */
    int morph;         /* 1: kind 1 / 3, its frames and blend; 0: a track */
    int lo, hi;        /* a track's open and shut frames (c = 0 at lo, 1 at hi) */
    int handle;        /* the mesh vertex that travels furthest as c goes 0..1 */
    Vec3 path[VRSPOT_PATH_N];   /* that vertex's room place, c = 0..1 */
} VrPart;

typedef struct VrSpot {
    int used;
    int marker;        /* 1: a spot from PROPS/hidespots.txt; 0: the place's typed box */
    int from;          /* the marker's index (vrmarkers) */
    char name[32];
    VrBox grab, bound; /* the marker's boxes, room space */
    VrPart part[VRSPOT_PARTS];
    int nparts;
    float c;           /* the one closure: 0 open .. 1 closed */
    int script_owned;  /* a script is playing the parts: hands off until it ends */
    int script_morph;  /* (the change that took it was a morph's words: the debug reason) */
    int idle;          /* (a morph the script left alone: ticks unchanged) */
    int coast;         /* 1: c keeps its speed to the end it was heading for */
    float coast_speed;
    /* the parts' state as we left them: a script's change shows here */
    const uint8_t *keys[VRSPOT_PARTS];
    int frames[VRSPOT_PARTS], frame[VRSPOT_PARTS], paused[VRSPOT_PARTS];
    uint32_t words[VRSPOT_PARTS][3];   /* (morph: +0x74, +0x78, +0x7C) */
    int wrote[VRSPOT_PARTS];
    int fade_logged;
    /* the place's own box (its typed `vr-hiding-spot`, and the fallback spots' geometry) */
    int leaf[VRSPOT_LEAVES], nleaves;
    float ox, oz, yaw, half_width, depth, floor_y, height;
    float nx, nz;                  /* the interior's normal: v's direction */
    float seg_lo[VRSPOT_LEAVES], seg_hi[VRSPOT_LEAVES];   /* each leaf's shut width, along u */
    float open_ang[VRSPOT_LEAVES], shut_ang[VRSPOT_LEAVES];
    int have_ang[VRSPOT_LEAVES];
    /* her state at the spot */
    int inside, full, half, enclosed, near;
    int was_full;
    float closure;
} VrSpot;

static VrSpot sSpots[VRSPOT_MAX];
static int sNSpots;
static int sActive = -1;
static int sRoom = -2;
static int sReleased;
static Vec3 sHere;
static int sHereHave;
static int sDebug = 1;   /* (the spots' debug lines: the misses and the first fade - `0 vr-spot-debug!` off) */
/* (DEBUG) her state at each spot as the last tick left it: the change the state line reports) */
static int sWasInside[VRSPOT_MAX], sWasFull[VRSPOT_MAX], sWasHalf[VRSPOT_MAX], sWasEnclosed[VRSPOT_MAX];

static void spot_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;

    if (!VR_LOG_ON()) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (gEngine.forth != NULL) {
        forth_printf(gEngine.forth, "%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

static int32_t s32(const uint8_t *p) {
    int32_t x;

    memcpy(&x, p, 4);
    return x;
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

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* a morph's weight w from its three words: (b == 1 ? t : 0) + (a == 1 ? 1 - t : 0) */
static float morph_weight(uint32_t a, uint32_t b, uint32_t t_bits) {
    float t = 0.0f;

    memcpy(&t, &t_bits, 4);
    return clamp01(((int)b == 1 ? t : 0.0f) + ((int)a == 1 ? 1.0f - t : 0.0f));
}

/* ---- the boxes (vrmarkers.c): a point in a box's own space, its up axis, and the tests on it ---- */

/* the point in the box's space (l[0..2]) and which of its axes is room up */
static void box_local(const VrBox *b, Vec3 p, float l[3], int *up) {
    Vec3 v = vrbox_to_local(b, p);

    l[0] = v.x;
    l[1] = v.y;
    l[2] = v.z;
    vrbox_up_axis(b, up);
}

/* 1 when the point's horizontal place is inside the box's footprint, grown by margin */
static int box_footprint(const VrBox *b, Vec3 p, float margin) {
    float l[3];
    int up, k;

    box_local(b, p, l, &up);
    for (k = 0; k < 3; k++) {
        if (k != up && fabsf(l[k]) > b->h[k] + margin) {
            return 0;
        }
    }
    return 1;
}

/* 1 when the point's height is within the box's own up range, grown by margin */
static int box_height(const VrBox *b, Vec3 p, float margin) {
    float l[3];
    int up;

    box_local(b, p, l, &up);
    return fabsf(l[up]) <= b->h[up] + margin;
}

/* the point moved back inside the box's footprint, shrunk by margin (its height kept) */
static Vec3 box_keep_in(const VrBox *b, Vec3 p, float margin) {
    Vec3 l = vrbox_to_local(b, p);
    int up, k;
    float c[3];

    vrbox_up_axis(b, &up);
    c[0] = l.x;
    c[1] = l.y;
    c[2] = l.z;
    for (k = 0; k < 3; k++) {
        float lim = b->h[k] - margin;

        if (k == up) {
            continue;
        }
        if (lim < 0.0f) {
            lim = 0.0f;
        }
        if (c[k] > lim) {
            c[k] = lim;
        } else if (c[k] < -lim) {
            c[k] = -lim;
        }
    }
    return vrbox_to_room(b, vec3(c[0], c[1], c[2]));
}

/* the target kept out of the box's footprint grown by margin, from a place already outside it: it
 * stops at the face it came to, an axis at a time (its height kept) */
static Vec3 box_keep_out(const VrBox *b, Vec3 at, Vec3 target, float margin) {
    Vec3 la = vrbox_to_local(b, at), lt = vrbox_to_local(b, target);
    int up, k;
    float a[3], t[3];

    vrbox_up_axis(b, &up);
    a[0] = la.x;
    a[1] = la.y;
    a[2] = la.z;
    t[0] = lt.x;
    t[1] = lt.y;
    t[2] = lt.z;
    for (k = 0; k < 3; k++) {
        float lim = b->h[k] + margin;

        if (k == up || fabsf(a[k]) <= lim) {
            continue;   /* (no wall on its height, or she was inside on that axis already) */
        }
        if (fabsf(t[k]) <= lim) {
            t[k] = a[k] > 0.0f ? lim : -lim;   /* (stopped at the face she came to) */
        }
    }
    return vrbox_to_room(b, vec3(t[0], t[1], t[2]));
}

/* how far the point is from the box, in the box's own axes (0 inside or touching) */
static float box_gap(const VrBox *b, Vec3 p) {
    Vec3 l = vrbox_to_local(b, p);
    float g[3] = {fabsf(l.x) - b->h[0], fabsf(l.y) - b->h[1], fabsf(l.z) - b->h[2]};
    float d = 0.0f;
    int k;

    for (k = 0; k < 3; k++) {
        if (g[k] > 0.0f) {
            d += g[k] * g[k];
        }
    }
    return sqrtf(d);
}

/* 1 when two [lo..hi] ranges meet */
static int ranges_overlap(float alo, float ahi, float blo, float bhi) {
    return alo <= bhi && blo <= ahi;
}

/* the box's own world bounds, its eight corners */
static void box_world(const VrBox *b, Vec3 *lo, Vec3 *hi) {
    int i;

    for (i = 0; i < 8; i++) {
        Vec3 c = vrbox_to_room(b, vec3(i & 1 ? b->h[0] : -b->h[0], i & 2 ? b->h[1] : -b->h[1],
                                       i & 4 ? b->h[2] : -b->h[2]));

        if (i == 0) {
            *lo = *hi = c;
            continue;
        }
        if (c.x < lo->x) {
            lo->x = c.x;
        }
        if (c.y < lo->y) {
            lo->y = c.y;
        }
        if (c.z < lo->z) {
            lo->z = c.z;
        }
        if (c.x > hi->x) {
            hi->x = c.x;
        }
        if (c.y > hi->y) {
            hi->y = c.y;
        }
        if (c.z > hi->z) {
            hi->z = c.z;
        }
    }
}

/* the box's own world y range: the height a hand may reach a spot at when its part has no bounds
 * of its own */
static void box_y_range(const VrBox *b, float *lo, float *hi) {
    Vec3 a, c;

    box_world(b, &a, &c);
    *lo = a.y;
    *hi = c.y;
}

/* the object's room bounds: every vertex through placed_world, the transform it is drawn with */
static int placed_bounds(const Placed *p, Vec3 *lo, Vec3 *hi) {
    Mat4 w = placed_world(p);
    int i;

    if (p->mesh.nv <= 0 || p->mesh.v == NULL) {
        return 0;
    }
    *lo = vec3(1.0e30f, 1.0e30f, 1.0e30f);
    *hi = vec3(-1.0e30f, -1.0e30f, -1.0e30f);
    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = mat4_point(&w, vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z));

        *lo = vec3_min(*lo, v);
        *hi = vec3_max(*hi, v);
    }
    return 1;
}

/* ---- a part's pose: its track, or its morph's two frames ---- */

static PlacedSet *cur_set(void) { return &gEngine.room.placed; }

/* how far apart two turns are, each part wrapped: the largest */
static float rot_gap(Vec3 a, Vec3 b) {
    float dx = fabsf(wrap_rad(a.x - b.x)), dy = fabsf(wrap_rad(a.y - b.y)), dz = fabsf(wrap_rad(a.z - b.z));

    return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
}

/* the frames a track turns through: its two flat ends are the stops (as vrhands' ward_span) */
static int part_span(const Placed *p, int *lo, int *hi) {
    Vec3 r0, rl, ra, pos;
    int a, b;

    if (p->keys == NULL || (p->format != 7 && p->format != 8) || p->frames < 3) {
        return 0;
    }
    placed_pose_at(p, 0.0f, &r0, &pos);
    for (a = 1; a < p->frames - 1; a++) {
        placed_pose_at(p, (float)a, &ra, &pos);
        if (rot_gap(ra, r0) >= 1.0e-3f) {
            break;
        }
    }
    placed_pose_at(p, (float)(p->frames - 1), &rl, &pos);
    for (b = p->frames - 2; b > 0; b--) {
        placed_pose_at(p, (float)b, &ra, &pos);
        if (rot_gap(ra, rl) >= 1.0e-3f) {
            break;
        }
    }
    if (b - a < 2) {
        return 0;
    }
    *lo = a - 1;
    *hi = b + 1;
    return 1;
}

/* a morph's vertex `v` at blend t between its frames 0 and 1, model space: placed.c's own decode of
 * a frame vertex (placed_morph_vertex, with the model's +0x60 start), so the curtain drawn and the
 * edge grabbed are the same point */
static int morph_pos(const Placed *p, float t, int v, Vec3 *out) {
    Vec3 a, b;

    if (!placed_morph_vertex(p, 0, v, &a) || !placed_morph_vertex(p, 1, v, &b)) {
        return 0;
    }
    *out = vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    return 1;
}

/* the part's vertex `v`'s room place at closure c */
static Vec3 part_vertex(const VrPart *pt, int v, float c) {
    Placed *p = &cur_set()->p[pt->id];

    if (pt->morph) {
        Vec3 m = p->pos;
        Mat4 w = placed_world(p);

        if (morph_pos(p, clamp01(c), v, &m)) {
            return mat4_point(&w, m);
        }
        return p->pos;
    }
    {
        Placed at = *p;
        Vec3 rot, pos;
        Mat4 w;
        float frame = (float)pt->lo + clamp01(c) * (float)(pt->hi - pt->lo);

        placed_pose_at(p, frame, &rot, &pos);
        at.rot = rot;
        at.pos = pos;
        w = placed_world(&at);
        return mat4_point(&w, vec3(p->mesh.v[v].x, p->mesh.v[v].y, p->mesh.v[v].z));
    }
}

/* the part's handle: the vertex whose room place travels furthest as c goes 0..1, and its path at
 * VRSPOT_PATH_N closures. 1 when something travels far enough to be a door. */
static int part_path(VrPart *pt) {
    Placed *p = &cur_set()->p[pt->id];
    Vec3 prev = vec3(0.0f, 0.0f, 0.0f), cur;
    float best = -1.0f;
    int v, k, handle = -1;

    if (p->mesh.nv <= 0) {
        return 0;
    }
    for (v = 0; v < p->mesh.nv; v++) {
        float travel = 0.0f;

        prev = part_vertex(pt, v, 0.0f);
        for (k = 1; k < VRSPOT_PATH_N; k++) {
            cur = part_vertex(pt, v, (float)k / (float)(VRSPOT_PATH_N - 1));
            travel += vec3_len(vec3_sub(cur, prev));
            prev = cur;
        }
        if (travel > best) {
            best = travel;
            handle = v;
        }
    }
    if (handle < 0 || best < (pt->morph ? 0.1f : VRSPOT_PART_TRAVEL)) {
        return 0;
    }
    pt->handle = handle;
    for (k = 0; k < VRSPOT_PATH_N; k++) {
        pt->path[k] = part_vertex(pt, handle, (float)k / (float)(VRSPOT_PATH_N - 1));
    }
    return 1;
}

static float placed_gap(const Placed *p, const VrBox *b) {
    Mat4 w = placed_world(p);
    float best = 1.0e30f;
    int i;

    if (p->mesh.nv <= 0 || p->mesh.v == NULL) {
        return best;
    }
    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = mat4_point(&w, vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z));
        float d = box_gap(b, v);

        if (d < best) {
            best = d;
        }
    }
    return best;
}
/* 1 when the track's pose at that frame is the object's defined one (the closed default) */
static int pose_is_def(const Placed *p, int frame) {
    Vec3 rot, pos;

    placed_pose_at(p, (float)frame, &rot, &pos);
    if (rot_gap(rot, p->def_rot) >= VRSPOT_SAME) {
        return 0;
    }
    if (p->format == 7) {
        return fabsf(pos.x - p->def_pos.x) < VRSPOT_SAME && fabsf(pos.y - p->def_pos.y) < VRSPOT_SAME &&
               fabsf(pos.z - p->def_pos.z) < VRSPOT_SAME;
    }
    return 1;
}

/* the room's name as the markers have it (the log) */
static void spot_room_name(char *out, size_t n) {
    char path[64];
    const char *base;
    size_t len;

    if (out == NULL || n == 0) {
        return;
    }
    out[0] = 0;
    if (gEngine.room.id < 0) {
        return;
    }
    pac_room_path(gEngine.room.id, path, sizeof(path));
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    len = strlen(base);
    if (len >= 4 && strcmp(base + len - 4, ".PAC") == 0) {
        len -= 4;
    }
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, base, len);
    out[len] = 0;
}

/* build the part: 0 when the object cannot be one (no track with flat ends, a morph without two
 * frames, nothing moving) */
static int part_build(VrPart *pt, int id) {
    PlacedSet *set = cur_set();
    Placed *p = &set->p[id];
    int lo = 0, hi = 0;

    memset(pt, 0, sizeof(*pt));
    pt->id = id;
    pt->morph = p->kind == 1 || p->kind == 3;
    if (pt->morph) {
        if (p->def == NULL || s32(p->def + 0x40) <= 0) {
            return 0;
        }
        pt->lo = 0;
        pt->hi = 1;
        return part_path(pt);
    }
    {
        Placed own = *p;

        placed_anim(set, &own, 0, 0);   /* (a copy: its own track 0, the swing's) */
        if (!part_span(&own, &lo, &hi)) {
            return 0;
        }
    }
    if (pose_is_def(p, hi)) {   /* (the shut end is the defined pose: c = 1 there) */
        pt->lo = lo;
        pt->hi = hi;
    } else if (pose_is_def(p, lo)) {
        pt->lo = hi;
        pt->hi = lo;
    } else {
        char room[16];

        spot_room_name(room, sizeof(room));
        spot_log("vrspots: %s closed end mismatch\n", room);
        pt->lo = lo;
        pt->hi = hi;
    }
    placed_anim(set, p, 0, 0);   /* (the spot drives it: its own track, held from c) */
    return part_path(pt);
}

/* pose the part from the spot's closure, and keep what we left it at */
static void part_apply(VrSpot *s, int i) {
    VrPart *pt = &s->part[i];
    Placed *p = &cur_set()->p[pt->id];

    if (pt->morph) {   /* (as kit.fs's `morph`: frame a, frame b, the blend t) */
        uint32_t t = 0;

        memcpy(&t, &s->c, 4);
        p->words[0x74 / 4] = 0;
        p->words[0x78 / 4] = 1;
        p->words[0x7C / 4] = t;
        s->words[i][0] = p->words[0x74 / 4];
        s->words[i][1] = p->words[0x78 / 4];
        s->words[i][2] = p->words[0x7C / 4];
    } else {
        placed_frame_set_f(cur_set(), p, (float)pt->lo + s->c * (float)(pt->hi - pt->lo));
        s->keys[i] = p->keys;
        s->frames[i] = p->frames;
        s->frame[i] = p->frame;
        s->paused[i] = p->paused;
    }
    s->wrote[i] = 1;
}

/* 1 when something else has the part since we left it: a script played its track or wrote its words */
static int part_changed(const VrSpot *s, int i) {
    const VrPart *pt = &s->part[i];
    Placed *p = &cur_set()->p[pt->id];

    if (!s->wrote[i]) {
        return 0;
    }
    if (pt->morph) {
        /* (the weight alone: a script's or placed_tick's swap of a/b, or its wrapping of t, is not
         * a change - our own write must never look like a script's) */
        float ww = morph_weight(s->words[i][0], s->words[i][1], s->words[i][2]);
        float wn = morph_weight(p->words[0x74 / 4], p->words[0x78 / 4], p->words[0x7C / 4]);

        return fabsf(wn - ww) > 0.01f;
    }
    return p->keys != s->keys[i] || p->frames != s->frames[i] || !p->paused || p->frame != s->frame[i];
}

/* the closure the part's own pose shows (a script left it there): -1 when it cannot be read */
static float part_read_c(const VrPart *pt) {
    Placed *p = &cur_set()->p[pt->id];

    if (pt->morph) {   /* (w = (b == 1 ? t : 0) + (a == 1 ? 1 - t : 0), as kit.fs's blend) */
        return morph_weight(p->words[0x74 / 4], p->words[0x78 / 4], p->words[0x7C / 4]);
    }
    {
        Placed own = *p;
        int n, best = -1;
        float bd = 1.0e30f;

        if (pt->hi == pt->lo) {
            return -1.0f;
        }
        placed_anim(cur_set(), &own, 0, 0);
        if (own.keys == NULL || own.frames <= 0) {
            return -1.0f;
        }
        for (n = 0; n < own.frames; n++) {
            Vec3 rot, pos;
            float d;

            placed_pose_at(&own, (float)n, &rot, &pos);
            if (own.format == 9) {
                d = vec3_len(vec3_sub(pos, p->pos));
            } else if (own.format == 7) {
                d = rot_gap(rot, p->rot) + vec3_len(vec3_sub(pos, p->pos)) * 0.05f;
            } else {
                d = rot_gap(rot, p->rot);
            }
            if (d < bd) {
                bd = d;
                best = n;
            }
        }
        if (best < 0) {
            return -1.0f;
        }
        return clamp01(((float)best - (float)pt->lo) / (float)(pt->hi - pt->lo));
    }
}

/* every part posed from the spot's closure */
static void spot_apply(VrSpot *s) {
    int i;

    for (i = 0; i < s->nparts; i++) {
        part_apply(s, i);
    }
}

/* the closure the parts' own poses show (the first part that can be read), else the one kept */
static float spot_read_c(const VrSpot *s) {
    int i;

    for (i = 0; i < s->nparts; i++) {
        float c = part_read_c(&s->part[i]);

        if (c >= 0.0f) {
            return c;
        }
    }
    return s->c;
}

/* a placed object that is never a spot's part: the nursery's chairs and its plain left/right */
static int part_never(const Placed *p) {
    return strncmp(p->name, "movechair", 9) == 0 || strcmp(p->name, "right") == 0 || strcmp(p->name, "left") == 0;
}

/* the spot's parts, logged once when they are picked */
static void spot_log_parts(const VrSpot *s) {
    char room[16], line[320];
    int i, n;

    spot_room_name(room, sizeof(room));
    n = snprintf(line, sizeof(line), "vrspots: %s %s parts:", room, s->name[0] != 0 ? s->name : "spot");
    for (i = 0; i < s->nparts && n > 0 && n < (int)sizeof(line); i++) {
        const VrPart *pt = &s->part[i];
        Placed *p = &cur_set()->p[pt->id];

        if (pt->morph) {
            n += snprintf(line + n, sizeof(line) - (size_t)n, " %s(morph)", p->name);
        } else {
            n += snprintf(line + n, sizeof(line) - (size_t)n, " %s(fmt %d, track 0 %d..%d)", p->name, p->format,
                          pt->lo, pt->hi);
        }
    }
    spot_log("%s\n", line);
}

/* pick the spot's parts by itself: every shown object within reach of either box that is a door -
 * a track with two flat ends that moves a vertex more than VRSPOT_PART_TRAVEL, or a morph */
static void part_auto(VrSpot *s) {
    PlacedSet *set = cur_set();
    float dist[PLACED_MAX];
    int ids[PLACED_MAX], n = 0, i, k;

    s->nparts = 0;
    for (i = 0; i < set->n; i++) {
        Placed *p = &set->p[i];
        float d, db;

        if (!p->shown || p->mesh.nv <= 0 || part_never(p)) {
            continue;
        }
        d = placed_gap(p, &s->grab);
        db = placed_gap(p, &s->bound);
        if (db < d) {
            d = db;
        }
        if (d > VRSPOT_PART_REACH) {
            continue;
        }
        for (k = n; k > 0 && dist[k - 1] > d; k--) {   /* (the nearest first) */
            dist[k] = dist[k - 1];
            ids[k] = ids[k - 1];
        }
        dist[k] = d;
        ids[k] = i;
        n++;
    }
    for (i = 0; i < n && s->nparts < VRSPOT_PARTS; i++) {
        VrPart pt;

        if (part_build(&pt, ids[i])) {
            s->part[s->nparts++] = pt;
        }
    }
}

/* (DEBUG) a morph's world bounds at one frame, with the model's +0x60 start and without it: the room
 * entry line that shows where a curtain (or any morph) is really drawn */
static int morph_bounds(const Placed *p, int frame, Vec3 *lo, Vec3 *hi, Vec3 *lo0, Vec3 *hi0) {
    Mat4 w = placed_world(p);
    Vec3 start;
    int n, i, got = 0;

    if (p->def == NULL || !placed_morph_start(p, &start)) {
        return 0;
    }
    n = s32(p->def + 0x40);
    for (i = 0; i < n && i <= 0x10000; i++) {
        Vec3 m, a, b;

        if (!placed_morph_vertex(p, frame, i, &m)) {
            continue;
        }
        a = mat4_point(&w, m);                                                  /* (as drawn) */
        b = mat4_point(&w, vec3(m.x - start.x, m.y - start.y, m.z - start.z));   /* (the start left out) */
        if (!got) {
            *lo = *hi = a;
            *lo0 = *hi0 = b;
            got = 1;
            continue;
        }
        lo->x = a.x < lo->x ? a.x : lo->x;
        lo->y = a.y < lo->y ? a.y : lo->y;
        lo->z = a.z < lo->z ? a.z : lo->z;
        hi->x = a.x > hi->x ? a.x : hi->x;
        hi->y = a.y > hi->y ? a.y : hi->y;
        hi->z = a.z > hi->z ? a.z : hi->z;
        lo0->x = b.x < lo0->x ? b.x : lo0->x;
        lo0->y = b.y < lo0->y ? b.y : lo0->y;
        lo0->z = b.z < lo0->z ? b.z : lo0->z;
        hi0->x = b.x > hi0->x ? b.x : hi0->x;
        hi0->y = b.y > hi0->y ? b.y : hi0->y;
        hi0->z = b.z > hi0->z ? b.z : hi0->z;
    }
    return got;
}

/* (DEBUG) the room's morphs, where each frame lands with the definition's start and without it */
static void spot_log_morphs(void) {
    PlacedSet *set = cur_set();
    int i, f;

    if (!sDebug) {
        return;
    }
    for (i = 0; i < set->n; i++) {
        Placed *p = &set->p[i];
        Vec3 start, o30 = vec3(0.0f, 0.0f, 0.0f);

        if (p->kind != 1 && p->kind != 3) {
            continue;
        }
        if (!placed_morph_start(p, &start)) {
            continue;
        }
        if (p->def + 0x3C <= p->def_end) {   /* (the float vector the definition holds beside the s32 one) */
            o30 = vec3(rdf(p->def + 0x30), rdf(p->def + 0x34), rdf(p->def + 0x38));
        }
        spot_log("placed: morph '%s' kind %d def_pos(%.3f %.3f %.3f) off30(%.3f %.3f %.3f) "
                 "start60*32/4096(%.3f %.3f %.3f)\n",
                 p->name, p->kind, (double)p->def_pos.x, (double)p->def_pos.y, (double)p->def_pos.z,
                 (double)o30.x, (double)o30.y, (double)o30.z, (double)(start.x * 32.0f),
                 (double)(start.y * 32.0f), (double)(start.z * 32.0f));
        for (f = 0; f < 2; f++) {
            Vec3 lo, hi, lo0, hi0;

            if (morph_bounds(p, f, &lo, &hi, &lo0, &hi0)) {
                spot_log("placed: morph '%s' f%d without(%.2f %.2f %.2f)..(%.2f %.2f %.2f) | "
                         "with(%.2f %.2f %.2f)..(%.2f %.2f %.2f)\n",
                         p->name, f, (double)lo0.x, (double)lo0.y, (double)lo0.z, (double)hi0.x, (double)hi0.y,
                         (double)hi0.z, (double)lo.x, (double)lo.y, (double)lo.z, (double)hi.x, (double)hi.y,
                         (double)hi.z);
            }
        }
    }
}

/* the current room's marker spots, a reset first when the room changed */
static void spots_room(struct Engine *e) {
    if (e == NULL || sRoom == e->room.id) {
        return;
    }
    if (sDebug) {
        spot_log("vrspots: room %d (was %d), %d spots reset\n", e->room.id, sRoom, sNSpots);
    }
    vrspot_reset();
    sRoom = e->room.id;
    spot_log_morphs();   /* (DEBUG: every morph of the room, as drawn and without its model's start) */
    {
        int ids[VRMARKERS_SPOTS], n, i;

        n = vrmarkers_spots_here(gEngine.room.id, ids, VRMARKERS_SPOTS);
        for (i = 0; i < n && sNSpots < VRSPOT_MAX; i++) {
            const VrBox *grab = vrmarkers_spot_grab(ids[i]);
            const VrBox *bound = vrmarkers_spot_bound(ids[i]);
            const char *name = vrmarkers_spot_name(ids[i]);
            VrSpot *s;

            if (grab == NULL || bound == NULL) {
                continue;   /* (a marker without both boxes is no spot) */
            }
            s = &sSpots[sNSpots];
            memset(s, 0, sizeof(*s));
            s->used = 1;
            s->marker = 1;
            s->from = ids[i];
            s->grab = *grab;
            s->bound = *bound;
            snprintf(s->name, sizeof(s->name), "%s", name != NULL ? name : "");
            part_auto(s);
            s->c = clamp01(spot_read_c(s));
            s->closure = s->c;
            spot_apply(s);
            sNSpots++;
            spot_log_parts(s);
            if (sDebug) {
                /* (the curtain check: the handle as drawn, and at both ends of its travel) */
                int k;

                for (k = 0; k < s->nparts; k++) {
                    const VrPart *pt = &s->part[k];
                    Placed *p = &cur_set()->p[pt->id];
                    Vec3 m0, m1, w0, w1;
                    Mat4 w = placed_world(p);

                    if (!pt->morph || !morph_pos(p, 0.0f, pt->handle, &m0) || !morph_pos(p, 1.0f, pt->handle, &m1)) {
                        continue;
                    }
                    w0 = mat4_point(&w, m0);
                    w1 = mat4_point(&w, m1);
                    spot_log("vrspots: %s %s '%s' handle c0 model(%.3f %.3f %.3f) world(%.3f %.3f %.3f) "
                             "c1 model(%.3f %.3f %.3f) world(%.3f %.3f %.3f) (world used)\n",
                             s->name, "morph", p->name, (double)m0.x, (double)m0.y, (double)m0.z, (double)w0.x,
                             (double)w0.y, (double)w0.z, (double)m1.x, (double)m1.y, (double)m1.z, (double)w1.x,
                             (double)w1.y, (double)w1.z);
                }
                if (s->nparts > 0) {   /* (the marker's boxes against the parts' whole travel: do they cover it?) */
                    char room[16];
                    Vec3 blo, bhi, glo, ghi, p0, p1;
                    int j, k2, have = 0;

                    spot_room_name(room, sizeof(room));
                    box_world(&s->bound, &blo, &bhi);
                    box_world(&s->grab, &glo, &ghi);
                    for (k2 = 0; k2 < s->nparts; k2++) {
                        for (j = 0; j < VRSPOT_PATH_N; j++) {
                            Vec3 q = s->part[k2].path[j];

                            if (!have) {
                                p0 = p1 = q;
                                have = 1;
                                continue;
                            }
                            p0.x = q.x < p0.x ? q.x : p0.x;
                            p0.z = q.z < p0.z ? q.z : p0.z;
                            p1.x = q.x > p1.x ? q.x : p1.x;
                            p1.z = q.z > p1.z ? q.z : p1.z;
                        }
                    }
                    spot_log("vrspots: %s '%s' bound x(%.2f..%.2f) z(%.2f..%.2f) grab x(%.2f..%.2f) z(%.2f..%.2f) "
                             "path x(%.2f..%.2f) z(%.2f..%.2f) overlap bound %d grab %d\n",
                             room, s->name, (double)blo.x, (double)bhi.x, (double)blo.z, (double)bhi.z,
                             (double)glo.x, (double)ghi.x, (double)glo.z, (double)ghi.z, (double)p0.x, (double)p1.x,
                             (double)p0.z, (double)p1.z,
                             ranges_overlap(blo.x, bhi.x, p0.x, p1.x) && ranges_overlap(blo.z, bhi.z, p0.z, p1.z),
                             ranges_overlap(glo.x, ghi.x, p0.x, p1.x) && ranges_overlap(glo.z, ghi.z, p0.z, p1.z));
                }
            }
        }
    }
}

/* ---- the place's own box (a spot without a marker: its typed box, and the hands' ward_* leaves) ---- */

/* a leaf track's turn at a frame (format 7 or 8; the doors that swing are y only) */
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
    sNSpots = 0;
    sActive = -1;
    sReleased = 0;
}

void vrspot_release(int on) { sReleased = on != 0; }

void vrspot_here(Vec3 p) {
    sHere = p;
    sHereHave = 1;
}

static float spot_gap_to_boxes(const VrSpot *m, const Placed *p) {
    float d = placed_gap(p, &m->grab), db = placed_gap(p, &m->bound);

    return db < d ? db : d;
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
    spots_room(e);
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
    /* a marker spot near these leaves owns them: its boxes are the spot's, the typed one is not */
    for (i = 0; i < sNSpots; i++) {
        VrSpot *m = &sSpots[i];
        float d;

        if (!m->used || !m->marker) {
            continue;
        }
        d = spot_gap_to_boxes(m, a);
        if (b != NULL) {
            float db = spot_gap_to_boxes(m, b);

            if (db < d) {
                d = db;
            }
        }
        if (d > VRSPOT_LEAF_REACH) {
            continue;
        }
        m->nleaves = 0;
        m->leaf[m->nleaves++] = na;
        if (b != NULL && m->nleaves < VRSPOT_LEAVES) {
            m->leaf[m->nleaves++] = nb;
        }
        {
            int k, n = 0;

            for (k = 0; k < m->nleaves && n < VRSPOT_PARTS; k++) {
                if (part_build(&m->part[n], m->leaf[k])) {
                    n++;
                }
            }
            if (n > 0) {
                m->nparts = n;
            } else {
                part_auto(m);   /* (the named leaves cannot be parts: the picked ones stay) */
            }
            m->c = clamp01(spot_read_c(m));
            m->closure = m->c;
            spot_apply(m);
        }
        {
            char room[16];

            spot_room_name(room, sizeof(room));
            spot_log("vrspots: %s using marker box, script box ignored\n", room);
        }
        spot_log_parts(m);
        return i;
    }
    /* no marker here: the typed box is the spot, and its leaves are the hands' own (ward_*) */
    for (i = 0; i < sNSpots; i++) {
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
    s->marker = 0;
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
    if (id >= sNSpots) {
        sNSpots = id + 1;   /* (the fallback spot is a spot too: the loops walk the array by count) */
    }
    {
        char room[16];

        spot_room_name(room, sizeof(room));
        spot_log("vrspots: %s typed box, %d leaves\n", room, s->nleaves);
    }
    return id;
}

/* ---- the spot's state: inside the BOUND, the closure's thresholds, the door's alpha ---- */

/* 1 when she is inside the spot's BOUND: her feet's footprint, and her hip's height (the box's
 * own bottom is above her feet, so the height is asked of the hip, generously) */
static int spot_bound_inside(const VrSpot *s, Vec3 feet) {
    Vec3 hip = vec3(feet.x, feet.y + VRSPOT_HIP, feet.z);

    return box_footprint(&s->bound, feet, 0.3f) && box_height(&s->bound, hip, 2.0f);
}

/* (DEBUG) her state at the spot changed: one line */
static void spot_state_log(const VrSpot *s) {
    char room[16];

    spot_room_name(room, sizeof(room));
    spot_log("vrspots: %s '%s' state inside=%d full=%d half=%d enclosed=%d c=%.3f\n", room, s->name, s->inside,
             s->full, s->half, s->enclosed, (double)s->c);
}

/* (DEBUG) a part's alpha changed: its layer, whether it is drawn, and every draw's own flags and
 * alpha ranges (the texture's alpha is not readable from here: the draw holds its GPU id) */
static void spot_fade_line(const VrSpot *s, int i, float a) {
    Placed *p = &cur_set()->p[s->part[i].id];
    int d;

    spot_log("vrspots: fade %s idx %d alpha %.2f layer %d shown %d draws %d\n", p->name, i, (double)a,
             placed_layer(p), p->shown, p->mesh.nd);
    for (d = 0; d < p->mesh.nd; d++) {
        const MeshDraw *x = &p->mesh.d[d];
        int k, n = x->count, amin = 255, amax = 0;

        for (k = x->first; k < x->first + x->count && k < p->mesh.nv; k++) {
            int va = p->mesh.v[k].rgba[3];

            if (va < amin) {
                amin = va;
            }
            if (va > amax) {
                amax = va;
            }
        }
        if (n <= 0 || amin > amax) {
            amin = amax = 0;
        }
        spot_log("vrspots: fade %s idx %d draw %d blend %d additive %d mask %d no_zwrite %d solid_tex %d group %d "
                 "tex alpha ?/? vertex alpha %d/%d\n",
                 p->name, i, d, x->blend, x->additive, x->mask, x->no_zwrite, x->solid_tex, x->group, amin, amax);
    }
}

/* (DEBUG) every part of that spot and its handle's place now: once per grip press near it */
void vrspot_log_edge(int id) {
    VrSpot *s;
    int k;

    if (!sDebug || id < 0 || id >= sNSpots || !sSpots[id].used) {
        return;
    }
    s = &sSpots[id];
    for (k = 0; k < s->nparts; k++) {
        Vec3 h = part_vertex(&s->part[k], s->part[k].handle, s->c);

        spot_log("vrspots: edge '%s' part %d %s handle(%.3f %.3f %.3f) c=%.3f\n", s->name, k,
                 cur_set()->p[s->part[k].id].name, (double)h.x, (double)h.y, (double)h.z, (double)s->c);
    }
}

/* a tick of a marker spot: a script's takeover and its end, the coast a let-go hand left, and every
 * part posed from the one closure */
static void spot_update(VrSpot *s) {
    int i, changed = 0, changed_morph = 0;

    for (i = 0; i < s->nparts; i++) {
        if (part_changed(s, i)) {
            changed = 1;
            if (s->part[i].morph) {
                changed_morph = 1;
            }
        }
    }
    if (!s->script_owned) {
        if (changed) {
            char room[16];

            spot_room_name(room, sizeof(room));
            s->script_owned = 1;
            s->script_morph = changed_morph;
            s->coast = 0;
            s->idle = 0;
            spot_log("vrspots: %s %s script owns it (%s)\n", room, s->name,
                     changed_morph ? "morph-words-changed" : "script-anim");
        }
    } else {
        int done = 1;

        s->idle = changed ? 0 : s->idle + 1;
        for (i = 0; i < s->nparts; i++) {
            VrPart *pt = &s->part[i];
            Placed *p = &cur_set()->p[pt->id];

            if (pt->morph) {
                if (s->idle < 2) {
                    done = 0;   /* (a morph: the script has stopped when its words rest) */
                }
            } else if (p->keys != NULL && !p->paused) {
                done = 0;   /* (its track still running: a loop never ends by itself) */
            }
        }
        if (done) {
            char room[16];

            s->script_owned = 0;
            s->idle = 0;
            s->c = clamp01(spot_read_c(s));
            spot_room_name(room, sizeof(room));
            spot_log("vrspots: %s %s script done, c %.2f\n", room, s->name, s->c);
        }
    }
    if (s->script_owned) {
        return;
    }
    if (s->coast) {
        s->c += s->coast_speed;
        if (s->c <= 0.0f) {
            s->c = 0.0f;
            s->coast = 0;
        } else if (s->c >= 1.0f) {
            s->c = 1.0f;
            s->coast = 0;
        }
    }
    s->closure = s->c;
    spot_apply(s);
}

/* the spot she is in or nearest: its inside and near set as it looks */
static int spot_pick(Engine *e, Vec3 at) {
    int i, best = -1;
    float best_d = 25.0f * 25.0f;

    (void)e;
    for (i = 0; i < sNSpots; i++) {
        VrSpot *s = &sSpots[i];

        if (!s->used) {
            continue;
        }
        if (s->marker) {
            float d;

            s->inside = spot_bound_inside(s, at);
            s->near = box_footprint(&s->bound, at, VRSPOT_NEAR) &&
                      (s->c <= VRSPOT_ENTER_MAX_CLOSURE || s->inside);
            if (s->inside) {
                return i;
            }
            d = box_gap(&s->bound, at);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        } else {
            float dx, dz, d;

            s->inside = spot_inside(s, at.x, at.y, at.z);
            s->near = spot_near(s, at.x, at.y, at.z);
            if (s->inside) {
                return i;
            }
            dx = at.x - s->ox;
            dz = at.z - s->oz;
            d = dx * dx + dz * dz;
            if (s->near && d < best_d) {
                best_d = d;
                best = i;
            }
        }
    }
    return best;
}

void vrspot_tick(Engine *e) {
    Actor *f;
    Vec3 at;
    int i, k, pick;

    if (e == NULL) {
        return;
    }
    spots_room(e);
    if (sReleased) {
        Actor *rf = fiona_actor(e);

        sActive = -1;
        if (rf != NULL) {
            sActive = spot_pick(e, sHereHave ? sHere : rf->pos);
        }
        for (i = 0; i < sNSpots; i++) {
            VrSpot *s = &sSpots[i];

            if (!s->used) {
                continue;
            }
            s->full = s->half = s->enclosed = 0;
            s->was_full = 0;
            if (s->marker) {
                for (k = 0; k < s->nparts; k++) {
                    cur_set()->p[s->part[k].id].alpha = 1.0f;
                }
            } else {
                for (k = 0; k < s->nleaves; k++) {
                    e->room.placed.p[s->leaf[k]].alpha = 1.0f;
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
    for (i = 0; i < sNSpots; i++) {
        if (sSpots[i].used && sSpots[i].marker) {
            spot_update(&sSpots[i]);   /* (the closure posed on every part, before her state is read) */
        }
    }
    for (i = 0; i < sNSpots && i < VRSPOT_MAX; i++) {   /* (her state as it was: the debug line's change) */
        sWasInside[i] = sSpots[i].inside;
        sWasFull[i] = sSpots[i].full;
        sWasHalf[i] = sSpots[i].half;
        sWasEnclosed[i] = sSpots[i].enclosed;
    }
    pick = spot_pick(e, at);
    sActive = pick;
    for (i = 0; i < sNSpots; i++) {
        VrSpot *s = &sSpots[i];
        float a;

        if (!s->used) {
            continue;
        }
        if (!s->marker) {   /* (a place's typed box, no marker: as it was) */
            float c = 1.0f;

            for (k = 0; k < s->nleaves; k++) {
                float lc = spot_leaf_closure(s, k, &e->room.placed.p[s->leaf[k]]);

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
            if (sDebug && i < VRSPOT_MAX &&
                (sWasInside[i] != s->inside || sWasFull[i] != s->full || sWasHalf[i] != s->half ||
                 sWasEnclosed[i] != s->enclosed)) {
                spot_state_log(s);
            }
            a = (i == pick && s->inside && s->full && gVrOptions.hide_door_see_through) ? 0.5f : 1.0f;
            for (k = 0; k < s->nleaves; k++) {
                e->room.placed.p[s->leaf[k]].alpha = a;
            }
            continue;
        }
        /* a marker spot: inside the BOUND, and the one closure's thresholds */
        s->closure = s->c;
        if (i == pick) {
            if (s->inside && (s->c >= 0.85f || (s->was_full && s->c >= 0.80f))) {
                s->full = 1;
                s->half = 0;
                s->was_full = 1;
            } else {
                s->full = 0;
                s->half = s->inside && s->c > 0.40f;
                if (!s->inside || s->c < 0.80f) {
                    s->was_full = 0;
                }
            }
            s->enclosed = s->inside && s->c >= 0.995f;
        } else {
            s->full = s->half = s->enclosed = 0;
            s->was_full = 0;
        }
        a = (i == pick && s->inside && s->full && gVrOptions.hide_door_see_through) ? 0.5f : 1.0f;
        if (sDebug && i < VRSPOT_MAX &&
            (sWasInside[i] != s->inside || sWasFull[i] != s->full || sWasHalf[i] != s->half ||
             sWasEnclosed[i] != s->enclosed)) {
            spot_state_log(s);
        }
        for (k = 0; k < s->nparts; k++) {
            Placed *pp = &cur_set()->p[s->part[k].id];
            float was = pp->alpha;

            pp->alpha = a;
            if (sDebug && was != a) {
                spot_fade_line(s, k, a);   /* (each time a part's alpha changes: the see-through check) */
            }
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

/* the marker spot's doorway rule: the BOUND's footprint is the room, and its edge may be crossed
 * only while the spot is open enough (VRSPOT_ENTER_MAX_CLOSURE); inside, she is kept a body's width
 * from the sides and the shut doorway */
static Vec3 spot_step_bound(const VrSpot *s, Vec3 at, Vec3 step) {
    Vec3 target = vec3_add(at, step);
    int in_at, in_to;

    if (s->c <= VRSPOT_ENTER_MAX_CLOSURE) {
        return step;   /* (open enough: her own step, as anywhere) */
    }
    in_at = box_footprint(&s->bound, at, 0.0f);
    in_to = box_footprint(&s->bound, target, 0.0f);
    if (in_at) {
        target = box_keep_in(&s->bound, target, VRSPOT_BODY_R);   /* (the sides, and the shut doorway) */
    } else if (in_to) {
        target = box_keep_out(&s->bound, at, target, VRSPOT_BODY_R);   /* (shut doors keep her out) */
    }
    target.y = at.y + step.y;
    return vec3_sub(target, at);
}

static Vec3 spot_step_id(Engine *e, int id, Vec3 at, Vec3 step) {
    VrSpot *s;
    PlacedSet *set;
    float u, v, u2, v2, u_raw, x, z;

    s = &sSpots[id];
    if (s->marker) {
        return spot_step_bound(s, at, step);
    }
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
    int id;

    spots_room(e);
    id = spot_pick(e, at);
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

/* ---- the hands' scrub of a marker spot ---- */

int vrspot_scrubbable(int id) {
    if (id < 0 || id >= sNSpots) {
        return 0;
    }
    return sSpots[id].used && sSpots[id].marker && sSpots[id].nparts > 0 && !sSpots[id].script_owned;
}

int vrspot_placed_part(int placed) {
    int i, k;

    for (i = 0; i < sNSpots; i++) {
        if (!sSpots[i].used || !sSpots[i].marker) {
            continue;
        }
        for (k = 0; k < sSpots[i].nparts; k++) {
            if (sSpots[i].part[k].id == placed) {
                return 1;
            }
        }
    }
    return 0;
}

int vrspot_grab_box(Vec3 at, float margin, float *dist) {
    int i, best = -1;
    float bd = 1.0e30f;

    for (i = 0; i < sNSpots; i++) {
        VrSpot *s = &sSpots[i];
        float d;

        if (!s->used || !s->marker || s->script_owned) {
            continue;
        }
        d = box_gap(&s->grab, at);
        if (d <= margin && (best < 0 || d < bd)) {
            best = i;
            bd = d;
        }
    }
    if (dist != NULL) {
        *dist = best >= 0 ? bd : -1.0f;
    }
    return best;
}

int vrspot_grab_edge(Vec3 at, float radius, float *dist, int *part) {
    int i, k, best = -1, bp = -1;
    float bd = radius;

    for (i = 0; i < sNSpots; i++) {
        VrSpot *s = &sSpots[i];
        float blo, bhi;

        if (!s->used || !s->marker || s->script_owned) {
            continue;
        }
        box_y_range(&s->bound, &blo, &bhi);
        for (k = 0; k < s->nparts; k++) {
            const VrPart *pt = &s->part[k];
            Vec3 lo, hi;
            float d, ylo, yhi;

            /* (the door's or the curtain's own height, grown by 2 - the hand reaches it anywhere
             * along it; the BOUND's height when the part has no bounds) */
            if (placed_bounds(&cur_set()->p[pt->id], &lo, &hi)) {
                ylo = lo.y - 2.0f;
                yhi = hi.y + 2.0f;
            } else {
                ylo = blo - 2.0f;
                yhi = bhi + 2.0f;
            }
            d = vrpath_edge_gap(pt->path, VRSPOT_PATH_N, s->c, at, ylo, yhi);   /* (x/z only) */
            if (d >= 0.0f && d <= bd) {
                bd = d;
                best = i;
                bp = k;
            }
        }
    }
    if (dist != NULL) {
        *dist = best >= 0 ? bd : -1.0f;
    }
    if (part != NULL) {
        *part = bp;
    }
    return best;
}

int vrspot_take(int id, Vec3 at, int *part, Vec3 *offset) {
    VrSpot *s;
    int i, best = -1;
    float bd = 1.0e30f;
    Vec3 h = at;

    if (!vrspot_scrubbable(id)) {
        return 0;
    }
    s = &sSpots[id];
    for (i = 0; i < s->nparts; i++) {
        Vec3 hi = part_vertex(&s->part[i], s->part[i].handle, s->c);
        float d = vec3_len(vec3_sub(at, hi));

        if (d < bd) {
            bd = d;
            best = i;
            h = hi;
        }
    }
    if (best < 0) {
        return 0;
    }
    if (part != NULL) {
        *part = best;
    }
    if (offset != NULL) {
        *offset = vec3_sub(at, h);   /* (nothing jumps to the hand) */
    }
    s->coast = 0;
    return 1;
}

float vrspot_scrub(int id, int part, Vec3 target, float c_now, float window) {
    if (id < 0 || id >= sNSpots || part < 0 || part >= sSpots[id].nparts) {
        return clamp01(c_now);
    }
    return vrpath_project(sSpots[id].part[part].path, VRSPOT_PATH_N, target, c_now, window);
}

void vrspot_set_closure(int id, float c) {
    VrSpot *s;

    if (id < 0 || id >= sNSpots || !sSpots[id].used || !sSpots[id].marker) {
        return;
    }
    s = &sSpots[id];
    s->c = clamp01(c);
    s->closure = s->c;
    if (!s->script_owned) {
        spot_apply(s);
    }
}

void vrspot_let_go(int id, float speed) {
    VrSpot *s;

    if (id < 0 || id >= sNSpots || !sSpots[id].used || !sSpots[id].marker) {
        return;
    }
    s = &sSpots[id];
    s->coast = fabsf(speed) >= VRSPOT_COAST;
    s->coast_speed = speed;
}

int vrspot_script_owned(int id) {
    return id >= 0 && id < sNSpots && sSpots[id].used && sSpots[id].script_owned;
}

const char *vrspot_script_reason(int id) {
    if (id < 0 || id >= sNSpots) {
        return "other";
    }
    return sSpots[id].script_morph ? "morph-words-changed" : "script-anim";
}

float vrspot_closure_of(int id) {
    return id >= 0 && id < sNSpots && sSpots[id].used ? sSpots[id].c : 0.0f;
}

const char *vrspot_name(int id) {
    return id >= 0 && id < sNSpots && sSpots[id].used ? sSpots[id].name : "";
}

void vrspot_debug(int on) { sDebug = on != 0; }
int vrspot_debug_on(void) { return sDebug; }

