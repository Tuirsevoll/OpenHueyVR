/* The held items' grips (src/game/itemprops.c): a line of PROPS/grips.txt read as the item's
 * wrist bone, offset, turn, optional grip pose's name and optional tint; and the matrix the model
 * is drawn with - wrist * T(offset) * R(quaternion). No models, no files: the text and the maths.
 *
 *   itemprops-check */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/itemprops.h"
#include "core/files.h"

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("item props: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) {
    return fabsf(a - b) < 0.0001f;
}

int main(void) {
    ItemGrip g;
    ItemGrip mg;
    const ItemGrip *gp;
    Mat4 wrist = mat4_identity(), m;
    Vec3 p;
    char path[128];

    memset(&g, 0, sizeof(g));
    CHECK("the plain line", itemprops_parse_line("39 46 0 1 0 0 0 0 1", &g) == 1 &&
                                g.item == 39 && g.bone == 46 && near(g.offset.x, 0.0f) &&
                                near(g.offset.y, 1.0f) && near(g.offset.z, 0.0f) &&
                                near(g.rot[0], 0.0f) && near(g.rot[3], 1.0f) &&
                                near(g.tint[0], 1.0f) && near(g.tint[1], 1.0f) && near(g.tint[2], 1.0f) &&
                                g.pose[0] == 0);

    CHECK("a grip pose's name", itemprops_parse_line("112 30 0.5 -1 2 0 0 0 1 SampleGrip", &g) == 1 &&
                                   g.item == 112 && g.bone == 30 && near(g.offset.x, 0.5f) &&
                                   near(g.offset.y, -1.0f) && near(g.offset.z, 2.0f) &&
                                   strcmp(g.pose, "SampleGrip") == 0 && near(g.tint[0], 1.0f));

    CHECK("a tint alone", itemprops_parse_line("145 46 0 0 0 0 0 0 1 0.5 0.25 0.125", &g) == 1 &&
                              g.item == 145 && near(g.tint[0], 0.5f) && near(g.tint[1], 0.25f) &&
                              near(g.tint[2], 0.125f) && g.pose[0] == 0);

    CHECK("a name and a tint", itemprops_parse_line("39 46 0 0 0 0 0 0 1 grip 1 0.5 0.25", &g) == 1 &&
                                   strcmp(g.pose, "grip") == 0 && near(g.tint[0], 1.0f) &&
                                   near(g.tint[1], 0.5f) && near(g.tint[2], 0.25f));

    CHECK("a comment is not a grip", itemprops_parse_line("# 39 46 0 0 0 0 0 0 1", &g) == 0);
    CHECK("a blank line is not a grip", itemprops_parse_line("   \t ", &g) == 0);
    CHECK("her other bones are not hand bones", itemprops_parse_line("39 31 0 0 0 0 0 0 1", &g) == 0);
    CHECK("a zero quaternion is not a turn", itemprops_parse_line("39 46 0 0 0 0 0 0 0", &g) == 0);
    CHECK("too few words", itemprops_parse_line("39 46 0 0 0 0 0 0", &g) == 0);
    CHECK("a tint needs three numbers", itemprops_parse_line("39 46 0 0 0 0 0 0 1 1 0.5", &g) == 0);
    CHECK("too many words", itemprops_parse_line("39 46 0 0 0 0 0 0 1 a b c d e f g h", &g) == 0);

    CHECK("a turn of length 2 is normalized", itemprops_parse_line("39 46 0 0 0 0 0 0 2", &g) == 1 &&
                                                 near(g.rot[3], 1.0f));

    /* the offset, with no turn: the wrist's origin moves by it */
    CHECK("the offset is in the wrist's axes", itemprops_parse_line("39 46 1 2 3 0 0 0 1", &g) == 1);
    m = itemprops_matrix(&g, &wrist);
    p = mat4_point(&m, vec3(0.0f, 0.0f, 0.0f));
    CHECK("the model's origin at the offset", near(p.x, 1.0f) && near(p.y, 2.0f) && near(p.z, 3.0f));

    /* no line: the wrist itself */
    m = itemprops_matrix(NULL, &wrist);
    p = mat4_point(&m, vec3(4.0f, 5.0f, 6.0f));
    CHECK("no line is the wrist as it is", near(p.x, 4.0f) && near(p.y, 5.0f) && near(p.z, 6.0f));

    /* a quarter turn about z: +x becomes +y, then the offset is added */
    CHECK("a quarter turn about z", itemprops_parse_line("39 46 1 0 0 0 0 0.70710678 0.70710678", &g) == 1);
    m = itemprops_matrix(&g, &wrist);
    p = mat4_point(&m, vec3(1.0f, 0.0f, 0.0f));
    CHECK("the turn about the wrist, then the offset", near(p.x, 1.0f) && near(p.y, 1.0f) && near(p.z, 0.0f));

    /* a wrist moved in the room takes the model with it */
    wrist.m[12] = 10.0f;
    wrist.m[13] = 20.0f;
    wrist.m[14] = 30.0f;
    m = itemprops_matrix(NULL, &wrist);
    p = mat4_point(&m, vec3(0.0f, 0.0f, 0.0f));
    CHECK("the wrist's place is kept", near(p.x, 10.0f) && near(p.y, 20.0f) && near(p.z, 30.0f));

    /* mirroring a line made for the other hand: x negated, the turn's y and z negated */
    CHECK("a line for her left hand", itemprops_parse_line("39 30 1 2 3 0 0.70710678 0 0.70710678", &g) == 1);
    mg = g;
    itemprops_mirror(&mg);
    CHECK("its mirror across her body is her right hand",
          mg.bone == 46 && near(mg.offset.x, -1.0f) && near(mg.offset.y, 2.0f) && near(mg.offset.z, 3.0f) &&
              near(mg.rot[0], 0.0f) && near(mg.rot[1], -0.70710678f) && near(mg.rot[2], 0.0f) &&
              near(mg.rot[3], 0.70710678f));

    /* a shared model file, named by the line */
    CHECK("a shared model name and a tint",
          itemprops_parse_line("145 46 0 0 0 0 0 0 1 model=antimony.gltf 0.5 0.25 0.125", &g) == 1 &&
              strcmp(g.model, "antimony.gltf") == 0 && near(g.tint[0], 0.5f) && near(g.tint[1], 0.25f) &&
              near(g.tint[2], 0.125f));
    itemprops_model_path(&g, 145, path, sizeof(path));
    CHECK("the model's path in PROPS/", strcmp(path, "PROPS/antimony.gltf") == 0);
    mg = g;
    itemprops_mirror(&mg);
    CHECK("the mirror keeps the shared model", strcmp(mg.model, "antimony.gltf") == 0);
    itemprops_model_path(NULL, 145, path, sizeof(path));
    CHECK("no model name: the item's own file", strcmp(path, "PROPS/item-145.gltf") == 0);
    itemprops_parse_line("145 46 0 0 0 0 0 0 1 model=PROPS/antimony.gltf", &g);
    itemprops_model_path(&g, 145, path, sizeof(path));
    CHECK("a path already in PROPS/ is used as it is", strcmp(path, "PROPS/antimony.gltf") == 0);
    CHECK("a model out of PROPS/ is refused",
          itemprops_parse_line("145 46 0 0 0 0 0 0 1 model=../antimony.gltf", &g) == 0);
    CHECK("an empty model name is refused", itemprops_parse_line("145 46 0 0 0 0 0 0 1 model=", &g) == 0);

    /* the sample's own grips.txt, read through the files layer */
    files_layer_add(ITEMPROPS_SAMPLE_DIR);
    itemprops_reset();
    gp = itemprops_get(148, 46);
    CHECK("the sample's grips.txt: item 148 with its name, shared model and tint",
          gp != NULL && gp->item == 148 && gp->bone == 46 && strcmp(gp->pose, "sample-grip") == 0 &&
              strcmp(gp->model, "antimony.gltf") == 0 && near(gp->offset.x, 1.0f) && near(gp->offset.y, 0.5f) &&
              near(gp->offset.z, 0.0f) && near(gp->tint[0], 1.0f) && near(gp->tint[1], 0.75f) &&
              near(gp->tint[2], 0.75f));
    itemprops_model_path(gp, 148, path, sizeof(path));
    CHECK("the sample's grips.txt: item 148's model path", strcmp(path, "PROPS/antimony.gltf") == 0);
    CHECK("the sample's grips.txt: item 148 has no line for the left hand", itemprops_get(148, 30) == NULL);
    CHECK("the sample's grips.txt: item 148 mirrors into the left hand",
          itemprops_grip(148, 30, &mg) == 1 && mg.bone == 30 && strcmp(mg.model, "antimony.gltf") == 0 &&
              near(mg.offset.x, -1.0f) && near(mg.offset.y, 0.5f) && near(mg.offset.z, 0.0f) &&
              near(mg.tint[1], 0.75f));

    gp = itemprops_get(145, 46);
    CHECK("the sample's grips.txt: item 145's right hand",
          gp != NULL && gp->item == 145 && gp->bone == 46 && gp->pose[0] == 0 &&
              strcmp(gp->model, "antimony.gltf") == 0 && near(gp->offset.x, 0.0f) && near(gp->offset.y, 0.0f) &&
              near(gp->offset.z, 0.0f) && near(gp->tint[0], 0.55f) && near(gp->tint[1], 0.55f) &&
              near(gp->tint[2], 0.55f));
    gp = itemprops_get(145, 30);
    CHECK("the sample's grips.txt: item 145's left hand, its own line",
          gp != NULL && gp->item == 145 && gp->bone == 30 && gp->pose[0] == 0 &&
              strcmp(gp->model, "antimony.gltf") == 0 && near(gp->offset.x, 0.0f) && near(gp->offset.y, 0.2f) &&
              near(gp->offset.z, 0.0f) && near(gp->rot[1], 0.70710678f) && near(gp->tint[0], 0.3f));
    itemprops_model_path(itemprops_get(145, 30), 145, path, sizeof(path));
    CHECK("the sample's grips.txt: both items name one model", strcmp(path, "PROPS/antimony.gltf") == 0);
    CHECK("the second line overrides the mirror",
          itemprops_grip(145, 30, &mg) == 1 && mg.bone == 30 && near(mg.offset.x, 0.0f) &&
              near(mg.offset.y, 0.2f) && near(mg.rot[1], 0.70710678f) && near(mg.tint[0], 0.3f));

    CHECK("the sample's grips.txt: no line for item 39",
          itemprops_get(39, 30) == NULL && itemprops_get(39, 46) == NULL && itemprops_grip(39, 30, &mg) == 0);

    printf("item props: %d failed\n", fails);
    return fails != 0;
}
