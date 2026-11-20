/* The key items' trigger boxes (PROPS/triggers.txt, a mod's library data): when the item the
 * menu equipped in her hand is inside a box of the room she is in, the game uses it - the same
 * rule the menu's Use runs. Read once through the files layer; the hand item's place comes from
 * vrhands_hand_item_at (her wrist and the item's grip line, the matrix it is drawn from).
 * The file's lines are `<room> <name> cx cy cz hx hy hz qw qx qy qz`: the room's PAC base name
 * ("ST_024"), a name for the log, the box's centre, its half sizes and its turn. */
#ifndef TRIGGERS_H
#define TRIGGERS_H

#include "../core/mathx.h"

#include <stddef.h>

/* forget the file and the arm (a new game: it is read again) */
void triggers_reset(void);
/* read PROPS/triggers.txt if it has not been read yet (the first game tick: the mods are on),
 * and log how many boxes it has */
void triggers_load_if_needed(void);
/* no hand item in a box: it re-arms, so the box fires again on the next entry */
void triggers_leave(void);
/* the room's name as triggers.txt has it: pac_room_path's base without ".PAC" ("ST_024") */
void triggers_room_name(int id, char *out, size_t n);
/* 1 on the tick the point enters a box of the current room (not while it stays in that box);
 * 0 otherwise. The first call reads the file. */
int triggers_hit(Vec3 p, int item);

#endif
