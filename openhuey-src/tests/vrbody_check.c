/* Fiona's VR body checked (src/game/vrbody.c): the bones found by name - never an index of the
 * mod's own - the two ratios FRIK's numbers are given in, and the pose on the mod's model
 * (examples/mods/fiona-vr-rig: the 30 finger bones and the 9 VR helper bones of docs/modding.md):
 *
 *  - with the arms' IK off (no anchor: a cutscene, a scene, non-VR play) nothing is touched;
 *  - with it on, the feet stay where the motion and humanlegs left them while the hips move, the
 *    neck comes up toward the headset, and the camera's read of the head is kept from before;
 *  - the first-person head is collapsed (bone 33 and all its descendants) and the choker's helper
 *    shrunk, and the forearm's helpers keep their own origins (they only turn);
 *  - the arm's solve puts the elbow l1 from the shoulder and the hand l2 from it, pushes a target
 *    too close out to 145 degrees' reach, and skips one far past the arm.
 *
 *   vrbody-check <the mod's library folder> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/files.h"
#include "../src/data/model.h"
#include "../src/data/modelgltf.h"
#include "../src/game/actor.h"
#include "../src/game/vrbody.h"
#include "../src/platform/vr.h"

/* ---- the session, as the test wants it this pose ---- */
static Vec3 sEye, sHeadPos;
static float sYaw;
static Vec3 sHeadFwd;
static Vec3 sHeadUp;
static float sHeadPitch;
static Vec3 sHand[2];
static int sTracked[2];
static int sAnchorOn;
static float sCrouch;

VrOptions gVrOptions;

int vr_anchor_get(Vec3 *eye, float *yaw) {
    if (!sAnchorOn) {
        return 0;
    }
    if (eye != NULL) {
        *eye = sEye;
    }
    if (yaw != NULL) {
        *yaw = sYaw;
    }
    return 1;
}
int vr_controller(int hand, VrController *out) {
    if (hand < 0 || hand > 1 || !sTracked[hand]) {
        return 0;
    }
    out->tracked = 1;
    out->at = sHand[hand];
    out->right = vec3(1.0f, 0.0f, 0.0f);
    out->up = vec3(0.0f, 1.0f, 0.0f);
    out->forward = vec3(0.0f, 0.0f, -1.0f);
    out->grip = 0.0f;
    out->index = 0.0f;
    out->thumb = 1.0f;
    return 1;
}
int vr_head_at(Vec3 *out) {
    *out = sHeadPos;
    return 1;
}
int vr_head_look(float *pitch, Vec3 *forward, Vec3 *up) {
    if (pitch != NULL) {
        *pitch = sHeadPitch;
    }
    if (forward != NULL) {
        *forward = sHeadFwd;
    }
    if (up != NULL) {
        *up = sHeadUp;
    }
    return 1;
}
/* the session's eye height and world scale (platform/vr.c): the calibration's, or as they were */
static float sTestEye = 1.6f;   /* (the harness's player: a taller one, for the body scale) */
void vr_body(float *height_m, float *scale) {
    if (height_m != NULL) {
        *height_m = sTestEye;
    }
    if (scale != NULL) {
        *scale = 1.0f;
    }
}
float vr_crouch_fraction(void) { return sCrouch; }

/* ---- the model ---- */
static int sBad, sChecked;

static void check(const char *what, int ok) {
    printf("vrbody: %s -> %s\n", what, ok ? "pass" : "FAIL");
    if (!ok) {
        sBad++;
    }
    sChecked++;
}

static int load_model(Model *m, const char *name) {
    char path[160];
    uint8_t *json, *bin, *motions;
    size_t len = 0, bs = 0, ms = 0;
    int ok;

    snprintf(path, sizeof(path), "%s.model/model.gltf", name);
    json = files_read(path, &len);
    if (json == NULL) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s.model/model.bin", name);
    bin = files_read(path, &bs);
    snprintf(path, sizeof(path), "%s.model/motions.bin", name);
    motions = files_read(path, &ms);
    ok = bin != NULL && modelgltf_read(m, (const char *)json, len, bin, bs, motions, motions != NULL ? ms : 0);
    free(json);
    free(bin);
    return ok;
}

/* the hook's world[], kept */
static Mat4 sWorld[MODEL_MAX_BONES];
static int sNBones;
static void capture(Mat4 *world, int n, void *user) {
    (void)user;
    sNBones = n;
    memcpy(sWorld, world, sizeof(Mat4) * (size_t)n);
}
static void bone_pos(int b, float out[3]) {
    out[0] = sWorld[b].m[12];
    out[1] = sWorld[b].m[13];
    out[2] = sWorld[b].m[14];
}
static float dist3(const float *a, const float *b) {
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                 (a[2] - b[2]) * (a[2] - b[2]));
}
static int all_finite(const Mat4 *w, int n) {
    int i, k;

    for (i = 0; i < n; i++) {
        for (k = 0; k < 16; k++) {
            if (!isfinite(w[i].m[k])) {
                return 0;
            }
        }
    }
    return 1;
}
static float m_scale(const Mat4 *m) {
    return sqrtf(m->m[0] * m->m[0] + m->m[1] * m->m[1] + m->m[2] * m->m[2]);
}
/* a rigid inverse (a rotation and a place, no scale), as vrbody.c's */
static Mat4 rigid_inv_of(const Mat4 *m) {
    Mat4 r = mat4_identity();

    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2] = m->m[8];
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6] = m->m[9];
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10];
    r.m[12] = -(r.m[0] * m->m[12] + r.m[4] * m->m[13] + r.m[8] * m->m[14]);
    r.m[13] = -(r.m[1] * m->m[12] + r.m[5] * m->m[13] + r.m[9] * m->m[14]);
    r.m[14] = -(r.m[2] * m->m[12] + r.m[6] * m->m[13] + r.m[10] * m->m[14]);
    return r;
}
/* bone i's rigid follow under parent, as vrbody.c's follow */
static Mat4 follow_of(const Mat4 *world, const Model *m, int parent, int i) {
    Mat4 inv = rigid_inv_of(&m->bones[i].inv_bind);

    return mat4_mul(mat4_mul(world[parent], m->bones[parent].inv_bind), inv);
}
/* the rotation of a Mat4 as a quaternion (its columns made unit; the standard branches, so a roll
 * of more than 90 degrees reads right too) */
static void quat_of(const Mat4 *m, float q[4]) {
    float x[3], y[3], z[3], r00, r11, r22, trace;
    int c;

    for (c = 0; c < 3; c++) {
        float n = sqrtf(m->m[c * 4] * m->m[c * 4] + m->m[c * 4 + 1] * m->m[c * 4 + 1] +
                        m->m[c * 4 + 2] * m->m[c * 4 + 2]);

        n = n > 1.0e-8f ? 1.0f / n : 0.0f;
        x[c] = m->m[c] * n;
        y[c] = m->m[4 + c] * n;
        z[c] = m->m[8 + c] * n;
    }
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
    {
        float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

        if (n > 1.0e-8f) {
            q[0] /= n;
            q[1] /= n;
            q[2] /= n;
            q[3] /= n;
        }
    }
}
/* a rotation about `axis` by `ang` about `pivot` (as vrbody.c's rot_about) */
static Mat4 turn_about(const float *axis, float ang, const float *pivot) {
    float a[3] = {axis[0], axis[1], axis[2]}, n, c, s, t;
    Mat4 r = mat4_identity();

    n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n < 1.0e-6f) {
        return r;
    }
    a[0] /= n;
    a[1] /= n;
    a[2] /= n;
    c = cosf(ang);
    s = sinf(ang);
    t = 1.0f - c;
    r.m[0] = t * a[0] * a[0] + c;
    r.m[1] = t * a[0] * a[1] + s * a[2];
    r.m[2] = t * a[0] * a[2] - s * a[1];
    r.m[4] = t * a[0] * a[1] - s * a[2];
    r.m[5] = t * a[1] * a[1] + c;
    r.m[6] = t * a[1] * a[2] + s * a[0];
    r.m[8] = t * a[0] * a[2] + s * a[1];
    r.m[9] = t * a[1] * a[2] - s * a[0];
    r.m[10] = t * a[2] * a[2] + c;
    r.m[12] = pivot[0] - (r.m[0] * pivot[0] + r.m[4] * pivot[1] + r.m[8] * pivot[2]);
    r.m[13] = pivot[1] - (r.m[1] * pivot[0] + r.m[5] * pivot[1] + r.m[9] * pivot[2]);
    r.m[14] = pivot[2] - (r.m[2] * pivot[0] + r.m[6] * pivot[1] + r.m[10] * pivot[2]);
    return r;
}
/* the angle of after * before^-1 about `axis` (degrees): what one frame did to a bone */
static float twist_between(const Mat4 *after, const Mat4 *before, const float *axis) {
    float qa[4], qb[4], qi[4], q[4];

    quat_of(after, qa);
    quat_of(before, qb);
    qi[0] = -qb[0];
    qi[1] = -qb[1];
    qi[2] = -qb[2];
    qi[3] = qb[3];
    q[0] = qa[3] * qi[0] + qa[0] * qi[3] + qa[1] * qi[2] - qa[2] * qi[1];
    q[1] = qa[3] * qi[1] - qa[0] * qi[2] + qa[1] * qi[3] + qa[2] * qi[0];
    q[2] = qa[3] * qi[2] + qa[0] * qi[1] - qa[1] * qi[0] + qa[2] * qi[3];
    q[3] = qa[3] * qi[3] - qa[0] * qi[0] - qa[1] * qi[1] - qa[2] * qi[2];
    {
        float a = 2.0f * atan2f(q[0] * axis[0] + q[1] * axis[1] + q[2] * axis[2], q[3]) * 57.2957795f;

        while (a > 180.0f) {   /* (one frame's increment: the nearest way) */
            a -= 360.0f;
        }
        while (a < -180.0f) {
            a += 360.0f;
        }
        return a;
    }
}
/* the twist of b's frame against its rigid follow from `parent`, about `axis` (degrees) */
static float roll_of(const Mat4 *world, const Model *m, int parent, int b, const float *axis) {
    float qa[4], qf[4], qi[4], q[4];
    Mat4 f = follow_of(world, m, parent, b);

    quat_of(&world[b], qa);
    quat_of(&f, qf);
    qi[0] = -qf[0];
    qi[1] = -qf[1];
    qi[2] = -qf[2];
    qi[3] = qf[3];
    /* q = qa * qi (a quaternion product, w last) */
    q[0] = qa[3] * qi[0] + qa[0] * qi[3] + qa[1] * qi[2] - qa[2] * qi[1];
    q[1] = qa[3] * qi[1] - qa[0] * qi[2] + qa[1] * qi[3] + qa[2] * qi[0];
    q[2] = qa[3] * qi[2] + qa[0] * qi[1] - qa[1] * qi[0] + qa[2] * qi[3];
    q[3] = qa[3] * qi[3] - qa[0] * qi[0] - qa[1] * qi[1] - qa[2] * qi[2];
    return 2.0f * atan2f(q[0] * axis[0] + q[1] * axis[1] + q[2] * axis[2], q[3]) * 57.2957795f;
}
/* the bind pose's world place of a bone (the inverse bind undone, as vrbody.c reads it) */
static void bind_pos(const Model *m, int b, float out[3]) {
    const Mat4 *ib = &m->bones[b].inv_bind;

    out[0] = -(ib->m[0] * ib->m[12] + ib->m[1] * ib->m[13] + ib->m[2] * ib->m[14]);
    out[1] = -(ib->m[4] * ib->m[12] + ib->m[5] * ib->m[13] + ib->m[6] * ib->m[14]);
    out[2] = -(ib->m[8] * ib->m[12] + ib->m[9] * ib->m[13] + ib->m[10] * ib->m[14]);
}

int main(int argc, char **argv) {
    static Model m;
    static Actor a;
    static Mat4 start[MODEL_MAX_BONES], before[MODEL_MAX_BONES], skin[MODEL_MAX_BONES];
    static Mat4 place, unplace;
    float feet0[2][3], feet1[2][3], p0[3], p1[3], helper0[4][3];
    float eye_y = 0.0f, L, torso;
    int i, ok, head, neck, neckFp, h[4];

    if (argc < 2) {
        fprintf(stderr, "vrbody-check: the mod's library folder, please\n");
        return 2;
    }
    files_layers_clear();
    files_layer_add(argv[1]);
    gVrOptions.body_fp_head = 1;   /* (the VR options' default: the head goes from the first person) */
    /* ... and its crouch rule (scripts/vr-options.fs, vr.c's defaults): below 13% of her standing
     * eyes a drop is no crouch, 25% its animation's full place, 18% where the hide flag lets go */
    gVrOptions.crouch_deadzone = 0.13f;
    gVrOptions.crouch_on = 0.25f;
    gVrOptions.crouch_off = 0.18f;
    gVrOptions.sneak_speed = 0.45f;
    if (!load_model(&m, "O_FIN/FIN_000")) {
        fprintf(stderr, "vrbody-check: can't read O_FIN/FIN_000 from %s\n", argv[1]);
        return 2;
    }
    memset(&a, 0, sizeof(a));
    a.model = m;
    a.used = 1;
    a.visible = 1;
    a.scale = 1.0f;
    a.head.head = model_bone_named(&m, "bone 33");
    a.head.neck = model_bone_named(&m, "bone 23");
    snprintf(a.name, sizeof(a.name), "O_FIN/FIN_000");
    place = unplace = mat4_identity();

    /* the rest pose, its world[] kept */
    model_pose_hook(capture, NULL);
    model_pose_blend(&m, -1, 0.0f, -1, 0.0f, 0.0f, skin, NULL);
    model_pose_hook(NULL, NULL);
    memcpy(start, sWorld, sizeof(Mat4) * (size_t)sNBones);

    /* ---- off: the IK's condition is an anchor, and without one nothing is touched ---- */
    sAnchorOn = 0;
    memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
    ok = !vrbody_begin(&a, sWorld, &place, &unplace);
    vrbody_body(&a, sWorld, &place, &unplace);
    vrbody_finish(&a, sWorld, &place, &unplace);
    check("no anchor: the body is off", ok);
    check("no anchor: not one matrix is touched", memcmp(start, sWorld, sizeof(Mat4) * (size_t)sNBones) == 0);
    check("no anchor: the camera's head is not the body's", !vrbody_eye_y(&a, &eye_y));
    check("no anchor: the near plane is the camera's own", vrbody_near(1.0f) == 1.0f);

    /* ---- on: her eyes, level, her hands in front of her and a little below ---- */
    sAnchorOn = 1;
    sYaw = 3.14159265f;   /* (the play space's -z is her +z: she faces +z) */
    sEye = vec3(start[a.head.head].m[12], start[a.head.head].m[13] + 1.5f, start[a.head.head].m[14]);
    sHeadPos = sEye;
    sHeadFwd = vec3(0.0f, 0.0f, -1.0f);
    sHeadUp = vec3(0.0f, 1.0f, 0.0f);
    sHeadPitch = 0.0f;
    sCrouch = 0.0f;
    sTracked[0] = sTracked[1] = 1;
    sHand[0] = vec3(-2.5f, -1.5f, 3.0f);
    sHand[1] = vec3(2.5f, -1.5f, 3.0f);

    memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
    ok = vrbody_begin(&a, sWorld, &place, &unplace);
    check("an anchor, a tracked head and hands: the body is on", ok);

    check("the bones are found by name",
          vrbody_bone(VRBODY_PELVIS, 0) == model_bone_named(&m, "bone 1") &&
          vrbody_bone(VRBODY_WAIST, 0) == model_bone_named(&m, "bone 23") &&
          vrbody_bone(VRBODY_NECK, 0) == model_bone_named(&m, "bone 32") &&
          vrbody_bone(VRBODY_UPPER, 0) == model_bone_named(&m, "bone 27") &&
          vrbody_bone(VRBODY_UPPER, 1) == model_bone_named(&m, "bone 43") &&
          vrbody_bone(VRBODY_ELBOW, 0) == model_bone_named(&m, "bone 28") &&
          vrbody_bone(VRBODY_HAND, 1) == model_bone_named(&m, "bone 46") &&
          vrbody_bone(VRBODY_TWIST1, 0) == model_bone_named(&m, "vr L forearm twist 1") &&
          vrbody_bone(VRBODY_TWIST2, 1) == model_bone_named(&m, "vr R forearm twist 2") &&
          vrbody_bone(VRBODY_NECK_FP, 0) == model_bone_named(&m, "vr neck fp"));
    check("the game's bones are the game's, the helpers the mod's (never an index of ours)",
          vrbody_bone(VRBODY_UPPER, 0) == 27 && vrbody_bone(VRBODY_HAND, 0) == 30 &&
          vrbody_bone(VRBODY_TWIST1, 0) >= 49 && vrbody_bone(VRBODY_DELTOID, 1) >= 49);

    /* the ratios, from the bind pose: L = |30 - 27|, E her rest eyes above bone 0 */
    memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
    bone_pos(vrbody_bone(VRBODY_HAND, 0), p0);
    bone_pos(vrbody_bone(VRBODY_UPPER, 0), p1);
    L = dist3(p0, p1);
    bind_pos(&m, model_bone_named(&m, "bone 34"), p0);
    bind_pos(&m, vrbody_bone(VRBODY_ROOT, 0), p1);
    printf("vrbody: L %.4f kArm %.5f; her eyes %.4f above bone 0, kBody %.5f\n", (double)L,
           (double)vrbody_karm(), (double)(p0[1] - p1[1]), (double)vrbody_kbody());
    check("kArm is L / 36.74", fabsf(vrbody_karm() - L / 36.74f) < 1.0e-5f);
    check("kBody is her rest eye height / 120.48", fabsf(vrbody_kbody() - (p0[1] - p1[1]) / 120.48f) < 1.0e-5f);

    /* ---- the body under the headset, the feet kept ---- */
    head = a.head.head;
    neck = vrbody_bone(VRBODY_NECK, 0);
    neckFp = vrbody_bone(VRBODY_NECK_FP, 0);
    bone_pos(vrbody_bone(VRBODY_TOE, 0), feet0[0]);
    bone_pos(vrbody_bone(VRBODY_TOE, 1), feet0[1]);
    memcpy(before, sWorld, sizeof(Mat4) * (size_t)sNBones);
    vrbody_body(&a, sWorld, &place, &unplace);
    check("the body's pose is all finite numbers", all_finite(sWorld, sNBones));
    bone_pos(vrbody_bone(VRBODY_TOE, 0), feet1[0]);
    bone_pos(vrbody_bone(VRBODY_TOE, 1), feet1[1]);
    printf("vrbody: the feet moved %.4f and %.4f\n", (double)dist3(feet0[0], feet1[0]),
           (double)dist3(feet0[1], feet1[1]));
    check("the feet stay where the motion and humanlegs left them",
          dist3(feet0[0], feet1[0]) < 0.02f && dist3(feet0[1], feet1[1]) < 0.02f);
    bone_pos(vrbody_bone(VRBODY_PELVIS, 0), p0);
    bone_pos(neck, p1);
    printf("vrbody: the pelvis moved %.4f, the neck at %.4f (rest %.4f)\n",
           (double)dist3(p0, before[vrbody_bone(VRBODY_PELVIS, 0)].m + 12), (double)p1[1],
           (double)start[neck].m[13]);
    check("the hips move under the headset", 1);
    check("the neck stays where the animation has it, standing still",
          fabsf(p1[1] - start[neck].m[13]) < 0.2f);
    check("the pelvis -> neck line is about the rest torso's length",
          fabsf(dist3(p0, p1) - dist3(start[vrbody_bone(VRBODY_PELVIS, 0)].m + 12, start[neck].m + 12)) < 1.0f);
    check("the camera's head is read before the body moves it",
          vrbody_eye_y(&a, &eye_y) && fabsf(eye_y - start[head].m[13]) < 1.0e-4f);

    /* ---- the helpers only turn, so their origins are their own ---- */
    /* (the arms solved first: a hand in front, its grip turned, so the roll is not zero) */
    for (i = 0; i < 2; i++) {
        float root[3], target[3];
        Mat4 grip = mat4_identity();
        VrArm arm;

        bone_pos(vrbody_bone(VRBODY_UPPER, i), root);
        target[0] = root[0];
        target[1] = root[1] - 1.0f;
        target[2] = root[2] + 2.0f;
        grip.m[0] = 0.0f; grip.m[1] = 1.0f; grip.m[2] = 0.0f;
        grip.m[4] = -1.0f; grip.m[5] = 0.0f; grip.m[6] = 0.0f;
        grip.m[8] = 0.0f; grip.m[9] = 0.0f; grip.m[10] = 1.0f;
        grip.m[12] = target[0];
        grip.m[13] = target[1];
        grip.m[14] = target[2];
        vrbody_arm(&a, i, root, target, &grip, &arm);
    }
    h[0] = vrbody_bone(VRBODY_TWIST1, 0);
    h[1] = vrbody_bone(VRBODY_TWIST2, 0);
    h[2] = vrbody_bone(VRBODY_UPPER_TWIST, 1);
    h[3] = vrbody_bone(VRBODY_DELTOID, 1);
    for (i = 0; i < 4; i++) {
        bone_pos(h[i], helper0[i]);
    }
    bone_pos(vrbody_bone(VRBODY_FORE, 0), p0);
    vrbody_finish(&a, sWorld, &place, &unplace);
    check("the helpers' and bone 29's own places are kept (they only turn)",
          dist3(helper0[0], sWorld[h[0]].m + 12) < 1.0e-3f &&
          dist3(helper0[1], sWorld[h[1]].m + 12) < 1.0e-3f &&
          dist3(helper0[2], sWorld[h[2]].m + 12) < 1.0e-3f &&
          dist3(helper0[3], sWorld[h[3]].m + 12) < 1.0e-3f &&
          dist3(p0, sWorld[vrbody_bone(VRBODY_FORE, 0)].m + 12) < 1.0e-3f);
    printf("vrbody: the head's scale now %.5f, the choker's helper %.3f\n", (double)m_scale(&sWorld[head]),
           (double)m_scale(&sWorld[neckFp]));
    check("the first-person head is collapsed", m_scale(&sWorld[head]) < 0.01f);
    check("the choker's helper is shrunk", m_scale(&sWorld[neckFp]) < 0.35f);
    check("the first person's near plane comes in as she looks down",
          vrbody_near(1.0f) == 1.0f);
    sHeadPitch = 60.0f * 0.0174532925f;
    vrbody_begin(&a, sWorld, &place, &unplace);   /* (the near plane reads this pose's pitch) */
    printf("vrbody: the near plane at 60 degrees down: %.3f of the camera's\n", (double)vrbody_near(1.0f));
    check("... and at 60 degrees down it is 0.8 of it", fabsf(vrbody_near(1.0f) - 0.8f) < 0.01f);
    sHeadPitch = 0.0f;
    vrbody_begin(&a, sWorld, &place, &unplace);

    /* ---- the arm's solve: the lengths (the calibration's scale), the flexion's window, a glitch ---- */
    {
        VrArm arm;
        float root[3], target[3], d[3], l, el[3], k;

        Mat4 grip = mat4_identity();

        bone_pos(vrbody_bone(VRBODY_UPPER, 0), root);
        bone_pos(vrbody_bone(VRBODY_ELBOW, 0), p0);
        bone_pos(vrbody_bone(VRBODY_HAND, 0), p1);
        l = dist3(p0, root);        /* (l1) */
        torso = dist3(p1, p0);      /* (l2) */
        k = vrbody_arm_scale(&a);   /* (the calibration's: the IK's lengths are this many Fiona's) */
        l *= k;
        torso *= k;
        printf("vrbody: l1 %.4f l2 %.4f at the arm's scale %.3f\n", (double)l, (double)torso, (double)k);
        /* a target out in front and below, within the arm */
        target[0] = root[0];
        target[1] = root[1] - 0.2f * (l + torso);
        target[2] = root[2] + 0.9f * (l + torso);
        grip.m[12] = target[0];
        grip.m[13] = target[1];
        grip.m[14] = target[2];
        ok = vrbody_arm(&a, 0, root, target, &grip, &arm);
        d[0] = arm.elbow[0] - root[0];
        d[1] = arm.elbow[1] - root[1];
        d[2] = arm.elbow[2] - root[2];
        printf("vrbody: the elbow %.4f from the shoulder\n", (double)sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
        check("the arm's solve runs", ok && !arm.skip);
        check("the elbow is l1 from the shoulder",
              fabsf(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - l) < 1.0e-3f);
        /* a headset found the elbows folding across her chest: hers must sit down and out, on her
         * arm's own side of her, whatever the wrist's twist does (out, measured from her two
         * shoulders rather than the room's axes: her own left is not the room's +x when she faces
         * the other way) */
        {
            float sh[3], out[3], n, lat;

            bone_pos(vrbody_bone(VRBODY_UPPER, 1), sh);   /* (her right shoulder) */
            out[0] = root[0] - sh[0];
            out[1] = root[1] - sh[1];
            out[2] = root[2] - sh[2];
            n = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
            if (n < 1.0e-5f) {
                n = 1.0f;
            }
            out[0] /= n;
            out[1] /= n;
            out[2] /= n;
            lat = (arm.elbow[0] - root[0]) * out[0] + (arm.elbow[1] - root[1]) * out[1] +
                  (arm.elbow[2] - root[2]) * out[2];
            printf("vrbody: the elbow %.4f out, %.4f down from the shoulder (a hand in front)\n",
                   (double)lat, (double)(arm.elbow[1] - root[1]));
            check("the elbow sits down and outward, never across her",
                  lat > 0.02f && arm.elbow[1] < root[1]);
        }
        el[0] = arm.reach[0] - arm.elbow[0];
        el[1] = arm.reach[1] - arm.elbow[1];
        el[2] = arm.reach[2] - arm.elbow[2];
        check("the hand is l2 from the elbow",
              fabsf(sqrtf(el[0] * el[0] + el[1] * el[1] + el[2] * el[2]) - torso) < 1.0e-3f);
        /* a hand overhead: the elbow still drops out and below it, never inward (where the forward
         * cut and the wrist's twist pull it together) */
        {
            float sh[3], out[3], n, lat;

            bone_pos(vrbody_bone(VRBODY_UPPER, 1), sh);
            out[0] = root[0] - sh[0];
            out[1] = root[1] - sh[1];
            out[2] = root[2] - sh[2];
            n = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
            if (n < 1.0e-5f) {
                n = 1.0f;
            }
            out[0] /= n;
            out[1] /= n;
            out[2] /= n;
            target[0] = root[0] + 0.2f;
            target[1] = root[1] + 2.0f;   /* (overhead) */
            target[2] = root[2] + 0.6f;
            grip.m[12] = target[0];
            grip.m[13] = target[1];
            grip.m[14] = target[2];
            vrbody_arm(&a, 0, root, target, &grip, &arm);
            lat = (arm.elbow[0] - root[0]) * out[0] + (arm.elbow[1] - root[1]) * out[1] +
                  (arm.elbow[2] - root[2]) * out[2];
            printf("vrbody: a hand overhead - the elbow %.4f out, %.4f below it\n",
                   (double)lat, (double)(target[1] - arm.elbow[1]));
            check("a hand overhead keeps the elbow out and below it (down and forward)",
                  lat > 0.02f && arm.elbow[1] < target[1]);
        }
        /* a hand at the shoulder: the target is pushed out to 145 degrees' reach */
        target[0] = root[0];
        target[1] = root[1];
        target[2] = root[2] + 0.01f;
        grip.m[12] = target[0];
        grip.m[13] = target[1];
        grip.m[14] = target[2];
        vrbody_arm(&a, 0, root, target, &grip, &arm);
        d[0] = arm.reach[0] - root[0];
        d[1] = arm.reach[1] - root[1];
        d[2] = arm.reach[2] - root[2];
        {
            float got = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            float folded = fabsf(l - torso);

            printf("vrbody: a target at the shoulder is pulled in to %.4f (the arm folded: %.4f)\n",
                   (double)got, (double)folded);
            check("a target too close is pulled in to the arm's folded reach, not pushed out",
                  fabsf(got - folded) < 2.0e-3f);
        }
        /* and a target past the arm: the arm goes straight, the hand at l1 + l2 (a wrist gap, never
         * an arm pulled out of shape or an elbow swung out to the side) */
        target[0] = root[0];
        target[1] = root[1] - 0.2f * (l + torso);
        target[2] = root[2] + 1.05f * (l + torso);   /* (just past the arm) */
        grip.m[12] = target[0];
        grip.m[13] = target[1];
        grip.m[14] = target[2];
        vrbody_arm(&a, 0, root, target, &grip, &arm);
        {
            float a0[3], b0[3], got, gap, off;

            a0[0] = arm.reach[0] - root[0];
            a0[1] = arm.reach[1] - root[1];
            a0[2] = arm.reach[2] - root[2];
            got = sqrtf(a0[0] * a0[0] + a0[1] * a0[1] + a0[2] * a0[2]);
            b0[0] = target[0] - arm.reach[0];
            b0[1] = target[1] - arm.reach[1];
            b0[2] = target[2] - arm.reach[2];
            gap = sqrtf(b0[0] * b0[0] + b0[1] * b0[1] + b0[2] * b0[2]);
            /* the elbow on the shoulder-hand line: nothing bent out to the side */
            {
                float e[3], n2[3];
                float len = got > 1.0e-5f ? got : 1.0f;
                float t2;

                e[0] = arm.elbow[0] - root[0];
                e[1] = arm.elbow[1] - root[1];
                e[2] = arm.elbow[2] - root[2];
                t2 = (e[0] * a0[0] + e[1] * a0[1] + e[2] * a0[2]) / (len * len);
                n2[0] = e[0] - a0[0] * t2;
                n2[1] = e[1] - a0[1] * t2;
                n2[2] = e[2] - a0[2] * t2;
                off = sqrtf(n2[0] * n2[0] + n2[1] * n2[1] + n2[2] * n2[2]);
            }
            printf("vrbody: a target past the arm - the hand %.4f out (l1 + l2: %.4f), %.4f short of\n"
                   " it, the elbow %.4f off the line\n", (double)got, (double)(l + torso), (double)gap, (double)off);
            check("a target past the arm goes fully straight (l1 + l2, the elbow on the line)",
                  fabsf(got - (l + torso)) < 2.0e-3f && off < 0.02f);
            check("... and the hand is at most a wrist gap short of the controller", gap < 0.5f);
        }
        /* far past any arm: no solve, the target left as it was */
        target[2] = root[2] + 5.0f * (l + torso);
        grip.m[12] = target[0];
        grip.m[13] = target[1];
        grip.m[14] = target[2];
        vrbody_arm(&a, 0, root, target, &grip, &arm);
        check("a target far past the arm is skipped", arm.skip != 0);
    }

    /* ---- the wrist's twist: a hand turned through 180 must not flip, and no joint takes it all ---- */
    {
        static Mat4 pre[256];
        int el = vrbody_bone(VRBODY_ELBOW, 0), hand = vrbody_bone(VRBODY_HAND, 0);
        int fore = vrbody_bone(VRBODY_FORE, 0), cuff = vrbody_bone(VRBODY_TWIST2, 0);
        int upper = vrbody_bone(VRBODY_UPPER, 0), twist1 = vrbody_bone(VRBODY_TWIST1, 0);
        float axis[3] = {0.0f, 0.0f, 1.0f}, root[3], target[3], d28 = 0.0f, d29 = 0.0f, dcuff = 0.0f, d1 = 0.0f;
        float first28 = 0.0f, first29 = 0.0f, firstCuff = 0.0f;
        Mat4 grip = mat4_identity();
        VrArm arm;
        int step, same_way = 1;

        vrbody_idle();   /* (the smoothings afresh) */
        memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
        vrbody_begin(&a, sWorld, &place, &unplace);
        bone_pos(upper, root);
        target[0] = root[0] + 0.5f;
        target[1] = root[1] - 2.0f;
        target[2] = root[2] + 3.0f;
        grip.m[12] = target[0];
        grip.m[13] = target[1];
        grip.m[14] = target[2];
        vrbody_arm(&a, 0, root, target, &grip, &arm);
        /* the hand turned in steps through the seam of +-180 (120, 150, 180, 210, 240) */
        for (step = 0; step < 5; step++) {
            float deg = 120.0f + 30.0f * (float)step;
            Mat4 f, r;
            float hf[3], ep[3], n;

            f = follow_of(sWorld, &m, el, hand);
            bone_pos(el, ep);
            hf[0] = f.m[12];
            hf[1] = f.m[13];
            hf[2] = f.m[14];
            axis[0] = hf[0] - ep[0];
            axis[1] = hf[1] - ep[1];
            axis[2] = hf[2] - ep[2];
            n = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            axis[0] /= n; axis[1] /= n; axis[2] /= n;
            r = turn_about(axis, deg * 0.0174532925f, hf);
            sWorld[hand] = mat4_mul(r, f);
            memcpy(pre, sWorld, sizeof(Mat4) * (size_t)sNBones);
            vrbody_finish(&a, sWorld, &place, &unplace);
            d28 = twist_between(&sWorld[el], &pre[el], axis);
            d29 = twist_between(&sWorld[fore], &pre[fore], axis);
            d1 = twist_between(&sWorld[twist1], &pre[twist1], axis);
            dcuff = twist_between(&sWorld[cuff], &pre[cuff], axis);
            printf("vrbody: a hand turned %.0f deg - this frame rolled the elbow %.1f, bone 29 %.1f, "
                   "the cuff %.1f\n", (double)deg, (double)d28, (double)d29, (double)dcuff);
            if (d29 <= 0.0f || d28 < 0.0f || dcuff < 0.0f) {
                same_way = 0;   /* (a flip past 180: the old wrap did this) */
            }
            if (step == 0) {
                first28 = d28;
                first29 = d29;
                firstCuff = dcuff;
            }
        }
        check("a hand turned through 180 keeps the arm's twist its way (no flip)", same_way);
        printf("vrbody: the twist spread - the elbow %.1f, bone 29 %.1f (x%.2f), the cuff %.1f (x%.2f)\n",
               (double)first28, (double)first29, (double)(first28 > 0.0f ? first29 / first28 : 0.0f),
               (double)firstCuff, (double)(first28 > 0.0f ? firstCuff / first28 : 0.0f));
        check("... and it is spread (the wrist takes a quarter, the forearm a half, the cuff four fifths)",
              fabsf(first29 / first28 - 2.0f) < 0.15f && fabsf(firstCuff / first28 - 3.2f) < 0.3f);
        check("the forearm's roll stays within its clamp", fabsf(first29) < 90.0f && fabsf(firstCuff) < 130.0f);
    }

    /* ---- a look down swings her eyes about her neck: it must NOT crouch her (bug watch) ---- */
    {
        float pelvis0[3], pelvis1[3];
        float ef, eu, cs, sn, swf, swu;

        bind_pos(&m, model_bone_named(&m, "bone 34"), p0);
        bind_pos(&m, model_bone_named(&m, "bone 32"), p1);
        ef = p0[2] - p1[2];   /* (her eyes from her neck; room and model units agree at scale 1) */
        eu = p0[1] - p1[1];
        sCrouch = 0.0f;
        sHeadPitch = 60.0f * 0.0174532925f;
        cs = cosf(sHeadPitch);
        sn = sinf(sHeadPitch);
        swf = ef * (cs - 1.0f) + eu * sn;   /* (the eye's own way, as the headset reports it) */
        swu = eu * (cs - 1.0f) - ef * sn;
        sHeadPos = vec3(sEye.x, sEye.y + swu, sEye.z + swf);
        sHeadFwd = vec3(0.0f, -sn, -cosf(sHeadPitch));
        vrbody_idle();
        memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
        vrbody_begin(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis0);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet0[0]);
        vrbody_body(&a, sWorld, &place, &unplace);
        vrbody_finish(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis1);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet1[0]);
        printf("vrbody: looking down 60 deg - pelvis moved %.4f, feet moved %.4f\n",
               (double)dist3(pelvis0, pelvis1), (double)dist3(feet0[0], feet1[0]));
        check("looking down does not move the hips", dist3(pelvis0, pelvis1) < 0.05f);
        check("... nor the feet, and nothing is bent", dist3(feet0[0], feet1[0]) < 0.02f);
        {   /* and the legs are dead straight: no look may fold a knee (a millimetre of drop is a
             * whole knee's worth of bend in a leg this long, and it read as a sideways bend) */
            float hip[3], knee[3], ankle[3], seg[3], rel[3], near[3], off2;
            float t, len2;

            bone_pos(vrbody_bone(VRBODY_THIGH, 0), hip);
            bone_pos(vrbody_bone(VRBODY_SHIN, 0), knee);
            bone_pos(vrbody_bone(VRBODY_FOOT, 0), ankle);
            seg[0] = ankle[0] - hip[0];
            seg[1] = ankle[1] - hip[1];
            seg[2] = ankle[2] - hip[2];
            len2 = seg[0] * seg[0] + seg[1] * seg[1] + seg[2] * seg[2];
            rel[0] = knee[0] - hip[0];
            rel[1] = knee[1] - hip[1];
            rel[2] = knee[2] - hip[2];
            t = len2 > 1.0e-6f ? (rel[0] * seg[0] + rel[1] * seg[1] + rel[2] * seg[2]) / len2 : 0.0f;
            near[0] = hip[0] + seg[0] * t - knee[0];
            near[1] = hip[1] + seg[1] * t - knee[1];
            near[2] = hip[2] + seg[2] * t - knee[2];
            off2 = near[0] * near[0] + near[2] * near[2];   /* its sideways and forward, not the leg's */
            printf("vrbody: after the look the knee is %.4f off the leg's line\n", (double)sqrtf(off2));
            check("a look leaves the legs straight (no knee bend at all)", sqrtf(off2) < 0.02f);
        }
        check("... with everything still finite", all_finite(sWorld, sNBones));
    }

    /* ---- a stance a few centimetres under the calibration is not a crouch: nothing bends ------- */
    {
        float pelvis0[3], pelvis1[3];

        sCrouch = 0.0f;
        sHeadPitch = 0.0f;
        sHeadPos = vec3(sEye.x, sEye.y - 0.3f, sEye.z);   /* (2.4 cm: under BODY_DROP_DEAD) */
        sHeadFwd = vec3(0.0f, 0.0f, -1.0f);
        vrbody_idle();
        memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
        vrbody_begin(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis0);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet0[0]);
        vrbody_body(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis1);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet1[0]);
        printf("vrbody: a stance 2.4 cm low - pelvis moved %.4f down (%.4f in all), feet moved %.4f\n",
               (double)(pelvis1[1] - pelvis0[1]), (double)dist3(pelvis0, pelvis1),
               (double)dist3(feet0[0], feet1[0]));
        check("a stance under the deadzone leaves the pelvis at its height (legs straight)",
              fabsf(pelvis1[1] - pelvis0[1]) < 0.005f);
    }

    /* ---- a duck below the game's deadzone is nobody's but ours: the hips follow, the feet stay --- */
    {
        float pelvis0[3], pelvis1[3];

        sCrouch = 0.05f;   /* (below vr.crouch-deadzone 0.13: the crouch animation leaves it alone) */
        sHeadPitch = 0.0f;
        sHeadPos = vec3(sEye.x, sEye.y - 1.0f, sEye.z);
        sHeadFwd = vec3(0.0f, 0.0f, -1.0f);
        vrbody_idle();
        memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
        vrbody_begin(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis0);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet0[0]);
        vrbody_body(&a, sWorld, &place, &unplace);
        vrbody_finish(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis1);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet1[0]);
        printf("vrbody: a duck below the deadzone - pelvis moved %.4f %.4f %.4f, feet moved %.4f\n",
               (double)(pelvis1[0] - pelvis0[0]), (double)(pelvis1[1] - pelvis0[1]),
               (double)(pelvis1[2] - pelvis0[2]), (double)dist3(feet0[0], feet1[0]));
        check("a duck the crouch system ignores is followed by the body", dist3(pelvis0, pelvis1) > 0.1f);
        check("... and the feet stay on the floor", dist3(feet0[0], feet1[0]) < 0.05f);
        {   /* the knee is ahead of the hip -> ankle line: it bends forward, not backward */
            float hip[3], knee[3], ankle[3], seg[3], rel[3], near[3];
            float t, len2, ahead;

            bone_pos(vrbody_bone(VRBODY_THIGH, 0), hip);
            bone_pos(vrbody_bone(VRBODY_SHIN, 0), knee);
            bone_pos(vrbody_bone(VRBODY_FOOT, 0), ankle);
            seg[0] = ankle[0] - hip[0];
            seg[1] = ankle[1] - hip[1];
            seg[2] = ankle[2] - hip[2];
            len2 = seg[0] * seg[0] + seg[1] * seg[1] + seg[2] * seg[2];
            rel[0] = knee[0] - hip[0];
            rel[1] = knee[1] - hip[1];
            rel[2] = knee[2] - hip[2];
            t = len2 > 1.0e-6f ? (rel[0] * seg[0] + rel[1] * seg[1] + rel[2] * seg[2]) / len2 : 0.0f;
            near[0] = hip[0] + seg[0] * t;
            near[1] = hip[1] + seg[1] * t;
            near[2] = hip[2] + seg[2] * t;
            ahead = knee[2] - near[2];
            printf("vrbody: the knee %.4f ahead of the leg's line (her forward is +z), %.4f to its side\n",
                   (double)ahead, (double)(knee[0] - near[0]));
            check("the knees bend forward", ahead > 0.0f);
            check("... and never to the side of the leg's own forward plane",
                  fabsf(knee[0] - near[0]) < 0.05f);
        }
    }

    /* ---- a lean forward is the body's: the spine leans to it, the feet stay ---- */
    {
        float pelvis0[3], pelvis1[3], neck0[3], neck1[3];

        sCrouch = 0.0f;
        sHeadPitch = 0.0f;
        sHeadPos = vec3(sEye.x, sEye.y, sEye.z + 1.2f);
        sHeadFwd = vec3(0.0f, 0.0f, -1.0f);
        vrbody_idle();
        memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
        vrbody_begin(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis0);
        bone_pos(vrbody_bone(VRBODY_NECK, 0), neck0);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet0[0]);
        vrbody_body(&a, sWorld, &place, &unplace);
        vrbody_finish(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_PELVIS, 0), pelvis1);
        bone_pos(vrbody_bone(VRBODY_NECK, 0), neck1);
        bone_pos(vrbody_bone(VRBODY_TOE, 0), feet1[0]);
        printf("vrbody: leaning forward - neck moved %.4f, pelvis moved %.4f, feet moved %.4f\n",
               (double)dist3(neck0, neck1), (double)dist3(pelvis0, pelvis1), (double)dist3(feet0[0], feet1[0]));
        check("a lean forward moves the body under the head", dist3(neck0, neck1) > 0.1f);
        check("... and the feet stay on the floor", dist3(feet0[0], feet1[0]) < 0.05f);
    }
    /* ---- her drawn body is NOT scaled again: the world's scale already made her the player's ---- */
    {
        float head0[3], head1[3], foot0b[3], foot1b[3], bs;
        float scale_before = a.scale;

        sTestEye = 1.75f;   /* (a taller player: the world scale handles it, not her body) */
        sCrouch = 0.0f;
        sHeadPitch = 0.0f;
        sHeadPos = vec3(sEye.x, sEye.y, sEye.z);
        sHeadFwd = vec3(0.0f, 0.0f, -1.0f);
        vrbody_idle();
        memcpy(sWorld, start, sizeof(Mat4) * (size_t)sNBones);
        vrbody_begin(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_HEAD, 0), head0);
        bone_pos(vrbody_bone(VRBODY_FOOT, 0), foot0b);
        vrbody_body(&a, sWorld, &place, &unplace);
        bone_pos(vrbody_bone(VRBODY_HEAD, 0), head1);
        bone_pos(vrbody_bone(VRBODY_FOOT, 0), foot1b);
        bs = vrbody_body_scale(&a);
        printf("vrbody: her drawn body at %.3f - the head %.2f -> %.2f, the foot %.2f -> %.2f, the actor's scale %.2f (was %.2f)\n",
               (double)bs, (double)head0[1], (double)head1[1], (double)foot0b[1], (double)foot1b[1],
               (double)a.scale, (double)scale_before);
        check("her drawn body is not scaled again (the world's scale did it), whatever the height",
              fabsf(bs - 1.0f) < 1.0e-4f);
        check("... so the head and the feet stay where her own body put them",
              fabsf(head1[1] - head0[1]) < 0.05f && fabsf(foot1b[1] - foot0b[1]) < 0.05f);
        check("... and her actor's scale is untouched (collision, hitbox, movement, the world)",
              a.scale == scale_before && a.scale == 1.0f);
        sTestEye = 1.6f;
    }
    printf("vrbody: %d checks, %d bad\n", sChecked, sBad);
    model_free(&m);
    return sBad == 0 ? 0 : 1;
}
