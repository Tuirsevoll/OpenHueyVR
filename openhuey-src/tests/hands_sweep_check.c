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
 * by hand here. A ladder's grab zone is asked too, and the pull to climb (ladder_pull_step): a
 * hand's play-space motion, pulled down 2 giving the body -2 (up) and the anchor following it,
 * pushed up 2 giving +2, and a hand held still - or asked again where it already is - giving none.
 *
 *   hands-sweep-check */
#include <stdio.h>
#include <string.h>

#include "../src/core/files.h"
#include "../src/data/modelgltf.h"
#include "../src/game/engine.h"
#include "../src/game/loot.h"
#include "../src/game/placed.h"
#include "../src/game/progress.h"
#include "../src/game/room.h"
#include "../src/game/vrbody.h"
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

int loot_item_of(int index) {
    (void)index;
    return -1;
}

void loot_preload_held(int item, int side) {
    (void)item;
    (void)side;
}

void loot_draw_held(int item, int side, const Mat4 *world, const Mat4 *view_proj) {
    (void)item;
    (void)side;
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

/* ---- the headset session, as a check may ask for it (off by default: the sweeps need none) ---- */
static int sVrOn;
static Vec3 sVrEye, sVrHeadPos, sVrHeadFwd, sVrHeadUp;
static float sVrYaw, sVrPitch;
static Vec3 sVrHand[2];
static int sVrTracked[2];

int vr_active(void) { return sVrOn; }

/* the VR options (platform/vr.c): a body of zeroes - no seated play, no first-person head */
VrOptions gVrOptions;

static float sVrBodyHeight = 1.6f, sVrBodyScale = 1.0f;   /* (the calibration's pair) */
void vr_body(float *height_m, float *scale) {
    if (height_m != NULL) {
        *height_m = sVrBodyHeight;
    }
    if (scale != NULL) {
        *scale = sVrBodyScale;
    }
}

float vr_crouch_fraction(void) { return 0.0f; }

int vr_head_at(Vec3 *out) {
    if (!sVrOn || out == NULL) {
        return 0;
    }
    *out = sVrHeadPos;
    return 1;
}

int vr_head_look(float *pitch, Vec3 *forward, Vec3 *up) {
    if (!sVrOn) {
        return 0;
    }
    if (pitch != NULL) {
        *pitch = sVrPitch;
    }
    if (forward != NULL) {
        *forward = sVrHeadFwd;
    }
    if (up != NULL) {
        *up = sVrHeadUp;
    }
    return 1;
}

int vr_anchor_get(Vec3 *eye, float *yaw) {
    if (!sVrOn) {
        return 0;
    }
    if (eye != NULL) {
        *eye = sVrEye;
    }
    if (yaw != NULL) {
        *yaw = sVrYaw;
    }
    return 1;
}

int vr_controller(int hand, VrController *out) {
    if (!sVrOn || out == NULL || hand < 0 || hand > 1 || !sVrTracked[hand]) {
        return 0;
    }
    out->tracked = 1;
    out->at = sVrHand[hand];
    out->right = vec3(1.0f, 0.0f, 0.0f);
    out->up = vec3(0.0f, 1.0f, 0.0f);
    out->forward = vec3(0.0f, 0.0f, -1.0f);
    out->grip = 0.0f;
    out->index = 0.0f;
    out->thumb = 1.0f;
    return 1;
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

/* ---- the arms posed (vrhands_pose): a bug that left them at the animation's pose was invisible
 * to every check - the hand went to the controller, the shoulders did not. A model with the arms'
 * bones named (the mod's own, examples/mods/fiona-vr-rig) is posed with a hand a side in front of
 * her, and the arm's bones must leave the pose the animation had them in. ---- */
static Model sArmModel;
static Actor sArmActor;
static Mat4 sRest[MODEL_MAX_BONES], sPosed[MODEL_MAX_BONES], sSkin[MODEL_MAX_BONES];
static int sRestN;
static int sFails;   /* (a FAIL anywhere in this file) */

static void capture(Mat4 *world, int n, void *user) {
    (void)user;
    sRestN = n;
    memcpy(sRest, world, sizeof(Mat4) * (size_t)n);
}
static int load_arm_model(const char *name) {
    char path[160];
    uint8_t *json, *bin, *motions = NULL;
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
    ok = bin != NULL && modelgltf_read(&sArmModel, (const char *)json, len, bin, bs, motions, motions != NULL ? ms : 0);
    free(json);
    free(bin);
    return ok;
}
static float bone_dist(const Mat4 *a, const Mat4 *b) {
    return sqrtf((a->m[12] - b->m[12]) * (a->m[12] - b->m[12]) +
                 (a->m[13] - b->m[13]) * (a->m[13] - b->m[13]) +
                 (a->m[14] - b->m[14]) * (a->m[14] - b->m[14]));
}

static void check_arm_pose(const char *library) {
    static Mat4 place, unplace;
    float hand[2][3], from[3];
    int side, sh, el, wr;

    files_layers_clear();
    files_layer_add(library);
    if (!load_arm_model("O_FIN/FIN_000")) {
        fprintf(stderr, "hands: can't read O_FIN/FIN_000 from %s: the arms are not posed here\n", library);
        return;
    }
    memset(&sArmActor, 0, sizeof(sArmActor));
    sArmActor.model = sArmModel;
    sArmActor.used = 1;
    sArmActor.visible = 1;
    sArmActor.scale = 1.0f;
    sArmActor.head.head = model_bone_named(&sArmModel, "bone 33");
    sArmActor.head.neck = model_bone_named(&sArmModel, "bone 23");
    snprintf(sArmActor.name, sizeof(sArmActor.name), "O_FIN/FIN_000");
    place = unplace = mat4_identity();

    /* the animation's pose, as the hook gets it */
    model_pose_hook(capture, NULL);
    model_pose_blend(&sArmModel, -1, 0.0f, -1, 0.0f, 0.0f, sSkin, NULL);
    model_pose_hook(NULL, NULL);
    memcpy(sPosed, sRest, sizeof(Mat4) * (size_t)sRestN);

    /* a session: her eyes level, her left is the room's +x, a hand a side in front of her */
    gVrOptions.cam_back = 0.06f;   /* (vr.c's defaults: the camera inside her head, in her space) */
    gVrOptions.cam_up = 0.01f;
    sVrOn = 1;
    sVrYaw = 3.14159265f;   /* (the play space's -z is her +z) */
    sVrEye = vec3(0.0f, 16.6f, 0.0f);
    sVrHeadPos = sVrEye;
    sVrHeadFwd = vec3(0.0f, 0.0f, -1.0f);
    sVrHeadUp = vec3(0.0f, 1.0f, 0.0f);
    sVrPitch = 0.0f;
    sVrTracked[0] = sVrTracked[1] = 1;
    sVrHand[0] = vec3(-2.2f, -1.0f, -3.6f);   /* (play space: her left, below, in front) */
    sVrHand[1] = vec3(2.2f, -1.0f, -3.6f);
    hand[0][0] = sVrEye.x + 2.2f;   /* (room: the anchor's yaw pi mirrors x and z) */
    hand[0][1] = sVrEye.y - 1.0f;
    hand[0][2] = sVrEye.z + 3.6f;
    hand[1][0] = sVrEye.x - 2.2f;
    hand[1][1] = sVrEye.y - 1.0f;
    hand[1][2] = sVrEye.z + 3.6f;

    vrhands_pose(&sArmActor, sPosed, &place, &unplace);

    /* One body, three calibrated heights: everything of hers is in her own space, so her
     * shoulder and eye must not move at all - the world's scale has already taken the player's
     * size into hers. */
    {
        static const float heights[3] = {1.60f, 1.63f, 1.74f};
        float first_sh[3] = {0.0f, 0.0f, 0.0f}, first_ey[3] = {0.0f, 0.0f, 0.0f};
        float worst_sh = 0.0f, worst_ey = 0.0f;
        int k, c, ub = vrbody_bone(VRBODY_UPPER, 0), eb = model_bone_named(&sArmModel, "bone 34");

        for (k = 0; k < 3; k++) {
            float sh[3], ey[3];

            sVrBodyHeight = heights[k];
            sVrBodyScale = 1.6f / heights[k];   /* (vr_calibrate's own world scale) */
            memcpy(sPosed, sRest, sizeof(Mat4) * (size_t)sRestN);
            vrbody_idle();
            vrbody_begin(&sArmActor, sPosed, &place, &unplace);
            vrbody_body(&sArmActor, sPosed, &place, &unplace);
            vrbody_shoulders(&sArmActor, sPosed, &place, &unplace, 0, hand[0]);
            vrbody_head_camera(&sArmActor, sPosed, &place, &unplace);
            for (c = 0; c < 3; c++) {
                sh[c] = sPosed[ub].m[12 + c];
                ey[c] = eb >= 0 ? sPosed[eb].m[12 + c] : sPosed[vrbody_bone(VRBODY_HEAD, 0)].m[12 + c];
            }
            if (k == 0) {
                for (c = 0; c < 3; c++) {
                    first_sh[c] = sh[c];
                    first_ey[c] = ey[c];
                }
            } else {
                for (c = 0; c < 3; c++) {
                    worst_sh = fmaxf(worst_sh, fabsf(sh[c] - first_sh[c]));
                    worst_ey = fmaxf(worst_ey, fabsf(ey[c] - first_ey[c]));
                }
            }
            printf("hands: at %.2f m - the shoulder %.4f %.4f %.4f, the eye %.4f %.4f %.4f (arm share %.3f)\n",
                   (double)heights[k], (double)sh[0], (double)sh[1], (double)sh[2], (double)ey[0],
                   (double)ey[1], (double)ey[2],
                   (double)(vrbody_arm_scale(&sArmActor) / vrbody_body_scale(&sArmActor)));
        }
        printf("hands: three heights, the shoulder moved %.5f and the eye %.5f at the most\n",
               (double)worst_sh, (double)worst_ey);
        if (worst_sh > 1.0e-3f || worst_ey > 1.0e-3f) {
            printf("hands: the body is not height-independent (FAIL)\n");
            sFails++;
        }
        sVrBodyHeight = 1.6f;
        sVrBodyScale = 1.0f;
        memcpy(sPosed, sRest, sizeof(Mat4) * (size_t)sRestN);
        vrhands_pose(&sArmActor, sPosed, &place, &unplace);   /* (back to the pose under test) */
    }

    for (side = 0; side < 2; side++) {
        const char *l = side == 0 ? "left" : "right";
        float turned;

        sh = model_bone_named(&sArmModel, side == 0 ? "bone 27" : "bone 43");
        el = model_bone_named(&sArmModel, side == 0 ? "bone 28" : "bone 44");
        wr = model_bone_named(&sArmModel, side == 0 ? "bone 30" : "bone 46");
        if (sh < 0 || el < 0 || wr < 0) {
            printf("hands: the %s arm's bones are not in this model (FAIL)\n", l);
            sFails++;
            continue;
        }
        /* the shoulder joint's own place barely moves - it is the joint - so the proof the arm was
         * posed is its own turn and the elbow's place */
        turned = sPosed[sh].m[0] * sRest[sh].m[0] + sPosed[sh].m[1] * sRest[sh].m[1] +
                 sPosed[sh].m[2] * sRest[sh].m[2];
        printf("hands: %s arm - shoulder turned %.2f, elbow moved %.2f, hand at the controller %.2f away\n",
               l, (double)(1.0f - turned), (double)bone_dist(&sPosed[el], &sRest[el]),
               (double)sqrtf((sPosed[wr].m[12] - hand[side][0]) * (sPosed[wr].m[12] - hand[side][0]) +
                             (sPosed[wr].m[13] - hand[side][1]) * (sPosed[wr].m[13] - hand[side][1]) +
                             (sPosed[wr].m[14] - hand[side][2]) * (sPosed[wr].m[14] - hand[side][2])));
        if (turned > 0.999f || bone_dist(&sPosed[el], &sRest[el]) < 0.5f) {
            printf("hands: the %s arm was left at the animation's pose (FAIL)\n", l);
            sFails++;
        }
        from[0] = sPosed[wr].m[12] - hand[side][0];
        from[1] = sPosed[wr].m[13] - hand[side][1];
        from[2] = sPosed[wr].m[14] - hand[side][2];
        if (from[0] * from[0] + from[1] * from[1] + from[2] * from[2] > 3.0f * 3.0f) {
            printf("hands: the %s hand is not at its controller (FAIL)\n", l);
            sFails++;
        }
        /* the elbow must be the hand's arm's elbow: about a forearm from the hand, not left where
         * the animation (or the clavicle's little turn) had it */
        printf("hands: %s arm - elbow to hand %.2f\n", l, (double)bone_dist(&sPosed[el], &sPosed[wr]));
        if (bone_dist(&sPosed[el], &sPosed[wr]) > 3.5f) {
            printf("hands: the %s elbow is not where the hand's arm solved it (FAIL)\n", l);
            sFails++;
        }
        /* her arm's root is her own, only turned toward the player's: the clavicle keeps its length,
         * so the shoulder never leaves her body by more than the 0.15 m the placement allows */
        {
            int ub = vrbody_bone(VRBODY_UPPER, side);
            float u = 15.651f / 1.6f;   /* (her rest eye height / the player's, the calibration's) */
            float dx = sPosed[ub].m[12] - sRest[ub].m[12];
            float dy = sPosed[ub].m[13] - sRest[ub].m[13];
            float dz = sPosed[ub].m[14] - sRest[ub].m[14];
            float d = sqrtf(dx * dx + dy * dy + dz * dz);

            printf("hands: %s shoulder - moved %.3f m from her own (the most: 0.150)\n", l,
                   (double)(d / u));
            if (d / u > 0.16f) {
                printf("hands: the %s shoulder left her body (FAIL)\n", l);
                sFails++;
            }
        }
        /* the hand is the one size the IK has always drawn: the arm's and the body's scales stop
         * at the wrist, so it and a finger come out the same (and not the forearm's) */
        {
            int hb = vrbody_bone(VRBODY_HAND, side), fb = -1, c2;
            float nh, nf = 0.0f, x, y, z;

            x = sPosed[hb].m[0];
            y = sPosed[hb].m[1];
            z = sPosed[hb].m[2];
            nh = sqrtf(x * x + y * y + z * z);
            for (c2 = 0; c2 < sArmModel.nbones; c2++) {
                if (sArmModel.bones[c2].parent == hb) {
                    fb = c2;
                    break;
                }
            }
            if (fb >= 0) {
                x = sPosed[fb].m[0];
                y = sPosed[fb].m[1];
                z = sPosed[fb].m[2];
                nf = sqrtf(x * x + y * y + z * z);
            }
            printf("hands: %s hand - the wrist %.3f, a finger %.3f (one hand's own size)\n",
                   l, (double)nh, (double)(fb >= 0 ? nf : nh));
            if (fabsf(nh - (fb >= 0 ? nf : nh)) > 0.02f) {
                printf("hands: the %s hand is not one size through (FAIL)\n", l);
                sFails++;
            }
        }
    }
}

/* one placed object of the room, its mesh `v` in its own space, unturned and at the origin */
static void placed_of(PlacedSet *s, MeshVertex *v, int nv) {
    memset(s, 0, sizeof(*s));
    s->n = 1;
    s->p[0].shown = 1;
    s->p[0].scale = 1.0f;
    s->p[0].mesh.v = v;
    s->p[0].mesh.nv = nv;
}

static void want(const char *what, int got, int expected) {
    if (got == expected) {
        printf("hands: %s -> %d (pass)\n", what, got);
    } else {
        printf("hands: %s -> %d, wanted %d (FAIL)\n", what, got, expected);
        sFails++;
    }
}

int main(int argc, char **argv) {
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
    /* the fingertip: the wrist's own path stays clear of the wall, the tip (wrist + off) reaches it.
     * `off` is a hand's length (about 3.5 units) along fwd, pointing at the wall. */
    {
        Vec3 off = vec3(-3.5f, 0.0f, 0.0f);
        Vec3 from = vec3(5.0f, 1.5f, 0.0f);
        Vec3 to = vec3(3.7f, 1.5f, 0.0f);
        Vec3 grip = path_stop(&wall, from, to, 0.40f);                          /* (the wrist: clear) */
        Vec3 tip = path_stop(&wall, vec3_add(from, off), vec3_add(to, off), 0.40f);
        Vec3 stop = vec3_sub(tip, off);                                        /* (taken back to the wrist) */
        Vec3 again = vec3_sub(path_stop(&wall, vec3_add(stop, off), vec3_add(to, off), 0.40f), off);

        want("the wrist's own path is clear of the wall", grip.x == to.x && grip.z == to.z, 1);
        want("a fingertip reaching the wall stops the wrist",
             fabsf(tip.x) >= 0.35f && fabsf(tip.x) <= 0.45f && stop.x >= to.x, 1);
        want("asking again from the fingertip's stop gives the same stop",
             fabsf(again.x - stop.x) <= 0.001f && fabsf(again.y - stop.y) <= 0.001f &&
                 fabsf(again.z - stop.z) <= 0.001f, 1);
    }
    {
        /* both the wrist and the fingertip stay clear */
        Vec3 off = vec3(-3.5f, 0.0f, 0.0f);
        Vec3 from = vec3(6.0f, 1.5f, 0.0f);
        Vec3 to = vec3(5.5f, 1.5f, 0.0f);
        Vec3 grip = path_stop(&wall, from, to, 0.40f);
        Vec3 tip = vec3_sub(path_stop(&wall, vec3_add(from, off), vec3_add(to, off), 0.40f), off);
        Vec3 stop = vec3_len(vec3_sub(from, tip)) < vec3_len(vec3_sub(from, grip)) ? tip : grip;

        want("a hand clear at wrist and fingertip is let through", stop.x == to.x && stop.z == to.z, 1);
    }
    /* the slide (path_slide): a hand pushing at an angle keeps the sideways part of its motion */
    {
        Vec3 from = vec3(2.0f, 1.5f, 0.0f);
        Vec3 to = vec3(-0.5f, 1.5f, 1.0f);            /* (forward 2.5, sideways 1.0) */
        Vec3 first = path_stop(&wall, from, to, 0.40f);
        Vec3 slide = path_slide(&wall, from, to, 0.40f);
        Vec3 again = path_slide(&wall, slide, to, 0.40f);

        want("a hand pushing at an angle still stops 0.40 from the wall",
             fabsf(slide.x - 0.40f) <= 0.05f, 1);
        want("and its sideways motion is kept, not left at the first stop",
             slide.z >= 0.95f && slide.z > first.z + 0.1f, 1);
        want("asking again from the slide's point gives the same point",
             fabsf(again.x - slide.x) <= 0.001f && fabsf(again.y - slide.y) <= 0.001f &&
                 fabsf(again.z - slide.z) <= 0.001f, 1);
    }
    {
        /* straight in: nothing sideways, so it stops dead */
        Vec3 slide = path_slide(&wall, vec3(2.0f, 1.5f, 0.0f), vec3(0.0f, 1.5f, 0.0f), 0.40f);

        want("a hand pushing straight in stops dead, with no sideways move",
             fabsf(slide.x - 0.40f) <= 0.05f && fabsf(slide.z) <= 0.001f, 1);
    }
    {
        /* parallel to the wall, a metre out: let through */
        Vec3 to = vec3(1.0f, 1.5f, 1.0f);
        Vec3 slide = path_slide(&wall, vec3(1.0f, 1.5f, -1.0f), to, 0.40f);

        want("a hand moving parallel to the wall is let through", slide.x == to.x && slide.z == to.z, 1);
    }
    {
        /* a fast hand crossing the wall in one move: still stopped in front of it */
        Vec3 slide = path_slide(&wall, vec3(-3.0f, 1.5f, 0.0f), vec3(3.0f, 1.5f, 0.0f), 0.40f);

        want("a fast hand crossing the wall in one move stays in front of it",
             slide.x <= -0.35f && fabsf(slide.x + 0.40f) <= 0.05f, 1);
    }
    /* a ladder's grab zone (vrhands_ladder_link): a link's two spots - its foot at y 20 (the lower
     * level: the game's y runs down) and its top at y 0, ten across the floor, so the line a hand
     * is near slants. The link's triangles are not read by the grab. */
    {
        static NavMesh ladder;

        memset(&ladder, 0, sizeof(ladder));
        ladder.nlinks = 1;
        ladder.links[0].tri[0] = ladder.links[0].tri[1] = -1;
        ladder.links[0].spot[0] = vec3(0.0f, 20.0f, 0.0f);
        ladder.links[0].spot[1] = vec3(10.0f, 0.0f, 0.0f);
        want("a hand beside a ladder's middle is in its grab zone",
             vrhands_ladder_link(&ladder, vec3(5.0f, 10.0f, 4.0f)), 0);
        want("a hand 12 units from the ladder's middle is not",
             vrhands_ladder_link(&ladder, vec3(5.0f, 10.0f, 12.0f)), -1);
        want("a hand above the top spot's reach is not",
             vrhands_ladder_link(&ladder, vec3(10.0f, -6.0f, 0.0f)), -1);
        want("a hand at the foot spot's height is in its grab zone",
             vrhands_ladder_link(&ladder, vec3(2.0f, 20.0f, 0.0f)), 0);
    }
    /* the pull to climb (ladder_pull_step): the play-space y grows as the hand is pulled down, and a
     * rise of it moves her body up - the body's y going down, the game's y running the other way - so
     * a hand pulled down 2 (play y 10 -> 12) gives the body -2, and one pushed up 2 gives +2. The
     * anchor follows the hand, so the same place again moves nothing. */
    {
        Vec3 anchor = vec3(0.0f, 10.0f, 0.0f);
        Vec3 play = vec3(0.0f, 12.0f, 0.0f);   /* (the hand pulled down by 2: the play-space y grows) */
        float dy = ladder_pull_step(&anchor, play);

        want("a hand pulled down 2 gives a body move of -2 (her up)", fabsf(dy + 2.0f) <= 1e-5f, 1);
        want("and the anchor has followed the hand", fabsf(anchor.y - 12.0f) <= 1e-5f, 1);
        want("the same hand place again gives no move", fabsf(ladder_pull_step(&anchor, play)) <= 1e-5f, 1);
        want("and the anchor is the hand's place still", fabsf(anchor.y - 12.0f) <= 1e-5f, 1);
    }
    {
        Vec3 anchor = vec3(0.0f, 10.0f, 0.0f);
        Vec3 play = vec3(0.0f, 8.0f, 0.0f);   /* (the hand pushed up by 2) */
        float dy = ladder_pull_step(&anchor, play);

        want("a hand pushed up 2 gives a body move of +2 (her down)", fabsf(dy - 2.0f) <= 1e-5f, 1);
        want("and the anchor has followed the hand", fabsf(anchor.y - 8.0f) <= 1e-5f, 1);
    }
    {
        Vec3 anchor = vec3(0.0f, 10.0f, 0.0f);
        Vec3 play = vec3(0.0f, 10.0f, 0.0f);   /* (held still) */

        want("a hand held still gives no move", fabsf(ladder_pull_step(&anchor, play)) <= 1e-5f, 1);
    }
    printf("hands: faces counted: %d of 2 (the wall), %d of 1 (the floor), %d of 1 (the table top)\n",
           face_tris(&wall), face_tris(&floor), face_tris(&table));
    if (argc > 1) {   /* (the mod's library: the arms posed, as the game poses them) */
        check_arm_pose(argv[1]);
    }
    if (sFails != 0) {
        fprintf(stderr, "FAIL: %d of the sweep's tests\n", sFails);
        return 1;
    }
    printf("hands: the sweeps find a wall, a floor, a table top, a door's leaf or a placed thing\n");
    return 0;
}
