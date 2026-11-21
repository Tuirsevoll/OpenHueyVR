/* Mod finger grip poses (PROPS/grip_poses.txt): the hand's finger bones turned and moved to the
 * shape a held item's grip names. The file is read through the files layer, so a mod's
 * library/data/PROPS/grip_poses.txt wins, and the pose is applied only in VR gameplay - after
 * the arms and the hand morphs are posed, on the model's own finger bones (never on the game's
 * 0..48). A bone's pose is like Blender's pose basis: local_posed =
 * local_rest * T(t) * R(q); the left hand uses the mirrored quaternion (x, -y, -z, w) and the
 * mirrored move (-tx, ty, tz). */
#ifndef GRIPPOSES_H
#define GRIPPOSES_H

#include "../core/mathx.h"

struct Actor;
struct Model;

#define VRGRIP_POSES 64
#define VRGRIP_BONES 40

typedef struct VrGripBone {
    char name[32];        /* "arm right finger 2c" */
    float q[4];           /* x y z w, the bone's pose rotation, normalised */
    float t[3];           /* x y z, the bone-local move (0 when the line omits it) */
} VrGripBone;

typedef struct VrGripPose {
    char name[32];        /* the name grips.txt uses */
    int nbones;
    VrGripBone bone[VRGRIP_BONES];
} VrGripPose;

/* Parse PROPS/grip_poses.txt text. A bone line is `<bone name> qx qy qz qw [tx ty tz]`; the
 * numbers are read from the end, so bone names may contain spaces. Poses go into `poses` (up to
 * max_poses), and the counts are the bone lines read and the malformed/overflow lines skipped.
 * Returns the number of poses and starts a new pose at every `pose <name>` line. It is public for
 * the check. */
int vrgrip_parse_text(const char *text, VrGripPose *poses, int max_poses, int *bone_lines, int *skipped);

/* forget the file and every pose (a new game / itemprops_reset) */
void vrgrip_reset(void);
/* no pose for either hand, blending stopped: the next pose starts fresh (a dropped hand, a
 * cutscene, a new model) */
void vrgrip_idle(void);

/* a Fiona model was loaded or freed (a costume change, a new game): forget the model readiness
 * and the grip state, so the next pose is found and started once for the model now loaded */
void vrgrip_forget_model(void);

/* What one hand wants this frame: a pose name (empty/NULL: none) and the item it belongs to,
 * for the log. Called by vrhands_pose for both hands before vrgrip_apply. */
void vrgrip_want(int side, const char *pose, int item);

/* Apply the wanted poses to the model-space world matrices, after the arms, the hand morphs
 * and the VR body have been posed. `dt` is the real frame time in seconds. */
void vrgrip_apply(struct Actor *a, Mat4 *world, float dt);

/* The built-in rig check: grip_orb at t = 1 on the bind pose, the right finger 2a / 3a / 5a / 2c
 * origins in bone 46's rest space. `out->ran` is 0 when the model has no finger bones (or no
 * grip_orb), `pass` is 1 when all four are within about 0.01 of the expected numbers. Public so the
 * check can run it without the log. */
typedef struct VrGripCheck {
    int ran;
    int pass;
    float got[4][3];      /* R finger 2a, R finger 3a, R finger 5a, R finger 2c */
} VrGripCheck;
int vrgrip_selfcheck_model(const struct Model *m, const VrGripPose *poses, int nposes, VrGripCheck *out);
/* the same, in `wrist`'s rest space (an outfit's real right wrist: not always bone 46) */
int vrgrip_selfcheck_model_at(const struct Model *m, int wrist, const VrGripPose *poses, int nposes,
                              VrGripCheck *out);

/* the left hand's mirror (the same convention as itemprops_mirror): q -> (x, -y, -z, w), and the
 * bone-local move -> (-x, y, z) (the character is mirrored across model X) */
void vrgrip_mirror_q(float q[4]);
void vrgrip_mirror_t(float t[3]);
/* shortest-path spherical interpolation: t = 0 gives a, t = 1 gives b */
void vrgrip_slerp(const float a[4], const float b[4], float t, float out[4]);

#endif
