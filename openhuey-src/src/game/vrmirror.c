/* The curtained mirrors a player draws by hand in VR: see vrmirror.h. The room's own words stay the
 * authority (the flag, the glass, the smash); this only moves the curtain's morph between its two
 * frames and says when one end has been reached. */
#include "vrmirror.h"

#include "../data/pac.h"
#include "engine.h"
#include "placed.h"
#include "vrmarkers.h"
#include "vrpath.h"
#include "vrspots.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct VrMirror {
    int used;
    int room;           /* the room it was registered in */
    char name[16];      /* the placed morph's name ("Curtain") */
    int obj;            /* its index in the room's placed set, -1 none */
    int n;              /* the definition's vertex count */
    int handle;         /* the vertex that travels furthest as c goes 0..1 */
    Vec3 path[VRMIRROR_PATH_N];
    float c;            /* the morph's t: 0 drawn back .. 1 drawn across */
    int rest;           /* the end its flag has: 0 bare, 1 covered (where a let-go springs to) */
    int spring;         /* easing to `rest` */
    float spring_from, spring_t;
    int fired;          /* 1 / 2 the last branch run (read once by the hand: its buzz) */
    int script_owned;   /* a script is playing the morph: hands off until it ends */
    int idle;           /* (a morph the script left alone: ticks unchanged) */
    /* the words as we left them: a script's change shows here */
    int wrote;
    uint32_t words[3];
    void *xt_ok, *xt_bare, *xt_cover;
} VrMirror;

static VrMirror sMirror[VRMIRROR_MAX];
static int sNMirrors;
static int sRoom = -2;
static int sDebug = 1;

static void mirror_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;

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

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static PlacedSet *cur_set(void) { return &gEngine.room.placed; }

/* a morph's weight w from its three words: (b == 1 ? t : 0) + (a == 1 ? 1 - t : 0) - the same
 * reading the hiding spots make (vrspots.c's morph_weight), so a curtain and a spot agree */
static float morph_weight(uint32_t a, uint32_t b, uint32_t t_bits) {
    float t = 0.0f;

    memcpy(&t, &t_bits, 4);
    return clamp01(((int)b == 1 ? t : 0.0f) + ((int)a == 1 ? 1.0f - t : 0.0f));
}

/* the room's PAC name (the log), as vrlevers.c has it */
static void mirror_room_name(char *out, size_t n) {
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

/* the placed object a mirror's name means here (its index), -1 none */
static int mirror_find(const char *name) {
    PlacedSet *set = cur_set();
    Placed *p = placed_named(set, name);

    return p != NULL ? (int)(p - set->p) : -1;
}

/* its vertex `v`'s room place at closure c: placed.c's own morph decode (the model's +0x60 start
 * included), so the curtain drawn and the edge grabbed are the same point */
static Vec3 mirror_vertex(const VrMirror *m, int v, float c) {
    Placed *p = &cur_set()->p[m->obj];
    Mat4 w;
    Vec3 a, b, mm;

    c = clamp01(c);
    if (!placed_morph_vertex(p, 0, v, &a) || !placed_morph_vertex(p, 1, v, &b)) {
        return p->pos;
    }
    mm = vec3(a.x + (b.x - a.x) * c, a.y + (b.y - a.y) * c, a.z + (b.z - a.z) * c);
    w = placed_world(p);
    return mat4_point(&w, mm);
}


/* its handle: the vertex whose room place travels furthest as c goes 0..1, and its path there */
static int mirror_path(VrMirror *m) {
    Vec3 prev, cur;
    float best = -1.0f;
    int v, k, handle = -1;

    for (v = 0; v < m->n; v++) {
        float travel = 0.0f;

        prev = mirror_vertex(m, v, 0.0f);
        for (k = 1; k < VRMIRROR_PATH_N; k++) {
            cur = mirror_vertex(m, v, (float)k / (float)(VRMIRROR_PATH_N - 1));
            travel += vec3_len(vec3_sub(cur, prev));
            prev = cur;
        }
        if (travel > best) {
            best = travel;
            handle = v;
        }
    }
    if (handle < 0 || best < VRMIRROR_TRAVEL) {
        return 0;
    }
    m->handle = handle;
    for (k = 0; k < VRMIRROR_PATH_N; k++) {
        m->path[k] = mirror_vertex(m, handle, (float)k / (float)(VRMIRROR_PATH_N - 1));
    }
    return 1;
}

/* the curtain's own world height at its current closure: the hand may reach it anywhere along it */
static void mirror_height(const VrMirror *m, float *lo, float *hi) {
    int v;

    *lo = 1.0e30f;
    *hi = -1.0e30f;
    for (v = 0; v < m->n; v++) {
        Vec3 w = mirror_vertex(m, v, m->c);

        if (w.y < *lo) {
            *lo = w.y;
        }
        if (w.y > *hi) {
            *hi = w.y;
        }
    }
    if (*lo > *hi) {   /* (nothing read: the handle's own place) */
        Vec3 h = mirror_vertex(m, m->handle >= 0 ? m->handle : 0, m->c);

        *lo = *hi = h.y;
    }
}

/* pose the curtain from the closure, keeping what we wrote (a script's change shows next tick) */
static void mirror_apply(VrMirror *m) {
    Placed *p = &cur_set()->p[m->obj];
    uint32_t t = 0;

    memcpy(&t, &m->c, 4);
    p->words[0x74 / 4] = 0;   /* (as kit.fs's `morph` and the places' `curtain`: a 0, b 1, t c) */
    p->words[0x78 / 4] = 1;
    p->words[0x7C / 4] = t;
    m->words[0] = p->words[0x74 / 4];
    m->words[1] = p->words[0x78 / 4];
    m->words[2] = p->words[0x7C / 4];
    m->wrote = 1;
}

/* 1 when a script has written the morph's words since we left them (judged by the weight, so our
 * own write - or a swap of a and b - is never a change) */
static int mirror_changed(const VrMirror *m) {
    Placed *p = &cur_set()->p[m->obj];
    float ww, wn;

    if (!m->wrote) {
        return 0;
    }
    ww = morph_weight(m->words[0], m->words[1], m->words[2]);
    wn = morph_weight(p->words[0x74 / 4], p->words[0x78 / 4], p->words[0x7C / 4]);
    return fabsf(wn - ww) > 0.01f;
}

/* the closure the morph's words show (what `mirror-shown` left it at) */
static float mirror_read_c(const VrMirror *m) {
    const Placed *p = &cur_set()->p[m->obj];

    return morph_weight(p->words[0x74 / 4], p->words[0x78 / 4], p->words[0x7C / 4]);
}

/* the room's own words: `ok? ( loud? -- why )` - 0 may be pulled now, 1 no time, 2 broken, 3 smashing */
static int mirror_ok_word(const VrMirror *m, int loud) {
    Forth *f = gEngine.forth;

    if (m->xt_ok == NULL || f == NULL) {
        return 0;   /* (no word of this room's: nothing blocks it) */
    }
    forth_push(f, loud ? -1 : 0);
    if (forth_call(f, (Word *)m->xt_ok) != 0) {
        return 3;   /* (a word that failed blocks: a curtain is not pulled on a broken room) */
    }
    return (int)forth_pop(f);
}

static void mirror_call(VrMirror *m, void *xt) {
    Forth *f = gEngine.forth;

    if (f == NULL || xt == NULL) {
        return;
    }
    if (forth_call(f, (Word *)xt) != 0) {
        mirror_log("vrmirror: '%s' a room word failed\n", m->name);
    }
}

static const char *mirror_why(int why) {
    switch (why) {
    case 1:
        return "no time";
    case 2:
        return "broken";
    case 3:
        return "smashing";
    default:
        return "other";
    }
}

/* (DEBUG) the registration line: where the handle sits at both ends, and what the room says of now */
static void mirror_log_registered(const VrMirror *m) {
    char room[16];
    Vec3 h0, h1;

    if (!sDebug) {
        return;
    }
    mirror_room_name(room, sizeof(room));
    h0 = mirror_vertex(m, m->handle, 0.0f);
    h1 = mirror_vertex(m, m->handle, 1.0f);
    mirror_log("vrmirror: %s '%s' registered c=%.2f bare=%d broken=%d handle c0 world(%.2f %.2f %.2f) "
               "c1 world(%.2f %.2f %.2f)\n",
               room, m->name, (double)m->c, m->rest == 0, mirror_ok_word(m, 0) == 2, (double)h0.x, (double)h0.y,
               (double)h0.z, (double)h1.x, (double)h1.y, (double)h1.z);
}

/* a tick of one mirror: a script's takeover and its end, the spring a let-go hand left, and the
 * curtain posed from the closure */
static void mirror_update(VrMirror *m) {
    Placed *p = &cur_set()->p[m->obj];
    char room[16];

    if (!p->shown) {
        return;   /* (the room has put it away: no curtain to pull) */
    }
    if (m->script_owned) {
        m->idle = mirror_changed(m) ? 0 : m->idle + 1;
        if (m->idle >= 2) {
            m->script_owned = 0;
            m->idle = 0;
            m->c = clamp01(mirror_read_c(m));
            m->rest = m->c >= 0.5f ? 1 : 0;
            m->spring = 0;
            if (sDebug) {
                mirror_room_name(room, sizeof(room));
                mirror_log("vrmirror: %s '%s' script done, c %.2f\n", room, m->name, (double)m->c);
            }
        }
        return;
    }
    if (mirror_changed(m)) {
        m->script_owned = 1;
        m->spring = 0;
        if (sDebug) {
            mirror_room_name(room, sizeof(room));
            mirror_log("vrmirror: %s '%s' script owns it\n", room, m->name);
        }
        return;
    }
    if (m->spring) {
        float target = m->rest ? 1.0f : 0.0f;

        m->spring_t += 1.0f / (float)VRMIRROR_SPRING_TICKS;
        if (m->spring_t >= 1.0f) {
            m->spring_t = 1.0f;
            m->spring = 0;
            m->c = target;
            if (sDebug) {
                mirror_room_name(room, sizeof(room));
                mirror_log("vrmirror: %s '%s' spring-back done c=%.2f\n", room, m->name, (double)m->c);
            }
        } else {
            m->c = m->spring_from + (target - m->spring_from) * m->spring_t;
        }
    }
    mirror_apply(m);
}

void vrmirror_reset(void) {
    memset(sMirror, 0, sizeof(sMirror));
    sNMirrors = 0;
}

int vrmirror_register(int room, const char *name, void *xt_ok, void *xt_bare, void *xt_cover) {
    VrMirror *m;
    Placed *p;
    char rname[16];
    int obj, i, ids[VRMARKERS_SPOTS];

    if (name == NULL || name[0] == 0) {
        return 0;
    }
    if (sRoom != room) {   /* (another room's curtain is not this one: the scripts register again) */
        vrmirror_reset();
        sRoom = room;
    }
    obj = mirror_find(name);
    if (obj < 0) {
        if (sDebug) {
            mirror_room_name(rname, sizeof(rname));
            mirror_log("vrmirror: %s '%s' no such object\n", rname, name);
        }
        return 0;
    }
    p = &cur_set()->p[obj];
    if (p->kind != 1 && p->kind != 3) {
        if (sDebug) {
            mirror_room_name(rname, sizeof(rname));
            mirror_log("vrmirror: %s '%s' skipped (kind %d, not a morph)\n", rname, p->name, p->kind);
        }
        return 0;
    }
    /* the hard rule: a curtain a hiding spot owns is never registered nor touched. The lookup is the
     * spots' own, read-only, and only asked when this room has a spot marker at all. */
    if (vrmarkers_spots_here(room, ids, VRMARKERS_SPOTS) > 0 && vrspot_placed_part(obj)) {
        mirror_room_name(rname, sizeof(rname));
        mirror_log("vrmirror: %s '%s' skipped (hiding spot)\n", rname, p->name);
        return 0;
    }
    for (i = 0; i < sNMirrors; i++) {   /* (came in again: the room's words and its pose refreshed) */
        if (sMirror[i].used && sMirror[i].room == room && sMirror[i].obj == obj) {
            m = &sMirror[i];
            m->xt_ok = xt_ok;
            m->xt_bare = xt_bare;
            m->xt_cover = xt_cover;
            m->wrote = 0;   /* (the room has just written it: their write is not a takeover) */
            m->script_owned = 0;
            m->spring = 0;
            m->c = clamp01(mirror_read_c(m));
            m->rest = m->c >= 0.5f ? 1 : 0;
            mirror_log_registered(m);
            return 1;
        }
    }
    if (sNMirrors >= VRMIRROR_MAX) {
        return 0;
    }
    m = &sMirror[sNMirrors];
    memset(m, 0, sizeof(*m));
    m->used = 1;
    m->room = room;
    m->obj = obj;
    snprintf(m->name, sizeof(m->name), "%s", p->name);
    m->n = p->def != NULL ? s32(p->def + 0x40) : 0;
    if (m->n <= 0 || m->n > 0x10000 || !mirror_path(m)) {
        mirror_room_name(rname, sizeof(rname));
        mirror_log("vrmirror: %s '%s' no vertex travels\n", rname, p->name);
        memset(m, 0, sizeof(*m));
        return 0;
    }
    m->c = clamp01(mirror_read_c(m));
    m->rest = m->c >= 0.5f ? 1 : 0;
    m->xt_ok = xt_ok;
    m->xt_bare = xt_bare;
    m->xt_cover = xt_cover;
    sNMirrors++;
    mirror_log_registered(m);
    return 1;
}

void vrmirror_tick(struct Engine *e) {
    int i;

    if (e == NULL) {
        return;
    }
    if (sRoom != e->room.id) {   /* (a room change with nothing registered: drop the old room's) */
        vrmirror_reset();
        sRoom = e->room.id;
    }
    for (i = 0; i < sNMirrors; i++) {
        if (sMirror[i].used && sMirror[i].obj >= 0 && sMirror[i].obj < cur_set()->n) {
            mirror_update(&sMirror[i]);
        }
    }
}


int vrmirror_scrubbable(int id) {
    Placed *p;

    if (id < 0 || id >= sNMirrors || !sMirror[id].used || sMirror[id].obj < 0) {
        return 0;
    }
    p = &cur_set()->p[sMirror[id].obj];
    return p->shown && !sMirror[id].script_owned;
}

int vrmirror_placed(int placed) {
    int i;

    for (i = 0; i < sNMirrors; i++) {
        if (sMirror[i].used && sMirror[i].obj == placed) {
            return 1;
        }
    }
    return 0;
}

int vrmirror_grab_edge(Vec3 at, float radius, float *dist) {
    int i, best = -1;
    float bd = radius;

    for (i = 0; i < sNMirrors; i++) {
        VrMirror *m = &sMirror[i];
        Vec3 h;
        float dx, dz, d, lo, hi;

        if (!vrmirror_scrubbable(i)) {
            continue;
        }
        h = mirror_vertex(m, m->handle, m->c);
        mirror_height(m, &lo, &hi);
        if (at.y < lo - VRMIRROR_HEIGHT || at.y > hi + VRMIRROR_HEIGHT) {
            continue;   /* (over or under the curtain: nothing to take) */
        }
        dx = at.x - h.x;
        dz = at.z - h.z;
        d = sqrtf(dx * dx + dz * dz);   /* (x/z only: the edge follows the curtain, at any height) */
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

int vrmirror_ok(int id, int loud) {
    if (id < 0 || id >= sNMirrors || !sMirror[id].used) {
        return 0;
    }
    return mirror_ok_word(&sMirror[id], loud);
}

int vrmirror_take(int id, Vec3 at, Vec3 *offset) {
    VrMirror *m;
    Vec3 h;
    int why;

    if (!vrmirror_scrubbable(id)) {
        return 0;
    }
    m = &sMirror[id];
    why = mirror_ok_word(m, 1);   /* (loud: the room shows its no-time remark once per press) */
    if (why != 0) {
        if (sDebug) {
            char room[16];

            mirror_room_name(room, sizeof(room));
            mirror_log("vrmirror: %s '%s' blocked: %s c=%.2f\n", room, m->name, mirror_why(why), (double)m->c);
        }
        return 0;
    }
    h = mirror_vertex(m, m->handle, m->c);
    if (offset != NULL) {
        *offset = vec3_sub(at, h);   /* (nothing jumps to the hand) */
    }
    m->spring = 0;
    return 1;
}

float vrmirror_scrub(int id, Vec3 target, float c_now, float window) {
    if (id < 0 || id >= sNMirrors) {
        return clamp01(c_now);
    }
    return vrpath_project(sMirror[id].path, VRMIRROR_PATH_N, target, c_now, window);
}

int vrmirror_set(int id, float c) {
    VrMirror *m;

    if (id < 0 || id >= sNMirrors || !vrmirror_scrubbable(id)) {
        return 0;
    }
    m = &sMirror[id];
    m->c = clamp01(c);
    m->spring = 0;
    mirror_apply(m);
    /* once per crossing: covered and drawn back past the low threshold, or bare and drawn across
     * past the high one - the flag only changes here, so the stalker logic sees a clean state */
    if (m->rest == 1 && m->c <= VRMIRROR_BARE_AT) {
        m->rest = 0;
        m->fired = 1;
        if (sDebug) {
            char room[16];

            mirror_room_name(room, sizeof(room));
            mirror_log("vrmirror: %s '%s' fire bare c=%.2f\n", room, m->name, (double)m->c);
        }
        mirror_call(m, m->xt_bare);
        return 1;
    }
    if (m->rest == 0 && m->c >= VRMIRROR_COVER_AT) {
        m->rest = 1;
        m->fired = 2;
        if (sDebug) {
            char room[16];

            mirror_room_name(room, sizeof(room));
            mirror_log("vrmirror: %s '%s' fire cover c=%.2f\n", room, m->name, (double)m->c);
        }
        mirror_call(m, m->xt_cover);
        return 2;
    }
    return 0;
}

int vrmirror_fire_take(int id) {
    int f;

    if (id < 0 || id >= sNMirrors) {
        return 0;
    }
    f = sMirror[id].fired;
    sMirror[id].fired = 0;
    return f;
}

void vrmirror_let_go(int id) {
    VrMirror *m;
    float target;

    if (id < 0 || id >= sNMirrors || !sMirror[id].used || sMirror[id].script_owned) {
        return;
    }
    m = &sMirror[id];
    target = m->rest ? 1.0f : 0.0f;
    if (fabsf(m->c - target) <= VRMIRROR_AT_END) {
        m->c = target;
        m->spring = 0;
        mirror_apply(m);
        return;
    }
    m->spring = 1;
    m->spring_from = m->c;
    m->spring_t = 0.0f;
    if (sDebug) {
        char room[16];

        mirror_room_name(room, sizeof(room));
        mirror_log("vrmirror: %s '%s' spring-back from c=%.2f to %.0f\n", room, m->name, (double)m->c,
                   (double)target);
    }
}

float vrmirror_closure(int id) { return id >= 0 && id < sNMirrors ? sMirror[id].c : 0.0f; }
int vrmirror_script_owned(int id) { return id >= 0 && id < sNMirrors && sMirror[id].script_owned; }
const char *vrmirror_name(int id) { return id >= 0 && id < sNMirrors ? sMirror[id].name : ""; }

void vrmirror_debug(int on) { sDebug = on != 0; }
int vrmirror_debug_on(void) { return sDebug; }

void vrmirror_log_take(int id, Vec3 at) {
    char room[16];

    if (!sDebug || id < 0 || id >= sNMirrors) {
        return;
    }
    mirror_room_name(room, sizeof(room));
    mirror_log("vrmirror: %s '%s' take c=%.2f hand=(%.2f %.2f %.2f)\n", room, sMirror[id].name,
               (double)sMirror[id].c, (double)at.x, (double)at.y, (double)at.z);
}

void vrmirror_log_release(int id, const char *why) {
    char room[16];

    if (!sDebug || id < 0 || id >= sNMirrors) {
        return;
    }
    mirror_room_name(room, sizeof(room));
    mirror_log("vrmirror: %s '%s' release reason=%s c=%.2f\n", room, sMirror[id].name, why != NULL ? why : "other",
               (double)sMirror[id].c);
}

