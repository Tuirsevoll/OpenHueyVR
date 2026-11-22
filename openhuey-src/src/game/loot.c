/* Collectible meshes you can pick up by hand. See loot.h. */
#include "loot.h"
#include "../platform/vrlog.h"

/* the Forth's dictionary extent (loot.c reads word bodies): the handle is opaque in forth.h */
#include "forth_internal.h"

#include "engine.h"
#include "itemprops.h"
#include "placed.h"
#include "progress.h"
#include "props.h"
#include "room.h"
#include "sprites.h"
#include "things.h"
#include "vrhands.h"
#include "vrbones.h"
#include "../core/files.h"
#include "../data/pac.h"
#include "../data/texbank.h"
#include "../hactor/hactor.h"
#include "../render/render.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/vr.h"
#include "forth.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define LOOT_N 128
#define CAT_N 1024
#define NAME_N 4096
#define NAME_LEN 48
#define KEY_ROOM 0x9
#define DOLL_ROOM 0x20
#define KEY_GROUP 0x18
#define MESH_KEY 0
#define MESH_DOLL 1
#define MESH_CARD 2
#define MESH_ICON 3
#define MESH_THING 4
#define MESH_PART 5
#define MESH_POWDER 6
#define MESH_BALL 7
#define ST_SPAWN 0
#define ST_PART 1
#define ST_PLACED 2
#define MODE_UP 0
#define MODE_THE 1
#define MODE_XT 2

typedef struct Name {
    char s[NAME_LEN];
    int v;
} Name;

typedef struct Cat {
    int room, item, fact, fx, mode, mesh, group;
    float x, y, z;
    int has;
} Cat;

/* The frame a thing is drawn with (right / up / fwd / yaw and its place): the world's own, or - a
 * thing in a hand - the hand's, taken at the draw (docs/subsystems/hands.md, "A held thing is drawn
 * with the hand"). */
typedef struct PropFrame {
    Vec3 pos, right, up, fwd;
    float yaw;
} PropFrame;

typedef struct LootProp {
    int used, style, item, fact, fx, mode, mesh, group, placed;
    void *xt;
    Vec3 home, pos;
    Vec3 right, up, fwd;
    int posed;
    float yaw;
    int holders; /* bit 0 / 1 */
    int pending;
    int spark;
    /* in a hand: `held` 1 when this draw took the frame from that hand (`draw`), 0 when it draws
     * where the world has it. Only ever set for a draw (loot_draw_from_hands). */
    int held;
    PropFrame draw;
} LootProp;

typedef struct Template {
    int ready;
    Room room;
    RoomMesh mesh;
    GpuMesh gpu;
    GpuTexture icon;
    int icon_item;
} Template;

static struct {
    int item;
    GpuTexture tex;
} sIcons[64];
static int sIconN;

/* A held item's model, cached by its file (several items may name one) and never the shared
 * MAX_PROPS pool the mods' props use. `state` 0 a free slot, 1 loaded, -1 its file could not be
 * read. Loaded once when an item is first held (loot_grab), so the draw only reads it. Nothing
 * frees these: a mod's prop-free/item-cleared can never free a held model. */
#define ITEM_PROP_BONE_LEFT 30
#define ITEM_PROP_BONE_RIGHT 46
#define ITEM_MODEL_SLOTS 256   /* (the 192 items' own names and the shared files; about 45 loaded) */
typedef struct ItemModel {
    int state;
    char path[128];
    Prop prop;
} ItemModel;
static ItemModel sItemModel[ITEM_MODEL_SLOTS];
/* one stderr line an item, the first time its model is drawn at rest (the sign check) */
static unsigned char sItemRestLogged[ITEMPROPS_ITEMS];
/* the debug option's three axes, made once (small RGB quads: no line primitive in render_mesh) */
static GpuMesh sItemAxes;
static MeshDraw sItemAxisDraw;
static int sItemAxesReady;

static Name sNames[NAME_N];
static int sNameN;
static int sKind[0xC0];
static char sIcon[0xC0][64];
static int sParsed;
static Cat sCat[CAT_N];
static int sCatN;
static LootProp sProp[LOOT_N];
static Template sKey, sDoll, sCard, sPowder, sBall;
static int sSpark[16], sSparkN;
static int sQn, sArmed, sSettle;
static struct {
    int item, fact, fx, mode;
    void *xt;
    int prop;
} sQ[8];

static int name_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int name_get(const char *s) {
    int i;

    for (i = 0; i < sNameN; i++) {
        if (name_eq(sNames[i].s, s)) {
            return sNames[i].v;
        }
    }
    return 0x80000000;
}

static void name_put(const char *s, int v) {
    int i;

    if (s == NULL || s[0] == 0 || strlen(s) >= NAME_LEN) {
        return;
    }
    for (i = 0; i < sNameN; i++) {
        if (name_eq(sNames[i].s, s)) {
            sNames[i].v = v;
            return;
        }
    }
    if (sNameN >= NAME_N) {
        return;
    }
    snprintf(sNames[sNameN].s, NAME_LEN, "%s", s);
    sNames[sNameN].v = v;
    sNameN++;
}

static int parse_num(const char *t, int *out) {
    char *e = NULL;
    long v;

    if (t[0] == '$') {
        v = strtol(t + 1, &e, 16);
    } else if (t[0] == '-' && t[1] == '$') {
        v = -strtol(t + 2, &e, 16);
    } else {
        v = strtol(t, &e, 10);
    }
    if (e == t || *e != 0) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int is_float_tok(const char *t, float *out) {
    int n = (int)strlen(t);
    char buf[64];
    char *e = NULL;

    if (n < 2 || n >= (int)sizeof(buf) || (t[n - 1] != 'e' && t[n - 1] != 'E')) {
        return 0;
    }
    memcpy(buf, t, (size_t)n - 1);
    buf[n - 1] = 0;
    *out = strtof(buf, &e);
    return e != buf && *e == 0;
}

static int ends_key(const char *s) {
    size_t n = strlen(s);

    return n >= 3 && strcmp(s + n - 3, "key") == 0;
}

static const char *script_root(void) {
    const char *e = getenv("HG2_SCRIPTS");

    if (e != NULL && e[0] != 0) {
        return e;
    }
    return HG2_SCRIPT_DIR;
}

static char *read_file(const char *path, size_t *n) {
    FILE *fp = fopen(path, "rb");
    char *b;
    long sz;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0 || sz > 2000000) {
        fclose(fp);
        return NULL;
    }
    b = malloc((size_t)sz + 1);
    if (b == NULL || fread(b, 1, (size_t)sz, fp) != (size_t)sz) {
        free(b);
        fclose(fp);
        return NULL;
    }
    b[sz] = 0;
    fclose(fp);
    *n = (size_t)sz;
    return b;
}

/* comments and strings become spaces, so the scanner only sees words */
static void blank_forth(char *s) {
    char *p = s;

    while (*p) {
        if (*p == '\\') {
            while (*p && *p != '\n') {
                *p++ = ' ';
            }
        } else if (*p == '(' && (p == s || isspace((unsigned char)p[-1]))) {
            int d = 1;
            *p++ = ' ';
            while (*p && d) {
                if (*p == '(') {
                    d++;
                } else if (*p == ')') {
                    d--;
                }
                if (*p) {
                    *p++ = ' ';
                }
            }
        } else if (*p == 's' && p[1] == '"') {
            *p++ = ' ';
            *p++ = ' ';
            while (*p && *p != '"') {
                *p++ = ' ';
            }
            if (*p == '"') {
                *p++ = ' ';
            }
        } else {
            p++;
        }
    }
}

static void learn_text(const char *text) {
    const char *p = text;

    while (*p) {
        char tok[80];
        int n = 0, v = 0, v2 = 0;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == 0) {
            break;
        }
        while (*p && !isspace((unsigned char)*p) && n < (int)sizeof(tok) - 1) {
            tok[n++] = *p++;
        }
        tok[n] = 0;
        if (strcmp(tok, "constant") == 0) {
            continue;
        }
        if (strcmp(tok, "fact:") == 0) {
            continue;
        }
        /* number, then constant/fact:, then the name: handled by looking ahead below */
        if (!parse_num(tok, &v)) {
            continue;
        }
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        n = 0;
        while (*p && !isspace((unsigned char)*p) && n < (int)sizeof(tok) - 1) {
            tok[n++] = *p++;
        }
        tok[n] = 0;
        if (strcmp(tok, "constant") != 0 && strcmp(tok, "fact:") != 0) {
            continue;
        }
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        n = 0;
        while (*p && !isspace((unsigned char)*p) && n < (int)sizeof(tok) - 1) {
            tok[n++] = *p++;
        }
        tok[n] = 0;
        if (tok[0] && !parse_num(tok, &v2)) {
            name_put(tok, v);
        }
    }
}

static void learn_kinds(const char *text) {
    const char *p = strstr(text, "create item-info");
    int id = 0;

    if (p == NULL) {
        return;
    }
    p = strchr(p, '\n');
    while (p != NULL && id < 0xC0 && strstr(p, "kind-slots") != p + 1) {
        int kind = 0, n = 0;
        char num[32];
        const char *c;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (strncmp(p, "kind-slots", 10) == 0 || strncmp(p, "\\ the kinds'", 13) == 0) {
            break;
        }
        if (*p == '\\' || *p == ':' || *p == 0) {
            break;
        }
        while (*p && !isspace((unsigned char)*p) && *p != ',' && n < (int)sizeof(num) - 1) {
            num[n++] = *p++;
        }
        num[n] = 0;
        if (!parse_num(num, &kind)) {
            break;
        }
        sKind[id] = kind;
        c = strstr(p, "\\ $");
        if (c != NULL && c < strchr(p, '\n')) {
            /* the row's own id, when the table skips none */
        }
        id++;
        p = strchr(p, '\n');
    }
}

static void learn_icons(const char *text) {
    const char *p = text;

    while ((p = strstr(p, "s\" ITEM")) != NULL) {
        char path[64];
        int n = 0, id = -1;
        const char *q = p + 3;
        const char *line = p;

        while (line > text && line[-1] != '\n') {
            line--;
        }
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (line[0] == '$') {
            id = (int)strtol(line + 1, NULL, 16);
        }
        while (*q && *q != '"' && n < 60) {
            path[n++] = *q++;
        }
        path[n] = 0;
        if (id >= 0 && id < 0xC0) {
            snprintf(sIcon[id], sizeof(sIcon[id]), "%s.TEX", path);
        }
        p = q;
    }
}

static void parse_places(void);

static void ensure_catalog(void) {
    char path[600];
    size_t n = 0;
    char *text;

    if (sParsed) {
        return;
    }
    sParsed = 1;
    memset(sKind, 0xFF, sizeof(sKind));
    snprintf(path, sizeof(path), "%s/room-names.fs", script_root());
    text = read_file(path, &n);
    if (text) {
        blank_forth(text);
        learn_text(text);
        free(text);
    }
    snprintf(path, sizeof(path), "%s/items/names.fs", script_root());
    text = read_file(path, &n);
    if (text) {
        blank_forth(text);
        learn_text(text);
        free(text);
    }
    snprintf(path, sizeof(path), "%s/story-names.fs", script_root());
    text = read_file(path, &n);
    if (text) {
        blank_forth(text);
        learn_text(text);
        free(text);
    }
    snprintf(path, sizeof(path), "%s/items/tables.fs", script_root());
    text = read_file(path, &n);
    if (text) {
        learn_kinds(text);
        learn_icons(text);
        free(text);
    }
    parse_places();
    VRLOG( "loot: %d collectibles\n", sCatN);
}

/* the throwable's own model (GAME_FIX.GFM): the magnesias, the doll, the stone */
static int gfm_of(int item, int *model, int *pal) {
    static const struct {
        const char *name;
        int model, pal;
    } map[] = {
        {"magnesia", 1, 0},
        {"refined-magnesia", 1, 1},
        {"prima-magnesia", 1, 2},
        {"bisque-doll", 2, 0},
        {"weeping-stone", 3, 0},
    };
    int i;

    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
        int id = name_get(map[i].name);

        if (id != (int)0x80000000 && item == id) {
            *model = map[i].model;
            *pal = map[i].pal;
            return 1;
        }
    }
    return 0;
}

static int named_id(const char *s) {
    int id = name_get(s);

    return id == (int)0x80000000 ? -1 : id;
}

static int is_powder(int item) {
    return item >= 0 && (item == named_id("antimony-powder") || item == named_id("refined-antimony") ||
                         item == named_id("antimony-tube"));
}

static int is_ball(int item) {
    return item >= 0 && item == named_id("ball");
}

/* The fallback an item is drawn with. A held item's own model is drawn before this by
 * item_prop_draw, so this stays the item's mesh where it lies - exactly as it was. */
static int mesh_for_item(int item) {
    int i, model, pal;

    if (gfm_of(item, &model, &pal)) {
        return MESH_THING;
    }
    if (is_powder(item)) {
        return MESH_POWDER;
    }
    if (is_ball(item)) {
        return MESH_BALL;
    }
    for (i = 0; i < sNameN; i++) {
        if (sNames[i].v == item && ends_key(sNames[i].s) && item < 0x100) {
            return MESH_KEY;
        }
    }
    if (item >= 0 && item < 0xC0 && sIcon[item][0]) {
        return MESH_ICON;
    }
    return MESH_CARD;
}

/* ---- a held item's own model (a mod's PROPS/item-<id>.gltf, PROPS/grips.txt) ----
 * The model is loaded once, when the item is first held, and drawn from the wrist bone the hand
 * itself is posed from. Its offset and turn come only from grips.txt. With no line the model sits
 * at that wrist as it is. The fallback below (mesh_for_item) is kept for every item without one. */

static Actor *loot_fiona(void) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        if (gEngine.actors[i].used && strncmp(gEngine.actors[i].name, "O_FI", 4) == 0) {
            return &gEngine.actors[i];
        }
    }
    return NULL;
}

/* the grip data's key: FIN_000's numbers, the file format's own (left 30, right 46). They mean a
 * hand, not a model bone, so itemprops' lines keep working in every outfit. */
static int item_side_bone(int side) {
    return side == 1 ? ITEM_PROP_BONE_RIGHT : ITEM_PROP_BONE_LEFT;
}

/* the model bone the hand is actually posed from (the outfit's own wrist, vrbones.h); the key
 * above only says which hand */
static int item_wrist_bone(int side) {
    Actor *a = loot_fiona();
    const VrBones *vb;
    int role = side == 1 ? VRB_WRIST_R : VRB_WRIST_L;

    if (a != NULL) {
        vb = vrbones_for(a);
        if (vb->ok && vb->bone[role] >= 0) {
            return vb->bone[role];
        }
    }
    return item_side_bone(side);
}

/* the model's own cache slot, by its file (one file, however many items name it) */
static ItemModel *item_model_find(const char *path) {
    int i;

    for (i = 0; i < ITEM_MODEL_SLOTS; i++) {
        if (sItemModel[i].state != 0 && strcmp(sItemModel[i].path, path) == 0) {
            return &sItemModel[i];
        }
    }
    return NULL;
}

/* the model loaded into its private cache the first time its file is asked for; NULL none */
static ItemModel *item_model_ensure(const char *path) {
    ItemModel *m;
    int i;

    m = item_model_find(path);
    if (m != NULL) {
        return m->state > 0 ? m : NULL;
    }
    for (i = 0; i < ITEM_MODEL_SLOTS && sItemModel[i].state != 0; i++) {
    }
    if (i == ITEM_MODEL_SLOTS) {
        fprintf(stderr, "loot: held model cache full (%d files): %s\n", ITEM_MODEL_SLOTS, path);
        return NULL;
    }
    m = &sItemModel[i];
    snprintf(m->path, sizeof(m->path), "%s", path);
    if (!files_exist(path)) {
        m->state = -1;
        return NULL;
    }
    if (!props_load_own(&m->prop, path)) {
        fprintf(stderr, "loot: held model %s not loaded\n", path);
        m->state = -1;
        return NULL;
    }
    m->state = 1;
    return m;
}

/* the file the item's model comes from in hand `side` (the grip's model=, or its own item-<id>) */
static void item_model_path_for(int item, int side, char *out, size_t n) {
    ItemGrip grip_store;
    const ItemGrip *grip = itemprops_grip(item, item_side_bone(side), &grip_store) ? &grip_store : NULL;

    itemprops_model_path(grip, item, out, n);
}

/* load the model an item lying about will use, off the render pass (the right hand's line, as
 * item_rest_draw draws it); repeated calls are cache hits */
static void item_model_preload(int item) {
    char path[128];

    if (item < 0 || item >= ITEMPROPS_ITEMS) {
        return;
    }
    item_model_path_for(item, 1, path, sizeof(path));
    (void)item_model_ensure(path);
}

int loot_item_prop_slot(int item, int side) {
    char path[128];
    ItemModel *m;

    if (item < 0 || item >= ITEMPROPS_ITEMS) {
        return -1;
    }
    item_model_path_for(item, side, path, sizeof(path));
    m = item_model_find(path);
    return m != NULL && m->state > 0 ? (int)(m - sItemModel) : -1;
}

int loot_item_prop_loaded(int item, int side) {
    return loot_item_prop_slot(item, side) >= 0;
}

static int item_holder_side(const LootProp *p) {
    if ((p->holders & 1) != 0) {
        return 0;
    }
    if ((p->holders & 2) != 0) {
        return 1;
    }
    return -1;
}

/* the bone's frame in the room, as the last posed skeleton has it: its axes are the columns */
static int item_wrist_matrix(int bone, Mat4 *out) {
    Actor *a = loot_fiona();
    Vec3 ax0, ax1, ax2;

    if (a == NULL || bone < 0 || bone >= a->model.nbones) {
        return 0;
    }
    ax0 = a->bone_axes[bone][0];
    ax1 = a->bone_axes[bone][1];
    ax2 = a->bone_axes[bone][2];
    if (vec3_len(ax0) < 1e-5f || vec3_len(ax1) < 1e-5f || vec3_len(ax2) < 1e-5f) {
        return 0;   /* (not posed yet) */
    }
    memset(out, 0, sizeof(*out));
    out->m[0] = ax0.x;  out->m[1] = ax0.y;  out->m[2] = ax0.z;
    out->m[4] = ax1.x;  out->m[5] = ax1.y;  out->m[6] = ax1.z;
    out->m[8] = ax2.x;  out->m[9] = ax2.y;  out->m[10] = ax2.z;
    out->m[12] = a->bones[bone].x;
    out->m[13] = a->bones[bone].y;
    out->m[14] = a->bones[bone].z;
    out->m[15] = 1.0f;
    return 1;
}

/* where a hand item's model sits in the room: the wrist and the item's grip line, the matrix
 * item_prop_draw draws it from (its origin). 0 when her hand is not posed yet. */
int loot_held_origin(int item, int side, Vec3 *out) {
    ItemGrip grip_store;
    const ItemGrip *grip;
    Mat4 wrist, world;
    int key = item_side_bone(side), bone = item_wrist_bone(side);

    if (out == NULL || item < 0) {
        return 0;
    }
    if (!item_wrist_matrix(bone, &wrist)) {
        return 0;
    }
    grip = itemprops_grip(item, key, &grip_store) ? &grip_store : NULL;
    world = itemprops_matrix(grip, &wrist);
    *out = vec3(world.m[12], world.m[13], world.m[14]);
    return 1;
}

/* one axis as two crossed thin quads, so it is seen from any side */
static void item_axis_vertex(MeshVertex *v, Vec3 p, uint8_t r, uint8_t g, uint8_t b) {
    memset(v, 0, sizeof(*v));
    v->x = p.x;
    v->y = p.y;
    v->z = p.z;
    v->rgba[0] = r;
    v->rgba[1] = g;
    v->rgba[2] = b;
    v->rgba[3] = 0x80;
}

static void item_axis_quad(MeshVertex *v, int *n, Vec3 dir, Vec3 side, uint8_t r, uint8_t g, uint8_t b) {
    Vec3 end = vec3_scale(dir, 2.0f);
    Vec3 s0 = vec3_scale(side, -0.035f), s1 = vec3_scale(side, 0.035f);
    Vec3 p0 = s0, p1 = s1, p2 = vec3_add(end, s1), p3 = vec3_add(end, s0);

    item_axis_vertex(&v[(*n)++], p0, r, g, b);
    item_axis_vertex(&v[(*n)++], p1, r, g, b);
    item_axis_vertex(&v[(*n)++], p2, r, g, b);
    item_axis_vertex(&v[(*n)++], p0, r, g, b);
    item_axis_vertex(&v[(*n)++], p2, r, g, b);
    item_axis_vertex(&v[(*n)++], p3, r, g, b);
}

static void item_axes_make(void) {
    MeshVertex v[36];
    int n = 0;

    item_axis_quad(v, &n, vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), 0x80, 0, 0);
    item_axis_quad(v, &n, vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 0x80, 0, 0);
    item_axis_quad(v, &n, vec3(0.0f, 1.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f), 0, 0x80, 0);
    item_axis_quad(v, &n, vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 0, 0x80, 0);
    item_axis_quad(v, &n, vec3(0.0f, 0.0f, 1.0f), vec3(1.0f, 0.0f, 0.0f), 0, 0, 0x80);
    item_axis_quad(v, &n, vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 1.0f, 0.0f), 0, 0, 0x80);
    render_mesh_upload(&sItemAxes, v, n);
    memset(&sItemAxisDraw, 0, sizeof(sItemAxisDraw));
    sItemAxisDraw.first = 0;
    sItemAxisDraw.count = n;
    sItemAxisDraw.texture = -1;
    sItemAxisDraw.part = MESH_SOLID;
    sItemAxisDraw.no_zwrite = 1;
    sItemAxesReady = 1;
}

static void item_axes_draw(const Mat4 *at, const Mat4 *view_proj) {
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    Mat4 mvp;

    if (!gVrOptions.item_prop_debug || at == NULL || view_proj == NULL) {
        return;
    }
    if (!sItemAxesReady) {
        item_axes_make();
    }
    mvp = mat4_mul(*view_proj, *at);
    render_mesh_tint(1.0f, 1.0f, 1.0f, 1.0f);
    render_mesh(&sItemAxes, &mvp, &sItemAxisDraw, 1, NULL, 0, all);
}

/* load (once) the model a hand will hold, and make the debug axes, off the render pass: a model
 * loaded during a draw can leave the renderer's state to the driver's timing, and the axes are
 * uploaded once rather than in the middle of a scene */
void loot_preload_held(int item, int side) {
    char path[128];

    if (gVrOptions.item_prop_debug && !sItemAxesReady) {
        item_axes_make();
    }
    item_model_path_for(item, side, path, sizeof(path));
    (void)item_model_ensure(path);
}

/* the item's mod model in the hand: 1 when it was drawn (the fallback then stays out of it) */
static int item_prop_draw(int item, int side, const Mat4 *view_proj) {
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    ItemModel *m;
    ItemGrip grip_store;
    const ItemGrip *grip;
    Prop *p;
    Mat4 wrist, world, mvp;
    Vec3 c;
    char path[128];
    int key = item_side_bone(side), bone = item_wrist_bone(side);
    float r = 1.0f, g = 1.0f, b = 1.0f;

    if (view_proj == NULL) {
        return 0;
    }
    /* the holding hand's own line; with only the other hand's line, its mirror across her body */
    grip = itemprops_grip(item, key, &grip_store) ? &grip_store : NULL;
    itemprops_model_path(grip, item, path, sizeof(path));
    m = item_model_ensure(path);
    if (m == NULL) {
        return 0;
    }
    p = &m->prop;
    if (!item_wrist_matrix(bone, &wrist)) {
        return 0;
    }
    world = itemprops_matrix(grip, &wrist);
    if (grip != NULL) {
        r = grip->tint[0];
        g = grip->tint[1];
        b = grip->tint[2];
    }
    c = mat4_point(&world, vec3((p->lo.x + p->hi.x) * 0.5f, (p->lo.y + p->hi.y) * 0.5f,
                                (p->lo.z + p->hi.z) * 0.5f));
    engine_lights_at(c);   /* (lit by the room's lights about its middle, as the props in the room) */
    mvp = mat4_mul(*view_proj, world);
    render_mesh_tint(r, g, b, 1.0f);
    render_mesh(&p->gpu, &mvp, p->d, p->nd, p->tex, p->ntex, all);
    render_mesh_tint(1.0f, 1.0f, 1.0f, 1.0f);
    item_axes_draw(&wrist, view_proj);
    item_axes_draw(&world, view_proj);
    return 1;
}

/* the fact's own spelling (c1f-3-camomile, got-study-key), not whichever name shares its number */
static int item_from_name(const char *fn) {
    int i, best = -1, bl = 0;

    if (fn == NULL || fn[0] == 0) {
        return -1;
    }
    for (i = 0; i < sNameN; i++) {
        size_t n = strlen(sNames[i].s);
        size_t f = strlen(fn);
        int id = sNames[i].v;

        if (n < 3 || n > f || id < 0 || id >= 0xC0) {
            continue;
        }
        {
            int model = 0, pal = 0;
            if (sIcon[id][0] == 0 && !ends_key(sNames[i].s) && !is_powder(id) && !is_ball(id) && !gfm_of(id, &model, &pal)) {
                continue;
            }
        }
        if (strcmp(fn + f - n, sNames[i].s) == 0 && (int)n > bl) {
            bl = (int)n;
            best = id;
        }
    }
    return best;
}

static int item_from_fact(int fact) {
    int i;

    if (fact < 0) {
        return -1;
    }
    for (i = 0; i < sNameN; i++) {
        if (sNames[i].v == fact && sNames[i].v >= 0xC0) {
            return item_from_name(sNames[i].s);
        }
    }
    for (i = 0; i < sNameN; i++) {
        if (sNames[i].v == fact) {
            return item_from_name(sNames[i].s);
        }
    }
    return -1;
}

static int token_item(const char *s) {
    int id = name_get(s);

    if (id == (int)0x80000000 || id < 0 || id >= 0xC0) {
        return -1;
    }
    if (sIcon[id][0] || ends_key(s) || is_powder(id) || is_ball(id)) {
        return id;
    }
    {
        int model, pal;
        if (gfm_of(id, &model, &pal)) {
            return id;
        }
    }
    return -1;
}

typedef struct Tok {
    char *s;
} Tok;

static int local_get(Name *loc, int nloc, const char *s) {
    int i, g;

    for (i = 0; i < nloc; i++) {
        if (name_eq(loc[i].s, s)) {
            return loc[i].v;
        }
    }
    g = name_get(s);
    return g;
}

static void add_cat(int room, int item, int fact, int fx, int mode, int mesh, float x, float y, float z, int has) {
    Cat *c;

    if (sCatN >= CAT_N) {
        return;
    }
    c = &sCat[sCatN++];
    c->room = room;
    c->item = item;
    c->fact = fact;
    c->fx = fx;
    c->mode = mode;
    c->mesh = mesh;
    c->group = -1;
    c->x = x;
    c->y = y;
    c->z = z;
    c->has = has;
}

static int cat_has_item(int room, int item, float x, float z) {
    int i;

    for (i = 0; i < sCatN; i++) {
        if (sCat[i].room == room && sCat[i].item == item && fabsf(sCat[i].x - x) < 3.0f && fabsf(sCat[i].z - z) < 3.0f) {
            return 1;
        }
    }
    return 0;
}

/* an action-button pickup that never sparkles: the word gives the item, often hiding a room part */
static void note_button_pickups(Tok *tok, int nt, Name *loc, int nloc, int room) {
    int i;

    for (i = 0; i + 1 < nt; i++) {
        char name[NAME_LEN];
        int end, k, item, fact, part, gives;

        if (strcmp(tok[i].s, ":") != 0) {
            continue;
        }
        if (strlen(tok[i + 1].s) >= NAME_LEN) {
            continue;
        }
        snprintf(name, NAME_LEN, "%s", tok[i + 1].s);
        end = i + 2;
        while (end < nt && strcmp(tok[end].s, ";") != 0) {
            end++;
        }
        item = -1;
        fact = -1;
        part = -1;
        gives = 0;
        for (k = i + 2; k < end; k++) {
            if (strcmp(tok[k].s, "give-item") == 0) {
                int b;
                gives = 1;
                for (b = k - 1; b >= i + 2 && b >= k - 4; b--) {
                    int id = token_item(tok[b].s);
                    if (id >= 0) {
                        item = id;
                        break;
                    }
                }
            }
            if (strcmp(tok[k].s, "fact!") == 0 && k > i + 2) {
                int f = local_get(loc, nloc, tok[k - 1].s);
                if (f != (int)0x80000000) {
                    fact = f;
                }
            }
            if (strcmp(tok[k].s, "hide-part") == 0 && k > i + 2) {
                int p = local_get(loc, nloc, tok[k - 1].s);
                if (p != (int)0x80000000 && p >= 0 && p < 64) {
                    part = p;
                }
            }
        }
        if (!gives || item < 0) {
            continue;
        }
        for (k = 1; k < nt; k++) {
            int b;
            float x, y, z, w, h;
            if (strcmp(tok[k].s, name) != 0 || strcmp(tok[k - 1].s, "[']") != 0) {
                continue;
            }
            for (b = k; b >= 0 && b >= k - 28; b--) {
                if (strcmp(tok[b].s, "touches?") != 0 || b < 5) {
                    continue;
                }
                if (!is_float_tok(tok[b - 1].s, &h) || !is_float_tok(tok[b - 2].s, &w) || !is_float_tok(tok[b - 3].s, &z) ||
                    !is_float_tok(tok[b - 4].s, &y) || !is_float_tok(tok[b - 5].s, &x)) {
                    continue;
                }
                if (cat_has_item(room, item, x, z)) {
                    break;
                }
                add_cat(room, item, fact, -1, MODE_THE, part >= 0 ? MESH_PART : mesh_for_item(item), x, y, z, 1);
                if (part >= 0 && sCatN > 0) {
                    sCat[sCatN - 1].group = 0x15 + part;
                }
                break;
            }
        }
    }
}

static void parse_one(const char *path) {
    size_t n = 0;
    char *text = read_file(path, &n);
    char *scan;
    Tok *tok = NULL;
    int nt = 0, cap = 0, i, room = -1;
    Name loc[192];
    int nloc = 0;
    struct {
        int fx, item, fact, mode, has, keyish;
        float x, y, z;
        char factname[NAME_LEN];
    } flick[64], pick[64];
    int nf = 0, np = 0;

    if (text == NULL) {
        return;
    }
    blank_forth(text);
    scan = text;
    while (*scan) {
        char word[80];
        int k = 0;

        while (*scan && isspace((unsigned char)*scan)) {
            scan++;
        }
        if (*scan == 0) {
            break;
        }
        while (*scan && !isspace((unsigned char)*scan) && k < (int)sizeof(word) - 1) {
            word[k++] = *scan++;
        }
        word[k] = 0;
        if (nt >= cap) {
            cap = cap * 2 + 256;
            tok = realloc(tok, (size_t)cap * sizeof(Tok));
        }
        tok[nt].s = malloc(strlen(word) + 1);
        if (tok[nt].s) {
            memcpy(tok[nt].s, word, strlen(word) + 1);
            nt++;
        }
    }
    free(text);
    for (i = 0; i < nt; i++) {
        int v = 0;
        /* place! is the last word of a place file, so the scan has to include the tail */
        if (i + 2 < nt && parse_num(tok[i].s, &v) && (strcmp(tok[i + 1].s, "constant") == 0 || strcmp(tok[i + 1].s, "fact:") == 0) &&
            nloc < 192) {
            snprintf(loc[nloc].s, NAME_LEN, "%s", tok[i + 2].s);
            loc[nloc].v = v;
            nloc++;
        }
        if (strcmp(tok[i].s, "place!") == 0 && i > 0) {
            int id = local_get(loc, nloc, tok[i - 1].s);
            if (id != 0x80000000) {
                room = id;
            }
        }
    }
    if (room < 0) {
        for (i = 0; i < nt; i++) {
            free(tok[i].s);
        }
        free(tok);
        return;
    }
    for (i = 0; i < nt; i++) {
        float x, y, z;
        if (strcmp(tok[i].s, "flicker-at") == 0 && i >= 4 && is_float_tok(tok[i - 1].s, &z) &&
            is_float_tok(tok[i - 2].s, &y) && is_float_tok(tok[i - 3].s, &x) && nf < 64) {
            int fx = local_get(loc, nloc, tok[i - 4].s);
            flick[nf].fx = fx == 0x80000000 ? -1 : fx;
            flick[nf].x = x;
            flick[nf].y = y;
            flick[nf].z = z;
            flick[nf].item = -1;
            flick[nf].fact = -1;
            flick[nf].mode = -1;
            flick[nf].has = 1;
            flick[nf].keyish = 0;
            flick[nf].factname[0] = 0;
            {
                int back, saw_fact = 0;
                for (back = i - 1; back >= 0 && back >= i - 48; back--) {
                    if (strcmp(tok[back].s, ";") == 0 || strcmp(tok[back].s, ":") == 0) {
                        break; /* the fact guarding this sparkle is in this word, not the previous one */
                    }
                    if (ends_key(tok[back].s)) {
                        flick[nf].keyish = 1;
                    }
                    if (!saw_fact && strcmp(tok[back].s, "fact?") == 0 && back > 0) {
                        int f = local_get(loc, nloc, tok[back - 1].s);
                        saw_fact = 1;
                        snprintf(flick[nf].factname, NAME_LEN, "%s", tok[back - 1].s);
                        if (f != 0x80000000) {
                            flick[nf].fact = f;
                        }
                    }
                }
            }
            nf++;
        }
        if ((strcmp(tok[i].s, "picks-up") == 0 || strcmp(tok[i].s, "takes-up") == 0 || strcmp(tok[i].s, "takes-the") == 0) &&
            np < 64) {
            int mode = strcmp(tok[i].s, "takes-the") == 0 ? MODE_THE : MODE_UP;
            float px, pz;
            if (strcmp(tok[i].s, "picks-up") == 0 && i >= 5 && is_float_tok(tok[i - 1].s, &pz) &&
                is_float_tok(tok[i - 2].s, &px)) {
                int item = local_get(loc, nloc, tok[i - 5].s);
                int fact = local_get(loc, nloc, tok[i - 4].s);
                int fx = local_get(loc, nloc, tok[i - 3].s);
                pick[np].item = item == 0x80000000 ? -1 : item;
                pick[np].fact = fact == 0x80000000 ? -1 : fact;
                pick[np].fx = fx == 0x80000000 ? -1 : fx;
                pick[np].x = px;
                pick[np].z = pz;
                pick[np].has = 1;
                pick[np].mode = mode;
                np++;
            } else if (i >= 3) {
                int item = local_get(loc, nloc, tok[i - 3].s);
                int fact = local_get(loc, nloc, tok[i - 2].s);
                int fx = local_get(loc, nloc, tok[i - 1].s);
                if (item != 0x80000000) {
                    pick[np].item = item;
                    pick[np].fact = fact == 0x80000000 ? -1 : fact;
                    pick[np].fx = fx == 0x80000000 ? -1 : fx;
                    pick[np].has = 0;
                    pick[np].mode = mode;
                    np++;
                }
            }
        }
    }
    for (i = 0; i < nf; i++) {
        int j, found = -1, hits = 0, one = -1;
        if (flick[i].fact >= 0) {
            for (j = 0; j < np; j++) {
                if (pick[j].fact == flick[i].fact) {
                    found = j;
                    break;
                }
            }
        }
        if (found < 0) {
            for (j = 0; j < np; j++) {
                if (!pick[j].has) {
                    continue;
                }
                if (fabsf(pick[j].x - flick[i].x) < 0.35f && fabsf(pick[j].z - flick[i].z) < 0.35f) {
                    found = j;
                    break;
                }
            }
        }
        if (found < 0) {
            for (j = 0; j < np; j++) {
                if (!pick[j].has && pick[j].fx == flick[i].fx && flick[i].fx >= 0) {
                    hits++;
                    one = j;
                }
            }
            if (hits == 1) {
                found = one;
            }
        }
        if (found >= 0) {
            int item = pick[found].item;
            int fact = pick[found].fact >= 0 ? pick[found].fact : flick[i].fact;
            int mode = pick[found].mode;
            if (item < 0) {
                item = item_from_name(flick[i].factname);
            }
            if (item < 0) {
                item = item_from_fact(fact);
            }
            if (item < 0) {
                continue; /* a sparkle with no item: the effect stays, no blank card */
            }
            if (mode < 0) {
                mode = MODE_UP;
            }
            add_cat(room, item, fact, flick[i].fx, mode, mesh_for_item(item), flick[i].x, flick[i].y, flick[i].z, 1);
        } else {
            int item = item_from_name(flick[i].factname);
            int mode;
            if (item < 0) {
                item = item_from_fact(flick[i].fact);
            }
            if (item < 0) {
                continue;
            }
            mode = MODE_UP;
            add_cat(room, item, flick[i].fact, flick[i].fx, mode, mesh_for_item(item), flick[i].x, flick[i].y, flick[i].z, 1);
        }
    }
    note_button_pickups(tok, nt, loc, nloc, room);
    for (i = 0; i < nt; i++) {
        free(tok[i].s);
    }
    free(tok);
}

static void parse_places(void) {
    char pat[600];
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s\\story\\places\\*.fs", script_root());
    {
        char *sl;
        for (sl = pat; *sl; sl++) {
            if (*sl == '/') {
                *sl = '\\';
            }
        }
    }
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char path[700];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        snprintf(path, sizeof(path), "%s\\story\\places\\%s", script_root(), fd.cFileName);
        {
            char *sl;
            for (sl = path; *sl; sl++) {
                if (*sl == '/') {
                    *sl = '\\';
                }
            }
        }
        parse_one(path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)pat;
#endif
}

static int word_in(Forth *f, Cell c) {
    return f != NULL && c > (Cell)f->mem && c < (Cell)f->here;
}

/* 1 when that pointer is a word's header - not a number or a branch's target that merely lies in
 * the dictionary. Room for the whole Word is kept, its code is there, and its name ends inside it
 * (a body scan reads these through pointers it found in compiled code, so a stray cell must never
 * be taken for a Word: it would be read as one). */
static int word_header(Forth *f, Cell c) {
    const Word *w = (const Word *)c;

    if (!word_in(f, c) || c + (Cell)sizeof(Word) > (Cell)f->here) {
        return 0;
    }
    return w->code != NULL && memchr(w->name, 0, sizeof(w->name)) != NULL;
}

static int grants(Forth *f, Word *w, int depth) {
    Cell *p;
    int n = 0;
    static Word *give, *picks, *takes, *the;

    if (w == NULL || depth > 2 || !word_header(f, (Cell)w)) {
        return 0;
    }
    if (give == NULL) {
        /* (a colon word: its `code` is docol, which every room pickup word has too. "give-item" is
         * a message, a constant word, and `give` lives in items' private vocabulary: the old
         * lookups found neither, and grants then said no to every pickup.) */
        give = forth_find(f, "items.private:give", 18);
        picks = forth_find(f, "story.kit:picks-up", 18);
        takes = forth_find(f, "story.kit:takes-up", 18);
        the = forth_find(f, "story.kit:takes-the", 19);
    }
    if (w == give || w == picks || w == takes || w == the) {
        return 1;
    }
    if (depth > 0 && (strstr(w->name, "pick") || strstr(w->name, "take") || strstr(w->name, "key") || strstr(w->name, "doll"))) {
        return 1;
    }
    if (give == NULL || w->code != give->code) {
        return 0;
    }
    p = w->body;
    for (n = 0; n < 96; n++) {
        Word *c;
        if ((uint8_t *)(p + n + 1) > f->here) {
            break;
        }
        if (!word_in(f, p[n])) {
            if (n > 0 && word_header(f, p[n - 1]) && strcmp(((Word *)p[n - 1])->name, "(lit)") == 0) {
                continue;
            }
            if (n > 4) {
                break;
            }
            continue;
        }
        if (!word_header(f, p[n])) {   /* (a branch's target, a number: not a word's header) */
            if (n > 4) {
                break;
            }
            continue;
        }
        c = (Word *)p[n];
        if (strcmp(c->name, "exit") == 0) {
            break;
        }
        if (strcmp(c->name, "(lit)") == 0 || strcmp(c->name, "(branch)") == 0 || strcmp(c->name, "(0branch)") == 0) {
            n++;
            continue;
        }
        if (strcmp(c->name, "(flit)") == 0) {
            n += (int)((sizeof(float) + sizeof(Cell) - 1) / sizeof(Cell));
            continue;
        }
        if (c == give || c == picks || c == takes || c == the) {
            return 1;
        }
        if (depth < 2 && grants(f, c, depth + 1)) {
            return 1;
        }
    }
    return depth > 0 && (strstr(w->name, "pick") || strstr(w->name, "take"));
}

/* where she is, for how near a prop is to her (a VR hand's offer, loot_offer): her model's own
 * place. (It used to look her body up by a *kind* name - `hactor_kind("fiona")`, which is a
 * message kind and finds no actor: every offer was measured from the world's origin, so no room's
 * pickup was ever offered to a prop for a physical grab.) */
static Vec3 fiona_at(void) {
    Actor *a = loot_fiona();

    return a != NULL ? a->pos : vec3(0.0f, 0.0f, 0.0f);
}

static float dist2(Vec3 a, Vec3 b) {
    Vec3 d = vec3_sub(a, b);
    return vec3_dot(d, d);
}

static void group_set(int g, int on) {
    if (g <= 0 || g > 255) {
        return;
    }
    if (on) {
        gEngine.room.groups[g >> 5] |= 1u << (g & 31);
    } else {
        gEngine.room.groups[g >> 5] &= ~(1u << (g & 31));
    }
}

int room_load_prop(Room *r, int id);

static void center_mesh(RoomMesh *m, float want) {
    int i;
    Vec3 c = vec3(0, 0, 0);
    float mx = 0;

    if (m->nv <= 0) {
        return;
    }
    for (i = 0; i < m->nv; i++) {
        c.x += m->v[i].x;
        c.y += m->v[i].y;
        c.z += m->v[i].z;
    }
    c = vec3_scale(c, 1.0f / (float)m->nv);
    for (i = 0; i < m->nv; i++) {
        m->v[i].x -= c.x;
        m->v[i].y -= c.y;
        m->v[i].z -= c.z;
        mx = fabsf(m->v[i].x) > mx ? fabsf(m->v[i].x) : mx;
        mx = fabsf(m->v[i].y) > mx ? fabsf(m->v[i].y) : mx;
        mx = fabsf(m->v[i].z) > mx ? fabsf(m->v[i].z) : mx;
    }
    if (want > 0 && mx > 0.05f) {
        float s = want / (mx * 2.0f);
        if (s < 1.0f || mx * 2.0f < want * 0.45f) {
            for (i = 0; i < m->nv; i++) {
                m->v[i].x *= s;
                m->v[i].y *= s;
                m->v[i].z *= s;
            }
        }
    }
}

static void take_group(Room *src, int group, RoomMesh *dst) {
    int i, nv = 0, nd = 0, at = 0;

    memset(dst, 0, sizeof(*dst));
    for (i = 0; i < src->mesh.nd; i++) {
        if (src->mesh.d[i].group == group) {
            nv += src->mesh.d[i].count;
            nd++;
        }
    }
    if (nv <= 0 || nd <= 0) {
        return;
    }
    dst->v = calloc((size_t)nv, sizeof(MeshVertex));
    dst->d = calloc((size_t)nd, sizeof(MeshDraw));
    dst->nv = nv;
    dst->nd = nd;
    nd = 0;
    for (i = 0; i < src->mesh.nd; i++) {
        MeshDraw *d = &src->mesh.d[i];
        if (d->group != group) {
            continue;
        }
        memcpy(dst->v + at, src->mesh.v + d->first, (size_t)d->count * sizeof(MeshVertex));
        dst->d[nd] = *d;
        dst->d[nd].first = at;
        dst->d[nd].group = 0;
        at += d->count;
        nd++;
    }
    center_mesh(dst, 3.2f);
}

static void take_placed(Placed *p, RoomMesh *dst) {
    int i;

    *dst = p->mesh;
    dst->v = calloc((size_t)(p->mesh.nv > 0 ? p->mesh.nv : 1), sizeof(MeshVertex));
    dst->d = calloc((size_t)(p->mesh.nd > 0 ? p->mesh.nd : 1), sizeof(MeshDraw));
    dst->nv = p->mesh.nv;
    dst->nd = p->mesh.nd;
    if (p->mesh.nv > 0) {
        memcpy(dst->v, p->mesh.v, (size_t)p->mesh.nv * sizeof(MeshVertex));
    }
    if (p->mesh.nd > 0) {
        memcpy(dst->d, p->mesh.d, (size_t)p->mesh.nd * sizeof(MeshDraw));
    }
    for (i = 0; i < dst->nv; i++) {
        dst->v[i].x *= p->scale;
        dst->v[i].y *= p->scale;
        dst->v[i].z *= p->scale;
    }
    center_mesh(dst, 3.2f);
}

static void make_card(RoomMesh *m) {
    MeshVertex *v;
    int i, side;

    memset(m, 0, sizeof(*m));
    m->v = calloc(12, sizeof(MeshVertex));
    m->d = calloc(1, sizeof(MeshDraw));
    m->nv = 12;
    m->nd = 1;
    v = m->v;
    /* a small card, the picture on both sides */
    {
        float xy[6][2] = {{-1.1f, 0}, {1.1f, 0}, {1.1f, 2.6f}, {-1.1f, 0}, {1.1f, 2.6f}, {-1.1f, 2.6f}};
        float uv[6][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 1}, {1, 0}, {0, 0}};
        for (side = 0; side < 2; side++) {
            for (i = 0; i < 6; i++) {
                int k = side == 0 ? i : 5 - i;
                MeshVertex *dst = &v[side * 6 + i];

                dst->x = xy[k][0];
                dst->y = xy[k][1];
                dst->z = 0;
                dst->s = side == 0 ? uv[k][0] : 1.0f - uv[k][0];
                dst->t = uv[k][1];
                dst->nz = side == 0 ? 1.0f : -1.0f;
                dst->rgba[0] = dst->rgba[1] = dst->rgba[2] = 0xFF;
                dst->rgba[3] = 0xFF;
            }
        }
    }
    m->d[0].count = 12;
    m->d[0].texture = 0;
    m->d[0].part = MESH_SOLID;
}

static void make_quad(RoomMesh *m, float s, float u0, float v0, float u1, float v1) {
    MeshVertex *v;
    int i, side;
    float xy[6][2] = {{-s, -s}, {s, -s}, {s, s}, {-s, -s}, {s, s}, {-s, s}};
    float uv[6][2] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v1}, {u1, v0}, {u0, v0}};

    memset(m, 0, sizeof(*m));
    m->v = calloc(12, sizeof(MeshVertex));
    m->d = calloc(1, sizeof(MeshDraw));
    m->nv = 12;
    m->nd = 1;
    v = m->v;
    for (side = 0; side < 2; side++) {
        for (i = 0; i < 6; i++) {
            int k = side == 0 ? i : 5 - i;
            MeshVertex *dst = &v[side * 6 + i];

            dst->x = xy[k][0];
            dst->y = xy[k][1];
            dst->z = 0;
            dst->s = side == 0 ? uv[k][0] : (u0 + u1 - uv[k][0]);
            dst->t = uv[k][1];
            dst->nz = side == 0 ? 1.0f : -1.0f;
            dst->rgba[0] = dst->rgba[1] = dst->rgba[2] = 0xFF;
            dst->rgba[3] = 0xFF;
        }
    }
    m->d[0].count = 12;
    m->d[0].texture = 0;
    m->d[0].part = MESH_SOLID;
}

static void ensure_key(void) {
    if (sKey.ready) {
        return;
    }
    if (!room_load_prop(&sKey.room, KEY_ROOM)) {
        return;
    }
    take_group(&sKey.room, KEY_GROUP, &sKey.mesh);
    if (sKey.mesh.nv > 0) {
        render_mesh_upload(&sKey.gpu, sKey.mesh.v, sKey.mesh.nv);
        sKey.ready = 1;
    }
}

static void ensure_doll(void) {
    Placed *p;

    if (sDoll.ready) {
        return;
    }
    if (!room_load_prop(&sDoll.room, DOLL_ROOM)) {
        return;
    }
    p = placed_named(&sDoll.room.placed, "dool");
    if (p == NULL || p->mesh.nv <= 0) {
        return;
    }
    take_placed(p, &sDoll.mesh);
    if (sDoll.mesh.nv > 0) {
        render_mesh_upload(&sDoll.gpu, sDoll.mesh.v, sDoll.mesh.nv);
        sDoll.ready = 1;
    }
}

static void ensure_card(void) {
    if (sCard.ready) {
        return;
    }
    make_card(&sCard.mesh);
    render_mesh_upload(&sCard.gpu, sCard.mesh.v, sCard.mesh.nv);
    sCard.ready = 1;
}

/* the thrown powder and the ball: the game draws them as sprites, so the hand gets that picture on a small two-sided quad */
static void ensure_sprites(void) {
    int w = 0, h = 0;
    GpuTexture tex;
    uint8_t white[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

    if (sPowder.ready) {
        return;
    }
    tex = (GpuTexture)sprites_texture_of(0x10, 1, -1, &w, &h);
    if (w < 32) {
        w = 512;
    }
    if (h < 32) {
        h = 256;
    }
    make_quad(&sPowder.mesh, 0.7f, 128.0f / (float)w, 64.0f / (float)h, 160.0f / (float)w, 96.0f / (float)h);
    render_mesh_upload(&sPowder.gpu, sPowder.mesh.v, sPowder.mesh.nv);
    sPowder.icon = tex != 0 ? tex : render_texture(white, 2, 2);
    sPowder.ready = 1;
    make_quad(&sBall.mesh, 0.55f, 0.0f, 0.0f, 1.0f, 1.0f);
    render_mesh_upload(&sBall.gpu, sBall.mesh.v, sBall.mesh.nv);
    sBall.icon = render_texture(white, 2, 2);
    sBall.ready = 1;
}

static GpuTexture icon_tex(int item) {
    TexBank *b;
    int i, w = 0, h = 0;
    uint8_t *rgba;

    if (item < 0 || item >= 0xC0 || sIcon[item][0] == 0) {
        return 0;
    }
    for (i = 0; i < sIconN; i++) {
        if (sIcons[i].item == item) {
            return sIcons[i].tex;
        }
    }
    if (sIconN >= 64) {
        return 0;
    }
    b = texbank_load(sIcon[item]);
    if (b == NULL) {
        sIcons[sIconN].item = item;
        sIcons[sIconN].tex = 0;
        sIconN++;
        return 0;
    }
    rgba = texbank_image(b, 0, -1, &w, &h);
    texbank_free(b);
    sIcons[sIconN].item = item;
    sIcons[sIconN].tex = rgba != NULL ? render_texture(rgba, w, h) : 0;
    free(rgba);
    return sIcons[sIconN++].tex;
}

static float xz2(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;

    return dx * dx + dz * dz;
}

static Cat *cat_at(float x, float y, float z) {
    int i, room = gEngine.room.id;
    Cat *best = NULL;
    float bd = 12.0f;

    for (i = 0; i < sCatN; i++) {
        float d;
        if (sCat[i].room != room || !sCat[i].has || sCat[i].group > 0) {
            continue;
        }
        d = fabsf(sCat[i].x - x) + fabsf(sCat[i].y - y) + fabsf(sCat[i].z - z);
        if (d >= 12.0f) {
            continue;
        }
        /* a named item beats a blank sparkle sitting on the same spot */
        if (best != NULL && best->item >= 0 && sCat[i].item < 0) {
            continue;
        }
        if (best == NULL || d < bd || (best->item < 0 && sCat[i].item >= 0)) {
            bd = d;
            best = &sCat[i];
        }
    }
    return best;
}

static Cat *cat_fx(int fx, float x, float z) {
    int i, room = gEngine.room.id;
    Cat *best = NULL;
    float bd = 30.0f * 30.0f;

    if (fx < 0) {
        return NULL;
    }
    for (i = 0; i < sCatN; i++) {
        float dx, dz, d;
        if (sCat[i].room != room || !sCat[i].has || sCat[i].group > 0 || sCat[i].fx != fx || sCat[i].item < 0) {
            continue;
        }
        dx = sCat[i].x - x;
        dz = sCat[i].z - z;
        d = dx * dx + dz * dz;
        if (d < bd) {
            bd = d;
            best = &sCat[i];
        }
    }
    return best;
}

static Cat *cat_item(int item) {
    int i, room = gEngine.room.id;

    for (i = 0; i < sCatN; i++) {
        if (sCat[i].room == room && sCat[i].item == item) {
            return &sCat[i];
        }
    }
    return NULL;
}

static LootProp *prop_near(float x, float y, float z, float r) {
    int i;
    LootProp *best = NULL;
    float bd = r * r;

    for (i = 0; i < LOOT_N; i++) {
        float d;
        if (!sProp[i].used) {
            continue;
        }
        d = dist2(sProp[i].home, vec3(x, y, z));
        if (d < bd) {
            bd = d;
            best = &sProp[i];
        }
        d = dist2(sProp[i].pos, vec3(x, y, z));
        if (d < bd) {
            bd = d;
            best = &sProp[i];
        }
    }
    return best;
}

static LootProp *fresh(void) {
    int i;

    for (i = 0; i < LOOT_N; i++) {
        if (!sProp[i].used) {
            memset(&sProp[i], 0, sizeof(sProp[i]));
            sProp[i].used = 1;
            sProp[i].fact = -1;
            sProp[i].fx = -1;
            sProp[i].item = -1;
            sProp[i].mode = -1;
            sProp[i].placed = -1;
            return &sProp[i];
        }
    }
    return NULL;
}

static void recover_item(LootProp *p) {
    int id;
    Cat *c;

    if (p->item < 0) {
        c = cat_at(p->home.x, p->home.y, p->home.z);
        if (c == NULL) {
            c = cat_fx(p->fx, p->home.x, p->home.z);
        }
        if (c != NULL) {
            if (p->fact < 0) {
                p->fact = c->fact;
            }
            if (p->fx < 0) {
                p->fx = c->fx;
            }
            if (c->mode >= 0 && p->mode < 0) {
                p->mode = c->mode;
            }
            if (c->item >= 0) {
                p->item = c->item;
                p->mesh = c->mesh;
            }
        }
    }
    if (p->item >= 0) {
        if (p->mode < 0) {
            p->mode = MODE_UP;
        }
        return;
    }
    id = item_from_fact(p->fact);
    if (id < 0) {
        return;
    }
    p->item = id;
    p->mesh = mesh_for_item(id);
    if (p->mode < 0) {
        p->mode = MODE_UP;
    }
}

static void fill_from_cat(LootProp *p, Cat *c, float x, float y, float z) {
    p->item = c->item;
    p->fact = c->fact;
    p->fx = c->fx;
    p->mode = c->mode;
    p->mesh = c->mesh;
    p->group = c->group;
    if (c->group > 0) {
        p->style = ST_PART;
    } else {
        p->style = ST_SPAWN;
    }
    p->home = p->pos = vec3(x, y, z);
    recover_item(p);
    item_model_preload(p->item);
    if (p->mesh == MESH_KEY) {
        ensure_key();
    } else if (p->mesh == MESH_DOLL) {
        ensure_doll();
    } else {
        ensure_card();
        if (p->item >= 0) {
            icon_tex(p->item);
        }
    }
}

void loot_reset(void) {
    memset(sProp, 0, sizeof(sProp));
    sSparkN = 0;
    sQn = 0;
    sArmed = 0;
    sSettle = -1;
}

void loot_sparkle(int fx, float x, float y, float z) {
    Cat *c;
    LootProp *p;

    if (!vr_active()) {
        return;
    }
    ensure_catalog();
    p = prop_near(x, y, z, 1.4f);
    if (p != NULL) {
        if (!p->holders && !p->pending && p->style == ST_SPAWN) {
            p->home = p->pos = vec3(x, y, z);
            if (fx >= 0) {
                p->fx = fx;
            }
            if (p->item < 0) {
                recover_item(p);
                if (p->item >= 0) {
                    ensure_card();
                    icon_tex(p->item);
                }
            }
            item_model_preload(p->item);
        }
        return;
    }
    c = cat_at(x, y, z);
    if (c == NULL) {
        c = cat_fx(fx, x, z);
    }
    if (c != NULL && c->item >= 0) {
        int i;
        for (i = 0; i < LOOT_N; i++) {
            if (sProp[i].used && sProp[i].item == c->item && !sProp[i].holders) {
                sProp[i].home = sProp[i].pos = vec3(x, y, z);
                sProp[i].fx = fx >= 0 ? fx : sProp[i].fx;
                item_model_preload(sProp[i].item);
                return;
            }
        }
    }
    if (c == NULL || c->item < 0) {
        return; /* no catalog item: the game's sparkle stays, with no blank card */
    }
    p = fresh();
    if (p == NULL) {
        return;
    }
    fill_from_cat(p, c, x, y, z);
    if (fx >= 0) {
        p->fx = fx;
    }
}

void loot_at(int item, float x, float y, float z) {
    Cat *c;
    LootProp *p;

    if (!vr_active()) {
        return;
    }
    ensure_catalog();
    p = NULL;
    {
        int i;
        for (i = 0; i < LOOT_N; i++) {
            if (sProp[i].used && sProp[i].item == item) {
                p = &sProp[i];
                break;
            }
        }
    }
    if (p != NULL) {
        if (!p->holders) {
            p->home = p->pos = vec3(x, y, z);
        }
        item_model_preload(p->item);
        return;
    }
    c = cat_item(item);
    p = fresh();
    if (p == NULL) {
        return;
    }
    if (c != NULL) {
        fill_from_cat(p, c, x, y, z);
    } else {
        p->item = item;
        p->mesh = mesh_for_item(item);
        p->style = ST_SPAWN;
        p->home = p->pos = vec3(x, y, z);
        item_model_preload(p->item);
        if (p->mesh == MESH_KEY) {
            ensure_key();
        } else {
            ensure_card();
        }
    }
}

void loot_move(int item, float x, float y, float z) {
    int i;

    for (i = 0; i < LOOT_N; i++) {
        if (sProp[i].used && sProp[i].item == item && !sProp[i].holders) {
            sProp[i].home = sProp[i].pos = vec3(x, y, z);
        }
    }
}

void loot_part(int item, int fact, int fx, int part) {
    LootProp *p;
    int g = 0x15 + part;
    int i;

    if (!vr_active()) {
        return;
    }
    ensure_catalog();
    ensure_key();
    for (i = 0; i < LOOT_N; i++) {
        if (sProp[i].used && sProp[i].style == ST_PART && sProp[i].group == g) {
            return;
        }
    }
    p = fresh();
    if (p == NULL) {
        return;
    }
    p->style = ST_PART;
    p->item = item;
    p->fact = fact;
    p->fx = fx;
    p->group = g;
    p->mesh = MESH_KEY;
    p->mode = MODE_THE;
    item_model_preload(p->item);
    /* the part's own centre, so the copy sits in the hand where the key was */
    if (gEngine.room.id == KEY_ROOM && sKey.ready) {
        p->home = p->pos = vec3(0, 0, 0);
        {
            int k, n = 0;
            Vec3 c = vec3(0, 0, 0);
            for (k = 0; k < gEngine.room.mesh.nd; k++) {
                MeshDraw *d = &gEngine.room.mesh.d[k];
                int v;
                if (d->group != g) {
                    continue;
                }
                for (v = 0; v < d->count; v++) {
                    MeshVertex *mv = &gEngine.room.mesh.v[d->first + v];
                    c.x += mv->x;
                    c.y += mv->y;
                    c.z += mv->z;
                    n++;
                }
            }
            if (n > 0) {
                p->home = p->pos = vec3_scale(c, 1.0f / (float)n);
            }
        }
    }
}

void loot_doll(int item, int fact, int fx) {
    Placed *p;
    LootProp *pr;
    int i, idx = -1;

    if (!vr_active()) {
        return;
    }
    p = placed_named(&gEngine.room.placed, "dool");
    if (p == NULL) {
        return;
    }
    idx = (int)(p - gEngine.room.placed.p);
    for (i = 0; i < LOOT_N; i++) {
        if (sProp[i].used && sProp[i].style == ST_PLACED && sProp[i].placed == idx) {
            sProp[i].item = item;
            sProp[i].fact = fact;
            sProp[i].fx = fx;
            return;
        }
    }
    pr = fresh();
    if (pr == NULL) {
        return;
    }
    pr->style = ST_PLACED;
    pr->item = item;
    pr->fact = fact;
    pr->fx = fx;
    pr->placed = idx;
    pr->mesh = MESH_DOLL;
    pr->mode = MODE_UP;
    pr->home = pr->pos = p->pos;
}

void loot_script(void *xt) {
    int i;

    for (i = LOOT_N - 1; i >= 0; i--) {
        if (sProp[i].used && sProp[i].xt == NULL) {
            sProp[i].xt = xt;
            sProp[i].mode = MODE_XT;
            return;
        }
    }
}

void loot_offer(Forth *f, void *xt) {
    Word *w = xt;
    Vec3 at;
    int i, best = -1;
    float bd = 8.0f * 8.0f;   /* (a place's touches area is 5 units across each way: its corners are 7.07) */

    if (!vr_active() || !grants(f, w, 0)) {
        return;
    }
    at = fiona_at();
    for (i = 0; i < LOOT_N; i++) {
        float d;
        if (!sProp[i].used || sProp[i].pending) {
            continue;
        }
        /* shelf and table sparkles sit well above her feet */
        d = xz2(at, sProp[i].home);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best >= 0) {
        sProp[best].xt = xt;
        sProp[best].mode = MODE_XT;
    }
}

static void queue_spark(int fx) {
    if (fx < 0 || sSparkN >= 16) {
        return;
    }
    sSpark[sSparkN++] = fx;
}

int loot_spark_pop(int *fx) {
    if (sSparkN <= 0) {
        return 0;
    }
    *fx = sSpark[--sSparkN];
    return 1;
}

static void queue_prop(LootProp *p) {
    int i = (int)(p - sProp);

    if (sQn >= 8 || p->pending) {
        return;
    }
    p->pending = 1;
    recover_item(p);
    sQ[sQn].item = p->item;
    sQ[sQn].fact = p->fact;
    sQ[sQn].fx = p->fx;
    sQ[sQn].mode = p->xt != NULL ? MODE_XT : p->mode;
    if (p->xt == NULL && p->item < 0) {
        sQ[sQn].mode = 3; /* back on the sparkle: this one has no pickup word yet */
    }
    if (sQ[sQn].mode < 0 && p->item >= 0) {
        sQ[sQn].mode = MODE_UP;
    }
    sQ[sQn].xt = p->xt;
    sQ[sQn].prop = i;
    sQn++;
}

int loot_waiting(void) {
    if (sArmed || sQn <= 0) {
        return 0;
    }
    sArmed = 1;
    return 1;
}

int loot_take(int *item, int *fact, int *fx, int *mode, void **xt) {
    if (sQn <= 0) {
        return 0;
    }
    sQn--;
    *item = sQ[sQn].item;
    *fact = sQ[sQn].fact;
    *fx = sQ[sQn].fx;
    *mode = sQ[sQn].mode;
    *xt = sQ[sQn].xt;
    sSettle = sQ[sQn].prop;
    return 1;
}

void loot_idle(void) { sArmed = 0; }

int loot_fact(void) {
    if (sSettle == -2) {
        return -2;
    }
    if (sSettle < 0 || sSettle >= LOOT_N || !sProp[sSettle].used) {
        return -2;
    }
    return sProp[sSettle].fact;
}

static void hide_world(LootProp *p) {
    if (p->style == ST_PART) {
        group_set(p->group, 0);
    }
    if (p->style == ST_PLACED && p->placed >= 0 && p->placed < gEngine.room.placed.n) {
        gEngine.room.placed.p[p->placed].shown = 0;
    }
}

void loot_consume(void) {
    if (sSettle >= 0 && sSettle < LOOT_N && sProp[sSettle].used) {
        hide_world(&sProp[sSettle]);
        sProp[sSettle].used = 0;
    }
    sSettle = -2;
}

int loot_keep(float *x, float *y, float *z, int *fx) {
    LootProp *p;

    if (sSettle < 0 || sSettle >= LOOT_N || !sProp[sSettle].used) {
        *x = *y = *z = 0;
        *fx = -1;
        sSettle = -1;
        return 0;
    }
    p = &sProp[sSettle];
    p->pending = 0;
    p->holders = 0;
    if (p->style == ST_PLACED && p->placed >= 0 && p->placed < gEngine.room.placed.n) {
        gEngine.room.placed.p[p->placed].shown = 1;
        gEngine.room.placed.p[p->placed].pos = p->pos;
    } else {
        p->pos = p->home;
    }
    if (p->style == ST_PART) {
        group_set(p->group, 1);
    }
    *x = p->home.x;
    *y = p->home.y;
    *z = p->home.z;
    *fx = p->fx;
    p->spark = 0;
    sSettle = -1;
    return p->fx >= 0;
}

static void retire_prop(int i) {
    if (i < 0 || i >= LOOT_N || !sProp[i].used) {
        return;
    }
    hide_world(&sProp[i]);
    if (i == sSettle) {
        sSettle = -2;
    }
    sProp[i].used = 0;
}

void loot_got(int item) {
    Vec3 at = fiona_at();
    int i, best = -1, stray = -1;
    float bd = 80.0f * 80.0f, sd = 40.0f * 40.0f;

    /* the prop this pickup is already resolving, wherever the hand is holding it */
    if (sSettle >= 0 && sSettle < LOOT_N && sProp[sSettle].used) {
        retire_prop(sSettle);
    }
    for (i = 0; i < LOOT_N; i++) {
        float d, h;
        if (!sProp[i].used) {
            continue;
        }
        if (sProp[i].pending && (sProp[i].item < 0 || sProp[i].item == item)) {
            retire_prop(i);
            continue;
        }
        d = xz2(at, sProp[i].pos);
        h = xz2(at, sProp[i].home);
        if (h < d) {
            d = h;
        }
        if (sProp[i].item == item && d < bd) {
            bd = d;
            best = i;
        } else if (sProp[i].item < 0 && d < sd) {
            sd = d;
            stray = i;
        }
    }
    if (best >= 0) {
        retire_prop(best);
    } else if (stray >= 0) {
        retire_prop(stray);
    }
}

int loot_nearest(Vec3 hand, int skip, float *d2) {
    int i, found = -1;

    *d2 = 5.0f * 5.0f;
    for (i = 0; i < LOOT_N; i++) {
        LootProp *p = &sProp[i];
        Vec3 at;
        float d;
        if (!p->used || p->pending || i == skip) {
            continue;
        }
        if (p->style == ST_PLACED) {
            if (p->placed < 0 || p->placed >= gEngine.room.placed.n || !gEngine.room.placed.p[p->placed].shown) {
                continue;
            }
            at = gEngine.room.placed.p[p->placed].pos;
        } else if (p->style == ST_PART) {
            int g = p->group;
            if (g <= 0 || (gEngine.room.groups[g >> 5] & (1u << (g & 31))) == 0) {
                continue;
            }
            at = p->pos;
        } else {
            at = p->pos;
        }
        d = dist2(hand, at);
        if (d < *d2) {
            *d2 = d;
            found = i;
        }
    }
    return found;
}

/* a hand takes the prop at `index` (side 0 the left, 1 the right): it is held there, and the room's
 * pickup it was offered for (loot_offer's `xt`) runs when that hand lets go (loot_let_go, then
 * story/kit.fs loot-run) - her action button's taking, or the item put away. 0 only for a prop
 * that is not there. */
int loot_grab(int index, int side) {
    LootProp *p;

    if (index < 0 || index >= LOOT_N || !sProp[index].used) {
        return 0;
    }
    p = &sProp[index];
    if (p->style == ST_PART) {
        group_set(p->group, 0);
    }
    if (p->style == ST_PLACED && p->placed >= 0 && p->placed < gEngine.room.placed.n) {
        gEngine.room.placed.p[p->placed].shown = 0;
    }
    if (!p->spark && p->fx >= 0) {
        queue_spark(p->fx);
        p->spark = 1;
    }
    loot_preload_held(p->item, side);   /* (loaded now, off the render pass: the hand's draw only reads it) */
    p->holders |= 1 << side;
    return 1;
}

void loot_follow(int index, Vec3 at, Vec3 right, Vec3 up, Vec3 fwd) {
    LootProp *p;

    if (index < 0 || index >= LOOT_N || !sProp[index].used) {
        return;
    }
    p = &sProp[index];
    p->pos = at;
    p->right = right;
    p->up = up;
    p->fwd = fwd;
    p->posed = 1;
    p->yaw = atan2f(fwd.x, fwd.z);
    if (p->style == ST_PLACED && p->placed >= 0 && p->placed < gEngine.room.placed.n) {
        gEngine.room.placed.p[p->placed].pos = at;
        gEngine.room.placed.p[p->placed].shown = 0;
    }
}

void loot_let_go(int index, int side) {
    LootProp *p;

    if (index < 0 || index >= LOOT_N || !sProp[index].used) {
        return;
    }
    p = &sProp[index];
    p->holders &= ~(1 << side);
    if (p->holders) {
        return;
    }
    queue_prop(p);
}

int loot_claims_placed(int placed_index) {
    int i;

    for (i = 0; i < LOOT_N; i++) {
        if (sProp[i].used && sProp[i].style == ST_PLACED && sProp[i].placed == placed_index) {
            return 1;
        }
    }
    return 0;
}

/* ---- a held thing, drawn with the hand (docs/subsystems/hands.md, "A held thing is drawn with the
 * hand") ----
 * The game moves at its 30 ticks a second but a headset draws every frame it can, and her arms are
 * posed afresh for each drawn frame (actor.c's pose hook: vrhands_pose) from the controller's pose
 * and the eyes as *that draw* has them. A held thing was placed by the tick instead, so between
 * ticks it stood still while the eyes walked on, and each new tick jumped it: the stutter - worst
 * with her moving and the hand still. So a thing in a hand is drawn from the very frame its hand is
 * drawn from (vrhands_hand_frame, at the same point in the frame), and sits exactly in the hand on
 * every drawn frame, as smoothly as the arm does. Nothing of the game's own frame is changed: what
 * the hand gave at the tick is what the game keeps (loot_follow). */

int loot_held(int index, int *holders) {
    if (index < 0 || index >= LOOT_N || !sProp[index].used || !sProp[index].holders) {
        return 0;
    }
    if (holders != NULL) {
        *holders = sProp[index].holders;
    }
    return 1;
}

int loot_item_of(int index) {
    if (index < 0 || index >= LOOT_N || !sProp[index].used) {
        return -1;
    }
    return sProp[index].item;
}

int loot_drawn_frame(int index, Vec3 *pos, Vec3 *right, Vec3 *up, Vec3 *fwd) {
    LootProp *p;

    if (index < 0 || index >= LOOT_N || !sProp[index].used) {
        return 0;
    }
    p = &sProp[index];
    if (p->held) {
        *pos = p->draw.pos;
        *right = p->draw.right;
        *up = p->draw.up;
        *fwd = p->draw.fwd;
        return 1;
    }
    *pos = p->pos;
    *right = p->right;
    *up = p->up;
    *fwd = p->fwd;
    return 1;
}

/* Before a draw, from the game's own frames: a thing a hand holds takes that hand's frame as this
 * draw has it (the same frame the arm is posed from), so it sits in the hand on every drawn frame.
 * A hand not tracked (a menu, a scene, no headset) keeps the frame the tick gave, as does a thing in
 * the world. Asked by draw_frame, once a frame, before an eye or an arm is drawn. */
void loot_draw_from_hands(void) {
    int i, side;

    for (i = 0; i < LOOT_N; i++) {
        LootProp *p = &sProp[i];

        p->held = 0;
        if (!p->used) {
            continue;
        }
        for (side = 0; p->holders && side < 2; side++) {
            if ((p->holders & (1 << side)) == 0) {
                continue;
            }
            if (vrhands_hand_frame(side, &p->draw.pos, &p->draw.right, &p->draw.up, &p->draw.fwd)) {
                p->draw.yaw = atan2f(p->draw.fwd.x, p->draw.fwd.z);
                p->held = 1;   /* (drawn from the hand: this draw only) */
                break;
            }
        }
    }
}

static Mat4 prop_world(const LootProp *p) {
    Mat4 m = mat4_identity();
    const Vec3 *right = &p->right, *up = &p->up, *fwd = &p->fwd, *pos = &p->pos;

    if (p->posed && p->holders) {   /* (in a hand: the hand's frame, as the tick or the draw gave it) */
        if (p->held) {   /* (a hand: where it is drawn this frame, not where the tick left it) */
            right = &p->draw.right;
            up = &p->draw.up;
            fwd = &p->draw.fwd;
            pos = &p->draw.pos;
        }
        m.m[0] = right->x;
        m.m[1] = right->y;
        m.m[2] = right->z;
        m.m[4] = up->x;
        m.m[5] = up->y;
        m.m[6] = up->z;
        m.m[8] = fwd->x;
        m.m[9] = fwd->y;
        m.m[10] = fwd->z;
    } else {   /* (at rest: its place and its yaw about room y, upright) */
        float c = cosf(p->yaw), s = sinf(p->yaw);

        m.m[0] = c;
        m.m[2] = -s;
        m.m[8] = s;
        m.m[10] = c;
    }
    m.m[12] = pos->x;
    m.m[13] = pos->y;
    m.m[14] = pos->z;
    return m;
}

static void draw_template(const Template *t, const Mat4 *vp, const Mat4 *world, int item) {
    Mat4 mvp;
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    GpuTexture icon;

    if (!t->ready || t->mesh.nd <= 0) {
        return;
    }
    mvp = mat4_mul(*vp, *world);
    icon = item >= 0 ? icon_tex(item) : 0;
    if (icon && t == &sCard) {
        GpuTexture one = icon;
        render_mesh(&t->gpu, &mvp, t->mesh.d, t->mesh.nd, &one, 1, all);
        return;
    }
    render_mesh(&t->gpu, &mvp, t->mesh.d, t->mesh.nd, t->room.textures, t->room.ntextures, all);
}

static void powder_tint(int item, float *r, float *g, float *b) {
    if (item == named_id("refined-antimony")) {
        *r = 128.0f / 255.0f;
        *g = 100.0f / 255.0f;
        *b = 0.0f;
    } else if (item == named_id("antimony-tube")) {
        *r = *g = *b = 128.0f / 255.0f;
    } else {
        *r = 64.0f / 255.0f;
        *g = 32.0f / 255.0f;
        *b = 128.0f / 255.0f;
    }
}

static void draw_flat(const Template *t, const Mat4 *vp, const Mat4 *world, float r, float g, float b) {
    Mat4 mvp;
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    GpuTexture one;

    if (!t->ready || t->mesh.nd <= 0) {
        return;
    }
    one = t->icon;
    mvp = mat4_mul(*vp, *world);
    render_mesh_tint(r, g, b, 1.0f);
    render_mesh(&t->gpu, &mvp, t->mesh.d, t->mesh.nd, &one, 1, all);
    render_mesh_tint(1.0f, 1.0f, 1.0f, 1.0f);
}

static int group_center(int group, Vec3 *out) {
    int k, n = 0;
    Vec3 c = vec3(0, 0, 0);

    if (group <= 0) {
        return 0;
    }
    for (k = 0; k < gEngine.room.mesh.nd; k++) {
        MeshDraw *d = &gEngine.room.mesh.d[k];
        int v;
        if (d->group != group) {
            continue;
        }
        for (v = 0; v < d->count; v++) {
            MeshVertex *mv = &gEngine.room.mesh.v[d->first + v];
            c.x += mv->x;
            c.y += mv->y;
            c.z += mv->z;
            n++;
        }
    }
    if (n <= 0) {
        return 0;
    }
    *out = vec3_scale(c, 1.0f / (float)n);
    return 1;
}

static int draw_room_group(int group, const Mat4 *hand, const Mat4 *vp) {
    static MeshDraw draws[96];
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    int k, nd = 0, n = 0;
    Vec3 c = vec3(0, 0, 0);
    float ext = 0, scale = 1.0f;
    Mat4 tr, sc, world, mvp;
    Room *r = &gEngine.room;

    if (group <= 0 || r->gpu.vao == 0) {
        return 0;
    }
    for (k = 0; k < r->mesh.nd && nd < 96; k++) {
        MeshDraw *d = &r->mesh.d[k];
        int v;
        if (d->group != group) {
            continue;
        }
        draws[nd] = *d;
        draws[nd].group = 0;
        nd++;
        for (v = 0; v < d->count; v++) {
            MeshVertex *mv = &r->mesh.v[d->first + v];
            c.x += mv->x;
            c.y += mv->y;
            c.z += mv->z;
            n++;
        }
    }
    if (nd <= 0 || n <= 0) {
        return 0;
    }
    c = vec3_scale(c, 1.0f / (float)n);
    for (k = 0; k < nd; k++) {
        int v;
        for (v = 0; v < draws[k].count; v++) {
            MeshVertex *mv = &r->mesh.v[draws[k].first + v];
            float dx = fabsf(mv->x - c.x), dy = fabsf(mv->y - c.y), dz = fabsf(mv->z - c.z);
            ext = dx > ext ? dx : ext;
            ext = dy > ext ? dy : ext;
            ext = dz > ext ? dz : ext;
        }
    }
    if (ext > 4.0f) {
        scale = 3.2f / ext;
    } else if (ext > 0.05f && ext < 0.8f) {
        scale = 1.6f / ext;
    }
    tr = mat4_identity();
    tr.m[12] = -c.x;
    tr.m[13] = -c.y;
    tr.m[14] = -c.z;
    sc = mat4_identity();
    sc.m[0] = sc.m[5] = sc.m[10] = scale;
    world = mat4_mul(*hand, mat4_mul(sc, tr));
    mvp = mat4_mul(*vp, world);
    render_mesh(&r->gpu, &mvp, draws, nd, r->textures, r->ntextures, all);
    return 1;
}

/* An item lying about (not in a hand) drawn with its own model, from the right hand's grip line
 * (model= and tint), at the prop's world matrix. `up_plus` puts the model's floor side (its lowest
 * y) on the item's spot and the rest above it - what a thing lying on the room's floor wants, the
 * room's +Y being up. Without it (the loot props' own line) the spot is the model's largest y, as
 * that path has always had it. The model is only looked up here: the callers preload it. */
static int item_rest_draw(const LootProp *p, const Mat4 *vp, int up_plus) {
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    ItemModel *m;
    ItemGrip grip_store;
    const ItemGrip *grip;
    Prop *mp;
    Mat4 world, mvp;
    Vec3 lo, hi, middle;
    char path[128];
    float min_y = 1e30f, max_y = -1e30f, spot_y, delta;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    int i;

    if (p == NULL || vp == NULL || p->item < 0 || p->item >= ITEMPROPS_ITEMS) {
        return 0;
    }
    /* the same line the held draw uses for the right hand; the model itself is only found here */
    grip = itemprops_grip(p->item, ITEM_PROP_BONE_RIGHT, &grip_store) ? &grip_store : NULL;
    item_model_path_for(p->item, 1, path, sizeof(path));
    m = item_model_find(path);
    if (m == NULL || m->state <= 0) {
        return 0;
    }
    mp = &m->prop;
    world = prop_world(p);
    lo = mp->lo;
    hi = mp->hi;
    for (i = 0; i < 8; i++) {
        Vec3 c = vec3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
        Vec3 w = mat4_point(&world, c);

        min_y = w.y < min_y ? w.y : min_y;
        max_y = w.y > max_y ? w.y : max_y;
    }
    spot_y = world.m[13];
    delta = up_plus ? spot_y - min_y : spot_y - max_y;   /* (floor side on the spot; up is +y) */
    world.m[13] += delta;
    min_y += delta;
    max_y += delta;
    middle = mat4_point(&world, vec3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f));
    engine_lights_at(middle);
    if (p->item < ITEMPROPS_ITEMS && !sItemRestLogged[p->item]) {
        sItemRestLogged[p->item] = 1;
        if (up_plus) {
            VRLOG( "loot: item %d at rest: spot y %.3f, model y %.3f .. %.3f (floor side on the spot, up +y)\n",
                    p->item, (double)spot_y, (double)min_y, (double)max_y);
        } else {
            VRLOG(
                    "loot: item %d at rest: spot y %.3f, model y %.3f .. %.3f (room up is -y; model should be "
                    "ABOVE the spot: its lowest y (ceiling side) is smaller than the spot, its floor side at the spot)\n",
                    p->item, (double)spot_y, (double)min_y, (double)max_y);
        }
    }
    if (grip != NULL) {
        r = grip->tint[0];
        g = grip->tint[1];
        b = grip->tint[2];
    }
    mvp = mat4_mul(*vp, world);
    render_mesh_tint(r, g, b, 1.0f);
    render_mesh(&mp->gpu, &mvp, mp->d, mp->nd, mp->tex, mp->ntex, all);
    render_mesh_tint(1.0f, 1.0f, 1.0f, 1.0f);
    return 1;
}

/* The item's own model lying at a point (a treat on the floor, things.c's kind 11): the rest draw's
 * upright placement with the model's floor side on the point and its body above it. The caller
 * preloads the model (loot_preload_held does the same path), so nothing is loaded in a draw. */
int loot_item_rest_draw(int item, Vec3 at, const Mat4 *view_proj) {
    LootProp p;

    memset(&p, 0, sizeof(p));
    p.used = 1;
    p.item = item;
    p.fx = -1;
    p.home = p.pos = at;
    return item_rest_draw(&p, view_proj, 1);
}

static void draw_spawned(LootProp *p, const Mat4 *vp) {
    int model, pal;
    Mat4 world = prop_world(p);

    if (p->holders) {   /* (a hand has it: its own model, at that hand's wrist) */
        int side = item_holder_side(p);

        if (side >= 0 && item_prop_draw(p->item, side, vp)) {
            return;
        }
    }
    if (!p->holders && item_rest_draw(p, vp, 0)) {   /* (lying about: its own model on the spot) */
        return;
    }
    if (gfm_of(p->item, &model, &pal) && things_model_draw(model, pal, &world, vp)) {
        return;
    }
    if (p->mesh == MESH_PART && draw_room_group(p->group, &world, vp)) {
        return;
    }
    if (p->mesh == MESH_POWDER || is_powder(p->item)) {
        float r, g, b;
        ensure_sprites();
        powder_tint(p->item, &r, &g, &b);
        draw_flat(&sPowder, vp, &world, r, g, b);
        return;
    }
    if (p->mesh == MESH_BALL || is_ball(p->item)) {
        ensure_sprites();
        draw_flat(&sBall, vp, &world, 1.0f, 1.0f, 0.45f);
        return;
    }
    if (p->mesh == MESH_KEY) {
        ensure_key();
        draw_template(&sKey, vp, &world, -1);
        return;
    }
    if (p->mesh == MESH_DOLL) {
        ensure_doll();
        if (sDoll.ready) {
            draw_template(&sDoll, vp, &world, -1);
            return;
        }
    }
    ensure_card();
    draw_template(&sCard, vp, &world, p->item);
}

static void draw_placed_held(LootProp *p, const Mat4 *vp) {
    Placed *pl;
    Mat4 sc = mat4_identity(), world, mvp;
    static const uint32_t all[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    int model, pal;
    Vec3 r = p->held ? p->draw.right : p->right;
    Vec3 u = p->held ? p->draw.up : p->up;
    Vec3 f = p->held ? p->draw.fwd : p->fwd;

    if (p->placed < 0 || p->placed >= gEngine.room.placed.n) {
        return;
    }
    if (vec3_len(r) < 0.5f || vec3_len(u) < 0.5f || vec3_len(f) < 0.5f) {
        return;   /* (a hand's frame not given yet: an item's mesh would land flat on the screen) */
    }
    if (p->holders) {
        int side = item_holder_side(p);

        if (side >= 0 && item_prop_draw(p->item, side, vp)) {
            return;
        }
    }
    pl = &gEngine.room.placed.p[p->placed];
    if (pl->mesh.nd > 0) {
        sc.m[0] = sc.m[5] = sc.m[10] = pl->scale;
        world = mat4_mul(prop_world(p), sc);
        mvp = mat4_mul(*vp, world);
        render_mesh(&pl->gpu, &mvp, pl->mesh.d, pl->mesh.nd, gEngine.room.textures, gEngine.room.ntextures, all);
        return;
    }
    world = prop_world(p);
    if (gfm_of(p->item, &model, &pal)) {
        things_model_draw(model, pal, &world, vp);
    }
}

void loot_draw(const Mat4 *view_proj, int layer) {
    int i;

    if (layer != 1 || !vr_active()) {
        return;
    }
    for (i = 0; i < LOOT_N; i++) {
        LootProp *p = &sProp[i];
        int show;
        if (!p->used) {
            continue;
        }
        if (p->style == ST_PLACED) {
            if (p->holders || p->pending) {
                draw_placed_held(p, view_proj);
            }
            continue;
        }
        show = p->style == ST_SPAWN || p->holders || p->pending;
        if (!show) {
            continue;
        }
        draw_spawned(p, view_proj);
    }
}

static int fact_on(int fact) {
    if (fact < 0 || fact >= STORY_FLAGS) {
        return 0;
    }
    return (gProgress.story[fact >> 5] >> (fact & 31)) & 1;
}

static int group_on(int g) {
    if (g <= 0 || g > 255) {
        return 0;
    }
    return (gEngine.room.groups[g >> 5] & (1u << (g & 31))) != 0;
}

void loot_maintain(void) {
    int i, room;

    if (!vr_active()) {
        return;
    }
    ensure_catalog();
    room = gEngine.room.id;
    for (i = 0; i < sCatN; i++) {
        Cat *c = &sCat[i];
        int j, have = 0;
        LootProp *p;
        Vec3 at, cen;
        if (c->room != room || c->group <= 0 || fact_on(c->fact) || !group_on(c->group)) {
            continue;
        }
        for (j = 0; j < LOOT_N; j++) {
            if (!sProp[j].used) {
                continue;
            }
            if (sProp[j].group == c->group || (c->item >= 0 && sProp[j].item == c->item)) {
                have = 1;
                break;
            }
        }
        if (have) {
            continue;
        }
        p = fresh();
        if (p == NULL) {
            return;
        }
        p->style = ST_PART;
        p->item = c->item;
        p->fact = c->fact;
        p->fx = c->fx;
        p->mode = c->mode < 0 ? MODE_THE : c->mode;
        p->mesh = MESH_PART;
        p->group = c->group;
        item_model_preload(p->item);
        at = vec3(c->x, c->y, c->z);
        if (group_center(c->group, &cen)) {
            float dx = cen.x - c->x, dz = cen.z - c->z;
            if (dx * dx + dz * dz < 64.0f) {
                at = cen;
            }
        }
        p->home = p->pos = at;
    }
}

void loot_draw_held(int item, int side, const Mat4 *world, const Mat4 *view_proj) {
    int model, pal;
    float r, g, b;

    if (item < 0 || world == NULL || view_proj == NULL) {
        return;
    }
    if (item_prop_draw(item, side, view_proj)) {   /* (its own line for that hand, or the other hand's mirror) */
        return;
    }
    ensure_catalog();
    if (gfm_of(item, &model, &pal) && things_model_draw(model, pal, world, view_proj)) {
        return;
    }
    if (is_powder(item)) {
        ensure_sprites();
        powder_tint(item, &r, &g, &b);
        draw_flat(&sPowder, view_proj, world, r, g, b);
        return;
    }
    if (is_ball(item)) {
        ensure_sprites();
        draw_flat(&sBall, view_proj, world, 1.0f, 1.0f, 0.45f);
        return;
    }
    if (mesh_for_item(item) == MESH_KEY) {
        ensure_key();
        draw_template(&sKey, view_proj, world, -1);
        return;
    }
    ensure_card();
    draw_template(&sCard, view_proj, world, item);
}
