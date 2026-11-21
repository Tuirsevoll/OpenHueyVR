/* The VR markers a mod ships (PROPS/hidespots.txt and PROPS/levers.txt, its library data): the
 * hiding spots' boxes - where a hand takes the door, and the volume Fiona may be in while it is
 * shut - and the levers' grab boxes. Read once through the files layer, as PROPS/triggers.txt is,
 * so a mod's library/data/PROPS/ copy is used. Nothing here hides, takes or moves anything: this
 * reads the two files and logs what a room has.
 *
 * The lines are `<room> <spot> <grab|bound> cx cy cz hx hy hz qw qx qy qz` (a spot's two: the same
 * room and spot) and `<room> <name> cx cy cz hx hy hz qw qx qy qz` (a lever). The room is its PAC
 * base name ("ST_026"), c the box's centre, h its half sizes along its own axes, and q its turn
 * (w x y z, normalised as triggers.txt's). All of it is in the room's own space - the numbers the
 * room's mesh, the placed objects and the hands use - and OpenHuey loads one room at its own
 * origin, so room space is world space: no offset, no axis flip. */
#ifndef VRMARKERS_H
#define VRMARKERS_H

#include "../core/mathx.h"

#define VRMARKERS_SPOTS 64
#define VRMARKERS_LEVERS 64

/* a marker's box, as triggers.txt's boxes: a centre, half sizes on its own axes, and its turn */
typedef struct VrBox {
    char room[16];     /* the room's PAC base name, as the file has it ("ST_026") */
    char name[32];     /* the spot's or the lever's name (the log) */
    float c[3];        /* its centre, room units */
    float h[3];        /* its half sizes, the box's own axes */
    float q[4];        /* its turn: w x y z, normalised */
} VrBox;

/* forget the files and the room logged last (a new game: they are read again) */
void vrmarkers_reset(void);
/* read PROPS/hidespots.txt and PROPS/levers.txt if they have not been read yet (the first game
 * tick: the mods are on), log how many there are, and log the markers of the room each time it
 * changes */
void vrmarkers_load_if_needed(void);
/* one line of either file, for the check (the file's read is this, line by line): 1 when it was a
 * box, 0 when it was blank, a comment or malformed. `levers` 0: PROPS/hidespots.txt, 1:
 * PROPS/levers.txt. A spot's grab and bound lines find each other by room and name. */
int vrmarkers_parse_line(const char *line, int levers);
/* the indices of the current room's spots (or levers), at most `max` of them, for out[0..n); the
 * room's name is triggers_room_name's. Returns how many were stored. */
int vrmarkers_spots_here(int room_id, int *out, int max);
int vrmarkers_levers_here(int room_id, int *out, int max);
/* a spot's boxes, NULL when it has none of that kind (the load warns of it) */
const VrBox *vrmarkers_spot_grab(int i);
const VrBox *vrmarkers_spot_bound(int i);
const char *vrmarkers_spot_name(int i);
const VrBox *vrmarkers_lever_box(int i);
const char *vrmarkers_lever_name(int i);

/* 1 when the point is inside the box (its own axes, as triggers.c's test), grown by `margin` on
 * every side; a box with a half of 0 never matches */
int vrbox_inside(const VrBox *b, Vec3 p, float margin);
/* the point in the box's own space (turned by its conjugate, the centre at the origin), and back */
Vec3 vrbox_to_local(const VrBox *b, Vec3 p);
Vec3 vrbox_to_room(const VrBox *b, Vec3 local);
/* the box axis nearest room +y: *axis is its index (0..2) and the returned unit vector is that
 * axis in room space, so its y is the sign - negative means the box's +axis points down */
Vec3 vrbox_up_axis(const VrBox *b, int *axis);

#endif
