/* Which of her model's bones is which, for every costume (vr_bones.txt).
 *
 * Fiona's model changes with her clothes and the story (scripts/fiona.fs `wear`): O_FIN, O_FIS,
 * O_FIH, O_FIO, O_FIB, O_FIC and O_FIF, each with variants _000.._006. Every one names its game
 * bones `bone 0`, `bone 1`, ..., but the numbers mean different body parts per outfit because the
 * cloth and skirt bones come first. The fiona-vr-rig mod gives every outfit the same 30 finger
 * bones (`arm left/right finger 1a..5c`) and the 9 `vr ...` helpers, and writes a
 * `<model>.model/vr_bones.txt` beside each model:
 *
 *   # comment lines start with '#'
 *   model = O_FIS/FIS_000
 *   game_bones = 68
 *   bones = 107
 *   floor_y = 0.0
 *   root = bone 0   [bone 0]
 *   pelvis = bone 1   [bone 1]
 *   L wrist = bone 50   [bone 30]
 *   ...
 *
 * Each line is `<role> = <bone name>`, with the FIN_000 equivalent in `[...]` (ignored). Unknown
 * keys and roles are ignored. `floor_y` is the model-space floor height: in FIS and FIC `bone 0`
 * binds at the hip height, not at the floor, so the eye height is measured from `floor_y`.
 *
 * `vrbones_for` reads the file through the files layer (a mod's copy wins), resolves each name with
 * model_bone_named, and cross-checks the result against the skeleton's own parent chains. With no
 * file it falls back to FIN_000's numbers only for a model of FIN's exact layout; every other model
 * is `ok = 0` and the arm IK / VR body are skipped (her own animation plays, nothing twists). */
#ifndef VRBONES_H
#define VRBONES_H

/* the roles a model can name. The order is the table's; `side` in the body is 0 her left, 1 her
 * right, and the roles keep that order so a side index picks the L or R entry. */
typedef enum {
    VRB_ROOT,
    VRB_PELVIS,
    VRB_WAIST,
    VRB_CHEST,
    VRB_NECK,
    VRB_HEAD,
    VRB_EYE_L,
    VRB_EYE_R,
    VRB_CLAV_L,
    VRB_UPPER_L,
    VRB_ELBOW_L,
    VRB_FORE_L,
    VRB_WRIST_L,
    VRB_CLAV_R,
    VRB_UPPER_R,
    VRB_ELBOW_R,
    VRB_FORE_R,
    VRB_WRIST_R,
    VRB_THIGH_L,
    VRB_SHIN_L,
    VRB_FOOT_L,
    VRB_TOE_L,
    VRB_THIGH_R,
    VRB_SHIN_R,
    VRB_FOOT_R,
    VRB_TOE_R,
    VRB_COUNT
} VrBoneRole;

typedef struct VrBones {
    int bone[VRB_COUNT];   /* the model's bone for each role, -1 when it has none */
    float floor_y;         /* the model-space floor height (FIS and FIC: not bone 0's bind y) */
    int from_file;         /* 1: the table came from <model>.model/vr_bones.txt */
    int ok;                /* 1: the table is complete and agrees with the skeleton */
    char name[64];         /* the model it was read for (e.g. "O_FIS/FIS_000") */
} VrBones;

struct Actor;

/* The table for this actor's model, cached by its name, bone count and vertex count; rebuilt when
 * any of them changes (wear frees and loads a new actor, so the name changes). Never NULL. */
const VrBones *vrbones_for(const struct Actor *a);

/* forget the cached table: the next vrbones_for reads its file afresh (a costume change, a new
 * game, a Fiona model loaded or freed) */
void vrbones_forget(void);

/* The role a vr_bones.txt key names ("L wrist", "waist", ...), or -1 for an unknown key. Public for
 * the checks and the console's `vrbones` command. */
int vrbones_role_named(const char *name);

#endif
