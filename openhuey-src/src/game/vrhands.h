/* Quest controllers on Fiona: her arms reach the grip poses, her hand shapes follow the
 * triggers, and the grip grabs a small object or a door. The A button is left alone. */
#ifndef VRHANDS_H
#define VRHANDS_H

#include "../core/mathx.h"

struct Actor;
struct Engine;

int vrhands_wants(const struct Actor *a);
void vrhands_pose(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace);
void vrhands_tick(struct Engine *e);
void vrhands_forget_poses(void);

#endif
