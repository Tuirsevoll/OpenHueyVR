/* The key items' trigger boxes (triggers.h): PROPS/triggers.txt read once, and the one the hand
 * item is in. The box test turns the point into the box's own axes with its quaternion and
 * compares it with the half sizes; a box with a half of 0 never matches. */
#include "triggers.h"

#include "../core/files.h"
#include "../data/pac.h"
#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRIGGERS_MAX 256   /* (the file's boxes: far more than the rooms' keys) */

typedef struct TrigBox {
    char room[16];     /* the room's PAC base name, as the file has it ("ST_024") */
    char name[32];     /* its own name (the log) */
    float c[3];        /* its centre, room units */
    float h[3];        /* its half sizes, the box's own axes */
    float q[4];        /* its turn: w x y z, normalised */
} TrigBox;

static TrigBox sBox[TRIGGERS_MAX];
static int sN;
static int sLoaded;
static int sRoom = -1;      /* the room the boxes were tested in (a change re-arms) */
static int sInside = -1;   /* the box entered and not left (-1 none) */

static void trig_log(const char *msg) {
    if (gEngine.forth != NULL) {
        forth_printf(gEngine.forth, "%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

void triggers_room_name(int id, char *out, size_t n) {
    char path[64];
    const char *base;
    size_t len;

    if (out == NULL || n == 0) {
        return;
    }
    out[0] = 0;
    if (id < 0) {
        return;
    }
    pac_room_path(id, path, sizeof(path));
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    len = strlen(base);
    if (len >= 4 && strcmp(base + len - 4, ".PAC") == 0) {
        len -= 4;
    }
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, base, len);
    out[len] = 0;
}

/* one line of the file: 1 when it is a box (12 words), 0 when it is blank, a comment or bad */
static int box_parse(char *line) {
    TrigBox b;
    float len;

    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == 0 || *line == '#' || *line == ';' || (line[0] == '/' && line[1] == '/')) {
        return 0;
    }
    if (sN >= TRIGGERS_MAX) {
        return 0;
    }
    if (sscanf(line, "%15s %31s %f %f %f %f %f %f %f %f %f %f", b.room, b.name,
               &b.c[0], &b.c[1], &b.c[2], &b.h[0], &b.h[1], &b.h[2],
               &b.q[0], &b.q[1], &b.q[2], &b.q[3]) != 12) {
        return 0;
    }
    len = sqrtf(b.q[0] * b.q[0] + b.q[1] * b.q[1] + b.q[2] * b.q[2] + b.q[3] * b.q[3]);
    if (len < 1e-6f) {
        b.q[0] = 1.0f;
        b.q[1] = b.q[2] = b.q[3] = 0.0f;
    } else {
        b.q[0] /= len;
        b.q[1] /= len;
        b.q[2] /= len;
        b.q[3] /= len;
    }
    sBox[sN++] = b;
    return 1;
}

static void triggers_load(void) {
    uint8_t *data;
    size_t size = 0, i;
    char line[256];
    size_t ln = 0;

    sLoaded = 1;
    data = files_read("PROPS/triggers.txt", &size);
    if (data != NULL) {
        for (i = 0; i <= size; i++) {
            char c = i < size ? (char)data[i] : '\n';

            if (c == '\n' || c == '\r') {
                line[ln] = 0;
                box_parse(line);
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
    {
        char msg[64];

        snprintf(msg, sizeof(msg), "triggers: loaded %d boxes\n", sN);
        trig_log(msg);
    }
}

void triggers_reset(void) {
    sN = 0;
    sLoaded = 0;
    sRoom = -1;
    sInside = -1;
}

void triggers_load_if_needed(void) {
    if (!sLoaded) {
        triggers_load();
    }
}

void triggers_leave(void) {
    sInside = -1;
}

/* the point inside the box, in the box's own axes: |local| <= half on each */
static int box_inside(const TrigBox *b, Vec3 p) {
    float vx, vy, vz, qw, qx, qy, qz, tx, ty, tz, lx, ly, lz;

    if (b->h[0] == 0.0f || b->h[1] == 0.0f || b->h[2] == 0.0f) {
        return 0;
    }
    vx = p.x - b->c[0];
    vy = p.y - b->c[1];
    vz = p.z - b->c[2];
    /* q^-1 * v * q: with the conjugate's vector -q.xyz, t = 2 * (-q.xyz x v) */
    qw = b->q[0];
    qx = -b->q[1];
    qy = -b->q[2];
    qz = -b->q[3];
    tx = 2.0f * (qy * vz - qz * vy);
    ty = 2.0f * (qz * vx - qx * vz);
    tz = 2.0f * (qx * vy - qy * vx);
    lx = vx + qw * tx + (qy * tz - qz * ty);
    ly = vy + qw * ty + (qz * tx - qx * tz);
    lz = vz + qw * tz + (qx * ty - qy * tx);
    return fabsf(lx) <= b->h[0] && fabsf(ly) <= b->h[1] && fabsf(lz) <= b->h[2];
}

int triggers_hit(Vec3 p, int item) {
    char room[16];
    int i;

    if (!sLoaded) {
        triggers_load();
    }
    if (gEngine.room.id != sRoom) {
        sRoom = gEngine.room.id;
        sInside = -1;   /* (another room: the boxes are its own) */
    }
    if (item < 0 || gEngine.room.id < 0) {
        sInside = -1;
        return 0;
    }
    triggers_room_name(gEngine.room.id, room, sizeof(room));
    for (i = 0; i < sN; i++) {
        if (strcmp(sBox[i].room, room) == 0 && box_inside(&sBox[i], p)) {
            if (i == sInside) {
                return 0;   /* (still in the box: it fires once) */
            }
            sInside = i;
            {
                char msg[128];

                snprintf(msg, sizeof(msg), "trigger fired: %s %s item %d\n", room, sBox[i].name, item);
                trig_log(msg);
            }
            return 1;
        }
    }
    sInside = -1;   /* (out: the next entry fires) */
    return 0;
}
