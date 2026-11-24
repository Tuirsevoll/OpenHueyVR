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
/* The portals: a neighbour seen through each seam's opening (main.c draw_scene does the drawing
 * with render_portal_*; this says where and how). Per eye: `vp` the eye's view-projection, `eye`
 * its place in the played room. mode 1: the eye is on the played room's side - the opening's quad
 * (`mark`, a little beyond the plane; `plane`, on it) is a window the neighbour is drawn into,
 * clipped at the plane (clip_b); mode 2: the eye is past the plane (a head through the doorway) -
 * the played room's own draws are clipped to its side (clip_a) and the neighbour drawn whole,
 * clipped to its own. Planes are in clip space (dot(plane, gl_Position) >= 0 is kept). The
 * neighbour is drawn with vp x t_ba, in renderer layer 1 and `see_layer`. */
typedef struct SeamPortal {
    int mode;                 /* 0: no portal now */
    Vec3 mark[4], plane[4];   /* quad corners 0 1 2 3: top left, top right, bottom left, bottom right */
    float clip_b[4], clip_a[4];
    Mat4 t_ba;
    Room *room;
    int skip_door, see_layer, tint;
} SeamPortal;
/* the played room's portals for this eye (up to max, one a seam whose neighbour is loaded and
 * whose door is not shut): how many */
#define SEAM_PORTALS_MAX 8
int roomseamless_portals(struct Engine *e, const Mat4 *vp, Vec3 eye, SeamPortal *out, int max);
/* The fog, tint and bloom across a doorway: look (the played room's, in and out) is blended with the
 * neighbour's by the eye's distance from the plane - half and half on it, the same on both sides, so a
 * handover does not show. 1 if it was changed (the caller puts the original back after the post pass). */
int roomseamless_look(struct Engine *e, Vec3 eye, RoomLook *look);
/* The room the handover is for, if it is already loaded beside the played one: it is swapped in as
 * the played room (room_promote finishes it) and the room just left stays beside it as the scripts
 * left it. Called by the `room` word in the tick of the crossing; 1 if swapped (no load needed). */
int roomseamless_swap_in(Room *played, int id);
/* the debug outline of the opening (seam-outline), drawn near the end of the picture */
void roomseamless_outline(struct Engine *e, const Mat4 *vp);
/* the debug words: what is SEAM_DBG_*, on or off */
enum { SEAM_DBG_PORTAL, SEAM_DBG_OUTLINE, SEAM_DBG_TINT, SEAM_DBG_LOG };
void roomseamless_debug(int what, int on);
/* one game tick's time in ms (main.c), for the log: the slow ones */
void roomseamless_note_tick(long tick, double ms);
/* seam-info's text: the played room's seam, T, the gate, the neighbour, Fiona's distance */
void roomseamless_info(char *buf, size_t n);
/* 1: this draw is one of the pair's gateway void boxes (the black cube shells the original uses
 * to hide what is beyond the doorway). Seamless mode does not draw them; room.c asks for every
 * solid room draw. Original mode always answers 0. */
int roomseamless_hide_draw(const Room *r, const MeshDraw *d);
#endif
