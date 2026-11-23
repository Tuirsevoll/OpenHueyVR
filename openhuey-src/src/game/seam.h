/* Seams: doorways between rooms that are not side by side in the map, joined like a portal
 * (PROPS/seams.txt, a mod's library data: Sean's Hallway Builder writes it).
 *
 * A line pairs two doorways and gives each side's frame in that room's own game units:
 *
 *   # roomA exitA roomB exitB | A: x y z yaw width height | B: x y z yaw width height
 *   ST_008 3 ST_01E 1  -75.2 4.144 -142.25 1.5708 39.94 30.0  -97.402 4.058 -140.031 -1.5708 39.94 30.0
 *   ST_001 2 ST_002 2  layout
 *
 * x y z: the floor in the middle of the opening, on the doorway's plane. yaw: the way OUT of that
 * room, (sin yaw, 0, cos yaw), in radians. width, height: the opening, in game units (the room
 * data's y is up: the floor of ST_001 is -10, the ceiling above it). `layout` instead of frames:
 * the two doorways already meet in the map, so room_layout.txt's matrices are the transform.
 *
 * The transform of an explicit seam: theta = yawB + pi - yawA, T_A->B(p) = pB + R(theta) (p - pA),
 * where R(theta) turns about Y so that a yaw `a` becomes a + theta. Walking out of A walks into B.
 */
#ifndef SEAM_H
#define SEAM_H

#include "../core/mathx.h"

typedef struct SeamFrame {
    Vec3 p;
    float yaw, width, height;
} SeamFrame;

typedef struct Seam {
    int a, ea, b, eb;      /* the rooms' base ids and their exits */
    int layout;            /* 1: no frames, the layout's matrices are the transform */
    SeamFrame fa, fb;
} Seam;

/* read PROPS/seams.txt once (the first call); a missing file is only logged */
void seam_load_if_needed(void);
void seam_clear(void);   /* forget it (a test's fresh table) */
int seam_count(void);
const Seam *seam_at(int i);
/* one line, parsed as the loader parses it (the check's): 1 if it was a seam line */
int seam_parse_line(const char *line);
/* the seam joining these two doorways (either order), or NULL */
const Seam *seam_find(int base_a, int ea, int base_b, int eb);
/* the turn of an explicit seam, theta */
float seam_theta(const Seam *s);
/* T from the A side's room space into B's (from_is_a) or B's into A's; 0 for a `layout` seam */
int seam_transform(const Seam *s, int from_is_a, Mat4 *T);
/* the frame of one side */
const SeamFrame *seam_frame(const Seam *s, int side_is_a);

/* a general 4 x 4 inverse (column-major, as Mat4): 0 if it is singular */
int seam_mat4_inverse(const Mat4 *m, Mat4 *out);

#endif
