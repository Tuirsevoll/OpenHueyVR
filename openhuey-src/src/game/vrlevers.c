/* The levers a player pulls by hand in VR: see vrlevers.h. A lever's box names a placed object
 * near it; its track 0 is scrubbed from the end its own pose sits at (the rest) to the other (the
 * pulled pose), one closure c, by the hand that holds it. */
#include "vrlevers.h"
#include "../platform/vrlog.h"

#include "../data/pac.h"
#include "engine.h"
#include "placed.h"
#include "vrmarkers.h"
#include "vrpath.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* the end of a track that matches the object's own pose: radians and room units */
#define VRLEVER_SAME 0.05f

typedef struct VrLever {
    int used;
    int from;           /* the marker's index (vrmarkers) */
    char name[32];
    VrBox box;
    int obj;            /* the placed object, -1 none */
    int lo, hi;         /* the rest frame (c = 0) and the pulled one (c = 1) */
    int handle;         /* the vertex that travels furthest as c goes 0..1 */
    Vec3 path[VRLEVER_PATH_N];
    float c;            /* 0 rest .. 1 pulled */
    int done;           /* pulled home: it cannot be grabbed again */
    int event;          /* the pull waiting for the place script (`vr-lever-pulled?` reads it) */
    int script_owned;   /* a script is playing the object: hands off until it ends */
    int spring;         /* let go before home: easing back to rest */
    float spring_from, spring_t;
    /* the object's state as we left it: a script's change shows here */
    const uint8_t *keys;
    int frames, frame, paused, wrote;
} VrLever;

static VrLever sLever[VRLEVER_MAX];
static int sNLevers;
static int sRoom = -2;
static int sDebug = 1;

static void lever_log(const char *fmt, ...) {
    char msg[400];
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

static PlacedSet *cur_set(void) { return &gEngine.room.placed; }

/* the room's name as the markers have it (the log; defined below) */
static void lever_room_name(char *out, size_t n);

/* how far apart two turns are, each part wrapped: the largest */
static float rot_gap(Vec3 a, Vec3 b) {
    float dx = fabsf(wrap_rad(a.x - b.x)), dy = fabsf(wrap_rad(a.y - b.y)), dz = fabsf(wrap_rad(a.z - b.z));

    return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
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

/* how near the object's bounds come to the box: the nearest of its vertices (0 inside) */
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

/* the frames a track turns through: its two flat ends are the stops (as vrspots' part_span) */
static int lever_span(const Placed *p, int *lo, int *hi) {
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

/* how far a pose is from the object's own (its turn, and a place when the format has one) */
static float pose_gap(Vec3 rot, Vec3 pos, const Placed *p) {
    float d = rot_gap(rot, p->rot);

    if (p->format == 7 || p->format == 9) {
        d += vec3_len(vec3_sub(pos, p->pos)) * 0.05f;
    }
    return d;
}

/* ---- the lever's object: its pose, its handle's path, and the state it is left at ---- */

/* its vertex `v`'s room place at closure c */
static Vec3 lever_vertex(const VrLever *l, int v, float c) {
    Placed *p = &cur_set()->p[l->obj];
    Placed at = *p;
    Vec3 rot, pos;
    Mat4 w;

    placed_pose_at(p, (float)l->lo + clamp01(c) * (float)(l->hi - l->lo), &rot, &pos);
    at.rot = rot;
    at.pos = pos;
    w = placed_world(&at);
    return mat4_point(&w, vec3(p->mesh.v[v].x, p->mesh.v[v].y, p->mesh.v[v].z));
}

/* its handle: the vertex whose room place travels furthest as c goes 0..1, and its path there.
 * 0 when nothing of it moves enough to be a lever. */
static int lever_path(VrLever *l) {
    Placed *p = &cur_set()->p[l->obj];
    Vec3 prev, cur;
    float best = -1.0f;
    int v, k, handle = -1;

    if (p->mesh.nv <= 0) {
        return 0;
    }
    for (v = 0; v < p->mesh.nv; v++) {
        float travel = 0.0f;

        prev = lever_vertex(l, v, 0.0f);
        for (k = 1; k < VRLEVER_PATH_N; k++) {
            cur = lever_vertex(l, v, (float)k / (float)(VRLEVER_PATH_N - 1));
            travel += vec3_len(vec3_sub(cur, prev));
            prev = cur;
        }
        if (travel > best) {
            best = travel;
            handle = v;
        }
    }
    if (handle < 0 || best < VRLEVER_TRAVEL) {
        return 0;
    }
    l->handle = handle;
    for (k = 0; k < VRLEVER_PATH_N; k++) {
        l->path[k] = lever_vertex(l, handle, (float)k / (float)(VRLEVER_PATH_N - 1));
    }
    return 1;
}

/* pose the object from the closure, keeping its state (a script's change shows next tick) */
static void lever_apply(VrLever *l) {
    Placed *p = &cur_set()->p[l->obj];

    placed_frame_set_f(cur_set(), p, (float)l->lo + l->c * (float)(l->hi - l->lo));
    l->keys = p->keys;
    l->frames = p->frames;
    l->frame = p->frame;
    l->paused = p->paused;
    l->wrote = 1;
}

/* 1 when a script has played the object since we left it */
static int lever_changed(const VrLever *l) {
    Placed *p = &cur_set()->p[l->obj];

    if (!l->wrote) {
        return 0;
    }
    return p->keys != l->keys || p->frames != l->frames || !p->paused || p->frame != l->frame;
}

/* the closure the object's own pose shows (a script left it there): -1 when it cannot be read */
static float lever_read_c(const VrLever *l) {
    Placed *p = &cur_set()->p[l->obj];
    Placed own = *p;
    int n, best = -1;
    float bd = 1.0e30f;

    if (l->hi == l->lo) {
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
    return clamp01(((float)best - (float)l->lo) / (float)(l->hi - l->lo));
}

/* the placed object a lever's box names: the nearest shown one within reach with a track 0 */
static void lever_find_object(VrLever *l) {
    PlacedSet *set = cur_set();
    int i, best = -1;
    float bd = VRLEVER_REACH;

    l->obj = -1;
    for (i = 0; i < set->n; i++) {
        Placed *p = &set->p[i];
        Placed own = *p;
        float d;

        if (!p->shown || p->mesh.nv <= 0) {
            continue;
        }
        d = placed_gap(p, &l->box);
        if (d > bd) {
            continue;
        }
        placed_anim(set, &own, 0, 0);
        if (own.keys == NULL || own.frames <= 0) {
            continue;
        }
        bd = d;
        best = i;
    }
    if (best < 0) {
        return;
    }
    {
        Placed *p = &set->p[best];
        Placed own = *p;
        int lo = 0, hi = 0, rest, pulled;
        Vec3 rot, pos;
        float dlo, dhi;

        l->obj = best;
        placed_anim(set, &own, 0, 0);   /* (a copy: its own track 0) */
        if (!lever_span(&own, &lo, &hi)) {
            lo = 0;
            hi = own.frames - 1;   /* (no flat ends: the first and last frames) */
        }
        /* the rest pose is the end the object's own pose sits at (normally the first) */
        placed_pose_at(&own, (float)lo, &rot, &pos);
        dlo = pose_gap(rot, pos, p);
        placed_pose_at(&own, (float)hi, &rot, &pos);
        dhi = pose_gap(rot, pos, p);
        if (dhi < dlo) {
            rest = hi;
            pulled = lo;
        } else {
            rest = lo;
            pulled = hi;
        }
        l->lo = rest;
        l->hi = pulled;
        placed_anim(set, p, 0, 0);   /* (the lever drives it: its own track, held from c) */
        if (!lever_path(l)) {
            l->obj = -1;   /* (nothing of it moves: no lever to pull) */
            return;
        }
        l->c = clamp01(lever_read_c(l));
        if (l->c >= 0.9f) {
            l->done = 1;   /* (found pulled: the room's own state) */
        }
        lever_apply(l);
    }
}

static void lever_log_object(const VrLever *l) {
    char room[16];

    lever_room_name(room, sizeof(room));
    if (l->obj < 0) {
        lever_log("vrlevers: %s '%s' -> no object found\n", room, l->name);
        return;
    }
    {
        Placed *p = &cur_set()->p[l->obj];

        lever_log("vrlevers: %s '%s' -> object %s, track 0 frames %d format %d\n", room, l->name, p->name, p->frames,
                  p->format);
    }
}

/* a tick of one lever: a script's takeover and its end, the spring a let-go hand left, and the
 * object posed from the closure */
static void lever_update(VrLever *l) {
    Placed *p = &cur_set()->p[l->obj];

    if (!p->shown) {
        return;   /* (the room has put it away: the gate open, the breaker down) */
    }
    if (l->script_owned) {
        if (p->keys == NULL || p->paused) {
            l->script_owned = 0;
            l->c = clamp01(lever_read_c(l));
            if (l->c >= 0.9f) {
                l->done = 1;
            }
        }
        return;
    }
    if (lever_changed(l)) {
        char room[16];

        lever_room_name(room, sizeof(room));
        l->script_owned = 1;
        l->spring = 0;
        lever_log("vrlevers: %s '%s' script owns it\n", room, l->name);
        return;
    }
    if (l->spring) {
        l->spring_t += 1.0f / (float)VRLEVER_SPRING_TICKS;
        if (l->spring_t >= 1.0f) {
            l->spring_t = 1.0f;
            l->spring = 0;
            l->c = 0.0f;
        } else {
            l->c = l->spring_from * (1.0f - l->spring_t);
        }
    }
    lever_apply(l);
}

static void levers_room(struct Engine *e) {
    if (e == NULL || sRoom == e->room.id) {
        return;
    }
    vrlevers_reset();
    sRoom = e->room.id;
    {
        int ids[VRMARKERS_LEVERS], n, i;

        n = vrmarkers_levers_here(gEngine.room.id, ids, VRMARKERS_LEVERS);
        for (i = 0; i < n && sNLevers < VRLEVER_MAX; i++) {
            const VrBox *box = vrmarkers_lever_box(ids[i]);
            const char *name = vrmarkers_lever_name(ids[i]);
            VrLever *l;

            if (box == NULL) {
                continue;
            }
            l = &sLever[sNLevers++];
            memset(l, 0, sizeof(*l));
            l->used = 1;
            l->from = ids[i];
            l->box = *box;
            snprintf(l->name, sizeof(l->name), "%s", name != NULL ? name : "");
            l->obj = -1;
            lever_find_object(l);
            lever_log_object(l);
        }
    }
}

void vrlevers_tick(struct Engine *e) {
    int i;

    if (e == NULL) {
        return;
    }
    levers_room(e);
    for (i = 0; i < sNLevers; i++) {
        if (sLever[i].used && sLever[i].obj >= 0) {
            lever_update(&sLever[i]);
        }
    }
}

static void lever_room_name(char *out, size_t n) {
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
/* ---- the hands' pull, and the place scripts' words ---- */

void vrlevers_reset(void) {
    memset(sLever, 0, sizeof(sLever));
    sNLevers = 0;
}

int vrlever_scrubbable(int id) {
    if (id < 0 || id >= sNLevers) {
        return 0;
    }
    return sLever[id].used && sLever[id].obj >= 0 && !sLever[id].done && !sLever[id].script_owned;
}

int vrlever_placed(int placed) {
    int i;

    for (i = 0; i < sNLevers; i++) {
        if (sLever[i].used && sLever[i].obj == placed) {
            return 1;
        }
    }
    return 0;
}

int vrlever_grab_box(Vec3 at, float margin, float *dist) {
    int i, best = -1;
    float bd = 1.0e30f;

    for (i = 0; i < sNLevers; i++) {
        float d;

        if (!vrlever_scrubbable(i)) {
            continue;
        }
        d = box_gap(&sLever[i].box, at);
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

int vrlever_grab_edge(Vec3 at, float radius, float *dist) {
    int i, best = -1;
    float bd = radius;

    for (i = 0; i < sNLevers; i++) {
        Vec3 h;
        float d;

        if (!vrlever_scrubbable(i)) {
            continue;
        }
        h = lever_vertex(&sLever[i], sLever[i].handle, sLever[i].c);
        d = vec3_len(vec3_sub(at, h));
        if (d <= bd) {
            bd = d;
            best = i;
        }
    }
    if (dist != NULL) {
        *dist = best >= 0 ? bd : -1.0f;
    }
    return best;
}

int vrlever_take(int id, Vec3 at, Vec3 *offset) {
    VrLever *l;
    Vec3 h;

    if (!vrlever_scrubbable(id)) {
        return 0;
    }
    l = &sLever[id];
    h = lever_vertex(l, l->handle, l->c);
    if (offset != NULL) {
        *offset = vec3_sub(at, h);   /* (nothing jumps to the hand) */
    }
    l->spring = 0;
    return 1;
}

float vrlever_scrub(int id, Vec3 target, float c_now, float window) {
    if (id < 0 || id >= sNLevers) {
        return clamp01(c_now);
    }
    return vrpath_project(sLever[id].path, VRLEVER_PATH_N, target, c_now, window);
}

void vrlever_set(int id, float c) {
    VrLever *l;

    if (id < 0 || id >= sNLevers) {
        return;
    }
    l = &sLever[id];
    if (l->obj < 0 || l->done || l->script_owned) {
        return;
    }
    l->c = clamp01(c);
    l->spring = 0;
    lever_apply(l);
}

void vrlever_let_go(int id) {
    VrLever *l;

    if (id < 0 || id >= sNLevers) {
        return;
    }
    l = &sLever[id];
    if (l->obj < 0 || l->done || l->script_owned) {
        return;
    }
    if (l->c <= 0.0f) {
        l->spring = 0;
        return;
    }
    l->spring = 1;   /* (a spring-loaded lever: back to rest over about 0.3 s) */
    l->spring_from = l->c;
    l->spring_t = 0.0f;
}

void vrlever_complete(int id) {
    VrLever *l;

    if (id < 0 || id >= sNLevers) {
        return;
    }
    l = &sLever[id];
    if (l->obj < 0 || l->done) {
        return;
    }
    l->c = 1.0f;
    l->done = 1;
    l->spring = 0;
    l->event = 1;   /* (the place script's `vr-lever-pulled?` reads it once) */
    lever_apply(l);
    {
        char room[16];

        lever_room_name(room, sizeof(room));
        lever_log("vrlevers: %s '%s' pulled\n", room, l->name);
    }
}

int vrlever_done_q(int id) { return id >= 0 && id < sNLevers && sLever[id].done; }
int vrlever_script_owned(int id) { return id >= 0 && id < sNLevers && sLever[id].script_owned; }
float vrlever_closure(int id) { return id >= 0 && id < sNLevers ? sLever[id].c : 0.0f; }
const char *vrlever_name(int id) { return id >= 0 && id < sNLevers ? sLever[id].name : ""; }

/* the lever called `name` (that many characters), NULL none */
static VrLever *lever_by_name(const char *name, int len) {
    int i;

    if (name == NULL || len <= 0) {
        return NULL;
    }
    for (i = 0; i < sNLevers; i++) {
        VrLever *l = &sLever[i];

        if (!l->used || (int)strlen(l->name) != len) {
            continue;
        }
        if (strncmp(l->name, name, (size_t)len) != 0) {
            continue;
        }
        return l;
    }
    return NULL;
}

int vrlever_here_q(const char *name, int len) {
    VrLever *l = lever_by_name(name, len);

    return l != NULL && l->obj >= 0;
}

int vrlever_pulled_take(const char *name, int len) {
    VrLever *l = lever_by_name(name, len);

    if (l == NULL || !l->event) {
        return 0;
    }
    l->event = 0;
    return 1;
}

/* (a console/test tool: the lever pulled home as a hand would, its event queued) */
int vrlever_pull(const char *name, int len) {
    VrLever *l = lever_by_name(name, len);

    if (l == NULL || !vrlever_scrubbable((int)(l - sLever))) {
        return 0;
    }
    vrlever_complete((int)(l - sLever));
    return 1;
}

void vrlever_debug(int on) { sDebug = on != 0; }
int vrlever_debug_on(void) { return sDebug; }

