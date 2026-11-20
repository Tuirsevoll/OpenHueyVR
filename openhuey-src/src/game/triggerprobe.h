/* DEBUG-ONLY: a trigger probe (see triggerprobe.c). It logs the current room's id and name and
 * Fiona's position, for checking the rooms' names and units against triggers.txt. Read-only: it
 * changes nothing in the game. Remove it (with its line in CMakeLists.txt and its one call in
 * engine_tick) when the check is done. */
#ifndef TRIGGERPROBE_H
#define TRIGGERPROBE_H

#include "engine.h"

/* once a game tick while the game plays (not while a menu is up): a line when the room changes,
 * and at most once every 2 seconds otherwise */
void triggerprobe_tick(Engine *e);

#endif
