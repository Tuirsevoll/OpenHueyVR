/* The handle path of a hiding spot's part: a short polyline a hand's target is projected onto to
 * give the spot's closure (game/vrspots.c). Its own file so the check can try it on paths of its
 * own, with no game behind it. */
#ifndef VRPATH_H
#define VRPATH_H

#include "../core/mathx.h"

/* the closure a target asks for on a path of n points (c = 0 at the first, 1 at the last): the
 * nearest point of its segments, searched only between c_now - window and c_now + window, clamped
 * to 0..1 */
float vrpath_project(const Vec3 *path, int n, Vec3 target, float c_now, float window);

#endif
