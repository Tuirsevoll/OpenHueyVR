/* Seamless room loading: see roomseamless.h. */
#include "roomseamless.h"
#include "engine.h"
#include "roomlayout.h"
#include "seam.h"
#include "exits.h"
#include "areas.h"
#include "camdirector.h"
#include "../core/files.h"
#include "../hactor/hactor.h"
#include "../platform/vr.h"
#include "../render/render.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEAMLESS_PAIRS_MAX 256
#define SEAMLESS_PAST 3.0f        /* units past the doorway's line: hand over */
#define SEAMLESS_PAST_FRAME -0.1f /* ... for a measured seam: the floor ends at its line */
#define SEAMLESS_PAST_MAX 20.0f   /* ... and no further past it (a save load's place is not a crossing) */
#define SEAMLESS_LOAD_REACH 350.0f /* a neighbour is loaded once she is this near its doorway */
#define SEAMLESS_LOAD_GAP 8        /* ticks between neighbour loads */
#define SEAMLESS_LOAD_AFTER 20     /* ... and before the first after a crossing or a new room */
#define SEAMLESS_LEVEL 30.0f      /* the most she may be above or below a doorway's floor to cross it */
#define SEAMLESS_MARGIN 8.0f      /* across the doorway, past its width */
#define SEAMLESS_HEWIE_NEAR 40.0f /* how near Hewie comes too */
#define SEAMLESS_COOLDOWN 15      /* ticks (0.5 s) between handovers */
#define SEAMLESS_TIMEOUT 120      /* ticks a handover may take before it is forgotten */
#define SEAMLESS_DOOR_SHUT 4.0f   /* a door swung less than this (degrees) is shut: no portal */
#define SEAMLESS_DOOR_OPEN 20.0f  /* ... and one swung less than this is not open enough to walk through */

typedef struct SeamlessPair {
    int a, exit_a, b, exit_b;
    const Seam *seam;   /* its frames (seams.txt; NULL: the layout's matrices are the transform) */
} SeamlessPair;

static SeamlessPair sPair[SEAMLESS_PAIRS_MAX];
static int sPairN;
static int sPairsLoaded;

/* the doorway's line in the played room's space: its middle, its normal (out of the played room,
 * toward the other room) and its width. An open passage has no door, so the line comes from its
 * spots (0 out, 1 in: their middle is the point both rooms agree on when the layout places them)
 * and its event area or gate (the line's direction and width: the box's edge most square across
 * the way through, a gate's own line). A door's line is the door: where it stands and its turn. A
 * seam's own measured frame is used as it is. */
typedef struct Doorway {
    Vec3 centre, normal, tangent;
    float halfwidth;
    float height;    /* the opening's, up from the floor (the room data's y is up) */
    int frame;       /* 1: from seams.txt's own frame, not worked out from the exit's data */
} Doorway;

#define DOORWAY_HEIGHT 60.0f   /* (an opening with no frame of its own: taller than any gate) */

/* the played room's seams: each doorway of it that a pair joins to another room */
#define SEAM_ACT_MAX 8
typedef struct ActiveSeam {
    const SeamlessPair *pair;
    int is_a;
    int my_exit, other, other_exit;
    Doorway line;
    int t_ok;
    Mat4 t_ab, t_ba;          /* played -> other, other -> played */
    float prev_past;          /* the previous tick's side of the line: a handover is a crossing, not */
    int prev_had;             /* merely being past it (a save's place or a door's arrival can be) */
} ActiveSeam;

static ActiveSeam sAct[SEAM_ACT_MAX];
/* the played room's doorways a pair names but that are not used, and why (seam-info) */
static struct { int exit; char why[120]; } sDrop[16];
static int sDropN;

static void drop_note(int exit, const char *fmt, ...) {
    va_list ap;

    if (sDropN >= 16) {
        return;
    }
    sDrop[sDropN].exit = exit;
    va_start(ap, fmt);
    vsnprintf(sDrop[sDropN].why, sizeof(sDrop[sDropN].why), fmt, ap);
    va_end(ap);
    sDropN++;
}
static int sActN;
static int sActRoom = -1;
static Room sNb[SEAM_ACT_MAX];   /* the other rooms, drawn only (id -1: free) */
static RoomLook sNbLook[SEAM_ACT_MAX];   /* each one's own fog / tint / bloom, for the blend */
static int sInit;
static long sNbNextLoad;       /* no neighbour is loaded before this tick (one at a time, not at a crossing) */
static int sTrace;               /* frames still to trace after a handover (seam-log) */
/* the handover in progress */
static int sPending;
static int sFrom, sTo, sExitTo;
static Vec3 sFromPos;            /* Fiona's place in the old room, at the load */
static float sFromYaw;
static Mat4 sWFrom, sWTo, sTFromTo;
static int sArrivalTaken, sHewieTaken;
static long sStartTick;
static long sCooldownUntil;
static Uint64 sLoadT0, sLoadNs;
static int sLoadTimed;
static int sSuppressedRoom = -1, sSuppressedExit = -1;

/* the debug words (seam-portal, seam-outline, seam-tint, seam-log) */
static int sDbgPortal = 1, sDbgOutline, sDbgTint, sDbgLog;

/* Seamless is on in the headset with the option set; HGA_SEAM_FLAT=1 turns it on in the flat-screen game too
 * (a debugging aid: stills of the doorways through --hidden --eval and screenshot) */
static int seam_on(void) {
    static int env = -1;

    if (env < 0) {
        env = getenv("HGA_SEAM_FLAT") != NULL;
    }
    return env || (vr_active() && gVrOptions.room_loading);
}

/* one line to the console (and the log) */
static void slog(const char *fmt, ...) {
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s", line);
}

/* a 3x4 matrix's linear part on a direction (no translation) */
static Vec3 turn_vec(const Mat4 *m, Vec3 v) {
    return vec3(m->m[0] * v.x + m->m[4] * v.y + m->m[8] * v.z,
                m->m[1] * v.x + m->m[5] * v.y + m->m[9] * v.z,
                m->m[2] * v.x + m->m[6] * v.y + m->m[10] * v.z);
}

/* `yaw` (0 along +z: an actor's heading) turned by m's linear part */
static float turn_yaw(const Mat4 *m, float yaw) {
    Vec3 v = turn_vec(m, vec3(sinf(yaw), 0.0f, cosf(yaw)));

    return atan2f(v.x, v.z);
}

static float wrap_angle(float a) {
    return atan2f(sinf(a), cosf(a));
}

/* the camera yaw whose forward (sin, -cos) is `v`: the change a handover makes to sAlign */
static float camera_yaw_of(Vec3 v) {
    return atan2f(v.x, -v.z);
}

static int base_of(int room);

/* the pair file: `ST_001 2 ST_002 2`, one a line */
static void pairs_load(void) {
    uint8_t *data;
    size_t size = 0, i, ln = 0;
    char line[256];
    int missing = 0;

    if (sPairsLoaded) {
        return;
    }
    sPairsLoaded = 1;
    roomlayout_load_if_needed();
    data = files_read("PROPS/seamless.txt", &size);
    if (data == NULL) {
        missing = 1;
    }
    if (data != NULL) {
        for (i = 0; i <= size; i++) {
            char c = i < size ? (char)data[i] : '\n';

            if (c == '\n' || c == '\r') {
                char na[16], nb[16];
                int ea, eb, a, b;

                line[ln] = 0;
                ln = 0;
                if (c == '\r' && i + 1 < size && data[i + 1] == '\n') {
                    i++;
                }
                if (sscanf(line, "%15s %d %15s %d", na, &ea, nb, &eb) != 4) {
                    continue;
                }
                a = roomlayout_room_id(na);
                b = roomlayout_room_id(nb);
                if (a < 0 || b < 0 || ea < 0 || ea >= ROOM_EXITS || eb < 0 || eb >= ROOM_EXITS) {
                    fprintf(stderr, "seamless: pair %s %d - %s %d skipped (bad names or exits)\n",
                            na, ea, nb, eb);
                    continue;
                }
                if (sPairN >= SEAMLESS_PAIRS_MAX) {
                    continue;
                }
                if (!roomlayout_get(a, NULL) || !roomlayout_get(b, NULL)) {
                    fprintf(stderr, "seamless: pair %s %d - %s %d skipped (not in the layout, or a check)\n",
                            na, ea, nb, eb);
                    continue;
                }
                sPair[sPairN++] = (SeamlessPair){base_of(a), ea, base_of(b), eb, NULL};   /* (a family's root) */
            } else if (ln + 1 < sizeof(line)) {
                line[ln++] = c;
            }
        }
        free(data);
    }
    if (missing) {
        fprintf(stderr, "seamless: no PROPS/seamless.txt\n");
    }
    /* the seams (PROPS/seams.txt): each line is a seam and a seamless pair; a pair in both files
     * is the seam */
    seam_load_if_needed();
    for (ln = 0; (int)ln < seam_count(); ln++) {
        const Seam *s = seam_at((int)ln);
        int k, have = 0;

        if (s->layout && (!roomlayout_get(s->a, NULL) || !roomlayout_get(s->b, NULL))) {
            fprintf(stderr, "seam: ST_%03X %d - ST_%03X %d skipped (a layout line, and not in the layout)\n",
                    s->a, s->ea, s->b, s->eb);
            continue;
        }
        for (k = 0; k < sPairN; k++) {
            int sa = base_of(s->a), sb = base_of(s->b);

            if ((sPair[k].a == sa && sPair[k].exit_a == s->ea && sPair[k].b == sb && sPair[k].exit_b == s->eb) ||
                (sPair[k].a == sb && sPair[k].exit_a == s->eb && sPair[k].b == sa && sPair[k].exit_b == s->ea)) {
                if (!(sPair[k].a == sa && sPair[k].exit_a == s->ea)) {   /* (listed the other way round: the frames' A side is ours) */
                    SeamlessPair r = {sPair[k].b, sPair[k].exit_b, sPair[k].a, sPair[k].exit_a, NULL};

                    sPair[k] = r;
                }
                sPair[k].seam = s->layout ? NULL : s;
                have = 1;
            }
        }
        if (!have && sPairN < SEAMLESS_PAIRS_MAX) {
            sPair[sPairN++] = (SeamlessPair){base_of(s->a), s->ea, base_of(s->b), s->eb, s->layout ? NULL : s};
        }
    }
    fprintf(stderr, "seamless: %d doorway pair(s)\n", sPairN);
}

/* the room a story version stands for, down the chain (ST_030 is a variant of ST_02F, the same
 * place as ST_028): the pairs name any of a family, so both sides are taken to the root */
static int base_of(int room) {
    int i;

    for (i = 0; i < 8; i++) {
        int base = roomlayout_base_id(room);

        if (base < 0 || base == room) {
            break;
        }
        room = base;
    }
    return room;   /* (a seam's room need not be in the layout) */
}

/* T from `from_room`'s space into `to_room`'s over a pair: the seam's frames, else the layout's
 * matrices */
static int pair_matrix(const SeamlessPair *p, int from_is_a, int from_room, int to_room, Mat4 *T) {
    if (p->seam != NULL) {
        return seam_transform(p->seam, from_is_a, T);
    }
    return roomlayout_between(from_room, to_room, T);
}

/* The gate's own state, from what its scripts have applied: open 1, shut 0. ST_001 shows the
 * open / shut gate parts (groups $15 / $16); ST_002 shows its open part (group $1B). Until one
 * is shown it is treated as shut, so the asset's default cage place cannot open it early. A shut
 * gate keeps the gateway closed and its box solid, as the original room's story has it. Only the
 * courtyard's lever gate (exit 2 of ST_001 and ST_002) has a state: any other seam is open. */
static int gateway_open(const Room *r, int exit) {
    uint32_t open_part, shut_part, gate_open_part;
    int base;

    if (r == NULL) {
        return -1;
    }
    base = base_of(r->id);
    if ((base != 1 && base != 2) || exit != 2) {
        return 1;
    }
    open_part = (r->groups[21 >> 5] >> (21 & 31)) & 1u;       /* ST_001, open */
    shut_part = (r->groups[22 >> 5] >> (22 & 31)) & 1u;       /* ST_001, shut */
    gate_open_part = (r->groups[27 >> 5] >> (27 & 31)) & 1u;  /* ST_002, open */
    if (open_part || gate_open_part) {
        return 1;
    }
    if (shut_part) {
        return 0;
    }
    /* (before the place's came-in has shown one of the gate parts: keep it shut, not open by
     * an asset's default cage or part position) */
    return 0;
}

/* a door in the exit that is swung shut: no way through, and nothing to see through */
static float door_swing(const Room *r, int exit) {
    if (r == NULL || exit < 0 || exit >= ROOM_DOORS || !r->doors[exit].present || !r->doors[exit].shown) {
        return 1000.0f;   /* (no door: as open as can be) */
    }
    return fabsf(r->doors[exit].swing);
}

/* a draw's own box: the vertices its run uses (the room mesh's v, not the moving batches') */
static int draw_box(const Room *r, const MeshDraw *d, Vec3 *lo, Vec3 *hi) {
    int i;

    if (r == NULL || d == NULL || r->mesh.v == NULL || d->count <= 0 || d->first < 0 ||
        (size_t)d->first + (size_t)d->count > (size_t)r->mesh.nv) {
        return 0;
    }
    *lo = *hi = vec3(r->mesh.v[d->first].x, r->mesh.v[d->first].y, r->mesh.v[d->first].z);
    for (i = d->first + 1; i < d->first + d->count; i++) {
        const MeshVertex *v = &r->mesh.v[i];

        lo->x = v->x < lo->x ? v->x : lo->x;  hi->x = v->x > hi->x ? v->x : hi->x;
        lo->y = v->y < lo->y ? v->y : lo->y;  hi->y = v->y > hi->y ? v->y : hi->y;
        lo->z = v->z < lo->z ? v->z : lo->z;  hi->z = v->z > hi->z ? v->z : hi->z;
    }
    return 1;
}

/* A doorway's void shell: what the original draws in a doorway to hide the room that is not
 * loaded behind it. Always drawn (part 0, group 0) and never textured - a box of a few triangles,
 * or a flat panel - whatever colour the room's light makes it (white, black, brown). The one
 * textured kind is an open 8-triangle box 30..70 across. A room that has a dozen such draws is
 * something else (a stage, not doorways): none of its are taken for shells. */
static int void_shell(const Room *r, const MeshDraw *d) {
    Vec3 lo, hi;
    int tris, big_dims;

    if (d == NULL || d->part != MESH_SOLID || d->group != 0 || !draw_box(r, d, &lo, &hi)) {
        return 0;
    }
    tris = d->count / 3;
    big_dims = (hi.x - lo.x >= 15.0f) + (hi.y - lo.y >= 15.0f) + (hi.z - lo.z >= 15.0f);
    if (d->texture < 0) {
        return tris >= 2 && tris <= 32 && big_dims >= 2;
    }
    return tris == 8 && hi.x - lo.x >= 30.0f && hi.y - lo.y >= 30.0f && hi.z - lo.z >= 30.0f &&
           hi.x - lo.x <= 70.0f && hi.y - lo.y <= 70.0f && hi.z - lo.z <= 70.0f;
}

/* which of a room's draws are void shells, worked out once per room (the draw asks for every run
 * of every layer of every eye) */
#define VOID_CACHE 12
static struct VoidMask {
    const Room *r;
    int id, nd;
    uint8_t *hide;
} sVoid[VOID_CACHE];
static int sVoidNext;

static const uint8_t *void_mask(const Room *r) {
    int i, n = 0;
    struct VoidMask *m = NULL;

    for (i = 0; i < VOID_CACHE; i++) {
        if (sVoid[i].r == r && sVoid[i].hide != NULL) {
            m = &sVoid[i];
            break;
        }
    }
    if (m != NULL && m->id == r->id && m->nd == r->mesh.nd) {
        return m->hide;
    }
    if (m == NULL) {
        m = &sVoid[sVoidNext];
        sVoidNext = (sVoidNext + 1) % VOID_CACHE;
    }
    free(m->hide);
    m->hide = calloc((size_t)(r->mesh.nd > 0 ? r->mesh.nd : 1), 1);
    m->r = r;
    m->id = r->id;
    m->nd = r->mesh.nd;
    if (m->hide == NULL) {
        return NULL;
    }
    for (i = 0; i < r->mesh.nd; i++) {
        m->hide[i] = (uint8_t)void_shell(r, &r->mesh.d[i]);
        n += m->hide[i];
    }
    if (n > 12) {
        memset(m->hide, 0, (size_t)r->mesh.nd);   /* (a stage, not doorways) */
        n = 0;
    }
    if (n > 0) {
        slog("seamless: ST_%03X: %d doorway void shell(s) not drawn\n", r->id, n);
    }
    return m->hide;
}
static int doorway_build(int room_id, int exit, Doorway *d) {
    Room *r = &gEngine.room;
    Vec3 out, in, passage, pn;
    int tri_out, tri_in, area = -1, kind = -1;
    float len;

    if (r->id != room_id || exit < 0 || exit >= ROOM_EXITS) {
        return 0;
    }
    tri_out = exit_spot(r, &gEngine.world, exit, 0, &out);
    tri_in = exit_spot(r, &gEngine.world, exit, 1, &in);
    if (tri_out < 0 || tri_in < 0) {
        return 0;
    }
    /* The spots (rooms.md, Doors_NavSpot): `out` (0) is 4 inside this room from its door, `in` (1)
     * is 12 beyond it, on the other room's side. The way through is out -> in (as main.c's
     * doorway_faded takes it); the middle of the two is a point both rooms agree on when the
     * layout places them. */
    passage = vec3(in.x - out.x, 0.0f, in.z - out.z);
    len = vec3_len(passage);
    if (len < 0.001f) {
        return 0;
    }
    pn = vec3_scale(passage, 1.0f / len);
    d->height = DOORWAY_HEIGHT;
    d->frame = 0;
    d->centre = vec3((out.x + in.x) * 0.5f, out.y, (out.z + in.z) * 0.5f);
    d->normal = pn;
    d->tangent = vec3(-pn.z, 0.0f, pn.x);
    d->halfwidth = 16.0f;
    if (room_id >= 0 && room_id < WORLD_ROOMS) {
        area = gEngine.world.exits[room_id][exit].camera & 0xFFFF;
        if (area >= 0 && area < 0xFFFF) {
            kind = area_kind(r, area);
        }
    }
    if (exit < ROOM_DOORS && r->doors[exit].present) {
        const RoomDoor *door = &r->doors[exit];

        d->centre = door->stand;
        /* (the leaf's line across its turn; the normal below is taken across it, into the room
         * beyond: the way the out - in spots say) */
        d->tangent = vec3(cosf(door->rot.y), 0.0f, -sinf(door->rot.y));
        d->halfwidth = 6.0f;   /* (the leaf is 12 wide) */
    } else if (kind == 0) {   /* a gate: its own line */
        Vec3 a, b;

        if (area_corner(r, area, 0, &a) && area_corner(r, area, 1, &b)) {
            Vec3 t = vec3(b.x - a.x, 0.0f, b.z - a.z);
            float l = vec3_len(t);

            if (l > 0.001f) {
                d->centre = vec3((a.x + b.x) * 0.5f, a.y, (a.z + b.z) * 0.5f);
                d->tangent = vec3_scale(t, 1.0f / l);
                d->halfwidth = l * 0.5f;
            }
        }
    } else if (kind == 1) {   /* a box area: its edge most square across the way through */
        Vec3 c[4];
        int k, n = 0, best = -1;
        float best_score = 2.0f, half = 0.0f;

        for (k = 0; k < 4; k++) {
            if (area_corner(r, area, k, &c[k])) {
                n++;
            }
        }
        for (k = 0; k < n; k++) {
            Vec3 e = vec3(c[(k + 1) % n].x - c[k].x, 0.0f, c[(k + 1) % n].z - c[k].z);
            float l = vec3_len(e);
            float score;

            if (l < 0.001f) {
                continue;
            }
            score = fabsf(vec3_dot(vec3_scale(e, 1.0f / l), pn));
            if (score < best_score) {
                best_score = score;
                best = k;
            }
        }
        if (best >= 0) {
            Vec3 e = vec3(c[(best + 1) % n].x - c[best].x, 0.0f, c[(best + 1) % n].z - c[best].z);

            d->tangent = vec3_norm(e);
            for (k = 0; k < n; k++) {
                float along = fabsf(vec3_dot(vec3_sub(c[k], d->centre), d->tangent));

                half = along > half ? along : half;
            }
            d->halfwidth = half;
        }
    }
    /* the normal is across the tangent and points the way through, into the other room */
    d->normal = vec3(-d->tangent.z, 0.0f, d->tangent.x);
    if (vec3_dot(d->normal, pn) < 0.0f) {
        d->normal = vec3_scale(d->normal, -1.0f);
    }
    d->centre.y = out.y;
    return 1;
}

/* one side of a pair as a doorway line in the played room: the seam's own frame, else worked out */
static int doorway_make(int room_id, const SeamlessPair *p, int is_a, Doorway *d) {
    int exit = is_a ? p->exit_a : p->exit_b;

    if (p->seam != NULL) {
        /* a seam's own measured frame: the opening's middle, its way out, its size */
        const SeamFrame *f = seam_frame(p->seam, is_a);

        d->centre = f->p;
        d->normal = vec3(sinf(f->yaw), 0.0f, cosf(f->yaw));
        d->tangent = vec3(-d->normal.z, 0.0f, d->normal.x);
        d->halfwidth = f->width * 0.5f;
        d->height = f->height;
        d->frame = 1;
        return 1;
    }
    return doorway_build(room_id, exit, d);
}

/* ---- the played room's seams ---- */

/* each doorway of the played room that a pair joins, with its line and its transforms (rebuilt
 * when a room is loaded: the lines come from its nav, its doors and its areas) */
static void actives_refresh(Engine *e) {
    int i;

    if (sActRoom == e->room.id) {
        return;
    }
    sActRoom = e->room.id;
    sActN = 0;
    sDropN = 0;
    if (sNbNextLoad < e->ticks + SEAMLESS_LOAD_AFTER) {
        sNbNextLoad = e->ticks + SEAMLESS_LOAD_AFTER;
    }
    pairs_load();
    for (i = 0; i < sPairN && sActN < SEAM_ACT_MAX; i++) {
        const SeamlessPair *p = &sPair[i];
        int base = base_of(e->room.id), is_a = p->a == base;
        ActiveSeam *s = &sAct[sActN];

        if (p->a != base && p->b != base) {
            continue;
        }
        memset(s, 0, sizeof(*s));
        s->pair = p;
        s->is_a = is_a;
        s->my_exit = is_a ? p->exit_a : p->exit_b;
        s->other_exit = is_a ? p->exit_b : p->exit_a;
        {
            /* the room the game's own graph sends this exit to now (a story version of the pair's
             * room, as the story stands): it must be the pair's room, at the pair's exit - else
             * this doorway is not what the pair says, and the old way out is kept for it */
            int to_exit = -1, next = world_exit_leads(&e->world, e->room.id, s->my_exit, &to_exit);
            int want = is_a ? p->b : p->a;

            if (next < 0 || base_of(next) != want || to_exit != s->other_exit) {
                drop_note(s->my_exit, "the game's graph leads to ST_%03X exit %d now, not the pair's ST_%03X exit %d", next < 0 ? 0 : next, to_exit, want, s->other_exit);
                slog("seamless: ST_%03X exit %d: the game's graph leads to ST_%03X exit %d, not the pair's ST_%03X exit %d: "
                     "left to the old way out\n", e->room.id, s->my_exit, next < 0 ? 0 : next, to_exit, want,
                     s->other_exit);
                continue;
            }
            s->other = next;
        }
        if (!doorway_make(e->room.id, p, is_a, &s->line)) {
            drop_note(s->my_exit, "no doorway line could be worked out (no exit spots)");
            slog("seamless: ST_%03X exit %d: no doorway line (no spots): left to the old way out\n", e->room.id,
                 s->my_exit);
            continue;
        }
        s->t_ok = pair_matrix(p, is_a, e->room.id, s->other, &s->t_ab) &&
                  pair_matrix(p, !is_a, s->other, e->room.id, &s->t_ba);
        if (!s->t_ok) {
            drop_note(s->my_exit, "no transform to ST_%03X", s->other);
            slog("seamless: ST_%03X exit %d: no transform to ST_%03X: left to the old way out\n", e->room.id,
                 s->my_exit, s->other);
            continue;
        }
        slog("seamless: ST_%03X exit %d -> ST_%03X exit %d (%s): centre %.1f %.1f %.1f normal %.3f %.3f width %.1f "
             "height %.1f\n",
             e->room.id, s->my_exit, s->other, s->other_exit, s->line.frame ? "frame" : "auto", s->line.centre.x,
             s->line.centre.y, s->line.centre.z, s->line.normal.x, s->line.normal.z, s->line.halfwidth * 2.0f,
             s->line.height);
        sActN++;
    }
}

static ActiveSeam *act_for_exit(int exit) {
    int i;

    for (i = 0; i < sActN; i++) {
        if (sAct[i].my_exit == exit) {
            return &sAct[i];
        }
    }
    return NULL;
}

static Room *nb_find(int id) {
    int i;

    for (i = 0; i < SEAM_ACT_MAX; i++) {
        if (sNb[i].id == id && id >= 0) {
            return &sNb[i];
        }
    }
    return NULL;
}

static void nb_free_all(const char *why) {
    int i;

    for (i = 0; i < SEAM_ACT_MAX; i++) {
        if (sNb[i].id >= 0) {
            slog("seamless: freed ST_%03X (drawing alone: %s)\n", sNb[i].id, why);
            room_free_view(&sNb[i]);
        }
        sNb[i].id = -1;
    }
}

/* the other rooms of the played room's seams kept in step: those no seam leads to now are freed,
 * and one missing is loaded (one a tick: the nearest doorway first) */
static void neighbours_update(Engine *e) {
    int i, best = -1;
    float best_d = 1e30f;
    HBody *b = hactor_body(gCamDir.player);

    for (i = 0; i < SEAM_ACT_MAX; i++) {
        int k, needed = 0;

        if (sNb[i].id < 0) {
            continue;
        }
        for (k = 0; k < sActN; k++) {
            needed |= sAct[k].other == sNb[i].id;
        }
        if (!needed) {
            slog("seamless: freed ST_%03X (drawing alone: no seam of ST_%03X leads there)\n", sNb[i].id, e->room.id);
            room_free_view(&sNb[i]);
            sNb[i].id = -1;
        }
    }
    if (e->ticks < sNbNextLoad) {
        return;   /* (a load is about 4 ms: not in the tick of a crossing, and one at a time) */
    }
    for (i = 0; i < sActN; i++) {
        float d;

        if (nb_find(sAct[i].other) != NULL) {
            continue;
        }
        if (b != NULL && b->has) {
            Vec3 p = vec3(b->pos[0], b->pos[1], b->pos[2]);

            d = vec3_len(vec3_sub(p, sAct[i].line.centre));
        } else {
            d = 0.0f;
        }
        if (d < best_d && d <= SEAMLESS_LOAD_REACH) {
            best_d = d;
            best = i;
        }
    }
    if (best >= 0) {
        Uint64 t0 = SDL_GetPerformanceCounter();
        int slot, ok, other = sAct[best].other;

        for (slot = 0; slot < SEAM_ACT_MAX && sNb[slot].id >= 0; slot++) {
        }
        if (slot >= SEAM_ACT_MAX) {
            return;
        }
        ok = room_load_view(&sNb[slot], other);
        if (ok) {
            room_look_parse(&sNb[slot], &sNbLook[slot]);
        }
        slog("seamless: neighbour ST_%03X %s in %.1f ms (beside ST_%03X)\n", other, ok ? "ready" : "FAILED",
             (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency(),
             e->room.id);
        sNbNextLoad = e->ticks + SEAMLESS_LOAD_GAP;
        if (!ok) {
            sNb[slot].id = -1;
            /* (not tried again for this room: its seam is left to the old way out) */
            memmove(&sAct[best], &sAct[best + 1], (size_t)(sActN - best - 1) * sizeof(ActiveSeam));
            sActN--;
        }
    }
}

/* each room's matrix into one common space, for the handover's "she did not move" check: the
 * layout's for a layout pair; for a seam the `from` room's own space and the `to` room's put
 * into it by T */
static int world_pair(const ActiveSeam *s, int from, int to, Mat4 *wf, Mat4 *wt) {
    if (s->pair->seam != NULL) {
        *wf = mat4_identity();
        return seam_mat4_inverse(&s->t_ab, wt);
    }
    return roomlayout_get(from, wf) && roomlayout_get(to, wt);
}

/* ---- the handover ---- */

void roomseamless_reset(void) {
    int i;

    if (sInit) {
        nb_free_all("a new game");
    }
    for (i = 0; i < SEAM_ACT_MAX; i++) {
        sNb[i].id = -1;
    }
    sInit = 1;
    sPending = 0;
    sArrivalTaken = sHewieTaken = 0;
    sCooldownUntil = 0;
    sLoadTimed = 0;
    sActRoom = -1;
    sActN = 0;
    sSuppressedRoom = sSuppressedExit = -1;
}

int roomseamless_go(int exit) {
    Engine *e = &gEngine;
    HBody *b;
    ActiveSeam *s;
    Cell arg;
    int kind;

    if (!seam_on() || sPending || exit < 0) {
        return 0;
    }
    s = act_for_exit(exit);
    if (s == NULL) {
        return 0;
    }
    if (gateway_open(&e->room, exit) != 1) {
        return 0;   /* (the story gate is still shut: no way through until the lever) */
    }
    if (door_swing(&e->room, exit) < SEAMLESS_DOOR_OPEN) {
        return 0;   /* (a door in the way: shut, or not open enough yet) */
    }
    b = hactor_body(gCamDir.player);
    if (b == NULL || !b->has || b->room != e->room.id) {
        return 0;
    }
    if (!world_pair(s, e->room.id, s->other, &sWFrom, &sWTo)) {
        slog("seamless: no layout for ST_%03X -> ST_%03X: no handover\n", e->room.id, s->other);
        return 0;
    }
    sTFromTo = s->t_ab;
    sFrom = e->room.id;
    sTo = s->other;
    sExitTo = s->other_exit;
    {
        int to_exit = -1;
        int next = world_exit_leads(&e->world, e->room.id, exit, &to_exit);

        slog("seamless: go-through: ST_%03X exit %d leads to ST_%03X exit %d (the pair's other side is ST_%03X)\n",
             e->room.id, exit, next, to_exit, s->other);
        if (next != s->other) {
            slog("seamless: WARNING the room graph and the seamless pair disagree here\n");
        }
    }
    sFromPos = vec3(b->pos[0], b->pos[1], b->pos[2]);
    sFromYaw = b->yaw;
    sArrivalTaken = sHewieTaken = 0;
    sLoadTimed = 0;
    sLoadNs = 0;
    sStartTick = e->ticks;
    sCooldownUntil = e->ticks + SEAMLESS_COOLDOWN;
    sPending = 1;
    kind = hactor_kind("go-through");
    if (kind < 0) {
        slog("seamless: no go-through message kind: no handover\n");
        sPending = 0;
        return 0;
    }
    arg = exit;
    /* the normal path: leaving-room -> the load -> arrived -> entered-room, marked seamless by
     * sPending (nothing in the scripts' stack or the save is different) */
    hactor_broadcast(-1, kind, &arg, 1);
    slog("seamless: handover ST_%03X->ST_%03X by exit %d (its exit %d; T [%.4f %.4f %.4f %.4f; "
         "%.4f %.4f %.4f %.4f; %.4f %.4f %.4f %.4f])\n",
         sFrom, sTo, exit, sExitTo, sTFromTo.m[0], sTFromTo.m[4], sTFromTo.m[8], sTFromTo.m[12],
         sTFromTo.m[1], sTFromTo.m[5], sTFromTo.m[9], sTFromTo.m[13],
         sTFromTo.m[2], sTFromTo.m[6], sTFromTo.m[10], sTFromTo.m[14]);
    return 1;
}

/* The room the handover is for, if it is already loaded as a neighbour: it becomes the played room
 * (the caller finishes it with room_promote) and the room just left takes its place beside it -
 * as the scripts left it, with its placed objects and doors - so neither is read or uploaded
 * again in the tick of the crossing. 1 if it was swapped. */
int roomseamless_swap_in(Room *played, int id) {
    static Room tmp;
    int i;

    if (!seam_on() || !sPending || id != sTo || played->id != sFrom) {
        return 0;
    }
    for (i = 0; i < SEAM_ACT_MAX && sNb[i].id != id; i++) {
    }
    if (i >= SEAM_ACT_MAX) {
        slog("seamless: ST_%03X was not loaded beside ST_%03X: loaded the usual way\n", id, played->id);
        return 0;
    }
    room_vases_stash(played);   /* (her vases are grown in its mesh: kept with it, so they do not grow again if she comes back) */
    tmp = *played;
    *played = sNb[i];
    sNb[i] = tmp;
    sNb[i].view_placed = 1;
    sNbLook[i] = gRoomLook;   /* (the room just left, as it looks now: the new room's look replaces it) */
    slog("seamless: ST_%03X swapped in as the played room; ST_%03X stays beside it as it was\n", id, sNb[i].id);
    return 1;
}
void roomseamless_load_begin(int id) {
    HBody *b = hactor_body(gCamDir.player);

    /* (a room (re)load, a save's included: its place is not a step across the line, and its
     * doorways are worked out afresh) */
    sActRoom = -1;
    if (!sPending || id != sTo || gEngine.room.id != sFrom) {
        return;
    }
    /* (her place the moment the room is swapped, not the one when the handover was asked for) */
    if (b != NULL && b->has && b->room == sFrom) {
        sFromPos = vec3(b->pos[0], b->pos[1], b->pos[2]);
        sFromYaw = b->yaw;
    }
    sLoadT0 = SDL_GetPerformanceCounter();
    sLoadTimed = 1;
}

void roomseamless_load_end(void) {
    if (sLoadTimed) {
        sLoadNs = SDL_GetPerformanceCounter() - sLoadT0;
        sLoadTimed = 0;
    }
}

/* one line after the room is in: the gateway's own cage (the courtyard's placed "ori") and the
 * room-mesh gate parts (groups $15 / $16: open / shut at ST_001's gate) - what is in the doorway */
static void log_doorway_after_arrival(Vec3 at) {
    Room *r = &gEngine.room;
    Placed *cage = placed_named(&r->placed, "ori");
    int open_part = (r->groups[21 >> 5] >> (21 & 31)) & 1;
    int shut_part = (r->groups[22 >> 5] >> (22 & 31)) & 1;

    if (cage != NULL) {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        int i, inside;

        for (i = 0; i < cage->mesh.nv; i++) {
            const MeshVertex *v = &cage->mesh.v[i];

            lo[0] = v->x < lo[0] ? v->x : lo[0];
            lo[1] = v->y < lo[1] ? v->y : lo[1];
            lo[2] = v->z < lo[2] ? v->z : lo[2];
            hi[0] = v->x > hi[0] ? v->x : hi[0];
            hi[1] = v->y > hi[1] ? v->y : hi[1];
            hi[2] = v->z > hi[2] ? v->z : hi[2];
        }
        inside = at.x >= cage->pos.x + lo[0] && at.x <= cage->pos.x + hi[0] &&
                 at.y >= cage->pos.y + lo[1] && at.y <= cage->pos.y + hi[1] &&
                 at.z >= cage->pos.z + lo[2] && at.z <= cage->pos.z + hi[2];
        slog("seamless: after arrival ST_%03X: 'ori' cage y %.2f (defined %.2f) shown %d, world y %.2f..%.2f; "
             "arrival %s its box; room gate parts $15 %d $16 %d\n",
             r->id, cage->pos.y, cage->def_pos.y, cage->shown, cage->pos.y + lo[1], cage->pos.y + hi[1],
             inside ? "inside" : "outside", open_part, shut_part);
    } else {
        slog("seamless: after arrival ST_%03X: no 'ori' cage; room gate parts $15 %d $16 %d\n",
             r->id, open_part, shut_part);
    }
}

int roomseamless_take_arrival(Vec3 *at, float *yaw) {
    Vec3 p, f0, f1, old_world, new_world;
    float y, ms = 0.0f, diff;
    Uint64 freq;

    if (!sPending || sArrivalTaken || gEngine.room.id != sTo) {
        return 0;
    }
    p = mat4_point(&sTFromTo, sFromPos);
    y = turn_yaw(&sTFromTo, sFromYaw);
    {
        /* a frame a few units off can put the exact place just off the floor: the nearest floor
         * within 12 units (never further: a far one is a teleport) is better than none */
        NavMesh *n = &gEngine.room.nav;
        float h, best = 12.0f * 12.0f;
        int i, found = -1;

        if (n->ntris > 0 && navmesh_find(n, p, 8.0f, &h) < 0) {
            for (i = 0; i < n->ntris; i++) {
                Vec3 c = navmesh_center(n, i);
                float d2 = (c.x - p.x) * (c.x - p.x) + (c.z - p.z) * (c.z - p.z);

                if (d2 < best && fabsf(c.y - p.y) < 20.0f) {
                    best = d2;
                    found = i;
                }
            }
            if (found >= 0) {
                slog("seamless: the arrival point is off the floor; the nearest floor, %.1f units away\n", sqrtf(best));
                p = navmesh_center(n, found);
            } else {
                slog("seamless: WARNING the arrival point is off the floor and none is within 12 units\n");
            }
        }
    }
    log_doorway_after_arrival(p);
    /* her look is her heading; the camera's yaw turns by the same matrix, so the room does not
     * spin in the headset (a pure move turns it by 0) */
    f0 = vec3(sinf(sFromYaw), 0.0f, cosf(sFromYaw));
    f1 = turn_vec(&sTFromTo, f0);
    engine_vr_turn_view(wrap_angle(camera_yaw_of(f1) - camera_yaw_of(f0)));
    old_world = mat4_point(&sWFrom, sFromPos);
    new_world = mat4_point(&sWTo, p);
    diff = vec3_len(vec3_sub(old_world, new_world));
    freq = SDL_GetPerformanceFrequency();
    if (freq > 0 && sLoadNs > 0) {
        ms = (float)((double)sLoadNs * 1000.0 / (double)freq);
    }
    slog("seamless: handover ST_%03X->ST_%03X in %.1f ms; world %.3f %.3f %.3f -> %.3f %.3f %.3f "
         "(diff %.3f) room %.3f %.3f %.3f\n",
         sFrom, sTo, ms, old_world.x, old_world.y, old_world.z, new_world.x, new_world.y, new_world.z,
         diff, p.x, p.y, p.z);
    if (diff > 0.5f) {
        slog("seamless: WARNING the handover moved her %.3f units in the world\n", diff);
    }
    sArrivalTaken = 1;
    if (at != NULL) {
        *at = p;
    }
    if (yaw != NULL) {
        *yaw = y;
    }
    return 1;
}

int roomseamless_arrived(void) {
    return sPending && sArrivalTaken;
}

int roomseamless_hewie_here(int id) {
    HBody *b;
    Vec3 p, head, point;
    float dx, dz, d, y, h;
    int tri;

    if (!sPending || sHewieTaken) {
        return 0;
    }
    b = hactor_body(id);
    if (b == NULL || !b->has || b->room != sFrom) {
        return 0;
    }
    dx = b->pos[0] - sFromPos.x;
    dz = b->pos[2] - sFromPos.z;
    d = sqrtf(dx * dx + dz * dz);
    if (d > SEAMLESS_HEWIE_NEAR) {
        return 0;
    }
    p = mat4_point(&sTFromTo, vec3(b->pos[0], b->pos[1], b->pos[2]));
    /* a seam whose rooms only touch at the doorway: behind her is not a place in the new room
     * (its floor stops at the doorway), so he is not put there - he comes in by the doorway the
     * way he does from further off */
    tri = navmesh_find(&gEngine.room.nav, p, 8.0f, &h);
    if (tri < 0) {
        slog("seamless: Hewie (%.1f units from her) would be off the new room's floor: he comes in by himself\n", d);
        return 0;
    }
    y = turn_yaw(&sTFromTo, b->yaw);
    if (b->has_head) {
        head = mat4_point(&sTFromTo, vec3(b->head[0], b->head[1], b->head[2]));
    }
    if (b->has_point) {
        point = mat4_point(&sTFromTo, vec3(b->point[0], b->point[1], b->point[2]));
    }
    b->pos[0] = p.x;
    b->pos[1] = p.y;
    b->pos[2] = p.z;
    b->yaw = y;
    b->room = gEngine.room.id;
    b->tri = tri;
    if (b->has_head) {
        b->head[0] = head.x;
        b->head[1] = head.y;
        b->head[2] = head.z;
    }
    if (b->has_point) {
        b->point[0] = point.x;
        b->point[1] = point.y;
        b->point[2] = point.z;
    }
    sHewieTaken = 1;
    slog("seamless: Hewie came too (%.1f units from her)\n", d);
    return 1;
}

/* ---- each tick ---- */

int roomseamless_exit(int exit) {
    if (!seam_on() || gEngine.room.id < 0) {
        return 0;
    }
    actives_refresh(&gEngine);   /* (the scripts ask in the arrival's own tick, before the tick below) */
    /* (if the line can't be built, there is no active seam: leave the old way out alone - better a
     * fade than a trap) */
    if (act_for_exit(exit) == NULL) {
        return 0;
    }
    if (sSuppressedRoom != gEngine.room.id || sSuppressedExit != exit) {
        sSuppressedRoom = gEngine.room.id;
        sSuppressedExit = exit;
        slog("seamless: ST_%03X exit %d: the old way out (take-exit, walk-out and fade) suppressed\n",
             gEngine.room.id, exit);
    }
    return 1;
}

void roomseamless_tick(struct Engine *ee) {
    Engine *e = (Engine *)ee;
    HBody *b;
    int i;

    if (!sInit) {
        for (i = 0; i < SEAM_ACT_MAX; i++) {
            sNb[i].id = -1;
        }
        sInit = 1;
    }
    if (!seam_on() && !vr_active()) {
        return;   /* (the flat-screen game is untouched: no files read, nothing drawn) */
    }
    if (!seam_on()) {
        nb_free_all("room loading: Original");
        if (sPending) {
            slog("seamless: handover ST_%03X->ST_%03X cancelled (room loading: Original)\n", sFrom, sTo);
            sPending = 0;
        }
        return;
    }
    roomlayout_load_if_needed();
    pairs_load();
    if (e->room.id < 0) {
        return;
    }
    if (sPending) {
        if (e->room.id == sTo) {
            if (!sArrivalTaken) {
                slog("seamless: handover ST_%03X->ST_%03X: no arrival to place her\n", sFrom, sTo);
            } else {
                engine_vr_reanchor(e);   /* (the hands and body pose from her new eyes this frame) */
            }
            sPending = 0;   /* (her arrival is in; the neighbours below follow the new room) */
            sNbNextLoad = e->ticks + SEAMLESS_LOAD_AFTER;
            sTrace = sDbgLog ? 60 : 0;
        } else if (e->ticks - sStartTick > SEAMLESS_TIMEOUT) {
            slog("seamless: handover ST_%03X->ST_%03X timed out\n", sFrom, sTo);
            sPending = 0;
        } else {
            return;   /* (no second one while one is in) */
        }
    }
    actives_refresh(e);
    neighbours_update(e);
    if (sActN == 0) {
        return;
    }
    if (e->paused || e->still || gCamDir.scene) {
        return;
    }
    if (e->ticks < sCooldownUntil) {
        return;
    }
    b = hactor_body(gCamDir.player);
    if (b == NULL || !b->has || b->room != e->room.id) {
        return;
    }
    {
        int best = -1;
        float best_across = 1e30f;

        for (i = 0; i < sActN; i++) {
            ActiveSeam *s = &sAct[i];
            const Doorway *d = &s->line;
            Vec3 p = vec3(b->pos[0], b->pos[1], b->pos[2]);
            Vec3 rel = vec3_sub(p, d->centre);
            float past = vec3_dot(rel, d->normal);
            float across = vec3_dot(rel, d->tangent);
            /* a measured seam's floor ends at its line (the walk meshes stop within half a unit of it),
             * so she cannot get 3 units past it: she hands over as she reaches the line */
            float cross = d->frame ? SEAMLESS_PAST_FRAME : SEAMLESS_PAST;
            /* a doorway on another floor (stairs, the room above or below) has the same place on the
             * map: only the one at her own height is crossed */
            int level = fabsf(p.y - d->centre.y) <= SEAMLESS_LEVEL;
            int crossed = s->prev_had && s->prev_past <= cross && past > cross && past <= SEAMLESS_PAST_MAX &&
                          fabsf(across) <= d->halfwidth + SEAMLESS_MARGIN && level;

            if (crossed && fabsf(across) < best_across) {   /* (two at once: the nearer to her middle) */
                best = i;
                best_across = fabsf(across);
            }
            s->prev_past = past;
            s->prev_had = 1;
        }
        if (best >= 0) {
            const ActiveSeam *s = &sAct[best];
            Vec3 p = vec3(b->pos[0], b->pos[1], b->pos[2]);

            slog("seamless: ST_%03X exit %d crossed the line toward ST_%03X (across %.1f, height %.1f from the floor)\n",
                 e->room.id, s->my_exit, s->other, vec3_dot(vec3_sub(p, s->line.centre), s->line.tangent),
                 p.y - s->line.centre.y);
            roomseamless_go(s->my_exit);
        }
    }
}

/* ---- drawing: the neighbours beside the played room ---- */

/* the eye in each neighbour's space (T played -> neighbour) */
int roomseamless_view_begin(struct Engine *ee, Vec3 eye, Vec3 forward) {
    Engine *e = (Engine *)ee;
    int i, any = 0;

    if (!sInit || e->room.id < 0 || sActRoom != e->room.id) {
        return 0;
    }
    for (i = 0; i < sActN; i++) {
        Room *nb = nb_find(sAct[i].other);

        if (nb == NULL || !sAct[i].t_ok) {
            continue;
        }
        room_draw_view_begin(nb, mat4_point(&sAct[i].t_ab, eye), turn_vec(&sAct[i].t_ab, forward));
        any = 1;
    }
    return any;
}

/* ---- the portals: each neighbour seen through its doorway (render_portal_*) ---- */

/* a plane (world space: n . x + d >= 0 kept) in the CLIP space of `vp`: planeClip = vp^-T plane */
static int plane_to_clip(const Mat4 *vp, Vec3 n, float d, float out[4]) {
    Mat4 inv;
    float pl[4] = {n.x, n.y, n.z, d};
    int i, j;

    if (!seam_mat4_inverse(vp, &inv)) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        out[i] = 0.0f;
        for (j = 0; j < 4; j++) {
            out[i] += inv.m[i * 4 + j] * pl[j];
        }
    }
    return 1;
}

static int portal_for(Engine *e, const ActiveSeam *s, const Mat4 *vp, Vec3 eye, SeamPortal *out) {
    Room *nb = nb_find(s->other);
    const Doorway *d = &s->line;
    float side, up_hi, hw;
    Vec3 c, n, t, b, up = vec3(0.0f, 1.0f, 0.0f);
    int k, outside[4] = {0, 0, 0, 0}, behind = 0;

    memset(out, 0, sizeof(*out));
    if (nb == NULL || !s->t_ok) {
        return 0;
    }
    c = d->centre;
    n = d->normal;
    t = d->tangent;
    /* the opening a door's or an exit area's line gives is the leaf's, not the doorway's: a window
     * that small shows the room beyond as a slit with void around it. The walls round the doorway
     * hide any margin, so a line that is not a measured frame is given at least a doorway's width. */
    hw = (d->frame ? d->halfwidth : fmaxf(d->halfwidth, 18.0f)) + (d->frame ? 2.0f : 4.0f);
    up_hi = d->height + 2.0f;
    side = vec3_dot(vec3_sub(eye, c), n);
    if (vec3_len(vec3_sub(eye, c)) > 600.0f) {
        return 0;
    }
    if (door_swing(&e->room, s->my_exit) < SEAMLESS_DOOR_SHUT) {
        return 0;   /* (a door shut in the opening: nothing to see through it) */
    }
    out->t_ba = s->t_ba;
    out->room = nb;
    out->skip_door = -1;
    if (s->my_exit < ROOM_DOORS && s->other_exit >= 0 && s->other_exit < ROOM_DOORS &&
        e->room.doors[s->my_exit].present && nb->doors[s->other_exit].present) {
        out->skip_door = s->other_exit;   /* (the played room's leaf is the one drawn and grabbed) */
    }
    out->see_layer = gRoomLook.has_fog && gRoomLook.fog_early ? 8 : 0x19;
    out->tint = sDbgTint;
    if (!plane_to_clip(vp, n, -vec3_dot(n, c), out->clip_b)) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        out->clip_a[k] = -out->clip_b[k];
    }
    b = vec3_add(c, vec3_scale(n, 0.5f));   /* the mark: a little beyond the plane */
    out->mark[0] = vec3_add(vec3_sub(b, vec3_scale(t, hw)), vec3_scale(up, up_hi));
    out->mark[1] = vec3_add(vec3_add(b, vec3_scale(t, hw)), vec3_scale(up, up_hi));
    out->mark[2] = vec3_add(vec3_sub(b, vec3_scale(t, hw)), vec3_scale(up, -2.0f));
    out->mark[3] = vec3_add(vec3_add(b, vec3_scale(t, hw)), vec3_scale(up, -2.0f));
    for (k = 0; k < 4; k++) {   /* (the plane's own: the same, put back on it) */
        out->plane[k] = vec3_sub(out->mark[k], vec3_scale(n, 0.5f));
    }
    if (side >= 0.0f) {
        /* The eye is past the plane: a head through the doorway - both rooms drawn clipped to their
         * sides. Only there: the plane runs on for ever, and a room that is not convex has places
         * far from the doorway on its far side (clipping the room away there showed through the
         * whole map). An eye past it anywhere else has no portal. */
        if (side > 12.0f || fabsf(vec3_dot(vec3_sub(eye, c), t)) > hw + 6.0f ||
            fabsf(eye.y - c.y) > SEAMLESS_LEVEL + d->height) {
            return 0;
        }
        out->mode = 2;
        return 1;
    }
    for (k = 0; k < 4; k++) {   /* (the opening anywhere in view?) */
        const float *m = vp->m;
        Vec3 q = out->mark[k];
        float x = m[0] * q.x + m[4] * q.y + m[8] * q.z + m[12], y = m[1] * q.x + m[5] * q.y + m[9] * q.z + m[13];
        float w = m[3] * q.x + m[7] * q.y + m[11] * q.z + m[15];

        if (w <= 0.0f) {
            behind++;
            continue;
        }
        outside[0] += x > w;
        outside[1] += x < -w;
        outside[2] += y > w;
        outside[3] += y < -w;
    }
    if (behind == 4 || outside[0] == 4 || outside[1] == 4 || outside[2] == 4 || outside[3] == 4) {
        return 0;   /* (the opening is not in this eye's view) */
    }
    out->mode = 1;
    return 1;
}

int roomseamless_portals(struct Engine *ee, const Mat4 *vp, Vec3 eye, SeamPortal *out, int max) {
    Engine *e = (Engine *)ee;
    int i, n = 0;

    if (!seam_on() || !sDbgPortal || !sInit || vp == NULL || sActRoom != e->room.id) {
        return 0;
    }
    for (i = 0; i < sActN && n < max; i++) {
        if (portal_for(e, &sAct[i], vp, eye, &out[n])) {
            n++;
        }
    }
    if (sTrace > 0) {   /* (a picture's per-eye line for a second after a crossing: what was drawn) */
        static Uint64 last;
        Uint64 now = SDL_GetPerformanceCounter();
        int loaded = 0, k;

        for (k = 0; k < SEAM_ACT_MAX; k++) {
            loaded += sNb[k].id >= 0;
        }
        slog("seam trace: tick %ld room ST_%03X, %d seam(s), %d neighbour(s), %d portal(s)%s; %.1f ms since the last\n",
             (long)e->ticks, e->room.id, sActN, loaded, n, n > 0 ? (out[0].mode == 1 ? " (window)" : " (head past)") : "",
             last != 0 ? (double)(now - last) * 1000.0 / (double)SDL_GetPerformanceFrequency() : 0.0);
        last = now;
        sTrace--;
    }
    return n;
}

/* the seams' openings drawn flat in the room (seam-outline): green, red while a handover is
 * blocked (a shut gate or door); a short arrow for "out" */
void roomseamless_outline(struct Engine *ee, const Mat4 *vp) {
    Engine *e = (Engine *)ee;
    int i;

    if (!sDbgOutline || !seam_on() || sActRoom != e->room.id) {
        return;
    }
    for (i = 0; i < sActN; i++) {
        const Doorway *d = &sAct[i].line;
        Vec3 c = d->centre, n = d->normal, t = d->tangent, up = vec3(0.0f, 1.0f, 0.0f), q[4], a[4];
        uint32_t col = gateway_open(&e->room, sAct[i].my_exit) == 1 &&
                               door_swing(&e->room, sAct[i].my_exit) >= SEAMLESS_DOOR_OPEN
                           ? 0x00FF4070u
                           : 0xFF202080u;

        q[0] = vec3_add(vec3_sub(c, vec3_scale(t, d->halfwidth)), vec3_scale(up, d->height));
        q[1] = vec3_add(vec3_add(c, vec3_scale(t, d->halfwidth)), vec3_scale(up, d->height));
        q[2] = vec3_sub(c, vec3_scale(t, d->halfwidth));
        q[3] = vec3_add(c, vec3_scale(t, d->halfwidth));
        render_debug_quad(vp, q, col);
        /* the arrow: a flat shaft along n on the floor, and a head */
        a[0] = vec3_add(vec3_add(c, vec3_scale(up, 0.3f)), vec3_scale(t, -0.8f));
        a[1] = vec3_add(vec3_add(c, vec3_scale(up, 0.3f)), vec3_scale(t, 0.8f));
        a[2] = vec3_add(a[0], vec3_scale(n, 12.0f));
        a[3] = vec3_add(a[1], vec3_scale(n, 12.0f));
        render_debug_quad(vp, a, 0xFFFF00C0u);
        a[0] = vec3_add(vec3_add(vec3_add(c, vec3_scale(up, 0.3f)), vec3_scale(n, 12.0f)), vec3_scale(t, -3.0f));
        a[1] = vec3_add(vec3_add(vec3_add(c, vec3_scale(up, 0.3f)), vec3_scale(n, 12.0f)), vec3_scale(t, 3.0f));
        a[2] = vec3_add(vec3_add(c, vec3_scale(up, 0.3f)), vec3_scale(n, 17.0f));
        a[3] = a[2];
        render_debug_quad(vp, a, 0xFFFF00C0u);
    }
}

/* a game tick's time (main.c): with seam-log on, the slow ones are named - a crossing's hitch is one of them */
void roomseamless_note_tick(long tick, double ms) {
    if (sDbgLog && ms > 5.0) {
        slog("seam trace: tick %ld took %.1f ms%s\n", tick, ms, sPending ? " (a crossing is in it)" : "");
    }
}

void roomseamless_debug(int what, int on) {
    switch (what) {
    case SEAM_DBG_PORTAL: sDbgPortal = on != 0; break;
    case SEAM_DBG_OUTLINE: sDbgOutline = on != 0; break;
    case SEAM_DBG_TINT: sDbgTint = on != 0; break;
    case SEAM_DBG_LOG: sDbgLog = on != 0; break;
    }
}

/* seam-info: what the played room's seams are doing now */
void roomseamless_info(char *buf, size_t n) {
    Engine *e = &gEngine;
    HBody *b = hactor_body(gCamDir.player);
    int i, len;

    if (sActRoom != e->room.id || (sActN == 0 && sDropN == 0)) {
        snprintf(buf, n, "seam: ST_%03X has no seam\n", e->room.id);
        return;
    }
    len = snprintf(buf, n, "seam: ST_%03X has %d seam(s), %d dropped\n", e->room.id, sActN, sDropN);
    for (i = 0; i < sDropN && len > 0 && (size_t)len < n; i++) {
        len += snprintf(buf + len, n - (size_t)len, " exit %d NOT USED: %s (the old way out applies; its box is hidden, so it shows void)\n", sDrop[i].exit,
                        sDrop[i].why);
    }
    for (i = 0; i < sActN && len > 0 && (size_t)len < n; i++) {
        const ActiveSeam *s = &sAct[i];
        const Doorway *d = &s->line;

        len += snprintf(buf + len, n - (size_t)len, " exit %d <-> ST_%03X exit %d (%s); gate %s; door swing %.0f; neighbour %s;"
                        " T move %.2f %.2f %.2f turn %.3f\n",
                        s->my_exit, s->other, s->other_exit, d->frame ? "frames" : "layout/auto",
                        gateway_open(&e->room, s->my_exit) == 1 ? "open" : "SHUT", door_swing(&e->room, s->my_exit),
                        nb_find(s->other) != NULL ? "ready" : "not loaded", s->t_ab.m[12], s->t_ab.m[13],
                        s->t_ab.m[14], atan2f(s->t_ab.m[8], s->t_ab.m[0]));
        if (len > 0 && (size_t)len < n && b != NULL && b->has) {
            Vec3 p = vec3(b->pos[0], b->pos[1], b->pos[2]);
            float past = vec3_dot(vec3_sub(p, d->centre), d->normal);

            len += snprintf(buf + len, n - (size_t)len, "   Fiona %.1f units %s the line (across %.1f of %.1f)\n",
                            fabsf(past), past >= 0.0f ? "past" : "before", vec3_dot(vec3_sub(p, d->centre), d->tangent),
                            d->halfwidth);
        }
    }
}

/* ---- the look (fog, tint, bloom) across a doorway ----
 * The played room's look is one set of numbers for the whole picture, so it changed all at once
 * when the played room did. Near a seam it is blended with the other room's by how far the eye is
 * from the plane - half and half on the plane, all the played room's from `LOOK_REACH` units away -
 * the same on both sides of it, so a handover (which swaps which room is "played") does not show. */
#define LOOK_REACH 80.0f
#define LOOK_SIDE 40.0f   /* how far beside the opening the blend still reaches */

static float lerpf(float a, float b, float f) {
    return a + (b - a) * f;
}

/* one colour (r g b a) of the two looks: a side without the effect has the other's colour and no
 * strength, so only the strength fades */
static void mix_colour(float *out, const float *a, int has_a, const float *b, int has_b, float f) {
    float ca[4], cb[4];
    int k;

    memcpy(ca, has_a ? a : b, sizeof(ca));
    memcpy(cb, has_b ? b : a, sizeof(cb));
    if (!has_a) {
        ca[3] = 0.0f;
    }
    if (!has_b) {
        cb[3] = 0.0f;
    }
    for (k = 0; k < 4; k++) {
        out[k] = lerpf(ca[k], cb[k], f);
    }
}

static void look_mix(RoomLook *a, const RoomLook *b, float f) {
    RoomLook o = *a;

    if (a->has_fog || b->has_fog) {
        mix_colour(o.fog_near_color, a->fog_near_color, a->has_fog, b->fog_near_color, b->has_fog, f);
        mix_colour(o.fog_far_color, a->fog_far_color, a->has_fog, b->fog_far_color, b->has_fog, f);
        o.fog_near = a->has_fog && b->has_fog ? lerpf(a->fog_near, b->fog_near, f) : a->has_fog ? a->fog_near : b->fog_near;
        o.fog_far = a->has_fog && b->has_fog ? lerpf(a->fog_far, b->fog_far, f) : a->has_fog ? a->fog_far : b->fog_far;
        o.fog_early = a->has_fog ? a->fog_early : b->fog_early;
        o.has_fog = 1;
    }
    if (a->has_tint || b->has_tint) {
        mix_colour(o.tint_glow, a->tint_glow, a->has_tint, b->tint_glow, b->has_tint, f);
        mix_colour(o.tint_contrast, a->tint_contrast, a->has_tint, b->tint_contrast, b->has_tint, f);
        o.tint_sharp = a->has_tint ? a->tint_sharp : b->tint_sharp;
        o.has_tint = 1;
    }
    if (a->has_bloom || b->has_bloom) {
        mix_colour(o.bloom, a->bloom, a->has_bloom, b->bloom, b->has_bloom, f);
        if (!a->has_bloom) {
            o.bloom_subtract = b->bloom_subtract;
            o.bloom_mode = b->bloom_mode;
        }
        o.has_bloom = 1;
    }
    *a = o;
}

int roomseamless_look(struct Engine *ee, Vec3 eye, RoomLook *look) {
    Engine *e = (Engine *)ee;
    int i, best = -1;
    float best_f = 0.0f;

    if (!seam_on() || !sInit || sActRoom != e->room.id || look == NULL) {
        return 0;
    }
    for (i = 0; i < sActN; i++) {
        const Doorway *d = &sAct[i].line;
        Vec3 rel = vec3_sub(eye, d->centre);
        float dist = -vec3_dot(rel, d->normal);   /* (before the plane: positive) */
        float lateral = fabsf(vec3_dot(rel, d->tangent)) - d->halfwidth;
        float f;
        int k;

        for (k = 0; k < SEAM_ACT_MAX && sNb[k].id != sAct[i].other; k++) {
        }
        if (k >= SEAM_ACT_MAX || door_swing(&e->room, sAct[i].my_exit) < SEAMLESS_DOOR_SHUT ||
            fabsf(eye.y - d->centre.y) > SEAMLESS_LEVEL + d->height) {
            continue;
        }
        if (dist > LOOK_REACH || dist < -LOOK_REACH || lateral > LOOK_SIDE) {
            continue;
        }
        lateral = lateral > 0.0f ? lateral : 0.0f;
        f = 0.5f - 0.5f * (dist / LOOK_REACH);
        f *= 1.0f - lateral / LOOK_SIDE;
        if (f > best_f) {
            best_f = f;
            best = k;
        }
    }
    if (best < 0 || best_f <= 0.0005f) {
        return 0;
    }
    look_mix(look, &sNbLook[best], best_f);
    return 1;
}
/* ---- the doorways' void boxes (the shells that hid what lies beyond them) ---- */

int roomseamless_hide_draw(const Room *r, const MeshDraw *d) {
    const uint8_t *mask;

    if (!seam_on() || r == NULL || d == NULL || r->id < 0) {
        return 0;
    }
    if (r->mesh.d == NULL || (const uint8_t *)d < (const uint8_t *)r->mesh.d ||
        (const uint8_t *)d >= (const uint8_t *)(r->mesh.d + r->mesh.nd)) {
        return 0;   /* (a moving batch: not the still room's shell) */
    }
    mask = void_mask(r);
    return mask != NULL && mask[d - r->mesh.d];
}