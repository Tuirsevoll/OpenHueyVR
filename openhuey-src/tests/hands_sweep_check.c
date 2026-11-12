/* The hands' wall sweep (src/game/vrhands.c hand_path_hits_wall, hand_path_hits_door,
 * hand_path_hits_placed) checked: a hand's straight path across a wall is a hit, one well clear of
 * it is not, and a flat face is a face like any other - a path along the floor, or down onto a
 * table's top, is a hit, while one staying well above the top is not. A fast sweep from one side of
 * a thin wall to the other - both ends outside the skin - is a hit too, as the hand passed through.
 * A door leaf counts on any face (a shut door's face: crossed, and not when it is the door that
 * hand holds; well beside it, no), and so does a placed object, its top included: moved and turned
 * by placed.c's own placement, it is found where it now is and not where it was. The arm's stop
 * (path_stop) is checked too: a path into a wall stops 0.40 short of it and asking again from
 * there gives the same stop, while a path clear of the wall - or a hand already at it - is let
 * through. Detection only: nothing is blocked, so only the sweeps are asked; the rooms are built
 * by hand here.
 *
 *   hands-sweep-check */
#include <stdio.h>
#include <string.h>

#include "../src/game/engine.h"
#include "../src/game/loot.h"
#include "../src/game/placed.h"
#include "../src/game/progress.h"
#include "../src/game/room.h"
#include "../src/game/vrhands.h"
#include "../src/hactor/hactor.h"
#include "../src/platform/vr.h"

/* ---- the game that vrhands.c is linked with (it is whole): none of it runs here, since only the
 * sweep is asked and the room is handed to it directly ---- */
Engine gEngine;
Progress gProgress;

void engine_sound_at(uint32_t id, int bank, Vec3 at) {
    (void)id;
    (void)bank;
    (void)at;
}

void hactor_set_blow_at(int (*fn)(float out[3])) { (void)fn; }

Vec3 actor_center(const Actor *a) {
    (void)a;
    return vec3(0.0f, 0.0f, 0.0f);
}

float actor_radius(const Actor *a) {
    (void)a;
    return 0.0f;
}

void placed_anim(PlacedSet *s, Placed *p, int id, int loop) {
    (void)s;
    (void)p;
    (void)id;
    (void)loop;
}

void placed_frame_set(PlacedSet *s, Placed *p, int frame) {
    (void)s;
    (void)p;
    (void)frame;
}

/* src/game/placed.c placed_world, line for line: scaled, turned about X, then Y, then Z, then
 * placed (the original's obj_mvp). The check does not link placed.c, so it stands in here - the
 * same fields of the same object, so a placed thing's real place and turn are used. */
Mat4 placed_world(const struct Placed *p) {
    float cx = cosf(p->rot.x), sx = sinf(p->rot.x), cy = cosf(p->rot.y), sy = sinf(p->rot.y);
    float cz = cosf(p->rot.z), sz = sinf(p->rot.z);
    Mat4 rx = mat4_identity(), ry = mat4_identity(), rz = mat4_identity(), t = mat4_identity(), sc = mat4_identity();

    rx.m[5] = cx;  rx.m[6] = sx;  rx.m[9] = -sx;  rx.m[10] = cx;
    ry.m[0] = cy;  ry.m[2] = -sy; ry.m[8] = sy;   ry.m[10] = cy;
    rz.m[0] = cz;  rz.m[1] = sz;  rz.m[4] = -sz;  rz.m[5] = cz;
    sc.m[0] = sc.m[5] = sc.m[10] = p->scale;
    t.m[12] = p->pos.x;
    t.m[13] = p->pos.y;
    t.m[14] = p->pos.z;
    return mat4_mul(t, mat4_mul(rz, mat4_mul(ry, mat4_mul(rx, sc))));
}

void room_door_swing(Room *r, int exit, float degrees, int at_once) {
    (void)r;
    (void)exit;
    (void)degrees;
    (void)at_once;
}

void room_door_passage(Room *r, int exit, int open, uint32_t locks) {
    (void)r;
    (void)exit;
    (void)open;
    (void)locks;
}

void loot_maintain(void) {}

void loot_draw_held(int item, const Mat4 *world, const Mat4 *view_proj) {
    (void)item;
    (void)world;
    (void)view_proj;
}

int loot_nearest(Vec3 hand, int skip, float *d2) {
    (void)hand;
    (void)skip;
    (void)d2;
    return -1;
}

void loot_grab(int index, int side) {
    (void)index;
    (void)side;
}

void loot_follow(int index, Vec3 at, Vec3 right, Vec3 up, Vec3 fwd) {
    (void)index;
    (void)at;
    (void)right;
    (void)up;
    (void)fwd;
}

void loot_let_go(int index, int side) {
    (void)index;
    (void)side;
}

int loot_claims_placed(int placed_index) {
    (void)placed_index;
    return 0;
}

int vr_active(void) { return 0; }

void vr_body(float *height_m, float *scale) {
    (void)height_m;
    (void)scale;
}

int vr_anchor_get(Vec3 *eye, float *yaw) {
    (void)eye;
    (void)yaw;
    return 0;
}

int vr_controller(int hand, VrController *out) {
    (void)hand;
    (void)out;
    return 0;
}

void vr_haptic(int hand, float amplitude, float seconds) {
    (void)hand;
    (void)amplitude;
    (void)seconds;
}

int vr_head_local(Vec3 *out) {
    (void)out;
    return 0;
}

int vr_head_forward(Vec3 *out) {
    (void)out;
    return 0;
}

/* ---- the rooms ---- */

/* a thin wall: the plane x = 0, three up and six wide, its normal along X (steep: a wall) */
static MeshVertex sWall[6] = {
    {0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 3.0f}, {0.0f, 3.0f, 3.0f},
    {0.0f, 0.0f, -3.0f}, {0.0f, 3.0f, 3.0f}, {0.0f, 3.0f, -3.0f},
};
static MeshDraw sWallDraw = {0, 6, -1, MESH_SOLID, 0, 0, 0, 0, 0, 0, 0};

/* a floor: the plane y = 0, its normal along Y (flat - and now a face like any other) */
static MeshVertex sFloor[3] = {
    {-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f},
};
static MeshDraw sFloorDraw = {0, 3, -1, MESH_SOLID, 0, 0, 0, 0, 0, 0, 0};

/* a table's top in the room's own mesh: the plane y = 1, 10 by 10 */
static MeshVertex sTop[3] = {
    {-5.0f, 1.0f, -5.0f}, {5.0f, 1.0f, -5.0f}, {0.0f, 1.0f, 5.0f},
};
static MeshDraw sTopDraw = {0, 3, -1, MESH_SOLID, 0, 0, 0, 0, 0, 0, 0};

/* one batch of one triangle list, in a room of its own */
static void room_of(Room *r, MeshVertex *v, int nv, MeshDraw *d, int nd) {
    memset(r, 0, sizeof(*r));
    r->id = 0;
    r->mesh.v = v;
    r->mesh.nv = nv;
    r->mesh.d = d;
    r->mesh.nd = nd;
}

/* how many of the room's triangles the sweep has to test: those of a shown draw that are not
 * slivers (their edges' cross product something) - every face, whichever way it faces */
static int face_tris(const Room *r) {
    const RoomMesh *m = &r->mesh;
    int i, n, faces = 0;

    for (i = 0; i < m->nd; i++) {
        int end;
        int group = m->d[i].group;

        if (m->d[i].count < 3 || m->d[i].mask || (group >= 256) ||
            (group > 0 && !(r->groups[group >> 5] >> (group & 31) & 1u))) {
            continue;
        }
        end = m->d[i].first + m->d[i].count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (n = m->d[i].first; n + 2 < end; n += 3) {
            Vec3 a = vec3(m->v[n].x, m->v[n].y, m->v[n].z);
            Vec3 b = vec3(m->v[n + 1].x, m->v[n + 1].y, m->v[n + 1].z);
            Vec3 c = vec3(m->v[n + 2].x, m->v[n + 2].y, m->v[n + 2].z);

            if (vec3_len(vec3_cross(vec3_sub(b, a), vec3_sub(c, a))) > 0.0f) {
                faces++;
            }
        }
    }
    return faces;
}

/* ---- the door and the placed things ---- */

/* a shut door: its leaf in its own frame (12 wide, 22 tall, 1 thick), standing at the origin, no
 * turn. Only the room's doors are read, so the engine is a stand of its own. */
static Engine sDoorEng;

static void door_of(Engine *e) {
    memset(e, 0, sizeof(*e));
    e->room.doors[0].present = 1;
}

/* a placed thing: a face 8 wide and 4 tall at z = 0, in the object's own space (it is placed at
 * the origin, unturned, to begin with: placed_world, stubbed below with placed.c's own rules) */
static MeshVertex sThing[6] = {
    {-4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 0.0f},
    {-4.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 0.0f}, {-4.0f, 4.0f, 0.0f},
};

/* and a table's top: a flat quad at y = 2, 8 by 8 (every face counts, so this one does too) */
static MeshVertex sTableTop[6] = {
    {-4.0f, 2.0f, -4.0f}, {4.0f, 2.0f, -4.0f}, {4.0f, 2.0f, 4.0f},
    {-4.0f, 2.0f, -4.0f}, {4.0f, 2.0f, 4.0f}, {-4.0f, 2.0f, 4.0f},
};

/* one placed object of the room, its mesh `v` in its own space, unturned and at the origin */
static void placed_of(PlacedSet *s, MeshVertex *v, int nv) {
    memset(s, 0, sizeof(*s));
    s->n = 1;
    s->p[0].shown = 1;
    s->p[0].scale = 1.0f;
    s->p[0].mesh.v = v;
    s->p[0].mesh.nv = nv;
}

static int sFails;

static void want(const char *what, int got, int expected) {
    if (got == expected) {
        printf("hands: %s -> %d (pass)\n", what, got);
    } else {
        printf("hands: %s -> %d, wanted %d (FAIL)\n", what, got, expected);
        sFails++;
    }
}

int main(void) {
    static Room wall, floor, table;
    static PlacedSet face, top, moved;

    room_of(&wall, sWall, 6, &sWallDraw, 1);
    room_of(&floor, sFloor, 3, &sFloorDraw, 1);
    room_of(&table, sTop, 3, &sTopDraw, 1);
    door_of(&sDoorEng);
    placed_of(&face, sThing, 6);
    placed_of(&top, sTableTop, 6);
    /* the same face moved to x = 10 and turned a quarter turn about the vertical: its plane, the
     * z = 0 one in its own space, now stands at x = 10 */
    placed_of(&moved, sThing, 6);
    moved.p[0].pos = vec3(10.0f, 0.0f, 0.0f);
    moved.p[0].rot = vec3(0.0f, 1.5707964f, 0.0f);
    /* (0.40: the skin the hands ask with) */
    want("a path across a wall", hand_path_hits_wall(&wall, vec3(-1.0f, 1.5f, 0.0f), vec3(1.0f, 1.5f, 0.0f), 0.40f), 1);
    want("a path well away from the wall",
         hand_path_hits_wall(&wall, vec3(3.0f, 1.5f, 0.0f), vec3(5.0f, 1.5f, 0.0f), 0.40f), 0);
    want("a fast sweep through the wall, both ends outside the skin",
         hand_path_hits_wall(&wall, vec3(-2.0f, 1.5f, 0.0f), vec3(2.0f, 1.5f, 0.0f), 0.40f), 1);
    want("a path along a floor (a floor counts)",
         hand_path_hits_wall(&floor, vec3(-1.0f, 0.2f, 0.0f), vec3(1.0f, 0.2f, 0.0f), 0.40f), 1);
    /* a table's top in the room's mesh, at y = 1 */
    want("a path coming down onto a table top, ending within the skin",
         hand_path_hits_wall(&table, vec3(0.0f, 2.5f, 0.0f), vec3(0.0f, 1.3f, 0.0f), 0.40f), 1);
    want("a path staying well above the table top",
         hand_path_hits_wall(&table, vec3(0.0f, 3.0f, 0.0f), vec3(0.0f, 2.0f, 0.0f), 0.40f), 0);
    /* a shut door's leaf: 12 wide, 22 tall, 1 thick, at the origin */
    want("a path across a shut door's leaf",
         hand_path_hits_door(&sDoorEng, -1, vec3(6.0f, 11.0f, -2.0f), vec3(6.0f, 11.0f, 1.0f), 0.40f), 1);
    want("the same path, that door the hand holds",
         hand_path_hits_door(&sDoorEng, 0, vec3(6.0f, 11.0f, -2.0f), vec3(6.0f, 11.0f, 1.0f), 0.40f), 0);
    want("a path well beside the door",
         hand_path_hits_door(&sDoorEng, -1, vec3(18.0f, 11.0f, -2.0f), vec3(18.0f, 11.0f, 1.0f), 0.40f), 0);
    /* a placed thing's face at z = 0, and a table's top at y = 2 */
    want("a path across a placed face",
         hand_path_hits_placed(&face, -1, vec3(0.0f, 2.0f, -2.0f), vec3(0.0f, 2.0f, 2.0f), 0.40f), 1);
    want("the same path, that object the hand holds",
         hand_path_hits_placed(&face, 0, vec3(0.0f, 2.0f, -2.0f), vec3(0.0f, 2.0f, 2.0f), 0.40f), 0);
    want("a path over a placed thing's flat top",
         hand_path_hits_placed(&top, -1, vec3(0.0f, 2.2f, -2.0f), vec3(0.0f, 2.2f, 2.0f), 0.40f), 1);
    /* the placed face moved to x = 10 and turned about the vertical */
    want("a path across the placed face where it was moved to",
         hand_path_hits_placed(&moved, -1, vec3(8.0f, 2.0f, 0.0f), vec3(12.0f, 2.0f, 0.0f), 0.40f), 1);
    want("a path only where the placed face used to be",
         hand_path_hits_placed(&moved, -1, vec3(0.0f, 2.0f, -2.0f), vec3(0.0f, 2.0f, 2.0f), 0.40f), 0);
    /* the arm's stop (path_stop) against the wall room's plane at x = 0 */
    {
        Vec3 to = vec3(3.0f, 1.5f, 0.0f);
        Vec3 stop = path_stop(&wall, vec3(-3.0f, 1.5f, 0.0f), to, 0.40f);
        Vec3 again = path_stop(&wall, stop, to, 0.40f);

        want("a path into a wall stops 0.40 short", fabsf(stop.x + 0.40f) <= 0.05f && stop.x < 0.0f, 1);
        want("asking again from the stop gives the same stop",
             fabsf(again.x - stop.x) <= 0.001f && fabsf(again.y - stop.y) <= 0.001f &&
                 fabsf(again.z - stop.z) <= 0.001f, 1);
    }
    {
        Vec3 to = vec3(5.0f, 1.5f, 0.0f);
        Vec3 stop = path_stop(&wall, vec3(3.0f, 1.5f, 0.0f), to, 0.40f);

        want("a path that never touches the wall is let through", stop.x == to.x && stop.z == to.z, 1);
    }
    {
        Vec3 to = vec3(3.0f, 1.5f, 0.0f);
        Vec3 stop = path_stop(&wall, vec3(-0.2f, 1.5f, 0.0f), to, 0.40f);

        want("a hand already at the wall is let through", stop.x == to.x && stop.z == to.z, 1);
    }
    /* a slow push: a short path whose end is in contact (2.0, the arm stop's own current value) */
    {
        Vec3 to = vec3(-1.9f, 1.5f, 0.0f);
        Vec3 stop = path_stop(&wall, vec3(-3.0f, 1.5f, 0.0f), to, 2.0f);
        Vec3 again = path_stop(&wall, stop, to, 2.0f);

        want("a slow push into the wall stops at the keep distance",
             fabsf(stop.x) >= 2.0f && fabsf(stop.x) <= 2.1f, 1);
        want("asking again from the slow push's stop gives the same stop",
             fabsf(again.x - stop.x) <= 0.001f && fabsf(again.y - stop.y) <= 0.001f &&
                 fabsf(again.z - stop.z) <= 0.001f, 1);
    }
    {
        Vec3 to = vec3(-3.0f, 1.5f, 0.0f);
        Vec3 stop = path_stop(&wall, vec3(-3.4f, 1.5f, 0.0f), to, 2.0f);

        want("a slow push that stays clear is let through", stop.x == to.x && stop.z == to.z, 1);
    }
    {
        /* (a step shorter than the 0.2 sampling: only the path's start is a sample, so its end
         * has to be tested on its own) */
        Vec3 to = vec3(-1.95f, 1.5f, 0.0f);
        Vec3 stop = path_stop(&wall, vec3(-2.05f, 1.5f, 0.0f), to, 2.0f);

        want("a push shorter than one sample is stopped too", fabsf(stop.x) >= 2.0f && fabsf(stop.x) <= 2.1f, 1);
    }
    printf("hands: faces counted: %d of 2 (the wall), %d of 1 (the floor), %d of 1 (the table top)\n",
           face_tris(&wall), face_tris(&floor), face_tris(&table));
    if (sFails != 0) {
        fprintf(stderr, "FAIL: %d of the sweep's tests\n", sFails);
        return 1;
    }
    printf("hands: the sweeps find a wall, a floor, a table top, a door's leaf or a placed thing\n");
    return 0;
}
