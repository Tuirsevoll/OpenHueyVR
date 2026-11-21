/* Occlusion for the 3-D audio: see sound3d.h.
 *
 * THE MUFFLING IS SWITCHED OFF (SOUND3D_OCCLUSION 0): the 3-D audio is the binaural HRTF alone -
 * a sound behind a wall is heard through it, placed, not attenuated or low-passed - and no rays
 * are cast. The hiding spot's *widening* (saudio_enclosed, below) is not the occlusion and stays:
 * inside a shut spot, what is outside is still harder to place. Set the constant to 1 to have a
 * wall, a shut door or a shut spot muffle a sound again; nothing else in the audio changes either
 * way.
 *
 * With it on, the test is the game's own line of walk - the same "straight from a to b over the
 * nav mesh" the characters use (navmesh_wall_masked) - with the flags a sound cannot pass: the
 * walls (0x4000) and a shut door's passage flags (0x60000, which Doors_RoomIn sets on the side it
 * is shut against and moves as it swings). It is cheap, it agrees with where the characters can
 * go, and it treats another room as blocked, which is what a sound through a wall should be. A
 * triangle raycast against the room mesh could replace sound3d_blocked later without the audio
 * knowing.
 *
 * A wardrobe or closet hide (fiona-hidden) muffles the room the same way, whoever the sound is
 * from. Her own body's sounds (within half a metre) stay as they were.
 */
#include "sound3d.h"

#include "progress.h"
#include "vrspots.h"

#include "../platform/saudio.h"

#include <math.h>

#define SOUND3D_OCCLUSION 0                  /* 1: walls, shut doors and shut hiding spots muffle */
#define SOUND3D_VOICES 48
#define SOUND3D_WALLS (0x4000u | 0x60000u)   /* a wall; a shut door's passage */
#define SOUND3D_CLIMB 60.0f                  /* how far above the floor a source may be (a drip) */
#define SOUND3D_HIDDEN 9                     /* scripts/flag-names.fs: $9 fiona-hidden */
#define SOUND3D_SELF (0.5f * 12.5f)          /* half a metre: her own body, as it was */

int sound3d_blocked(const NavMesh *nav, Vec3 from, Vec3 to) {
    int tri;
    float h;

    if (nav == NULL || nav->ntris <= 0) {
        return 0;
    }
    tri = navmesh_find(nav, from, SOUND3D_CLIMB, &h);
    if (tri < 0) {
        return 0;   /* (the ears off the floor - a ladder, mid-air: leave the sound clear) */
    }
    return navmesh_wall_masked(nav, tri, from, to, NULL, SOUND3D_WALLS);
}

void sound3d_tick(Engine *e) {
    static float world[SOUND3D_VOICES][3];
    int voices[SOUND3D_VOICES];
    Vec3 ear;
    int n, i, spot, enclosed;

    if (e == NULL || !saudio_on() || !saudio_listener_at(&ear)) {
        return;
    }
    n = saudio_sources(voices, world, SOUND3D_VOICES);
    if (n <= 0) {
        return;
    }
    spot = vrspot_active();
    enclosed = spot && vrspot_enclosed();
    for (i = 0; i < n; i++) {
        Vec3 at = vec3(world[i][0], world[i][1], world[i][2]);
        float dx = at.x - ear.x, dy = at.y - ear.y, dz = at.z - ear.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        float blocked = 0.0f;   /* (the occlusion is off: nothing is ever muffled) */

#if SOUND3D_OCCLUSION
        if (d > SOUND3D_SELF) {   /* (her own body's sounds are never muffled) */
            int hidden = (int)(gProgress.state[SOUND3D_HIDDEN >> 5] >> (SOUND3D_HIDDEN & 31)) & 1;

            if (enclosed || (!spot && hidden)) {
                blocked = 1.0f;   /* (inside a shut spot, or hidden behind a flat screen) */
            } else {
                blocked = (float)sound3d_blocked(&e->room.nav, ear, at);
            }
        }
#endif
        saudio_occlude(voices[i], blocked);
        /* the hiding spot's widening: not the occlusion, so it stays either way */
        saudio_enclosed(voices[i], (d > SOUND3D_SELF && enclosed) ? 1.0f : 0.0f);
    }
}
