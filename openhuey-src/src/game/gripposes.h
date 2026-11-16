/* Mod finger grip poses (PROPS/grip_poses.txt): the hand's finger bones turned to the shape a
 * held item's grip names. The file is read through the files layer, so a mod's
 * library/data/PROPS/grip_poses.txt wins, and the pose is applied only in VR gameplay - after
 * the arms and the hand morphs are posed, on the model's own finger bones (never on the game's
 * 0..48). The turn is a bone-local pose rotation, like Blender's pose rotation: local_posed =
 * local_rest * R(q); the left hand uses the mirrored quaternion (x, -y, -z, w). */
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
} VrGripBone;

typedef struct VrGripPose {
    char name[32];        /* the name grips.txt uses */
    int nbones;
    VrGripBone bone[VRGRIP_BONES];
} VrGripPose;

/* Parse PROPS/grip_poses.txt text. Poses go into `poses` (up to max_poses), and the counts are
 * the bone lines read and the malformed/overflow lines skipped. Returns the number of poses and
 * starts a new pose at every `pose <name>` line. It is public for the check. */
int vrgrip_parse_text(const char *text, VrGripPose *poses, int max_poses, int *bone_lines, int *skipped);

/* forget the file and every pose (a new game / itemprops_reset) */
void vrgrip_reset(void);
/* no pose for either hand, blending stopped: the next pose starts fresh (a dropped hand, a
 * cutscene, a new model) */
void vrgrip_idle(void);

/* What one hand wants this frame: a pose name (empty/NULL: none) and the item it belongs to,
 * for the log. Called by vrhands_pose for both hands before vrgrip_apply. */
void vrgrip_want(int side, const char *pose, int item);

/* Apply the wanted poses to the model-space world matrices, after the arms, the hand morphs
 * and the VR body have been posed. `dt` is the real frame time in seconds. */
void vrgrip_apply(struct Actor *a, Mat4 *world, float dt);

/* The built-in rig check: grip_orb at t = 1 on the bind pose, the right finger 2c / 1c tips and
 * the left finger 2c tip in their wrists' rest spaces. `out->ran` is 0 when the model has no
 * finger bones (or no grip_orb), `pass` is 1 when all three are within 0.05 of the expected
 * numbers. Public so the check can run it without the log. */
typedef struct VrGripCheck {
    int ran;
    int pass;
    float got[3][3];      /* R finger 2c, R finger 1c, L finger 2c */
} VrGripCheck;
int vrgrip_selfcheck_model(const struct Model *m, const VrGripPose *poses, int nposes, VrGripCheck *out);

/* the left hand's mirror (the same convention as itemprops_mirror): (x, -y, -z, w) */
void vrgrip_mirror_q(float q[4]);
/* shortest-path spherical interpolation: t = 0 gives a, t = 1 gives b */
void vrgrip_slerp(const float a[4], const float b[4], float t, float out[4]);

#endif
