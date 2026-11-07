/* Arms follow the controllers. Finger shapes are the model's hand morphs (one blend): the
 * index trigger and the grip share it, and a third shape is the point when the mesh has one.
 * Doors swing in degrees (0 shut, open either way to ±90). A hand moves one only while it
 * is touching it. A shut door follows a grip on the knob. A door off the latch can be pushed
 * from either face, anywhere on the leaf. Letting go eases it by the 45° rule, unless it was
 * already moving, in which case it coasts to the stop it is heading for. A locked door never
 * moves; gripping its knob rattles once. */
#include "vrhands.h"

#include "engine.h"
#include "progress.h"
#include "room.h"

#include "../platform/vr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HOLD_NONE 0
#define HOLD_DOOR 1
#define HOLD_PROP 2

static int sHold[2], sHoldId[2], sDoorAxis[2];
static float sGripWas[2], sGrabYaw[2];
static Vec3 sGrabRot[2];
static int sSettle[ROOM_DOORS];
static int sCoast[ROOM_DOORS];
static float sVel[ROOM_DOORS];
static int sGrace[2], sGraceDoor[2];   /* after a throw, that hand does not shove this door */
static Vec3 sHandAt[2];
static int sHandHave[2];
static int sPoseRoom = -2;
/* one pose per door, shared by both rooms it joins */
static float sDoorSwing[DOORS];
static unsigned char sDoorHave[DOORS], sDoorOpen[DOORS];

static const Model *sArmModel;
static int sArmBones, sArmVerts;
static int sShoulder[2], sElbow[2], sHandBone[2], sArmFound[2];
static const Model *sExtentModel;
static int sExtentVerts;
static float sExtent[2];
static Vec3 sCenter[2];

#define VR_UNITS_PER_METER 12.5f
#define HAND_METRES 0.19f   /* wrist to fingertip before HAND_SCALE */
/* Both hands, every pose. 1 leaves the fitted size; 1.5 is half again as large. */
#define HAND_SCALE 1.5f

int vrhands_wants(const Actor *a) {
    return a != NULL && a->used && vr_active() && strncmp(a->name, "O_FI", 4) == 0;
}

static int leg_bone(const Actor *a, int b) {
    int i, k;

    if (!a->hlegs.on) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 4; k++) {
            if (a->hlegs.bones[i][k] == b) {
                return 1;
            }
        }
    }
    return 0;
}

static void find_arms(Actor *a, const Mat4 *world) {
    const Model *m = &a->model;
    float best[2] = {-1.0f, -1.0f};
    int i;

    if (sArmModel == m && sArmBones == m->nbones && sArmVerts == m->nv && sArmFound[0] && sArmFound[1]) {
        return;
    }
    sArmModel = m;
    sArmBones = m->nbones;
    sArmVerts = m->nv;
    sArmFound[0] = sArmFound[1] = 0;
    for (i = 0; i < m->nbones; i++) {
        int p = m->bones[i].parent;
        int g = p >= 0 ? m->bones[p].parent : -1;
        float dx, dy, dz, span;
        int side;

        if (g < 0 || leg_bone(a, i) || leg_bone(a, p) || leg_bone(a, g)) {
            continue;
        }
        dx = world[i].m[12] - world[g].m[12];
        dy = world[i].m[13] - world[g].m[13];
        dz = world[i].m[14] - world[g].m[14];
        span = sqrtf(dx * dx + dy * dy + dz * dz);
        if (span < 3.0f || span > 14.0f) {
            continue;
        }
        /* yaw 0 faces +z, and her left is +x. The left controller drives that arm. */
        side = world[i].m[12] >= 0.0f ? 0 : 1;
        if (fabsf(world[i].m[12]) <= best[side]) {
            continue;
        }
        best[side] = fabsf(world[i].m[12]);
        sShoulder[side] = g;
        sElbow[side] = p;
        sHandBone[side] = i;
        sArmFound[side] = 1;
    }
}

/* play space matches the camera: +X right, +Y up, -Z forward. `p` is an offset from her eyes. */
static Vec3 play_to_world(Vec3 eye, float yaw, Vec3 p) {
    float sy = sinf(yaw), cy = cosf(yaw);

    return vec3(eye.x + cy * p.x - sy * p.z, eye.y + p.y, eye.z + sy * p.x + cy * p.z);
}

static int hand_world(int side, Vec3 *at, Vec3 *right, Vec3 *up, Vec3 *fwd, float *grip, float *index, float *thumb) {
    VrController c;
    Vec3 eye, zero = vec3(0.0f, 0.0f, 0.0f);
    float yaw;

    if (!vr_controller(side, &c) || !c.tracked || !vr_anchor_get(&eye, &yaw)) {
        return 0;
    }
    *at = play_to_world(eye, yaw, c.at);
    *right = vec3_norm(play_to_world(zero, yaw, c.right));
    *up = vec3_norm(play_to_world(zero, yaw, c.up));
    *fwd = vec3_norm(play_to_world(zero, yaw, c.forward));
    *grip = c.grip;
    *index = c.index;
    *thumb = c.thumb;
    return 1;
}

static void v_sub(float *o, const float *a, const float *b) {
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static float v_len(const float *a) {
    return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

static void v_norm(float *o, const float *a) {
    float n = v_len(a);

    if (n < 1.0e-5f) {
        o[0] = 0.0f;
        o[1] = 1.0f;
        o[2] = 0.0f;
        return;
    }
    o[0] = a[0] / n;
    o[1] = a[1] / n;
    o[2] = a[2] / n;
}

static void v_cross(float *o, const float *a, const float *b) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

/* the middle joint, bent to the side of `pole`. 1 when the target is out of reach. */
static int place_middle(const float *root, float *mid, const float *end, const float *pole, float l1, float l2) {
    float d[3], side[3], dist, a, h, ll;

    v_sub(d, end, root);
    dist = v_len(d);
    if (dist < 1.0e-4f) {
        return 1;
    }
    v_norm(d, d);
    if (dist > l1 + l2) {
        dist = l1 + l2;
    }
    v_cross(side, pole, d);
    v_norm(side, side);
    ll = l1 * l1;
    a = (ll - l2 * l2 + dist * dist) / (2.0f * dist);
    h = ll - a * a;
    h = h > 0.0f ? sqrtf(h) : 0.0f;
    mid[0] = root[0] + a * d[0] - h * side[0];
    mid[1] = root[1] + a * d[1] - h * side[1];
    mid[2] = root[2] + a * d[2] - h * side[2];
    return dist >= l1 + l2 - 1.0e-3f;
}

/* X along from->to, Z from `pole` made square to it */
static void aim(Mat4 *m, const float *from, const float *to, const float *pole) {
    float x[3], y[3], z[3];

    *m = mat4_identity();
    v_sub(x, to, from);
    v_norm(x, x);
    v_norm(z, pole);
    v_cross(y, z, x);
    v_norm(y, y);
    v_cross(z, x, y);
    v_norm(z, z);
    m->m[0] = x[0]; m->m[1] = x[1]; m->m[2] = x[2];
    m->m[4] = y[0]; m->m[5] = y[1]; m->m[6] = y[2];
    m->m[8] = z[0]; m->m[9] = z[1]; m->m[10] = z[2];
    m->m[12] = from[0];
    m->m[13] = from[1];
    m->m[14] = from[2];
}

static Mat4 rigid_inverse(const Mat4 *m) {
    Mat4 r = mat4_identity();

    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2] = m->m[8];
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6] = m->m[9];
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10];
    r.m[12] = -(r.m[0] * m->m[12] + r.m[4] * m->m[13] + r.m[8] * m->m[14]);
    r.m[13] = -(r.m[1] * m->m[12] + r.m[5] * m->m[13] + r.m[9] * m->m[14]);
    r.m[14] = -(r.m[2] * m->m[12] + r.m[6] * m->m[13] + r.m[10] * m->m[14]);
    return r;
}

static int descends(const Model *m, int bone, int root) {
    int n = 0;

    if (bone < 0 || bone >= m->nbones) {
        return 0;
    }
    for (bone = m->bones[bone].parent; bone >= 0 && n < 64; bone = m->bones[bone].parent, n++) {
        if (bone == root) {
            return 1;
        }
    }
    return 0;
}

static void curl_hand(Actor *a, int side, float grip, float index, float thumb) {
    int nshapes = 0, i;
    float w;

    for (i = 0; i < a->model.nmorphs; i++) {
        const ModelMorph *mo = &a->model.morphs[i];

        if (!mo->face && mo->hand == side && mo->nshapes > nshapes) {
            nshapes = mo->nshapes;
        }
    }
    if (nshapes < 2) {
        return;
    }
    /* shape 0 is the open hand, 1 the fist, and 2 a point when the mesh has it */
    if (index < 0.35f && grip > 0.45f && nshapes > 2) {
        a->face.hand[side].a = 2;
        a->face.hand[side].b = 2;
        a->face.hand[side].w = 0.0f;
        return;
    }
    w = index < 0.35f ? grip * 0.45f : (index > grip ? index : grip);
    if (thumb < 0.5f) {
        w *= 0.75f;   /* thumb off the stick opens the hand back out */
    }
    if (w > 1.0f) {
        w = 1.0f;
    }
    a->face.hand[side].a = 0;
    a->face.hand[side].b = 1;
    a->face.hand[side].w = w;
}

/* wrist-to-fingertip reach, and where the mesh sits in this bone's space. Fiona's left hand
 * runs out along +X and her right along -X; Y is the thin palm axis. */
static void hand_measure(const Model *m, int bone, float *extent, Vec3 *center) {
    Vec3 sum = vec3(0.0f, 0.0f, 0.0f);
    float maxd = 0.0f;
    int i, k, n = 0;

    for (i = 0; i < m->nv; i++) {
        for (k = 0; k < 4; k++) {
            int b = m->v[i].bones[k];
            Vec3 local;
            float d;

            if (m->v[i].weights[k] < 0.15f || b < 0 || b >= m->nbones) {
                continue;
            }
            if (b != bone && !descends(m, b, bone)) {
                continue;
            }
            local = mat4_point(&m->bones[bone].inv_bind, m->v[i].pos);
            d = vec3_len(local);
            if (d > maxd) {
                maxd = d;
            }
            sum = vec3_add(sum, local);
            n++;
            break;
        }
    }
    *extent = maxd;
    *center = n > 0 ? vec3_scale(sum, 1.0f / (float)n) : vec3(0.0f, 0.0f, 0.0f);
}

/* fitted wrist-to-fingertip size, then HAND_SCALE. Recomputed when the mesh is replaced. */
static float hand_scale(const Actor *a, int side, int bone) {
    float body = 1.0f, extent, s;

    if (sExtentModel != &a->model || sExtentVerts != a->model.nv) {
        sExtentModel = &a->model;
        sExtentVerts = a->model.nv;
        sExtent[0] = sExtent[1] = 0.0f;
    }
    if (sExtent[side] <= 0.0f) {
        hand_measure(&a->model, bone, &sExtent[side], &sCenter[side]);
    }
    extent = sExtent[side];
    vr_body(NULL, &body);
    if (body < 0.2f) {
        body = 1.0f;
    }
    if (extent < 0.05f) {
        s = 2.2f;
    } else {
        s = (HAND_METRES * VR_UNITS_PER_METER / body) / extent;
        if (s < 1.0f) {
            s = 1.0f;
        }
    }
    return s * HAND_SCALE;
}

/* grip pose, after the skeleton is written. Bone X follows the controller for the left hand.
 * The right mesh points the other way along X, and its palm faces the other way, so it turns
 * 180° about controller up (local Z): fingers match the left and the palm faces the same side.
 * The bone then shifts so the mesh center, not the wrist, sits on the grip — the same slide for both. */
static Mat4 grip_matrix(Vec3 at, Vec3 fwd, Vec3 up, float scale, Vec3 center, int side) {
    Vec3 x = vec3_norm(fwd);
    Vec3 z = vec3_norm(up);
    Vec3 y = vec3_norm(vec3_cross(z, x));
    Vec3 seat;
    Mat4 m = mat4_identity();

    z = vec3_norm(vec3_cross(x, y));
    if (side == 1) {
        x = vec3_scale(x, -1.0f);
        y = vec3_scale(y, -1.0f);
    }
    seat = vec3_add(vec3_add(vec3_scale(x, center.x * scale), vec3_scale(y, center.y * scale)),
                    vec3_scale(z, center.z * scale));
    at = vec3_sub(at, seat);
    m.m[0] = x.x * scale; m.m[1] = x.y * scale; m.m[2] = x.z * scale;
    m.m[4] = y.x * scale; m.m[5] = y.y * scale; m.m[6] = y.z * scale;
    m.m[8] = z.x * scale; m.m[9] = z.y * scale; m.m[10] = z.z * scale;
    m.m[12] = at.x;
    m.m[13] = at.y;
    m.m[14] = at.z;
    return m;
}

static void pose_arm(Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace, int side, Vec3 at, Vec3 fwd, Vec3 up) {
    const Model *m = &a->model;
    int sh = sShoulder[side], el = sElbow[side], ha = sHandBone[side];
    Mat4 room[3], grip, old;
    float root[3], mid[3], end[3], pole[3], l1, l2;
    int i;

    if (!sArmFound[side] || sh < 0 || el < 0 || ha < 0 || ha >= m->nbones) {
        return;
    }
    room[0] = mat4_mul(*place, world[sh]);
    room[1] = mat4_mul(*place, world[el]);
    room[2] = mat4_mul(*place, world[ha]);
    root[0] = room[0].m[12]; root[1] = room[0].m[13]; root[2] = room[0].m[14];
    mid[0] = room[1].m[12]; mid[1] = room[1].m[13]; mid[2] = room[1].m[14];
    v_sub(pole, mid, root);
    l1 = v_len(pole);
    end[0] = room[2].m[12] - mid[0];
    end[1] = room[2].m[13] - mid[1];
    end[2] = room[2].m[14] - mid[2];
    l2 = v_len(end);
    {
        float scale = hand_scale(a, side, ha);

        grip = grip_matrix(at, fwd, up, scale, sCenter[side], side);
    }
    if (l1 > 0.05f && l2 > 0.05f) {
        float reach[3] = {grip.m[12], grip.m[13], grip.m[14]};

        place_middle(root, mid, reach, pole, l1, l2);
        aim(&room[0], root, mid, pole);
        aim(&room[1], mid, reach, pole);
        world[sh] = mat4_mul(*unplace, room[0]);
        world[el] = mat4_mul(*unplace, room[1]);
    }
    old = world[ha];
    world[ha] = mat4_mul(*unplace, grip);
    {
        Mat4 delta = mat4_mul(world[ha], rigid_inverse(&old));

        for (i = 0; i < m->nbones; i++) {
            if (descends(m, i, ha)) {
                world[i] = mat4_mul(delta, world[i]);
            }
        }
    }
}

static void log_hands(float left, float right, int posed) {
    static unsigned n;

    if (posed <= 0) {
        return;
    }
    if (n % 120 == 0) {
        fprintf(stderr, "vr hands: HAND_SCALE %.2f, right palm flipped 180 about controller up, both seated on the grip (bone scale %.2f %.2f)\n",
                HAND_SCALE, left, right);
    }
    n++;
}

void vrhands_pose(Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    float applied[2] = {0.0f, 0.0f};
    int side, posed = 0;

    if (!vrhands_wants(a) || !a->visible || world == NULL) {
        return;
    }
    find_arms(a, world);
    for (side = 0; side < 2; side++) {
        Vec3 at, right, fwd, up;
        float grip, index, thumb;

        if (!sArmFound[side] || !hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            continue;
        }
        (void)right;
        applied[side] = hand_scale(a, side, sHandBone[side]);
        pose_arm(a, world, place, unplace, side, at, fwd, up);
        curl_hand(a, side, grip, index, thumb);
        posed++;
    }
    log_hands(applied[0], applied[1], posed);
}

static float wrap_deg(float d) {
    while (d > 180.0f) {
        d -= 360.0f;
    }
    while (d < -180.0f) {
        d += 360.0f;
    }
    return d;
}

/* the same yaw the leaf is drawn with: local +X is (cos y, -sin y), local +Z is (sin y, cos y) */
static float swing_of(const RoomDoor *d, Vec3 hand, int axis) {
    float dx = hand.x - d->pos.x, dz = hand.z - d->pos.z;
    float ang = axis ? atan2f(dx, dz) : atan2f(-dz, dx);

    return wrap_deg((ang - d->rot.y) * (180.0f / 3.14159265f));
}

static float clamp_swing(float s) {
    if (s > 90.0f) {
        return 90.0f;
    }
    if (s < -90.0f) {
        return -90.0f;
    }
    return s;
}

static Vec3 handle_at(const RoomDoor *d, int axis) {
    float y = d->rot.y + d->swing * (3.14159265f / 180.0f);

    if (axis) {
        return vec3(d->pos.x + sinf(y) * 10.0f, d->pos.y + 11.0f, d->pos.z + cosf(y) * 10.0f);
    }
    return vec3(d->pos.x + cosf(y) * 10.0f, d->pos.y + 11.0f, d->pos.z - sinf(y) * 10.0f);
}

static float dist2(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;

    return dx * dx + dy * dy + dz * dz;
}

#define DOOR_TOUCH 4.0f     /* world units: reach for a knob */
#define DOOR_LATCH 1.0f     /* within this of shut, only a gripped knob moves it */
#define DOOR_HALF  45.0f    /* past this far open, either way, a slow release finishes open */
#define DOOR_WIDE  90.0f    /* open stop, both directions */
#define DOOR_SHUT  0.0f
#define COAST_SPEED 1.5f    /* degrees a frame: a release faster than this keeps going */
#define PUSH_GRACE  12      /* frames the throwing hand ignores the leaf */
#define RATTLE_SOUND 0x71u  /* Fiona's bank: the locked handle */
#define RATTLE_BANK  5

static Mat4 door_place(const RoomDoor *d) {
    float y = d->rot.y + d->swing * 3.14159265f / 180.0f;
    float cx = cosf(d->rot.x), sx = sinf(d->rot.x), cy = cosf(y), sy = sinf(y);
    float cz = cosf(d->rot.z), sz = sinf(d->rot.z);
    Mat4 rx = mat4_identity(), ry = mat4_identity(), rz = mat4_identity(), t = mat4_identity();

    rx.m[5] = cx;  rx.m[6] = sx;  rx.m[9] = -sx;  rx.m[10] = cx;
    ry.m[0] = cy;  ry.m[2] = -sy; ry.m[8] = sy;   ry.m[10] = cy;
    rz.m[0] = cz;  rz.m[1] = sz;  rz.m[4] = -sz;  rz.m[5] = cz;
    t.m[12] = d->pos.x;
    t.m[13] = d->pos.y;
    t.m[14] = d->pos.z;
    return mat4_mul(t, mat4_mul(rz, mat4_mul(ry, rx)));
}

static int in_box(Vec3 p, Vec3 lo, Vec3 hi, float margin) {
    return p.x >= lo.x - margin && p.x <= hi.x + margin && p.y >= lo.y - margin && p.y <= hi.y + margin &&
           p.z >= lo.z - margin && p.z <= hi.z + margin;
}

static int touching_knob(const RoomDoor *d, Vec3 hand, int *axis) {
    float a0 = dist2(hand, handle_at(d, 0));
    float a1 = dist2(hand, handle_at(d, 1));
    float reach = DOOR_TOUCH * DOOR_TOUCH;

    if (a0 > reach && a1 > reach) {
        return 0;
    }
    *axis = a0 <= a1 ? 0 : 1;
    return 1;
}

static int door_index(const Engine *e, int exit) {
    int room = e->room.id;

    if (room < 0 || room >= WORLD_ROOMS || exit < 0 || exit >= ROOM_EXITS) {
        return -1;
    }
    return e->world.exits[room][exit].door;
}

/* a locked door, a doorway, or one a character already holds: the hands leave it alone */
static int door_blocked(const Engine *e, int exit) {
    int d = door_index(e, exit);
    uint32_t state;

    if (d < 0 || d >= DOORS || d >= e->world.ndoors) {
        return 1;
    }
    if (e->world.doors[d].flags & 1u) {
        return 1;
    }
    state = gProgress.doors[d];
    return (state & 8u) != 0 || (state & 1u) != 0;
}

static int door_locked(const Engine *e, int exit) {
    int d = door_index(e, exit);

    if (d < 0 || d >= DOORS || d >= e->world.ndoors) {
        return 0;
    }
    return (gProgress.doors[d] & 8u) != 0;
}

/* the swing that is about to finish is held, so the doors actor's let-go writes the open bit
 * and the passage when it comes to rest. A stuck door is not marked held on the way open. */
static void claim_for_letgo(Engine *e, int exit, int open) {
    int d = door_index(e, exit);
    uint32_t state;

    if (d < 0 || d >= DOORS) {
        return;
    }
    state = gProgress.doors[d];
    if ((state & 8u) != 0 || ((state & 4u) != 0 && open)) {
        return;
    }
    gProgress.doors[d] = state | 1u;
}

static Vec3 door_local(const RoomDoor *d, Vec3 hand) {
    Mat4 place = door_place(d);
    Mat4 inv = rigid_inverse(&place);

    return mat4_point(&inv, hand);
}

/* the segment crosses the leaf, even when a fast hand would skip the wood between frames */
static int seg_leaf(Vec3 a, Vec3 b, float *tHit) {
    float o[3] = {a.x, a.y, a.z};
    float dir[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    float lo[3] = {0.0f, 0.0f, -1.0f};
    float hi[3] = {12.0f, 22.0f, 0.0f};
    float t0 = 0.0f, t1 = 1.0f;
    int i;

    for (i = 0; i < 3; i++) {
        if (dir[i] > -1.0e-5f && dir[i] < 1.0e-5f) {
            if (o[i] < lo[i] || o[i] > hi[i]) {
                return 0;
            }
        } else {
            float inv = 1.0f / dir[i];
            float ta = (lo[i] - o[i]) * inv;
            float tb = (hi[i] - o[i]) * inv;

            if (ta > tb) {
                float s = ta;
                ta = tb;
                tb = s;
            }
            if (ta > t0) {
                t0 = ta;
            }
            if (tb < t1) {
                t1 = tb;
            }
            if (t0 > t1) {
                return 0;
            }
        }
    }
    *tHit = t0;
    return 1;
}

/* a shove moves the leaf with the hand. A fast hand's whole step counts, so it cannot
 * pass through without the door taking that speed. The leaf stops at the latch. */
static int push_step(const RoomDoor *d, Vec3 prev, Vec3 curr, int have_prev, float *delta) {
    Vec3 a = have_prev ? door_local(d, prev) : door_local(d, curr);
    Vec3 b = door_local(d, curr);
    float t = 0.0f, x, dz, vel, side, step, next;
    int hit = seg_leaf(a, b, &t);

    if (!hit && !in_box(b, vec3(0.0f, 0.0f, -1.0f), vec3(12.0f, 22.0f, 0.0f), 0.15f)) {
        return 0;
    }
    x = a.x + (b.x - a.x) * t;
    if (x < 2.0f) {
        x = 2.0f;
    }
    dz = b.z - a.z;
    vel = -(dz) * (180.0f / 3.14159265f) / x;
    side = (a.z + (b.z - a.z) * t) + 0.5f;
    if (side > -0.05f && side < 0.05f) {
        side = b.z + 0.5f;
    }
    step = vel;
    if (fabsf(step) < 5.0f && in_box(b, vec3(0.0f, 0.0f, -1.0f), vec3(12.0f, 22.0f, 0.0f), 0.15f)) {
        step = side >= 0.0f ? 5.0f : -5.0f;
    }
    if ((side >= 0.0f && step < 0.0f) || (side < 0.0f && step > 0.0f)) {
        return 0;
    }
    next = d->swing + step;
    if ((d->swing > 0.0f && next < 0.0f) || (d->swing < 0.0f && next > 0.0f)) {
        next = 0.0f;
    }
    if (next > DOOR_WIDE) {
        next = DOOR_WIDE;
    } else if (next < -DOOR_WIDE) {
        next = -DOOR_WIDE;
    }
    *delta = next - d->swing;
    return *delta > 0.4f || *delta < -0.4f;
}

static int nearest_prop(PlacedSet *s, Vec3 hand, int other, float *best) {
    int i, found = -1;

    *best = 7.0f * 7.0f;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 ext;
        float edge, d2;

        if (!p->shown || (other >= 0 && sHold[other] == HOLD_PROP && sHoldId[other] == i)) {
            continue;
        }
        ext = vec3_sub(p->mesh.hi, p->mesh.lo);
        edge = ext.x > ext.y ? ext.x : ext.y;
        if (ext.z > edge) {
            edge = ext.z;
        }
        if (edge < 0.2f || edge > 28.0f) {
            continue;
        }
        d2 = dist2(hand, p->pos);
        if (d2 < *best) {
            *best = d2;
            found = i;
        }
    }
    return found;
}

static void release_hand(Room *r, int side) {
    int id = sHoldId[side];

    if (sHold[side] == HOLD_DOOR && id >= 0 && id < ROOM_DOORS) {
        sGrace[side] = PUSH_GRACE;
        sGraceDoor[side] = id;
        if (sVel[id] <= -COAST_SPEED || sVel[id] >= COAST_SPEED) {
            sCoast[id] = 1;
            sSettle[id] = 0;
        } else {
            sCoast[id] = 0;
            sSettle[id] = 18;   /* about half a second, then it finishes the swing */
        }
    }
    sHold[side] = HOLD_NONE;
    (void)r;
}

static void drive_door(Engine *e, int id, int axis, Vec3 hand) {
    RoomDoor *d;
    float next;

    if (id < 0 || id >= ROOM_DOORS || door_blocked(e, id)) {
        return;
    }
    d = &e->room.doors[id];
    if (!d->present || d->mode == 1) {
        return;
    }
    next = clamp_swing(swing_of(d, hand, axis));
    sVel[id] = next - d->swing;
    sCoast[id] = 0;
    room_door_swing(&e->room, id, next, 1);
    sSettle[id] = 0;
}

/* the 5°-a-frame ease to shut, or to open at -90 / +90, then the doors actor writes the state */
static void ease_door(Engine *e, int id, float goal) {
    RoomDoor *d = &e->room.doors[id];
    int open = goal <= -DOOR_HALF || goal >= DOOR_HALF;

    d->mode = 2;
    d->closing = !open;
    d->target = goal;
    d->sounded = 0;
    d->keys = NULL;
    claim_for_letgo(e, id, open);
}

/* still moving when the grip let go: keep that speed until the stop it is heading for */
static void coast_door(Engine *e, int id) {
    RoomDoor *d = &e->room.doors[id];
    float v = sVel[id];
    float s, goal;
    int hit;

    if (!d->present || d->mode == 1 || door_blocked(e, id) || v == 0.0f) {
        sCoast[id] = 0;
        return;
    }
    s = d->swing + v;
    hit = 0;
    goal = DOOR_SHUT;
    if (v < 0.0f) {
        if (d->swing <= DOOR_SHUT) {
            goal = -DOOR_WIDE;
            hit = s <= goal;
        } else {
            hit = s <= DOOR_SHUT;
        }
    } else {
        if (d->swing >= DOOR_SHUT) {
            goal = DOOR_WIDE;
            hit = s >= goal;
        } else {
            hit = s >= DOOR_SHUT;
        }
    }
    if (hit) {
        room_door_swing(&e->room, id, goal, 1);
        ease_door(e, id, goal);
        sCoast[id] = 0;
        sVel[id] = 0.0f;
        return;
    }
    room_door_swing(&e->room, id, s, 1);
}

static int swing_open(float s) {
    return s <= -8.0f || s >= 8.0f;
}

static int door_locked_id(int d) {
    return d >= 0 && d < DOORS && (gProgress.doors[d] & 8u) != 0;
}

/* the leaf and the opened bit stay together, so a shut door is shut on the nav mesh too */
static void match_progress(Engine *e, int exit, int d, float swing) {
    int open = swing_open(swing);
    uint32_t state = gProgress.doors[d];
    uint32_t next = open ? (state | 2u) : (state & ~2u);

    if (next != state) {
        gProgress.doors[d] = next;
    }
    room_door_passage(&e->room, exit, open, 0);
}

void vrhands_forget_poses(void) {
    memset(sDoorHave, 0, sizeof(sDoorHave));
    sPoseRoom = -2;
}

/* the angle last seen on this door, on whichever side of it was loaded */
static void poses_restore(Engine *e) {
    int room = e->room.id;
    int i;

    if (room < 0 || room >= WORLD_ROOMS || room == sPoseRoom) {
        return;
    }
    sPoseRoom = room;
    for (i = 0; i < ROOM_DOORS; i++) {
        int d = door_index(e, i);

        if (!e->room.doors[i].present || d < 0 || d >= DOORS || !sDoorHave[d] || door_locked_id(d)) {
            continue;
        }
        room_door_swing(&e->room, i, sDoorSwing[d], 1);
        match_progress(e, i, d, sDoorSwing[d]);
        sDoorOpen[d] = (unsigned char)swing_open(sDoorSwing[d]);
    }
}

static void poses_save(Engine *e) {
    int i;

    if (e->room.id < 0 || e->room.id >= WORLD_ROOMS) {
        return;
    }
    for (i = 0; i < ROOM_DOORS; i++) {
        int d = door_index(e, i);
        float s;

        if (!e->room.doors[i].present || d < 0 || d >= DOORS) {
            continue;
        }
        s = e->room.doors[i].swing;
        if (sDoorHave[d] && !door_locked_id(d) && (s - sDoorSwing[d] > 0.5f || sDoorSwing[d] - s > 0.5f)) {
            match_progress(e, i, d, s);   /* a hand moved it: the passage follows the leaf */
        }
        sDoorSwing[d] = s;
        sDoorOpen[d] = (unsigned char)swing_open(s);
        sDoorHave[d] = 1;
    }
}

void vrhands_tick(Engine *e) {
    int side, i;

    if (e == NULL) {
        return;
    }
    poses_restore(e);
    if (!vr_active() || !vr_anchor_get(NULL, NULL)) {
        poses_save(e);
        return;
    }
    for (side = 0; side < 2; side++) {
        Vec3 at, right, fwd, up;
        float grip, index, thumb, doorD, propD;
        int rising, door, axis = 0, prop;

        if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            if (sHold[side] != HOLD_NONE) {
                release_hand(&e->room, side);
            }
            sGripWas[side] = 0.0f;
            sHandHave[side] = 0;
            if (sGrace[side] > 0) {
                sGrace[side]--;
            }
            continue;
        }
        (void)right;
        (void)index;
        (void)thumb;
        rising = grip > 0.65f && sGripWas[side] <= 0.4f;
        if (rising) {
            int rattled = -1, rattAxis = 0;
            float rattD = DOOR_TOUCH * DOOR_TOUCH;

            for (i = 0; i < ROOM_DOORS; i++) {
                RoomDoor *d = &e->room.doors[i];
                int ax = 0;
                float kn;

                if (!d->present || !door_locked(e, i) || !touching_knob(d, at, &ax)) {
                    continue;
                }
                kn = dist2(at, handle_at(d, ax));
                if (rattled < 0 || kn < rattD) {
                    rattled = i;
                    rattAxis = ax;
                    rattD = kn;
                }
            }
            if (rattled >= 0) {
                engine_sound_at(RATTLE_SOUND, RATTLE_BANK, handle_at(&e->room.doors[rattled], rattAxis));
            }
        }
        if (grip <= 0.4f) {
            if (sHold[side] != HOLD_NONE) {
                release_hand(&e->room, side);
            }
        } else if (sHold[side] == HOLD_NONE && rising) {
            door = -1;
            doorD = DOOR_TOUCH * DOOR_TOUCH;
            for (i = 0; i < ROOM_DOORS; i++) {
                RoomDoor *d = &e->room.doors[i];
                int ax = 0;
                float kn = 0.0f;

                if (!d->present || d->mode == 1 || door_blocked(e, i) || !touching_knob(d, at, &ax)) {
                    continue;
                }
                kn = dist2(at, handle_at(d, ax));
                if (door < 0 || kn < doorD) {
                    door = i;
                    axis = ax;
                    doorD = kn;
                }
            }
            prop = nearest_prop(&e->room.placed, at, side ^ 1, &propD);
            if (door >= 0 && (prop < 0 || doorD <= propD)) {
                sHold[side] = HOLD_DOOR;
                sHoldId[side] = door;
                sDoorAxis[side] = axis;
                drive_door(e, door, axis, at);
            } else if (prop >= 0) {
                sHold[side] = HOLD_PROP;
                sHoldId[side] = prop;
                sGrabYaw[side] = atan2f(fwd.x, fwd.z);
                sGrabRot[side] = e->room.placed.p[prop].rot;
            }
        } else if (sHold[side] == HOLD_DOOR) {
            if (door_blocked(e, sHoldId[side])) {
                release_hand(&e->room, side);
            } else {
                drive_door(e, sHoldId[side], sDoorAxis[side], at);
            }
        } else if (sHold[side] == HOLD_PROP) {
            Placed *p = sHoldId[side] >= 0 && sHoldId[side] < e->room.placed.n ? &e->room.placed.p[sHoldId[side]] : NULL;

            if (p == NULL || !p->shown) {
                sHold[side] = HOLD_NONE;
            } else {
                p->pos = at;
                p->rot = sGrabRot[side];
                p->rot.y = sGrabRot[side].y + (atan2f(fwd.x, fwd.z) - sGrabYaw[side]);
            }
        }
        if (sHold[side] == HOLD_NONE && grip <= 0.4f) {
            for (i = 0; i < ROOM_DOORS; i++) {
                RoomDoor *d = &e->room.doors[i];
                float step;

                if (!d->present || d->mode == 1 || door_blocked(e, i)) {
                    continue;
                }
                if (d->swing > -DOOR_LATCH && d->swing < DOOR_LATCH) {
                    continue;
                }
                if (sHold[side ^ 1] == HOLD_DOOR && sHoldId[side ^ 1] == i) {
                    continue;
                }
                if (sGrace[side] > 0 && sGraceDoor[side] == i) {
                    continue;
                }
                if (!push_step(d, sHandAt[side], at, sHandHave[side], &step)) {
                    continue;
                }
                sVel[i] = step;
                sCoast[i] = 0;
                room_door_swing(&e->room, i, d->swing + step, 1);
                if (d->swing > -DOOR_LATCH && d->swing < DOOR_LATCH) {
                    sSettle[i] = 0;
                    ease_door(e, i, DOOR_SHUT);
                } else if (d->swing <= -DOOR_WIDE || d->swing >= DOOR_WIDE) {
                    sSettle[i] = 0;
                    ease_door(e, i, d->swing);
                } else {
                    sSettle[i] = 18;
                }
            }
        }
        sHandAt[side] = at;
        sHandHave[side] = 1;
        sGripWas[side] = grip;
        if (sGrace[side] > 0) {
            sGrace[side]--;
        }
    }
    for (i = 0; i < ROOM_DOORS; i++) {
        int held = (sHold[0] == HOLD_DOOR && sHoldId[0] == i) || (sHold[1] == HOLD_DOOR && sHoldId[1] == i);

        if (held) {
            sSettle[i] = 0;
            sCoast[i] = 0;
        } else if (sCoast[i]) {
            coast_door(e, i);
        } else if (sSettle[i] > 0 && --sSettle[i] == 0 && e->room.doors[i].present && !door_blocked(e, i)) {
            float s = e->room.doors[i].swing;
            float goal = DOOR_SHUT;

            if (s < -DOOR_HALF) {
                goal = -DOOR_WIDE;
            } else if (s > DOOR_HALF) {
                goal = DOOR_WIDE;
            }
            ease_door(e, i, goal);
        }
    }
    poses_save(e);
}
