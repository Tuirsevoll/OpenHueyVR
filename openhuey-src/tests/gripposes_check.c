/* The mod finger grip poses (src/game/gripposes.c): the file's lines (bone names with spaces,
 * optional bone-local moves, comments, malformed lines, the 31-character pose name), the left
 * hand's rotation and move mirrors, slerp's endpoints, the sample's 24 poses / 67 offsets, and
 * the built-in rig self-check on Fiona's VR model.
 *
 *   gripposes-check */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/files.h"
#include "data/model.h"
#include "data/modelgltf.h"
#include "game/actor.h"
#include "game/gripposes.h"

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("grip poses: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

static int load_model(Model *m, const char *name) {
    char path[160];
    uint8_t *json, *bin, *motions;
    size_t len = 0, bs = 0, ms = 0;
    int ok;

    snprintf(path, sizeof(path), "%s.model/model.gltf", name);
    json = files_read(path, &len);
    if (json == NULL) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s.model/model.bin", name);
    bin = files_read(path, &bs);
    snprintf(path, sizeof(path), "%s.model/motions.bin", name);
    motions = files_read(path, &ms);
    ok = bin != NULL && modelgltf_read(m, (const char *)json, len, bin, bs, motions, motions != NULL ? ms : 0);
    free(json);
    free(bin);
    return ok;
}

static Mat4 rigid_inv(const Mat4 *m) {
    Mat4 r = mat4_identity();

    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2] = m->m[8];
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6] = m->m[9];
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10];
    r.m[12] = -(r.m[0] * m->m[12] + r.m[4] * m->m[13] + r.m[8] * m->m[14]);
    r.m[13] = -(r.m[1] * m->m[12] + r.m[5] * m->m[13] + r.m[9] * m->m[14]);
    r.m[14] = -(r.m[2] * m->m[12] + r.m[6] * m->m[13] + r.m[10] * m->m[14]);
    return r;
}

int main(void) {
    static const char *text =
        "# a comment\n"
        "\n"
        "pose grip_test\n"
        "arm right finger 1a 0 0 0 1 0.1 -0.2 0.3\n"
        "arm right finger 2c 0 0 0 2   # a turn of length 2 is normalised\n"
        "arm right finger 6c 0 0 0 1\n"
        "arm right finger 3c 0 0 0 0\n"
        "arm right finger 4c 0 0 0\n"
        "arm right finger 5c nope 0 0 1\n"
        "pose 1234567890123456789012345678901\n"
        "arm right finger 2c 0 0 0 1\n"
        "pose 12345678901234567890123456789012\n"
        "arm right finger 2c 0 0 0 1\n";
    VrGripPose poses[VRGRIP_POSES];
    VrGripPose one[2];
    int lines = 0, skipped = 0, n;
    float a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float b[4] = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
    float q[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float out[4];
    uint8_t *data;
    size_t size = 0;
    Model m;
    VrGripCheck chk;

    n = vrgrip_parse_text(text, poses, VRGRIP_POSES, &lines, &skipped);
    CHECK("the parser reads the two valid poses and skips the bad lines",
          n == 2 && lines == 4 && skipped == 5);
    CHECK("a 7-number line reads the bone-local move",
          poses[0].nbones == 3 && near(poses[0].bone[0].t[0], 0.1f) &&
              near(poses[0].bone[0].t[1], -0.2f) && near(poses[0].bone[0].t[2], 0.3f));
    CHECK("a 4-number line defaults the move to zero",
          near(poses[0].bone[1].t[0], 0.0f) && near(poses[0].bone[1].t[1], 0.0f) &&
              near(poses[0].bone[1].t[2], 0.0f));
    CHECK("a bone name with spaces and a normalised turn",
          strcmp(poses[0].bone[0].name, "arm right finger 1a") == 0 &&
              near(poses[0].bone[0].q[3], 1.0f) && near(poses[0].bone[1].q[3], 1.0f) &&
              strcmp(poses[0].bone[1].name, "arm right finger 2c") == 0);
    CHECK("a 31-character pose name", strcmp(poses[1].name, "1234567890123456789012345678901") == 0);

    n = vrgrip_parse_text("pose p\narm right finger 2c 0 0 0 1\n", one, 1, &lines, &skipped);
    CHECK("one pose, one bone line, nothing skipped", n == 1 && one[0].nbones == 1 && lines == 1 && skipped == 0);

    vrgrip_mirror_q(q);
    CHECK("the left hand's mirror is (x, -y, -z, w)", near(q[0], 1.0f) && near(q[1], -2.0f) &&
                                                         near(q[2], -3.0f) && near(q[3], 4.0f));

    {
        float t[3] = {0.25f, -0.5f, 0.75f};

        vrgrip_mirror_t(t);
        CHECK("the left hand's move mirror is (-x, y, z)",
              near(t[0], -0.25f) && near(t[1], -0.5f) && near(t[2], 0.75f));
    }

    vrgrip_slerp(a, b, 0.0f, out);
    CHECK("slerp at 0 gives a", near(out[0], a[0]) && near(out[1], a[1]) && near(out[2], a[2]) &&
                                    near(out[3], a[3]));
    vrgrip_slerp(a, b, 1.0f, out);
    CHECK("slerp at 1 gives b", near(out[0], b[0]) && near(out[1], b[1]) && near(out[2], b[2]) &&
                                    near(out[3], b[3]));
    vrgrip_slerp(a, b, 0.5f, out);
    CHECK("slerp half way is a unit turn", near(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] +
                                                    out[3] * out[3], 1.0f));

    files_layer_add(GRIPPOSES_POSE_DIR);
    data = files_read("PROPS/grip_poses.txt", &size);
    CHECK("the sample's PROPS/grip_poses.txt is found", data != NULL && size > 0);
    if (data != NULL) {
        n = vrgrip_parse_text((const char *)data, poses, VRGRIP_POSES, &lines, &skipped);
        CHECK("the sample has 24 poses and 360 bone lines", n == 24 && lines == 360 && skipped == 0);
        free(data);
        {
            int offsets = 0, pi, bi;

            for (pi = 0; pi < n; pi++) {
                for (bi = 0; bi < poses[pi].nbones; bi++) {
                    const float *t = poses[pi].bone[bi].t;

                    if (t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f) {
                        offsets++;
                    }
                }
            }
            CHECK("the sample has 67 knuckle offsets", offsets == 67);
        }
    }

    files_layer_add(GRIPPOSES_MODEL_DIR);
    memset(&m, 0, sizeof(m));
    CHECK("Fiona's VR model is found", load_model(&m, "O_FIN/FIN_000"));
    CHECK("the rig self-check runs and passes", vrgrip_selfcheck_model(&m, poses, n, &chk) && chk.ran && chk.pass);
    printf("grip poses: self-check R2a (%.3f %.3f %.3f), R3a (%.3f %.3f %.3f), "
           "R5a (%.3f %.3f %.3f), R2c (%.3f %.3f %.3f)\n",
           (double)chk.got[0][0], (double)chk.got[0][1], (double)chk.got[0][2], (double)chk.got[1][0],
           (double)chk.got[1][1], (double)chk.got[1][2], (double)chk.got[2][0], (double)chk.got[2][1],
           (double)chk.got[2][2], (double)chk.got[3][0], (double)chk.got[3][1], (double)chk.got[3][2]);

    {   /* the runtime path: the pose is applied in 0.10 s, and a release restores the rest pose */
        Actor a;
        Mat4 w[MODEL_MAX_BONES];
        int b2a = model_bone_named(&m, "arm right finger 2a");
        int b2c = model_bone_named(&m, "arm right finger 2c");
        int wr = model_bone_named(&m, "bone 46");
        Vec3 p;
        int i;

        memset(&a, 0, sizeof(a));
        a.model = m;
        a.face.hand[1].a = 0;
        a.face.hand[1].b = 1;
        a.face.hand[1].w = 1.0f;
        vrgrip_reset();
        vrgrip_want(1, "grip_orb", 145);
        for (i = 0; i < m.nbones; i++) {
            w[i] = rigid_inv(&m.bones[i].inv_bind);
        }
        vrgrip_apply(&a, w, 0.10f);
        p = mat4_point(&m.bones[wr].inv_bind, vec3(w[b2a].m[12], w[b2a].m[13], w[b2a].m[14]));
        CHECK("the runtime path moves the grip_orb knuckle",
              fabsf(p.x + 0.892f) < 0.012f && fabsf(p.y - 0.144f) < 0.012f &&
                  fabsf(p.z - 0.227f) < 0.012f);
        p = mat4_point(&m.bones[wr].inv_bind, vec3(w[b2c].m[12], w[b2c].m[13], w[b2c].m[14]));
        CHECK("the runtime path reaches the grip_orb tip",
              fabsf(p.x + 1.085f) < 0.012f && fabsf(p.y + 0.384f) < 0.012f &&
                  fabsf(p.z - 0.261f) < 0.012f);
        CHECK("the pose holds the hand at its rest morph", a.face.hand[1].w < 0.01f);

        vrgrip_want(1, NULL, -1);
        vrgrip_apply(&a, w, 0.10f);
        p = mat4_point(&m.bones[wr].inv_bind, vec3(w[b2c].m[12], w[b2c].m[13], w[b2c].m[14]));
        CHECK("a release restores the finger's rest tip",
              fabsf(p.x + 1.468f) < 0.05f && fabsf(p.y - 0.035f) < 0.05f && fabsf(p.z - 0.248f) < 0.05f);
        vrgrip_reset();
    }
    model_free(&m);

    printf("grip poses: %d failed\n", fails);
    return fails != 0;
}
