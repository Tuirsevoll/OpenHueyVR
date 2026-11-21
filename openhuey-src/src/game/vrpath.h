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
/* how far a point is from a path's own point at closure c, horizontally (x/z): -1 when the point's
 * height is outside [y_lo, y_hi], or there is no path (a door is reached anywhere along its height) */
float vrpath_edge_gap(const Vec3 *path, int n, float c, Vec3 at, float y_lo, float y_hi);

#endif
