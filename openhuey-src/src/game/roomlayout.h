/* The rooms' places in one shared world (PROPS/room_layout.txt, a mod's library data: Sean's
 * export_layout.py writes it from the connected blend).
 *
 * A line is `<room> <name> <flag> m00 m01 m02 tx m10 m11 m12 ty m20 m21 m22 tz`: the room's PAC
 * base name ("ST_001"), a name for people, a flag, and a 3x4 affine matrix taking a point in that
 * room's own space (game units, Y down) into the shared world of the same units. The third word:
 *
 *   base                  an ordinary room
 *   variant-of=ST_xxx     a story version or reuse of that room: it shares the base's place (and
 *                         its numbers are the base's)
 *   same-place-as=ST_xxx  a version that already sits on its base
 *   check                 skip it (logged)
 *
 * From room A's space into room B's: T_A->B = inverse(W_B) x W_A.  The placement is read, never
 * written: the game keeps every room at its own origin and draws a neighbour by this matrix.
 *
 * Reading is through the files layer (core/files.h), so a mod's file is found as triggers.txt is.
 */
#ifndef ROOMLAYOUT_H
#define ROOMLAYOUT_H

#include "../core/mathx.h"

#include <stddef.h>

/* read PROPS/room_layout.txt once (the first game tick), logging how many rooms it has and how
 * many check lines were skipped. A missing file is only logged. */
void roomlayout_load_if_needed(void);
/* forget the file and its rooms (a test's fresh table; a new game reads it again if needed) */
void roomlayout_clear(void);
/* room `room_id`'s matrix from its own space into the shared world; 0 if the room is missing, a
 * check line, or a variant whose base is missing. A variant or same-place room gives its base's
 * matrix (as the file has it, the two are the same numbers). */
int roomlayout_get(int room_id, Mat4 *W);
/* T takes room `from`'s space into room `to`'s: inverse(W_to) x W_from. 0 if either has no
 * matrix. */
int roomlayout_between(int from, int to, Mat4 *T);
/* the base room a loaded room's place is the base's: itself for base, the named room for
 * variant-of / same-place-as; -1 if it is missing or a check line. */
int roomlayout_base_id(int room_id);
/* what the file was read as (the log and the check): rooms kept, check lines skipped */
int roomlayout_entries(void);
int roomlayout_skipped(void);
/* the room id a name in either file means ("ST_001"): -1 if it is not one */
int roomlayout_room_id(const char *name);
/* one line of the file, parsed as the loader parses it (the check's): 1 if it was a room line */
int roomlayout_parse_line(const char *line);

#endif
