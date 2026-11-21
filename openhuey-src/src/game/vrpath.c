/* The handle path a hand scrubs a hiding spot's part along: see vrpath.h. */
#include "vrpath.h"

#include <math.h>

float vrpath_project(const Vec3 *path, int n, Vec3 target, float c_now, float window) {
    float best = c_now, best_d = 1.0e30f;
    int i0, i1, i;

    if (path == NULL || n < 2) {
        return c_now < 0.0f ? 0.0f : c_now > 1.0f ? 1.0f : c_now;
    }
    if (c_now < 0.0f) {
        c_now = 0.0f;
    } else if (c_now > 1.0f) {
        c_now = 1.0f;
    }
    best = c_now;
    i0 = (int)floorf((c_now - window) * (float)(n - 1));
    i1 = (int)ceilf((c_now + window) * (float)(n - 1));
    if (i0 < 0) {
        i0 = 0;
    }
    if (i1 > n - 1) {
        i1 = n - 1;
    }
    if (i1 <= i0) {   /* (the window past an end: the last segment) */
        i1 = n - 1;
        i0 = i1 - 1;
    }
    for (i = i0; i < i1; i++) {
        Vec3 a = path[i], b = path[i + 1];
        Vec3 ab = vec3_sub(b, a), ap = vec3_sub(target, a);
        float len2 = vec3_dot(ab, ab), t, d;
        Vec3 q;

        t = len2 > 1.0e-8f ? vec3_dot(ap, ab) / len2 : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        q = vec3_add(a, vec3_scale(ab, t));
        d = vec3_len(vec3_sub(target, q));
        if (d < best_d) {
            best_d = d;
            best = ((float)i + t) / (float)(n - 1);
        }
    }
    return best < 0.0f ? 0.0f : best > 1.0f ? 1.0f : best;
}
