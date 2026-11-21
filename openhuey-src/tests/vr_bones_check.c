/* The VR bones by outfit (src/game/vrbones.c): every costume's vr_bones.txt read through the mods
 * layer, resolved against that model's skeleton, and checked against the numbers the file names.
 *
 *   vr-bones-check <the mod's library folder> [<the game's library folder>]
 *
 * The mod's library is layered over the second (the game's own data), so a model the mod does not
 * carry is read from the game. Models that are in neither are skipped: the sample mod in the repo
 * carries FIN_000 only, the full fiona-vr-rig carries every costume.
 *
 * For each O_FIN/FIN_000, FIN_006, O_FIS/FIS_000, O_FIH, O_FIO, O_FIB, O_FIC and O_FIF, variants
 * 000 and 006: ok = 1, from_file = 1, the table's numbers are the file's (the FIN_000 numbering in
 * the brackets is not what is used), floor_y is the file's, and the wrist is the parent of the
 * mod's `arm left/right finger 1a`. FIN_000's numbers are also compared with the old hard-coded
 * ones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/files.h"
#include "../src/data/model.h"
#include "../src/data/modelgltf.h"
#include "../src/game/actor.h"
#include "../src/game/vrbones.h"

static int sBad, sChecked, sSkipped;

static void check(const char *what, int ok) {
    printf("vr-bones: %s -> %s\n", what, ok ? "pass" : "FAIL");
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
    ok = bin != NULL && modelgltf_read(m, (const char *)json, len, bin, bs, motions,
                                       motions != NULL ? ms : 0);
    free(json);
    free(bin);
    /* (motions is taken by modelgltf_read: model_free frees it) */
    return ok;
}

static float bind_y(const Mat4 *inv_bind) {
    float x = inv_bind->m[12], y = inv_bind->m[13], z = inv_bind->m[14];

    return -(inv_bind->m[1] * x + inv_bind->m[5] * y + inv_bind->m[9] * z);
}

typedef struct Expect {
    const char *prefix;   /* "O_FIN/FIN_" */
    const char *outfit;   /* "O_FIN" for the message */
    int fins;             /* 1: variants 000 and 006; 0: _000 only (the slip) */
    int waist, chest, neck, head, eye_l, eye_r;
    int l_arm[5], r_arm[5];   /* clavicle, upperarm, elbow, forearm, wrist */
} Expect;

static const Expect kExpect[] = {
    {"O_FIN/FIN_", "O_FIN", 1, 23, 24, 32, 33, 34, 35,
     {26, 27, 28, 29, 30}, {42, 43, 44, 45, 46}},
    {"O_FIS/FIS_", "O_FIS", 0, 43, 44, 52, 53, 54, 55,
     {46, 47, 48, 49, 50}, {62, 63, 64, 65, 66}},
    {"O_FIH/FIH_", "O_FIH", 1, 23, 24, 32, 33, 34, 35,
     {26, 27, 28, 29, 30}, {57, 58, 59, 60, 61}},
    {"O_FIO/FIO_", "O_FIO", 1, 19, 20, 28, 29, 30, 31,
     {22, 23, 24, 25, 26}, {53, 54, 55, 56, 57}},
    {"O_FIB/FIB_", "O_FIB", 1, 10, 11, 19, 20, 21, 22,
     {13, 14, 15, 16, 17}, {29, 30, 31, 32, 33}},
    {"O_FIC/FIC_", "O_FIC", 1, 10, 11, 21, 22, 23, 24,
     {15, 16, 17, 18, 19}, {31, 32, 33, 34, 35}},
    {"O_FIF/FIF_", "O_FIF", 1, 10, 11, 18, 19, 20, 21,
     {12, 13, 14, 15, 16}, {22, 23, 24, 25, 26}}
};

static int parent_of(const Model *m, int bone) {
    return bone >= 0 && bone < m->nbones ? m->bones[bone].parent : -1;
}

static void check_one(const Expect *e, int variant) {
    char name[64], what[240];
    Model m;
    Actor a;
    const VrBones *vb;
    int finger, i;

    snprintf(name, sizeof(name), "%s%03d", e->prefix, variant);
    memset(&m, 0, sizeof(m));
    if (!load_model(&m, name)) {
        sSkipped++;
        printf("vr-bones: %s not in this library -> skipped\n", name);
        return;
    }
    memset(&a, 0, sizeof(a));
    a.used = 1;
    snprintf(a.name, sizeof(a.name), "%s", name);
    a.model = m;
    vb = vrbones_for(&a);

    snprintf(what, sizeof(what), "%s is read from vr_bones.txt", name);
    check(what, vb->from_file == 1);
    snprintf(what, sizeof(what), "%s agrees with its skeleton (ok)", name);
    check(what, vb->ok == 1);
    snprintf(what, sizeof(what), "%s root 0 pelvis 1 legs 2..9", name);
    check(what, vb->bone[VRB_ROOT] == 0 && vb->bone[VRB_PELVIS] == 1 &&
               vb->bone[VRB_THIGH_L] == 2 && vb->bone[VRB_SHIN_L] == 3 &&
               vb->bone[VRB_FOOT_L] == 4 && vb->bone[VRB_TOE_L] == 5 &&
               vb->bone[VRB_THIGH_R] == 6 && vb->bone[VRB_SHIN_R] == 7 &&
               vb->bone[VRB_FOOT_R] == 8 && vb->bone[VRB_TOE_R] == 9);
    snprintf(what, sizeof(what), "%s waist %d chest %d neck %d head %d eyes %d/%d", name,
             e->waist, e->chest, e->neck, e->head, e->eye_l, e->eye_r);
    check(what, vb->bone[VRB_WAIST] == e->waist && vb->bone[VRB_CHEST] == e->chest &&
               vb->bone[VRB_NECK] == e->neck && vb->bone[VRB_HEAD] == e->head &&
               vb->bone[VRB_EYE_L] == e->eye_l && vb->bone[VRB_EYE_R] == e->eye_r);
    snprintf(what, sizeof(what), "%s L arm %d/%d/%d/%d/%d R arm %d/%d/%d/%d/%d", name,
             e->l_arm[0], e->l_arm[1], e->l_arm[2], e->l_arm[3], e->l_arm[4],
             e->r_arm[0], e->r_arm[1], e->r_arm[2], e->r_arm[3], e->r_arm[4]);
    for (i = 0; i < 5; i++) {
        if (vb->bone[VRB_CLAV_L + i] != e->l_arm[i] ||
            vb->bone[VRB_CLAV_R + i] != e->r_arm[i]) {
            check(what, 0);
            break;
        }
    }
    if (i == 5) {
        check(what, 1);
    }
    snprintf(what, sizeof(what), "%s floor_y is the model's floor (%.2f)", name,
             (double)vb->floor_y);
    check(what, vb->floor_y == 0.0f);
    /* the wrist's chain, the model's own (not the file's words) */
    snprintf(what, sizeof(what), "%s L wrist is its forearm's child (and so to the clavicle)", name);
    check(what, parent_of(&m, vb->bone[VRB_WRIST_L]) == vb->bone[VRB_FORE_L] &&
               parent_of(&m, vb->bone[VRB_FORE_L]) == vb->bone[VRB_ELBOW_L] &&
               parent_of(&m, vb->bone[VRB_ELBOW_L]) == vb->bone[VRB_UPPER_L] &&
               parent_of(&m, vb->bone[VRB_UPPER_L]) == vb->bone[VRB_CLAV_L]);
    snprintf(what, sizeof(what), "%s R wrist is its forearm's child (and so to the clavicle)", name);
    check(what, parent_of(&m, vb->bone[VRB_WRIST_R]) == vb->bone[VRB_FORE_R] &&
               parent_of(&m, vb->bone[VRB_FORE_R]) == vb->bone[VRB_ELBOW_R] &&
               parent_of(&m, vb->bone[VRB_ELBOW_R]) == vb->bone[VRB_UPPER_R] &&
               parent_of(&m, vb->bone[VRB_UPPER_R]) == vb->bone[VRB_CLAV_R]);
    finger = model_bone_named(&m, "arm left finger 1a");
    if (finger >= 0) {
        snprintf(what, sizeof(what), "%s arm left finger 1a hangs from the L wrist", name);
        check(what, parent_of(&m, finger) == vb->bone[VRB_WRIST_L]);
    }
    finger = model_bone_named(&m, "arm right finger 1a");
    if (finger >= 0) {
        snprintf(what, sizeof(what), "%s arm right finger 1a hangs from the R wrist", name);
        check(what, parent_of(&m, finger) == vb->bone[VRB_WRIST_R]);
    }
    /* the FIS/FIC note: bone 0 binds at the hip, so the floor must not be its bind y */
    if (strcmp(e->outfit, "O_FIS") == 0 || strcmp(e->outfit, "O_FIC") == 0) {
        float root_y = bind_y(&m.bones[vb->bone[VRB_ROOT]].inv_bind);

        snprintf(what, sizeof(what), "%s floor_y is not bone 0's bind y (%.2f)", name,
                 (double)root_y);
        check(what, root_y > 5.0f && vb->floor_y < 1.0f);
    }
    model_free(&m);
}

int main(int argc, char **argv) {
    size_t i;

    if (argc < 2) {
        fprintf(stderr, "vr-bones-check: the mod's library folder, please\n");
        return 2;
    }
    files_layers_clear();
    if (argc >= 3) {
        files_layer_add(argv[2]);   /* (the game's own library, under the mod) */
    }
    files_layer_add(argv[1]);
    for (i = 0; i < sizeof(kExpect) / sizeof(kExpect[0]); i++) {
        check_one(&kExpect[i], 0);
        if (kExpect[i].fins) {
            check_one(&kExpect[i], 6);
        }
    }
    printf("vr-bones: %d checks, %d bad, %d models skipped\n", sChecked, sBad, sSkipped);
    return sBad == 0 ? 0 : 1;
}
