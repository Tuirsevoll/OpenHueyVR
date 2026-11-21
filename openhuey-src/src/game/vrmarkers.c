/* The VR markers (vrmarkers.h): PROPS/hidespots.txt and PROPS/levers.txt read once through the
 * files layer, and the boxes' test - the point turned into the box's own axes with its quaternion
 * - as triggers.c reads and tests its key items' boxes. Nothing hides, takes or moves anything
 * here: the files are read and what a room has is logged. */
#include "vrmarkers.h"

#include "../core/files.h"
#include "engine.h"
#include "triggers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VrSpot {
    char room[16];     /* the room's PAC base name, as the file has it ("ST_026") */
    char name[32];     /* the spot's name ("wardrobe"), its grab and bound lines' */
    int has_grab, has_bound;
    VrBox grab, bound;
} VrSpot;

static VrSpot sSpot[VRMARKERS_SPOTS];
static int sNSpots;
static VrBox sLever[VRMARKERS_LEVERS];
static int sNLevers;
static int sLoaded;
static int sRoom = -1;   /* the room whose markers were logged last (a change logs again) */

static void vrmarkers_log(const char *msg) {
    if (gEngine.forth != NULL) {
        forth_printf(gEngine.forth, "%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

static void name_set(char *dst, size_t n, const char *src) {
    snprintf(dst, n, "%s", src);
}

/* the turn normalised as box_parse does: a zero turn is none */
static void box_normalise(VrBox *b) {
    float len = sqrtf(b->q[0] * b->q[0] + b->q[1] * b->q[1] + b->q[2] * b->q[2] + b->q[3] * b->q[3]);

    if (len < 1e-6f) {
        b->q[0] = 1.0f;
        b->q[1] = b->q[2] = b->q[3] = 0.0f;
    } else {
        b->q[0] /= len;
        b->q[1] /= len;
        b->q[2] /= len;
        b->q[3] /= len;
    }
}

/* a box's ten numbers: its centre, its half sizes and its turn (the names are read before) */
static int box_read(const char *text, VrBox *b) {
    if (sscanf(text, "%f %f %f %f %f %f %f %f %f %f", &b->c[0], &b->c[1], &b->c[2], &b->h[0],
               &b->h[1], &b->h[2], &b->q[0], &b->q[1], &b->q[2], &b->q[3]) != 10) {
        return 0;
    }
    box_normalise(b);
    return 1;
}

/* past the spaces; NULL when the line is blank, a comment or one to skip */
static const char *line_body(const char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == 0 || *line == '#' || *line == ';' || (line[0] == '/' && line[1] == '/')) {
        return NULL;
    }
    return line;
}

/* one line of PROPS/hidespots.txt: 1 when it was a spot's box (its grab or its bound), 0 when it
 * was blank, a comment or bad. A spot is its room and name: the two lines fill the one entry. */
static int spot_parse(const char *line) {
    char room[16], name[32], kind[16];
    int used = 0, i;
    VrSpot *s = NULL;
    VrBox b;

    if ((line = line_body(line)) == NULL) {
        return 0;
    }
    if (sscanf(line, "%15s %31s %15s%n", room, name, kind, &used) != 3) {
        return 0;
    }
    if (strcmp(kind, "grab") != 0 && strcmp(kind, "bound") != 0) {
        return 0;
    }
    memset(&b, 0, sizeof(b));
    name_set(b.room, sizeof(b.room), room);
    name_set(b.name, sizeof(b.name), name);
    if (!box_read(line + used, &b)) {
        return 0;
    }
    for (i = 0; i < sNSpots; i++) {
        if (strcmp(sSpot[i].room, room) == 0 && strcmp(sSpot[i].name, name) == 0) {
            s = &sSpot[i];
            break;
        }
    }
    if (s == NULL) {
        if (sNSpots >= VRMARKERS_SPOTS) {
            return 0;
        }
        s = &sSpot[sNSpots++];
        memset(s, 0, sizeof(*s));
        name_set(s->room, sizeof(s->room), room);
        name_set(s->name, sizeof(s->name), name);
    }
    if (strcmp(kind, "grab") == 0) {
        s->grab = b;
        s->has_grab = 1;
    } else {
        s->bound = b;
        s->has_bound = 1;
    }
    return 1;
}

/* one line of PROPS/levers.txt: `<room> <name> c... q...` */
static int lever_parse(const char *line) {
    char room[16], name[32];
    int used = 0;
    VrBox b;

    if ((line = line_body(line)) == NULL) {
        return 0;
    }
    if (sscanf(line, "%15s %31s%n", room, name, &used) != 2) {
        return 0;
    }
    if (sNLevers >= VRMARKERS_LEVERS) {
        return 0;
    }
    memset(&b, 0, sizeof(b));
    name_set(b.room, sizeof(b.room), room);
    name_set(b.name, sizeof(b.name), name);
    if (!box_read(line + used, &b)) {
        return 0;
    }
    sLever[sNLevers++] = b;
    return 1;
}

/* the whole file, line by line (as triggers_load reads triggers.txt) */
static void vrmarkers_read(const char *path, int levers) {
    uint8_t *data;
    size_t size = 0, i, ln = 0;
    char line[256];

    data = files_read(path, &size);
    if (data == NULL) {
        return;   /* (no file: the mod does not ship it) */
    }
    for (i = 0; i <= size; i++) {
        char c = i < size ? (char)data[i] : '\n';

        if (c == '\n' || c == '\r') {
            line[ln] = 0;
            if (levers) {
                lever_parse(line);
            } else {
                spot_parse(line);
            }
            ln = 0;
            if (c == '\r' && i + 1 < size && data[i + 1] == '\n') {
                i++;
            }
        } else if (ln + 1 < sizeof(line)) {
            line[ln++] = c;
        }
    }
    free(data);
}

static void vrmarkers_load(void) {
    char msg[96];
    int i;

    sLoaded = 1;
    vrmarkers_read("PROPS/hidespots.txt", 0);
    vrmarkers_read("PROPS/levers.txt", 1);
    snprintf(msg, sizeof(msg), "vrmarkers: %d hiding spots, %d levers\n", sNSpots, sNLevers);
    vrmarkers_log(msg);
    for (i = 0; i < sNSpots; i++) {
        const VrSpot *s = &sSpot[i];

        if (!s->has_grab || !s->has_bound) {   /* (the spot cannot be used whole) */
            snprintf(msg, sizeof(msg), "vrmarkers: warning: %s spot '%s' has no %s box\n", s->room, s->name,
                     !s->has_grab ? "grab" : "bound");
            vrmarkers_log(msg);
        }
    }
}

void vrmarkers_reset(void) {
    sNSpots = 0;
    sNLevers = 0;
    sLoaded = 0;
    sRoom = -1;
}

/* " grab c=(..) h=(..)": the box's numbers as the file has them, nothing into out for none */
static void box_part(char *out, size_t n, const char *what, const VrBox *b) {
    if (b == NULL) {
        out[0] = 0;
        return;
    }
    snprintf(out, n, " %s c=(%.3f %.3f %.3f) h=(%.3f %.3f %.3f)", what, (double)b->c[0], (double)b->c[1],
             (double)b->c[2], (double)b->h[0], (double)b->h[1], (double)b->h[2]);
}

/* the markers of the room she is in, once each time it changes */
static void vrmarkers_log_room(void) {
    char room[16], msg[288], grab[96], bound[96];
    int i;

    if (gEngine.room.id < 0) {
        return;
    }
    triggers_room_name(gEngine.room.id, room, sizeof(room));
    for (i = 0; i < sNSpots; i++) {
        const VrSpot *s = &sSpot[i];

        if (strcmp(s->room, room) != 0) {
            continue;
        }
        box_part(grab, sizeof(grab), "grab", s->has_grab ? &s->grab : NULL);
        box_part(bound, sizeof(bound), "bound", s->has_bound ? &s->bound : NULL);
        if (grab[0] == 0 && bound[0] == 0) {
            continue;
        }
        snprintf(msg, sizeof(msg), "vrmarkers: %s spot '%s'%s%s\n", room, s->name, grab, bound);
        vrmarkers_log(msg);
    }
    for (i = 0; i < sNLevers; i++) {
        if (strcmp(sLever[i].room, room) != 0) {
            continue;
        }
        snprintf(msg, sizeof(msg), "vrmarkers: %s lever '%s' c=(%.3f %.3f %.3f)\n", room, sLever[i].name,
                 (double)sLever[i].c[0], (double)sLever[i].c[1], (double)sLever[i].c[2]);
        vrmarkers_log(msg);
    }
}

void vrmarkers_load_if_needed(void) {
    if (!sLoaded) {
        vrmarkers_load();
    }
    if (gEngine.room.id != sRoom) {   /* (another room: its own markers) */
        sRoom = gEngine.room.id;
        vrmarkers_log_room();
    }
}

int vrmarkers_parse_line(const char *line, int levers) {
    return levers ? lever_parse(line) : spot_parse(line);
}

int vrmarkers_spots_here(int room_id, int *out, int max) {
    char room[16];
    int i, n = 0;

    if (!sLoaded) {
        vrmarkers_load();
    }
    if (out == NULL || max <= 0 || room_id < 0) {
        return 0;
    }
    triggers_room_name(room_id, room, sizeof(room));
    for (i = 0; i < sNSpots && n < max; i++) {
        if (strcmp(sSpot[i].room, room) == 0) {
            out[n++] = i;
        }
    }
    return n;
}

int vrmarkers_levers_here(int room_id, int *out, int max) {
    char room[16];
    int i, n = 0;

    if (!sLoaded) {
        vrmarkers_load();
    }
    if (out == NULL || max <= 0 || room_id < 0) {
        return 0;
    }
    triggers_room_name(room_id, room, sizeof(room));
    for (i = 0; i < sNLevers && n < max; i++) {
        if (strcmp(sLever[i].room, room) == 0) {
            out[n++] = i;
        }
    }
    return n;
}

const VrBox *vrmarkers_spot_grab(int i) {
    return i >= 0 && i < sNSpots && sSpot[i].has_grab ? &sSpot[i].grab : NULL;
}

const VrBox *vrmarkers_spot_bound(int i) {
    return i >= 0 && i < sNSpots && sSpot[i].has_bound ? &sSpot[i].bound : NULL;
}

const char *vrmarkers_spot_name(int i) {
    return i >= 0 && i < sNSpots ? sSpot[i].name : NULL;
}

const VrBox *vrmarkers_lever_box(int i) {
    return i >= 0 && i < sNLevers ? &sLever[i] : NULL;
}

const char *vrmarkers_lever_name(int i) {
    return i >= 0 && i < sNLevers ? sLever[i].name : NULL;
}

/* q v q^-1: the point turned by the quaternion (w, x, y, z) */
static Vec3 quat_turn(float qw, float qx, float qy, float qz, Vec3 v) {
    float tx = 2.0f * (qy * v.z - qz * v.y);
    float ty = 2.0f * (qz * v.x - qx * v.z);
    float tz = 2.0f * (qx * v.y - qy * v.x);

    return vec3(v.x + qw * tx + (qy * tz - qz * ty), v.y + qw * ty + (qz * tx - qx * tz),
                v.z + qw * tz + (qx * ty - qy * tx));
}

Vec3 vrbox_to_local(const VrBox *b, Vec3 p) {
    Vec3 v = vec3_sub(p, vec3(b->c[0], b->c[1], b->c[2]));

    return quat_turn(b->q[0], -b->q[1], -b->q[2], -b->q[3], v);
}

Vec3 vrbox_to_room(const VrBox *b, Vec3 local) {
    Vec3 v = quat_turn(b->q[0], b->q[1], b->q[2], b->q[3], local);

    return vec3_add(v, vec3(b->c[0], b->c[1], b->c[2]));
}

int vrbox_inside(const VrBox *b, Vec3 p, float margin) {
    Vec3 l;

    if (b->h[0] == 0.0f || b->h[1] == 0.0f || b->h[2] == 0.0f) {
        return 0;   /* (as triggers.c: no box, no match) */
    }
    l = vrbox_to_local(b, p);
    return fabsf(l.x) <= b->h[0] + margin && fabsf(l.y) <= b->h[1] + margin &&
           fabsf(l.z) <= b->h[2] + margin;
}

Vec3 vrbox_up_axis(const VrBox *b, int *axis) {
    static const Vec3 sAxis[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    Vec3 up = sAxis[0];
    float best = 0.0f;
    int i, k = 0;

    for (i = 0; i < 3; i++) {
        Vec3 a = quat_turn(b->q[0], b->q[1], b->q[2], b->q[3], sAxis[i]);
        float d = fabsf(a.y);

        if (i == 0 || d > best) {
            best = d;
            up = a;
            k = i;
        }
    }
    if (axis != NULL) {
        *axis = k;
    }
    return up;
}
