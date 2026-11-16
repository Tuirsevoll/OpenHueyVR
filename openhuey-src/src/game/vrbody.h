/* Fiona's VR body (src/game/vrbody.c): a clean reimplementation of the documented behaviour of
 * FRIK - the body under the headset, the elbow's own IK and the forearm's twist helpers - on this
 * game's skeleton. It is called from vrhands_pose and runs only where the arms are posed (VR
 * gameplay: an anchor, her shown, no cutscene); a cutscene, a scene or non-VR play never calls it,
 * so the motions are left exactly as they are.
 *
 * All of it works in the model space vrhands_pose hands it (the same world[] matrices, before the
 * skinning), except the headset and the hands, which are in the room: `place` takes a model-space
 * matrix into the room and `unplace` brings it back. */
#ifndef VRBODY_H
#define VRBODY_H

#include "../core/mathx.h"

struct Actor;

/* the bones the body found by name (vrbody_bone). `side` 0 is her left, 1 her right */
enum {
    VRBODY_ROOT,          /* bone 0: the floor */
    VRBODY_PELVIS,        /* bone 1 */
    VRBODY_WAIST,         /* bone 23 */
    VRBODY_CHEST,         /* bone 24 */
    VRBODY_NECK,          /* bone 32 */
    VRBODY_HEAD,          /* bone 33 */
    VRBODY_CLAV,          /* bone 26 / 42 */
    VRBODY_UPPER,         /* bone 27 / 43: the shoulder joint */
    VRBODY_ELBOW,         /* bone 28 / 44: the elbow hinge */
    VRBODY_FORE,          /* bone 29 / 45: the PS2's own forearm roll */
    VRBODY_HAND,          /* bone 30 / 46: the wrist */
    VRBODY_TWIST1,        /* vr L/R forearm twist 1 */
    VRBODY_TWIST2,        /* vr L/R forearm twist 2 */
    VRBODY_UPPER_TWIST,   /* vr L/R upperarm twist */
    VRBODY_DELTOID,       /* vr L/R shoulder */
    VRBODY_NECK_FP,       /* vr neck fp */
    VRBODY_THIGH,         /* bone 2 / 6 */
    VRBODY_SHIN,          /* bone 3 / 7 */
    VRBODY_FOOT,          /* bone 4 / 8: the ankle the leg's solve moves */
    VRBODY_TOE,           /* bone 5 / 9 */
    VRBODY_PARTS
};

/* the bone this body found by name, -1 when the model has none */
int vrbody_bone(int part, int side);
/* The arm's scale, from the calibration (the player's reach over Fiona's, FRIK-style): the arm's
 * bones are stretched by it so her hand is the controller's - no floating hand - and the IK's
 * lengths are that many times Fiona's. 1 before a model is read; the height sizes it before a
 * reach is measured. */
float vrbody_arm_scale(const struct Actor *a);
/* the uniform scale her *drawn* skeleton takes (her body against the player's height, FRIK's
 * body scale): the skinned pose only - her actor's scale, collision, the world and the camera are
 * untouched. 1 before a model is read. */
float vrbody_body_scale(const struct Actor *a);
/* the two ratios FRIK's numbers are given in, computed once a model (0 before the first pose) */
float vrbody_karm(void);
float vrbody_kbody(void);

/* One pose: find the bones (once a model, logged once), read the headset and the hands, and work
 * out the body's forward (the yaw from the head and the hands, smoothed). 0: the body is not
 * active - no anchor, no tracked headset - and every call below does nothing. */
int vrbody_begin(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace);
/* The body under the headset: the chest's turn toward the body's forward, the pelvis and the
 * spine (with the feet left where they were), the crouch's bend. */
void vrbody_body(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace);
/* FRIK's DampenHands: one hand's frame smoothed for the body's pose only (a door, a ladder and a
 * grab keep the raw controller). 1 when it smoothed it. */
int vrbody_dampen(int side, Vec3 *at, Vec3 *fwd, Vec3 *up);
/* The clavicle turned toward a target far from the shoulder, before that arm's solve. `target` is
 * a room point (the grip). */
void vrbody_clavicle(struct Actor *a, Mat4 *world, const Mat4 *unplace, int side, const float *target);
/* FRIK's shoulders: the target her arm's root should sit at - 0.20 m below the eyes, 0.075 m behind
 * them, 0.18 m to the side, in the room's units along her body's yaw - worked out in the room and
 * brought into her model's space (unplace), then reached by TURNING the clavicle (its length kept,
 * nothing stretched) and held within 0.15 m of her own posed shoulder. Call it before
 * vrbody_clavicle and vrbody_arm (the clavicle's own turn toward a far hand comes after it). */
void vrbody_shoulders(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace, int side,
                      const float *target);

/* The arm's solve in the room: the elbow from FRIK's limit window and the wrist's twist. */
typedef struct VrArm {
    int skip;        /* the target is far past any arm (a tracker glitch): no solve this frame */
    float reach[3];  /* the hand target after the flexion limits (room) */
    float elbow[3];  /* the elbow (room) */
    float pole[3];   /* the bend direction to aim the bones with (room) */
} VrArm;

/* 1 when the arm was solved; `grip` is the hand's grip matrix in the room (its axes give the
 * wrist's twist), `root` the shoulder joint and `target` the grip's translation. */
int vrbody_arm(struct Actor *a, int side, const float *root, const float *target, const Mat4 *grip, VrArm *out);
/* After the arms: the forearm's roll (bone 29 and the two forearm helpers), the upper arm's twist
 * and the deltoid, then the first-person head. Each helper is rebuilt from its parent every frame,
 * so nothing accumulates. */
void vrbody_finish(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace);
/* The near plane the first-person view should use: lowered as the headset looks down (VRIK), else
 * `znear` as it is. */
float vrbody_near(float znear);
/* Her head's height in the room as it was before this pose's body and first-person head (1 when it
 * is this actor's; `y` left alone otherwise). The camera places her eyes on her head, so it must
 * read this, not the head the body and the shrink moved. */
int vrbody_eye_y(const struct Actor *a, float *y);
/* no pose this frame (the IK is off: a cutscene, a scene, non-VR): nothing of the body's is used
 * by the camera or the near plane */
void vrbody_idle(void);

#endif
