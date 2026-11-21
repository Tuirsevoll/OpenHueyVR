/* DEBUG-ONLY: the VR hiding spot probe (see vrspotprobe.c). It logs the facts the hiding rework
 * needs from the real game data: a room's hiding spot boxes (the mod's PROPS/hidespots.txt), every
 * placed object near a GRAB box, the animation tracks that could be its doors and curtains, and
 * where Fiona's feet are while she stands at a spot's BOUND.
 *
 * Read-only: no object, track, box or game flag is moved, hidden, played or changed - an object's
 * animation is only ever read through a *copy* of it.
 *
 * Remove this file, its CMakeLists.txt line and its call in engine_tick when the hiding rework is
 * done. */
#ifndef VRSPOTPROBE_H
#define VRSPOTPROBE_H

#include "../core/mathx.h"
#include "engine.h"
#include "placed.h"

/* once a game tick, right after vrspot_tick: the room's spots and their objects once a room
 * entry, and Fiona's feet every second while she is at a spot's bound. Nothing in a --test run. */
void vrspotprobe_tick(Engine *e);
/* the scripts' own her-at, as vrspot_here is given it (fiona.fs's frame-end): the feet source */
void vrspotprobe_here(Vec3 p);
/* a script starting track `id` on a placed object (bind_engine.c's `placed-op` 1 / 2): logged when
 * the object is near a spot's GRAB box - the log only, what the script asked for is unchanged */
void vrspotprobe_script_track(const Placed *p, int id, int loop);

#endif
