/* The VR markers (src/game/vrmarkers.c): PROPS/hidespots.txt's spot boxes and PROPS/levers.txt's
 * read as boxes - the turn as the file has it, the test inside and outside one, the way in and out
 * of a box's own space, and the box axis nearest room +y (the height axis).
 *
 *   vrmarkers-check */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/files.h"
#include "game/engine.h"
#include "game/vrmarkers.h"

/* (the little of the game vrmarkers.c and triggers.c read when no room is loaded) */
Engine gEngine;

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("vr markers: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) {
    return fabsf(a - b) < 0.0001f;
}

int main(void) {
    /* the mod's two ST_026 lines and its ST_002 lever, as the files have them */
    static const char *grab_line =
        "ST_026 wardrobe grab 24.4114 12.6923 35.2774 3.8153 1.3535 11.7158 0.270598 -0.270598 0.653282 0.653282";
    static const char *bound_line =
        "ST_026 wardrobe bound 21.5715 12.6923 40.8213 3.8153 7.5129 11.7158 0.270598 -0.270598 0.653282 0.653281";
    static const char *lever_line =
        "ST_002 gate 45.8997 -1.2077 170.4921 1.7610 0.8400 5.3092 0.353553 -0.612372 0.353553 0.612372";
    const VrBox *grab, *bound, *lever;
    Vec3 c, p, back, up;
    int idx[4], axis = -1;

    vrmarkers_reset();
    CHECK("a spot's grab line parses", vrmarkers_parse_line(grab_line, 0));
    CHECK("a spot's bound line parses", vrmarkers_parse_line(bound_line, 0));
    CHECK("a lever line parses", vrmarkers_parse_line(lever_line, 1));
    CHECK("a comment, a short line and a bad kind are not boxes",
          !vrmarkers_parse_line("# <room_id> <spot> <grab|bound> c...", 0) &&
              !vrmarkers_parse_line("ST_026 wardrobe", 0) && !vrmarkers_parse_line("ST_026 x nope 0", 0));

    grab = vrmarkers_spot_grab(0);
    bound = vrmarkers_spot_bound(0);
    lever = vrmarkers_lever_box(0);
    CHECK("the accessors give the boxes and their names",
          grab != NULL && bound != NULL && lever != NULL &&
              strcmp(vrmarkers_spot_name(0), "wardrobe") == 0 &&
              strcmp(vrmarkers_lever_name(0), "gate") == 0);
    CHECK("a spot without a bound box gives none", vrmarkers_spot_bound(1) == NULL && vrmarkers_spot_name(1) == NULL);

    CHECK("the room's spot and lever are found by the room's name",
          vrmarkers_spots_here(0x26, idx, 4) == 1 && idx[0] == 0 &&
              vrmarkers_levers_here(0x2, idx, 4) == 1 && idx[0] == 0);
    CHECK("another room has neither", vrmarkers_spots_here(0x27, idx, 4) == 0 &&
                                          vrmarkers_levers_here(0x3, idx, 4) == 0);

    if (bound != NULL && grab != NULL && lever != NULL) {
        c = vec3(bound->c[0], bound->c[1], bound->c[2]);
        CHECK("the bound box's centre is inside its own box", vrbox_inside(bound, c, 0.0f));
        CHECK("... and inside it grown by a margin", vrbox_inside(bound, c, 0.5f));
        p = vrbox_to_room(bound, vec3(bound->h[0] + 10.0f, 0.0f, 0.0f));
        CHECK("a point 10 units outside the bound box is not inside it", !vrbox_inside(bound, p, 0.0f));
        CHECK("... but it is with a margin of 11", vrbox_inside(bound, p, 11.0f));
        p = vrbox_to_room(bound, vec3(bound->h[0] - 0.01f, -(bound->h[1] - 0.01f), bound->h[2] - 0.01f));
        CHECK("a point just inside a corner of it is inside", vrbox_inside(bound, p, 0.0f));

        p = vec3(21.5715f, 12.6923f, 40.8213f);
        back = vrbox_to_room(bound, vrbox_to_local(bound, p));
        CHECK("out of the bound box's space and back gives the point again",
              near(back.x, p.x) && near(back.y, p.y) && near(back.z, p.z));
        p = vec3(45.8997f, -1.2077f, 170.4921f);
        back = vrbox_to_room(lever, vrbox_to_local(lever, p));
        CHECK("the same for the lever's space (a quarter turn each way)",
              near(back.x, p.x) && near(back.y, p.y) && near(back.z, p.z));

        up = vrbox_up_axis(bound, &axis);
        CHECK("the bound box's height axis is its own z, room up",
              axis == 2 && up.y > 0.999f && near(vec3_len(up), 1.0f));
        up = vrbox_up_axis(lever, &axis);
        CHECK("the lever's height axis is a unit axis in its widest room-y",
              axis >= 0 && axis < 3 && near(vec3_len(up), 1.0f) && fabsf(up.y) >= 0.577f);
    } else {
        CHECK("the sample lines gave the boxes", 0);
    }

    {   /* the mod's own two files, through the files layer (the real ones a game reads) */
        int nrows = 0, nlevers = 0, i, whole = 1;

        files_layer_add(ITEMPROPS_SAMPLE_DIR);
        vrmarkers_reset();
        vrmarkers_load_if_needed();   /* (no room: nothing logged of one) */
        while (vrmarkers_spot_name(nrows) != NULL) {
            nrows++;
        }
        while (vrmarkers_lever_name(nlevers) != NULL) {
            nlevers++;
        }
        for (i = 0; i < nrows; i++) {
            if (vrmarkers_spot_grab(i) == NULL || vrmarkers_spot_bound(i) == NULL) {
                whole = 0;
            }
        }
        printf("vr markers: the sample has %d spots, %d levers\n", nrows, nlevers);
        CHECK("the sample's PROPS/hidespots.txt reads as whole spots (a grab and a bound each)",
              nrows > 0 && whole);
        CHECK("the sample's PROPS/levers.txt reads as levers", nlevers > 0);
    }

    printf("vr markers: %d failed\n", fails);
    return fails != 0;
}
