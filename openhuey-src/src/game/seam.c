/* Seams: see seam.h. */
#include "seam.h"
#include "roomlayout.h"
#include "../core/files.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEAMS_MAX 128
#define PI_F 3.14159265f

static Seam sSeam[SEAMS_MAX];
static int sSeamN;
static int sLoaded;

static float wrap_pi(float a) {
    return atan2f(sinf(a), cosf(a));
}

int seam_parse_line(const char *line) {
    char na[24], nb[24], word[48];
    int ea, eb, a, b, n;
    float f[12];
    Seam s;

    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '#' || *line == 0) {
        return 0;
    }
    n = sscanf(line, "%23s %d %23s %d %47s", na, &ea, nb, &eb, word);
    if (n < 4) {
        return 0;
    }
    a = roomlayout_room_id(na);
    b = roomlayout_room_id(nb);
    if (a < 0 || b < 0 || ea < 0 || ea > 15 || eb < 0 || eb > 15) {
        return 0;
    }
    memset(&s, 0, sizeof(s));
    s.a = a;
    s.ea = ea;
    s.b = b;
    s.eb = eb;
    if (n == 5 && strcmp(word, "layout") == 0) {
        s.layout = 1;
    } else if (sscanf(line, "%*s %*d %*s %*d %f %f %f %f %f %f %f %f %f %f %f %f", &f[0], &f[1], &f[2], &f[3], &f[4],
                      &f[5], &f[6], &f[7], &f[8], &f[9], &f[10], &f[11]) == 12) {
        s.fa = (SeamFrame){vec3(f[0], f[1], f[2]), f[3], f[4], f[5]};
        s.fb = (SeamFrame){vec3(f[6], f[7], f[8]), f[9], f[10], f[11]};
    } else {
        return 0;
    }
    if (sSeamN >= SEAMS_MAX) {
        return 0;
    }
    sSeam[sSeamN++] = s;
    return 1;
}

void seam_clear(void) {
    sSeamN = 0;
    sLoaded = 0;
}

void seam_load_if_needed(void) {
    uint8_t *data;
    size_t size = 0, i, ln = 0;
    char line[512];

    if (sLoaded) {
        return;
    }
    sLoaded = 1;
    sSeamN = 0;
    roomlayout_load_if_needed();
    data = files_read("PROPS/seams.txt", &size);
    if (data == NULL) {
        fprintf(stderr, "seam: no PROPS/seams.txt\n");
        return;
    }
    for (i = 0; i <= size; i++) {
        char c = i < size ? (char)data[i] : '\n';

        if (c == '\n' || c == '\r') {
            line[ln] = 0;
            ln = 0;
            if (c == '\r' && i + 1 < size && data[i + 1] == '\n') {
                i++;
            }
            if (seam_parse_line(line)) {
                const Seam *s = &sSeam[sSeamN - 1];

                fprintf(stderr, "seam: ST_%03X exit %d <-> ST_%03X exit %d (%s)\n", s->a, s->ea, s->b, s->eb,
                        s->layout ? "layout" : "frames");
            }
        } else if (ln + 1 < sizeof(line)) {
            line[ln++] = c;
        }
    }
    free(data);
    fprintf(stderr, "seam: %d seam(s)\n", sSeamN);
}

int seam_count(void) {
    return sSeamN;
}

const Seam *seam_at(int i) {
    return i >= 0 && i < sSeamN ? &sSeam[i] : NULL;
}

const Seam *seam_find(int base_a, int ea, int base_b, int eb) {
    int i;

    for (i = 0; i < sSeamN; i++) {
        const Seam *s = &sSeam[i];

        if ((s->a == base_a && s->ea == ea && s->b == base_b && s->eb == eb) ||
            (s->a == base_b && s->ea == eb && s->b == base_a && s->eb == ea)) {
            return s;
        }
    }
    return NULL;
}

float seam_theta(const Seam *s) {
    return wrap_pi(s->fb.yaw + PI_F - s->fa.yaw);
}

const SeamFrame *seam_frame(const Seam *s, int side_is_a) {
    return side_is_a ? &s->fa : &s->fb;
}

/* turn about Y so that a heading (sin a, cos a) becomes (sin(a + theta), cos(a + theta)) */
static Mat4 turn_y(float theta) {
    Mat4 r = mat4_identity();
    float c = cosf(theta), sn = sinf(theta);

    r.m[0] = c;
    r.m[8] = sn;
    r.m[2] = -sn;
    r.m[10] = c;
    return r;
}

static Mat4 move_by(Vec3 p) {
    Mat4 t = mat4_identity();

    t.m[12] = p.x;
    t.m[13] = p.y;
    t.m[14] = p.z;
    return t;
}

int seam_transform(const Seam *s, int from_is_a, Mat4 *T) {
    float theta;
    Mat4 ab;

    if (s == NULL || s->layout) {
        return 0;
    }
    theta = seam_theta(s);
    ab = mat4_mul(move_by(s->fb.p), mat4_mul(turn_y(theta), move_by(vec3_scale(s->fa.p, -1.0f))));
    if (from_is_a) {
        *T = ab;
        return 1;
    }
    return seam_mat4_inverse(&ab, T);
}

int seam_mat4_inverse(const Mat4 *m, Mat4 *out) {
    const float *a = m->m;
    float inv[16], det;
    int i;

    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0.0f) {
        return 0;
    }
    det = 1.0f / det;
    for (i = 0; i < 16; i++) {
        out->m[i] = inv[i] * det;
    }
    return 1;
}
