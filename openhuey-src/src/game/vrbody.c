/* Fiona's VR body: the body under the headset, the elbow's own IK and the forearm's twist helpers.
 *
 * This is a clean reimplementation of FRIK's documented behaviour - "FRIK - Full Immersive VR
 * Rigging for Fallout 4 VR" - https://github.com/rollingrock/Fallout4-VR-FRIK - whose rules
 * (getNeckYaw, setBodyUnderHMD, setBodyPosture, its shoulder's IK, its elbow's limit window and
 * its DampenHands) are named beside the numbers below - and of VRIK's head ("shrink" mode, its
 * lowering of the near plane) - https://github.com/prog011/VRIK. No code was copied from either:
 * FRIK is GPL, and none of its source was read; the formulas are the behaviour its documentation
 * and this project's notes describe.
 *
 * The unit conversion is FRIK's own design: its numbers are Fallout units, so a distance along the
 * arm is multiplied by kArm = L / 36.74 (L the arm's rest length, FRIK's default 36.74) and a body
 * offset by kBody = E / 120.48 (E her rest eye height, FRIK's default height 120.48). Both are
 * worked out once per model load, from the bind pose.
 *
 * Everything here is behind the arm IK's own condition (vrhands_wants + a shown Fiona + an anchor;
 * fiona_view drops the anchor in a cutscene, a scene and the free camera), so with the IK off - a
 * cutscene, non-VR play - not one matrix is touched: the `vr ...` helpers keep their rest under
 * their parents and the motions are exactly as they were. */
#include "vrbody.h"
#include "actor.h"
#include "vrbones.h"

#include "../platform/vr.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- FRIK's and VRIK's numbers, each with the name it has there ------------------------------
 * Distances are given in FRIK units: kArm multiplies one along the arm, kBody one of the body. */

/* FRIK's armLength and playerHeight, the defaults its numbers are in */
#define FRIK_ARM_LENGTH 36.74f
#define FRIK_PLAYER_HEIGHT 120.48f
/* her rest eyes above the head bone, when the model has no eye bones to measure them by */
#define EYE_ABOVE_HEAD 0.55f

/* 2. FRIK's getNeckYaw: the body's yaw from the head and the hands */
#define NECK_HAND_MIN 10.0f        /* a hand nearer the head than this: no yaw at all */
#define NECK_HAND_ABOVE 0.05f      /* a hand above the head takes this much off, a unit */
#define NECK_HAND_CROSS 0.02f      /* crossed hands add this much of the overlap back */
#define NECK_YAW_BODY 0.7f         /* the body turns this much of the neck's yaw */
#define NECK_YAW_LIMIT 50.0f       /* degrees */
#define NECK_PITCH_SAFE 80.0f      /* degrees: past it the yaw comes from the head's up */
#define NECK_KEEP 0.75f            /* a frame at 90 Hz keeps this much of the body's forward */
/* the chest's turn (her facing against the body's forward), bone by bone */
#define CHEST_WAIST 0.3f           /* the waist's share of the chest's turn */
#define CHEST_CHEST 0.4f           /* the chest's */
#define CHEST_NECK 0.3f            /* the neck's */

/* 3. FRIK's setBodyUnderHMD / setBodyPosture; VRIK's headInFront / headAbove */
#define BODY_HEAD_FRONT 3.0f       /* (FRIK's fPlayerBodyOffsetForwardStanding -3) */
#define BODY_LOOK_BACK 5.0f        /* a look up or down moves the body back this much */
#define BODY_LOOK_DOWN 6.0f        /* ... and the neck's target down this much */
#define BODY_PITCH_BASE 105.3f     /* degrees: FRIK's crouch bend */
#define BODY_PITCH_LOOK 0.1f       /* the look adds this much a degree */
#define BODY_PITCH_DIV 1.2f
#define BODY_COM_DROP 0.6666667f   /* (FRIK divides the COM's height by 1.5) */
#define SPINE_WAIST 0.45f          /* the swing's shares: the waist */
#define SPINE_CHEST 0.40f          /* the chest */
#define SPINE_NECK 0.15f           /* the neck */
#define SPINE_FORWARD 60.0f        /* degrees the swing is clamped to */
#define SPINE_BACK 25.0f
#define SPINE_SIDE 35.0f
#define BODY_KEEP 0.6f             /* a frame at 90 Hz keeps this much of the hip's move and the swing */
#define LEG_STRETCH 0.1f           /* a leg may reach its rest length plus this, no more */
#define SEATED_HMD_UP 31.0f        /* FRIK's seated offsets, in kBody units */
#define SEATED_BODY_UP -4.0f
#define SEATED_BODY_FRONT 2.0f
#define VR_SEATED 0                /* 1: the seated offsets always (the game's own seated option
                                    * asks for them too) */
#define VR_CROUCH_BEND 0           /* 1: FRIK's crouch bend too; 0 (here): her crouch posture is
                                    * the game's own crouch animation's (scripts/story/crouch.fs
                                    * scrubs it from vr.crouch-deadzone up), so a bend of ours
                                    * would fold her twice */

/* 4. FRIK's shoulder (clavicle) IK */
#define CLAV_REACH 0.15f           /* the far reach's share of the arm */
#define CLAV_DOWN 0.4f             /* a downward offset counts this much (glTF is Y up: -Y is down) */

/* 5. the arm: FRIK's elbow's limit window and the wrist's twist */
#define ARM_TOO_FAR 2.25f          /* times the arm: a tracker's glitch, no solve */
#define WRIST_K_FLOOR 0.45f
#define WRIST_K_TOP 0.8f
#define WRIST_KEEP 0.75f           /* a frame at 90 Hz keeps this much of the wrist's twist */
#define BEHIND_OFF 10.0f           /* a hand this far behind the shoulder is free */
#define BEHIND_SPAN 40.0f
#define CROSS_ANGLE 135.0f         /* the crossed-hands plane's normal, off forward, degrees */
#define CROSS_OFF 16.0f
#define CROSS_SPAN 20.0f
#define LIFT_SPAN 60.0f            /* the hand's height over the shoulder that lifts the elbow */
#define LIFT_GAIN 1.4f
#define ELBOW_MIN -85.0f           /* degrees */
#define ELBOW_BEHIND 50.0f
#define ELBOW_MAX 55.0f
#define ELBOW_CROSS_CUT 90.0f
#define ELBOW_UP_CUT 70.0f
#define ELBOW_KEEP 10.0f           /* the most is at least the least plus this */
#define ELBOW_SMOOTH 9.0f          /* degrees: the smooth max's band */
#define ELBOW_BEND_SIGN -1.0f      /* the elbow's tip from straight down (mirrored per side). -1: off a
                                    * headset that had the elbows folding INWARD across her body; this
                                    * way down leans out over the hand instead */
#define ELBOW_SWING_SIGN -1.0f     /* the vertical turn of the elbow's direction (mirrored too): down
                                    * and a little forward at rest, down and forward with the hands up */
#define ELBOW_OUTWARD 0.2f         /* the elbow's own side of her body, always: this much of the side is
                                    * in its direction whatever the wrist's twist does to the swing, so
                                    * no pose can fold an elbow into her chest */
#define ELBOW_ACROSS 16.0f         /* the distance past the body's middle that counts */
#define ELBOW_ACROSS_LOW 0.2f      /* ... and its share when the hand is low */
#define ELBOW_ARM_TWIST 0.25f
#define ELBOW_BEHIND_HEAD 15.0f
#define ELBOW_BEHIND_GAIN 1.2f
#define ELBOW_FWD_ACROSS 90.0f     /* degrees the elbow comes forward when the hand crosses */
#define ELBOW_FWD_BEHIND 120.0f    /* ... and when it is behind the head */
#define ELBOW_AIM_TWIST 150.0f     /* degrees the bend's direction is turned about up */
#define ELBOW_AIM_TWIST_CUT 25.0f
#define HAND_KEEP 0.7f             /* FRIK's DampenHands: a frame at 90 Hz keeps this much */
#define VR_DAMPEN_HANDS 1          /* 1: the body's hands are smoothed (DampenHands on) */

/* 6. the forearm's roll: FRIK's cumulative 0 / 0.5 / 1, split on this mesh's own bones */
#define TWIST_FORE 0.5f            /* the forearm and vr X forearm twist 1 */
#define TWIST_CUFF 0.8f            /* vr X forearm twist 2 (the PS2 split, not FRIK's 1.0) */
#define VR_TWIST_FRIK_CUFF 0       /* 1: the cuff takes all of it (FRIK's 1.0). Tried both: at a 170
                                    * degree wrist, 0.8 gives steps of 42/42/51/34 degrees along the
                                    * arm, 1.0 gives 42/42/85/0 - the same total with half of it
                                    * across the cuff alone, so 0.8 pinches less. */
#define TWIST_LIMIT 170.0f         /* degrees the wrist's twist is clamped to */
#define TWIST_KEEP 0.6f            /* a frame at 90 Hz keeps this much of it */
#define TWIST_UPPER 0.25f          /* the upper arm's share of it, FRIK's armUpperTwistSplit idea:
                                    * spread up the arm so no one joint takes the lot */
#define TWIST_UPPER_LIMIT 45.0f    /* degrees one bone may be rolled by */
#define TWIST_FORE_LIMIT 85.0f     /* (0.5 of the 170 clamp: no bite) */
#define TWIST_CUFF_LIMIT 140.0f    /* (0.8 of it is 136: no bite; 1.0 would be, hence 140) */

/* 7. the upper arm: FRIK's armUpperTwistSplit (it ships 0; we use 0.5) and the deltoid */
#define UPPER_TWIST 0.5f
#define DELTOID 0.5f               /* halfway between the clavicle's rest and the arm */

/* 8. the first-person head: VRIK's shrink, and its near plane */
#define VR_FP_NECK_SCALE 0.3f
#define VR_FP_HEAD_SCALE 0.001f
#define FP_NEAR_FROM 30.0f         /* degrees down: the near plane starts to come in */
#define FP_NEAR_TO 60.0f
#define FP_NEAR_SHARE 0.8f         /* ... to this share of it */

/* ---- the calibration (platform/vr.c: vr_calibrate, the VR options' Calibrate Height & Reach) ---- */
#define VR_UNITS_PER_METER 12.5f   /* (vr.c's and vrhands.c's own: the room's units for a metre) */
#define ARM_REACH_OF_HEIGHT 0.47f  /* before one is measured: a measured reach is about half the
                                    * eye height (0.86 m at 1.73 m); 0.40 left the arm short */
#define FIONA_HEIGHT_M 1.6f        /* her, as the game says she is (docs/game_design.md) */
#define BODY_SCALE_MIN 0.85f       /* her drawn body against the player's height, within limits */
#define BODY_SCALE_MAX 1.25f
#define ARM_SCALE_MIN 0.90f        /* the residual on her two arm bones, FRIK-style: a small share,
                                    * never a stretched arm */
#define ARM_SCALE_MAX 1.32f           /* (a headset at 1.74 m needed a shade under this: the hands
                                    * meet the controllers at full stretch, unpinned) */
#define MOVE_MIN 0.5f              /* her eye height over the player's: their motion in her sizes */
#define MOVE_MAX 1.15f
#define BODY_DROP_DEAD 0.04f       /* metres a head drop passes before the body follows it (a
                                    * relaxed stance is not a crouch, and standing tall is no bend) */
#define ARM_REACH_EPS 1.0e-3f      /* the hand may sit this far short at full stretch (a wrist gap) */
/* FRIK's shoulders: her arm starts where the player's does, not where her model's chest happens to
 * be - below the eyes, a little behind them, and out to each side, in the player's metres and along
 * her body's yaw (never the head's pitch). When a controller is further than the arm is long the
 * shoulder is turned toward it by the clavicle (its own share of the arm, CLAV_REACH), so the gap
 * is the shoulder's and never a stretched bone. The calibration measures the reach from this same
 * point, so the two agree. */
#define SH_UP_M 0.20f              /* the shoulder joint below the eyes */
#define SH_BACK_M 0.075f           /* ... behind them (toward the back of the head) */
#define SH_SIDE_M 0.18f            /* ... to each side */
#define SH_MAX_M 0.15f             /* the most the placed shoulder may differ from her own posed one */

/* the camera's seat in her head: the point of her head that goes to the camera is this far BEHIND
 * her eyes (toward the back of her head, along her forward) and this much HIGHER, in her metres
 * (units_per_m 12.5). The options fine-tune them (vr.cam-back, vr.cam-up, the VR Enhancements
 * page's Body: Camera Back / Camera Height); these are their defaults and the ends of the range. */
#define EYE_BACK_M 0.06f
#define EYE_UP_M 0.01f
#define EYE_BACK_MAX 0.12f
#define EYE_UP_RANGE 0.05f
#define HEAD_SEAT_MAX_M 0.30f    /* the most the head seat may move her head in a frame: it is what the
                                * view is read from, so it must never chase it */

#define DEG (0.017453292519943295f)

/* ---- what the body found, once a model ------------------------------------------------------ */

typedef struct VrBody {
    const Model *model;
    int bones, verts;
    char name[64];                         /* the model file: a costume change with the same counts */
    float scale;                           /* (the ratios are room units) */
    int ok;
    int root, pelvis, waist, chest, neck, head;
    int clav[2], upper[2], elbow[2], fore[2], hand[2];
    int thigh[2], shin[2], foot[2], toe[2];
    int twist1[2], twist2[2], upperTwist[2], deltoid[2], neckFp;
    int eyeBone;                           /* her eye bone, or -1: the head + EYE_ABOVE_HEAD */
    int haveFore, haveUpper, haveNeckFp;   /* the vr ... helpers that are there */
    float kArm, kBody;                     /* the ratios: room units a FRIK unit is */
    float armLen;                          /* |pos(30) - pos(27)|, rest, model units */
    float torso;                           /* pelvis -> neck, rest, model units */
    float arm1, arm2;                      /* |28 - 27| and |30 - 28|, rest, model units */
    float leg1[2], leg2[2];                /* |3 - 2| and |4 - 3|, rest, model units */
    float eyeFwd, eyeUp;                   /* the eyes from the neck at rest, room units: a look
                                            * swings them about the neck, which is not a crouch */
    /* the calibration: her own eye height and eye-to-wrist reach (room units) against the
     * player's, and what they make of the arm's scale and the body's move */
    float eyeWorld, reachRest;
    float bodyScale;                       /* her drawn skeleton against the player's height */
    float unitsPerM;                       /* the room's units for a metre of *the player* */
    float armScale, moveScale, dropDead;
} VrBody;

static VrBody sBones;

/* ---- this frame ---------------------------------------------------------------------------- */

static int sActive;             /* the IK is on: this body may touch the world */
static int sFrame;              /* begin's count: a hand or a twist a frame behind is taken anew */
static int sWasActive = -1000;
static float sDt = 1.0f / 90.0f;
static Uint64 sTick;
static Vec3 sEye;               /* her eyes: the anchor the hands and the headset sit on */
static Vec3 sHmd;               /* the headset, in the room */
static float sPitch;            /* the headset's pitch, positive looking down, radians */
static float sF[3];             /* the body's forward, room, unit, smoothed */
static float sFm[3];            /* ... and in model space */
static float sS[2][3];          /* the body's sideways toward each arm, room */
static float sKbodyM;           /* the body's ratio in model units (a->scale undone) */
static int sFHave;
/* FRIK's DampenHands: one hand's smoothed frame */
static int sHandSeen[2];
static Vec3 sHandPos[2];
static float sHandQuat[2][4];
/* the wrist's twist, smoothed, a side each */
static float sTwist[2];
static int sTwistHave[2];
/* the arms this pose solved (a hand the headset lost keeps the animation's arm: its helpers then
 * stay the rigid follow of their parents, as untouched as the arm) */
static int sArmOn[2];
/* the forearm's own roll last frame (its unwrap's reference and its smoothing), a side each */
static float sRoll[2];
static int sRollHave[2];
/* the body's own, smoothed */
static float sPelvisOff[3];
static int sPelvisHave;
static float sSwing[3];
static int sSwingHave;
/* the camera's read of her head: before the body and the first-person head move it */
static const struct Actor *sHeadWho;
static float sHeadY;
static float sHeadPos[2];   /* (her room place when that head was read: a room change is not hers) */
static int sHeadHave;
/* the signs, said once (the elbow's two turns and the legs' rules) */
static int sSignsLogged;
/* the shoulders' offset, said once */
static int sShouldersLogged;
static int sShoulderLogged[2];   /* (each side's own numbers, once) */
static int sShoulderWarned[2];   /* (the range warning, once a side) */
static int sHeadCamLogged;       /* (her eye against the camera, once) */
static int sHeadSeatWarned;      /* (a seat out of range, once) */
/* the calibration last logged (so the scale factors are said once, not a frame) */
static float sLoggedEye = -1.0f, sLoggedReach = -1.0f;

/* ---- vectors and matrices (the column-major Mat4 the rest of the game uses) ----------------- */

static void v_set(float *o, float x, float y, float z) { o[0] = x; o[1] = y; o[2] = z; }
static void v_copy3(float *o, const float *a) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; }
static void v_add3(float *o, const float *a, const float *b) { o[0] = a[0] + b[0]; o[1] = a[1] + b[1]; o[2] = a[2] + b[2]; }
static void v_sub3(float *o, const float *a, const float *b) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static float v_dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float v_len3(const float *a) { return sqrtf(v_dot3(a, a)); }
static void v_scale3(float *o, const float *a, float s) { o[0] = a[0] * s; o[1] = a[1] * s; o[2] = a[2] * s; }
static void v_cross3(float *o, const float *a, const float *b) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
/* 1 when it was longer than nothing (a zero vector is left as it is; its caller handles it) */
static int v_unit3(float *o, const float *a) {
    float n = v_len3(a);

    if (n < 1.0e-6f) {
        v_copy3(o, a);
        return 0;
    }
    v_scale3(o, a, 1.0f / n);
    return 1;
}
static float v_dist3(const float *a, const float *b) {
    float d[3];

    v_sub3(d, a, b);
    return v_len3(d);
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float wrap_pi(float a) {
    while (a > 3.14159265f) {
        a -= 6.28318531f;
    }
    while (a < -3.14159265f) {
        a += 6.28318531f;
    }
    return a;
}
/* a column of a matrix, made unit (a bone's scale left out) */
static void m_col_unit(const Mat4 *m, int c, float out[3]) {
    float v[3] = {m->m[c * 4], m->m[c * 4 + 1], m->m[c * 4 + 2]};

    if (!v_unit3(out, v)) {
        out[0] = c == 0 ? 1.0f : 0.0f;
        out[1] = c == 1 ? 1.0f : 0.0f;
        out[2] = c == 2 ? 1.0f : 0.0f;
    }
}
static void m_pos(const Mat4 *m, float out[3]) {
    out[0] = m->m[12];
    out[1] = m->m[13];
    out[2] = m->m[14];
}
/* a rotation about `axis` by `ang`, about the point `pivot` (column-major, as Mat4 is) */
static void rot_about(const float *axis, float ang, const float *pivot, Mat4 *r) {
    float a[3], c, s, t;

    memset(r, 0, sizeof(*r));
    r->m[15] = 1.0f;
    if (!v_unit3(a, axis)) {
        r->m[0] = r->m[5] = r->m[10] = 1.0f;
        return;
    }
    c = cosf(ang);
    s = sinf(ang);
    t = 1.0f - c;
    r->m[0] = t * a[0] * a[0] + c;
    r->m[1] = t * a[0] * a[1] + s * a[2];
    r->m[2] = t * a[0] * a[2] - s * a[1];
    r->m[4] = t * a[0] * a[1] - s * a[2];
    r->m[5] = t * a[1] * a[1] + c;
    r->m[6] = t * a[1] * a[2] + s * a[0];
    r->m[8] = t * a[0] * a[2] + s * a[1];
    r->m[9] = t * a[1] * a[2] - s * a[0];
    r->m[10] = t * a[2] * a[2] + c;
    r->m[12] = pivot[0] - (r->m[0] * pivot[0] + r->m[4] * pivot[1] + r->m[8] * pivot[2]);
    r->m[13] = pivot[1] - (r->m[1] * pivot[0] + r->m[5] * pivot[1] + r->m[9] * pivot[2]);
    r->m[14] = pivot[2] - (r->m[2] * pivot[0] + r->m[6] * pivot[1] + r->m[10] * pivot[2]);
}
/* a vector turned about `axis` by `ang` (Rodrigues) */
static void v_turn(const float *v, const float *axis, float ang, float out[3]) {
    float a[3], c, s, t, cr[3], d;

    if (!v_unit3(a, axis)) {
        v_copy3(out, v);
        return;
    }
    c = cosf(ang);
    s = sinf(ang);
    t = 1.0f - c;
    v_cross3(cr, a, v);
    d = v_dot3(a, v) * t;
    out[0] = v[0] * c + cr[0] * s + a[0] * d;
    out[1] = v[1] * c + cr[1] * s + a[1] * d;
    out[2] = v[2] * c + cr[2] * s + a[2] * d;
}
/* a rigid inverse: a rotation and a place, no scale (the inverse binds are rigid) */
static Mat4 rigid_inv(const Mat4 *m) {
    Mat4 r = mat4_identity();

    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2] = m->m[8];
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6] = m->m[9];
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10];
    r.m[12] = -(r.m[0] * m->m[12] + r.m[4] * m->m[13] + r.m[8] * m->m[14]);
    r.m[13] = -(r.m[1] * m->m[12] + r.m[5] * m->m[13] + r.m[9] * m->m[14]);
    r.m[14] = -(r.m[2] * m->m[12] + r.m[6] * m->m[13] + r.m[10] * m->m[14]);
    return r;
}
/* a bone under `root` (its parent chain; a bone is not its own ancestor) */
static int under(const Model *m, int bone, int root) {
    int n = 0;

    if (bone < 0 || bone >= m->nbones || root < 0) {
        return 0;
    }
    for (bone = m->bones[bone].parent; bone >= 0 && n < MODEL_MAX_BONES; bone = m->bones[bone].parent, n++) {
        if (bone == root) {
            return 1;
        }
    }
    return 0;
}
/* everything `bone` carries - it and its descendants - turned by r (r * world) */
static void turn_tree(Mat4 *world, const Model *m, int bone, const Mat4 *r) {
    int i;

    if (bone < 0 || bone >= m->nbones) {
        return;
    }
    for (i = 0; i < m->nbones; i++) {
        if (i == bone || under(m, i, bone)) {
            world[i] = mat4_mul(*r, world[i]);
        }
    }
}
/* the same, moved by `d` (their turns left as they are) */
static void shift_tree(Mat4 *world, const Model *m, int bone, const float *d) {
    int i;

    if (bone < 0 || bone >= m->nbones) {
        return;
    }
    for (i = 0; i < m->nbones; i++) {
        if (i == bone || under(m, i, bone)) {
            world[i].m[12] += d[0];
            world[i].m[13] += d[1];
            world[i].m[14] += d[2];
        }
    }
}
/* The whole body moved by `d` - every bone under the root, the root itself left where it is. Her
 * model's bone 0 is at the hips and bone 1 (the pelvis) is only the legs' parent: the spine hangs
 * from the root beside it, so "the pelvis and its descendants" is the root's whole subtree. */
static void shift_body(Mat4 *world, const Model *m, int root, const float *d) {
    int i;

    for (i = 0; i < m->nbones; i++) {
        if (i != root && under(m, i, root)) {
            world[i].m[12] += d[0];
            world[i].m[13] += d[1];
            world[i].m[14] += d[2];
        }
    }
}
/* the rigid follow of `i` under its `parent` in the current pose:
 * world[parent] * inv_bind[parent] * rigid_inverse(&inv_bind[i]) */
static Mat4 follow(const Mat4 *world, const Model *m, int parent, int i) {
    Mat4 inv = rigid_inv(&m->bones[i].inv_bind);

    return mat4_mul(mat4_mul(world[parent], m->bones[parent].inv_bind), inv);
}

/* ---- rotations as quaternions (x, y, z, w) -------------------------------------------------- */

static void q_mul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static void q_normal(float q[4]) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

    if (n < 1.0e-8f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    q[0] /= n;
    q[1] /= n;
    q[2] /= n;
    q[3] /= n;
}
static void q_conj(const float q[4], float o[4]) {
    o[0] = -q[0];
    o[1] = -q[1];
    o[2] = -q[2];
    o[3] = q[3];
}
/* the rotation part of a Mat4, its columns made unit, as a quaternion */
static void q_of_mat(const Mat4 *m, float q[4]) {
    float x[3], y[3], z[3], r00, r11, r22, trace;

    m_col_unit(m, 0, x);
    m_col_unit(m, 1, y);
    m_col_unit(m, 2, z);
    r00 = x[0];
    r11 = y[1];
    r22 = z[2];
    trace = r00 + r11 + r22;
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;

        q[3] = 0.25f * s;
        q[0] = (y[2] - z[1]) / s;
        q[1] = (z[0] - x[2]) / s;
        q[2] = (x[1] - y[0]) / s;
    } else if (r00 > r11 && r00 > r22) {
        float s = sqrtf(1.0f + r00 - r11 - r22) * 2.0f;

        q[3] = (y[2] - z[1]) / s;
        q[0] = 0.25f * s;
        q[1] = (y[0] + x[1]) / s;
        q[2] = (z[0] + x[2]) / s;
    } else if (r11 > r22) {
        float s = sqrtf(1.0f + r11 - r00 - r22) * 2.0f;

        q[3] = (z[0] - x[2]) / s;
        q[0] = (y[0] + x[1]) / s;
        q[1] = 0.25f * s;
        q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1.0f + r22 - r00 - r11) * 2.0f;

        q[3] = (x[1] - y[0]) / s;
        q[0] = (z[0] + x[2]) / s;
        q[1] = (z[1] + y[2]) / s;
        q[2] = 0.25f * s;
    }
    q_normal(q);
}
/* a quaternion's rotation as a Mat4 about `pivot` */
static void q_about(const float q[4], const float *pivot, Mat4 *r) {
    float a[4] = {q[0], q[1], q[2], q[3]}, axis[3], ang;

    q_normal(a);
    ang = 2.0f * acosf(clampf(a[3], -1.0f, 1.0f));
    v_set(axis, a[0], a[1], a[2]);
    rot_about(axis, ang, pivot, r);
}
/* the shortest turn from a to b, as a quaternion (the identity when either is nothing) */
static void q_between(const float *a, const float *b, float q[4]) {
    float ua[3], ub[3], cr[3], d;

    q[0] = q[1] = q[2] = 0.0f;
    q[3] = 1.0f;
    if (!v_unit3(ua, a) || !v_unit3(ub, b)) {
        return;
    }
    d = clampf(v_dot3(ua, ub), -1.0f, 1.0f);
    v_cross3(cr, ua, ub);
    q[0] = cr[0];
    q[1] = cr[1];
    q[2] = cr[2];
    q[3] = 1.0f + d;
    q_normal(q);
}
/* the shortest-arc slerp, t 0..1 */
static void q_slerp(const float a[4], const float b[4], float t, float o[4]) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float bb[4] = {b[0], b[1], b[2], b[3]}, k0, k1, ang;

    if (d < 0.0f) {
        d = -d;
        bb[0] = -bb[0];
        bb[1] = -bb[1];
        bb[2] = -bb[2];
        bb[3] = -bb[3];
    }
    if (d > 0.9995f) {
        o[0] = a[0] + (bb[0] - a[0]) * t;
        o[1] = a[1] + (bb[1] - a[1]) * t;
        o[2] = a[2] + (bb[2] - a[2]) * t;
        o[3] = a[3] + (bb[3] - a[3]) * t;
        q_normal(o);
        return;
    }
    ang = acosf(clampf(d, -1.0f, 1.0f));
    k0 = sinf((1.0f - t) * ang) / sinf(ang);
    k1 = sinf(t * ang) / sinf(ang);
    o[0] = a[0] * k0 + bb[0] * k1;
    o[1] = a[1] * k0 + bb[1] * k1;
    o[2] = a[2] * k0 + bb[2] * k1;
    o[3] = a[3] * k0 + bb[3] * k1;
}
/* a hand's rotation from the axes grip_matrix is built from: its forward and the back of the hand */
static void q_of_hand(Vec3 fwd, Vec3 up, float q[4]) {
    Vec3 x = vec3_norm(fwd), z = vec3_norm(up), y = vec3_norm(vec3_cross(z, x));
    Mat4 m = mat4_identity();

    z = vec3_norm(vec3_cross(x, y));
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    q_of_mat(&m, q);
}
/* ... and the two axes it is built from, back */
static void hand_of_q(const float q[4], Vec3 *fwd, Vec3 *up) {
    Mat4 m;
    float zero[3] = {0.0f, 0.0f, 0.0f}, x[3], z[3];

    q_about(q, zero, &m);
    m_col_unit(&m, 0, x);
    m_col_unit(&m, 2, z);
    *fwd = vec3(x[0], x[1], x[2]);
    *up = vec3(z[0], z[1], z[2]);
}
/* how much of a frame at 90 Hz's retention one of this frame keeps */
static float keep90(float retention) {
    return powf(retention, sDt * 90.0f);
}
/* FRIK's smooth max: max(a, b) with a band, so nothing steps */
static float smooth_max(float a, float b, float band) {
    float d = a - b;

    return 0.5f * (a + b + sqrtf(d * d + band * band));
}
/* The game's own crouch, as scripts/story/crouch.fs reads it: below vr.crouch-deadzone a drop is no
 * crouch at all; from there to vr.crouch-on its crouch animation scrubs 0..1 (and the hide flag
 * comes on). That system owns her crouch posture, so vrbody must not bend her again for the same
 * drop: this is the share it is already animating (0 for a look's few centimetres). */
static float crouch_excess(float frac) {
    float lo = gVrOptions.crouch_deadzone, hi = gVrOptions.crouch_on;

    if (!(frac > lo)) {
        return 0.0f;
    }
    if (hi <= lo + 1.0e-4f) {
        return 1.0f;
    }
    return clampf((frac - lo) / (hi - lo), 0.0f, 1.0f);
}

/* ---- the bones by name, once a model -------------------------------------------------------- */

static void bind_pos(const Model *m, int bone, float out[3]) {
    Mat4 b = rigid_inv(&m->bones[bone].inv_bind);

    out[0] = b.m[12];
    out[1] = b.m[13];
    out[2] = b.m[14];
}

static void cache_bones(const struct Actor *a) {
    const Model *m = &a->model;
    const VrBones *vb;
    /* the roles the body asks for, by side; the game bones are the outfit's own (vrbones.h) */
    static const int kClavRole[2] = {VRB_CLAV_L, VRB_CLAV_R};
    static const int kUpperRole[2] = {VRB_UPPER_L, VRB_UPPER_R};
    static const int kElbowRole[2] = {VRB_ELBOW_L, VRB_ELBOW_R};
    static const int kForeRole[2] = {VRB_FORE_L, VRB_FORE_R};
    static const int kHandRole[2] = {VRB_WRIST_L, VRB_WRIST_R};
    static const int kThighRole[2] = {VRB_THIGH_L, VRB_THIGH_R};
    static const int kShinRole[2] = {VRB_SHIN_L, VRB_SHIN_R};
    static const int kFootRole[2] = {VRB_FOOT_L, VRB_FOOT_R};
    static const int kToeRole[2] = {VRB_TOE_L, VRB_TOE_R};
    static const char *const kTwist1[2] = {"vr L forearm twist 1", "vr R forearm twist 1"};
    static const char *const kTwist2[2] = {"vr L forearm twist 2", "vr R forearm twist 2"};
    static const char *const kUpTwist[2] = {"vr L upperarm twist", "vr R upperarm twist"};
    static const char *const kDelt[2] = {"vr L shoulder", "vr R shoulder"};
    float scale = a->scale != 0.0f ? a->scale : 1.0f;
    float floor_y = 0.0f, p0[3], p1[3], eye;
    int side, ok, eyes, eye2;

    if (sBones.model == m && sBones.bones == m->nbones && sBones.verts == m->nv &&
        sBones.scale == scale && strcmp(sBones.name, a->name) == 0) {
        return;
    }
    memset(&sBones, 0, sizeof(sBones));
    sBones.model = m;
    sBones.bones = m->nbones;
    sBones.verts = m->nv;
    sBones.scale = scale;
    snprintf(sBones.name, sizeof(sBones.name), "%s", a->name);
    vb = vrbones_for(a);
    if (!vb->ok) {
        /* no table for this outfit: the body (and the arms' IK) are left off, as in a cutscene */
        fprintf(stderr, "vrbody: no VR bones for %s: no VR body\n", a->name);
        return;
    }
    sBones.root = vb->bone[VRB_ROOT];
    sBones.pelvis = vb->bone[VRB_PELVIS];
    sBones.waist = vb->bone[VRB_WAIST];
    sBones.chest = vb->bone[VRB_CHEST];
    sBones.neck = vb->bone[VRB_NECK];
    sBones.head = vb->bone[VRB_HEAD];
    floor_y = vb->floor_y;
    for (side = 0; side < 2; side++) {
        sBones.upper[side] = vb->bone[kUpperRole[side]];
        sBones.elbow[side] = vb->bone[kElbowRole[side]];
        sBones.hand[side] = vb->bone[kHandRole[side]];
        sBones.clav[side] = vb->bone[kClavRole[side]];
        sBones.fore[side] = vb->bone[kForeRole[side]];
        sBones.thigh[side] = vb->bone[kThighRole[side]];
        sBones.shin[side] = vb->bone[kShinRole[side]];
        sBones.foot[side] = vb->bone[kFootRole[side]];
        sBones.toe[side] = vb->bone[kToeRole[side]];
        sBones.twist1[side] = model_bone_named(m, kTwist1[side]);
        sBones.twist2[side] = model_bone_named(m, kTwist2[side]);
        sBones.upperTwist[side] = model_bone_named(m, kUpTwist[side]);
        sBones.deltoid[side] = model_bone_named(m, kDelt[side]);
    }
    sBones.neckFp = model_bone_named(m, "vr neck fp");
    sBones.haveUpper = sBones.upperTwist[0] >= 0 && sBones.upperTwist[1] >= 0 &&
                       sBones.deltoid[0] >= 0 && sBones.deltoid[1] >= 0;
    sBones.haveFore = sBones.twist1[0] >= 0 && sBones.twist1[1] >= 0 &&
                      sBones.twist2[0] >= 0 && sBones.twist2[1] >= 0;
    sBones.haveNeckFp = sBones.neckFp >= 0;
    ok = sBones.root >= 0 && sBones.pelvis >= 0 && sBones.waist >= 0 && sBones.chest >= 0 &&
         sBones.neck >= 0 && sBones.head >= 0;
    for (side = 0; side < 2; side++) {
        if (sBones.upper[side] < 0 || sBones.elbow[side] < 0 || sBones.hand[side] < 0 ||
            sBones.clav[side] < 0 || sBones.fore[side] < 0 || sBones.thigh[side] < 0 ||
            sBones.shin[side] < 0 || sBones.foot[side] < 0) {
            ok = 0;
        }
    }
    if (!ok) {
        fprintf(stderr, "vrbody: a bone of hers is missing by name: no VR body\n");
        return;
    }
    /* the lengths the bind pose has, in model units: exactly what the motions were checked on */
    bind_pos(m, sBones.hand[0], p0);
    bind_pos(m, sBones.upper[0], p1);
    sBones.armLen = v_dist3(p0, p1);
    bind_pos(m, sBones.upper[0], p0);
    bind_pos(m, sBones.elbow[0], p1);
    sBones.arm1 = v_dist3(p0, p1);
    bind_pos(m, sBones.elbow[0], p0);
    bind_pos(m, sBones.hand[0], p1);
    sBones.arm2 = v_dist3(p0, p1);
    bind_pos(m, sBones.pelvis, p0);
    bind_pos(m, sBones.neck, p1);
    sBones.torso = v_dist3(p0, p1);
    for (side = 0; side < 2; side++) {
        bind_pos(m, sBones.thigh[side], p0);
        bind_pos(m, sBones.shin[side], p1);
        sBones.leg1[side] = v_dist3(p0, p1);
        bind_pos(m, sBones.shin[side], p0);
        bind_pos(m, sBones.foot[side], p1);
        sBones.leg2[side] = v_dist3(p0, p1);
    }
    /* her rest eye height above the model's floor (floor_y: in FIS and FIC bone 0 binds at the hip,
     * not at the floor, so bone 0's bind y is not the floor): the eye bones' own, else the head's
     * + a little */
    eyes = vb->bone[VRB_EYE_L];
    sBones.eyeBone = eyes;
    eye2 = vb->bone[VRB_EYE_R];
    if (eyes >= 0 && eye2 >= 0) {
        bind_pos(m, eyes, p1);
        eye = p1[1];
        bind_pos(m, eye2, p1);
        eye = 0.5f * (eye + p1[1]);
    } else {
        bind_pos(m, sBones.head, p1);
        eye = p1[1] + EYE_ABOVE_HEAD;
    }
    sBones.kArm = sBones.armLen * scale / FRIK_ARM_LENGTH;
    sBones.kBody = (eye - floor_y) * scale / FRIK_PLAYER_HEIGHT;
    /* how far her eyes sit from her neck at rest: a look up or down swings them about the neck by
     * this, and that swing must not be read as the body's own move (it would crouch her) */
    {
        float neck[3], e[3], hand[3];
        int b = eyes >= 0 ? eyes : sBones.head;

        bind_pos(m, sBones.neck, neck);
        bind_pos(m, b, e);
        if (eyes < 0) {
            e[1] += EYE_ABOVE_HEAD;
        }
        sBones.eyeFwd = (e[2] - neck[2]) * scale;
        sBones.eyeUp = (e[1] - neck[1]) * scale;
        /* and her own sizes the calibration is against: her eyes above the floor, and the eye to
         * the wrist with the arm out (her full reach, the T-pose the bind has) */
        sBones.eyeWorld = (e[1] - floor_y) * scale;
        bind_pos(m, sBones.hand[0], hand);
        sBones.reachRest = v_dist3(e, hand) * scale;
    }
    sBones.ok = 1;
    fprintf(stderr,
            "vrbody: bones by name - pelvis %d waist %d chest %d neck %d head %d; arms %d/%d %d/%d %d/%d;"
            " clavicles %d/%d; legs %d/%d %d/%d %d/%d %d/%d\n",
            sBones.pelvis, sBones.waist, sBones.chest, sBones.neck, sBones.head,
            sBones.upper[0], sBones.upper[1], sBones.elbow[0], sBones.elbow[1],
            sBones.hand[0], sBones.hand[1], sBones.clav[0], sBones.clav[1],
            sBones.thigh[0], sBones.thigh[1], sBones.shin[0], sBones.shin[1],
            sBones.foot[0], sBones.foot[1], sBones.toe[0], sBones.toe[1]);
    fprintf(stderr,
            "vrbody: vr helpers - twists %d/%d %d/%d; upper %d/%d; deltoids %d/%d; neck fp %d;"
            " kArm %.4f kBody %.4f; steps: yaw, posture+feet, clavicle, arms, forearm%s, upper%s, head%s\n",
            sBones.twist1[0], sBones.twist1[1], sBones.twist2[0], sBones.twist2[1],
            sBones.upperTwist[0], sBones.upperTwist[1], sBones.deltoid[0], sBones.deltoid[1], sBones.neckFp,
            sBones.kArm, sBones.kBody,
            sBones.haveFore ? "+cuff" : " (no helpers: skipped)",
            sBones.haveUpper ? "" : " (no helpers: skipped)",
            sBones.haveNeckFp ? "" : " (no helper: skipped)");
}

/* The calibration's numbers for the body, FRIK's way. Her *drawn* body is scaled to the player's
 * height (a uniform scale of the posed skeleton alone - her actor's scale, her collision, the
 * world, the camera and everything gameplay reads stay exactly as they were), and her two arm
 * bones take a small residual so her full reach is the player's. Nothing is stretched past
 * ARM_SCALE_MIN..MAX, and the hand is always the controller's: the target is the controller, never
 * a moved hand. Before a reach is measured the eye height sizes it. Logged once per change. */
static void body_scales(const struct Actor *a) {
    float eye_m = 1.6f, world = 1.0f, reach_m, player_reach, player_arm, fiona_arm, units_per_m;

    vr_body(&eye_m, &world);
    if (!(eye_m > 0.5f) || eye_m > 3.0f) {
        eye_m = 1.6f;
    }
    if (!(world > 0.2f)) {
        world = 1.0f;
    }
    /* Her drawn body is NOT scaled: the height calibration already sets the VR world's scale
     * (vr.c's 1.6 / the player's height), so scaling her again corrected her twice and buried her
     * head and chest under the camera the taller the player was. One scale, the world's: her body
     * is her own size, her head is put at the camera (vrbody_head_camera), and only her arms take a
     * share. */
    sBones.bodyScale = 1.0f;
    reach_m = gVrOptions.arm_reach;
    if (!(reach_m > 0.15f)) {
        reach_m = ARM_REACH_OF_HEIGHT * eye_m;
    }
    /* Every offset and threshold of the body's is in HER space: ONE constant metre's units
     * (VR_UNITS_PER_METER, the value a 1.6 m calibration gives), never one that moves with the
     * player's height. The world's scale has already mapped the player onto her, so anything given
     * in metres and converted per height would be corrected twice - the taller the player, the
     * more wrong (a shoulder an extra 0.02 m low at 1.74 m, a body move of 0.67 instead of 0.78).
     * With this constant every height looks exactly like the 1.6 m calibration. */
    units_per_m = VR_UNITS_PER_METER;
    sBones.unitsPerM = units_per_m;
    /* The calibrated reach, into her space: the world's scale (1.6 / the player's height) takes
     * the player's own size into her metres first, so a taller player's longer reach still lands
     * at her arm (a 1.74 m player's 0.86 m is 0.79 m of hers). */
    player_reach = reach_m * world * VR_UNITS_PER_METER;
    /* The reach is measured from the headset to the controller, which is the eyes to the hand: the
     * arm itself starts at the shoulder *the body places* (vrbody_shoulders), so the same offset
     * comes off it - |up, back, side|, in her metres - and the two agree. Her arm is her own
     * shoulder to wrist. Their ratio is the share her two arm bones take. */
    player_arm = player_reach -
                 sqrtf(SH_UP_M * SH_UP_M + SH_BACK_M * SH_BACK_M + SH_SIDE_M * SH_SIDE_M) * units_per_m;
    fiona_arm = (sBones.arm1 + sBones.arm2) * sBones.bodyScale;
    sBones.armScale = clampf(player_arm / (fiona_arm > 0.05f ? fiona_arm : 1.0f),
                             ARM_SCALE_MIN, ARM_SCALE_MAX);
    /* and the body's move: her eye height over *her* model's (1.6 m), a constant 0.78 */
    sBones.moveScale = clampf(sBones.eyeWorld / (FIONA_HEIGHT_M * VR_UNITS_PER_METER),
                              MOVE_MIN, MOVE_MAX);
    sBones.dropDead = BODY_DROP_DEAD * units_per_m;
    (void)a;
    if (gVrOptions.eye_height != sLoggedEye || gVrOptions.arm_reach != sLoggedReach) {
        sLoggedEye = gVrOptions.eye_height;
        sLoggedReach = gVrOptions.arm_reach;
        fprintf(stderr,
                "vrbody: calibration - eyes %.2f m, reach %.2f m%s; her body scaled %.3f (the player's"
                " height); fiona_arm_len %.3f, player_arm_len %.3f (room units), reach_scale %.3f"
                " (clamped %.2f..%.2f); units_per_m %.3f; body move %.3f\n",
                (double)eye_m, (double)reach_m, gVrOptions.arm_reach > 0.0f ? " (measured)" : " (from the height)",
                (double)sBones.bodyScale, (double)fiona_arm, (double)player_arm,
                (double)sBones.armScale, (double)ARM_SCALE_MIN, (double)ARM_SCALE_MAX, (double)units_per_m,
                (double)sBones.moveScale);
        fprintf(stderr, "vrbody: her actor's scale is %.3f (untouched: the body scale is the drawn pose"
                " only - collision, hitbox, movement and the world are hers)\n", (double)a->scale);
    }
}

float vrbody_body_scale(const struct Actor *a) {
    if (a == NULL) {
        return 1.0f;
    }
    cache_bones(a);
    if (!sBones.ok) {
        return 1.0f;
    }
    body_scales(a);
    return sBones.bodyScale;
}

float vrbody_arm_scale(const struct Actor *a) {
    if (a == NULL) {
        return 1.0f;
    }
    cache_bones(a);
    if (!sBones.ok) {
        return 1.0f;
    }
    body_scales(a);
    /* the whole multiplier from her rest model bone to the drawn, room-space one: the body's
     * uniform scale and the arm's residual share of it */
    return sBones.bodyScale * sBones.armScale;
}

int vrbody_bone(int part, int side) {
    if (side < 0 || side > 1) {
        side = 0;
    }
    switch (part) {
    case VRBODY_ROOT: return sBones.root;
    case VRBODY_PELVIS: return sBones.pelvis;
    case VRBODY_WAIST: return sBones.waist;
    case VRBODY_CHEST: return sBones.chest;
    case VRBODY_NECK: return sBones.neck;
    case VRBODY_HEAD: return sBones.head;
    case VRBODY_CLAV: return sBones.clav[side];
    case VRBODY_UPPER: return sBones.upper[side];
    case VRBODY_ELBOW: return sBones.elbow[side];
    case VRBODY_FORE: return sBones.fore[side];
    case VRBODY_HAND: return sBones.hand[side];
    case VRBODY_TWIST1: return sBones.twist1[side];
    case VRBODY_TWIST2: return sBones.twist2[side];
    case VRBODY_UPPER_TWIST: return sBones.upperTwist[side];
    case VRBODY_DELTOID: return sBones.deltoid[side];
    case VRBODY_NECK_FP: return sBones.neckFp;
    case VRBODY_THIGH: return sBones.thigh[side];
    case VRBODY_SHIN: return sBones.shin[side];
    case VRBODY_FOOT: return sBones.foot[side];
    case VRBODY_TOE: return sBones.toe[side];
    default: return -1;
    }
}

float vrbody_karm(void) { return sBones.kArm; }
float vrbody_kbody(void) { return sBones.kBody; }

/* ---- this pose's setup ---------------------------------------------------------------------- */

static void play_point(Vec3 eye, float yaw, Vec3 p, float out[3]) {
    float sy = sinf(yaw), cy = cosf(yaw);

    out[0] = eye.x + cy * p.x - sy * p.z;
    out[1] = eye.y + p.y;
    out[2] = eye.z + sy * p.x + cy * p.z;
}
static void play_dir(float yaw, const Vec3 v, float out[3]) {
    float sy = sinf(yaw), cy = cosf(yaw);

    out[0] = cy * v.x - sy * v.z;
    out[1] = v.y;
    out[2] = sy * v.x + cy * v.z;
}
/* a direction made level, unit; 0 when it pointed straight up or down (out left nothing) */
static int flat_dir(const float *v, float out[3]) {
    float h[3] = {v[0], 0.0f, v[2]};

    return v_unit3(out, h);
}
/* one hand in the room. 0: that controller is not tracked this frame */
static int hand_room(int side, Vec3 eye, float yaw, float out[3]) {
    VrController c;

    if (!vr_controller(side, &c) || !c.tracked) {
        return 0;
    }
    play_point(eye, yaw, c.at, out);
    return 1;
}

/* FRIK's getNeckYaw, then the body's forward. 1: the body is on. */
int vrbody_begin(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    Vec3 eye, f0, u0, c0;
    float hmd[3], fh[3], base[3], right[3], sum[3], d[3], want[3], side[3], lat, w;
    float yaw, pitch = 0.0f, neck = 0.0f, keep, scale = a->scale != 0.0f ? a->scale : 1.0f;
    float hand[2][3];
    Uint64 now;
    int i, have[2], fresh;

    sActive = 0;
    /* sHeadHave is NOT dropped: the camera reads her head's own y (vrbody_eye_y) and the seat
     * below moves that bone, so forgetting it would let the view read the seated head back and
     * climb, frame after frame - after a cutscene, most visibly. It is the frame's own pre-body
     * head each time vrbody_body runs, and only vrbody_idle of another actor leaves it stale.
     */
    sArmOn[0] = sArmOn[1] = 0;
    sFrame++;
    now = SDL_GetTicksNS();
    if (sTick != 0) {
        sDt = (float)(now - sTick) / 1.0e9f;
        sDt = clampf(sDt, 1.0f / 240.0f, 0.1f);
    }
    sTick = now;
    cache_bones(a);
    if (!sBones.ok || world == NULL || place == NULL || unplace == NULL) {
        return 0;
    }
    if (!vr_anchor_get(&eye, &yaw) || !vr_head_at(&sHmd) || !vr_head_look(&pitch, &f0, &u0)) {
        return 0;
    }
    sEye = eye;
    sPitch = pitch;
    sKbodyM = sBones.kBody / scale;
    body_scales(a);

    /* the headset's forward, level, in the room; a straight-down look has none: the head's up
     * (VRIK's pitch-safe heading), else the body's last forward */
    c0 = vec3(f0.x, f0.y, f0.z);
    play_dir(yaw, c0, fh);
    if (!flat_dir(fh, fh)) {
        c0 = vec3(u0.x, u0.y, u0.z);
        play_dir(yaw, c0, base);
        if (!flat_dir(base, fh) && sFHave) {
            v_copy3(fh, sF);
        }
        if (v_len3(fh) < 0.5f) {
            v_set(fh, 0.0f, 0.0f, 1.0f);
        }
    }
    v_copy3(hmd, &sHmd.x);
    have[0] = hand_room(0, eye, yaw, hand[0]);
    have[1] = hand_room(1, eye, yaw, hand[1]);

    /* 1. a hand nearer the head than 10 kArm: no yaw at all. 2. a hand overhead takes some away.
     * 3. crossed hands (the left to the right of the right) give some back. */
    if (have[0] && have[1] &&
        v_dist3(hand[0], hmd) >= NECK_HAND_MIN * sBones.kArm &&
        v_dist3(hand[1], hmd) >= NECK_HAND_MIN * sBones.kArm) {
        w = 1.0f;
        for (i = 0; i < 2; i++) {
            if (hand[i][1] > hmd[1]) {
                w -= NECK_HAND_ABOVE / sBones.kArm * (hand[i][1] - hmd[1]);
            }
        }
        w = w > 0.0f ? w : 0.0f;
        v_cross3(right, fh, (const float[3]){0.0f, 1.0f, 0.0f});
        v_sub3(d, hand[0], hmd);
        lat = v_dot3(d, right);
        v_sub3(d, hand[1], hmd);
        lat -= v_dot3(d, right);
        if (lat > 0.0f) {
            w += NECK_HAND_CROSS / sBones.kArm * lat;
        }
        w = w > 0.0f ? w : 0.0f;
        /* 4. the way the hands sum points, from the head's forward: the neck's yaw */
        v_sub3(sum, hand[0], hmd);
        v_sub3(d, hand[1], hmd);
        v_add3(sum, sum, d);
        v_set(sum, sum[0], 0.0f, sum[2]);
        if (v_len3(sum) > 1.0e-4f) {
            float sgn;
            float cr[3];

            v_unit3(sum, sum);
            v_copy3(base, fh);
            if (fabsf(sPitch) > NECK_PITCH_SAFE * DEG) {
                c0 = vec3(u0.x, u0.y, u0.z);
                play_dir(yaw, c0, d);
                if (flat_dir(d, base) && v_dot3(base, fh) < 0.0f) {
                    v_scale3(base, base, -1.0f);
                }
            }
            v_cross3(cr, base, sum);
            sgn = atan2f(cr[1], v_dot3(base, sum));
            neck = clampf(sgn * w, -NECK_YAW_LIMIT * DEG, NECK_YAW_LIMIT * DEG);
        }
    }

    /* 5. the body's forward: the head's, turned 0.7 of the neck's yaw, eased in */
    v_turn(fh, (const float[3]){0.0f, 1.0f, 0.0f}, NECK_YAW_BODY * neck, want);
    fresh = !sFHave || sFrame - sWasActive > 2;
    if (fresh) {
        v_copy3(sF, want);
        sFHave = 1;
        sPelvisHave = 0;
        sSwingHave = 0;
        sHandSeen[0] = sHandSeen[1] = 0;
        sTwistHave[0] = sTwistHave[1] = 0;
        sRollHave[0] = sRollHave[1] = 0;
    } else {
        keep = keep90(NECK_KEEP);
        for (i = 0; i < 3; i++) {
            sF[i] = sF[i] * keep + want[i] * (1.0f - keep);
        }
        v_unit3(sF, sF);
    }
    sWasActive = sFrame;
    /* the same in model space: the place's rotation, no scale */
    v_set(sFm, unplace->m[0] * sF[0] + unplace->m[4] * sF[1] + unplace->m[8] * sF[2],
          unplace->m[1] * sF[0] + unplace->m[5] * sF[1] + unplace->m[9] * sF[2],
          unplace->m[2] * sF[0] + unplace->m[6] * sF[1] + unplace->m[10] * sF[2]);
    if (!v_unit3(sFm, sFm)) {
        v_set(sFm, 0.0f, 0.0f, 1.0f);
    }
    /* the body's sideways, toward each arm: her left is the model's +x */
    v_cross3(side, (const float[3]){0.0f, 1.0f, 0.0f}, sF);
    v_unit3(side, side);
    v_copy3(sS[0], side);
    v_scale3(sS[1], side, -1.0f);
    sActive = 1;
    /* once: where her shoulders are put, in the player's metres - the offset the reach is measured
     * from too, so the arm and the calibration agree */
    if (!sShouldersLogged) {
        sShouldersLogged = 1;
        fprintf(stderr, "vrbody: shoulders - %.2f m below the eyes, %.3f m behind them, %.2f m to each"
                " side (%.3f m from the eye: the reach is measured from there), held within %.2f m of"
                " her own and reached by the clavicle's turn\n", (double)SH_UP_M, (double)SH_BACK_M,
                (double)SH_SIDE_M,
                (double)sqrtf(SH_UP_M * SH_UP_M + SH_BACK_M * SH_BACK_M + SH_SIDE_M * SH_SIDE_M),
                (double)SH_MAX_M);
    }
    /* once, to the console: the sign conventions a headset report needs (the elbow's two vertical
     * turns and the legs' rules) - the first thing to quote when an elbow or a knee looks wrong */
    if (!sSignsLogged) {
        sSignsLogged = 1;
        fprintf(stderr,
                "vrbody: signs - elbow bend %+.0f (of the elbow's own side, mirrored), elbow swing %+.0f,"
                " elbow outward %.2f; the knees follow her pelvis's forward, in the leg's own forward"
                " plane, and both legs stay straight until the head is %.0f cm below the calibrated"
                " height\n",
                (double)ELBOW_BEND_SIGN, (double)ELBOW_SWING_SIGN, (double)ELBOW_OUTWARD,
                (double)(BODY_DROP_DEAD * 100.0f));
    }
    return 1;
}

/* ---- the legs, kept on their feet ----------------------------------------------------------- */

/* a bone aimed from -> to: X along it, Z from the pole made square to it (humanlegs.c's aim) */
static void aim_leg(Mat4 *m, const float *from, const float *to, const float *pole) {
    float x[3], y[3], z[3], k;

    v_sub3(x, to, from);
    if (!v_unit3(x, x)) {
        return;
    }
    v_copy3(z, pole);
    k = v_dot3(z, x);
    z[0] -= k * x[0];
    z[1] -= k * x[1];
    z[2] -= k * x[2];
    if (!v_unit3(z, z)) {
        v_set(z, x[1], -x[0], 0.0f);
        if (!v_unit3(z, z)) {
            v_set(z, 0.0f, x[2], -x[1]);
            v_unit3(z, z);
        }
    }
    v_cross3(y, z, x);
    v_unit3(y, y);
    v_cross3(z, x, y);
    v_unit3(z, z);
    *m = mat4_identity();
    m->m[0] = x[0]; m->m[1] = x[1]; m->m[2] = x[2];
    m->m[4] = y[0]; m->m[5] = y[1]; m->m[6] = y[2];
    m->m[8] = z[0]; m->m[9] = z[1]; m->m[10] = z[2];
    m->m[12] = from[0];
    m->m[13] = from[1];
    m->m[14] = from[2];
}
/* One leg's two bones solved to the ankle `foot_at` it had, the knee set toward `bend` (her
 * pelvis's own forward, and the target then squared into the leg's forward plane - never to the
 * side) and the bones aimed with `roll` for their Z axis (her hip's own,
 * as humanlegs.c's aim uses it: the pole only sets which way the knee goes, not the skin's roll -
 * aiming a leg with the forward instead twisted its skin a quarter turn, which read as a sideways
 * knee). The same law of cosines as humanlegs.c's leg, which cannot be re-run here: it reads the
 * feet from the skeleton this body has just moved. A leg reaches its rest length plus LEG_STRETCH,
 * no more. */
static void replant(Mat4 *world, const Model *m, int side, const float *foot_at, const float *bend,
                    const float *roll) {
    int thigh = sBones.thigh[side], shin = sBones.shin[side], foot = sBones.foot[side];
    float root[3], end[3], d[3], side_dir[3], mid[3], was[3], delta[3];
    float dist, l1, l2, a, h;

    m_pos(&world[thigh], root);
    v_copy3(end, foot_at);
    l1 = sBones.leg1[side];
    l2 = sBones.leg2[side];
    v_sub3(d, end, root);
    dist = v_len3(d);
    if (dist < 1.0e-4f) {
        return;
    }
    v_unit3(d, d);
    dist = clampf(dist, fabsf(l1 - l2) + 1.0e-3f, l1 + l2 + LEG_STRETCH);
    /* the knee's side: the bend's direction made square to the leg. Past full length h is 0: the
     * legs are straight at her full height. */
    v_scale3(side_dir, d, v_dot3(bend, d));
    v_sub3(side_dir, bend, side_dir);
    if (!v_unit3(side_dir, side_dir)) {   /* (a pole along the leg: any side will do, but square it too) */
        v_set(side_dir, 0.0f, 0.0f, 1.0f);
        v_scale3(was, d, v_dot3(side_dir, d));
        v_sub3(side_dir, side_dir, was);
        if (!v_unit3(side_dir, side_dir)) {
            v_set(side_dir, 1.0f, 0.0f, 0.0f);
        }
    }
    a = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    h = sqrtf(fmaxf(0.0f, l1 * l1 - a * a));
    v_scale3(mid, d, a);
    v_add3(mid, root, mid);
    v_scale3(side_dir, side_dir, h);
    v_add3(mid, mid, side_dir);
    /* the knee target sits in the leg's own forward plane - the plane through the hip that holds the
     * leg and her pelvis's forward - so it cannot drift sideways: whatever the pole left, taken out */
    {
        float nrm[3], off[3];

        v_cross3(nrm, d, bend);
        if (v_unit3(nrm, nrm)) {
            v_sub3(off, mid, root);
            v_scale3(off, nrm, -v_dot3(off, nrm));
            v_add3(mid, mid, off);
        }
    }
    v_scale3(end, d, dist);
    v_add3(end, root, end);
    aim_leg(&world[thigh], root, mid, roll);
    aim_leg(&world[shin], mid, end, roll);
    /* the foot and its toe keep their turn, moved with the ankle's solve */
    m_pos(&world[foot], was);
    v_sub3(delta, end, was);
    shift_tree(world, m, foot, delta);
}

/* a bone's own scale: its 3x3 by s, its place kept a share s of the way from `p` */
static void scale_about(Mat4 *m, const float *p, float s);

/* ---- 2's chest turn and 3's body under the headset ----------------------------------------- */

/* Her pelvis's own forward, model space: the bind's forward (+z, as her eyes sit in front of her
 * neck) turned by how far her hips have turned since the bind - the animation's turn and ours,
 * never the chest's own swing. */
static void pelvis_forward(const Mat4 *world, const Model *m, int pelvis, float out[3]) {
    Mat4 d;

    if (pelvis < 0 || pelvis >= m->nbones) {
        v_set(out, 0.0f, 0.0f, 1.0f);
        return;
    }
    d = mat4_mul(world[pelvis], rigid_inv(&m->bones[pelvis].inv_bind));
    v_set(out, d.m[8], d.m[9], d.m[10]);
    if (!v_unit3(out, out)) {
        v_set(out, 0.0f, 0.0f, 1.0f);
    }
}

void vrbody_body(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    const Model *m = &a->model;
    float head[3], up[3] = {0.0f, 1.0f, 0.0f}, pivot[3], n[3], n_aim[3], p0[3], d[3];
    float n_hip_room[3], n_aim_room[3], want[3], swing[3], axis[3], fwd_a, side_a, c, cex, bp, delta, keep, len;
    float feet[2][3];
    Mat4 r;
    int i, side, seated, eye_bone, deep = 0;

    if (!sActive) {
        return;
    }
    /* The camera places her eyes on her head's height. Read it now, before the body and the
     * first-person head move it, so neither ever feeds back into the camera. */
    eye_bone = a->head.head >= 0 ? a->head.head : sBones.head;
    if (eye_bone >= 0 && eye_bone < m->nbones) {
        m_pos(&world[eye_bone], head);
        sHeadY = mat4_point(place, vec3(head[0], head[1], head[2])).y;
        sHeadWho = a;
        sHeadPos[0] = a->pos.x;
        sHeadPos[1] = a->pos.z;
        sHeadHave = 1;
    }
    /* the feet as the motion and humanlegs left them, before the pelvis moves */
    for (side = 0; side < 2; side++) {
        m_pos(&world[sBones.foot[side]], feet[side]);
    }
    /* 2. the body's forward against her facing: spread over the waist, the chest and the neck */
    delta = wrap_pi(atan2f(sFm[0], sFm[2]));
    if (fabsf(delta) > 1.0e-4f) {
        static const float kShare[3] = {CHEST_WAIST, CHEST_CHEST, CHEST_NECK};
        int b[3];

        b[0] = sBones.waist;
        b[1] = sBones.chest;
        b[2] = sBones.neck;
        for (i = 0; i < 3; i++) {
            m_pos(&world[b[i]], pivot);
            rot_about(up, delta * kShare[i], pivot, &r);
            turn_tree(world, m, b[i], &r);
        }
    }

    /* 3. The neck's target. The headset is at her eyes, not her neck: a look up or down swings those
     * eyes about the neck (by the model's own eye-to-neck offset), and that swing is not the body
     * moving - read as one, it crouched her legs whenever she looked down. So the neck's move is the
     * eye's less its swing, and a drop the game's own crouch animation is already scrubbing her with
     * (scripts/story/crouch.fs, past vr.crouch-deadzone) is left to it: only what it leaves moves
     * the hips. The look's back/down offsets shape the spine's aim only, never the hips. */
    seated = VR_SEATED != 0 || gVrOptions.seated != 0;
    cex = seated ? 0.0f : crouch_excess(clampf(vr_crouch_fraction(), 0.0f, 1.0f));
    c = VR_CROUCH_BEND ? cex : 0.0f;
    bp = c * (BODY_PITCH_BASE + BODY_PITCH_LOOK * (sPitch / DEG)) / BODY_PITCH_DIV * DEG;
    m_pos(&world[sBones.neck], head);
    {
        Vec3 nk = mat4_point(place, vec3(head[0], head[1], head[2]));
        float cs = cosf(sPitch), sn = sinf(sPitch);
        float swing_f = sBones.eyeFwd * (cs - 1.0f) + sBones.eyeUp * sn;   /* the eye's own way */
        float swing_u = sBones.eyeUp * (cs - 1.0f) - sBones.eyeFwd * sn;   /* ... and its rise */
        float front = BODY_HEAD_FRONT * sBones.kBody;
        float move = sBones.moveScale;   /* (their motion in her sizes: the calibration) */
        float dx = ((sHmd.x - sEye.x) - sF[0] * swing_f) * move;
        float dz = ((sHmd.z - sEye.z) - sF[2] * swing_f) * move;
        float dy = (sHmd.y - sEye.y) - swing_u;

        /* the vertical: a relaxed stance sits a few centimetres below the calibrated one, and
         * standing tall must not stretch her legs - neither is a crouch. A real drop (past the
         * deadzone) is followed, in her sizes, less the part her crouch animation scrubs. */
        if (dy > 0.0f) {
            dy = 0.0f;
        } else if (-dy <= sBones.dropDead) {
            dy = 0.0f;   /* (standing at the calibrated height, or a relaxed stance: not a crouch) */
        } else {
            dy = (dy + sBones.dropDead) * move * (1.0f - cex);
            deep = 1;
        }
        n_hip_room[0] = nk.x + dx - sF[0] * front;
        n_hip_room[1] = nk.y + dy;
        n_hip_room[2] = nk.z + dz - sF[2] * front;
        if (seated) {
            n_hip_room[1] += SEATED_HMD_UP * sBones.kBody;
        }
        n_aim_room[0] = n_hip_room[0] - BODY_LOOK_BACK * fabsf(sPitch) * sBones.kBody * sF[0];
        n_aim_room[1] = n_hip_room[1] - BODY_LOOK_DOWN * sBones.kBody * sPitch;
        n_aim_room[2] = n_hip_room[2] - BODY_LOOK_BACK * fabsf(sPitch) * sBones.kBody * sF[2];
    }
    {
        Vec3 nm = mat4_point(unplace, vec3(n_hip_room[0], n_hip_room[1], n_hip_room[2]));
        Vec3 am = mat4_point(unplace, vec3(n_aim_room[0], n_aim_room[1], n_aim_room[2]));

        n[0] = nm.x;
        n[1] = nm.y;
        n[2] = nm.z;
        n_aim[0] = am.x;
        n_aim[1] = am.y;
        n_aim[2] = am.z;
    }
    m_pos(&world[sBones.pelvis], p0);
    v_sub3(d, n, p0);
    /* the hips go back as she folds forward, then the torso's length is kept to the target */
    v_scale3(want, sFm, -tanf(bp) * v_len3(d));
    v_add3(want, p0, want);
    v_sub3(d, want, n);
    if (!v_unit3(d, d)) {
        v_scale3(d, sFm, -1.0f);
    }
    v_scale3(want, d, sBones.torso);
    v_add3(want, n, want);
    /* FRIK's COM: two thirds of the vertical change is dropped */
    delta = want[1] - p0[1];
    want[1] -= BODY_COM_DROP * delta;
    /* Her height is where the calibration put it until the player is really down: a look, a lean or
     * a relaxed stance must leave the pelvis at the height the legs are straight at (a few millimetres
     * of drop is a whole knee's worth of bend in a leg this long). Only a duck past the deadzone
     * lowers it, and then the knees fold - down her pelvis's forward. */
    if (!deep) {
        want[1] = p0[1];
    }
    if (seated) {
        want[0] += sFm[0] * SEATED_BODY_FRONT * sKbodyM;
        want[1] += SEATED_BODY_UP * sKbodyM;
        want[2] += sFm[2] * SEATED_BODY_FRONT * sKbodyM;
    }
    v_sub3(want, want, p0);
    if (!sPelvisHave) {
        v_copy3(sPelvisOff, want);
        sPelvisHave = 1;
    } else {
        keep = keep90(BODY_KEEP);
        for (i = 0; i < 3; i++) {
            sPelvisOff[i] = sPelvisOff[i] * keep + want[i] * (1.0f - keep);
        }
    }
    {   /* The hips may rise no further than the legs can reach, or the feet would be pulled off
         * the floor: the move is scaled back until both legs are within their rest length plus
         * LEG_STRETCH of the feet. (A crouch only takes it down, which the knees can always do.) */
        float s = 1.0f;
        int k, fits = 0;

        for (k = 0; k < 12 && !fits; k++) {
            fits = 1;
            for (side = 0; side < 2; side++) {
                float hip[3];

                m_pos(&world[sBones.thigh[side]], hip);
                hip[0] += sPelvisOff[0] * s;
                hip[1] += sPelvisOff[1] * s;
                hip[2] += sPelvisOff[2] * s;
                if (v_dist3(feet[side], hip) > sBones.leg1[side] + sBones.leg2[side] + LEG_STRETCH) {
                    fits = 0;
                }
            }
            if (!fits) {
                s *= 0.5f;
            }
        }
        if (s < 1.0f) {
            v_scale3(sPelvisOff, sPelvisOff, s);
        }
    }
    shift_body(world, m, sBones.root, sPelvisOff);

    /* the spine: the pelvis -> neck line turned to the new pelvis -> target, clamped, spread */
    m_pos(&world[sBones.pelvis], p0);
    m_pos(&world[sBones.neck], head);
    v_sub3(d, head, p0);
    if (!v_unit3(d, d)) {
        v_copy3(d, sFm);
    }
    v_sub3(want, n_aim, p0);
    if (!v_unit3(want, want)) {
        v_copy3(want, d);
    }
    {
        float q[4], ang;

        q_between(d, want, q);
        ang = 2.0f * acosf(clampf(q[3], -1.0f, 1.0f));
        if (sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) < 1.0e-6f || ang < 1.0e-5f) {
            v_set(swing, 0.0f, 0.0f, 0.0f);
        } else {
            v_set(swing, q[0], q[1], q[2]);
            v_unit3(swing, swing);
            v_scale3(swing, swing, ang);
        }
    }
    /* its forward and sideways parts, each clamped (a forward lean, a sideways lean) */
    v_cross3(axis, up, sFm);
    v_unit3(axis, axis);
    fwd_a = clampf(v_dot3(swing, axis), -SPINE_BACK * DEG, SPINE_FORWARD * DEG);
    side_a = clampf(v_dot3(swing, sFm), -SPINE_SIDE * DEG, SPINE_SIDE * DEG);
    v_scale3(swing, axis, fwd_a);
    v_scale3(d, sFm, side_a);
    v_add3(swing, swing, d);
    if (!sSwingHave) {
        v_copy3(sSwing, swing);
        sSwingHave = 1;
    } else {
        keep = keep90(BODY_KEEP);
        for (i = 0; i < 3; i++) {
            sSwing[i] = sSwing[i] * keep + swing[i] * (1.0f - keep);
        }
    }
    len = v_len3(sSwing);
    if (len > 1.0e-5f) {
        static const float kShare[3] = {SPINE_WAIST, SPINE_CHEST, SPINE_NECK};
        int b[3];

        b[0] = sBones.waist;
        b[1] = sBones.chest;
        b[2] = sBones.neck;
        v_unit3(axis, sSwing);
        for (i = 0; i < 3; i++) {
            m_pos(&world[b[i]], pivot);
            rot_about(axis, len * kShare[i], pivot, &r);
            turn_tree(world, m, b[i], &r);
        }
    }
    /* the feet are where they were: the legs are solved back to them, the knees down her pelvis's
     * own forward (never the chest's swing, never sideways), their skin's roll left as her hips had
     * it (a pole of the forward here twisted the legs sideways) */
    {
        float pf[3];

        pelvis_forward(world, m, sBones.pelvis, pf);
        for (side = 0; side < 2; side++) {
            float roll[3];

            m_col_unit(&world[sBones.thigh[side]], 2, roll);
            replant(world, m, side, feet[side], pf, roll);
        }
    }
    /* Her drawn body scaled to the player's height (FRIK's body scale): a uniform scale of this
     * pose alone, about her feet, so her shoulders sit where the player's do and the arms solve
     * from there to the controllers. Her actor's scale, collision, hitbox, movement, the world and
     * the camera are not touched: nothing but the skinned pose ever sees this. */
    if (fabsf(sBones.bodyScale - 1.0f) > 1.0e-4f && m->nbones > 0) {
        float pivot[3];

        m_pos(&world[sBones.root], pivot);
        pivot[1] = fminf(feet[0][1], feet[1][1]);   /* (her feet stay on the floor) */
        for (i = 0; i < m->nbones; i++) {
            scale_about(&world[i], pivot, sBones.bodyScale);
        }
    }
}

/* ---- FRIK's DampenHands and its shoulder's (clavicle's) IK ---------------------------------- */

int vrbody_dampen(int side, Vec3 *at, Vec3 *fwd, Vec3 *up) {
    float q[4], keep, t;

    if (!sActive || !VR_DAMPEN_HANDS || at == NULL || fwd == NULL || up == NULL || side < 0 || side > 1) {
        return 0;
    }
    q_of_hand(*fwd, *up, q);
    if (!sHandSeen[side]) {
        sHandPos[side] = *at;
        memcpy(sHandQuat[side], q, sizeof(q));
        sHandSeen[side] = 1;
        return 0;
    }
    keep = keep90(HAND_KEEP);
    t = 1.0f - keep;
    sHandPos[side] = vec3_add(sHandPos[side], vec3_scale(vec3_sub(*at, sHandPos[side]), t));
    q_slerp(sHandQuat[side], q, t, sHandQuat[side]);
    *at = sHandPos[side];
    hand_of_q(sHandQuat[side], fwd, up);
    return 1;
}

void vrbody_clavicle(struct Actor *a, Mat4 *world, const Mat4 *unplace, int side, const float *target) {
    const Model *m = &a->model;
    float t[3], sh[3], origin[3], d[3], off[3], ua[3], ub[3], axis[3];
    float len, adjust, L, ang;
    Mat4 r;
    Vec3 tm;

    if (!sActive || target == NULL || world == NULL || unplace == NULL || side < 0 || side > 1) {
        return;
    }
    if (sBones.clav[side] < 0 || sBones.upper[side] < 0) {
        return;
    }
    tm = mat4_point(unplace, vec3(target[0], target[1], target[2]));
    t[0] = tm.x;
    t[1] = tm.y;
    t[2] = tm.z;
    m_pos(&world[sBones.upper[side]], sh);
    m_pos(&world[sBones.clav[side]], origin);
    v_sub3(d, t, sh);
    len = v_len3(d);
    if (len < 1.0e-5f) {
        return;
    }
    L = sBones.armLen * sBones.bodyScale;   /* (a fraction of HER arm, in her own model's units: nothing
                                             * here may move with the player's height) */
    adjust = clampf(len - 0.5f * L, 0.0f, 0.85f * L) / (0.85f * L);
    v_scale3(off, d, adjust * CLAV_REACH * L / len);
    if (off[1] < 0.0f) {
        off[1] *= CLAV_DOWN;
    }
    v_sub3(ua, sh, origin);
    v_add3(ub, ua, off);
    if (!v_unit3(ua, ua) || !v_unit3(ub, ub)) {
        return;
    }
    v_cross3(axis, ua, ub);
    ang = atan2f(v_len3(axis), clampf(v_dot3(ua, ub), -1.0f, 1.0f));
    if (ang < 1.0e-5f) {
        return;
    }
    rot_about(axis, ang, origin, &r);
    turn_tree(world, m, sBones.clav[side], &r);
}

/* ---- 3b. her head at the camera: her eyes where the player's are ------------------------------ */

void vrbody_head_camera(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    const Model *m;
    Vec3 e;
    float yaw, eye[3], cam[3], anchor[3], cm[3], am[3], delta[3], fwd[3], back, upm, u;
    int b;

    if (!sActive || a == NULL || world == NULL || place == NULL || unplace == NULL) {
        return;
    }
    m = &a->model;
    if (sBones.head < 0) {
        return;
    }
    if (!vr_anchor_get(&e, &yaw)) {
        return;
    }
    (void)yaw;
    cam[0] = e.x;
    cam[1] = e.y;
    cam[2] = e.z;
    u = sBones.unitsPerM > 0.5f ? sBones.unitsPerM : VR_UNITS_PER_METER;
    /* The camera sits a little *inside* her head: the point of her head that goes to the camera is
     * EYE_BACK_M behind her eyes - back along her forward, toward the centre of her head - and
     * EYE_UP_M above them (the room's +Y is the ceiling). Both in her space; the player's fine
     * tuning (vr.cam-back, vr.cam-up) is clamped to its range. */
    back = clampf(gVrOptions.cam_back, 0.0f, EYE_BACK_MAX) * u;
    upm = clampf(gVrOptions.cam_up, -EYE_UP_RANGE, EYE_UP_RANGE) * u;
    b = sBones.eyeBone >= 0 && sBones.eyeBone < m->nbones ? sBones.eyeBone : sBones.head;
    m_pos(&world[b], eye);
    if (b != sBones.eyeBone) {
        eye[1] += EYE_ABOVE_HEAD;   /* (no eye bone: the head bone + a little) */
    }
    /* her eye point and the anchor point, in the room (the game's own +Y up, her forward level) */
    {
        Vec3 er = mat4_point(place, vec3(eye[0], eye[1], eye[2]));

        fwd[0] = sF[0];
        fwd[1] = 0.0f;
        fwd[2] = sF[2];
        if (!v_unit3(fwd, fwd)) {
            v_set(fwd, 0.0f, 0.0f, 1.0f);
        }
        anchor[0] = er.x - fwd[0] * back;
        anchor[1] = er.y + upm;
        anchor[2] = er.z - fwd[2] * back;
    }
    /* The camera is her head's place by the anchor's own making (her head + 1.5), so her head is
     * part of one unit with it: the anchor point goes to the camera and the neck stretches to it.
     * That is what keeps the view inside her head - looking down shows her chest. */
    {
        Vec3 c2 = mat4_point(unplace, vec3(cam[0], cam[1], cam[2]));
        Vec3 a2 = mat4_point(unplace, vec3(anchor[0], anchor[1], anchor[2]));

        cm[0] = c2.x;
        cm[1] = c2.y;
        cm[2] = c2.z;
        am[0] = a2.x;
        am[1] = a2.y;
        am[2] = a2.z;
    }
    v_sub3(delta, cm, am);
    /* The seat may never be far: the view is read from her head, so a seat that chased a moved
     * camera would climb frame after frame (a cutscene's end showed it). This bounds it, and says
     * so once if it is ever asked for. */
    {
        float lim = HEAD_SEAT_MAX_M * u;
        float len = v_len3(delta);

        if (len > lim) {
            if (!sHeadSeatWarned) {
                sHeadSeatWarned = 1;
                fprintf(stderr, "vrbody: the head seat asked for %.3f m - held to %.2f m\n",
                        (double)(len / u), (double)HEAD_SEAT_MAX_M);
            }
            v_scale3(delta, delta, lim / len);
        }
    }
    if (v_len3(delta) > 1.0e-5f) {
        shift_tree(world, m, sBones.head, delta);
    }
    if (!sHeadCamLogged) {
        float d = v_len3(delta);

        sHeadCamLogged = 1;
        m_pos(&world[b], eye);
        fprintf(stderr, "vrbody: head at the camera - camera %.2f %.2f %.2f, her eye %.2f %.2f %.2f,"
                " %.3f m apart before the seat; the camera %.3f m behind her eyes and %.3f m above them;"
                " reach_scale %.3f\n",
                (double)cam[0], (double)cam[1], (double)cam[2], (double)eye[0], (double)eye[1],
                (double)eye[2], (double)(d / u), (double)(back / u), (double)(upm / u),
                (double)sBones.armScale);
    }
}

/* ---- 4b. FRIK's shoulders: her arm's root where the player's is ---------------------------------- */

void vrbody_shoulders(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace, int side,
                      const float *target) {
    const Model *m;
    Vec3 e, wm;
    float eye[3], yaw, fwd[3], want[3], sh[3], origin[3], ua[3], ub[3], d[3], axis[3];
    float len, sane, u, back, out, ang;
    Mat4 r;

    if (!sActive || a == NULL || world == NULL || unplace == NULL || target == NULL ||
        side < 0 || side > 1) {
        return;
    }
    m = &a->model;
    if (sBones.upper[side] < 0 || sBones.clav[side] < 0) {
        return;
    }
    if (!vr_anchor_get(&e, &yaw)) {   /* (her eyes, in the room - where the camera anchor is) */
        return;
    }
    (void)yaw;
    eye[0] = e.x;
    eye[1] = e.y;
    eye[2] = e.z;
    u = sBones.unitsPerM > 0.5f ? sBones.unitsPerM : VR_UNITS_PER_METER;
    /* her body's yaw, not the head's: a look up or down must not move a shoulder */
    fwd[0] = sF[0];
    fwd[1] = 0.0f;
    fwd[2] = sF[2];
    if (!v_unit3(fwd, fwd)) {
        v_set(fwd, 0.0f, 0.0f, 1.0f);
    }
    /* the player's own shoulder, in the ROOM (the anchor's own space and up): below the eyes, a
     * little behind them, out to her side */
    back = SH_BACK_M * u;
    out = SH_SIDE_M * u;
    want[0] = eye[0] - fwd[0] * back + sS[side][0] * out;
    want[1] = eye[1] - SH_UP_M * u;
    want[2] = eye[2] - fwd[2] * back + sS[side][2] * out;
    /* into her model's space - world[] is the model, the anchor is the room (the same conversion
     * vrbody_clavicle makes; mixing the two was a shoulder thrown far from her body) */
    wm = mat4_point(unplace, vec3(want[0], want[1], want[2]));
    want[0] = wm.x;
    want[1] = wm.y;
    want[2] = wm.z;
    m_pos(&world[sBones.upper[side]], sh);
    m_pos(&world[sBones.clav[side]], origin);
    /* never far from where her own body put it: 0.15 m at the most, blended toward the target
     * (a warning once a side, so a bad offset can be seen rather than a thrown shoulder) */
    sane = SH_MAX_M * u;
    v_sub3(d, want, sh);
    len = v_len3(d);
    if (len > sane) {
        if (!sShoulderWarned[side]) {
            sShoulderWarned[side] = 1;
            fprintf(stderr, "vrbody: the %s shoulder target is %.3f m from her own: held to %.2f m\n",
                    side == 0 ? "left" : "right", (double)(len / u), (double)SH_MAX_M);
        }
        v_scale3(d, d, sane / len);
        v_add3(want, sh, d);
    }
    /* the clavicle TURNS toward it and keeps its length - the shoulder lands on its own arc (
     * that rotation is where the shoulder's reach comes from; nothing is stretched) */
    v_sub3(ua, sh, origin);
    v_sub3(ub, want, origin);
    if (!v_unit3(ua, ua) || !v_unit3(ub, ub)) {
        return;
    }
    v_cross3(axis, ua, ub);
    ang = atan2f(v_len3(axis), clampf(v_dot3(ua, ub), -1.0f, 1.0f));
    if (ang > 1.0e-5f) {
        rot_about(axis, ang, origin, &r);
        turn_tree(world, m, sBones.clav[side], &r);
    }
    /* once a side: the eye, the shoulder it asks for, hers as posed, and how far apart (metres) */
    if (!sShoulderLogged[side]) {
        Vec3 sp = mat4_point(place != NULL ? place : unplace, vec3(sh[0], sh[1], sh[2]));
        Vec3 wp = mat4_point(place != NULL ? place : unplace, vec3(want[0], want[1], want[2]));
        float dm = sqrtf((wp.x - sp.x) * (wp.x - sp.x) + (wp.y - sp.y) * (wp.y - sp.y) +
                         (wp.z - sp.z) * (wp.z - sp.z));

        sShoulderLogged[side] = 1;
        fprintf(stderr, "vrbody: %s shoulder - eye %.2f %.2f %.2f, asked %.2f %.2f %.2f, hers %.2f %.2f %.2f; %.3f m apart (held to %.2f m at most)\n",
                side == 0 ? "left" : "right", (double)eye[0], (double)eye[1], (double)eye[2],
                (double)wp.x, (double)wp.y, (double)wp.z, (double)sp.x, (double)sp.y, (double)sp.z,
                (double)(dm / u), (double)SH_MAX_M);
    }
}

/* ---- 5. the arm's solve -------------------------------------------------------------------- */

int vrbody_arm(struct Actor *a, int side, const float *root, const float *target, const Mat4 *grip,
               VrArm *out) {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float scale = a->scale != 0.0f ? a->scale : 1.0f;
    float d[3], c0[3], c1[3], back[3], thumb[3], npl[3], npt[3], rel[3], bend[3];
    float sum[3], proj[3], dif[3], elbow[3];
    float l1, l2, len1, len2, dist, t1, t2, k, raw, keep, hb, behind, crossd;
    float across, armtwist, behindhead, fwd, lift, upw, lo, hi, limit, turn, tw, a_cos, h;

    if (!sActive || out == NULL || root == NULL || target == NULL || grip == NULL || side < 0 || side > 1) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    v_copy3(out->reach, target);
    if (sBones.upper[side] < 0 || sBones.elbow[side] < 0 || sBones.hand[side] < 0) {
        return 0;
    }
    l1 = len1 = sBones.arm1 * scale * sBones.bodyScale * sBones.armScale;
    l2 = len2 = sBones.arm2 * scale * sBones.bodyScale * sBones.armScale;
    v_sub3(d, target, root);
    dist = v_len3(d);
    if (dist < 1.0e-5f || l1 < 1.0e-4f || l2 < 1.0e-4f) {
        return 0;
    }
    if (dist > ARM_TOO_FAR * (len1 + len2)) {
        out->skip = 1;
        return 0;
    }
    v_unit3(d, d);
    /* 1. the hand is the controller's: the target is never moved out or in. All an arm of her
     * length can do is bend, or go straight - so the distance is clamped to what the two bones
     * reach: fully folded is |l1 - l2|, fully straight is l1 + l2 and not a hair more (that is
     * where a far hand leaves a wrist gap rather than pulling her arm out of shape), and the
     * elbow's 145 degree window only steers its direction below, never the reach. */
    dist = clampf(dist, fabsf(l1 - l2) + ARM_REACH_EPS, l1 + l2 + ARM_REACH_EPS);
    v_scale3(out->reach, d, dist);
    v_add3(out->reach, root, out->reach);

    /* 2. the wrist's twist: the back of the hand and its thumb side, in the body's frame (up is
     * the room's up, as FRIK's z is up) */
    m_col_unit(grip, 0, c0);
    m_col_unit(grip, 1, c1);
    if (side == 0) {
        v_scale3(back, c0, -1.0f);
        v_scale3(thumb, c1, -1.0f);
    } else {
        v_copy3(back, c0);
        v_copy3(thumb, c1);
    }
    t1 = asinf(clampf(v_dot3(back, up), -0.999f, 0.999f));
    t2 = -asinf(clampf(v_dot3(thumb, up), -0.599f, 0.999f));
    k = clampf((v_dot3(back, up) + 0.866f) * 1.155f, WRIST_K_FLOOR, WRIST_K_TOP);
    raw = t1 + k * (t2 - t1);
    if (!sTwistHave[side]) {
        sTwist[side] = raw;
        sTwistHave[side] = 1;
    } else {
        keep = keep90(WRIST_KEEP);
        sTwist[side] = sTwist[side] * keep + raw * (1.0f - keep);
    }
    tw = sTwist[side];

    /* 3. the limit window */
    v_sub3(rel, target, root);
    hb = -v_dot3(rel, sF);   /* how far the hand is behind the shoulder's plane */
    behind = clampf((hb - BEHIND_OFF * sBones.kArm) / (BEHIND_SPAN * sBones.kArm), 0.0f, 1.0f);
    v_turn(sF, up, (side == 0 ? 1.0f : -1.0f) * CROSS_ANGLE * DEG, npl);
    v_scale3(npt, npl, CROSS_OFF * sBones.kArm);
    v_add3(npt, root, npt);
    v_sub3(rel, target, npt);
    /* the distance past that plane: on the far side of it, which is -n's way (a hand across the
     * body is past it) */
    crossd = clampf(-v_dot3(rel, npl) / (CROSS_SPAN * sBones.kArm), 0.0f, 1.0f);
    lift = 1.0f - clampf((root[1] + LIFT_SPAN * sBones.kArm - target[1]) / (LIFT_SPAN * sBones.kArm),
                         0.0f, 1.0f);
    upw = clampf(lift * LIFT_GAIN, 0.0f, 1.0f);
    lo = (ELBOW_MIN + ELBOW_BEHIND * behind) * DEG;
    hi = (ELBOW_MAX - smooth_max(ELBOW_CROSS_CUT * crossd, ELBOW_UP_CUT * upw, ELBOW_SMOOTH)) * DEG;
    hi = smooth_max(hi, lo + ELBOW_KEEP * DEG, ELBOW_SMOOTH * DEG);
    limit = lo + (tw + 1.57079633f) / 3.14159265f * (hi - lo);
    limit = clampf(limit, lo, hi);

    /* 4. the pole: straight down, turned about the body's sideways by the window's angle */
    v_set(bend, 0.0f, -1.0f, 0.0f);
    v_turn(bend, sS[side], (side == 0 ? 1.0f : -1.0f) * ELBOW_BEND_SIGN * limit, bend);

    /* 5. forward: the hand across the body pulls the elbow forward, and so does one behind the head */
    v_sub3(rel, target, root);
    across = clampf(-v_dot3(rel, sS[side]) / (ELBOW_ACROSS * sBones.kArm), 0.0f, 1.0f);
    if (target[1] < root[1]) {
        across *= ELBOW_ACROSS_LOW;
    }
    v_scale3(sum, sF, 0.5f);
    v_add3(sum, sS[side], sum);
    v_unit3(sum, sum);
    armtwist = clampf(v_dot3(thumb, sum) - fmaxf(0.0f, across + 0.25f), 0.0f, 1.0f);
    behindhead = clampf(hb / (ELBOW_BEHIND_HEAD * sBones.kArm), 0.0f, 1.0f) *
                 clampf(upw * ELBOW_BEHIND_GAIN, 0.0f, 1.0f);
    fwd = smooth_max(ELBOW_FWD_ACROSS * across, ELBOW_FWD_BEHIND * behindhead, ELBOW_SMOOTH);
    turn = (ELBOW_AIM_TWIST - ELBOW_AIM_TWIST_CUT * armtwist - fwd) * DEG;
    v_turn(bend, up, (side == 0 ? ELBOW_SWING_SIGN : -ELBOW_SWING_SIGN) * turn, bend);

    /* and never inward: the swing's sideways part turns over as the wrist twists, and an elbow that
     * crosses the body's middle reads as the joint bending the wrong way. A floor of the side's own
     * direction keeps the elbow on her arm's side of her in every pose. */
    {
        float lat = v_dot3(bend, sS[side]) - ELBOW_OUTWARD;

        if (lat < 0.0f) {
            float fix[3];

            v_scale3(fix, sS[side], -lat);
            v_add3(bend, bend, fix);
            if (!v_unit3(bend, bend)) {   /* (exactly inward: down and out instead) */
                v_scale3(bend, sS[side], ELBOW_OUTWARD);
                bend[1] = -1.0f;
                v_unit3(bend, bend);
            }
        }
    }

    /* 6. the elbow, in the plane of shoulder -> target and the bend's direction */
    v_sub3(d, out->reach, root);
    v_unit3(d, d);
    v_scale3(proj, d, v_dot3(bend, d));
    v_sub3(dif, bend, proj);
    if (!v_unit3(dif, dif)) {
        v_scale3(proj, d, v_dot3(sF, d));
        v_sub3(dif, sF, proj);
        if (!v_unit3(dif, dif)) {
            v_set(dif, 0.0f, 0.0f, 1.0f);
        }
    }
    a_cos = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    h = sqrtf(fmaxf(0.0f, l1 * l1 - a_cos * a_cos));
    v_scale3(elbow, d, a_cos);
    v_add3(elbow, root, elbow);
    v_scale3(dif, dif, h);
    v_add3(elbow, elbow, dif);
    v_copy3(out->elbow, elbow);
    v_copy3(out->pole, bend);
    sArmOn[side] = 1;
    return 1;
}

/* ---- 6, 7 and 8: the forearm's roll, the upper arm's, the head --------------------------------- */

/* a bone's own scale: its 3x3 by s, its place kept a share s of the way from `p` */
static void scale_about(Mat4 *m, const float *p, float s) {
    int i;

    for (i = 0; i < 3; i++) {
        m->m[i] *= s;
        m->m[4 + i] *= s;
        m->m[8 + i] *= s;
        m->m[12 + i] = p[i] + (m->m[12 + i] - p[i]) * s;
    }
}
/* the twist of a rotation about `axis`: 2 atan2(q.xyz . axis, q.w), wrapped to (-pi, pi] */
static float twist_of(const float q[4], const float *axis) {
    return wrap_pi(2.0f * atan2f(v_dot3(q, axis), q[3]));
}
/* the same, not wrapped: a turn past 180 degrees keeps its way (the caller unwraps it against the
 * last frame's and clamps it, so it cannot flip from +170 to -170 across the seam) */
static float twist_raw(const float q[4], const float *axis) {
    return 2.0f * atan2f(v_dot3(q, axis), q[3]);
}
/* a bone rolled about its own length (its X axis): its Y and Z turned, its place and what it
 * carries left where they are */
static void roll_bone(Mat4 *m, float ang) {
    float ca = cosf(ang), sa = sinf(ang);
    float yx = m->m[4], yy = m->m[5], yz = m->m[6];
    float zx = m->m[8], zy = m->m[9], zz = m->m[10];

    m->m[4] = ca * yx + sa * zx;
    m->m[5] = ca * yy + sa * zy;
    m->m[6] = ca * yz + sa * zz;
    m->m[8] = -sa * yx + ca * zx;
    m->m[9] = -sa * yy + ca * zy;
    m->m[10] = -sa * yz + ca * zz;
}

void vrbody_finish(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    const Model *m = &a->model;
    Mat4 f, r;
    float axis[3], p[3], was[3], qa[4], qb[4], q[4], theta, keep;
    int side;

    (void)a;
    (void)place;
    (void)unplace;
    if (!sActive) {
        return;
    }

    /* 6. the forearm's roll: what the hand did against a rigid follow of the elbow */
    for (side = 0; side < 2; side++) {
        int el = sBones.elbow[side], hand = sBones.hand[side], fore = sBones.fore[side];

        if (!sArmOn[side] || el < 0 || hand < 0 || fore < 0) {
            continue;
        }
        m_pos(&world[hand], p);
        m_pos(&world[el], was);
        v_sub3(axis, p, was);
        if (!v_unit3(axis, axis)) {
            continue;
        }
        f = follow(world, m, el, hand);
        q_of_mat(&world[hand], qa);
        q_of_mat(&f, qb);
        q_conj(qb, qb);
        q_mul(qa, qb, q);
        theta = twist_raw(q, axis);   /* (not wrapped to +-pi: past 180 it keeps its way) */
        if (sRollHave[side]) {   /* (the nearest turn to last frame's: it does not flip at +-180) */
            while (theta - sRoll[side] > 3.14159265f) {
                theta -= 6.28318531f;
            }
            while (theta - sRoll[side] < -3.14159265f) {
                theta += 6.28318531f;
            }
        }
        theta = clampf(theta, -TWIST_LIMIT * DEG, TWIST_LIMIT * DEG);
        if (!sRollHave[side]) {
            sRoll[side] = theta;
            sRollHave[side] = 1;
        } else {
            keep = keep90(TWIST_KEEP);
            sRoll[side] = sRoll[side] * keep + theta * (1.0f - keep);
        }
        theta = sRoll[side];
        /* Where the roll sits along the arm: none at the shoulder, TWIST_UPPER of it at the elbow,
         * 0.5 at the forearm's elbow half, the cuff's share at the cuff and all of it at the hand -
         * each an absolute turn about the forearm's axis, monotonic, and each joint's share of it
         * clamped. The upper arm and the elbow bone take the elbow's share as a roll of their own
         * (a roll about a bone's length moves nothing the solve placed), so a wrist turned 180
         * degrees cannot candy-wrap one joint: the twist is spread up the whole arm. */
        {
            float t = fabsf(theta);
            float up_a = fminf(TWIST_UPPER * t, TWIST_UPPER_LIMIT * DEG);
            float fore_a = clampf(TWIST_FORE * t, up_a, TWIST_FORE_LIMIT * DEG);
            float cuff_a = clampf((VR_TWIST_FRIK_CUFF ? 1.0f : TWIST_CUFF) * t, fore_a,
                                  TWIST_CUFF_LIMIT * DEG);
            float sgn = theta < 0.0f ? -1.0f : 1.0f;
            float s27 = (world[sBones.upper[side]].m[0] * axis[0] + world[sBones.upper[side]].m[1] * axis[1] +
                         world[sBones.upper[side]].m[2] * axis[2] < 0.0f) ? -1.0f : 1.0f;
            float s28 = (world[el].m[0] * axis[0] + world[el].m[1] * axis[1] +
                         world[el].m[2] * axis[2] < 0.0f) ? -1.0f : 1.0f;

            roll_bone(&world[sBones.upper[side]], sgn * s27 * up_a);
            roll_bone(&world[el], sgn * s28 * up_a);
            /* the forearm and the cuff's helpers, from the elbow's own follow: what the upper arm
             * already took, taken off theirs */
            f = follow(world, m, el, fore);
            m_pos(&f, p);
            rot_about(axis, sgn * (fore_a - up_a), p, &r);
            world[fore] = mat4_mul(r, f);
            if (sBones.twist1[side] >= 0) {
                int h = sBones.twist1[side];

                f = follow(world, m, el, h);
                m_pos(&f, p);
                rot_about(axis, sgn * (fore_a - up_a), p, &r);
                world[h] = mat4_mul(r, f);
            }
            if (sBones.twist2[side] >= 0) {
                int h = sBones.twist2[side];

                f = follow(world, m, el, h);
                m_pos(&f, p);
                rot_about(axis, sgn * (cuff_a - up_a), p, &r);
                world[h] = mat4_mul(r, f);
            }
        }
    }

    /* 7. the upper arm's twist and the deltoid, from the arm against its clavicle's own follow */
    for (side = 0; side < 2; side++) {
        int clav = sBones.clav[side], up = sBones.upper[side], el = sBones.elbow[side];
        float tu, au[3];

        if (!sArmOn[side] || clav < 0 || up < 0 || el < 0) {
            continue;
        }
        f = follow(world, m, clav, up);
        q_of_mat(&world[up], qa);
        q_of_mat(&f, qb);
        q_conj(qb, qb);
        q_mul(qa, qb, q);
        m_pos(&world[el], p);
        m_pos(&world[up], was);
        v_sub3(au, p, was);
        if (!v_unit3(au, au)) {
            continue;
        }
        tu = twist_of(q, au);
        if (sBones.upperTwist[side] >= 0) {   /* the upper arm's own twist: half of it taken back */
            int h = sBones.upperTwist[side];

            f = follow(world, m, up, h);
            m_pos(&f, p);
            rot_about(au, -UPPER_TWIST * tu, p, &r);
            world[h] = mat4_mul(r, f);
        }
        if (sBones.deltoid[side] >= 0) {   /* the deltoid: halfway between the clavicle and the arm */
            float qi[4], id[4] = {0.0f, 0.0f, 0.0f, 1.0f}, half[4];
            int h = sBones.deltoid[side];

            q_conj(q, qi);
            q_slerp(id, qi, DELTOID, half);
            f = follow(world, m, up, h);
            m_pos(&world[up], p);
            q_about(half, p, &r);
            world[h] = mat4_mul(r, f);
        }
    }

    /* 8. the first-person head: the neck's helper shrunk on its own origin, and the head and
     * everything on it (the eyes, the hair, a mod's bones too) collapsed into the neck's top.
     * VRIK's "shrink" mode: its "push back" would stretch this one-piece neck. */
    if (gVrOptions.body_fp_head && sBones.haveNeckFp && sBones.neck >= 0 && sBones.head >= 0) {
        int fp = sBones.neckFp, head = sBones.head, i;
        float c[3], hp[3];

        f = follow(world, m, sBones.neck, fp);
        m_pos(&f, c);
        world[fp] = f;
        scale_about(&world[fp], c, VR_FP_NECK_SCALE);
        /* hp: the head's own place, kept VR_FP_NECK_SCALE of the way from the neck's top */
        m_pos(&world[head], hp);
        for (i = 0; i < 3; i++) {
            hp[i] = c[i] + VR_FP_NECK_SCALE * (hp[i] - c[i]);
        }
        /* TODO: a shadow, a mirror or a future selfie view should get the whole head. They are
         * the same skinned vertices (actor_prepare poses once), so skipping this for those
         * passes needs a second pose, which is not cheap: left for the pass that asks for it.
         * A skinned normal is made unit at the end (actor.c's skin: vec3_norm leaves a
         * zero-length one zero), so the collapsed head's normals give no NaN. */
        for (i = 0; i < m->nbones; i++) {
            if (i == head || under(m, i, head)) {
                scale_about(&world[i], hp, VR_FP_HEAD_SCALE);
            }
        }
    }
}

/* ---- the camera and the near plane ---------------------------------------------------------- */

float vrbody_near(float znear) {
    float t;

    if (!sActive) {
        return znear;
    }
    t = clampf((sPitch / DEG - FP_NEAR_FROM) / (FP_NEAR_TO - FP_NEAR_FROM), 0.0f, 1.0f);
    return znear * (1.0f + (FP_NEAR_SHARE - 1.0f) * t);
}

int vrbody_eye_y(const struct Actor *a, float *y) {
    if (!sHeadHave || a == NULL || a != sHeadWho || y == NULL) {
        return 0;
    }
    /* The head remembered is the one before any body pose (the seat below moves the bone). A room
     * change can leave it metres from where she is now (she has been put somewhere else): then it
     * is not hers and the caller takes her posed head instead. */
    if (fabsf(a->pos.x - sHeadPos[0]) + fabsf(a->pos.z - sHeadPos[1]) > 2.0f * VR_UNITS_PER_METER) {
        return 0;   /* (another room's head: not hers any more) */
    }
    *y = sHeadY;
    return 1;
}

/* Her place has jumped under the body (a room change, a teleport, a snap turn): the next posed
 * frame starts its smoothings afresh. A hand's smoothed frame, the hip's offset and the spine's
 * swing are kept in the room's own space, so easing from where they were in the place she left
 * stretches her arms toward it for the first frames of the new one. */
void vrbody_forget_place(void) {
    sFHave = 0;
    sPelvisHave = 0;
    sSwingHave = 0;
    sHandSeen[0] = sHandSeen[1] = 0;
    sTwistHave[0] = sTwistHave[1] = 0;
    sRollHave[0] = sRollHave[1] = 0;
}

void vrbody_idle(void) {
    sActive = 0;
    /* (sHeadHave kept: the camera keeps the last head it found before any body pose, exactly as
     * during a pose - dropping it would hand the view the seated head and it would climb) */
    sArmOn[0] = sArmOn[1] = 0;
    /* A cutscene, a menu or non-VR play has moved her: the next posed frame starts its smoothings
     * afresh instead of easing from where the body was before the jump. */
    vrbody_forget_place();
}

