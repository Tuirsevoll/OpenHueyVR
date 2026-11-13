/* Quest controllers on Fiona: her arms reach the grip poses, her hand shapes follow the
 * triggers, and the grip grabs a small object or a door. The A button is left alone. */
#ifndef VRHANDS_H
#define VRHANDS_H

#include "../core/mathx.h"
#include "../data/navmesh.h"

struct Actor;
struct Engine;
struct Room;
struct PlacedSet;

int vrhands_wants(const struct Actor *a);
void vrhands_pose(struct Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace);
/* (temporary, for the headset smoothing test) the microseconds vrhands_pose and its hand_stop()
 * calls have added up since the last call, then cleared. Either pointer may be NULL. */
void vrhands_take_times(unsigned *pose, unsigned *stop);
/* a pet pushes the bone under the hand. Called from the pose, after the skeleton is built. */
void vrhands_pet_pose(struct Actor *a, Mat4 *world, int nbones, const Mat4 *unplace);
void vrhands_tick(struct Engine *e);
/* 1 when the straight path a hand took this frame (`from` to `to`) came within `skin` of one of
 * the room's walls: a mesh triangle steep enough to be a wall (|normal.y| below 0.5), sampled
 * along the path. Detection only: nothing is blocked. */
int hand_path_hits_wall(const struct Room *r, Vec3 from, Vec3 to, float skin);
/* the same path against the room's door leaves: every face of a leaf counts (a shut one's face,
 * an open one's edge). Door `skip` (the one this hand holds; -1 none) is left out. Detection only. */
int hand_path_hits_door(const struct Engine *e, int skip, Vec3 from, Vec3 to, float skin);
/* the same path against the room's placed objects: every face of them counts (a table's top as
 * much as its side). Placed object `skip` (the one this hand holds; -1 none) is left out. */
int hand_path_hits_placed(const struct PlacedSet *s, int skip, Vec3 from, Vec3 to, float skin);
/* Where a hand's path may go in the room's mesh: the free point nearest the first face the path
 * comes within `keep` of (the free point nearest the wall), or `to` when nothing is in the way or
 * the hand is already in contact at `from` (a hand is never trapped). The point it returns is
 * always free, so asking again from it gives the same answer. */
Vec3 path_stop(const struct Room *r, Vec3 from, Vec3 to, float keep);
/* The same, but sliding: where the stop meets a wall, floor or ceiling, the part of the remaining
 * motion that goes into the surface is removed and the hand keeps the sideways part, so it slides
 * along the surface (up to three rounds, so gentle corners can be turned). A hand pushing straight
 * in still stops dead. The result is always free of the surface, like path_stop's. */
Vec3 path_slide(const struct Room *r, Vec3 from, Vec3 to, float keep);
void vrhands_forget_poses(void);
/* 1 when that controller is on Hewie. A touch also gives that hand a mild buzz. */
int vrhands_petting(int side);
/* a hard swing that met the room or a placed mesh. The point is Fiona's blow for one script frame. */
int vrhands_blow_at(float out[3]);
/* 1 when a hand moved fast enough to count, whether or not it touched anything. */
int vrhands_swing(void);
/* both hands met a stalker, fast, within a few frames of each other. One script frame, until taken. */
int vrhands_shove_ready(void);
void vrhands_shove_take(void);
/* the exit a two-hand shove met while it was shut and free, or -1. One script frame, until taken. */
int vrhands_door_shove(void);
/* +1 opens toward +90, -1 toward -90: the side the hands pushed from. */
int vrhands_door_way(void);
void vrhands_door_take(void);
/* bits of the exits whose leaf swept a stalker last tick. */
int vrhands_door_hits(void);
/* the equipped magnesia or antimony, in the hand. Drawn with the things. */
void vrhands_defense_draw(const Mat4 *view_proj);
/* a grip release that should become a thing. 1 once, then cleared. */
int vrhands_defense_take(int *kind, float *x, float *y, float *z, float *vx, float *vy, float *vz);
/* the ladder a hand at `at` is in the grab zone of in `nav` - its link index, -1 none: near the
 * line of its two spots across the floor, within its height and a rise past either end. The room
 * is handed in, so hands_sweep_check can ask it with ladders of its own. */
int vrhands_ladder_link(const NavMesh *nav, Vec3 at);
/* the ladder that hand holds (the link index), or -1. A grab of the air beside a ladder while
 * Fiona climbs: nothing moves her yet. */
int vrhands_ladder_hold(int side);

#endif
