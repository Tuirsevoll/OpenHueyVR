/* Seamless room loading: see roomseamless.h. */
#include "roomseamless.h"
#include "engine.h"
#include "roomlayout.h"
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
#define SEAMLESS_PAST_MAX 20.0f   /* ... and no further past it (a save load's place is not a crossing) */
#define SEAMLESS_MARGIN 8.0f      /* across the doorway, past its width */
#define SEAMLESS_HEWIE_NEAR 40.0f /* how near Hewie comes too */
#define SEAMLESS_COOLDOWN 15      /* ticks (0.5 s) between handovers */
#define SEAMLESS_TIMEOUT 120      /* ticks a handover may take before it is forgotten */

typedef struct SeamlessPair {
    int a, exit_a, b, exit_b;
} SeamlessPair;

static SeamlessPair sPair[SEAMLESS_PAIRS_MAX];
static int sPairN;
static int sPairsLoaded;

static Room sNeighbour;          /* the other room, drawn only */
static int sInit;

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

/* the previous tick's side of the line: a handover is a crossing, not merely being past it (a
 * save's place or a door's arrival can be past it) */
static float sPrevPast;
static int sPrevHad, sPrevRoom = -1;

/* the gateway's keep-Fiona-off flags were cleared in this room (log when the count changes) */
static int sGatewayRoom = -2;
static int sGatewayCleared = -1;
static int sGatewayState = -3;

/* one line to the console (and the log) */
static void slog(const char *fmt, ...) {
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s", line);
}

static void room_name(int id, char *out, size_t n) {
    if (id < 0) {
        snprintf(out, n, "none");
    } else {
        snprintf(out, n, "ST_%03X", id);
    }
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
                sPair[sPairN++] = (SeamlessPair){a, ea, b, eb};
                fprintf(stderr, "seamless: pair ST_%03X %d - ST_%03X %d\n", a, ea, b, eb);
            } else if (ln + 1 < sizeof(line)) {
                line[ln++] = c;
            }
        }
        free(data);
    }
    if (missing) {
        fprintf(stderr, "seamless: no PROPS/seamless.txt\n");
    }
}

/* the seamless doorway of the played room: its exit, the other room and that room's exit */
static int pair_for(int room, int *my_exit, int *other, int *other_exit) {
    int base, i;

    pairs_load();
    base = roomlayout_base_id(room);
    if (base < 0) {
        return 0;
    }
    for (i = 0; i < sPairN; i++) {
        if (sPair[i].a == base) {
            *my_exit = sPair[i].exit_a;
            *other = sPair[i].b;
            *other_exit = sPair[i].exit_b;
            return 1;
        }
        if (sPair[i].b == base) {
            *my_exit = sPair[i].exit_b;
            *other = sPair[i].a;
            *other_exit = sPair[i].exit_a;
            return 1;
        }
    }
    return 0;
}

/* The gate's own state, from what its scripts have applied: open 1, shut 0. ST_001 shows the
 * open / shut gate parts (groups $15 / $16); ST_002 shows its open part (group $1B). Until one
 * is shown it is treated as shut, so the asset's default cage place cannot open it early. A shut
 * gate keeps the gateway closed and its box solid, as the original room's story has it. */
static int gateway_open(const Room *r) {
    uint32_t open_part, shut_part, gate_open_part;

    if (r == NULL) {
        return -1;
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

/* the pair's base rooms: the black void shells at the gateway (the original's occluders) are
 * drawn in both and are what this feature takes away */
static int room_is_paired(int id) {
    static int last_id = -2, last_result;
    int base, i;

    if (id < 0) {
        return 0;
    }
    if (id == last_id) {
        return last_result;
    }
    last_id = id;
    last_result = 0;
    pairs_load();
    base = roomlayout_base_id(id);
    if (base < 0) {
        return 0;
    }
    for (i = 0; i < sPairN; i++) {
        if (sPair[i].a == base || sPair[i].b == base) {
            last_result = 1;
            break;
        }
    }
    return last_result;
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

/* The gateway's void shell: a tall, square, unlit group-0 solid draw (12 triangles in ST_001,
 * 8 in ST_002; a room box, not the gate cage or the gatehouse's walls). */
static int draw_is_void_box(const Room *r, const MeshDraw *d, Vec3 *lo_out, Vec3 *hi_out) {
    Vec3 lo, hi;
    int tris;

    if (d == NULL || d->part != MESH_SOLID || d->group != 0 || !draw_box(r, d, &lo, &hi)) {
        return 0;
    }
    tris = d->count / 3;
    if (tris < 6 || tris > 24) {
        return 0;
    }
    if (hi.x - lo.x < 30.0f || hi.z - lo.z < 30.0f || hi.y - lo.y < 30.0f) {
        return 0;
    }
    if (lo_out != NULL) {
        *lo_out = lo;
    }
    if (hi_out != NULL) {
        *hi_out = hi;
    }
    return 1;
}

/* the doorway's line in the played room's space: its middle, its normal (toward the other room)
 * and its width. An open passage has no door, so the line comes from its spots (0 out, 1 in: their
 * middle is the point both rooms agree on when the layout places them) and its event area or gate
 * (the line's direction and width: the box's edge most square across the way through, a gate's own
 * line). A door's line is the door: where it stands and its turn. */
typedef struct Doorway {
    Vec3 centre, normal, tangent;
    float halfwidth;
} Doorway;

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
    /* The spots: `out` (0) is beyond the doorway, on the other room's side; `in` (1) is inside
     * this room. The way through is out - in; the middle of the two is a point both rooms agree
     * on when the layout places them. */
    passage = vec3(out.x - in.x, 0.0f, out.z - in.z);
    len = vec3_len(passage);
    if (len < 0.001f) {
        return 0;
    }
    pn = vec3_scale(passage, 1.0f / len);
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

/* the line, kept for the room and exit it was built for */
static int sLineRoom = -1, sLineExit = -1, sLineOk;
static Doorway sLine;
static int sSuppressedRoom = -1, sSuppressedExit = -1;

static int doorway_line(int room_id, int exit, Doorway *d) {
    if (sLineRoom != room_id || sLineExit != exit) {
        sLineRoom = room_id;
        sLineExit = exit;
        sLineOk = doorway_build(room_id, exit, &sLine);
        if (sLineOk) {
            slog("seamless: ST_%03X exit %d line: centre %.1f %.1f %.1f normal %.3f %.3f width %.1f\n",
                 room_id, exit, sLine.centre.x, sLine.centre.y, sLine.centre.z,
                 sLine.normal.x, sLine.normal.z, sLine.halfwidth * 2.0f);
        }
    }
    if (!sLineOk) {
        return 0;
    }
    *d = sLine;
    return 1;
}

/* ---- the draw-only neighbour ---- */

static void neighbour_free(const char *why) {
    if (sNeighbour.id >= 0) {
        slog("seamless: freed ST_%03X (drawing alone: %s)\n", sNeighbour.id, why);
        room_free_view(&sNeighbour);
    }
    sNeighbour.id = -1;
}

static void neighbour_ensure(Engine *e, int other) {
    Mat4 t;
    char a[16], b[16];

    if (sNeighbour.id == other) {
        return;
    }
    if (sNeighbour.id >= 0) {
        neighbour_free("the pair's other room changed");
    }
    if (!room_load_view(&sNeighbour, other)) {
        sNeighbour.id = -1;
        slog("seamless: can't load ST_%03X to draw beside ST_%03X\n", other, e->room.id);
        return;
    }
    room_name(e->room.id, a, sizeof(a));
    room_name(other, b, sizeof(b));
    if (roomlayout_between(other, e->room.id, &t)) {
        slog("seamless: drawing %s next to %s; T %s->%s [%.4f %.4f %.4f %.4f; %.4f %.4f %.4f %.4f; "
             "%.4f %.4f %.4f %.4f]\n",
             b, a, b, a, t.m[0], t.m[4], t.m[8], t.m[12], t.m[1], t.m[5], t.m[9], t.m[13],
             t.m[2], t.m[6], t.m[10], t.m[14]);
    } else {
        slog("seamless: drawing %s next to %s (no T between them)\n", b, a);
    }
}

/* ---- the handover ---- */

void roomseamless_reset(void) {
    if (sInit && sNeighbour.id >= 0) {
        room_free_view(&sNeighbour);
    }
    sNeighbour.id = -1;
    sInit = 1;
    sPending = 0;
    sArrivalTaken = sHewieTaken = 0;
    sCooldownUntil = 0;
    sLoadTimed = 0;
    sLineRoom = sLineExit = -1;
    sLineOk = 0;
    sSuppressedRoom = sSuppressedExit = -1;
    sPrevHad = 0;
    sPrevRoom = -1;
    sGatewayRoom = -2;
    sGatewayCleared = -1;
    sGatewayState = -3;
}

int roomseamless_go(int exit) {
    Engine *e = &gEngine;
    HBody *b;
    int my, other, oe;
    Cell arg;
    int kind;

    if (!vr_active() || !gVrOptions.room_loading || sPending || exit < 0) {
        return 0;
    }
    if (!pair_for(e->room.id, &my, &other, &oe) || my != exit) {
        return 0;
    }
    if (gateway_open(&e->room) != 1) {
        return 0;   /* (the story gate is still shut: no way through until the lever) */
    }
    b = hactor_body(gCamDir.player);
    if (b == NULL || !b->has || b->room != e->room.id) {
        return 0;
    }
    if (!roomlayout_between(e->room.id, other, &sTFromTo) ||
        !roomlayout_get(e->room.id, &sWFrom) || !roomlayout_get(other, &sWTo)) {
        slog("seamless: no layout for ST_%03X -> ST_%03X: no handover\n", e->room.id, other);
        return 0;
    }
    sFrom = e->room.id;
    sTo = other;
    sExitTo = oe;
    {
        int to_exit = -1;
        int next = world_exit_leads(&e->world, e->room.id, exit, &to_exit);

        slog("seamless: go-through: ST_%03X exit %d leads to ST_%03X exit %d (the pair's other side is ST_%03X)\n",
             e->room.id, exit, next, to_exit, other);
        if (next != other) {
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


void roomseamless_load_begin(int id) {
    HBody *b = hactor_body(gCamDir.player);

    /* (a room (re)load, a save's included: its place is not a step across the line) */
    sPrevHad = 0;
    sPrevRoom = -1;
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

/* The room's largest Fiona-walkable nav component: the gateway's box is a small island in the
 * story gate's floor, so an arrival converted into it would trap her. Use the main room floor
 * instead of the doorway's own spot (that spot is in the box too). Returns the point (its
 * triangle's middle) in *out, and 1 if the exact point was not on the main floor and out was used. */
static int arrival_walkable(Vec3 exact, Vec3 *out, float *shift) {
    Room *r = &gEngine.room;
    NavMesh *n = &r->nav;
    HBody *b = hactor_body(gCamDir.player);
    uint32_t mask = b != NULL ? b->mask : 0;
    Vec3 box_lo[8], box_hi[8];
    int nboxes = 0, exact_in_box = 0;
    float h;
    int tri = navmesh_find(n, exact, 8.0f, &h);
    int *comp, *queue, head, tail, i, j, start, best_comp = -1, best_size = 0, best = -1;
    float best_d = 1e30f;

    *out = exact;
    *shift = 0.0f;
    for (i = 0; i < r->mesh.nd && nboxes < (int)(sizeof(box_lo) / sizeof(box_lo[0])); i++) {
        if (draw_is_void_box(r, &r->mesh.d[i], &box_lo[nboxes], &box_hi[nboxes])) {
            if (exact.x >= box_lo[nboxes].x && exact.x <= box_hi[nboxes].x &&
                exact.y >= box_lo[nboxes].y && exact.y <= box_hi[nboxes].y &&
                exact.z >= box_lo[nboxes].z && exact.z <= box_hi[nboxes].z) {
                exact_in_box = 1;
            }
            nboxes++;
        }
    }
    if (n->ntris <= 0) {
        return 0;
    }
    comp = malloc((size_t)n->ntris * sizeof(int));
    queue = malloc((size_t)n->ntris * sizeof(int));
    if (comp == NULL || queue == NULL) {
        free(comp);
        free(queue);
        return 0;
    }
    for (i = 0; i < n->ntris; i++) {
        comp[i] = -1;
    }
    for (start = 0; start < n->ntris; start++) {
        int count = 0, cid;

        if (comp[start] >= 0 || (n->tris[start].flags & mask)) {
            continue;
        }
        cid = start;
        comp[start] = cid;
        queue[0] = start;
        head = 0;
        tail = 1;
        while (head < tail) {
            int t = queue[head++];

            count++;
            for (i = 0; i < 3; i++) {
                int u = n->tris[t].next[i];

                if (u < 0 || u >= n->ntris || comp[u] >= 0 || (n->tris[u].flags & mask)) {
                    continue;
                }
                comp[u] = cid;
                queue[tail++] = u;
            }
        }
        if (count > best_size) {
            best_size = count;
            best_comp = cid;
        }
    }
    if (!exact_in_box && tri >= 0 && tri < n->ntris && comp[tri] == best_comp) {
        free(comp);
        free(queue);
        return 0;   /* (the converted place is on the room's own floor, outside the box) */
    }
    for (i = 0; i < n->ntris; i++) {
        Vec3 c;
        float d;
        int in_box = 0;

        if (comp[i] != best_comp) {
            continue;
        }
        c = navmesh_center(n, i);
        if (exact_in_box) {
            for (j = 0; j < nboxes; j++) {
                if (c.x >= box_lo[j].x && c.x <= box_hi[j].x && c.y >= box_lo[j].y &&
                    c.y <= box_hi[j].y && c.z >= box_lo[j].z && c.z <= box_hi[j].z) {
                    in_box = 1;
                    break;
                }
            }
            if (in_box) {
                continue;   /* (outside the gateway's black shell) */
            }
        }
        d = (c.x - exact.x) * (c.x - exact.x) + (c.z - exact.z) * (c.z - exact.z);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    free(comp);
    free(queue);
    if (best < 0) {
        return 0;
    }
    *out = navmesh_center(n, best);
    *shift = sqrtf(best_d);
    return 1;
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
        Vec3 walked;
        float shift = 0.0f;

        if (arrival_walkable(p, &walked, &shift)) {
            slog("seamless: arrival point is in/behind the gateway's void box; using the nearest "
                 "walkable point outside it, %.1f units away\n", shift);
            p = walked;
        }
    }
    log_doorway_after_arrival(p);
    /* her look is her heading; the camera's yaw turns by the same matrix, so the room does not
     * spin in the headset (a pure move, as this pair, turns it by 0) */
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
    float dx, dz, d, y;

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
    {
        float h;

        b->tri = navmesh_find(&gEngine.room.nav, p, 8.0f, &h);
    }
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
    int my, other, oe;
    Doorway d;

    if (!vr_active() || !gVrOptions.room_loading) {
        return 0;
    }
    if (!pair_for(gEngine.room.id, &my, &other, &oe) || my != exit) {
        return 0;
    }
    /* (if the line can't be built, leave the old way out alone: better a fade than a trap) */
    if (!doorway_line(gEngine.room.id, exit, &d)) {
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
    int my, other, oe;
    HBody *b;
    Doorway d;

    if (!sInit) {
        sNeighbour.id = -1;
        sInit = 1;
    }
    if (!vr_active()) {
        return;   /* (the flat-screen game is untouched: no files read, nothing drawn) */
    }
    if (!gVrOptions.room_loading) {
        if (sNeighbour.id >= 0) {
            neighbour_free("room loading: Original");
        }
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
    roomseamless_open_gateway(&e->room);   /* (the black shell's keep-off ring: open it) */
    if (sPending) {
        if (e->room.id == sTo) {
            if (!sArrivalTaken) {
                slog("seamless: handover ST_%03X->ST_%03X: no arrival to place her\n", sFrom, sTo);
            } else {
                engine_vr_reanchor(e);   /* (the hands and body pose from her new eyes this frame) */
            }
            sPending = 0;   /* (her arrival is in; the neighbour below follows the new room) */
        } else if (e->ticks - sStartTick > SEAMLESS_TIMEOUT) {
            slog("seamless: handover ST_%03X->ST_%03X timed out\n", sFrom, sTo);
            sPending = 0;
        } else {
            return;   /* (no second one while one is in) */
        }
    }
    if (!pair_for(e->room.id, &my, &other, &oe)) {
        if (sNeighbour.id >= 0) {
            neighbour_free("the room is not in a seamless pair");
        }
        return;
    }
    neighbour_ensure(e, other);
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
    if (!doorway_line(e->room.id, my, &d)) {
        return;
    }
    {
        Vec3 p = vec3(b->pos[0], b->pos[1], b->pos[2]);
        Vec3 rel = vec3_sub(p, d.centre);
        float past = vec3_dot(rel, d.normal);
        float across = vec3_dot(rel, d.tangent);

        if (sPrevRoom != e->room.id) {
            sPrevHad = 0;   /* (nothing to cross from in a room just come into) */
            sPrevRoom = e->room.id;
        }
        if (sPrevHad && sPrevPast <= SEAMLESS_PAST && past > SEAMLESS_PAST && past <= SEAMLESS_PAST_MAX &&
            fabsf(across) <= d.halfwidth + SEAMLESS_MARGIN) {
            roomseamless_go(my);
        }
        sPrevPast = past;
        sPrevHad = 1;
    }
}


/* ---- drawing: the neighbour beside the played room ---- */

/* the eye in the neighbour's space (T played -> neighbour) */
int roomseamless_view_begin(struct Engine *ee, Vec3 eye, Vec3 forward) {
    Engine *e = (Engine *)ee;
    Mat4 t;

    if (!sInit || sNeighbour.id < 0 || e->room.id < 0) {
        return 0;
    }
    if (!roomlayout_between(e->room.id, sNeighbour.id, &t)) {
        return 0;
    }
    room_draw_view_begin(&sNeighbour, mat4_point(&t, eye), turn_vec(&t, forward));
    return 1;
}

int roomseamless_draw(struct Engine *ee, const Mat4 *view_proj, int layer) {
    Engine *e = (Engine *)ee;
    int see_layer = gRoomLook.has_fog && gRoomLook.fog_early ? 8 : 0x19;
    Mat4 t, nvp;
    int skip = -1;
    int my, other, oe;

    if (!sInit || sNeighbour.id < 0 || (layer != 1 && layer != see_layer)) {
        return 0;   /* (solid and see-through only: no glows, bloom mask or the later passes) */
    }
    if (!roomlayout_between(sNeighbour.id, e->room.id, &t)) {
        return 0;
    }
    nvp = mat4_mul(*view_proj, t);
    if (pair_for(e->room.id, &my, &other, &oe) && other == sNeighbour.id &&
        e->room.doors[my].present && sNeighbour.doors[oe].present) {
        /* (the shared doorway has a door on both sides: the played room's is the one drawn and
         * grabbed; the neighbour's copy would be a second leaf in the same place) */
        skip = oe;
    }
    room_draw_view(&sNeighbour, &nvp, layer, skip);
    return 1;
}

/* ---- the gateway's void boxes (the black shells) ---- */

int roomseamless_hide_draw(const Room *r, const MeshDraw *d) {
    if (!vr_active() || !gVrOptions.room_loading || r == NULL || d == NULL || !room_is_paired(r->id)) {
        return 0;
    }
    if (r->mesh.d == NULL || (const uint8_t *)d < (const uint8_t *)r->mesh.d ||
        (const uint8_t *)d >= (const uint8_t *)(r->mesh.d + r->mesh.nd)) {
        return 0;   /* (a moving batch: not the still room's black shell) */
    }
    return draw_is_void_box(r, d, NULL, NULL);
}

void roomseamless_open_gateway(Room *r) {
    Vec3 lo[8], hi[8];
    Doorway d;
    int state, n = 0, cleared = 0, my, other, oe, i, j;

    if (!vr_active() || !gVrOptions.room_loading || r == NULL || !room_is_paired(r->id)) {
        return;
    }
    state = gateway_open(r);
    if (state != 1) {
        if (sGatewayRoom != r->id || sGatewayState != state) {
            sGatewayRoom = r->id;
            sGatewayState = state;
            sGatewayCleared = -1;
            slog("seamless: ST_%03X gateway: gate %s; the gateway box stays solid\n",
                 r->id, state == 0 ? "shut" : "not known yet");
        }
        return;
    }
    if (!pair_for(r->id, &my, &other, &oe) || !doorway_line(r->id, my, &d)) {
        return;
    }
    for (i = 0; i < r->mesh.nd && n < (int)(sizeof(lo) / sizeof(lo[0])); i++) {
        Vec3 a, b;

        if (draw_is_void_box(r, &r->mesh.d[i], &a, &b) &&
            d.centre.x >= a.x - 5.0f && d.centre.x <= b.x + 5.0f &&
            d.centre.z >= a.z - 5.0f && d.centre.z <= b.z + 5.0f) {
            lo[n] = a;
            hi[n] = b;
            n++;
        }
    }
    for (i = 0; i < r->nav.ntris; i++) {
        Vec3 c;
        /* (Fiona's own blocking flags: solid-floor 8, keeps-fiona-off 10, barred 20000,
         * keeps-people-off 8000000 / 20000000, grass/water 2000000 - scripts/floor-flags.fs) */
        const uint32_t fiona_blocks = 0x28020018u;

        if (!(r->nav.tris[i].flags & fiona_blocks)) {
            continue;
        }
        c = navmesh_center(&r->nav, i);
        for (j = 0; j < n; j++) {
            if (c.x >= lo[j].x - 10.0f && c.x <= hi[j].x + 10.0f &&
                c.z >= lo[j].z - 10.0f && c.z <= hi[j].z + 10.0f &&
                c.y >= lo[j].y - 6.0f && c.y <= hi[j].y + 6.0f) {
                r->nav.tris[i].flags &= ~fiona_blocks;
                cleared++;
                break;
            }
        }
    }
    if (sGatewayRoom != r->id || sGatewayCleared != cleared || sGatewayState != 1) {
        sGatewayRoom = r->id;
        sGatewayCleared = cleared;
        sGatewayState = 1;
        slog("seamless: ST_%03X gateway: open gate, %d void box(es), opened %d blocking nav triangle(s)\n",
             r->id, n, cleared);
    }
}

