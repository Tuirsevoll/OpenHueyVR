/* The seams (src/game/seam.c): PROPS/seams.txt's lines as the loader parses them, and the transform
 * of a frame pair - the main test doorway's numbers move by about (-22.2, -0.09, +2.2) and do not
 * turn - and a turned one.
 *
 *   seam-check */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/mathx.h"
#include "game/roomlayout.h"
#include "game/seam.h"

static int fails;

#define CHECK(what, ok)                                      \
    do {                                                     \
        int _ok = (ok);                                      \
        printf("seam: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL"); \
        if (!_ok) {                                          \
            fails++;                                         \
        }                                                    \
    } while (0)

static int near(float a, float b) {
    return fabsf(a - b) < 0.002f;
}

int main(void) {
    static const char *main_line =
        "ST_008 3 ST_01E 1  -75.200 4.144 -142.250 1.5708 39.94 30.00  -97.402 4.058 -140.031 -1.5708 39.94 30.00";
    const Seam *s;
    Mat4 t, back;
    Vec3 pa, pb, q;

    seam_clear();
    CHECK("a comment is not a seam", !seam_parse_line("# roomA exitA roomB exitB | A: x y z"));
    CHECK("a frame line parses", seam_parse_line(main_line));
    CHECK("a layout line parses", seam_parse_line("ST_001 2 ST_002 2  layout"));
    CHECK("a short or bad line does not",
          !seam_parse_line("ST_001 2 ST_002 2  1 2 3") && !seam_parse_line("ST_ZZZ 2 ST_002 2 layout") &&
              !seam_parse_line("ST_001 99 ST_002 2 layout"));
    CHECK("two seams", seam_count() == 2);
    s = seam_find(0x1E, 1, 0x08, 3);
    CHECK("found in either order", s != NULL && s->a == 8 && s->b == 0x1E && !s->layout);
    CHECK("the layout seam has no frames", seam_find(1, 2, 2, 2) != NULL && seam_find(1, 2, 2, 2)->layout);
    if (s == NULL) {
        return 1;
    }
    CHECK("the main pair's turn is about 0", fabsf(seam_theta(s)) < 0.001f);
    CHECK("a layout seam has no transform", !seam_transform(seam_find(1, 2, 2, 2), 1, &t));
    CHECK("A's transform exists", seam_transform(s, 1, &t));
    CHECK("it moves by about (-22.2, -0.09, +2.2)",
          near(t.m[12], -22.202f) && near(t.m[13], -0.086f) && near(t.m[14], 2.219f));
    pa = vec3(-75.2f, 4.144f, -142.25f);
    pb = mat4_point(&t, pa);
    CHECK("A's frame point lands on B's", near(pb.x, -97.402f) && near(pb.y, 4.058f) && near(pb.z, -140.031f));
    CHECK("the inverse is B -> A", seam_transform(s, 0, &back));
    q = mat4_point(&back, vec3(-97.402f, 4.058f, -140.031f));
    CHECK("B's frame point lands on A's", near(q.x, pa.x) && near(q.y, pa.y) && near(q.z, pa.z));
    /* 3 units out of A (along +x, its yaw 90 degrees) is 3 units into B (along +x, in from B's -x out) */
    q = mat4_point(&t, vec3(pa.x + 3.0f, pa.y, pa.z));
    CHECK("3 out of A is 3 into B", near(q.x, -97.402f + 3.0f) && near(q.z, -140.031f));

    /* a turned seam: A out +z (yaw 0), B out +x (yaw 90): she walks out of A along +z and into B
     * along -x (in from B's +x out), a turn of yawB + pi - yawA = 3 pi / 2 = -pi / 2 */
    CHECK("a turned line parses", seam_parse_line("ST_002 0 ST_009 0  10 0 20 0 8 30  100 5 -50 1.5708 8 30"));
    s = seam_find(2, 0, 9, 0);
    CHECK("it turns by -pi/2", s != NULL && near(seam_theta(s), -1.5708f) );
    if (s != NULL && seam_transform(s, 1, &t)) {
        Vec3 f0 = vec3(0.0f, 0.0f, 1.0f), f1;

        pb = mat4_point(&t, vec3(10.0f, 0.0f, 20.0f));
        CHECK("its frame point lands on B's", near(pb.x, 100.0f) && near(pb.z, -50.0f));
        f1 = vec3(t.m[0] * f0.x + t.m[4] * f0.y + t.m[8] * f0.z, 0.0f, t.m[2] * f0.x + t.m[6] * f0.y + t.m[10] * f0.z);
        CHECK("A's out (+z) becomes B's in (-x)", near(f1.x, -1.0f) && near(f1.z, 0.0f));
        q = mat4_point(&t, vec3(10.0f, 0.0f, 23.0f));
        CHECK("3 out of A is 3 into B", near(q.x, 100.0f - 3.0f) && near(q.z, -50.0f));
    }
    {
        Mat4 id = mat4_identity(), inv;

        id.m[0] = 2.0f;
        CHECK("a general inverse", seam_mat4_inverse(&id, &inv) && near(inv.m[0], 0.5f));
    }
    printf("seam: %s\n", fails == 0 ? "all pass" : "FAILED");
    return fails == 0 ? 0 : 1;
}
