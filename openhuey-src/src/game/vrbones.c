/* Which of her model's bones is which, for every costume (vrbones.h). */
#include "vrbones.h"
#include "../platform/vrlog.h"

#include "actor.h"

#include "../core/files.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FIN_000's numbering, the only one the VR code used to know. It is the fallback for a model of
 * exactly FIN's layout, and the numbers the in-game assertion compares a FIN_000 file against. */
static const int kFinBones[VRB_COUNT] = {
    /* root      pelvis     waist      chest      neck       head */
    0,           1,         23,        24,        32,        33,
    /* eye L     eye R */
    34,          35,
    /* clav L    upper L    elbow L    fore L     wrist L */
    26,          27,        28,        29,        30,
    /* clav R    upper R    elbow R    fore R     wrist R */
    42,          43,        44,        45,        46,
    /* thigh L   shin L     foot L     toe L */
    2,           3,         4,         5,
    /* thigh R   shin R     foot R     toe R */
    6,           7,         8,         9
};

typedef struct RoleName {
    const char *name;
    int role;
} RoleName;

/* the keys the file may write. `bust` and the shoulder pads are not roles the VR code asks for:
 * they are simply unknown keys and are ignored. */
static const RoleName kRoles[] = {
    {"root", VRB_ROOT},
    {"pelvis", VRB_PELVIS},
    {"waist", VRB_WAIST},
    {"chest", VRB_CHEST},
    {"neck", VRB_NECK},
    {"head", VRB_HEAD},
    {"L eye", VRB_EYE_L},
    {"R eye", VRB_EYE_R},
    {"L clavicle", VRB_CLAV_L},
    {"L upperarm", VRB_UPPER_L},
    {"L elbow", VRB_ELBOW_L},
    {"L forearm", VRB_FORE_L},
    {"L wrist", VRB_WRIST_L},
    {"R clavicle", VRB_CLAV_R},
    {"R upperarm", VRB_UPPER_R},
    {"R elbow", VRB_ELBOW_R},
    {"R forearm", VRB_FORE_R},
    {"R wrist", VRB_WRIST_R},
    {"L thigh", VRB_THIGH_L},
    {"L shin", VRB_SHIN_L},
    {"L foot", VRB_FOOT_L},
    {"L toe", VRB_TOE_L},
    {"R thigh", VRB_THIGH_R},
    {"R shin", VRB_SHIN_R},
    {"R foot", VRB_FOOT_R},
    {"R toe", VRB_TOE_R}
};

/* the roles a table must have for the VR code to use it (the eyes and toes may be absent: the
 * body falls back to the head for the eyes, and a foot solves without a toe) */
static const int kRequired[] = {
    VRB_ROOT, VRB_PELVIS, VRB_WAIST, VRB_CHEST, VRB_NECK, VRB_HEAD,
    VRB_CLAV_L, VRB_UPPER_L, VRB_ELBOW_L, VRB_FORE_L, VRB_WRIST_L,
    VRB_CLAV_R, VRB_UPPER_R, VRB_ELBOW_R, VRB_FORE_R, VRB_WRIST_R,
    VRB_THIGH_L, VRB_SHIN_L, VRB_FOOT_L,
    VRB_THIGH_R, VRB_SHIN_R, VRB_FOOT_R
};

/* one table, the model loaded now (only Fiona's is ever asked for) */
static VrBones sBones;
static int sHave;
static int sBonesCount, sVerts;
static int sFinCompared;

int vrbones_role_named(const char *name) {
    size_t i;

    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(kRoles) / sizeof(kRoles[0]); i++) {
        if (strcmp(name, kRoles[i].name) == 0) {
            return kRoles[i].role;
        }
    }
    return -1;
}

static void reset_table(void) {
    int i;

    memset(&sBones, 0, sizeof(sBones));
    for (i = 0; i < VRB_COUNT; i++) {
        sBones.bone[i] = -1;
    }
}

void vrbones_forget(void) {
    sHave = 0;
    sBonesCount = 0;
    sVerts = 0;
    reset_table();
}

/* trim the spaces (and a CR the line may carry) off both ends of a string in place */
static char *trim(char *s) {
    char *end;

    while (*s != 0 && isspace((unsigned char)*s)) {
        s++;
    }
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = 0;
    return s;
}

static int parent_of(const Model *m, int bone) {
    return bone >= 0 && bone < m->nbones ? m->bones[bone].parent : -1;
}


/* the root's bind y, from the inverse bind matrix (the same rigid inverse vrbody.c uses) */
static float bone_bind_y(const Mat4 *inv_bind) {
    float x = inv_bind->m[12], y = inv_bind->m[13], z = inv_bind->m[14];
    float floor = -(inv_bind->m[1] * x + inv_bind->m[5] * y + inv_bind->m[9] * z);

    return floor == 0.0f ? 0.0f : floor;   /* (-0.00 would print as -0.00) */
}

/* "bone 30" hangs from 29, 28, 27, 26 and 24: FIN_000's own arm, whatever else the model has */
static int fin_layout(const Model *m) {
    int b;

    if (m->nbones != 49) {
        return 0;
    }
    b = model_bone_named(m, "bone 30");
    return b == 30 && parent_of(m, 30) == 29 && parent_of(m, 29) == 28 &&
           parent_of(m, 28) == 27 && parent_of(m, 27) == 26 && parent_of(m, 26) == 24;
}

/* wrist -> forearm -> elbow -> upperarm -> clavicle, all present */
static int arm_chain_ok(const Model *m, int wrist, int fore, int elbow, int upper, int clav) {
    return wrist >= 0 && fore >= 0 && elbow >= 0 && upper >= 0 && clav >= 0 &&
           parent_of(m, wrist) == fore && parent_of(m, fore) == elbow &&
           parent_of(m, elbow) == upper && parent_of(m, upper) == clav;
}

static int required_present(void) {
    size_t i;

    for (i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); i++) {
        if (sBones.bone[kRequired[i]] < 0) {
            return 0;
        }
    }
    return 1;
}

static void log_table(void) {
    const VrBones *v = &sBones;

    VRLOG(
            "vrbones: %s %s - L arm %d/%d/%d/%d/%d R arm %d/%d/%d/%d/%d chest %d neck %d head %d"
            " eyes %d/%d floor %.2f (%s)\n",
            v->name, v->from_file ? "from vr_bones.txt" : "no vr_bones.txt (FIN layout)",
            v->bone[VRB_CLAV_L], v->bone[VRB_UPPER_L], v->bone[VRB_ELBOW_L], v->bone[VRB_FORE_L],
            v->bone[VRB_WRIST_L],
            v->bone[VRB_CLAV_R], v->bone[VRB_UPPER_R], v->bone[VRB_ELBOW_R], v->bone[VRB_FORE_R],
            v->bone[VRB_WRIST_R],
            v->bone[VRB_CHEST], v->bone[VRB_NECK], v->bone[VRB_HEAD],
            v->bone[VRB_EYE_L], v->bone[VRB_EYE_R], (double)v->floor_y,
            v->ok ? "ok" : "not ok");
}

/* FIN_000's numbers must come out exactly as they did before the per-outfit tables: log the first
 * comparison of the fallback numbers with the file's. */
static void compare_fin_once(void) {
    static const int role_order[] = {
        VRB_ROOT, VRB_PELVIS, VRB_WAIST, VRB_CHEST, VRB_NECK, VRB_HEAD, VRB_EYE_L, VRB_EYE_R,
        VRB_CLAV_L, VRB_UPPER_L, VRB_ELBOW_L, VRB_FORE_L, VRB_WRIST_L,
        VRB_CLAV_R, VRB_UPPER_R, VRB_ELBOW_R, VRB_FORE_R, VRB_WRIST_R,
        VRB_THIGH_L, VRB_SHIN_L, VRB_FOOT_L, VRB_TOE_L,
        VRB_THIGH_R, VRB_SHIN_R, VRB_FOOT_R, VRB_TOE_R
    };
    size_t i;
    int same = 1;

    if (sFinCompared || strcmp(sBones.name, "O_FIN/FIN_000") != 0) {
        return;
    }
    sFinCompared = 1;
    for (i = 0; i < sizeof(role_order) / sizeof(role_order[0]); i++) {
        int b = sBones.bone[role_order[i]];

        if (b >= 0 && b != kFinBones[role_order[i]]) {
            same = 0;
            VRLOG( "vrbones: FIN_000 assertion: role %d is %d, was %d\n",
                    role_order[i], b, kFinBones[role_order[i]]);
        }
    }
    VRLOG(
            "vrbones: FIN_000 old and new indices are %s - root %d pelvis %d waist %d chest %d"
            " neck %d head %d eyes %d/%d L arm %d/%d/%d/%d/%d R arm %d/%d/%d/%d/%d\n",
            same ? "identical" : "DIFFERENT",
            sBones.bone[VRB_ROOT], sBones.bone[VRB_PELVIS], sBones.bone[VRB_WAIST],
            sBones.bone[VRB_CHEST], sBones.bone[VRB_NECK], sBones.bone[VRB_HEAD],
            sBones.bone[VRB_EYE_L], sBones.bone[VRB_EYE_R],
            sBones.bone[VRB_CLAV_L], sBones.bone[VRB_UPPER_L], sBones.bone[VRB_ELBOW_L],
            sBones.bone[VRB_FORE_L], sBones.bone[VRB_WRIST_L],
            sBones.bone[VRB_CLAV_R], sBones.bone[VRB_UPPER_R], sBones.bone[VRB_ELBOW_R],
            sBones.bone[VRB_FORE_R], sBones.bone[VRB_WRIST_R]);
}


/* the file's own chained skeleton must agree with the names it wrote. On a mismatch the table is
 * not used at all: the arm IK and the VR body are skipped rather than twisting her. */
static void cross_check(const Model *m) {
    int finger, mismatch = 0;

    if (!arm_chain_ok(m, sBones.bone[VRB_WRIST_L], sBones.bone[VRB_FORE_L],
                      sBones.bone[VRB_ELBOW_L], sBones.bone[VRB_UPPER_L],
                      sBones.bone[VRB_CLAV_L])) {
        mismatch = 1;
    }
    if (!arm_chain_ok(m, sBones.bone[VRB_WRIST_R], sBones.bone[VRB_FORE_R],
                      sBones.bone[VRB_ELBOW_R], sBones.bone[VRB_UPPER_R],
                      sBones.bone[VRB_CLAV_R])) {
        mismatch = 1;
    }
    /* the mod's finger 1a hangs from the wrist: the one check that ties the file's wrist to the
     * finger bones the grip poses find by name */
    finger = model_bone_named(m, "arm left finger 1a");
    if (finger >= 0 && parent_of(m, finger) != sBones.bone[VRB_WRIST_L]) {
        mismatch = 1;
    }
    finger = model_bone_named(m, "arm right finger 1a");
    if (finger >= 0 && parent_of(m, finger) != sBones.bone[VRB_WRIST_R]) {
        mismatch = 1;
    }
    if (mismatch) {
        sBones.ok = 0;
        VRLOG(
                "vrbones: %s vr_bones.txt does not match the skeleton (L arm %d/%d/%d/%d/%d,"
                " R arm %d/%d/%d/%d/%d): arm IK and VR body off\n",
                sBones.name,
                sBones.bone[VRB_CLAV_L], sBones.bone[VRB_UPPER_L], sBones.bone[VRB_ELBOW_L],
                sBones.bone[VRB_FORE_L], sBones.bone[VRB_WRIST_L],
                sBones.bone[VRB_CLAV_R], sBones.bone[VRB_UPPER_R], sBones.bone[VRB_ELBOW_R],
                sBones.bone[VRB_FORE_R], sBones.bone[VRB_WRIST_R]);
    }
}

/* `role = bone name   [FIN_000]` (the bracket and anything after '[' ignored) */
static int parse_line(char *line, const Model *m) {
    char *eq, *role, *value, *cut;
    int r;

    cut = strchr(line, '#');
    if (cut != NULL) {
        *cut = 0;
    }
    line = trim(line);
    if (*line == 0) {
        return 0;
    }
    eq = strchr(line, '=');
    if (eq == NULL) {
        return 0;
    }
    *eq = 0;
    role = trim(line);
    value = trim(eq + 1);
    cut = strchr(value, '[');
    if (cut != NULL) {
        *cut = 0;
        value = trim(value);
    }
    if (*role == 0 || *value == 0) {
        return 0;
    }
    if (strcmp(role, "floor_y") == 0) {
        char *end = NULL;
        float y = strtof(value, &end);

        if (end != value && end != NULL && *end == 0) {
            sBones.floor_y = y;
        }
        return 1;
    }
    r = vrbones_role_named(role);
    if (r < 0) {
        return 0;   /* (model, game_bones, bones, bust, shoulder pads: not ours) */
    }
    sBones.bone[r] = model_bone_named(m, value);
    return 1;
}

static void parse_text(char *text, const Model *m) {
    char *p = text;

    while (*p != 0) {
        char *line = p;
        char *nl = strchr(p, '\n');

        if (nl != NULL) {
            *nl = 0;
            p = nl + 1;
        } else {
            p += strlen(p);
        }
        parse_line(line, m);
    }
}

/* no file: only a model of FIN_000's exact layout may keep today's numbers */
static void fallback_fin(const Model *m) {
    int i;

    if (!fin_layout(m)) {
        sBones.ok = 0;
        return;
    }
    for (i = 0; i < VRB_COUNT; i++) {
        sBones.bone[i] = kFinBones[i];
    }
    sBones.floor_y = bone_bind_y(&m->bones[kFinBones[VRB_ROOT]].inv_bind);
    sBones.ok = 1;
}

const VrBones *vrbones_for(const struct Actor *a) {
    const Model *m;
    char path[160];
    uint8_t *data;
    size_t size = 0;

    if (a == NULL) {
        static VrBones none;
        return &none;
    }
    m = &a->model;
    if (sHave && strcmp(sBones.name, a->name) == 0 && sBonesCount == m->nbones &&
        sVerts == m->nv) {
        return &sBones;
    }
    reset_table();
    snprintf(sBones.name, sizeof(sBones.name), "%s", a->name);
    sBonesCount = m->nbones;
    sVerts = m->nv;
    sHave = 1;

    snprintf(path, sizeof(path), "%s.model/vr_bones.txt", a->name);
    data = files_read(path, &size);
    if (data != NULL) {
        (void)size;
        sBones.from_file = 1;
        parse_text((char *)data, m);
        free(data);
        sBones.ok = required_present();
        cross_check(m);
        if (sBones.ok) {
            compare_fin_once();
        }
    } else {
        fallback_fin(m);
        if (sBones.ok) {
            compare_fin_once();
        }
    }
    if (!sBones.ok && !sBones.from_file) {
        VRLOG(
                "vrbones: %s has no vr_bones.txt and is not the FIN layout: arm IK and VR body off\n",
                sBones.name);
    } else {
        log_table();
    }
    return &sBones;
}
