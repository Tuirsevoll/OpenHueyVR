/* Occlusion for the 3-D audio: see sound3d.h.
 *
 * The test is the game's own line of walk - the same "straight from a to b over the nav mesh"
 * the characters use (navmesh_wall_masked) - with the flags a sound cannot pass: the walls
 * (0x4000) and a shut door's passage flags (0x60000, which Doors_RoomIn sets on the side it is
 * shut against and moves as it swings). It is cheap, it agrees with where the characters can go,
 * and it treats another room as blocked, which is what a sound through a wall should be. A
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
    int n, i, hidden, spot, enclosed;

    if (e == NULL || !saudio_on() || !saudio_listener_at(&ear)) {
        return;
    }
    n = saudio_sources(voices, world, SOUND3D_VOICES);
    if (n <= 0) {
        return;
    }
    hidden = (int)(gProgress.state[SOUND3D_HIDDEN >> 5] >> (SOUND3D_HIDDEN & 31)) & 1;
    spot = vrspot_active();
    enclosed = spot && vrspot_enclosed();
    for (i = 0; i < n; i++) {
        Vec3 at = vec3(world[i][0], world[i][1], world[i][2]);
        float dx = at.x - ear.x, dy = at.y - ear.y, dz = at.z - ear.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);

        if (d <= SOUND3D_SELF) {
            saudio_occlude(voices[i], 0.0f);   /* her own body: as it was */
            saudio_enclosed(voices[i], 0.0f);
        } else if (enclosed) {
            saudio_occlude(voices[i], 1.0f);   /* inside a shut hiding spot: muffled ... */
            saudio_enclosed(voices[i], 1.0f);  /* ... and its direction widened */
        } else if (!spot && hidden) {
            saudio_occlude(voices[i], 1.0f);   /* the flat screen's hiding: the room is muffled */
            saudio_enclosed(voices[i], 0.0f);
        } else {
            saudio_occlude(voices[i], (float)sound3d_blocked(&e->room.nav, ear, at));
            saudio_enclosed(voices[i], 0.0f);
        }
    }
}
