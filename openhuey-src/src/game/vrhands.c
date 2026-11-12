/* Arms follow the controllers. Finger shapes are the model's hand morphs (one blend): the
 * index trigger and the grip share it, and a third shape is the point when the mesh has one.
 * Doors swing in degrees (0 shut, open either way to ±90). A hand moves one only while it
 * is touching it. A shut door follows a grip on the knob. A door off the latch can be pushed
 * from either face, anywhere on the leaf. Letting go eases it by the 45° rule, unless it was
 * already moving, in which case it coasts to the stop it is heading for. A locked door never
 * moves; gripping its knob rattles once. A placed leaf - the nursery wardrobe's doors - is held
 * by its animation's frame instead: the hand's angle about its hinge is the frame its track gives,
 * and a leaf let go while it was turning keeps that speed to the stop it was heading for. */
#include "vrhands.h"
#include "loot.h"
#include "things.h"

#include "engine.h"
#include "progress.h"
#include "room.h"
#include "placed.h"

#include "../hactor/hactor.h"
#include "../platform/vr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HOLD_NONE 0
#define HOLD_DOOR 1
#define HOLD_PROP 2
#define HOLD_LOOT 3
#define HOLD_WARD 4

static int sHold[2], sHoldId[2], sDoorAxis[2];
static float sGripWas[2], sGrabYaw[2];
static Vec3 sGrabRot[2];
/* a wardrobe leaf taken by that hand: the angle it took it at, the frame it held, and the span and
 * rate of the leaf's turn track (frames, radians a frame) */
static float sWardAng[2], sWardRate[2];
static int sWardFrame[2], sWardLo[2], sWardHi[2];
/* the frame the hand's drive gave that leaf last tick, before it is rounded */
static float sWardLast[2];
/* the track the hand took that leaf with: a script that plays the leaf after that (placed_anim: a
 * track of its own, `paused` cleared) shows as a change here, and the leaf is taken again */
static const uint8_t *sWardKeys[2];
static int sWardFrames[2];
/* a leaf a hand has let go: the speed it was turning at and the stop it runs to (ward_tick) */
typedef struct WardMove {
    int mode;      /* 0 still - in a hand, or let go slower than WARD_COAST - 1 coasting */
    float at;      /* its frame, not yet rounded */
    float speed;   /* frames a tick */
} WardMove;
static WardMove sWardMove[PLACED_MAX];
static int sWardRoom = -2;   /* (the room the moves were made in: another room's indices are not these) */
/* (temporary: the wardrobe grab's diagnostics - the hands that have buzzed for a leaf, and the log's
 * count) */
static int sWardNear[2], sWardLogs;
/* (temporary: the hiding spot's probe - the last line it gave, and the room it was read in) */
static int sHideSeen, sHideInside, sHideFrame, sHideRoom = -2;
static int sSettle[ROOM_DOORS];
static int sCoast[ROOM_DOORS];
static float sVel[ROOM_DOORS];
static int sPushNow[ROOM_DOORS];   /* a bare hand moved this leaf this frame */
static int sPushWas[ROOM_DOORS];   /* it did so last frame: contact ending is a release */
static int sPushRoom = -2;
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
/* the bind pose's upper arm and forearm, model units scaled to room units: once per model */
static float sArmLen[2][2];
static int sArmLenHave[2];
static const Model *sLenModel;
static int sLenBones, sLenVerts;
/* the palm, the last bone of each arm's chain, which the controller drives; where the wrist
 * sits on elbow->palm as the bind pose has it; and the hand's centre in the palm's space */
static int sPalmBone[2];
static float sWristT[2];
static Vec3 sGripCenter[2];
/* the elbow's bend direction last frame (a fallback when the pole lies along the arm) and the
 * wrist's twist last frame (so the half-twist does not flip when it passes +-180 degrees) */
static float sBendWas[2][3];
static int sBendHave[2];
static float sTwistWas[2];
static int sTwistHave[2];
/* the last good up vector aim() built for each side, kept for a reference along the bone */
static float sUpWas[2][3];
static int sUpHave[2];
static const Model *sExtentModel;
static int sExtentVerts;
static float sExtent[2];
static Vec3 sCenter[2];

#define VR_UNITS_PER_METER 12.5f
#define HAND_METRES 0.19f   /* wrist to fingertip before HAND_SCALE */
/* the arm's length against Fiona's model's own, so the palm reaches a real arm's distance:
 * a value to be replaced by a calibrated one later */
#define ARM_LENGTH_SCALE 1.5f
/* the elbows' bend in her body's frame: down and out, a little behind her (her forward is +z) */
#define ELBOW_BACK -0.3f
/* the elbow's bend turns at most this a frame (5 degrees), and a pole nearer than this to the
 * arm's line (sin of the angle, about 11.5 degrees) is blended toward straight down first */
#define ELBOW_STEP 0.087266f
#define ELBOW_ALONG 0.2f
/* how the controller's twist about the forearm is shared: the forearm's aim takes the first
 * share, the wrist the next, and the palm keeps the rest (1 - TWIST_FOREARM - TWIST_WRIST) */
#define TWIST_FOREARM 0.5f
#define TWIST_WRIST 0.25f
/* aim()'s reference nearer than this to the bone's length (sin of the angle, about 2.9°) is
 * taken as degenerate: the last good up for that side is used instead */
#define AIM_PARALLEL 0.05f
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

/* the palm: of the wrist bone's children, the one the hand's vertices sit on most (the last
 * bone of the arm's chain); the wrist itself when the model has none */
static int palm_bone(const Model *m, int wrist) {
    int i, k, v, best = wrist, bestn = 0;

    for (i = 0; i < m->nbones; i++) {
        int n = 0;

        if (m->bones[i].parent != wrist) {
            continue;
        }
        for (v = 0; v < m->nv; v++) {
            for (k = 0; k < 4; k++) {
                if (m->v[v].bones[k] == i && m->v[v].weights[k] >= 0.15f) {
                    n++;
                    break;
                }
            }
        }
        if (n > bestn) {
            bestn = n;
            best = i;
        }
    }
    return best;
}

static void find_arms(Actor *a, const Mat4 *world) {
    const Model *m = &a->model;
    float best[2] = {-1.0f, -1.0f};
    int i, k;

    if (sArmModel == m && sArmBones == m->nbones && sArmVerts == m->nv && sArmFound[0] && sArmFound[1]) {
        return;
    }
    sArmModel = m;
    sArmBones = m->nbones;
    sArmVerts = m->nv;
    sArmFound[0] = sArmFound[1] = 0;
    sPalmBone[0] = sPalmBone[1] = -1;
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
        sHandBone[side] = i;   /* the wrist; the palm is the child the hand's skin sits on */
        sArmFound[side] = 1;
    }
    for (k = 0; k < 2; k++) {
        if (sArmFound[k]) {
            sPalmBone[k] = palm_bone(m, sHandBone[k]);
        }
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

/* the middle joint, bent toward `pole` in the plane square to the arm, its direction turned at
 * most ELBOW_STEP a frame so it cannot snap between straight and bent.
 * 1 when the target is out of reach. */
static int place_middle(const float *root, float *mid, const float *end, const float *pole,
                        float l1, float l2, int side) {
    float d[3], target[3], dist, a, h, ll, dot, plen, sin_a;

    v_sub(d, end, root);
    dist = v_len(d);
    if (dist < 1.0e-4f) {
        return 1;
    }
    v_norm(d, d);
    if (dist > l1 + l2) {
        dist = l1 + l2;
    }
    /* the pole is where the elbow goes. Near along the arm it is blended toward straight down
     * first, so its part square to the arm is never left undefined (no jump case) */
    plen = v_len(pole);
    dot = pole[0] * d[0] + pole[1] * d[1] + pole[2] * d[2];
    target[0] = pole[0] - dot * d[0];
    target[1] = pole[1] - dot * d[1];
    target[2] = pole[2] - dot * d[2];
    ll = v_len(target);
    sin_a = plen > 1.0e-5f ? ll / plen : 0.0f;
    if (sin_a < ELBOW_ALONG) {
        float k = 1.0f - sin_a / ELBOW_ALONG;
        float p[3];

        p[0] = pole[0] * (1.0f - k);
        p[1] = pole[1] * (1.0f - k) - k;   /* toward straight down */
        p[2] = pole[2] * (1.0f - k);
        dot = p[0] * d[0] + p[1] * d[1] + p[2] * d[2];
        target[0] = p[0] - dot * d[0];
        target[1] = p[1] - dot * d[1];
        target[2] = p[2] - dot * d[2];
    }
    v_norm(target, target);
    /* at most ELBOW_STEP a frame toward it, from last frame's bend for this side */
    if (sBendHave[side]) {
        float cp = sBendWas[side][0] * target[0] + sBendWas[side][1] * target[1] +
                   sBendWas[side][2] * target[2];
        float ang;

        if (cp > 1.0f) {
            cp = 1.0f;
        } else if (cp < -1.0f) {
            cp = -1.0f;
        }
        ang = acosf(cp);
        if (ang > ELBOW_STEP) {
            float t = ELBOW_STEP / ang;
            float s0 = sinf((1.0f - t) * ang) / sinf(ang);
            float s1 = sinf(t * ang) / sinf(ang);
            float v[3];

            v[0] = sBendWas[side][0] * s0 + target[0] * s1;
            v[1] = sBendWas[side][1] * s0 + target[1] * s1;
            v[2] = sBendWas[side][2] * s0 + target[2] * s1;
            /* last frame's bend was square to last frame's arm: square it to this one */
            dot = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
            v[0] -= dot * d[0];
            v[1] -= dot * d[1];
            v[2] -= dot * d[2];
            ll = v_len(v);
            if (ll > 1.0e-4f) {
                v_norm(target, v);
            }
        }
    }
    sBendWas[side][0] = target[0];
    sBendWas[side][1] = target[1];
    sBendWas[side][2] = target[2];
    sBendHave[side] = 1;
    ll = l1 * l1;
    a = (ll - l2 * l2 + dist * dist) / (2.0f * dist);
    h = ll - a * a;
    h = h > 0.0f ? sqrtf(h) : 0.0f;
    mid[0] = root[0] + a * d[0] + h * target[0];
    mid[1] = root[1] + a * d[1] + h * target[1];
    mid[2] = root[2] + a * d[2] + h * target[2];
    return dist >= l1 + l2 - 1.0e-3f;
}

/* X along the given unit `length` axis, Z from `ref` made square to it. A `ref` nearly along the
 * bone leaves the up undefined, so the last good up for that side is used instead (and a fixed
 * one only when that too lies along the bone). */
static void aim_x(Mat4 *m, const float *from, const float *length, const float *ref, int side) {
    float y[3], z[3], ll, rl;

    *m = mat4_identity();
    rl = v_len(ref);
    v_cross(y, ref, length);
    ll = v_len(y);
    if (rl < 1.0e-5f || ll < AIM_PARALLEL * rl) {
        if (sUpHave[side]) {
            y[0] = sUpWas[side][0];
            y[1] = sUpWas[side][1];
            y[2] = sUpWas[side][2];
        } else {
            y[0] = 0.0f;
            y[1] = 1.0f;
            y[2] = 0.0f;
        }
    }
    v_cross(z, length, y);
    if (v_len(z) < 1.0e-4f) {   /* that up lies along the bone too: any square one will do */
        y[0] = fabsf(length[1]) < 0.9f ? 0.0f : 1.0f;
        y[1] = fabsf(length[1]) < 0.9f ? 1.0f : 0.0f;
        y[2] = 0.0f;
        v_cross(z, length, y);
    }
    v_norm(z, z);
    v_cross(y, z, length);
    v_norm(y, y);
    sUpWas[side][0] = y[0];
    sUpWas[side][1] = y[1];
    sUpWas[side][2] = y[2];
    sUpHave[side] = 1;
    m->m[0] = length[0]; m->m[1] = length[1]; m->m[2] = length[2];
    m->m[4] = y[0]; m->m[5] = y[1]; m->m[6] = y[2];
    m->m[8] = z[0]; m->m[9] = z[1]; m->m[10] = z[2];
    m->m[12] = from[0];
    m->m[13] = from[1];
    m->m[14] = from[2];
}

/* X along from->to, Z from `ref` made square to it */
static void aim(Mat4 *m, const float *from, const float *to, const float *ref, int side) {
    float x[3];

    v_sub(x, to, from);
    v_norm(x, x);
    aim_x(m, from, x, ref, side);
}

/* as aim, with the bone's X the other way. The right arm's bones are mirrored in place: their
 * bind X - the length axis - runs with the model's +x, back toward the shoulder, not out along
 * the limb as the left's does. Aiming it outward mirrored the right arm's skin (a stretch). */
static void aim_rev(Mat4 *m, const float *from, const float *to, const float *ref, int side) {
    float x[3];

    v_sub(x, from, to);
    v_norm(x, x);
    aim_x(m, from, x, ref, side);
}

/* turn a bone about its own X - its length - by `angle` (right-handed about +X) */
static void twist_bone(Mat4 *m, float angle) {
    float ca = cosf(angle), sa = sinf(angle);
    float yx = m->m[4], yy = m->m[5], yz = m->m[6];
    float zx = m->m[8], zy = m->m[9], zz = m->m[10];

    m->m[4] = ca * yx + sa * zx;
    m->m[5] = ca * yy + sa * zy;
    m->m[6] = ca * yz + sa * zz;
    m->m[8] = -sa * yx + ca * zx;
    m->m[9] = -sa * yy + ca * zy;
    m->m[10] = -sa * yz + ca * zz;
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

static void hand_measure(const Model *m, int bone, float *extent, Vec3 *center);

/* the arm's two lengths from the bind pose: the inverse of each bone's inv_bind (rigid) gives
 * its bind world position, and the distances go to room units with the actor's own scale, as
 * the placed bones the solver measures are. Computed once per model, as find_arms caches. */
static void arm_lengths(const Actor *a) {
    const Model *m = &a->model;
    float scale = a->scale != 0.0f ? a->scale : 1.0f;
    int side;

    if (sLenModel != m || sLenBones != m->nbones || sLenVerts != m->nv) {
        sLenModel = m;
        sLenBones = m->nbones;
        sLenVerts = m->nv;
        sArmLenHave[0] = sArmLenHave[1] = 0;
    }
    for (side = 0; side < 2; side++) {
        int wrist = sHandBone[side], palm = sPalmBone[side] >= 0 ? sPalmBone[side] : wrist;
        Mat4 sh, el, pa, wr;
        float d[3], dpalm, wext;
        Vec3 wc;

        if (!sArmFound[side] || sArmLenHave[side]) {
            continue;
        }
        sh = rigid_inverse(&m->bones[sShoulder[side]].inv_bind);
        el = rigid_inverse(&m->bones[sElbow[side]].inv_bind);
        pa = rigid_inverse(&m->bones[palm].inv_bind);
        wr = rigid_inverse(&m->bones[wrist].inv_bind);
        d[0] = sh.m[12] - el.m[12];
        d[1] = sh.m[13] - el.m[13];
        d[2] = sh.m[14] - el.m[14];
        sArmLen[side][0] = v_len(d) * scale * ARM_LENGTH_SCALE;
        d[0] = el.m[12] - pa.m[12];
        d[1] = el.m[13] - pa.m[13];
        d[2] = el.m[14] - pa.m[14];
        dpalm = v_len(d);
        sArmLen[side][1] = dpalm * scale * ARM_LENGTH_SCALE;
        d[0] = el.m[12] - wr.m[12];
        d[1] = el.m[13] - wr.m[13];
        d[2] = el.m[14] - wr.m[14];
        sWristT[side] = palm != wrist && dpalm > 0.05f ? v_len(d) / dpalm : 0.0f;
        /* hand_scale measures the hand's centre in the wrist's space; that point goes into the
         * palm's space, so the grip still centres the hand on the controller through the palm */
        hand_measure(m, wrist, &wext, &wc);
        {
            Vec3 pm = mat4_point(&wr, wc);

            sGripCenter[side] = mat4_point(&m->bones[palm].inv_bind, pm);
        }
        sArmLenHave[side] = 1;
    }
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

/* grip pose, every frame after the OpenXR grip is written into the skeleton.
 * With the palm facing inward and the fingers forward, grip -Z (fwd) points at the thumb,
 * straight up, and grip +Y points backward. Bone Y is the palm axis and is left alone, so
 * neither palm turns outward or over.
 * The left mesh fingers run along +X, so that axis turns onto -grip +Y (forward) and bone Z
 * takes the old thumb axis (up). The right mesh fingers run along -X and its palm faces the
 * other way, so it turns 180° about grip +Y first, then the opposite quarter turn: bone +X
 * lands on +grip +Y and the fingers still point forward. */
static Mat4 grip_matrix(Vec3 at, Vec3 fwd, Vec3 up, float scale, Vec3 center, int side) {
    Vec3 x = vec3_norm(fwd);
    Vec3 z = vec3_norm(up);
    Vec3 y = vec3_norm(vec3_cross(z, x));
    Vec3 ox, seat;
    Mat4 m = mat4_identity();

    z = vec3_norm(vec3_cross(x, y));
    if (side == 1) {
        x = vec3_scale(x, -1.0f);
        y = vec3_scale(y, -1.0f);
    }
    ox = x;
    if (side == 0) {
        x = vec3_scale(z, -1.0f);
        z = ox;
    } else {
        x = z;
        z = vec3_scale(ox, -1.0f);
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
    int sh = sShoulder[side], el = sElbow[side];
    int wr = sHandBone[side], pa = sPalmBone[side] >= 0 ? sPalmBone[side] : sHandBone[side];
    Mat4 room[3], grip, old;
    float root[3], mid[3], pole[3], l1, l2, yaw, hs;
    int i;

    if (!sArmFound[side] || sh < 0 || el < 0 || wr < 0 || wr >= m->nbones || pa < 0 || pa >= m->nbones) {
        return;
    }
    if (!vr_anchor_get(NULL, &yaw)) {
        return;
    }
    room[0] = mat4_mul(*place, world[sh]);
    room[1] = mat4_mul(*place, world[el]);
    room[2] = mat4_mul(*place, world[wr]);
    root[0] = room[0].m[12]; root[1] = room[0].m[13]; root[2] = room[0].m[14];
    mid[0] = room[1].m[12]; mid[1] = room[1].m[13]; mid[2] = room[1].m[14];
    l1 = sArmLen[side][0];   /* the bind pose's, not this animation's */
    l2 = sArmLen[side][1];
    hs = hand_scale(a, side, wr);   /* the hand's scale, still measured on the wrist */
    grip = grip_matrix(at, fwd, up, hs, sGripCenter[side], side);
    if (l1 > 0.05f && l2 > 0.05f) {
        /* the bend is in her body's frame (yaw 0 along +z, her left +x), not the headset's:
         * down and out on that side, a little behind her, so the elbows point out and back */
        float out_sign = side == 0 ? 1.0f : -1.0f;
        float bend_s = sinf(a->yaw), bend_c = cosf(a->yaw);
        Vec3 bend = vec3(bend_c * out_sign + bend_s * ELBOW_BACK, -1.0f,
                         -bend_s * out_sign + bend_c * ELBOW_BACK);
        float reach[3] = {grip.m[12], grip.m[13], grip.m[14]};
        float d[3];
        float dist;

        pole[0] = bend.x;
        pole[1] = bend.y;
        pole[2] = bend.z;
        /* out of reach: the palm target is pulled onto the arm's length along its own way from
         * the shoulder - the grip's seat-adjusted point, which carries the hand's built-in
         * offset - so the clamp begins exactly where the palm is and nothing jumps. Only the
         * grip's translation moves; its rotation and scale stay as they are. */
        v_sub(d, reach, root);
        dist = v_len(d);
        if (dist > l1 + l2 && dist > 1.0e-4f) {
            float k = (l1 + l2) * 0.999f / dist;

            reach[0] = root[0] + d[0] * k;
            reach[1] = root[1] + d[1] * k;
            reach[2] = root[2] + d[2] * k;
            grip.m[12] = reach[0];
            grip.m[13] = reach[1];
            grip.m[14] = reach[2];
        }
        place_middle(root, mid, reach, pole, l1, l2, side);
        if (side == 0) {
            aim(&room[0], root, mid, sBendWas[side], side);
            aim(&room[1], mid, reach, sBendWas[side], side);
        } else {
            /* the right arm's bind X runs back toward the shoulder: aiming it outward mirrored
             * its skin (the stretch); the reversed aim keeps the bone's length axis as it was */
            aim_rev(&room[0], root, mid, sBendWas[side], side);
            aim_rev(&room[1], mid, reach, sBendWas[side], side);
        }
        /* the arm is longer than Fiona's model's: lengthen the upper arm and forearm along the
         * bone (X) only; Y and Z, the thickness, stay as they are, so the joints still meet */
        room[0].m[0] *= ARM_LENGTH_SCALE; room[0].m[1] *= ARM_LENGTH_SCALE; room[0].m[2] *= ARM_LENGTH_SCALE;
        room[1].m[0] *= ARM_LENGTH_SCALE; room[1].m[1] *= ARM_LENGTH_SCALE; room[1].m[2] *= ARM_LENGTH_SCALE;
        if (pa != wr) {
            /* the wrist on the forearm's line from the elbow to the palm, the bind pose's share */
            float wp[3];

            wp[0] = mid[0] + (reach[0] - mid[0]) * sWristT[side];
            wp[1] = mid[1] + (reach[1] - mid[1]) * sWristT[side];
            wp[2] = mid[2] + (reach[2] - mid[2]) * sWristT[side];
            if (side == 0) {
                aim(&room[2], wp, reach, pole, side);
            } else {
                aim_rev(&room[2], wp, reach, pole, side);
            }
            /* the controller's twist about the forearm, shared along the arm below: the palm
             * keeps the controller's own rotation and the forearm and wrist take their shares */
            {
                float px = grip.m[4], py = grip.m[5], pz = grip.m[6];
                float plen = sqrtf(px * px + py * py + pz * pz);
                float xx = room[2].m[0], xy = room[2].m[1], xz = room[2].m[2];
                float yx = room[2].m[4], yy = room[2].m[5], yz = room[2].m[6];

                if (plen > 1.0e-5f) {
                    float dot, nx, ny, nz, nl;

                    px /= plen; py /= plen; pz /= plen;
                    dot = px * xx + py * xy + pz * xz;   /* the palm's Y square to the forearm */
                    nx = px - dot * xx;
                    ny = py - dot * xy;
                    nz = pz - dot * xz;
                    nl = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nl > 1.0e-4f) {
                        float sin_a = ((yy * nz - yz * ny) * xx + (yz * nx - yx * nz) * xy +
                                       (yx * ny - yy * nx) * xz) / nl;
                        float cos_a = (yx * nx + yy * ny + yz * nz) / nl;
                        float twist = atan2f(sin_a, cos_a);

                        /* the nearest turn to last frame's: the angle does not flip at +-180 */
                        if (sTwistHave[side]) {
                            while (twist - sTwistWas[side] > 3.14159265f) {
                                twist -= 6.28318531f;
                            }
                            while (twist - sTwistWas[side] < -3.14159265f) {
                                twist += 6.28318531f;
                            }
                        } else {
                            sTwistHave[side] = 1;
                        }
                        sTwistWas[side] = twist;
                        /* spread it: the forearm turns by its share through its own aim, the
                         * wrist by its own share next, and the palm keeps the rest (its own
                         * controller rotation) */
                        twist_bone(&room[1], TWIST_FOREARM * twist);
                        twist_bone(&room[2], (TWIST_FOREARM + TWIST_WRIST) * twist);
                    }
                }
            }
            /* the wrist keeps the hand's scale (its vertices are part of the hand's skin) */
            room[2].m[0] *= hs; room[2].m[1] *= hs; room[2].m[2] *= hs;
            room[2].m[4] *= hs; room[2].m[5] *= hs; room[2].m[6] *= hs;
            room[2].m[8] *= hs; room[2].m[9] *= hs; room[2].m[10] *= hs;
            world[wr] = mat4_mul(*unplace, room[2]);
        }
        world[sh] = mat4_mul(*unplace, room[0]);
        world[el] = mat4_mul(*unplace, room[1]);
    }
    old = world[pa];
    world[pa] = mat4_mul(*unplace, grip);
    {
        Mat4 delta = mat4_mul(world[pa], rigid_inverse(&old));

        for (i = 0; i < m->nbones; i++) {
            if (descends(m, i, pa)) {
                world[i] = mat4_mul(delta, world[i]);
            }
        }
    }
}

static void log_hands(float left, float right, int posed) {
    (void)left;
    (void)right;
    (void)posed;   /* (silent: the hand-fit line is off) */
}

void vrhands_pose(Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    float applied[2] = {0.0f, 0.0f};
    int side, posed = 0;

    if (!vrhands_wants(a) || !a->visible || world == NULL) {
        return;
    }
    find_arms(a, world);
    arm_lengths(a);
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

static float wrap_rad(float a) {
    while (a > 3.14159265f) {
        a -= 6.28318531f;
    }
    while (a < -3.14159265f) {
        a += 6.28318531f;
    }
    return a;
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

static Mat4 door_place_at(const RoomDoor *d, float swing) {
    float y = d->rot.y + swing * 3.14159265f / 180.0f;
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

static Mat4 door_place(const RoomDoor *d) { return door_place_at(d, d->swing); }

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

static Vec3 door_local_at(const RoomDoor *d, Vec3 hand, float swing) {
    Mat4 place = door_place_at(d, swing);
    Mat4 inv = rigid_inverse(&place);

    return mat4_point(&inv, hand);
}

static Vec3 door_local(const RoomDoor *d, Vec3 hand) { return door_local_at(d, hand, d->swing); }

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
 * pass through without the door taking that speed. The leaf stops at the latch.
 * `speed` is the hand's own angular step, before the slow-contact minimum, so a
 * release can coast with that speed the way a gripped throw does. */
static int push_step(const RoomDoor *d, Vec3 prev, Vec3 curr, int have_prev, float *delta, float *speed) {
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
    if (speed != NULL) {
        *speed = vel;
    }
    return *delta > 0.4f || *delta < -0.4f;
}

/* ---- the nursery wardrobe's doors ----
 * A leaf is a placed object ("left_door") whose animation turns it about its place. Its track has
 * two flat ends - 0..30 flat open, 53..76 flat shut - and the frames between them are the swing.
 * A grip holds that frame, so the hand's angle about the hinge is the leaf's own turn; the frame
 * its track gives for that angle is where the leaf is put. Let go slowly, it stays there; let go
 * while it is turning, it keeps that speed to the stop it was heading for (ward_release, ward_tick).
 * The hiding plays the same leaves on tracks of their own (doors-anim: placed_anim, which clears
 * `paused` and starts its track at frame 0), so a hand takes the leaf again on its own track when it
 * finds another loaded (ward_own_track), and each tick while it holds it (ward_retake).
 * (right_door is the same path: its direction comes from its own track, not from a sign here.) */
#define WARD_LEAF "left_door"   /* (the leaves driven by hand; right_door joins it once proven) */
#define WARD_TOUCH 5.0f         /* world units from the leaf's face: a hand on it */
#define WARD_STILL 1.0e-3f      /* radians a frame that barely turns: a flat end of the track */
/* degrees a tick the leaf itself turns: let go faster than this, it keeps that speed to the stop
 * it was heading for (the exit doors' COAST_SPEED, taken in the leaf's own swing so a track that
 * turns more or less a frame than this one is judged by the same throw) */
#define WARD_COAST 1.5f

/* 1 when a placed object is a leaf a hand may turn (Placed.name: 16 bytes) */
static int ward_leaf(const char *name) {
    return strncmp(name, WARD_LEAF, 16) == 0;
}

/* the turn a frame of the leaf's track gives (format 8: a key's three floats are the turn) */
static Vec3 ward_key(const Placed *p, int frame) {
    Vec3 r = vec3(0.0f, 0.0f, 0.0f);

    if (frame < 0) {
        frame = 0;
    }
    if (frame >= p->frames) {
        frame = p->frames - 1;
    }
    memcpy(&r, p->keys + frame * 12, 12);
    return r;
}

/* how far apart two turns of the track are: its largest part that differs */
static float ward_gap(Vec3 a, Vec3 b) {
    float dx = fabsf(a.x - b.x), dy = fabsf(a.y - b.y), dz = fabsf(a.z - b.z);

    return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
}

/* the frames the leaf turns through: its track's two flat ends are the stops (30 and 53 here) */
static int ward_span(const Placed *p, int *lo, int *hi) {
    int a, b;

    if (p->keys == NULL || p->format != 8 || p->frames < 3) {
        return 0;
    }
    for (a = 1; a < p->frames - 1 && ward_gap(ward_key(p, a), ward_key(p, 0)) < WARD_STILL; a++) {
    }
    for (b = p->frames - 2; b > 0 && ward_gap(ward_key(p, b), ward_key(p, p->frames - 1)) < WARD_STILL; b--) {
    }
    if (b - a < 2) {
        return 0;
    }
    *lo = a - 1;
    *hi = b + 1;
    return 1;
}

/* a leaf's bounds in the world the hands are in: its vertices through placed_world, the transform it
 * is drawn with (a placed mesh carries no bounds of its own) */
static void ward_bounds(const Placed *p, Vec3 *lo, Vec3 *hi) {
    Mat4 w = placed_world(p);
    int i;

    *lo = vec3(1.0e30f, 1.0e30f, 1.0e30f);
    *hi = vec3(-1.0e30f, -1.0e30f, -1.0e30f);
    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = mat4_point(&w, vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z));

        *lo = vec3_min(*lo, v);
        *hi = vec3_max(*hi, v);
    }
}

/* the far corner of the leaf in its own space: the point that swings widest about the place (the
 * turn is applied there, so the tip is taken there too) */
static Vec3 ward_tip(const Placed *p) {
    Vec3 lo = vec3(1.0e30f, 1.0e30f, 1.0e30f), hi = vec3(-1.0e30f, -1.0e30f, -1.0e30f);
    int i;

    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z);

        lo = vec3_min(lo, v);
        hi = vec3_max(hi, v);
    }
    return vec3(fabsf(lo.x) > fabsf(hi.x) ? lo.x : hi.x, (lo.y + hi.y) * 0.5f,
                fabsf(lo.z) > fabsf(hi.z) ? lo.z : hi.z);
}

/* the hand's angle about the leaf's place with the leaf turned that far: where its tip sits.
 * (the turn's own axes give the hand its direction, whichever way round the track turns) */
static float ward_angle(const Placed *p, Vec3 turn, Vec3 tip) {
    Placed at = *p;
    Vec3 w;
    Mat4 place;

    at.rot = turn;
    place = placed_world(&at);
    w = mat4_point(&place, tip);
    return atan2f(w.x - p->pos.x, w.z - p->pos.z);
}

/* the hand's radians a frame of the swing: the two ends of the track, posed */
static float ward_rate(const Placed *p, int lo, int hi, Vec3 tip) {
    return wrap_rad(ward_angle(p, ward_key(p, hi), tip) - ward_angle(p, ward_key(p, lo), tip)) / (float)(hi - lo);
}

/* the wardrobe leaf a hand is on: how near it is to the leaf's face, and which one it is (-1 none) */
static int nearest_ward(PlacedSet *s, Vec3 hand, int other, float *best) {
    int i, found = -1;

    *best = WARD_TOUCH * WARD_TOUCH;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 lo, hi, gap;
        float reach;

        if (!p->shown || p->mesh.nv <= 0 || !ward_leaf(p->name)) {
            continue;
        }
        if (other >= 0 && sHold[other] == HOLD_WARD && sHoldId[other] == i) {
            continue;
        }
        ward_bounds(p, &lo, &hi);
        gap = vec3(hand.x - fminf(fmaxf(hand.x, lo.x), hi.x), hand.y - fminf(fmaxf(hand.y, lo.y), hi.y),
                   hand.z - fminf(fmaxf(hand.z, lo.z), hi.z));
        reach = vec3_len(gap);
        if (reach * reach < *best) {
            *best = reach * reach;
            found = i;
        }
    }
    return found;
}

/* (temporary: the leaf nearest a hand, out to `reach_max` world units: -1, and `best` left at that,
 * when none is) */
static int ward_near(PlacedSet *s, Vec3 hand, float reach_max, float *best) {
    int i, found = -1;

    *best = reach_max;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 lo, hi, gap;
        float reach;

        if (!p->shown || p->mesh.nv <= 0 || !ward_leaf(p->name)) {
            continue;
        }
        ward_bounds(p, &lo, &hi);
        gap = vec3(hand.x - fminf(fmaxf(hand.x, lo.x), hi.x), hand.y - fminf(fmaxf(hand.y, lo.y), hi.y),
                   hand.z - fminf(fmaxf(hand.z, lo.z), hi.z));
        reach = vec3_len(gap);
        if (reach < *best) {
            *best = reach;
            found = i;
        }
    }
    return found;
}

/* a track no hand can scrub: as the room left it, its own to play */
static void ward_unload(Placed *p, int loaded) {
    if (loaded) {
        p->keys = NULL;
        p->frames = p->frame = p->paused = 0;
    }
}

/* the leaf back on its own track: the first its record gives - the one the room poses it with, and
 * the only one whose frames are this swing's. The hiding plays the leaves on tracks of their own
 * (doors-anim: a short open, a short shut), whose frames are no frames of that swing - a hand that
 * took the leaf on one would either scrub the wrong frames or be refused, a script track having no
 * flat ends to know its stops by. 1 when `p` is already on it; else the leaf is put on it, its pose
 * left as it stands (ward_take holds the end that pose sits at). */
static int ward_own_track(PlacedSet *s, Placed *p) {
    Placed own = *p;

    placed_anim(s, &own, 0, 0);
    if (own.keys == NULL || own.frames <= 0 || (own.keys == p->keys && own.frames == p->frames)) {
        return 1;   /* (nothing to take it on but what the room left, or already on its own) */
    }
    p->loop = own.loop;
    p->keys = own.keys;
    p->frames = own.frames;
    p->format = own.format;
    return 0;
}

/* a hand takes the leaf: its track loaded if the room never played it (or left one of its own on
 * it), the leaf held at the frame its pose already shows, so it does not jump into the hand */
static int ward_take(PlacedSet *s, int side, int id, Vec3 hand) {
    Placed *p = &s->p[id];
    Vec3 tip;
    float rate;
    int loaded = 0, lo = 0, hi = 0, frame;

    if (p->keys == NULL || p->frames <= 0) {
        placed_anim(s, p, 0, 0);   /* (its first track: the pose it is defined with) */
        loaded = 1;
    } else if (!ward_own_track(s, p)) {
        loaded = 1;   /* (a track of the room's on it: back to the leaf's own, to be scrubbed) */
    }
    if (p->mesh.nv <= 0 || !ward_span(p, &lo, &hi)) {
        ward_unload(p, loaded);
        return 0;
    }
    tip = ward_tip(p);
    rate = ward_rate(p, lo, hi, tip);
    if (fabsf(rate) < 1.0e-5f) {
        ward_unload(p, loaded);
        return 0;
    }
    if (loaded) {   /* (never played: the end of the swing its own pose sits at) */
        float pose = wrap_rad(ward_angle(p, p->rot, tip) - ward_angle(p, ward_key(p, lo), tip));
        float shut = wrap_rad(ward_angle(p, ward_key(p, hi), tip) - ward_angle(p, ward_key(p, lo), tip));

        frame = fabsf(pose) <= fabsf(pose - shut) ? lo : hi;
    } else {
        frame = p->frame < lo ? lo : p->frame > hi ? hi : p->frame;   /* (a flat end holds at its stop) */
    }
    placed_frame_set(s, p, frame);
    sWardFrame[side] = frame;
    /* (the hand may turn it a frame past each stop, where the leaf sits still: its pose does not
     * change there, so a stop reached one frame inside the swing is not left ajar) */
    sWardLo[side] = lo > 1 ? lo - 1 : 0;
    sWardHi[side] = hi + 1 < p->frames - 1 ? hi + 1 : p->frames - 1;
    sWardRate[side] = rate;
    sWardAng[side] = atan2f(hand.x - p->pos.x, hand.z - p->pos.z);
    sWardMove[id].mode = 0;   /* (taken again: what it did on its own ends here) */
    sWardMove[id].speed = 0.0f;
    sWardLast[side] = (float)frame;
    sWardKeys[side] = p->keys;   /* (the track it is held on: a script's own is taken again) */
    sWardFrames[side] = p->frames;
    return 1;
}

/* a hand holds the leaf, but it is no longer the track it took it on: a script has played it since
 * (placed_anim: a track of its own, `paused` cleared) or its frames are being played on - so the
 * hand takes it again where it now stands, and its frames are the hand's once more. 1 when the leaf
 * is held (taken again here, or held where it was), 0 when it cannot be held just now (a script's
 * track with no stops of its own: nothing is driven on it, and the next tick tries again). */
static int ward_retake(PlacedSet *s, int side, int id, Placed *p, Vec3 hand) {
    if (p->paused && p->keys == sWardKeys[side] && p->frames == sWardFrames[side]) {
        return 1;   /* (still held where the hand took it) */
    }
    return ward_take(s, side, id, hand);
}

/* the hand turns the leaf about its hinge: the frame its track gives for the hand's angle. The step
 * a tick it takes is kept, so a hand that lets go while the leaf is turning leaves it its speed
 * (ward_release); a hand held against a stop gives it no step, so the leaf stops there. */
static void ward_drive(PlacedSet *s, int side, int id, Placed *p, Vec3 hand) {
    float ang, frame;

    if (!p->paused || p->keys != sWardKeys[side] || p->frames != sWardFrames[side]) {
        return;   /* (not the track the hand holds: ward_retake takes the leaf again first) */
    }
    ang = atan2f(hand.x - p->pos.x, hand.z - p->pos.z);
    frame = (float)sWardFrame[side] + wrap_rad(ang - sWardAng[side]) / sWardRate[side];

    if (frame < (float)sWardLo[side]) {
        frame = (float)sWardLo[side];
    } else if (frame > (float)sWardHi[side]) {
        frame = (float)sWardHi[side];
    }
    if (id >= 0 && id < PLACED_MAX) {
        sWardMove[id].speed = frame - sWardLast[side];   /* (frames a tick) */
    }
    sWardLast[side] = frame;
    placed_frame_set(s, p, (int)(frame + 0.5f));
}

/* the hand has let the leaf go: turning faster than WARD_COAST a tick, the leaf keeps that speed
 * towards the stop it was heading for (ward_tick carries it there); slower, it stays where the
 * hand left it. */
static void ward_release(PlacedSet *s, int side, int id) {
    WardMove *w = &sWardMove[id];
    Placed *p = &s->p[id];
    float degs;

    w->mode = 0;
    if (!p->shown || p->keys == NULL || p->frames <= 0 || !p->paused || p->format != 8) {
        return;
    }
    /* the frames a tick in the degrees of the leaf's own swing (radians a frame of its track) */
    degs = fabsf(w->speed) * fabsf(sWardRate[side]) * (180.0f / 3.14159265f);
    if (degs < WARD_COAST) {
        return;
    }
    w->at = (float)p->frame;
    w->mode = 1;
}

/* a leaf no hand holds now: it coasts, as an exit door let go of while moving does, until it meets
 * the stop it was heading for - the flat open end of its track one way, the flat shut end the
 * other. Placed leaves are the room's, so a change of room drops what was running here. */
static void ward_tick(Engine *e) {
    PlacedSet *s = &e->room.placed;
    int i;

    if (sWardRoom != e->room.id) {
        memset(sWardMove, 0, sizeof(sWardMove));
        sWardRoom = e->room.id;
        return;
    }
    for (i = 0; i < s->n; i++) {
        WardMove *w = &sWardMove[i];
        Placed *p = &s->p[i];
        int lo = 0, hi = 0;

        if (w->mode == 0) {
            continue;
        }
        if (!p->shown || p->keys == NULL || !p->paused || p->format != 8 || !ward_leaf(p->name) ||
            !ward_span(p, &lo, &hi)) {
            w->mode = 0;   /* (a script has the leaf again, or it is put away: hands off) */
            continue;
        }
        w->at += w->speed;
        if (w->speed > 0.0f ? w->at >= (float)hi : w->at <= (float)lo) {
            w->at = w->speed > 0.0f ? (float)hi : (float)lo;   /* (the stop it was heading for) */
            w->mode = 0;
        }
        placed_frame_set(s, p, (int)(w->at + 0.5f));
    }
}

/* (temporary: the hiding spot's own line - nothing here moves her or the leaf)
 * Where she is, her eyes (vr_anchor_get: the anchor the hands and the grabs sit on), against the
 * bounds the wardrobe's leaf has in the world: the same ward_bounds a hand reaches for it by, its
 * vertices through placed_world, which is the pose its frame gives. A line to stderr the first time
 * it is read, then only when the answer changes - she is in or out of those bounds, or the leaf
 * stands at another frame - so a session in the nursery says whether those bounds can tell inside
 * from outside at all. A room without the leaf, or no anchor to ask, says nothing and forgets the
 * last line: the next look is read fresh. */
static void hide_probe(Engine *e) {
    PlacedSet *s = &e->room.placed;
    Placed *p = NULL;
    Vec3 eye, lo, hi;
    int i, inside;

    if (sHideRoom != e->room.id) {   /* (another room's leaves and frames are not these) */
        sHideSeen = 0;
        sHideRoom = e->room.id;
    }
    for (i = 0; i < s->n; i++) {
        if (s->p[i].shown && s->p[i].mesh.nv > 0 && ward_leaf(s->p[i].name)) {
            p = &s->p[i];
            break;
        }
    }
    if (p == NULL || !vr_anchor_get(&eye, NULL)) {
        sHideSeen = 0;   /* (no leaf to stand in, or no eyes to stand there with: the next look says it) */
        return;
    }
    ward_bounds(p, &lo, &hi);
    inside = eye.x >= lo.x && eye.x <= hi.x && eye.y >= lo.y && eye.y <= hi.y && eye.z >= lo.z && eye.z <= hi.z;
    if (sHideSeen && inside == sHideInside && p->frame == sHideFrame) {
        return;   /* (the same answer as the last look: nothing new to say) */
    }
    sHideSeen = 1;
    sHideInside = inside;
    sHideFrame = p->frame;
    fprintf(stderr, "hide: inside %d frame %d\n", inside, p->frame);
}

static int nearest_prop(PlacedSet *s, Vec3 hand, int other, float *best) {
    int i, found = -1;

    *best = 7.0f * 7.0f;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 ext;
        float edge, d2;

        if (!p->shown || (other >= 0 && sHold[other] == HOLD_PROP && sHoldId[other] == i) || loot_claims_placed(i) ||
            ward_leaf(p->name)) {   /* (a leaf is turned by hand, not carried) */
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
    } else if (sHold[side] == HOLD_WARD && id >= 0 && id < r->placed.n) {
        ward_release(&r->placed, side, id);   /* (a leaf still turning keeps its speed) */
    }
    sHold[side] = HOLD_NONE;
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

/* the controller point against the posed mesh. About 6 cm: a touch, not a reach. */
#define PET_SKIN 0.75f

/* closest distance squared from p to triangle abc (Christer Ericson) */
static float tri_dist2(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    Vec3 ab = vec3_sub(b, a), ac = vec3_sub(c, a), ap = vec3_sub(p, a);
    float d1 = vec3_dot(ab, ap), d2 = vec3_dot(ac, ap);

    if (d1 <= 0.0f && d2 <= 0.0f) {
        return vec3_dot(ap, ap);
    }
    {
        Vec3 bp = vec3_sub(p, b);
        float d3 = vec3_dot(ab, bp), d4 = vec3_dot(ac, bp);

        if (d3 >= 0.0f && d4 <= d3) {
            return vec3_dot(bp, bp);
        }
        {
            float vc = d1 * d4 - d3 * d2;

            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                float v = d1 / (d1 - d3);
                Vec3 r = vec3_sub(vec3_add(a, vec3_scale(ab, v)), p);

                return vec3_dot(r, r);
            }
            {
                Vec3 cp = vec3_sub(p, c);
                float d5 = vec3_dot(ab, cp), d6 = vec3_dot(ac, cp);

                if (d6 >= 0.0f && d5 <= d6) {
                    return vec3_dot(cp, cp);
                }
                {
                    float vb = d5 * d2 - d1 * d6;

                    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                        float w = d2 / (d2 - d6);
                        Vec3 r = vec3_sub(vec3_add(a, vec3_scale(ac, w)), p);

                        return vec3_dot(r, r);
                    }
                    {
                        float va = d3 * d6 - d5 * d4;

                        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
                            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                            Vec3 r = vec3_sub(vec3_add(b, vec3_scale(vec3_sub(c, b), w)), p);

                            return vec3_dot(r, r);
                        }
                        {
                            float sum = va + vb + vc;

                            if (sum < 1.0e-8f && sum > -1.0e-8f) {
                                return vec3_dot(ap, ap);
                            }
                            {
                                float denom = 1.0f / sum;
                                float v = vb * denom, w = vc * denom;
                                Vec3 r = vec3_sub(vec3_add(a, vec3_add(vec3_scale(ab, v), vec3_scale(ac, w))), p);

                                return vec3_dot(r, r);
                            }
                        }
                    }
                }
            }
        }
    }
}

static const Actor *hewie_mesh(void) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];

        if (a->used && a->visible && a->posed != NULL && strncmp(a->name, "O_HEW", 5) == 0) {
            return a;
        }
    }
    return NULL;
}

static int primary_bone(const SkinVertex *v) {
    int k, best = v->bones[0];
    float w = v->weights[0];

    for (k = 1; k < 4; k++) {
        if (v->weights[k] > w) {
            w = v->weights[k];
            best = v->bones[k];
        }
    }
    return best;
}

/* 1 when the hand is on the mesh. `bone` is the joint that owns the hit. */
static int mesh_touch(const Actor *dog, Vec3 p, int *bone) {
    const Model *m = &dog->model;
    float lim = PET_SKIN * PET_SKIN;
    float reach = actor_radius(dog) + PET_SKIN;
    float best = lim;
    Vec3 d = vec3_sub(p, actor_center(dog));
    int i, hit = 0;

    if (vec3_dot(d, d) > reach * reach) {
        return 0;
    }
    for (i = 0; i < m->nd; i++) {
        const MeshDraw *draw = &m->d[i];
        int v, end;

        if (draw->count < 3) {
            continue;
        }
        end = draw->first + draw->count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (v = draw->first; v + 2 < end; v += 3) {
            Vec3 a = vec3(dog->posed[v].x, dog->posed[v].y, dog->posed[v].z);
            Vec3 b = vec3(dog->posed[v + 1].x, dog->posed[v + 1].y, dog->posed[v + 1].z);
            Vec3 c = vec3(dog->posed[v + 2].x, dog->posed[v + 2].y, dog->posed[v + 2].z);
            float dist = tri_dist2(p, a, b, c);
            float da, db, dc;
            int pick;

            if (dist > best) {
                continue;
            }
            da = vec3_len(vec3_sub(p, a));
            db = vec3_len(vec3_sub(p, b));
            dc = vec3_len(vec3_sub(p, c));
            pick = v;
            if (db < da && db <= dc) {
                pick = v + 1;
            } else if (dc < da && dc < db) {
                pick = v + 2;
            }
            best = dist;
            hit = 1;
            if (bone != NULL && pick < m->nv) {
                *bone = primary_bone(dog->shaped != NULL ? &dog->shaped[pick] : &m->v[pick]);
            }
        }
    }
    return hit;
}

/* a shove on one joint, in room space. Past the part's reach it lets go. */
#define PET_HEAD 0x1F
#define PET_NECK 0x1D
static Vec3 sPetOff[MODEL_MAX_BONES];
static Vec3 sPetWant[MODEL_MAX_BONES];
static int sPetTick = -1;

static int bone_of(const Model *m, int bone, int anc) {
    int guard = 0;

    while (bone >= 0 && bone < m->nbones && guard++ < MODEL_MAX_BONES) {
        if (bone == anc) {
            return 1;
        }
        bone = m->bones[bone].parent;
    }
    return 0;
}

static int foot_chain(const Model *m, int bone) {
    static const int kFeet[] = {0x0B, 0x0F, 0x17, 0x1C};
    int i;

    for (i = 0; i < 4; i++) {
        if (bone_of(m, bone, kFeet[i])) {
            return 1;
        }
    }
    return 0;
}

/* 1 the head, 2 an ear, 3 the tail, 4 the jaw. 0 stays on the skeleton. */
#define PET_JAW 0x25
static int pet_part(const Actor *dog, int bone) {
    const Model *m = &dog->model;
    Vec3 p, head, rel, fwd;
    float along;

    if (bone < 0 || bone >= m->nbones || bone >= MODEL_MAX_BONES) {
        return 0;
    }
    if (bone_of(m, bone, PET_NECK) && !bone_of(m, bone, PET_HEAD)) {
        return 0;
    }
    if (bone == PET_HEAD) {
        return 1;
    }
    if (bone_of(m, bone, PET_JAW)) {
        return 4;
    }
    if (bone_of(m, bone, PET_HEAD)) {
        return 2;
    }
    p = dog->bones[bone];
    if (p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) {
        return 0;
    }
    if (m->nbones > PET_HEAD) {
        head = dog->bones[PET_HEAD];
        rel = vec3_sub(p, head);
        /* an ear that is not parented under the skull still sits just above it */
        if (head.y != 0.0f && rel.y > 0.2f && vec3_len(rel) < 5.5f) {
            return 2;
        }
    }
    if (bone == 0 || bone_of(m, bone, 0x10) || foot_chain(m, bone)) {
        return 0;
    }
    fwd = vec3(sinf(dog->yaw), 0.0f, cosf(dog->yaw));
    along = (p.x - dog->pos.x) * fwd.x + (p.z - dog->pos.z) * fwd.z;
    return along < -2.5f ? 3 : 0;
}

/* v turned about a unit axis. The joint itself is not moved. */
static Vec3 pet_spin_vec(Vec3 v, Vec3 axis, float ang) {
    float c = cosf(ang), s = sinf(ang);

    return vec3_add(vec3_add(vec3_scale(v, c), vec3_scale(vec3_cross(axis, v), s)),
                    vec3_scale(axis, vec3_dot(axis, v) * (1.0f - c)));
}

/* last frame's axis, with the pet swing taken back off, so the turn does not pile up */
static Vec3 pet_rest_axis(Vec3 axis, Vec3 spin) {
    float ang = vec3_len(spin);
    Vec3 n = vec3_norm(axis);

    if (ang < 0.001f) {
        return n;
    }
    return vec3_norm(pet_spin_vec(n, vec3_scale(spin, 1.0f / ang), -ang));
}

static void pet_open(void) {
    int i;

    if (sPetTick == (int)gEngine.ticks) {
        return;
    }
    sPetTick = (int)gEngine.ticks;
    for (i = 0; i < MODEL_MAX_BONES; i++) {
        sPetWant[i] = vec3(0.0f, 0.0f, 0.0f);
    }
}

static void pet_push(const Actor *dog, int bone, Vec3 hand) {
    int kind = pet_part(dog, bone);
    int k, bestk = -1;
    Vec3 pivot, to, best, axis, want;
    float d, maxa, letgo, reach, bestd = -1.0f, ang, al;

    if (kind == 0 || bone < 0 || bone >= MODEL_MAX_BONES) {
        return;
    }
    /* head stays a short nod; an ear can fold most of the way over */
    if (kind == 1) {
        maxa = 0.55f; letgo = 1.15f; reach = 6.0f;
    } else if (kind == 2) {
        maxa = 1.90f; letgo = 2.55f; reach = 8.0f;
    } else if (kind == 4) {
        maxa = 0.50f; letgo = 1.20f; reach = 5.5f;
    } else {
        maxa = 1.05f; letgo = 1.90f; reach = 9.0f;
    }
    pivot = dog->bones[bone];
    to = vec3_sub(hand, pivot);
    d = vec3_len(to);
    if (d < 0.35f || d > reach) {
        return;   /* off the socket: the part goes back to rest */
    }
    to = vec3_scale(to, 1.0f / d);
    for (k = 0; k < 3; k++) {
        Vec3 ax = pet_rest_axis(dog->bone_axes[bone][k], sPetOff[bone]);
        float dp;

        if (vec3_len(ax) < 0.5f) {
            continue;
        }
        dp = vec3_dot(ax, to);
        if (fabsf(dp) > bestd) {
            bestd = fabsf(dp);
            bestk = k;
            best = dp >= 0.0f ? ax : vec3_scale(ax, -1.0f);
        }
    }
    if (bestk < 0) {
        return;
    }
    ang = acosf(fminf(1.0f, fmaxf(-1.0f, vec3_dot(best, to))));
    if (ang > letgo || ang < 0.02f) {
        return;   /* past the stop, or already aimed: let the joint ease home */
    }
    if (ang > maxa) {
        ang = maxa;
    }
    axis = vec3_cross(best, to);
    al = vec3_len(axis);
    if (al < 1.0e-4f) {
        return;
    }
    want = vec3_scale(axis, ang / al);
    if (vec3_len(sPetWant[bone]) < vec3_len(want)) {
        sPetWant[bone] = want;
    }
}

static void pet_settle(void) {
    int i;

    if (sPetTick != (int)gEngine.ticks) {
        sPetTick = (int)gEngine.ticks;
        for (i = 0; i < MODEL_MAX_BONES; i++) {
            sPetWant[i] = vec3(0.0f, 0.0f, 0.0f);
        }
    }
    for (i = 0; i < MODEL_MAX_BONES; i++) {
        sPetOff[i] = vec3_add(sPetOff[i], vec3_scale(vec3_sub(sPetWant[i], sPetOff[i]), 0.45f));
        if (vec3_len(sPetOff[i]) < 0.02f && vec3_len(sPetWant[i]) < 0.02f) {
            sPetOff[i] = vec3(0.0f, 0.0f, 0.0f);
        }
    }
}

static int bone_depth(const Model *m, int bone) {
    int depth = 0, guard = 0;

    while (bone >= 0 && bone < m->nbones && guard++ < MODEL_MAX_BONES) {
        int parent = m->bones[bone].parent;

        if (parent < 0 || parent >= m->nbones) {
            break;
        }
        depth++;
        bone = parent;
    }
    return depth;
}

/* turn a posed bone about `pivot`. The bone that owns the pivot stays put; its children keep their length. */
static void pet_turn_about(Mat4 *w, Vec3 pivot, Vec3 axis, float ang) {
    Vec3 p = pet_spin_vec(vec3_sub(vec3(w->m[12], w->m[13], w->m[14]), pivot), axis, ang);
    int c;

    for (c = 0; c < 3; c++) {
        Vec3 v = pet_spin_vec(vec3(w->m[c * 4], w->m[c * 4 + 1], w->m[c * 4 + 2]), axis, ang);

        w->m[c * 4] = v.x;
        w->m[c * 4 + 1] = v.y;
        w->m[c * 4 + 2] = v.z;
    }
    w->m[12] = pivot.x + p.x;
    w->m[13] = pivot.y + p.y;
    w->m[14] = pivot.z + p.z;
}

void vrhands_pet_pose(Actor *a, Mat4 *world, int nbones, const Mat4 *unplace) {
    const Model *m;
    int pass, i;

    if (a == NULL || world == NULL || unplace == NULL || strncmp(a->name, "O_HEW", 5) != 0) {
        return;
    }
    m = &a->model;
    if (nbones > m->nbones) {
        nbones = m->nbones;
    }
    if (nbones > MODEL_MAX_BONES) {
        nbones = MODEL_MAX_BONES;
    }
    for (pass = 0; pass < nbones; pass++) {
        for (i = 0; i < nbones; i++) {
            Vec3 spin = sPetOff[i];
            Vec3 axis;
            float ang;
            int j;

            if (bone_depth(m, i) != pass || vec3_len(spin) < 0.02f) {
                continue;
            }
            ang = vec3_len(spin);
            spin = vec3_scale(spin, 1.0f / ang);
            axis = vec3(unplace->m[0] * spin.x + unplace->m[4] * spin.y + unplace->m[8] * spin.z,
                        unplace->m[1] * spin.x + unplace->m[5] * spin.y + unplace->m[9] * spin.z,
                        unplace->m[2] * spin.x + unplace->m[6] * spin.y + unplace->m[10] * spin.z);
            if (vec3_len(axis) < 1.0e-4f) {
                continue;
            }
            axis = vec3_norm(axis);
            {
                Vec3 pivot = vec3(world[i].m[12], world[i].m[13], world[i].m[14]);

                for (j = 0; j < nbones; j++) {
                    if (bone_of(m, j, i)) {
                        pet_turn_about(&world[j], pivot, axis, ang);
                    }
                }
            }
        }
    }
}

/* a hard swing on a surface. About 3 cm, and fast enough that a rest or a pet does not count. */
#define SWING_SKIN 0.40f
#define SWING_SPEED 1.25f
#define SWING_FAST 8.0f
#define ENV_COOL 12
#define VOICE_COOL 12
/* about 1.4 m past the skin, so a shove lands beside him */
#define SHOVE_REACH 18.0f
#define SHOVE_WINDOW 20
#define SHOVE_COOL 24
/* above the 5° ease and a slow push; a slammed door moves at 15° */
#define DOOR_HIT_SPEED 10.0f
#define DOOR_HIT_MARGIN 8.0f
static int sBlowOn, sShoveOn, sSwingOn, sEnvCool, sShoveCool, sVoiceCool;
static Vec3 sBlowAt;
static Vec3 sHandLocal[2], sHeadLocal, sBodyDelta, sFwd;
static int sHeadHave;
static int sStalkTick[2] = {-1, -1};
static int sDoorTick[2] = {-1, -1}, sDoorHit[2] = {-1, -1}, sDoorShove = -1, sDoorWay = -1;
static float sDoorZ[2];
static float sSwingWas[ROOM_DOORS];
static int sLeafArm[ROOM_DOORS], sLeafHits, sLeafRoom = -2;

int vrhands_blow_at(float out[3]) {
    if (!sBlowOn) {
        return 0;
    }
    if (out != NULL) {
        out[0] = sBlowAt.x;
        out[1] = sBlowAt.y;
        out[2] = sBlowAt.z;
    }
    return 1;
}

int vrhands_swing(void) { return sSwingOn; }

int vrhands_shove_ready(void) { return sShoveOn; }

int vrhands_door_shove(void) { return sDoorShove; }

int vrhands_door_way(void) { return sDoorWay < 0 ? -1 : 1; }

void vrhands_door_take(void) {
    sDoorShove = -1;
    sDoorTick[0] = sDoorTick[1] = -1;
    sDoorHit[0] = sDoorHit[1] = -1;
}

int vrhands_door_hits(void) { return sLeafHits; }

void vrhands_shove_take(void) {
    sShoveOn = 0;
    sShoveCool = SHOVE_COOL;
    sStalkTick[0] = sStalkTick[1] = -1;
}

static int group_shown(const Room *r, int group) {
    if (group <= 0) {
        return 1;
    }
    if (group >= 256) {
        return 0;
    }
    return (r->groups[group >> 5] >> (group & 31) & 1u) != 0;
}

static int tris_near(const MeshVertex *v, int nv, const MeshDraw *d, int nd, Vec3 p, float lim2) {
    int i;

    for (i = 0; i < nd; i++) {
        int n, end;

        if (d[i].count < 3 || d[i].mask) {
            continue;
        }
        end = d[i].first + d[i].count;
        if (end > nv) {
            end = nv;
        }
        for (n = d[i].first; n + 2 < end; n += 3) {
            Vec3 a = vec3(v[n].x, v[n].y, v[n].z);
            Vec3 b = vec3(v[n + 1].x, v[n + 1].y, v[n + 1].z);
            Vec3 c = vec3(v[n + 2].x, v[n + 2].y, v[n + 2].z);

            if (tri_dist2(p, a, b, c) <= lim2) {
                return 1;
            }
        }
    }
    return 0;
}

static int room_touch(const Room *r, Vec3 p) {
    const RoomMesh *m = &r->mesh;
    float lim2 = SWING_SKIN * SWING_SKIN;
    int i;

    for (i = 0; i < m->nd; i++) {
        const MeshDraw *d = &m->d[i];
        int n, end;

        if (d->count < 3 || d->mask || !group_shown(r, d->group)) {
            continue;
        }
        end = d->first + d->count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (n = d->first; n + 2 < end; n += 3) {
            Vec3 a = vec3(m->v[n].x, m->v[n].y, m->v[n].z);
            Vec3 b = vec3(m->v[n + 1].x, m->v[n + 1].y, m->v[n + 1].z);
            Vec3 c = vec3(m->v[n + 2].x, m->v[n + 2].y, m->v[n + 2].z);

            if (tri_dist2(p, a, b, c) <= lim2) {
                return 1;
            }
        }
    }
    return 0;
}

static int placed_touch(const PlacedSet *s, Vec3 p) {
    float lim2 = SWING_SKIN * SWING_SKIN;
    int i;

    for (i = 0; i < s->n; i++) {
        const Placed *o = &s->p[i];
        Mat4 w;
        int n;

        if (!o->shown || o->mesh.v == NULL || o->mesh.nv < 3) {
            continue;
        }
        w = placed_world(o);
        for (n = 0; n + 2 < o->mesh.nv; n += 3) {
            Vec3 a = mat4_point(&w, vec3(o->mesh.v[n].x, o->mesh.v[n].y, o->mesh.v[n].z));
            Vec3 b = mat4_point(&w, vec3(o->mesh.v[n + 1].x, o->mesh.v[n + 1].y, o->mesh.v[n + 1].z));
            Vec3 c = mat4_point(&w, vec3(o->mesh.v[n + 2].x, o->mesh.v[n + 2].y, o->mesh.v[n + 2].z));

            if (tri_dist2(p, a, b, c) <= lim2) {
                return 1;
            }
        }
    }
    return 0;
}

static int stalker_name(const char *name) {
    return strncmp(name, "O_DB", 4) == 0 || strncmp(name, "O_DN", 4) == 0 || strncmp(name, "O_LR", 4) == 0 ||
           strncmp(name, "O_RC", 4) == 0;
}

static int stalker_touch(Vec3 p) {
    float lim2 = SHOVE_REACH * SHOVE_REACH;
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];

        if (!a->used || !a->visible || a->posed == NULL || a->model.d == NULL || !stalker_name(a->name)) {
            continue;
        }
        if (tris_near(a->posed, a->model.nv, a->model.d, a->model.nd, p, lim2)) {
            return 1;
        }
    }
    return 0;
}

static float box_gap(Vec3 p, Vec3 lo, Vec3 hi) {
    float dx = p.x < lo.x ? lo.x - p.x : p.x > hi.x ? p.x - hi.x : 0.0f;
    float dy = p.y < lo.y ? lo.y - p.y : p.y > hi.y ? p.y - hi.y : 0.0f;
    float dz = p.z < lo.z ? lo.z - p.z : p.z > hi.z ? p.z - hi.z : 0.0f;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* shut, unlocked, not held, not stuck, and not already swinging */
static int door_shut_free(const Engine *e, int exit) {
    const RoomDoor *d;
    int id;

    if (exit < 0 || exit >= ROOM_DOORS) {
        return 0;
    }
    d = &e->room.doors[exit];
    if (!d->present || d->mode != 0 || fabsf(d->swing) > DOOR_LATCH || door_blocked(e, exit)) {
        return 0;
    }
    id = door_index(e, exit);
    return id < 0 || (gProgress.doors[id] & 4u) == 0;
}

static int door_shove_touch(const Engine *e, Vec3 hand, int *exit) {
    int i, found = -1;
    float best = SHOVE_REACH;

    for (i = 0; i < ROOM_DOORS; i++) {
        Vec3 q;
        float dist;

        if (!door_shut_free(e, i)) {
            continue;
        }
        q = door_local(&e->room.doors[i], hand);
        dist = box_gap(q, vec3(0.0f, 0.0f, -0.5f), vec3(12.0f, 22.0f, 0.5f));
        if (dist <= best) {
            best = dist;
            found = i;
        }
    }
    if (found < 0) {
        return 0;
    }
    *exit = found;
    return 1;
}

/* the headset's own step this frame, so a run does not count as a swing */
static void swing_body(void) {
    Vec3 head;

    sBodyDelta = vec3(0.0f, 0.0f, 0.0f);
    sFwd = vec3(0.0f, 0.0f, -1.0f);
    if (!vr_head_local(&head)) {
        sHeadHave = 0;
        return;
    }
    if (!vr_head_forward(&sFwd)) {
        sFwd = vec3(0.0f, 0.0f, -1.0f);
    }
    if (sHeadHave) {
        sBodyDelta = vec3_sub(head, sHeadLocal);
    }
    sHeadLocal = head;
    sHeadHave = 1;
}

/* the script frame just read last tick's blows. A fast hand on a mesh sets the next one. */
static void swing_begin(void) {
    sBlowOn = 0;
    sShoveOn = 0;
    sSwingOn = 0;
    sDoorShove = -1;
    if (sEnvCool > 0) {
        sEnvCool--;
    }
    if (sShoveCool > 0) {
        sShoveCool--;
    }
    if (sVoiceCool > 0) {
        sVoiceCool--;
    }
}

static void swing_hand(int side, Vec3 at) {
    VrController c;
    Vec3 moved;
    float speed, forward;
    int now = (int)gEngine.ticks;
    int door = -1, on_stalker;

    if (!vr_controller(side, &c) || !c.tracked) {
        sHandHave[side] = 0;
        return;
    }
    if (!sHandHave[side]) {
        sHandLocal[side] = c.at;
        return;
    }
    /* play space, with the body's own step taken off, and only a shove toward where she looks */
    moved = vec3_sub(vec3_sub(c.at, sHandLocal[side]), sBodyDelta);
    sHandLocal[side] = c.at;
    forward = vec3_dot(moved, sFwd);
    speed = vec3_len(moved);
    if (forward < SWING_SPEED || speed > SWING_FAST) {
        return;
    }
    if (sVoiceCool <= 0) {
        sSwingOn = 1;
        sVoiceCool = VOICE_COOL;
    }
    if (door_shove_touch(&gEngine, at, &door)) {
        sDoorTick[side] = now;
        sDoorHit[side] = door;
        sDoorZ[side] = door_local(&gEngine.room.doors[door], at).z;
        return;
    }
    on_stalker = stalker_touch(at);
    if (on_stalker && sShoveCool <= 0) {
        sStalkTick[side] = now;
        return;
    }
    if (!on_stalker && sEnvCool <= 0 && (room_touch(&gEngine.room, at) || placed_touch(&gEngine.room.placed, at))) {
        sBlowAt = at;
        sBlowOn = 1;
        sEnvCool = ENV_COOL;
    }
}

static void swing_finish(void) {
    int now = (int)gEngine.ticks;
    int a = sStalkTick[0], b = sStalkTick[1];
    int da = sDoorTick[0], db = sDoorTick[1];

    if (sShoveCool <= 0 && a >= 0 && b >= 0 && now - a <= SHOVE_WINDOW && now - b <= SHOVE_WINDOW) {
        sShoveOn = 1;
        sStalkTick[0] = sStalkTick[1] = -1;
    }
    if (da >= 0 && db >= 0 && sDoorHit[0] == sDoorHit[1] && now - da <= SHOVE_WINDOW && now - db <= SHOVE_WINDOW &&
        door_shut_free(&gEngine, sDoorHit[0])) {
        sDoorShove = sDoorHit[0];
        sDoorWay = (sDoorZ[0] + sDoorZ[1]) >= 0.0f ? 1 : -1;
        sDoorTick[0] = sDoorTick[1] = -1;
        sDoorHit[0] = sDoorHit[1] = -1;
    }
}

static int on_leaf(const RoomDoor *d, float swing, Vec3 p) {
    Vec3 q = door_local_at(d, p, swing);

    return q.x >= -DOOR_HIT_MARGIN && q.x <= 12.0f + DOOR_HIT_MARGIN && q.y >= -DOOR_HIT_MARGIN &&
           q.y <= 22.0f + DOOR_HIT_MARGIN && q.z >= -DOOR_HIT_MARGIN && q.z <= DOOR_HIT_MARGIN;
}

static int leaf_meets_point(const RoomDoor *d, float from, float to, Vec3 p) {
    float span = to - from;
    int n = (int)(fabsf(span) / 8.0f) + 1;
    int i;

    if (n > 6) {
        n = 6;
    }
    for (i = 0; i <= n; i++) {
        if (on_leaf(d, from + span * ((float)i / (float)n), p)) {
            return 1;
        }
    }
    return 0;
}

static int leaf_meets(const RoomDoor *d, float from, float to) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];
        Vec3 p, mid, high;

        if (!a->used || !a->visible || !stalker_name(a->name)) {
            continue;
        }
        p = a->pos;
        mid = vec3(p.x, p.y + 10.0f, p.z);
        high = vec3(p.x, p.y + 18.0f, p.z);
        if (leaf_meets_point(d, from, to, p) || leaf_meets_point(d, from, to, mid) || leaf_meets_point(d, from, to, high)) {
            return 1;
        }
    }
    return 0;
}

/* a leaf that moved fast this frame, however it was driven, and a stalker standing in that sweep */
static void leaf_scan(Engine *e) {
    int i, hits = 0;

    if (e->room.id != sLeafRoom) {
        sLeafRoom = e->room.id;
        for (i = 0; i < ROOM_DOORS; i++) {
            sSwingWas[i] = e->room.doors[i].swing;
            sLeafArm[i] = 0;
        }
        sLeafHits = 0;
        return;
    }
    for (i = 0; i < ROOM_DOORS; i++) {
        RoomDoor *d = &e->room.doors[i];
        float s = d->present ? d->swing : sSwingWas[i];
        float delta = s - sSwingWas[i];

        sSwingWas[i] = s;
        if (!d->present || fabsf(delta) < DOOR_HIT_SPEED) {
            sLeafArm[i] = 0;
            continue;
        }
        if (sLeafArm[i]) {
            continue;
        }
        if (leaf_meets(d, s - delta, s)) {
            sLeafArm[i] = 1;
            hits |= 1 << i;
        }
    }
    sLeafHits = hits;
}

/* an equipped magnesia (dropped) or antimony (thrown), shown in the right hand until a
 * grip closes after it appears and then opens. */
static struct {
    int item, kind, drop, side;
    int armed, saw_low, shown;
    int pending, pkind;
    Vec3 at, right, up, fwd;
    Vec3 hist[4];
    int histN;
    Vec3 pat, pvel;
} sDef = {.item = -1, .kind = -1, .side = 1};

static int defense_kind(int item, int *drop) {
    switch (item) {
    case 0x90: *drop = 0; return 0; /* ball */
    case 0x91: *drop = 0; return 1; /* antimony powder */
    case 0x93: *drop = 0; return 3; /* bisque doll */
    case 0x94: *drop = 0; return 4; /* refined antimony */
    case 0x97: *drop = 0; return 6; /* antimony tube */
    case 0x92: *drop = 1; return 2; /* magnesia */
    case 0x95: *drop = 1; return 5; /* refined magnesia */
    case 0x98: *drop = 1; return 7; /* prima magnesia */
    case 0x9B: *drop = 1; return 8; /* weeping stone */
    default: return -1;
    }
}

static int world_held(void) { return (gProgress.state[0] & (1u << 8)) != 0; }

static void defense_hide(void) {
    sDef.item = -1;
    sDef.kind = -1;
    sDef.armed = 0;
    sDef.saw_low = 0;
    sDef.shown = 0;
    sDef.histN = 0;
}

static void defense_note(Vec3 at) {
    if (sDef.histN < 4) {
        sDef.hist[sDef.histN++] = at;
        return;
    }
    sDef.hist[0] = sDef.hist[1];
    sDef.hist[1] = sDef.hist[2];
    sDef.hist[2] = sDef.hist[3];
    sDef.hist[3] = at;
}

static Vec3 defense_flick(Vec3 fwd, Vec3 up) {
    Vec3 best = vec3(0.0f, 0.0f, 0.0f);
    float bd = 0.0f;
    int i;

    for (i = 1; i < sDef.histN; i++) {
        Vec3 d = vec3_sub(sDef.hist[i], sDef.hist[i - 1]);
        float L = vec3_len(d);

        if (L > bd) {
            bd = L;
            best = d;
        }
    }
    if (bd < 2.0f) {
        return vec3_add(vec3_scale(fwd, 6.0f), vec3_scale(up, 0.4f));
    }
    if (bd > 12.0f) {
        return vec3_scale(best, 12.0f / bd);
    }
    return best;
}

static Vec3 defense_place(Vec3 at, Vec3 up, Vec3 fwd) {
    return vec3_add(at, vec3_add(vec3_scale(up, 0.35f), vec3_scale(fwd, 0.6f)));
}

static void defense_tick(void) {
    int item, kind, drop = 0, side;
    Vec3 at, right, up, fwd, place;
    float grip, index, thumb;

    if (!vr_active()) {
        defense_hide();
        return;
    }
    item = gProgress.equipped[2];
    kind = defense_kind(item, &drop);
    if (kind < 0) {
        defense_hide();
        return;
    }
    if (item != sDef.item || kind != sDef.kind) {
        sDef.item = item;
        sDef.kind = kind;
        sDef.drop = drop;
        sDef.armed = 0;
        sDef.saw_low = 0;
        sDef.histN = 0;
        sDef.side = 1;
    }
    side = sDef.side;
    if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
        if (!hand_world(side ^ 1, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            return;
        }
        side = side ^ 1;
        if (side != sDef.side) {
            sDef.side = side;
            sDef.armed = 0;
            sDef.saw_low = 0;
            sDef.histN = 0;
        }
    }
    place = defense_place(at, up, fwd);
    sDef.at = place;
    sDef.right = right;
    sDef.up = up;
    sDef.fwd = fwd;
    sDef.shown = 1;
    sDef.kind = kind;
    sDef.drop = drop;
    defense_note(place);
    if (world_held() || sHold[side] != HOLD_NONE) {
        return;
    }
    if (!sDef.armed) {
        if (grip <= 0.4f) {
            sDef.saw_low = 1;
        }
        if (sDef.saw_low && grip > 0.65f) {
            sDef.armed = 1;
        }
        return;
    }
    if (grip <= 0.4f) {
        sDef.pending = 1;
        sDef.pkind = kind;
        sDef.pat = place;
        sDef.pvel = sDef.drop ? vec3(0.0f, 0.0f, 0.0f) : defense_flick(fwd, up);
        defense_hide();
    }
}

void vrhands_defense_draw(const Mat4 *view_proj) {
    Mat4 m;

    if (!sDef.shown || sDef.kind < 0 || view_proj == NULL) {
        return;
    }
    m = mat4_identity();
    m.m[0] = sDef.right.x;
    m.m[1] = sDef.right.y;
    m.m[2] = sDef.right.z;
    m.m[4] = sDef.up.x;
    m.m[5] = sDef.up.y;
    m.m[6] = sDef.up.z;
    m.m[8] = sDef.fwd.x;
    m.m[9] = sDef.fwd.y;
    m.m[10] = sDef.fwd.z;
    m.m[12] = sDef.at.x;
    m.m[13] = sDef.at.y;
    m.m[14] = sDef.at.z;
    loot_draw_held(sDef.item, &m, view_proj);
}

int vrhands_defense_take(int *kind, float *x, float *y, float *z, float *vx, float *vy, float *vz) {
    if (!sDef.pending) {
        *kind = 0;
        *x = *y = *z = *vx = *vy = *vz = 0.0f;
        return 0;
    }
    *kind = sDef.pkind;
    *x = sDef.pat.x;
    *y = sDef.pat.y;
    *z = sDef.pat.z;
    *vx = sDef.pvel.x;
    *vy = sDef.pvel.y;
    *vz = sDef.pvel.z;
    sDef.pending = 0;
    return 1;
}

int vrhands_petting(int side) {
    const Actor *dog;
    Vec3 at, right, up, fwd;
    float grip, index, thumb;
    int bone = -1;

    if (side < 0 || side > 1 || !vr_active()) {
        return 0;
    }
    pet_open();
    dog = hewie_mesh();
    if (dog == NULL) {
        return 0;
    }
    if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
        return 0;
    }
    (void)right;
    (void)up;
    (void)fwd;
    (void)grip;
    (void)index;
    (void)thumb;
    if (!mesh_touch(dog, at, &bone)) {
        return 0;
    }
    pet_push(dog, bone, at);
    vr_haptic(side, 0.075f, 0.04f);
    return 1;
}

void vrhands_tick(Engine *e) {
    int side, i;

    if (e == NULL) {
        return;
    }
    hactor_set_blow_at(vrhands_blow_at);   /* hactor's `body-point` asks the hands for a hard swing here */
    swing_begin();
    pet_settle();
    poses_restore(e);
    if (!vr_active() || !vr_anchor_get(NULL, NULL)) {
        poses_save(e);
        leaf_scan(e);
        return;
    }
    swing_body();
    if (sPushRoom != e->room.id) {
        memset(sPushNow, 0, sizeof(sPushNow));
        memset(sPushWas, 0, sizeof(sPushWas));
        sPushRoom = e->room.id;
    }
    for (side = 0; side < 2; side++) {
        Vec3 at, right, fwd, up;
        float grip, index, thumb, doorD, propD, wardD;
        int rising, door, axis = 0, prop, ward;

        if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            if (sHold[side] == HOLD_LOOT) {
                loot_let_go(sHoldId[side], side);
                sHold[side] = HOLD_NONE;
            } else if (sHold[side] != HOLD_NONE) {
                release_hand(&e->room, side);
            }
            sGripWas[side] = 0.0f;
            sHandHave[side] = 0;
            sWardNear[side] = 0;   /* (temporary: no hand, no leaf in reach) */
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
            if (sWardLogs < 20) {   /* (temporary: what a rising grip sees of the wardrobe's leaves) */
                float d = 0.0f;

                if (ward_near(&e->room.placed, at, 60.0f, &d) >= 0) {
                    sWardLogs++;
                    fprintf(stderr, "ward: hand %d dist %.1f grip %.2f\n", side, d, grip);
                }
            }
        }
        {   /* (temporary: a buzz the first frame a hand is within reach of a leaf) */
            float d = 0.0f;

            if (ward_near(&e->room.placed, at, WARD_TOUCH, &d) >= 0) {
                if (!sWardNear[side]) {
                    sWardNear[side] = 1;
                    vr_haptic(side, 0.3f, 0.05f);
                }
            } else {
                sWardNear[side] = 0;   /* (out of range: in again buzzes again) */
            }
        }
        if (grip <= 0.4f) {
            if (sHold[side] == HOLD_LOOT) {
                loot_let_go(sHoldId[side], side);
                sHold[side] = HOLD_NONE;
            } else if (sHold[side] != HOLD_NONE) {
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
            ward = nearest_ward(&e->room.placed, at, side ^ 1, &wardD);
            prop = nearest_prop(&e->room.placed, at, side ^ 1, &propD);
            {
                int skip = sHold[side ^ 1] == HOLD_LOOT ? sHoldId[side ^ 1] : -1;
                float lootD = 0;
                int loot = loot_nearest(at, skip, &lootD);

                if (loot >= 0 && (door < 0 || lootD < doorD) && (prop < 0 || lootD <= propD) &&
                    (ward < 0 || lootD <= wardD)) {
                    sHold[side] = HOLD_LOOT;
                    sHoldId[side] = loot;
                    loot_grab(loot, side);
                    loot_follow(loot, at, right, up, fwd);
                    door = -1;
                    prop = -1;
                }
            }
            if (sHold[side] == HOLD_NONE && ward >= 0 && (door < 0 || wardD <= doorD) &&
                (prop < 0 || wardD <= propD) && ward_take(&e->room.placed, side, ward, at)) {
                sHold[side] = HOLD_WARD;   /* (the hand has the leaf: its frame follows the hand) */
                sHoldId[side] = ward;
                door = -1;
                prop = -1;
            }
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
        } else if (sHold[side] == HOLD_LOOT) {
            loot_follow(sHoldId[side], at, right, up, fwd);
        } else if (sHold[side] == HOLD_PROP) {
            Placed *p = sHoldId[side] >= 0 && sHoldId[side] < e->room.placed.n ? &e->room.placed.p[sHoldId[side]] : NULL;

            if (p == NULL || !p->shown) {
                sHold[side] = HOLD_NONE;
            } else {
                p->pos = at;
                p->rot = sGrabRot[side];
                p->rot.y = sGrabRot[side].y + (atan2f(fwd.x, fwd.z) - sGrabYaw[side]);
            }
        } else if (sHold[side] == HOLD_WARD) {
            Placed *p = sHoldId[side] >= 0 && sHoldId[side] < e->room.placed.n ? &e->room.placed.p[sHoldId[side]] : NULL;

            if (p == NULL || !p->shown || !ward_leaf(p->name)) {
                sHold[side] = HOLD_NONE;   /* (gone: a room away from it, or put away) */
            } else {
                /* (the hiding may have played it since, or left its track to run: the leaf is
                 * taken again where it now stands, and then turned by the hand as before) */
                if (ward_retake(&e->room.placed, side, sHoldId[side], p, at)) {
                    ward_drive(&e->room.placed, side, sHoldId[side], p, at);
                }
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
                if (!push_step(d, sHandAt[side], at, sHandHave[side], &step, &sVel[i])) {
                    continue;
                }
                sPushNow[i] = 1;
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
        swing_hand(side, at);
        sHandAt[side] = at;
        sHandHave[side] = 1;
        sGripWas[side] = grip;
        if (sGrace[side] > 0) {
            sGrace[side]--;
        }
    }
    swing_finish();
    for (i = 0; i < ROOM_DOORS; i++) {
        int held = (sHold[0] == HOLD_DOOR && sHoldId[0] == i) || (sHold[1] == HOLD_DOOR && sHoldId[1] == i);
        float sw;

        /* the hand has left the leaf: a fast shove keeps its speed, as a gripped throw does */
        if (sPushNow[i]) {
            sPushWas[i] = 1;
            sPushNow[i] = 0;
        } else if (sPushWas[i] && !held) {
            sPushWas[i] = 0;
            sw = e->room.doors[i].swing;
            if (sw > -DOOR_LATCH && sw < DOOR_LATCH) {
                /* the shove already carried it onto the latch */
            } else if (sw <= -DOOR_WIDE || sw >= DOOR_WIDE) {
                /* against the open stop */
            } else if (sVel[i] <= -COAST_SPEED || sVel[i] >= COAST_SPEED) {
                int h;

                sCoast[i] = 1;
                sSettle[i] = 0;
                for (h = 0; h < 2; h++) {
                    if (sGrace[h] == 0 || sGraceDoor[h] == i) {
                        sGrace[h] = PUSH_GRACE;
                        sGraceDoor[h] = i;
                    }
                }
            }
        } else {
            sPushWas[i] = 0;
        }
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
    ward_tick(e);
    hide_probe(e);   /* (temporary: where she is against the leaf's bounds, and the leaf's frame) */
    poses_save(e);
    leaf_scan(e);
    loot_maintain();
    defense_tick();
}
