/* DEBUG-ONLY: the VR hiding spot probe: see vrspotprobe.h. It reads only - a placed object's own
 * animation is never started, stopped or stepped; every pose here comes from a copy of it (placed_anim
 * on `Placed at = *p`), and a morph's vertices from a scratch buffer with the same blend maths as
 * placed.c's morph_fill. Remove this file, its CMakeLists.txt line and its call in engine_tick
 * when the hiding rework is done. */
#include "vrspotprobe.h"
#include "../platform/vrlog.h"

#include "vrmarkers.h"
#include "vrspots.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* how near an object's bounds must be to a spot's GRAB box to count as one of its doors (units) */
#define PROBE_NEAR 4.0f
/* how near for the object's tracks / morph to be dumped: the mod's GRAB box is the hand's plate, and
 * a wardrobe's other panel (4.6 here) or a curtain hung round the spot (10.4) sits further out */
#define PROBE_DUMP 12.0f
/* how near Fiona must be to a spot's BOUND box for a feet line (units) */
#define PROBE_FEET 10.0f
/* the hip point above her feet (room +y is up) */
#define PROBE_HIP 8.0f
/* a pose equal to the object's defined one: radians and room units */
#define PROBE_SAME 1.0e-3f

static int sRoom = -2;       /* (the room whose spots were logged; -2 none yet) */
static long sFeetTick = -1;  /* (the tick of the last feet line) */
static Vec3 sHere;           /* (the scripts' her-at, as vrspot_here was given it) */
static int sHereHave;

static void probe_log(const char *fmt, ...) {
    char msg[640];
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

static int32_t prd32(const uint8_t *p) {
    int32_t x;

    memcpy(&x, p, 4);
    return x;
}

static int16_t prd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static void prdf3(Vec3 *v, const uint8_t *p) {
    memcpy(&v->x, p, 4);
    memcpy(&v->y, p + 4, 4);
    memcpy(&v->z, p + 8, 4);
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

/* how far the point is from the box, in the box's own axes (0 inside or touching) */
static float point_gap(Vec3 p, const VrBox *b) {
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

/* how far the object's bounds come to the box: the nearest of its vertices, in the box's own axes
 * (0 when one is inside or touching). A morph's vertices are its frame 0's, as its mesh holds. */
static float grab_gap(const Placed *p, const VrBox *b) {
    Mat4 w = placed_world(p);
    float best = 1.0e30f;
    int i;

    if (p->mesh.nv <= 0 || p->mesh.v == NULL) {
        return best;
    }
    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = mat4_point(&w, vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z));
        float d = point_gap(v, b);

        if (d < best) {
            best = d;
        }
    }
    return best;
}

/* a frame of a track: its turn, and its place (formats 7 and 9 carry it; else the defined one) */
typedef struct ProbeKey {
    Vec3 rot, pos;
    int pos_from_key;
} ProbeKey;

/* the turn (and, formats 7 / 9, the place) a frame of the track gives: as vrhands.c's ward_key,
 * read the way placed.c's placed_frame_set does (9 is the place) */
static ProbeKey probe_key(const Placed *p, int frame) {
    ProbeKey k;

    k.rot = vec3(0.0f, 0.0f, 0.0f);
    k.pos = p->def_pos;
    k.pos_from_key = 0;
    if (p->keys == NULL || p->frames <= 0) {
        return k;
    }
    if (frame < 0) {
        frame = 0;
    }
    if (frame >= p->frames) {
        frame = p->frames - 1;
    }
    if (p->format == 7) {
        prdf3(&k.rot, p->keys + frame * 24);
        prdf3(&k.pos, p->keys + frame * 24 + 12);
        k.pos_from_key = 1;
    } else if (p->format == 8) {
        prdf3(&k.rot, p->keys + frame * 12);
    } else if (p->format == 9) {
        prdf3(&k.pos, p->keys + frame * 12);
        k.pos_from_key = 1;
    }
    return k;
}

/* how far apart two turns are: the largest part that differs (vrhands.c's ward_gap) */
static float probe_rot_gap(Vec3 a, Vec3 b) {
    float dx = fabsf(a.x - b.x), dy = fabsf(a.y - b.y), dz = fabsf(a.z - b.z);

    return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
}

/* the turn's part that differs, wrapped to a turn: a track's 3.927 is the same turn as -2.356 */
static float probe_ang_gap(float a, float b) { return fabsf(remainderf(a - b, 6.28318531f)); }

/* 1 when two poses are the same to PROBE_SAME: the turn (wrapped), and the place when a track gives it */
static int probe_same(const ProbeKey *a, const ProbeKey *b) {
    if (probe_ang_gap(a->rot.x, b->rot.x) >= PROBE_SAME || probe_ang_gap(a->rot.y, b->rot.y) >= PROBE_SAME ||
        probe_ang_gap(a->rot.z, b->rot.z) >= PROBE_SAME) {
        return 0;
    }
    if (a->pos_from_key || b->pos_from_key) {
        return fabsf(a->pos.x - b->pos.x) < PROBE_SAME && fabsf(a->pos.y - b->pos.y) < PROBE_SAME &&
               fabsf(a->pos.z - b->pos.z) < PROBE_SAME;
    }
    return 1;
}

/* the frames the track turns through: its two flat ends are the stops (vrhands.c's ward_span) */
static int probe_span(const Placed *p, int *lo, int *hi) {
    int a, b;

    if (p->keys == NULL || (p->format != 7 && p->format != 8) || p->frames < 3) {
        return 0;
    }
    for (a = 1; a < p->frames - 1 && probe_rot_gap(probe_key(p, a).rot, probe_key(p, 0).rot) < 1.0e-3f; a++) {
    }
    for (b = p->frames - 2; b > 0 && probe_rot_gap(probe_key(p, b).rot, probe_key(p, p->frames - 1).rot) < 1.0e-3f;
         b--) {
    }
    if (b - a < 2) {
        return 0;
    }
    *lo = a - 1;
    *hi = b + 1;
    return 1;
}

/* the object's animation record in the room's section 15 (as placed_anim finds it), NULL none */
static const uint8_t *anim_record(const PlacedSet *set, const Placed *p) {
    uint32_t n, j;

    if (set->anims == NULL || set->anims_size < 4) {
        return NULL;
    }
    n = (uint32_t)prd32(set->anims);
    for (j = 0; j < n && 4 + (size_t)(j + 1) * 4 <= set->anims_size; j++) {
        uint32_t o = (uint32_t)prd32(set->anims + 4 + j * 4);
        const uint8_t *e = set->anims + o;

        if (o + 0x18 > set->anims_size || strncmp((const char *)e, p->name, 16) != 0) {
            continue;
        }
        return e;
    }
    return NULL;
}

/* a morph's n vertices at frames a and b blended by t, model space: placed.c's own decode of a frame
 * vertex (placed_morph_vertex, with the model's +0x60 start), so the probe reads what is drawn (the
 * colours and the UVs are not needed here) */
static int morph_positions(const Placed *p, int a, int b, float t, MeshVertex *v, int n) {
    float w = 1.0f - t;
    int i;

    for (i = 0; i < n; i++) {
        Vec3 va, vb;

        memset(&v[i], 0, sizeof(v[i]));
        if (!placed_morph_vertex(p, a, i, &va) || !placed_morph_vertex(p, b, i, &vb)) {
            return 0;
        }
        v[i].x = va.x * w + vb.x * t;
        v[i].y = va.y * w + vb.y * t;
        v[i].z = va.z * w + vb.z * t;
    }
    return 1;
}

/* the morph frames the definition holds: the table at +0x70 is 8 bytes a frame (a colour offset,
 * then a position offset) and its data follows it, so the smallest first offset is its end */
static int morph_frame_count(const Placed *p) {
    const uint8_t *def = p->def;
    int32_t a, b;
    size_t first, size;

    if (def == NULL || p->def_end == NULL) {
        return 0;
    }
    size = (size_t)(p->def_end - def);
    if (size < 0x80) {
        return 0;
    }
    a = prd32(def + 0x70);
    b = prd32(def + 0x74);
    if (a < 0 || b < 0) {
        return 0;
    }
    first = (size_t)(a < b ? a : b);
    if (first <= 0x70 || first > size || (first - 0x70) % 8 != 0) {
        return 0;
    }
    return (int)((first - 0x70) / 8);
}

static const Actor *fiona_actor(const Engine *e) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        if (e->actors[i].used && strncmp(e->actors[i].name, "O_FI", 4) == 0) {
            return &e->actors[i];
        }
    }
    return NULL;
}

/* the vertex that travels furthest through the swing (its frame-to-frame steps added up), and its
 * room place every 4th frame of the swing, with its total travel */
static void probe_swing(const Placed *p, int lo, int hi, int index, int id) {
    int nf = hi - lo + 1, nv = p->mesh.nv, k, f, vi = -1;
    Mat4 *w;
    float *path, best = -1.0f;
    Vec3 v;

    if (nf < 2 || nv <= 0 || (w = malloc((size_t)nf * sizeof(*w))) == NULL) {
        return;
    }
    path = calloc((size_t)nv, sizeof(*path));
    if (path == NULL) {
        free(w);
        return;
    }
    for (k = 0; k < nf; k++) {   /* every frame of the swing, posed on a copy of the object */
        Placed c = *p;
        ProbeKey kk = probe_key(p, lo + k);

        c.rot = kk.rot;
        c.pos = kk.pos;
        w[k] = placed_world(&c);
    }
    for (k = 0; k < nv; k++) {
        Vec3 mv = vec3(p->mesh.v[k].x, p->mesh.v[k].y, p->mesh.v[k].z);
        Vec3 a = mat4_point(&w[0], mv);

        for (f = 1; f < nf; f++) {
            Vec3 b = mat4_point(&w[f], mv);

            path[k] += vec3_len(vec3_sub(b, a));
            a = b;
        }
        if (path[k] > best) {
            best = path[k];
            vi = k;
        }
    }
    if (vi < 0) {
        free(path);
        free(w);
        return;
    }
    probe_log("VRSPOTPROBE track %d '%s' id %d travel vertex %d %.3f\n", index, p->name, id, vi, best);
    for (f = 0; f < nf; f += 4) {
        v = mat4_point(&w[f], vec3(p->mesh.v[vi].x, p->mesh.v[vi].y, p->mesh.v[vi].z));
        probe_log("VRSPOTPROBE track %d '%s' id %d vertex %d f %d (%.3f %.3f %.3f)\n", index, p->name, id, vi,
                  lo + f, v.x, v.y, v.z);
    }
    if ((nf - 1) % 4 != 0) {   /* (the swing's own last frame too) */
        v = mat4_point(&w[nf - 1], vec3(p->mesh.v[vi].x, p->mesh.v[vi].y, p->mesh.v[vi].z));
        probe_log("VRSPOTPROBE track %d '%s' id %d vertex %d f %d (%.3f %.3f %.3f)\n", index, p->name, id, vi, hi,
                  v.x, v.y, v.z);
    }
    free(path);
    free(w);
}

/* every track of a placed object near a spot's GRAB box (kind 0 and 2; the nursery's door panels
 * are 2): its stops, the poses at its ends, and the vertex that travels furthest (its room place
 * every 4th frame of the swing) */
static void probe_tracks(PlacedSet *set, const Placed *p, int index) {
    const uint8_t *rec = anim_record(set, p);
    int count, id;

    if (rec == NULL) {
        probe_log("VRSPOTPROBE track %d '%s' no animation record\n", index, p->name);
        return;
    }
    count = (int)prd32(rec + 0x10);
    probe_log("VRSPOTPROBE track %d '%s' %d tracks\n", index, p->name, count);
    for (id = 0; id < count && id < 64; id++) {
        Placed at = *p;   /* (a copy: the room's own object is never started or stepped) */
        ProbeKey k0, kl, kh, kn, def;
        int lo = 0, hi = 0, span;

        placed_anim(set, &at, id, 0);
        probe_log("VRSPOTPROBE track %d '%s' id %d frames %d format %d\n", index, p->name, id, at.frames,
                  at.format);
        if (at.keys == NULL || at.frames <= 0) {
            continue;
        }
        span = probe_span(&at, &lo, &hi);
        if (span) {
            probe_log("VRSPOTPROBE track %d '%s' id %d flat %d %d\n", index, p->name, id, lo, hi);
        } else {
            probe_log("VRSPOTPROBE track %d '%s' id %d flat none\n", index, p->name, id);
            lo = hi = 0;
        }
        k0 = probe_key(&at, 0);
        kl = probe_key(&at, lo);
        kh = probe_key(&at, hi);
        kn = probe_key(&at, at.frames - 1);
        probe_log("VRSPOTPROBE track %d '%s' id %d pose f0 rot=(%.3f %.3f %.3f) pos=(%.3f %.3f %.3f)\n", index,
                  p->name, id, k0.rot.x, k0.rot.y, k0.rot.z, k0.pos.x, k0.pos.y, k0.pos.z);
        if (span) {
            probe_log("VRSPOTPROBE track %d '%s' id %d pose f%d rot=(%.3f %.3f %.3f) pos=(%.3f %.3f %.3f)\n",
                      index, p->name, id, lo, kl.rot.x, kl.rot.y, kl.rot.z, kl.pos.x, kl.pos.y, kl.pos.z);
            probe_log("VRSPOTPROBE track %d '%s' id %d pose f%d rot=(%.3f %.3f %.3f) pos=(%.3f %.3f %.3f)\n",
                      index, p->name, id, hi, kh.rot.x, kh.rot.y, kh.rot.z, kh.pos.x, kh.pos.y, kh.pos.z);
        }
        probe_log("VRSPOTPROBE track %d '%s' id %d pose f%d rot=(%.3f %.3f %.3f) pos=(%.3f %.3f %.3f)\n", index,
                  p->name, id, at.frames - 1, kn.rot.x, kn.rot.y, kn.rot.z, kn.pos.x, kn.pos.y, kn.pos.z);
        def.rot = p->def_rot;
        def.pos = p->def_pos;
        def.pos_from_key = 0;
        if (probe_same(&k0, &def)) {
            probe_log("VRSPOTPROBE track %d '%s' id %d DEFAULT-POSE at frame 0\n", index, p->name, id);
        }
        if (probe_same(&kn, &def)) {
            probe_log("VRSPOTPROBE track %d '%s' id %d DEFAULT-POSE at frame %d\n", index, p->name, id,
                      at.frames - 1);
        }
        if (span) {
            probe_swing(&at, lo, hi, index, id);
        }
    }
}

/* a morph near a spot's GRAB box (kind 1 / 3): its two frames and blend, how many frames its
 * definition holds, and the vertex that moves furthest between frame 0 and frame 1, in the room */
static void probe_morph(const Placed *p, int index) {
    int frames = morph_frame_count(p);
    int n = p->def != NULL ? (int)prd32(p->def + 0x40) : 0;
    MeshVertex *v0, *v1;
    Mat4 w;
    Vec3 best0 = vec3(0.0f, 0.0f, 0.0f), best1 = best0;
    float travel = -1.0f;
    int vi = -1, i;

    probe_log("VRSPOTPROBE morph %d '%s' a %d b %d t %.3f frames %d vertices %d (table at +0x70: %d %d)\n", index,
              p->name, p->morph_a, p->morph_b, p->morph_t, frames, n,
              p->def != NULL ? (int)prd32(p->def + 0x70) : -1, p->def != NULL ? (int)prd32(p->def + 0x74) : -1);
    if (frames < 2) {
        probe_log("VRSPOTPROBE morph %d '%s' no frame 1 to compare\n", index, p->name);
        return;
    }
    if (n <= 0 || n > 0x10000) {
        probe_log("VRSPOTPROBE morph %d '%s' no vertices to compare\n", index, p->name);
        return;
    }
    v0 = malloc((size_t)n * sizeof(*v0));
    v1 = malloc((size_t)n * sizeof(*v1));
    if (v0 == NULL || v1 == NULL || !morph_positions(p, 0, 0, 0.0f, v0, n) ||
        !morph_positions(p, 1, 1, 0.0f, v1, n)) {
        probe_log("VRSPOTPROBE morph %d '%s' frames 0 and 1 can't be read\n", index, p->name);
        free(v0);
        free(v1);
        return;
    }
    w = placed_world(p);
    for (i = 0; i < n; i++) {
        Vec3 a = mat4_point(&w, vec3(v0[i].x, v0[i].y, v0[i].z));
        Vec3 b = mat4_point(&w, vec3(v1[i].x, v1[i].y, v1[i].z));
        float d = vec3_len(vec3_sub(b, a));

        if (d > travel) {
            travel = d;
            vi = i;
            best0 = a;
            best1 = b;
        }
    }
    if (vi >= 0) {
        probe_log("VRSPOTPROBE morph %d '%s' travel vertex %d %.3f (%.3f %.3f %.3f) -> (%.3f %.3f %.3f)\n", index,
                  p->name, vi, travel, best0.x, best0.y, best0.z, best1.x, best1.y, best1.z);
    }
    free(v0);
    free(v1);
}

/* one of a spot's boxes: its centre and half sizes, and which of its own axes is room up */
static void probe_box(const char *what, int s, const VrBox *b) {
    Vec3 up;
    int axis = -1;

    if (b == NULL) {
        probe_log("VRSPOTPROBE spot %d %s none\n", s, what);
        return;
    }
    up = vrbox_up_axis(b, &axis);
    probe_log("VRSPOTPROBE spot %d '%s' %s c=(%.3f %.3f %.3f) h=(%.3f %.3f %.3f) up axis %d (%.3f %.3f %.3f) "
              "sign %c\n",
              s, b->name, what, b->c[0], b->c[1], b->c[2], b->h[0], b->h[1], b->h[2], axis, up.x, up.y, up.z,
              up.y < 0.0f ? '-' : '+');
}

/* a room entry: its spots, every placed object's bounds and how near they come to each GRAB box, and
 * the tracks and morphs of the ones near it (the room's own objects are only read) */
static void probe_room(Engine *e) {
    PlacedSet *set = &e->room.placed;
    int ids[VRMARKERS_SPOTS], n, s, i;

    n = vrmarkers_spots_here(e->room.id, ids, VRMARKERS_SPOTS);
    probe_log("VRSPOTPROBE room %d placed %d spots %d\n", e->room.id, set->n, n);
    for (s = 0; s < n; s++) {
        const VrBox *grab = vrmarkers_spot_grab(ids[s]), *bound = vrmarkers_spot_bound(ids[s]);

        probe_box("grab", s, grab);
        probe_box("bound", s, bound);
        for (i = 0; i < set->n; i++) {
            const Placed *p = &set->p[i];
            Vec3 lo, hi;
            int near = 0, dump = 0;
            float d = -1.0f;
            int have = placed_bounds(p, &lo, &hi);

            if (have && grab != NULL) {
                d = grab_gap(p, grab);
                near = d <= PROBE_NEAR;
                dump = d <= PROBE_DUMP;
            }
            if (!have) {
                probe_log("VRSPOTPROBE spot %d object %d '%s' kind %d shown %d leaf %d def_pos=(%.3f %.3f %.3f) "
                          "def_rot=(%.3f %.3f %.3f) bounds=(none)\n",
                          s, i, p->name, p->kind, p->shown, p->hiding_leaf, p->def_pos.x, p->def_pos.y,
                          p->def_pos.z, p->def_rot.x, p->def_rot.y, p->def_rot.z);
            } else {
                probe_log("VRSPOTPROBE spot %d object %d '%s' kind %d shown %d leaf %d def_pos=(%.3f %.3f %.3f) "
                          "def_rot=(%.3f %.3f %.3f) bounds=(%.3f %.3f %.3f)..(%.3f %.3f %.3f) grab_dist %.3f%s\n",
                          s, i, p->name, p->kind, p->shown, p->hiding_leaf, p->def_pos.x, p->def_pos.y,
                          p->def_pos.z, p->def_rot.x, p->def_rot.y, p->def_rot.z, lo.x, lo.y, lo.z, hi.x, hi.y,
                          hi.z, d, near ? " NEAR-GRAB" : dump ? " NEAR-SPOT" : "");
            }
            if (!dump) {
                continue;
            }
            if (p->kind == 1 || p->kind == 3) {
                probe_morph(p, i);
            } else {
                probe_tracks(set, p, i);   /* (0 rigid and 2 the room's batch layout - the nursery's
                                            * door panels are 2 - both carry tracks) */
            }
        }
    }
}

void vrspotprobe_here(Vec3 p) {
    sHere = p;
    sHereHave = 1;
}

void vrspotprobe_tick(Engine *e) {
    const Actor *f;
    Vec3 feet;
    int ids[VRMARKERS_SPOTS], n, s, printed;

    if (e == NULL || e->testing || e->room.id < 0) {
        return;   /* (not a game: the headless --test runs stay quiet) */
    }
    if (e->room.id != sRoom) {
        sRoom = e->room.id;
        sFeetTick = -1;
        probe_room(e);
    }
    f = fiona_actor(e);
    if (!sHereHave && f == NULL) {
        return;   /* (no Fiona yet: no feet to measure) */
    }
    feet = sHereHave ? sHere : f->pos;
    if (sFeetTick >= 0 && e->ticks - sFeetTick < TICKS_PER_SECOND) {
        return;   /* (once a second) */
    }
    n = vrmarkers_spots_here(e->room.id, ids, VRMARKERS_SPOTS);
    printed = 0;
    for (s = 0; s < n; s++) {
        const VrBox *b = vrmarkers_spot_bound(ids[s]);
        float d;

        if (b == NULL) {
            continue;
        }
        d = point_gap(feet, b);
        if (d > PROBE_FEET) {
            continue;   /* (nowhere near this spot) */
        }
        probe_log("VRSPOTPROBE feet (%.3f %.3f %.3f) spot %d '%s' dist %.3f bound_inside %d hip8_inside %d "
                  "closure %.3f\n",
                  feet.x, feet.y, feet.z, s, vrmarkers_spot_name(ids[s]), d, vrbox_inside(b, feet, 0.0f),
                  vrbox_inside(b, vec3(feet.x, feet.y + PROBE_HIP, feet.z), 0.0f), vrspot_closure());
        printed = 1;
    }
    if (printed) {
        sFeetTick = e->ticks;
    }
}

void vrspotprobe_script_track(const Placed *p, int id, int loop) {
    int ids[VRMARKERS_SPOTS], n, s;

    if (p == NULL || gEngine.testing || gEngine.room.id < 0) {
        return;
    }
    n = vrmarkers_spots_here(gEngine.room.id, ids, VRMARKERS_SPOTS);
    for (s = 0; s < n; s++) {
        const VrBox *grab = vrmarkers_spot_grab(ids[s]);

        if (grab == NULL) {
            continue;
        }
        if (grab_gap(p, grab) <= PROBE_DUMP) {
            probe_log("VRSPOTPROBE script track %d on '%s' (loop %d)\n", id, p->name, loop);
            return;
        }
    }
}
