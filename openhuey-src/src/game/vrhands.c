/* Arms follow the controllers. Finger shapes are the model's hand morphs (one blend): the
 * index trigger and the grip share it, and a third shape is the point when the mesh has one.
 * Doors already swing in degrees (0 shut, -90 open); a grip holds that angle, and after it
 * lets go the door eases to whichever of those it is nearer. */
#include "vrhands.h"

#include "engine.h"
#include "room.h"

#include "../platform/vr.h"

#include <math.h>
#include <string.h>

#define HOLD_NONE 0
#define HOLD_DOOR 1
#define HOLD_PROP 2

static int sHold[2], sHoldId[2], sDoorAxis[2];
static float sGripWas[2], sGrabYaw[2];
static Vec3 sGrabRot[2];
static int sSettle[ROOM_DOORS];

static const Model *sArmModel;
static int sShoulder[2], sElbow[2], sHandBone[2], sArmFound[2];
static const Model *sExtentModel;
static float sExtent[2];

#define VR_UNITS_PER_METER 12.5f
#define HAND_METRES 0.19f   /* wrist to fingertip, sized for the player */

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

    if (sArmModel == m && sArmFound[0] && sArmFound[1]) {
        return;
    }
    sArmModel = m;
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

/* how far the hand mesh reaches from this bone, in model units */
static float hand_extent(const Model *m, int bone) {
    float maxd = 0.0f;
    int i, k;

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
        }
    }
    return maxd;
}

/* enlarge the bind-pose hand until it is a real hand in front of the player */
static float hand_scale(const Actor *a, int side, int bone) {
    float body = 1.0f, extent, s;

    if (sExtentModel != &a->model) {
        sExtentModel = &a->model;
        sExtent[0] = sExtent[1] = 0.0f;
    }
    if (sExtent[side] <= 0.0f) {
        sExtent[side] = hand_extent(&a->model, bone);
    }
    extent = sExtent[side];
    vr_body(NULL, &body);
    if (body < 0.2f) {
        body = 1.0f;
    }
    if (extent < 0.05f) {
        return 2.2f;
    }
    s = (HAND_METRES * VR_UNITS_PER_METER / body) / extent;
    if (s < 1.0f) {
        s = 1.0f;
    }
    if (s > 3.5f) {
        s = 3.5f;
    }
    return s;
}

/* the grip pose itself: bone X points with the controller, Y is its up, then scaled */
static Mat4 grip_matrix(Vec3 at, Vec3 fwd, Vec3 up, float scale) {
    Vec3 x = vec3_norm(fwd);
    Vec3 z = vec3_norm(up);
    Vec3 y = vec3_norm(vec3_cross(z, x));
    Mat4 m = mat4_identity();

    z = vec3_norm(vec3_cross(x, y));
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
    if (l1 > 0.05f && l2 > 0.05f) {
        float reach[3] = {at.x, at.y, at.z};

        place_middle(root, mid, reach, pole, l1, l2);
        aim(&room[0], root, mid, pole);
        aim(&room[1], mid, reach, pole);
        world[sh] = mat4_mul(*unplace, room[0]);
        world[el] = mat4_mul(*unplace, room[1]);
    }
    grip = grip_matrix(at, fwd, up, hand_scale(a, side, ha));
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

void vrhands_pose(Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    int side;

    if (!vrhands_wants(a) || !a->visible || world == NULL) {
        return;
    }
    find_arms(a, world);
    for (side = 0; side < 2; side++) {
        Vec3 at, right, fwd, up;
        float grip, index, thumb;

        if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            continue;
        }
        (void)right;
        pose_arm(a, world, place, unplace, side, at, fwd, up);
        curl_hand(a, side, grip, index, thumb);
    }
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

static float swing_of(const RoomDoor *d, Vec3 hand, int axis) {
    float dx = hand.x - d->pos.x, dz = hand.z - d->pos.z;
    float ang = axis ? atan2f(dx, dz) : atan2f(dz, dx);

    return wrap_deg((ang - d->rot.y) * (180.0f / 3.14159265f));
}

static float clamp_swing(float s) {
    if (s > 25.0f) {
        return 25.0f;
    }
    if (s < -120.0f) {
        return -120.0f;
    }
    return s;
}

static Vec3 handle_at(const RoomDoor *d, int axis) {
    float y = d->rot.y + d->swing * (3.14159265f / 180.0f);

    if (axis) {
        return vec3(d->pos.x + sinf(y) * 10.0f, d->pos.y + 11.0f, d->pos.z + cosf(y) * 10.0f);
    }
    return vec3(d->pos.x + cosf(y) * 10.0f, d->pos.y + 11.0f, d->pos.z + sinf(y) * 10.0f);
}

static float dist2(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;

    return dx * dx + dy * dy + dz * dz;
}

static int nearest_door(Room *r, Vec3 hand, int *axis, float *best) {
    int i, found = -1;

    *best = 12.0f * 12.0f;
    for (i = 0; i < ROOM_DOORS; i++) {
        RoomDoor *d = &r->doors[i];
        float a0, a1, d0;

        if (!d->present || d->mode == 1) {
            continue;
        }
        a0 = dist2(hand, handle_at(d, 0));
        a1 = dist2(hand, handle_at(d, 1));
        d0 = a0 < a1 ? a0 : a1;
        if (d0 < *best) {
            *best = d0;
            *axis = a0 <= a1 ? 0 : 1;
            found = i;
        }
    }
    return found;
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
    if (sHold[side] == HOLD_DOOR && sHoldId[side] >= 0 && sHoldId[side] < ROOM_DOORS) {
        sSettle[sHoldId[side]] = 18;   /* about half a second, then it finishes the swing */
    }
    sHold[side] = HOLD_NONE;
    (void)r;
}

static void drive_door(Room *r, int id, int axis, Vec3 hand) {
    RoomDoor *d;

    if (id < 0 || id >= ROOM_DOORS) {
        return;
    }
    d = &r->doors[id];
    if (!d->present) {
        return;
    }
    room_door_swing(r, id, clamp_swing(swing_of(d, hand, axis)), 1);
    sSettle[id] = 0;
}

void vrhands_tick(Engine *e) {
    int side, i;

    if (e == NULL || !vr_active() || !vr_anchor_get(NULL, NULL)) {
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
            continue;
        }
        (void)right;
        (void)index;
        (void)thumb;
        rising = grip > 0.65f && sGripWas[side] <= 0.4f;
        if (grip <= 0.4f) {
            if (sHold[side] != HOLD_NONE) {
                release_hand(&e->room, side);
            }
        } else if (sHold[side] == HOLD_NONE && rising) {
            door = nearest_door(&e->room, at, &axis, &doorD);
            prop = nearest_prop(&e->room.placed, at, side ^ 1, &propD);
            if (door >= 0 && (prop < 0 || doorD <= propD)) {
                sHold[side] = HOLD_DOOR;
                sHoldId[side] = door;
                sDoorAxis[side] = axis;
                drive_door(&e->room, door, axis, at);
            } else if (prop >= 0) {
                sHold[side] = HOLD_PROP;
                sHoldId[side] = prop;
                sGrabYaw[side] = atan2f(fwd.x, fwd.z);
                sGrabRot[side] = e->room.placed.p[prop].rot;
            }
        } else if (sHold[side] == HOLD_DOOR) {
            drive_door(&e->room, sHoldId[side], sDoorAxis[side], at);
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
            door = nearest_door(&e->room, at, &axis, &doorD);
            if (door >= 0 && doorD < 8.0f * 8.0f &&
                !(sHold[side ^ 1] == HOLD_DOOR && sHoldId[side ^ 1] == door)) {
                float want = clamp_swing(swing_of(&e->room.doors[door], at, axis));
                float now = e->room.doors[door].swing;
                float step = want - now;

                if (step > 5.0f) {
                    step = 5.0f;
                }
                if (step < -5.0f) {
                    step = -5.0f;
                }
                if (step > 0.4f || step < -0.4f) {
                    room_door_swing(&e->room, door, now + step, 1);
                    sSettle[door] = 18;
                }
            }
        }
        sGripWas[side] = grip;
    }
    for (i = 0; i < ROOM_DOORS; i++) {
        int held = (sHold[0] == HOLD_DOOR && sHoldId[0] == i) || (sHold[1] == HOLD_DOOR && sHoldId[1] == i);

        if (held) {
            sSettle[i] = 0;
        } else if (sSettle[i] > 0 && --sSettle[i] == 0 && e->room.doors[i].present) {
            room_door_move(&e->room, i, e->room.doors[i].swing < -45.0f, 0);
        }
    }
}
