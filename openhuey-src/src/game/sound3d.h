/* Occlusion for the 3-D audio: see sound3d.c. The muffling is switched off there for now (the
 * 3-D audio is the HRTF alone): sound3d_tick still runs for the hiding spot's widening, and
 * sound3d_blocked is kept for when the muffling is wanted again. */
#ifndef SOUND3D_H
#define SOUND3D_H

#include "../core/mathx.h"
#include "../data/navmesh.h"

#include "engine.h"

/* once a game tick: for every spatial sound, whether the room's geometry is between it and the
 * ears (saudio_occlude - nothing at all while the muffling is off, sound3d.c) and how enclosed
 * the ears are (saudio_enclosed). */
void sound3d_tick(Engine *e);

/* the room's line of walk from `from` to `to` meets a wall or a shut door first (or leaves the
 * mesh: another room): 1 blocked, 0 clear. Sharing this is deliberate: a stalker's line of sight
 * can use it later (it is the game's own Actor_WalkMesh kind of test, not a mesh raycast - a
 * RoomMesh ray can replace it behind this name without touching the audio). */
int sound3d_blocked(const NavMesh *nav, Vec3 from, Vec3 to);

#endif
