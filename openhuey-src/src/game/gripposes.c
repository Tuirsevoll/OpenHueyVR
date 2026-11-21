/* Mod finger grip poses (gripposes.h). */
#include "gripposes.h"

#include "actor.h"
#include "vrbones.h"

#include "../core/files.h"
#include "../data/model.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRIP_POSE_LOG 1
#define GRIP_POSE_PATH "PROPS/grip_poses.txt"
#define GRIP_POSE_BLEND_IN 0.10f
#define GRIP_POSE_BLEND_OUT 0.10f
#define GRIP_POSE_LINE 512
#define GRIP_POSE_WORDS 64

typedef struct Quat {
    float x, y, z, w;
} Quat;

static Quat q_identity(void) {
    Quat q = {0.0f, 0.0f, 0.0f, 1.0f};

    return q;
}

static Quat q_normalize(Quat q) {
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);

    if (n < 1.0e-12f) {
        return q_identity();
    }
    q.x /= n;
    q.y /= n;
    q.z /= n;
    q.w /= n;
    return q;
}

/* a * b: b applies first (the same order as mat4_mul) */
static Quat q_mul(Quat a, Quat b) {
    Quat r;

    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return r;
}

/* turn v by the unit quaternion q: q * (v, 0) * inverse(q) */
static Vec3 q_rotate(Quat q, Vec3 v) {
    Quat qv = {v.x, v.y, v.z, 0.0f};
    Quat qi = {-q.x, -q.y, -q.z, q.w};
    Quat r = q_mul(q_mul(q, qv), qi);

    return vec3(r.x, r.y, r.z);
}

/* The rotation of a matrix's columns, with the scale removed and the frame re-orthonormalised.
 * world[] carries the hand's scale, so this is how the bone's own turn is read back. */
static Quat q_from_mat(const Mat4 *m) {
    Vec3 x = vec3(m->m[0], m->m[1], m->m[2]);
    Vec3 y = vec3(m->m[4], m->m[5], m->m[6]);
    Vec3 z;
    float lx = vec3_len(x), ly, lz;
    float tr;
    Quat q;

    if (lx < 1.0e-9f) {
        return q_identity();
    }
    x = vec3_scale(x, 1.0f / lx);
    y = vec3_sub(y, vec3_scale(x, vec3_dot(y, x)));
    ly = vec3_len(y);
    if (ly < 1.0e-9f) {
        return q_identity();
    }
    y = vec3_scale(y, 1.0f / ly);
    z = vec3_cross(x, y);
    lz = vec3_len(z);
    if (lz < 1.0e-9f) {
        return q_identity();
    }
    z = vec3_scale(z, 1.0f / lz);
    /* (x, y, z are now the columns of a pure rotation) */
    tr = x.x + y.y + z.z;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;

        q.w = 0.25f * s;
        q.x = (y.z - z.y) / s;
        q.y = (z.x - x.z) / s;
        q.z = (x.y - y.x) / s;
    } else if (x.x > y.y && x.x > z.z) {
        float s = sqrtf(1.0f + x.x - y.y - z.z) * 2.0f;

        q.w = (y.z - z.y) / s;
        q.x = 0.25f * s;
        q.y = (y.x + x.y) / s;
        q.z = (z.x + x.z) / s;
    } else if (y.y > z.z) {
        float s = sqrtf(1.0f + y.y - x.x - z.z) * 2.0f;

        q.w = (z.x - x.z) / s;
        q.x = (y.x + x.y) / s;
        q.y = 0.25f * s;
        q.z = (z.y + y.z) / s;
    } else {
        float s = sqrtf(1.0f + z.z - x.x - y.y) * 2.0f;

        q.w = (x.y - y.x) / s;
        q.x = (z.x + x.z) / s;
        q.y = (z.y + y.z) / s;
        q.z = 0.25f * s;
    }
    return q_normalize(q);
}

/* The same matrix convention as mathx.h: column-major, a * b means b applies first. */
static Mat4 mat4_rot(Quat q) {
    Mat4 r = mat4_identity();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

static Mat4 mat4_at(float x, float y, float z) {
    Mat4 m = mat4_identity();

    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

/* rigid inverse: the transpose of the rotation and the negated turned place (as vrhands.c) */
static Mat4 rigid_inverse(const Mat4 *m) {
    Mat4 r = mat4_identity();

    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2] = m->m[8];
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6] = m->m[9];
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10];
    r.m[12] = -(r.m[0] * m->m[12] + r.m[4] * m->m[13] + r.m[8] * m->m[14]);
    r.m[13] = -(r.m[1] * m->m[12] + r.m[5] * m->m[13] + r.m[9] * m->m[14]);
    r.m[14] = -(r.m[2] * m->m[12] + r.m[6] * m->m[13] + r.m[10] * m->m[14]);
    return r;
}

/* bone i's rest transform relative to its parent: inv_bind(parent) * inverse(inv_bind(i)) */
static Mat4 local_rest(const Model *m, int i) {
    int p = m->bones[i].parent;

    if (p < 0 || p >= m->nbones) {
        return rigid_inverse(&m->bones[i].inv_bind);
    }
    return mat4_mul(m->bones[p].inv_bind, rigid_inverse(&m->bones[i].inv_bind));
}

/* The local turn of a bone in this frame: inverse(world[parent]) * world[i], with the scale
 * taken off the rotation columns. */
static Quat local_turn(const Mat4 *world, const Model *m, int i) {
    int p = m->bones[i].parent;
    Mat4 rel;

    if (p < 0 || p >= m->nbones) {
        rel = world[i];
    } else {
        rel = mat4_mul(rigid_inverse(&world[p]), world[i]);
    }
    return q_from_mat(&rel);
}

/* Does `bone` descend from `root`? (The same bounded walk vrhands.c uses.) */
static int descends(const Model *m, int bone, int root) {
    int n = 0;

    if (bone < 0 || bone >= m->nbones || root < 0 || root >= m->nbones) {
        return 0;
    }
    for (bone = m->bones[bone].parent; bone >= 0 && n < MODEL_MAX_BONES; bone = m->bones[bone].parent, n++) {
        if (bone == root) {
            return 1;
        }
    }
    return 0;
}

static int bone_depth(const Model *m, int i) {
    int n = 0;

    while (i >= 0 && i < m->nbones && n < MODEL_MAX_BONES) {
        i = m->bones[i].parent;
        n++;
    }
    return n;
}

static int parse_float_word(const char *s, float *out) {
    char *e = NULL;
    float v = strtof(s, &e);

    if (e == s || *e != 0 || !isfinite(v)) {
        return 0;
    }
    *out = v;
    return 1;
}

void vrgrip_mirror_q(float q[4]) {
    if (q == NULL) {
        return;
    }
    q[1] = -q[1];
    q[2] = -q[2];
}

void vrgrip_mirror_t(float t[3]) {
    if (t == NULL) {
        return;
    }
    t[0] = -t[0];   /* model X is left-right: the bone's own X is the odd axis */
}

void vrgrip_slerp(const float a[4], const float b[4], float t, float out[4]) {
    Quat qa, qb;
    float d, s0, s1;

    if (out == NULL) {
        return;
    }
    if (a == NULL) {
        a = b;
    }
    if (b == NULL) {
        b = a;
    }
    if (t <= 0.0f) {
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        out[3] = a[3];
        return;
    }
    if (t >= 1.0f) {
        out[0] = b[0];
        out[1] = b[1];
        out[2] = b[2];
        out[3] = b[3];
        return;
    }
    qa.x = a[0]; qa.y = a[1]; qa.z = a[2]; qa.w = a[3];
    qb.x = b[0]; qb.y = b[1]; qb.z = b[2]; qb.w = b[3];
    qa = q_normalize(qa);
    qb = q_normalize(qb);
    d = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    if (d < 0.0f) {   /* shortest path */
        qb.x = -qb.x;
        qb.y = -qb.y;
        qb.z = -qb.z;
        qb.w = -qb.w;
        d = -d;
    }
    if (d > 0.9995f) {   /* nearly the same: straight line, normalised */
        qb.x = qa.x + (qb.x - qa.x) * t;
        qb.y = qa.y + (qb.y - qa.y) * t;
        qb.z = qa.z + (qb.z - qa.z) * t;
        qb.w = qa.w + (qb.w - qa.w) * t;
        qb = q_normalize(qb);
    } else {
        float th = acosf(d);
        s0 = sinf((1.0f - t) * th);
        s1 = sinf(t * th);
        qb.x = (qa.x * s0 + qb.x * s1) / sinf(th);
        qb.y = (qa.y * s0 + qb.y * s1) / sinf(th);
        qb.z = (qa.z * s0 + qb.z * s1) / sinf(th);
        qb.w = (qa.w * s0 + qb.w * s1) / sinf(th);
    }
    out[0] = qb.x;
    out[1] = qb.y;
    out[2] = qb.z;
    out[3] = qb.w;
}

int vrgrip_parse_text(const char *text, VrGripPose *poses, int max_poses, int *bone_lines, int *skipped) {
    const char *p;
    VrGripPose *cur = NULL;
    int nposes = 0, lines = 0, bad = 0;

    if (bone_lines != NULL) {
        *bone_lines = 0;
    }
    if (skipped != NULL) {
        *skipped = 0;
    }
    if (text == NULL || poses == NULL || max_poses <= 0) {
        if (text != NULL && skipped != NULL) {
            *skipped = 1;
        }
        return 0;
    }
    p = text;
    while (*p != 0) {
        const char *e = p;
        char line[GRIP_POSE_LINE];
        char *words[GRIP_POSE_WORDS];
        size_t n;
        int nw = 0;
        char *s;

        while (*e != 0 && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n > 0 && p[n - 1] == '\r') {
            n--;
        }
        if (n >= sizeof(line)) {
            bad++;
        } else {
            memcpy(line, p, n);
            line[n] = 0;
            s = strchr(line, '#');
            if (s != NULL) {
                *s = 0;   /* (the format's comment: the rest of the line) */
            }
            s = line;
            while (*s != 0) {
                while (*s != 0 && isspace((unsigned char)*s)) {
                    s++;
                }
                if (*s == 0 || nw >= GRIP_POSE_WORDS) {
                    break;
                }
                words[nw++] = s;
                while (*s != 0 && !isspace((unsigned char)*s)) {
                    s++;
                }
                if (*s != 0) {
                    *s++ = 0;
                }
            }
            if (nw == 0) {
                /* blank or a comment: not a line */
            } else if (strcmp(words[0], "pose") == 0) {
                size_t l = nw > 1 ? strlen(words[1]) : 0;

                if (l == 0 || l >= sizeof(poses[0].name)) {
                    cur = NULL;
                    bad++;
                } else if (nposes >= max_poses) {
                    cur = NULL;
                    bad++;
                } else {
                    cur = &poses[nposes++];
                    memset(cur, 0, sizeof(*cur));
                    snprintf(cur->name, sizeof(cur->name), "%s", words[1]);
                }
            } else if (cur == NULL) {
                bad++;
            } else if (nw < 5 || cur->nbones >= VRGRIP_BONES) {
                bad++;
            } else {
                char name[32];
                float q[4], t[3] = {0.0f, 0.0f, 0.0f};
                float tmp;
                size_t k, j = 0, first_num;
                int nnum = 0;

                /*
                 * The numbers are the line's numeric suffix, so bone names can contain spaces.
                 * Four are the quaternion; three more are the optional bone-local move. Any
                 * other count (an incomplete quaternion, a stray word, too many numbers) is a
                 * malformed line, as before.
                 */
                while ((size_t)nnum < (size_t)nw && parse_float_word(words[nw - 1 - nnum], &tmp)) {
                    nnum++;
                }
                if (nnum != 4 && nnum != 7) {
                    bad++;
                    goto next_line;
                }
                first_num = (size_t)nw - (size_t)nnum;
                if (first_num == 0) {
                    bad++;
                    goto next_line;
                }
                if (nnum == 7 && (!parse_float_word(words[first_num + 4], &t[0]) ||
                                  !parse_float_word(words[first_num + 5], &t[1]) ||
                                  !parse_float_word(words[first_num + 6], &t[2]))) {
                    bad++;
                    goto next_line;
                }
                for (k = 0; k < first_num; k++) {
                    size_t wl = strlen(words[k]);

                    if (wl > sizeof(name) - 1 - j - (j > 0 ? 1u : 0u)) {
                        j = sizeof(name);   /* (too long) */
                        break;
                    }
                    if (j > 0) {
                        name[j++] = ' ';
                    }
                    memcpy(name + j, words[k], wl);
                    j += wl;
                    name[j] = 0;
                }
                if (j == 0 || j >= sizeof(name) ||
                    !parse_float_word(words[first_num], &q[0]) ||
                    !parse_float_word(words[first_num + 1], &q[1]) ||
                    !parse_float_word(words[first_num + 2], &q[2]) ||
                    !parse_float_word(words[first_num + 3], &q[3])) {
                    bad++;
                } else {
                    float q2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];

                    if (q2 < 1.0e-12f) {
                        bad++;
                    } else {
                        float inv = 1.0f / sqrtf(q2);
                        VrGripBone *b = &cur->bone[cur->nbones++];

                        b->q[0] = q[0] * inv;
                        b->q[1] = q[1] * inv;
                        b->q[2] = q[2] * inv;
                        b->q[3] = q[3] * inv;
                        b->t[0] = t[0];
                        b->t[1] = t[1];
                        b->t[2] = t[2];
                        snprintf(b->name, sizeof(b->name), "%s", name);
                        lines++;
                    }
                }
            }
        }
next_line:
        p = *e != 0 ? e + 1 : e;
    }
    if (bone_lines != NULL) {
        *bone_lines = lines;
    }
    if (skipped != NULL) {
        *skipped = bad;
    }
    return nposes;
}



/* ---- the file, and each pose's bones resolved on the model ---- */

static VrGripPose sPoses[VRGRIP_POSES];
static int sPoseN;
static int sLoaded;
static int sDisabled;
static const Model *sModel;
static int sModelBones;
static int sModelVerts;

typedef struct ResolvedPose {
    int nbones;
    int bone[VRGRIP_BONES];
    float q[VRGRIP_BONES][4];
    float t[VRGRIP_BONES][3];
} ResolvedPose;
static ResolvedPose sResolved[2][VRGRIP_POSES];
static unsigned char sResolvedDone[2][VRGRIP_POSES];
static unsigned char sResolvedLogged[2][VRGRIP_POSES][VRGRIP_BONES];

static const char *side_name(int side) {
    return side == 0 ? "L" : "R";
}

static int pose_named(const char *name) {
    int i;

    if (name == NULL || name[0] == 0) {
        return -1;
    }
    for (i = 0; i < sPoseN; i++) {
        if (strcmp(sPoses[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void poses_load(void) {
    uint8_t *data;
    size_t size = 0;
    int lines = 0, skipped = 0, offsets = 0, i, j;

    if (sLoaded) {
        return;
    }
    sLoaded = 1;
    data = files_read(GRIP_POSE_PATH, &size);
    if (data == NULL) {
#if GRIP_POSE_LOG
        fprintf(stderr, "[grip-poses] no %s (finger poses are off)\n", GRIP_POSE_PATH);
#endif
        return;
    }
    sPoseN = vrgrip_parse_text((const char *)data, sPoses, VRGRIP_POSES, &lines, &skipped);
    free(data);
    for (i = 0; i < sPoseN; i++) {
        for (j = 0; j < sPoses[i].nbones; j++) {
            const float *t = sPoses[i].bone[j].t;

            if (t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f) {
                offsets++;
            }
        }
    }
#if GRIP_POSE_LOG
    fprintf(stderr, "[grip-poses] loaded %d poses, %d bone lines, %d knuckle offsets, %d skipped from %s\n",
            sPoseN, lines, offsets, skipped, GRIP_POSE_PATH);
#endif
}

/* Does the pose name any of that hand's own bones? (A file with arm left lines uses them for the
 * left hand as they are; one with only arm right lines is mirrored for it.) */
static int pose_has_left(const VrGripPose *p) {
    int i;

    for (i = 0; i < p->nbones; i++) {
        if (strncmp(p->bone[i].name, "arm left ", 9) == 0) {
            return 1;
        }
    }
    return 0;
}

static int pose_has_right(const VrGripPose *p) {
    int i;

    for (i = 0; i < p->nbones; i++) {
        if (strncmp(p->bone[i].name, "arm right ", 10) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Resolve one pose on a model for one hand. The left hand mirrors the right hand's quaternion;
 * if the pose has arm left lines of its own, those are used as they are. */
static const ResolvedPose *resolve_pose(const Model *m, int side, int pidx) {
    const VrGripPose *p;
    ResolvedPose *r;
    int i, has_left, has_right;

    if (pidx < 0 || pidx >= sPoseN || side < 0 || side > 1) {
        return NULL;
    }
    if (sResolvedDone[side][pidx]) {
        return &sResolved[side][pidx];
    }
    p = &sPoses[pidx];
    r = &sResolved[side][pidx];
    memset(r, 0, sizeof(*r));
    has_left = pose_has_left(p);
    has_right = pose_has_right(p);
    for (i = 0; i < p->nbones; i++) {
        const char *name = p->bone[i].name;
        const char *use = name;
        char mapped[32];
        int mirror = 0, b;

        if (strstr(name, "finger") == NULL) {
            continue;   /* (only finger bones may be written: never the game's 0..48) */
        }
        if (side == 0) {
            if (strncmp(name, "arm left ", 9) == 0) {
                use = name;
            } else if (strncmp(name, "arm right ", 10) == 0 && !has_left) {
                snprintf(mapped, sizeof(mapped), "arm left %s", name + 10);
                use = mapped;
                mirror = 1;
            } else {
                continue;   /* (an arm left line is there: the mirror would double it) */
            }
        } else {
            if (strncmp(name, "arm right ", 10) == 0) {
                use = name;
            } else if (strncmp(name, "arm left ", 9) == 0 && !has_right) {
                snprintf(mapped, sizeof(mapped), "arm right %s", name + 9);
                use = mapped;
                mirror = 1;
            } else {
                continue;
            }
        }
        b = model_bone_named(m, use);
        if (b < 0) {
#if GRIP_POSE_LOG
            if (!sResolvedLogged[side][pidx][i]) {
                sResolvedLogged[side][pidx][i] = 1;
                fprintf(stderr, "[grip-poses] pose '%s': no bone '%s' on this model\n", p->name, use);
            }
#endif
            continue;
        }
        if (r->nbones >= VRGRIP_BONES) {
            break;
        }
        r->bone[r->nbones] = b;
        r->q[r->nbones][0] = p->bone[i].q[0];
        r->q[r->nbones][1] = p->bone[i].q[1];
        r->q[r->nbones][2] = p->bone[i].q[2];
        r->q[r->nbones][3] = p->bone[i].q[3];
        r->t[r->nbones][0] = p->bone[i].t[0];
        r->t[r->nbones][1] = p->bone[i].t[1];
        r->t[r->nbones][2] = p->bone[i].t[2];
        if (mirror) {
            vrgrip_mirror_q(r->q[r->nbones]);
            vrgrip_mirror_t(r->t[r->nbones]);
        }
        r->nbones++;
    }
    sResolvedDone[side][pidx] = 1;
    return r;
}

static int resolved_has(const ResolvedPose *r, int bone) {
    int i;

    if (r == NULL) {
        return 0;
    }
    for (i = 0; i < r->nbones; i++) {
        if (r->bone[i] == bone) {
            return 1;
        }
    }
    return 0;
}

static int resolved_q(const ResolvedPose *r, int bone, float q[4]) {
    int i;

    if (r == NULL) {
        return 0;
    }
    for (i = 0; i < r->nbones; i++) {
        if (r->bone[i] == bone) {
            q[0] = r->q[i][0];
            q[1] = r->q[i][1];
            q[2] = r->q[i][2];
            q[3] = r->q[i][3];
            return 1;
        }
    }
    return 0;
}

static Vec3 resolved_t(const ResolvedPose *r, int bone) {
    int i;

    if (r == NULL) {
        return vec3(0.0f, 0.0f, 0.0f);
    }
    for (i = 0; i < r->nbones; i++) {
        if (r->bone[i] == bone) {
            return vec3(r->t[i][0], r->t[i][1], r->t[i][2]);
        }
    }
    return vec3(0.0f, 0.0f, 0.0f);
}

/* ---- the built-in rig check ---- */

static int map_pose_name(const VrGripPose *p, int i, int side, char out[32], int *mirror) {
    const char *name = p->bone[i].name;
    int has_left = pose_has_left(p), has_right = pose_has_right(p);

    *mirror = 0;
    if (strstr(name, "finger") == NULL) {
        return 0;
    }
    if (side == 0) {
        if (strncmp(name, "arm left ", 9) == 0) {
            snprintf(out, 32, "%s", name);
        } else if (strncmp(name, "arm right ", 10) == 0 && !has_left) {
            snprintf(out, 32, "arm left %s", name + 10);
            *mirror = 1;
        } else {
            return 0;
        }
    } else {
        if (strncmp(name, "arm right ", 10) == 0) {
            snprintf(out, 32, "%s", name);
        } else if (strncmp(name, "arm left ", 9) == 0 && !has_right) {
            snprintf(out, 32, "arm right %s", name + 9);
            *mirror = 1;
        } else {
            return 0;
        }
    }
    return 1;
}

/* world[i] = world[parent] * local_rest * T(t) * R(local turn). local_rest is
 * T(rest translation) * R(rest turn), so this is the same as
 * T(rest translation + R(rest turn) * t) * R(local turn): t is in the bone's own rest axes,
 * before the pose turn, exactly Blender's matrix_basis order. The parent's scale is inherited;
 * t = 0 leaves the bone at its rest translation. */
static void set_world_bone(Mat4 *world, const Model *m, int i, Quat local, Vec3 t) {
    int p = m->bones[i].parent;
    Mat4 rest = local_rest(m, i);
    Quat qrest = q_from_mat(&rest);
    Vec3 moved = q_rotate(qrest, t);
    Mat4 at = mat4_at(rest.m[12] + moved.x, rest.m[13] + moved.y, rest.m[14] + moved.z);
    Mat4 turn = mat4_mul(at, mat4_rot(local));

    if (p < 0 || p >= m->nbones) {
        world[i] = turn;
    } else {
        world[i] = mat4_mul(world[p], turn);
    }
}

/* Move every descendant of `i` that is not itself posed with the delta i took (the same trick
 * pose_arm uses for the palm's descendants). */
static void carry_descendants(const Model *m, Mat4 *world, int i, const Mat4 *old, const unsigned char *active) {
    Mat4 od = *old, nd = world[i], delta;
    float sb = sqrtf(od.m[0] * od.m[0] + od.m[1] * od.m[1] + od.m[2] * od.m[2]);
    float sn = sqrtf(nd.m[0] * nd.m[0] + nd.m[1] * nd.m[1] + nd.m[2] * nd.m[2]);
    float f;
    int c, j;

    if (sb < 1.0e-6f) {
        sb = 1.0f;
    }
    if (sn < 1.0e-6f) {
        sn = 1.0f;
    }
    f = sn / sb;
    for (c = 0; c < 3; c++) {
        od.m[c] /= sb;
        od.m[4 + c] /= sb;
        od.m[8 + c] /= sb;
        nd.m[c] /= sn;
        nd.m[4 + c] /= sn;
        nd.m[8 + c] /= sn;
    }
    delta = mat4_mul(nd, rigid_inverse(&od));
    for (c = 0; c < 3; c++) {
        delta.m[c] *= f;
        delta.m[4 + c] *= f;
        delta.m[8 + c] *= f;
        delta.m[12 + c] = (1.0f - f) * world[i].m[12 + c] + f * delta.m[12 + c];
    }
    for (j = 0; j < m->nbones; j++) {
        int a, skip = 0, n = 0;

        if (j == i || active[j] || !descends(m, j, i)) {
            continue;
        }
        /* A posed bone between j and i carries its own subtree. */
        for (a = m->bones[j].parent; a >= 0 && a != i && n < MODEL_MAX_BONES; a = m->bones[a].parent, n++) {
            if (active[a]) {
                skip = 1;
                break;
            }
        }
        if (!skip) {
            world[j] = mat4_mul(delta, world[j]);
        }
    }
}

/* Apply one pose exactly (t = 1) to a bind-pose world[] for the rig check. */
static void apply_pose_exact(const Model *m, Mat4 *world, const VrGripPose *p, int side) {
    unsigned char active[MODEL_MAX_BONES] = {0};
    Quat qpose[MODEL_MAX_BONES];
    Vec3 tpose[MODEL_MAX_BONES];
    int order[MODEL_MAX_BONES];
    int n = 0, i;

    for (i = 0; i < p->nbones; i++) {
        char used[32];
        int mirror, b;

        if (!map_pose_name(p, i, side, used, &mirror)) {
            continue;
        }
        b = model_bone_named(m, used);
        if (b < 0 || active[b]) {
            continue;
        }
        active[b] = 1;
        qpose[b].x = p->bone[i].q[0];
        qpose[b].y = p->bone[i].q[1];
        qpose[b].z = p->bone[i].q[2];
        qpose[b].w = p->bone[i].q[3];
        tpose[b] = vec3(p->bone[i].t[0], p->bone[i].t[1], p->bone[i].t[2]);
        if (mirror) {
            qpose[b].y = -qpose[b].y;
            qpose[b].z = -qpose[b].z;
            tpose[b].x = -tpose[b].x;
        }
        order[n++] = b;
    }
    for (i = 1; i < n; i++) {   /* parents before children */
        int b = order[i], d = bone_depth(m, b), k = i;

        while (k > 0 && bone_depth(m, order[k - 1]) > d) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = b;
    }
    for (i = 0; i < n; i++) {
        int b = order[i];
        Mat4 rest = local_rest(m, b);
        Quat qrest = q_from_mat(&rest);
        Mat4 old = world[b];

        set_world_bone(world, m, b, q_mul(qrest, qpose[b]), tpose[b]);
        carry_descendants(m, world, b, &old, active);
    }
}
static Vec3 point_in_rest(const Model *m, const Mat4 *world, int bone, int wrist) {
    Vec3 p = vec3(world[bone].m[12], world[bone].m[13], world[bone].m[14]);

    return mat4_point(&m->bones[wrist].inv_bind, p);
}

int vrgrip_selfcheck_model_at(const Model *m, int wrist, const VrGripPose *poses, int nposes,
                              VrGripCheck *out) {
    static const char *names[4] = {"arm right finger 2a", "arm right finger 3a",
                                   "arm right finger 5a", "arm right finger 2c"};
    static const float posed[4][3] = {{-0.892f, 0.144f, 0.227f},
                                      {-1.001f, 0.111f, 0.055f},
                                      {-0.860f, 0.180f, -0.289f},
                                      {-1.085f, -0.384f, 0.261f}};
    const VrGripPose *p = NULL;
    Mat4 world[MODEL_MAX_BONES];
    int bones[4];
    int i, k, ok = 1;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (m == NULL || poses == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < nposes; i++) {
        if (strcmp(poses[i].name, "grip_orb") == 0) {
            p = &poses[i];
            break;
        }
    }
    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        bones[i] = model_bone_named(m, names[i]);
        if (bones[i] < 0) {
            return 0;
        }
    }
    if (wrist < 0) {
        return 0;
    }
    out->ran = 1;
    for (i = 0; i < m->nbones; i++) {
        world[i] = rigid_inverse(&m->bones[i].inv_bind);
    }
    apply_pose_exact(m, world, p, 1);
    for (i = 0; i < 4; i++) {
        Vec3 v = point_in_rest(m, world, bones[i], wrist);

        out->got[i][0] = v.x;
        out->got[i][1] = v.y;
        out->got[i][2] = v.z;
    }
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 3; k++) {
            if (fabsf(out->got[i][k] - posed[i][k]) > 0.012f) {
                ok = 0;
            }
        }
    }
    out->pass = ok;
    return 1;
}

/* the original check, on FIN_000's right wrist (bone 46): the check's own model, and a model
 * without a vr_bones.txt that keeps FIN's numbering */
int vrgrip_selfcheck_model(const Model *m, const VrGripPose *poses, int nposes, VrGripCheck *out) {
    return vrgrip_selfcheck_model_at(m, m != NULL ? model_bone_named(m, "bone 46") : -1,
                                     poses, nposes, out);
}


/* ---- the hand state ---- */

#define GRIP_STEADY 0
#define GRIP_IN 1
#define GRIP_OUT 2

typedef struct GripState {
    char want[32];
    int want_item;
    int cur;             /* the pose fully shown (-1: none) */
    int from;            /* the pose being left */
    int to;              /* the pose being entered (-1: none) */
    int mode;            /* GRIP_STEADY / GRIP_IN / GRIP_OUT */
    float t;
    unsigned char base_valid[MODEL_MAX_BONES];
    Quat base[MODEL_MAX_BONES];
} GripState;
static GripState sGrip[2];
static int sStateInit;

static struct {
    char name[32];
    int item;
} sMissing[32];
static int sMissingN;

static void reset_motion(int side) {
    GripState *s = &sGrip[side];

    memset(s->base_valid, 0, sizeof(s->base_valid));
    s->cur = s->from = s->to = -1;
    s->mode = GRIP_STEADY;
    s->t = 0.0f;
}

static void reset_all_motion(void) {
    int side;

    for (side = 0; side < 2; side++) {
        reset_motion(side);
        sGrip[side].want[0] = 0;
        sGrip[side].want_item = -1;
    }
    sStateInit = 1;
}

void vrgrip_idle(void) {
    reset_all_motion();
}

void vrgrip_reset(void) {
    memset(sPoses, 0, sizeof(sPoses));
    memset(sResolved, 0, sizeof(sResolved));
    memset(sResolvedDone, 0, sizeof(sResolvedDone));
    memset(sResolvedLogged, 0, sizeof(sResolvedLogged));
    memset(sMissing, 0, sizeof(sMissing));
    sPoseN = 0;
    sLoaded = 0;
    sDisabled = 0;
    sModel = NULL;
    sModelBones = 0;
    sModelVerts = 0;
    sMissingN = 0;
    reset_all_motion();
}

void vrgrip_want(int side, const char *pose, int item) {
    GripState *s;

    if (side < 0 || side > 1) {
        return;
    }
    if (!sStateInit) {
        reset_all_motion();
    }
    s = &sGrip[side];
    s->want_item = item;
    if (pose == NULL || pose[0] == 0) {
        s->want[0] = 0;
    } else {
        snprintf(s->want, sizeof(s->want), "%s", pose);
    }
}

static void log_missing_pose(const char *name, int item) {
#if GRIP_POSE_LOG
    int i;

    for (i = 0; i < sMissingN; i++) {
        if (strcmp(sMissing[i].name, name) == 0 && sMissing[i].item == item) {
            return;
        }
    }
    if (sMissingN < (int)(sizeof(sMissing) / sizeof(sMissing[0]))) {
        snprintf(sMissing[sMissingN].name, sizeof(sMissing[sMissingN].name), "%s", name);
        sMissing[sMissingN].item = item;
        sMissingN++;
    }
    fprintf(stderr, "[grip-poses] pose '%s' not found (item %d) - using today's curl\n", name, item);
#else
    (void)name;
    (void)item;
#endif
}

static void model_ready(const struct Actor *a) {
    VrGripCheck chk;
    const Model *m = &a->model;
    const VrBones *vb = vrbones_for(a);
    int wrist = vb->ok && vb->bone[VRB_WRIST_R] >= 0 ? vb->bone[VRB_WRIST_R] :
                model_bone_named(m, "bone 46");
    int pidx = pose_named("grip_orb");
    int left = 0, right = 0, any = 0;

    if (pidx >= 0) {
        const VrGripPose *p = &sPoses[pidx];
        int i;

        for (i = 0; i < p->nbones; i++) {
            char used[32];
            int mirror, side;

            for (side = 0; side < 2; side++) {
                if (map_pose_name(p, i, side, used, &mirror) && model_bone_named(m, used) >= 0) {
                    if (side == 0) {
                        left++;
                    } else {
                        right++;
                    }
                    any = 1;
                }
            }
        }
    } else {
        any = model_bone_named(m, "arm right finger 2c") >= 0 || model_bone_named(m, "arm left finger 2c") >= 0;
    }
#if GRIP_POSE_LOG
    fprintf(stderr, "[grip-poses] finger bones: left %d/15, right %d/15\n", left, right);
#endif
    if (!any) {
        sDisabled = 1;
#if GRIP_POSE_LOG
        fprintf(stderr, "[grip-poses] self-check skipped: no finger bones\n");
#endif
        return;
    }
    if (pidx < 0) {
#if GRIP_POSE_LOG
        fprintf(stderr, "[grip-poses] self-check skipped: no grip_orb pose\n");
#endif
        return;
    }
    if (!vrgrip_selfcheck_model_at(m, wrist, sPoses, sPoseN, &chk) || !chk.ran) {
        sDisabled = 1;
#if GRIP_POSE_LOG
        fprintf(stderr, "[grip-poses] self-check skipped: no finger bones\n");
#endif
        return;
    }
#if GRIP_POSE_LOG
    fprintf(stderr, "[grip-poses] self-check %s: R2a (%.3f, %.3f, %.3f), R3a (%.3f, %.3f, %.3f),"
                    " R5a (%.3f, %.3f, %.3f), R2c (%.3f, %.3f, %.3f)\n",
            chk.pass ? "PASS" : "FAIL", (double)chk.got[0][0], (double)chk.got[0][1], (double)chk.got[0][2],
            (double)chk.got[1][0], (double)chk.got[1][1], (double)chk.got[1][2], (double)chk.got[2][0],
            (double)chk.got[2][1], (double)chk.got[2][2], (double)chk.got[3][0], (double)chk.got[3][1],
            (double)chk.got[3][2]);
#endif
}

static float smooth01(float t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

static void capture_base(const Model *m, const Mat4 *world, GripState *s, const ResolvedPose *r) {
    int i;

    if (r == NULL) {
        return;
    }
    for (i = 0; i < r->nbones; i++) {
        int b = r->bone[i];

        if (!s->base_valid[b]) {
            s->base[b] = local_turn(world, m, b);
            s->base_valid[b] = 1;
        }
    }
}

static void gather_active(const Model *m, GripState *s, const ResolvedPose *from, const ResolvedPose *to,
                          const ResolvedPose *cur, unsigned char active[MODEL_MAX_BONES], int order[MODEL_MAX_BONES],
                          int *n) {
    int i;

    *n = 0;
    memset(active, 0, MODEL_MAX_BONES);
    for (i = 0; i < m->nbones; i++) {
        if (s->base_valid[i] || resolved_has(from, i) || resolved_has(to, i) || resolved_has(cur, i)) {
            active[i] = 1;
            order[(*n)++] = i;
        }
    }
    for (i = 1; i < *n; i++) {
        int b = order[i], d = bone_depth(m, b), k = i;

        while (k > 0 && bone_depth(m, order[k - 1]) > d) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = b;
    }
}

static void morph_rest(Actor *a, int side, float weight) {
    if (weight <= 0.0f) {
        return;
    }
    if (weight > 1.0f) {
        weight = 1.0f;
    }
    if (a->face.hand[side].a == 0) {
        a->face.hand[side].w *= 1.0f - weight;
    } else if (a->face.hand[side].a == a->face.hand[side].b) {
        int old = a->face.hand[side].a;

        a->face.hand[side].a = 0;
        a->face.hand[side].b = old;
        a->face.hand[side].w = 1.0f - weight;
    } else if (weight >= 0.5f) {
        a->face.hand[side].a = 0;
        a->face.hand[side].b = 0;
        a->face.hand[side].w = 0.0f;
    }
}

static Quat target_turn(const Model *m, GripState *s, int b, const ResolvedPose *to, const ResolvedPose *cur) {
    Mat4 rest = local_rest(m, b);
    Quat qrest = q_from_mat(&rest);
    float qq[4];

    if (to != NULL && resolved_q(to, b, qq)) {
        Quat qpose;

        qpose.x = qq[0];
        qpose.y = qq[1];
        qpose.z = qq[2];
        qpose.w = qq[3];
        return q_mul(qrest, qpose);
    }
    if (s->mode == GRIP_STEADY && cur != NULL && resolved_q(cur, b, qq)) {
        Quat qpose;

        qpose.x = qq[0];
        qpose.y = qq[1];
        qpose.z = qq[2];
        qpose.w = qq[3];
        return q_mul(qrest, qpose);
    }
    if (s->base_valid[b]) {
        return s->base[b];
    }
    return qrest;
}

/* The bone-local move the pose is heading for: the target pose's t, the shown pose's t while
 * steady, or zero when the hand is leaving the pose / returning to rest. */
static Vec3 target_move(GripState *s, int b, const ResolvedPose *to, const ResolvedPose *cur) {
    if (to != NULL) {
        return resolved_t(to, b);
    }
    if (s->mode == GRIP_STEADY && cur != NULL) {
        return resolved_t(cur, b);
    }
    return vec3(0.0f, 0.0f, 0.0f);
}

static void apply_side(Actor *a, Mat4 *world, int side, float dt) {
    const Model *m = &a->model;
    GripState *s = &sGrip[side];
    const ResolvedPose *from, *to, *cur;
    unsigned char active[MODEL_MAX_BONES];
    int order[MODEL_MAX_BONES], nactive = 0;
    int widx, i, finish = 0;
    float tt, weight;

    if (!sStateInit) {
        reset_all_motion();
    }
    widx = pose_named(s->want);
    if (s->want[0] != 0 && widx < 0) {
        log_missing_pose(s->want, s->want_item);
        widx = -1;
    }
    if (sDisabled) {
        widx = -1;
    }
    if (s->mode == GRIP_STEADY) {
        if (widx != s->cur) {
            if (widx >= 0) {
                s->from = s->cur;
                s->to = widx;
                s->mode = GRIP_IN;
                s->t = 0.0f;
                capture_base(m, world, s, resolve_pose(m, side, widx));
#if GRIP_POSE_LOG
                fprintf(stderr, "[grip-poses] %s hand: item %d pose %s, blend in %.2fs\n", side_name(side),
                        s->want_item, s->want, (double)GRIP_POSE_BLEND_IN);
#endif
            } else if (s->cur >= 0) {
                s->from = s->cur;
                s->to = -1;
                s->mode = GRIP_OUT;
                s->t = 0.0f;
#if GRIP_POSE_LOG
                fprintf(stderr, "[grip-poses] %s hand: released, blend out %.2fs\n", side_name(side),
                        (double)GRIP_POSE_BLEND_OUT);
#endif
            }
        }
    } else {
        int showing = s->to;

        if (!(widx < 0 && s->mode == GRIP_OUT) && !(widx >= 0 && s->mode == GRIP_IN && widx == showing)) {
            if (widx >= 0) {
                s->from = showing >= 0 ? showing : s->from;
                s->to = widx;
                s->mode = GRIP_IN;
                s->t = 0.0f;
                capture_base(m, world, s, resolve_pose(m, side, widx));
#if GRIP_POSE_LOG
                fprintf(stderr, "[grip-poses] %s hand: item %d pose %s, blend in %.2fs\n", side_name(side),
                        s->want_item, s->want, (double)GRIP_POSE_BLEND_IN);
#endif
            } else {
                s->from = showing >= 0 ? showing : s->from;
                s->to = -1;
                s->mode = GRIP_OUT;
                s->t = 0.0f;
#if GRIP_POSE_LOG
                fprintf(stderr, "[grip-poses] %s hand: released, blend out %.2fs\n", side_name(side),
                        (double)GRIP_POSE_BLEND_OUT);
#endif
            }
        }
    }
    if (s->mode != GRIP_STEADY && dt > 0.0f) {
        float dur = s->mode == GRIP_IN ? GRIP_POSE_BLEND_IN : GRIP_POSE_BLEND_OUT;

        s->t += dt / dur;
        if (s->t >= 1.0f) {
            if (s->to >= 0) {
                s->cur = s->to;
                s->from = -1;
                s->to = -1;
                s->mode = GRIP_STEADY;
                s->t = 1.0f;
                finish = 1;   /* (clear the old base after this frame's final write) */
            } else {
                /*
                 * Keep GRIP_OUT at t = 1 for this frame: the final slerp writes the base
                 * exactly, then the state is cleared below. Clearing it here would skip the
                 * last write and leave the fingers at the pose's last partial frame.
                 */
                finish = 2;
            }
        }
    }
    from = resolve_pose(m, side, s->from);
    to = resolve_pose(m, side, s->to);
    cur = resolve_pose(m, side, s->cur);
    gather_active(m, s, from, to, cur, active, order, &nactive);
    tt = s->mode == GRIP_STEADY ? (s->cur >= 0 ? 1.0f : 0.0f) : smooth01(s->t);
    for (i = 0; i < nactive; i++) {
        int b = order[i];
        Quat qcur = local_turn(world, m, b);
        Quat qtgt = target_turn(m, s, b, to, cur);
        Quat qr = q_identity();
        Vec3 ta = resolved_t(from, b);
        Vec3 tb = target_move(s, b, to, cur);
        Vec3 tr = vec3_add(vec3_scale(ta, 1.0f - tt), vec3_scale(tb, tt));
        Mat4 old = world[b];

        vrgrip_slerp((float *)&qcur, (float *)&qtgt, tt, (float *)&qr);
        set_world_bone(world, m, b, qr, tr);
        carry_descendants(m, world, b, &old, active);
    }
    weight = s->mode == GRIP_IN ? smooth01(s->t) : s->mode == GRIP_OUT ? 1.0f - smooth01(s->t) :
                                 s->cur >= 0 ? 1.0f : 0.0f;
    morph_rest(a, side, weight);
    if (finish == 1) {
        cur = resolve_pose(m, side, s->cur);
        for (i = 0; i < m->nbones; i++) {
            if (!resolved_has(cur, i)) {
                s->base_valid[i] = 0;
            }
        }
    } else if (finish == 2) {
        s->cur = -1;
        s->from = -1;
        s->to = -1;
        s->mode = GRIP_STEADY;
        s->t = 0.0f;
        memset(s->base_valid, 0, sizeof(s->base_valid));
    }
}

void vrgrip_apply(Actor *a, Mat4 *world, float dt) {
    int side;

    if (a == NULL || world == NULL || a->model.nbones <= 0) {
        return;
    }
    poses_load();
    if (sModel != &a->model || sModelBones != a->model.nbones || sModelVerts != a->model.nv) {
        sModel = &a->model;
        sModelBones = a->model.nbones;
        sModelVerts = a->model.nv;
        memset(sResolved, 0, sizeof(sResolved));
        memset(sResolvedDone, 0, sizeof(sResolvedDone));
        memset(sResolvedLogged, 0, sizeof(sResolvedLogged));
        sDisabled = 0;
        reset_motion(0);
        reset_motion(1);
        model_ready(a);
    }
    if (sDisabled) {
        return;
    }
    if (!(dt > 0.0f)) {
        dt = 0.0f;
    } else if (dt > 0.25f) {
        dt = 0.25f;
    }
    for (side = 0; side < 2; side++) {
        apply_side(a, world, side, dt);
    }
}
