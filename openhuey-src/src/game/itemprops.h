/* A mod's held-item grips (PROPS/grips.txt): where an item's own model sits in a hand. Up to two
 * lines an item - one for each hand, so the other hand's model can be placed exactly - and, with
 * only one line, the other hand's place is its mirror across her body. Read once through the files
 * layer, so a mod's library/data/PROPS/grips.txt is the one read. */
#ifndef ITEMPROPS_H
#define ITEMPROPS_H

#include "../core/mathx.h"

#include <stddef.h>

#define ITEMPROPS_ITEMS 0xC0   /* the game's item ids: 0 .. 0xBF */

typedef struct ItemGrip {
    int used;              /* a line was read for it */
    int item;              /* the item's id (0 .. 0xBF) */
    int bone;              /* her hand's bone: 30 (left, +x) or 46 (right, -x) */
    Vec3 offset;           /* in the wrist bone's own axes, game units */
    float rot[4];          /* x y z w, turned about the offset */
    char pose[32];         /* the optional grip pose's name: PROPS/grip_poses.txt applies it */
    char model[96];        /* the optional model's file in PROPS/ (empty: item-<id>.gltf) */
    float tint[3];         /* multiplies the model's colours (1 1 1: as authored) */
} ItemGrip;

/* forget every line and the file (a new game's scripts) */
void itemprops_reset(void);
/* the line for `item` made for `bone` (30 or 46), or NULL when the mod has none for that hand */
const ItemGrip *itemprops_get(int item, int bone);
/* The grip for the hand `bone`: its own line when there is one, else the other hand's line
 * mirrored across her body (+x to -x: the offset's x negated, the turn's y and z negated). `out`
 * is filled and 1 returned; 0 when the item has no line at all. */
int itemprops_grip(int item, int bone, ItemGrip *out);
/* the mirror of `g`, for the other hand (itemprops_grip's own turn) */
void itemprops_mirror(ItemGrip *g);
/* one line of grips.txt: 1 when it is a grip, 0 when it is blank, a comment or malformed */
int itemprops_parse_line(const char *line, ItemGrip *out);
/* wrist * T(offset) * R(rot): where the model sits, in the room. `g` NULL gives the wrist as it is */
Mat4 itemprops_matrix(const ItemGrip *g, const Mat4 *wrist);
/* the files-layer path of the model a grip names: PROPS/<model>, or PROPS/item-<id>.gltf when the
 * grip names none (`g` NULL: the item's own). A name already starting with PROPS/ is used as is. */
void itemprops_model_path(const ItemGrip *g, int item, char *out, size_t n);

#endif
