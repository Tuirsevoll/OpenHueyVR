/* DEBUG-ONLY: the trigger probe. It logs the current room (its numeric id and the PAC's base name,
 * "ST_003") and Fiona's position, so the room names and units can be checked against triggers.txt.
 *
 * It reads only: gEngine.room.id, the room's path (pac_room_path) and Fiona's body - the id the
 * scripts hand the camera director (`cam-player`, fiona.fs's `on spawned: self cam-player!`), read
 * through hactor_body as camdirector.c's target_at reads the one followed. No game state is
 * touched, and nothing is logged while a menu is up or in a --test run.
 *
 * Remove this file, its CMakeLists.txt line and its call in engine_tick when the check is done. */
#include "triggerprobe.h"
#include "../platform/vrlog.h"

#include "../data/pac.h"
#include "../hactor/hactor.h"
#include "camdirector.h"

#include <stdio.h>
#include <string.h>

/* 2 seconds at the game's fixed frame rate (TICKS_PER_SECOND, engine.h) */
#define TRIGPROBE_TICKS (2 * TICKS_PER_SECOND)

void triggerprobe_tick(Engine *e) {
    static int sRoom = -1;   /* (the room of the last line) */
    static long sTick;       /* (the tick of the last line) */
    char path[64], name[32];
    const char *base;
    HBody *b;
    size_t len;
    int room;
    long tick;

    if (e->testing) {
        return;   /* (not a game: the headless --test runs stay quiet) */
    }
    room = e->room.id;
    tick = e->ticks;
    b = gCamDir.player < 0 ? NULL : hactor_body(gCamDir.player);
    if (room < 0 || b == NULL || !b->has) {
        return;   /* (no room, or no Fiona with a body yet) */
    }
    if (room == sRoom && tick - sTick < TRIGPROBE_TICKS) {
        return;
    }
    /* the room's file, "ST_000/ST_003.PAC": its base name without ".PAC" */
    pac_room_path(room, path, sizeof(path));
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    len = strlen(base);
    if (len >= 4 && strcmp(base + len - 4, ".PAC") == 0) {
        len -= 4;
    }
    snprintf(name, sizeof(name), "%.*s", (int)len, base);
    sRoom = room;
    sTick = tick;
    /* the same log the game's messages go to (the console's log and stdout) */
    if (VR_LOG_ON()) {
        forth_printf(e->forth, "TRIGPROBE room=%d name=%s fiona=%.3f %.3f %.3f\n", room, name,
                     b->pos[0], b->pos[1], b->pos[2]);
    }
}
