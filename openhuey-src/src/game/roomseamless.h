/* Seamless room loading: a doorway whose two rooms are drawn together and walked through with no
 * fade, no door and no jump (the VR option "Room loading: Seamless (test)").
 *
 * PROPS/seamless.txt names the doorways, one a line: `<room> <exit> <room> <exit>` with the rooms'
 * PAC base names ("ST_001 2 ST_002 2"). Both rooms must be in PROPS/room_layout.txt (and not a
 * `check` line). The pair is the base rooms'; a story version of either (variant-of) uses the same
 * pair because the layout puts it in the base's place.
 *
 * The played room is the room the engine holds (room.c). In Seamless mode, while it is part of a
 * pair, the other room is loaded draw-only into a second Room and drawn with the current room's
 * view-projection times T(other -> current). Crossing the doorway's line (the exit's spots and
 * its event area/gate, or its door) hands the played room over to the other: the normal
 * `go-through` runs (leaving-room, the load, arrived, entered-room), but Fiona and a nearby Hewie
 * are put where they were in the old room's space converted by T(from -> to), so nothing jumps.
 * Original mode does none of it: the old walk-out and its fade are today's.
 *
 * Everything is in each room's own coordinates; only the layout's placement is added on top. */
#ifndef ROOMSEAMLESS_H
#define ROOMSEAMLESS_H

#include "room.h"

#include <stdint.h>

struct Engine;

/* a new game (bind_engine_reset): forget the handover in progress and let the neighbour go. The
 * layout and the pairs are files, read once and kept. */
void roomseamless_reset(void);
/* one game tick, after the actors' frame (their moves and messages delivered): keep the
 * neighbour in step with the played room, and hand over when Fiona is through the doorway. */
void roomseamless_tick(struct Engine *e);
/* the `room` word's load: time a seamless handover's load (and take her exact place just before
 * it). Called by forth/bind_engine.c's p_room. */
void roomseamless_load_begin(int id);
void roomseamless_load_end(void);
/* 1 if `exit` of the played room is a seamless doorway now: the scripts' old way out (kit.fs
 * exit-out: take-exit, the walk-out and its fade) is suppressed for it. */
int roomseamless_exit(int exit);
/* `seamless-go ( exit -- )`: the handover by that exit, if it is the played room's seamless one.
 * The normal go-through is queued; 1 if it was. */
int roomseamless_go(int exit);
/* the arrival's place, from the old room's captured place by T(from -> to): 1 and x y z yaw when
 * this arrival is the seamless one (once). fiona.fs's on arrived uses it instead of arrive. */
int roomseamless_take_arrival(Vec3 *at, float *yaw);
/* 1 while a seamless handover's arrival is the one just made (until that tick ends): the story's
 * own arrival placement (kit.fs way-in -> at-exit) must not put her at the exit spot again. */
int roomseamless_arrived(void);
/* whether actor `id`'s body was within 40 units of Fiona before the load; if so it is put at the
 * converted place (its room and triangle too) and this is 1, once. hewie.fs's on arrived uses it
 * instead of his off-screen arrival. */
int roomseamless_hewie_here(int id);
/* the neighbour's moving batches for this eye, its eye converted into its space: 1 if it has
 * one. Draw the eye's layers after the played room's (main.c draw_scene/draw_layer). */
int roomseamless_view_begin(struct Engine *e, Vec3 eye, Vec3 forward);
/* the neighbour's room draws in renderer layer `layer` (only the solid and see-through layers are
 * drawn), with view_proj x T(neighbour -> played): 1 if it drew. */
int roomseamless_draw(struct Engine *e, const Mat4 *view_proj, int layer);
/* 1: this draw is one of the pair's gateway void boxes (the black cube shells the original uses
 * to hide what is beyond the doorway). Seamless mode does not draw them; room.c asks for every
 * solid room draw. Original mode always answers 0. */
int roomseamless_hide_draw(const Room *r, const MeshDraw *d);
/* Clear the keep-Fiona-off flags on the gateway's nav triangles so she can walk through the
 * pair's opening instead of being held inside its void box. Only Seamless mode, only pair rooms;
 * the PAC and every other mode keep their flags. */
void roomseamless_open_gateway(Room *r);

#endif
