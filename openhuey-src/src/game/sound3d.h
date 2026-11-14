/* Occlusion for the 3-D audio: see sound3d.c. */
#ifndef SOUND3D_H
#define SOUND3D_H

#include "../core/mathx.h"
#include "../data/navmesh.h"

#include "engine.h"

/* once a game tick: for every spatial sound, whether the room's geometry is between it and the
 * ears, and how much (saudio_occlude). The audiothread only smooths what this decides. */
void sound3d_tick(Engine *e);

/* the room's line of walk from `from` to `to` meets a wall or a shut door first (or leaves the
 * mesh: another room): 1 muffled, 0 clear. Sharing this is deliberate: a stalker's line of sight
 * can use it later (it is the game's own Actor_WalkMesh kind of test, not a mesh raycast - a
 * RoomMesh ray can replace it behind this name without touching the audio). */
int sound3d_blocked(const NavMesh *nav, Vec3 from, Vec3 to);

#endif
