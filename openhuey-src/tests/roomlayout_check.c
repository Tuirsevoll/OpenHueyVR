/* The rooms' places (src/game/roomlayout.c): PROPS/room_layout.txt's lines as the loader parses
 * them - a base, a variant, a same-place room and a check line - and T between two rooms.
 *
 *   roomlayout-check */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/files.h"
#include "core/mathx.h"
#include "game/roomlayout.h"

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("room layout: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

int main(void) {
    static const char *line_a =
        "ST_001 castle-1f-1 base -0.0000 -0.0000 -1.0000 -5425.3857 0.0000 1.0000 -0.0000 721.1445 1.0000 -0.0000 -0.0000 10338.6641";
    static const char *line_b =
        "ST_002 castle-1f-2 base -0.0000 -0.0000 -1.0000 -5270.7256 0.0000 1.0000 -0.0000 721.1445 1.0000 -0.0000 -0.0000 10136.1445";
    Mat4 wa, wb, wv, t;
    Vec3 pa = vec3(1.0f, 2.0f, 3.0f);
    Vec3 world_a, world_b, moved;

    roomlayout_clear();
    CHECK("a base line parses", roomlayout_parse_line(line_a));
    CHECK("another parses", roomlayout_parse_line(line_b));
    CHECK("a comment, a short line and a bad name are not rooms",
          !roomlayout_parse_line("# room -> world") &&
              !roomlayout_parse_line("ST_001 castle-1f-1 base 1 2 3") &&
              !roomlayout_parse_line("ST_ZZZ x base 1 0 0 0 0 1 0 0 0 0 1 0"));
    CHECK("a variant of A parses",
          roomlayout_parse_line("ST_030 x variant-of=ST_001 0 0 0 0 0 0 0 0 0 0 0 0"));
    CHECK("a same-place room of B parses",
          roomlayout_parse_line("ST_017 x same-place-as=ST_002 0 0 0 0 0 0 0 0 0 0 0 0"));
    CHECK("a check line parses (and is skipped)",
          roomlayout_parse_line("ST_099 x check 0 0 0 0 0 0 0 0 0 0 0 0"));
    CHECK("the counts", roomlayout_entries() == 5 && roomlayout_skipped() == 1);
    CHECK("names are hex room ids", roomlayout_room_id("ST_00A") == 10 && roomlayout_room_id("ST_GGG") < 0);

    CHECK("A has its matrix", roomlayout_get(1, &wa) && near(wa.m[12], -5425.3857f) && near(wa.m[8], -1.0f));
    CHECK("B has its matrix", roomlayout_get(2, &wb) && near(wb.m[14], 10136.1445f));
    CHECK("a variant gives its base's matrix",
          roomlayout_get(0x30, &wv) && near(wv.m[12], wa.m[12]) && near(wv.m[14], wa.m[14]));
    CHECK("a same-place room gives its base's matrix",
          roomlayout_get(0x17, &wv) && near(wv.m[12], wb.m[12]) && near(wv.m[14], wb.m[14]));
    CHECK("a check room and one not there have none",
          !roomlayout_get(0x99, &wv) && !roomlayout_get(0x98, &wv));
    CHECK("the pair's T is a pure move of about (+202.5, 0, +154.7)",
          roomlayout_between(1, 2, &t) && near(t.m[0], 1.0f) && near(t.m[5], 1.0f) &&
              near(t.m[10], 1.0f) && near(t.m[12], 202.5196f) && near(t.m[13], 0.0f) &&
              near(t.m[14], 154.6601f));
    world_a = mat4_point(&wa, pa);
    moved = mat4_point(&t, pa);
    world_b = mat4_point(&wb, moved);
    CHECK("A's point in the world is where T then B puts it",
          near(world_a.x, world_b.x) && near(world_a.y, world_b.y) && near(world_a.z, world_b.z));
    CHECK("no T to a room with no place", !roomlayout_between(1, 0x98, &t));

#ifdef ROOMLAYOUT_SAMPLE_DIR
    /* the mod's own file (examples/mods/item-props/library/data/PROPS/room_layout.txt) */
    files_layers_clear();
    roomlayout_clear();
    files_layer_add(ROOMLAYOUT_SAMPLE_DIR);
    roomlayout_load_if_needed();
    CHECK("the mod's file loads (167 rooms, no check lines)",
          roomlayout_entries() == 167 && roomlayout_skipped() == 0);
    CHECK("ST_001 and ST_002 are in it", roomlayout_get(1, &wa) && roomlayout_get(2, &wb));
    CHECK("and their T is the move Sean's blend gives",
          roomlayout_between(1, 2, &t) && near(t.m[12], 202.5196f) && near(t.m[13], 0.0f) &&
              near(t.m[14], 154.6601f));
    CHECK("a story version (ST_030 variant-of ST_02F) has a place",
          roomlayout_get(0x30, &wv) && roomlayout_base_id(0x30) == 0x2F);
#endif

    printf("room layout: %d failed\n", fails);
    return fails != 0;
}
