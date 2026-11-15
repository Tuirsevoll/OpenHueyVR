/* The item grips a mod gives its held models (itemprops.h). */
#include "itemprops.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/files.h"

#define GRIP_TOKENS 16
#define GRIP_TOKEN 128
#define GRIP_PATH "PROPS/grips.txt"

static ItemGrip sGrips[ITEMPROPS_ITEMS][2];   /* (0: bone 30, 1: bone 46) */
static int sLoaded;

static int grip_slot(int bone) {
    return bone == 46 ? 1 : 0;
}

static int parse_int(const char *s, int *out) {
    char *e = NULL;
    long v;

    if (s[0] == '$') {
        v = strtol(s + 1, &e, 16);
    } else {
        v = strtol(s, &e, 10);
    }
    if (e == s || *e != 0) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int parse_float(const char *s, float *out) {
    char *e = NULL;
    float v = strtof(s, &e);

    if (e == s || *e != 0 || !isfinite(v)) {
        return 0;
    }
    *out = v;
    return 1;
}

/* the line's words: comments ('#', ';' or '//') end it, as do runs of whitespace */
static int grip_tokens(const char *line, char tok[GRIP_TOKENS][GRIP_TOKEN]) {
    char buf[512];
    size_t n = strcspn(line, "#;/");
    const char *p;
    int nt = 0;

    if (line[n] == '/' && line[n + 1] != '/') {
        n = strlen(line);   /* (a lone '/' is part of a word) */
    }
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, line, n);
    buf[n] = 0;
    p = buf;
    while (*p != 0) {
        int k = 0;

        while (*p != 0 && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == 0) {
            break;
        }
        if (nt >= GRIP_TOKENS) {
            return GRIP_TOKENS + 1;   /* (too many: not a line for us) */
        }
        while (*p != 0 && !isspace((unsigned char)*p) && k < GRIP_TOKEN - 1) {
            tok[nt][k++] = *p++;
        }
        while (*p != 0 && !isspace((unsigned char)*p)) {
            p++;   /* (a word longer than a token: still one word) */
        }
        tok[nt][k] = 0;
        nt++;
    }
    return nt;
}

int itemprops_parse_line(const char *line, ItemGrip *out) {
    char tok[GRIP_TOKENS][GRIP_TOKEN];
    ItemGrip g;
    int nt, i, tint_n;

    if (line == NULL || out == NULL) {
        return 0;
    }
    memset(&g, 0, sizeof(g));
    nt = grip_tokens(line, tok);
    if (nt < 9 || nt > GRIP_TOKENS) {
        return 0;
    }
    if (!parse_int(tok[0], &g.item) || g.item < 0 || g.item >= ITEMPROPS_ITEMS) {
        return 0;
    }
    if (!parse_int(tok[1], &g.bone) || (g.bone != 30 && g.bone != 46)) {
        return 0;   /* (only her two hand bones: 30 her left, 46 her right) */
    }
    if (!parse_float(tok[2], &g.offset.x) || !parse_float(tok[3], &g.offset.y) ||
        !parse_float(tok[4], &g.offset.z)) {
        return 0;
    }
    if (!parse_float(tok[5], &g.rot[0]) || !parse_float(tok[6], &g.rot[1]) ||
        !parse_float(tok[7], &g.rot[2]) || !parse_float(tok[8], &g.rot[3])) {
        return 0;
    }
    g.tint[0] = g.tint[1] = g.tint[2] = 1.0f;
    tint_n = 0;
    for (i = 9; i < nt; i++) {
        float v;

        if (strncmp(tok[i], "model=", 6) == 0) {   /* (a model shared by several items) */
            const char *m = tok[i] + 6;

            if (g.model[0] != 0 || m[0] == 0 || m[0] == '/' || strstr(m, "..") != NULL ||
                strlen(m) >= sizeof(g.model)) {
                return 0;
            }
            snprintf(g.model, sizeof(g.model), "%s", m);
        } else if (parse_float(tok[i], &v)) {       /* (the tint's three numbers) */
            if (tint_n >= 3) {
                return 0;
            }
            g.tint[tint_n++] = v;
        } else {                                    /* (the grip pose's name) */
            if (g.pose[0] != 0 || strlen(tok[i]) >= sizeof(g.pose)) {
                return 0;
            }
            snprintf(g.pose, sizeof(g.pose), "%s", tok[i]);
        }
    }
    if (tint_n != 0 && tint_n != 3) {
        return 0;
    }
    {
        float q2 = g.rot[0] * g.rot[0] + g.rot[1] * g.rot[1] + g.rot[2] * g.rot[2] + g.rot[3] * g.rot[3];

        if (q2 < 1e-12f) {
            return 0;   /* (a turn of no length: not a quaternion) */
        }
        if (fabsf(q2 - 1.0f) > 1e-4f) {
            float k = 1.0f / sqrtf(q2);

            g.rot[0] *= k;
            g.rot[1] *= k;
            g.rot[2] *= k;
            g.rot[3] *= k;
        }
    }
    g.tint[0] = g.tint[0] < 0.0f ? 0.0f : g.tint[0];
    g.tint[1] = g.tint[1] < 0.0f ? 0.0f : g.tint[1];
    g.tint[2] = g.tint[2] < 0.0f ? 0.0f : g.tint[2];
    g.used = 1;
    *out = g;
    return 1;
}

static void grips_load(void) {
    uint8_t *data;
    size_t size = 0;
    char *p;
    int line = 0;

    sLoaded = 1;
    data = files_read(GRIP_PATH, &size);
    if (data == NULL) {
        return;   /* (no grips: every model sits at the wrist as it is) */
    }
    p = (char *)data;
    while (*p != 0) {
        char *nl = strchr(p, '\n');
        ItemGrip g;

        if (nl != NULL) {
            *nl = 0;
        }
        line++;
        if (itemprops_parse_line(p, &g)) {
            int slot = grip_slot(g.bone);

            if (sGrips[g.item][slot].used) {
                fprintf(stderr, "item props: %s:%d: item %d already has a grip for bone %d (the first is kept)\n",
                        GRIP_PATH, line, g.item, g.bone);
            } else {
                sGrips[g.item][slot] = g;
            }
        }
        p = nl != NULL ? nl + 1 : p + strlen(p);
    }
    free(data);
}

void itemprops_reset(void) {
    memset(sGrips, 0, sizeof(sGrips));
    sLoaded = 0;
}

const ItemGrip *itemprops_get(int item, int bone) {
    int slot;

    if (!sLoaded) {
        grips_load();
    }
    if (item < 0 || item >= ITEMPROPS_ITEMS || (bone != 30 && bone != 46)) {
        return NULL;
    }
    slot = grip_slot(bone);
    return sGrips[item][slot].used ? &sGrips[item][slot] : NULL;
}

void itemprops_mirror(ItemGrip *g) {
    if (g == NULL) {
        return;
    }
    g->offset.x = -g->offset.x;
    g->rot[1] = -g->rot[1];
    g->rot[2] = -g->rot[2];
    g->bone = g->bone == 30 ? 46 : 30;
}

int itemprops_grip(int item, int bone, ItemGrip *out) {
    const ItemGrip *g;

    if (out == NULL || (bone != 30 && bone != 46)) {
        return 0;
    }
    g = itemprops_get(item, bone);
    if (g != NULL) {
        *out = *g;
        return 1;
    }
    g = itemprops_get(item, bone == 30 ? 46 : 30);
    if (g != NULL) {
        *out = *g;
        itemprops_mirror(out);
        return 1;
    }
    return 0;
}

void itemprops_model_path(const ItemGrip *g, int item, char *out, size_t n) {
    if (out == NULL || n == 0) {
        return;
    }
    if (g != NULL && g->model[0] != 0) {
        if (strncmp(g->model, "PROPS/", 6) == 0) {
            snprintf(out, n, "%s", g->model);
        } else {
            snprintf(out, n, "PROPS/%s", g->model);
        }
        return;
    }
    snprintf(out, n, "PROPS/item-%d.gltf", item);
}

Mat4 itemprops_matrix(const ItemGrip *g, const Mat4 *wrist) {
    Mat4 r = mat4_identity(), t = mat4_identity(), m;
    float x, y, z, w, xx, yy, zz, xy, xz, yz, xw, yw, zw, q2;

    if (g == NULL) {
        return *wrist;
    }
    x = g->rot[0];
    y = g->rot[1];
    z = g->rot[2];
    w = g->rot[3];
    q2 = x * x + y * y + z * z + w * w;
    if (q2 < 1e-12f) {
        x = y = z = 0.0f;
        w = 1.0f;
    } else if (fabsf(q2 - 1.0f) > 1e-4f) {
        float k = 1.0f / sqrtf(q2);

        x *= k;
        y *= k;
        z *= k;
        w *= k;
    }
    xx = x * x;  yy = y * y;  zz = z * z;
    xy = x * y;  xz = x * z;  yz = y * z;
    xw = x * w;  yw = y * w;  zw = z * w;
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + zw);
    r.m[2] = 2.0f * (xz - yw);
    r.m[4] = 2.0f * (xy - zw);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + xw);
    r.m[8] = 2.0f * (xz + yw);
    r.m[9] = 2.0f * (yz - xw);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    t.m[12] = g->offset.x;
    t.m[13] = g->offset.y;
    t.m[14] = g->offset.z;
    m = mat4_mul(t, r);   /* (the turn about the wrist, then the offset in its axes) */
    return mat4_mul(*wrist, m);
}
