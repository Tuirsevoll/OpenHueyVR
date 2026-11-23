/* The rooms' places in one shared world: see roomlayout.h. */
#include "roomlayout.h"

#include "../core/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOMLAYOUT_MAX 512

enum {
    RL_BASE,
    RL_VARIANT,
    RL_SAME_PLACE,
    RL_CHECK,
    RL_UNKNOWN
};

typedef struct RoomPlace {
    int id;
    int flag;
    int base;        /* the room the flag names (variant-of / same-place-as), -1 none */
    Mat4 W;
} RoomPlace;

static RoomPlace sRoom[ROOMLAYOUT_MAX];
static int sRoomN;
static int sSkipped;
static int sLoaded;
static int sBadLines;

/* a line's room name ("ST_001") as the room's id, -1 if it is not one */
static int room_id_of(const char *name) {
    char *end = NULL;
    unsigned long v;

    if (name == NULL || strncmp(name, "ST_", 3) != 0 || name[3] == 0) {
        return -1;
    }
    v = strtoul(name + 3, &end, 16);
    if (end == name + 3 || (end != NULL && *end != 0) || v > 0xFFFu) {
        return -1;
    }
    return (int)v;
}

/* the file's twelve numbers as Mat4: rows m00 m01 m02 tx, m10 m11 m12 ty, m20 m21 m22 tz in
 * column-major storage (m[col * 4 + row], core/mathx.h) */
static Mat4 affine_of(const float m[12]) {
    Mat4 r = mat4_identity();

    r.m[0] = m[0];   r.m[4] = m[1];   r.m[8] = m[2];    r.m[12] = m[3];
    r.m[1] = m[4];   r.m[5] = m[5];   r.m[9] = m[6];    r.m[13] = m[7];
    r.m[2] = m[8];   r.m[6] = m[9];   r.m[10] = m[10];  r.m[14] = m[11];
    r.m[3] = r.m[7] = r.m[11] = 0.0f;
    r.m[15] = 1.0f;
    return r;
}

static RoomPlace *room_entry(int id) {
    int i;

    for (i = 0; i < sRoomN; i++) {
        if (sRoom[i].id == id) {
            return &sRoom[i];
        }
    }
    return NULL;
}

int roomlayout_room_id(const char *name) {
    return room_id_of(name);
}

int roomlayout_parse_line(const char *line) {
    char room[16], name[32], flag[48];
    float v[12];
    int id, n, flag_kind = RL_BASE, base = -1;
    RoomPlace *e;

    if (line == NULL) {
        return 0;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == 0 || *line == '#' || *line == ';' || (line[0] == '/' && line[1] == '/')) {
        return 0;
    }
    n = sscanf(line, "%15s %31s %47s %f %f %f %f %f %f %f %f %f %f %f %f",
               room, name, flag,
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]);
    if (n != 15 || (id = room_id_of(room)) < 0) {
        sBadLines++;
        return 0;
    }
    if (strcmp(flag, "base") == 0) {
        flag_kind = RL_BASE;
    } else if (strncmp(flag, "variant-of=", 11) == 0) {
        flag_kind = RL_VARIANT;
        base = room_id_of(flag + 11);
    } else if (strncmp(flag, "same-place-as=", 14) == 0) {
        flag_kind = RL_SAME_PLACE;
        base = room_id_of(flag + 14);
    } else if (strcmp(flag, "check") == 0) {
        flag_kind = RL_CHECK;
    } else {
        flag_kind = RL_UNKNOWN;
    }
    if (flag_kind == RL_CHECK || flag_kind == RL_UNKNOWN) {
        if (flag_kind == RL_CHECK) {
            fprintf(stderr, "seamless: layout: %s check skipped\n", room);
        }
        sSkipped++;
    }
    e = room_entry(id);
    if (e == NULL) {
        if (sRoomN >= ROOMLAYOUT_MAX) {
            sBadLines++;
            return 0;
        }
        e = &sRoom[sRoomN++];
        memset(e, 0, sizeof(*e));
        e->id = id;
    }
    e->flag = flag_kind;
    e->base = base;
    e->W = affine_of(v);
    return 1;
}

static void roomlayout_load(void) {
    uint8_t *data;
    size_t size = 0, i, ln = 0;
    char line[512];
    int missing = 0;

    sLoaded = 1;
    data = files_read("PROPS/room_layout.txt", &size);
    if (data != NULL) {
        for (i = 0; i <= size; i++) {
            char c = i < size ? (char)data[i] : '\n';

            if (c == '\n' || c == '\r') {
                line[ln] = 0;
                roomlayout_parse_line(line);
                ln = 0;
                if (c == '\r' && i + 1 < size && data[i + 1] == '\n') {
                    i++;
                }
            } else if (ln + 1 < sizeof(line)) {
                line[ln++] = c;
            }
        }
        free(data);
    } else {
        missing = 1;
    }
    fprintf(stderr, "seamless: layout loaded %d rooms (%d check skipped%s)\n",
            sRoomN, sSkipped, missing ? ", no PROPS/room_layout.txt" : "");
    if (sBadLines > 0) {
        fprintf(stderr, "seamless: layout: %d lines unread\n", sBadLines);
    }
}

void roomlayout_load_if_needed(void) {
    if (!sLoaded) {
        roomlayout_load();
    }
}

void roomlayout_clear(void) {
    memset(sRoom, 0, sizeof(sRoom));
    sRoomN = 0;
    sSkipped = 0;
    sBadLines = 0;
    sLoaded = 0;
}

int roomlayout_entries(void) {
    roomlayout_load_if_needed();
    return sRoomN;
}

int roomlayout_skipped(void) {
    roomlayout_load_if_needed();
    return sSkipped;
}

/* W for a room, its base's for a variant (a variant's own numbers are the base's anyway): 1 if
 * there is one. `depth` stops a file whose bases point at each other. */
static int room_matrix(int id, Mat4 *W, int depth) {
    RoomPlace *e;

    if (depth > 8 || id < 0) {
        return 0;
    }
    e = room_entry(id);
    if (e == NULL || e->flag == RL_CHECK || e->flag == RL_UNKNOWN) {
        return 0;
    }
    if ((e->flag == RL_VARIANT || e->flag == RL_SAME_PLACE) && e->base >= 0 && e->base != id) {
        if (room_matrix(e->base, W, depth + 1)) {
            return 1;
        }
        /* (the named base is not in the file: its own numbers still say where it is) */
    }
    if (W != NULL) {
        *W = e->W;
    }
    return 1;
}

int roomlayout_get(int room_id, Mat4 *W) {
    roomlayout_load_if_needed();
    return room_matrix(room_id, W, 0);
}

int roomlayout_base_id(int room_id) {
    RoomPlace *e;

    roomlayout_load_if_needed();
    e = room_entry(room_id);
    if (e == NULL || e->flag == RL_CHECK || e->flag == RL_UNKNOWN) {
        return -1;
    }
    if ((e->flag == RL_VARIANT || e->flag == RL_SAME_PLACE) && e->base >= 0) {
        return e->base;
    }
    return e->id;
}

int roomlayout_between(int from, int to, Mat4 *T) {
    Mat4 a, b, inv;
    float tx, ty, tz;

    if (!roomlayout_get(from, &a) || !roomlayout_get(to, &b)) {
        return 0;
    }
    /* column-major: the linear part's rows are (m0 m4 m8), (m1 m5 m9), (m2 m6 m10) */
    inv = mat4_identity();
    inv.m[0] = b.m[0];   inv.m[4] = b.m[1];   inv.m[8] = b.m[2];
    inv.m[1] = b.m[4];   inv.m[5] = b.m[5];   inv.m[9] = b.m[6];
    inv.m[2] = b.m[8];   inv.m[6] = b.m[9];   inv.m[10] = b.m[10];
    tx = b.m[12];
    ty = b.m[13];
    tz = b.m[14];
    inv.m[12] = -(b.m[0] * tx + b.m[1] * ty + b.m[2] * tz);
    inv.m[13] = -(b.m[4] * tx + b.m[5] * ty + b.m[6] * tz);
    inv.m[14] = -(b.m[8] * tx + b.m[9] * ty + b.m[10] * tz);
    inv.m[3] = inv.m[7] = inv.m[11] = 0.0f;
    inv.m[15] = 1.0f;
    if (T != NULL) {
        *T = mat4_mul(inv, a);
    }
    return 1;
}
