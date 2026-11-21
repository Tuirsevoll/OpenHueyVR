/* hg2: start-up and the main loop.
 *
 *   hg2 [options] [data-dir]
 *     --hidden          no window shown (tests, screenshots)
 *     --frames N        stop after N frames
 *     --eval "code"     run Forth after the scripts have loaded (repeatable)
 *     --import disc.iso the disc imported into the library beside the executable, then out
 *     --mod folder|zip  a mod turned on (repeatable; with --test the only mods)
 *     --mods folder     the folder of mods (instead of mods/ beside the library)
 *     --title           the title even without a window (its tests; else, hidden, a new game at once)
 *     --update          a release updated to the newest build (platform/update.c), then out
 *     --test file.fs    the test file run, headless; its exit status the test's. A quick run: the file
 *                       ends at its `full-suite` (tester) - all of it with --full or $OPENHUEY_FULL_TESTS
 *
 * The game data: a disc image or folder named (read as it is), else the library
 * (platform/library.c), else - with a window - the player is asked for the disc to import.
 *
 * Start-up: the window (the GL context and the keyboard), an OpenXR session, the renderer, the
 * Forth system with the engine's words, then ClankForth's prelude, scripts/base.fs and scripts/game.fs
 * - which decide everything else. Each frame: input, game ticks at a fixed 30 a second, the original's
 * frame rate (Forth tasks and hooks), then drawing. On a headset the picture is both eyes, every
 * refresh, with the head pose on the game's camera; tests and --hidden do not open a session. */
#include "core/files.h"
#include "forth.h"
#include "game/engine.h"
#include "game/camdirector.h"
#include "game/particles.h"
#include "game/progress.h"
#include "game/things.h"
#include "game/vrhands.h"
#include "game/vrbody.h"
#include "game/loot.h"
#include "game/props.h"
#include "game/sprites.h"
#include "game/mods.h"
#include "data/exetables.h"
#include "game/areas.h"
#include "game/exits.h"
#include "hactor/hactor.h"
#include "platform/disc.h"
#include "platform/library.h"
#include "platform/snddrv.h"
#include "platform/update.h"
#include "platform/sound.h"
#include "platform/platform.h"
#include "platform/vr.h"
#include "platform/voice.h"
#include "platform/movie.h"
#include "platform/gl.h"
#include "render/render.h"
#include "render/post.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* temporary, for the headset smoothing test; remove later */
#define VR_STEP_LOG 0

#define MAX_EVALS 16

static int sTestStatus;
static int sFullTests;   /* (a full test run: --full, $OPENHUEY_FULL_TESTS - `full-tests?`) */
static void p_exit_status(Forth *f, Word *w) {   /* ( n -- ) a test's exit status (--test) */
    (void)w;
    sTestStatus = (int)forth_pop(f);
}
static void p_error_count(Forth *f, Word *w) {   /* ( -- n ) errors raised (not in `catch`) so far */
    (void)w;
    forth_push(f, forth_errors(f));
}

typedef struct Options {
    int hidden;
    long frames;   /* 0: run until closed */
    const char *evals[MAX_EVALS];
    int nevals;
    const char *data;
    const char *import; /* --import DISC: the disc imported into the library, then out */
    int update;         /* --update: updated to the newest release, then out */
    int title;          /* --title: the title, hidden too (tests) */
    const char *test;   /* --test FILE: run it after the scripts, headless, and exit with its status */
    int full;           /* --full (or $OPENHUEY_FULL_TESTS): the test file all through, not to its full-suite */
    const char *mods[MAX_EVALS];   /* --mod DIR: a mod's folder, on (the tests' only mods) */
    const char *mods_folder;       /* --mods DIR: the folder of mods instead of <base>mods/ (the tests' too) */
    int nmods;
} Options;

static int parse_options(int argc, char **argv, Options *o) {
    int i;

    memset(o, 0, sizeof(*o));
    o->data = getenv("HG_DATA");   /* (none: the disc chosen before, or ask) */
    o->full = getenv("OPENHUEY_FULL_TESTS") != NULL && strcmp(getenv("OPENHUEY_FULL_TESTS"), "0") != 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hidden") == 0) {
            o->hidden = 1;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            o->frames = atol(argv[++i]);
        } else if (strcmp(argv[i], "--eval") == 0 && i + 1 < argc && o->nevals < MAX_EVALS) {
            o->evals[o->nevals++] = argv[++i];
        } else if (strcmp(argv[i], "--import") == 0 && i + 1 < argc) {
            o->import = argv[++i];
        } else if (strcmp(argv[i], "--title") == 0) {
            o->title = 1;
        } else if (strcmp(argv[i], "--update") == 0) {
            o->update = 1;
        } else if (strcmp(argv[i], "--mods") == 0 && i + 1 < argc) {
            o->mods_folder = argv[++i];
        } else if (strcmp(argv[i], "--mod") == 0 && i + 1 < argc && o->nmods < MAX_EVALS) {
            o->mods[o->nmods++] = argv[++i];
        } else if (strcmp(argv[i], "--full") == 0) {
            o->full = 1;
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            o->test = argv[++i];
            o->hidden = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: openhuey [--hidden] [--frames N] [--eval code] [--test file.fs [--full]] [--mod folder|zip] [--mods folder] [--import disc.iso] [--update] [--title] [disc.iso | data folder]\n");
            return 0;
        } else {
            o->data = argv[i];
        }
    }
    return 1;
}

/* the tables only the game's executable has (data/exetables.h): the library's, else read from the
 * executable - on the disc ($HG_EXE, or beside the data folder), else the repository's baserom
 * (the developers') */
static void load_world(World *w) {
    DiscFile *f = files_exist("../exe/doors.bin") ? NULL : disc_exe();
    uint8_t *data = NULL;
    size_t n = 0;
    Exe exe;

    if (files_exist("../exe/doors.bin")) {   /* (a library's: no executable wanted) */
        if (!world_load(w)) {
            fprintf(stderr, "openhuey: the library's exe/doors.bin or rooms.bin is damaged: rooms won't connect\n");
        }
        return;
    }
    if (f != NULL) {
        n = (size_t)disc_file_size(f);
        data = malloc(n);
        if (data != NULL && disc_file_read(f, data, n) != n) {
            free(data);
            data = NULL;
        }
        disc_file_close(f);
    }
    if (data == NULL && HG2_DEFAULT_EXE[0] != 0) {
        data = SDL_LoadFile(HG2_DEFAULT_EXE, &n);
    }
    if (exe_take(&exe, data, n)) {
        exetables_use_exe(&exe);
    }
    if (!world_load(w)) {
        fprintf(stderr, "openhuey: no SLUS_210.75 found (set HG_EXE): rooms won't connect\n");
    }
}

/* the scripts: $HG2_SCRIPTS, the repository's (a developer's build), or beside the executable (a
 * release: scripts/ next to it) */
static const char *script_dir(void) {
    static char dir[1024];
    char probe[1100];

    if (getenv("HG2_SCRIPTS") != NULL) {
        return getenv("HG2_SCRIPTS");
    }
    snprintf(probe, sizeof(probe), "%s/game.fs", HG2_SCRIPT_DIR);
    if (HG2_SCRIPT_DIR[0] != 0 && SDL_GetPathInfo(probe, NULL)) {
        return HG2_SCRIPT_DIR;
    }
    snprintf(dir, sizeof(dir), "%sscripts", SDL_GetBasePath() != NULL ? SDL_GetBasePath() : "./");
    return dir;
}

static int load_scripts(Forth *f) {
    const char *dir = script_dir();
    char path[1024];

    if (forth_prelude(f) != 0) {   /* (the language's rest: ClankForth's, built in) */
        return 0;
    }
    snprintf(path, sizeof(path), "%s/base.fs", dir);   /* (OpenHuey's own words for every script) */
    if (forth_include(f, path) != 0) {
        return 0;
    }
    forth_add_root(f, dir);   /* where USING: finds vocabularies */
    snprintf(path, sizeof(path), "%s/game.fs", dir);
    if (forth_include(f, path) != 0) {
        return 0;
    }
    mods_load_scripts(f, dir);   /* (a mod's failing is said, the game goes on) */
    return 1;
}

/* the room's lights for an actor, and its shadows: from its two lights with the most of the light
 * on it, each as dark as its share. The shares ease from frame to frame (SHARE_EASE), so walking
 * from one light to the next one shadow fades as the other comes - not one put out and another
 * cast at once where the brightest changes, or where a nav triangle's lights do */
#define SHADOWS_PER_ACTOR 2
#define SHADOWS_DRAWN 3    /* (one fading out as two are wanted) */
#define SHARE_EASE 0.15f   /* (of the way to the new share each frame: most of it in half a second) */
#define SHARE_LEAST 0.04f  /* (less: no shadow) */
#define SHOWN_STEP 0.05f   /* (a shadow's darkness changes at most this a frame: a fade, never a jump) */

/* a light's brightness at a point, as room_lights_at weighs it (0: none there) */
static float light_score(const Room *r, int i, Vec3 at, uint32_t mask) {
    const RoomLight *l = &r->lights[i];
    float score = (0.3f * l->color.x + 0.6f * l->color.y + 0.1f * l->color.z) * l->intensity, d;

    if (!(mask >> i & 1)) {
        return 0.0f;
    }
    if (l->range > 0.0f) {
        d = vec3_len(vec3_sub(at, l->pos));
        score = d < l->range ? score * (l->range - d) / l->range : 0.0f;
    }
    return fmaxf(score, 0.0f);
}

/* a shadow from light l (a view from it, fitted round the actor and reaching the floor beyond) */
static void cast_shadow(Actor *a, const RoomLight *l, Vec3 center, float radius, float strength) {
    Vec3 dir = vec3_sub(center, l->pos), up;
    float dist = vec3_len(dir);
    Mat4 proj, view, vp;
    int slot;

    if (l->shadow < 1.0f || dist < radius * 1.5f) {
        return;   /* (a light whose shadows reach under 1 casts none: Shadow_Draw; or the light is inside it) */
    }
    dir = vec3_scale(dir, 1.0f / dist);
    up = fabsf(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    proj = mat4_perspective(2.0f * atanf(radius * 1.25f / dist), 1.0f, fmaxf(1.0f, dist - radius * 2.0f),
                            dist + radius * 6.0f + 200.0f);
    view = mat4_look(l->pos, dir, up);
    vp = mat4_mul(proj, view);
    slot = render_shadow_add(&vp, center, radius, strength, vec3(l->pos.x, l->pos.y + 5.0f, l->pos.z), l->shadow);
    if (slot >= 0 && !a->shadow_off) {
        render_shadow_mesh(&a->gpu, a->model.d, a->model.nd);
    }
}

static void light_actor(Engine *e, Actor *a) {
    const Room *r = &e->room;
    Vec3 center = actor_center(a);
    float radius = actor_radius(a), score[16] = {0}, wanted[16], total = 0.0f, base, lum, fall = 1.0f, h, cut, sum;
    const RoomLight *l;
    int pick[SHADOWS_PER_ACTOR + 1], drawn[SHADOWS_DRAWN], tri, i, j, k, n = r->nlights < 16 ? r->nlights : 16;
    uint32_t mask;

    if (e->nstage > 0) {   /* (a stage: its lights, and no shadow map - there is nothing to fall on) */
        a->lights[0] = 0;
        a->lights[1] = e->nstage > 1 ? 1 : -1;
        a->lights[2] = e->nstage > 2 ? 2 : -1;
        a->shadow_room = 0;
        return;
    }
    room_lights_at(r, center, a->lights);
    tri = navmesh_find(&r->nav, center, 10.0f, &h);
    mask = tri >= 0 ? r->nav.tris[tri].lights : (r->nlights > 0 ? 1u : 0u);
    for (i = 0; i < n; i++) {
        score[i] = light_score(r, i, center, mask);
        total += score[i];
    }
    if (a->shadow_room != r->id + 1) {   /* (a new room, or new to it: the shares as they are) */
        for (i = 0; i < 16; i++) {
            a->shadow_share[i] = total > 0.0f && i < n ? score[i] / total : 0.0f;
        }
    } else {
        for (i = 0; i < 16; i++) {
            float want = total > 0.0f && i < n ? score[i] / total : 0.0f;

            a->shadow_share[i] += (want - a->shadow_share[i]) * SHARE_EASE;
        }
    }
    /* the shares' top two, and the next: the two wanted by how far each is above that third, so a
     * light taking over second place starts from nothing as the one it passes goes to nothing */
    for (k = 0; k <= SHADOWS_PER_ACTOR; k++) {
        pick[k] = -1;
        for (i = 0; i < n; i++) {
            int taken = 0, j;

            for (j = 0; j < k; j++) {
                taken |= pick[j] == i;
            }
            if (!taken && (pick[k] < 0 || a->shadow_share[i] > a->shadow_share[pick[k]])) {
                pick[k] = i;
            }
        }
    }
    for (i = 0; i < 16; i++) {
        wanted[i] = 0.0f;
    }
    cut = pick[SHADOWS_PER_ACTOR] >= 0 ? a->shadow_share[pick[SHADOWS_PER_ACTOR]] : 0.0f;
    for (sum = 0.0f, k = 0; k < SHADOWS_PER_ACTOR; k++) {
        sum += pick[k] >= 0 ? fmaxf(0.0f, a->shadow_share[pick[k]] - cut) : 0.0f;
    }
    if (pick[0] >= 0 && a->shadow_share[pick[0]] >= SHARE_LEAST && sum > 0.0f) {
        /* as dark as before from the brightest (a dim room's shadows lighter), times each one's part */
        l = &r->lights[pick[0]];
        if (l->range > 0.0f) {
            fall = fmaxf(0.0f, 1.0f - vec3_len(vec3_sub(center, l->pos)) / l->range);
        }
        lum = (0.3f * l->color.x + 0.6f * l->color.y + 0.1f * l->color.z) * l->intensity / 128.0f;
        base = gRender.shadow_strength * fminf(1.0f, fmaxf(0.6f, lum * fall * 4.0f)) *
               (e->extra.on ? fminf(1.0f, e->extra.scale) : 1.0f);   /* (a scene's dimmed lights cast less) */
        for (k = 0; k < SHADOWS_PER_ACTOR; k++) {
            if (pick[k] >= 0) {   /* (two lights alike: each a little lighter than one alone) */
                wanted[pick[k]] = base * fminf(1.0f, fmaxf(0.0f, a->shadow_share[pick[k]] - cut) / sum * 1.5f);
            }
        }
    }
    for (i = 0; i < 16; i++) {
        if (a->shadow_room != r->id + 1) {
            a->shadow_shown[i] = wanted[i];
        } else {
            a->shadow_shown[i] += fmaxf(-SHOWN_STEP, fminf(SHOWN_STEP, wanted[i] - a->shadow_shown[i]));
        }
    }
    a->shadow_room = r->id + 1;
    /* drawn: the darkest few as they are shown */
    for (k = 0; k < SHADOWS_DRAWN; k++) {
        int best = -1;

        for (i = 0; i < n; i++) {
            if (a->shadow_shown[i] > 0.01f && (best < 0 || a->shadow_shown[i] > a->shadow_shown[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        cast_shadow(a, &r->lights[best], center, radius, a->shadow_shown[best]);
        drawn[k] = best;
        a->shadow_shown[best] = -a->shadow_shown[best];   /* (taken: not again this frame) */
    }
    for (j = 0; j < k; j++) {
        a->shadow_shown[drawn[j]] = -a->shadow_shown[drawn[j]];
    }
}

/* the lights of the next lit draws: the actor's (or the fixed key light) */
/* a direction rotated by `angle` about a unit axis (Rodrigues) */
static Vec3 turn_about(Vec3 v, Vec3 axis, float angle) {
    float c = cosf(angle), s = sinf(angle);

    return vec3_add(vec3_add(vec3_scale(v, c), vec3_scale(vec3_cross(axis, v), s)),
                    vec3_scale(axis, vec3_dot(axis, v) * (1.0f - c)));
}

/* where the PS2 took a model's light directions from (light_setup: its root bone) */
static Vec3 light_root(const Actor *a) {
    int b;

    for (b = 0; b < a->model.nbones && a->model.bones[b].parent >= 0; b++) {
    }
    return b < a->model.nbones ? actor_bone(a, b) : actor_center(a);
}

/* the lights for the next lit draws: the room's (or a stage's) three of `idx`, the camera's
 * scripted ones, its ambient; `root` where the PS2's lighting takes their directions from */
static void lights_for(Engine *e, const int idx[3], Vec3 root) {
    DrawLight lights[3];
    Vec3 ambient = e->nstage > 0 ? e->stage_ambient : e->room.ambient;
    int n = 0, k;

    for (k = 0; k < 3; k++) {
        if (idx[k] >= 0) {
            const RoomLight *l = e->nstage > 0 ? &e->stage[idx[k]] : &e->room.lights[idx[k]];

            lights[n].pos = l->pos;
            lights[n].color = vec3_scale(l->color, l->intensity);
            lights[n].range = l->range;
            lights[n].fall = l->reach;
            n++;
        }
    }
    if (e->extra.on && e->nstage == 0) {   /* (Lights_Scripted: the camera's lights into slots 2, then 1) */
        Vec3 eye = e->camera.pos, fwd = camera_forward(&e->camera), up = vec3(0, 1, 0);
        int slot = 2;

        for (k = 0; k < n; k++) {
            lights[k].color = vec3_scale(lights[k].color, e->extra.scale);
        }
        for (; n < 3; n++) {
            lights[n].pos = eye;
            lights[n].color = vec3(0, 0, 0);
            lights[n].range = 0.0f;
            lights[n].fall = 0.0f;
        }
        ambient = vec3_add(ambient, e->extra.ambient);
        for (k = 0; k < 2; k++) {
            Vec3 d = vec3_scale(fwd, -1.0f), axis;

            if (!e->extra.cam[k].on) {
                continue;
            }
            axis = vec3_norm(vec3_cross(up, d));
            d = turn_about(d, axis, e->extra.cam[k].up);
            d = turn_about(d, up, e->extra.cam[k].about);
            lights[slot].pos = vec3_add(eye, vec3_scale(d, 100000.0f));   /* (a direction: far off, no falloff) */
            lights[slot].color = e->extra.cam[k].color;
            lights[slot].range = 0.0f;
            lights[slot].fall = 0.0f;   /* (Lights_Scripted: no falloff) */
            slot--;
        }
    }
    if (e->nstage > 0) {
        render_draw_lights(ambient, lights, n, root);
    } else {
        render_draw_lights(ambient, lights, e->room.nlights > 0 || e->extra.on ? n : -1, root);
    }
}

static void use_lights(Engine *e, const Actor *a) {
    lights_for(e, a->lights, light_root(a));
}

void engine_lights_at(Vec3 at) {
    int idx[3] = {-1, -1, -1};

    if (gEngine.nstage > 0) {
        idx[0] = 0;
        idx[1] = gEngine.nstage > 1 ? 1 : -1;
        idx[2] = gEngine.nstage > 2 ? 2 : -1;
    } else {
        room_lights_at(&gEngine.room, at, idx);
    }
    lights_for(&gEngine, idx, at);
}

/* the characters again, mirrored in each reflecting quad's plane (Reflection_Draw: those on the
 * eye's side of it; drawn faded by its strength, without their contact shadows) */
/* the side panels of castle 2F's reflecting door and window (Reflection_SidePanels): by kind (the
 * door's, the window's), the one each side */
static const Vec3 kReflPanels[4][4] = {
    {{50.0f, 30.0f, -21.3043f}, {50.0f, 30.0f, -28.75f}, {50.0f, 1.25f, -21.3043f}, {50.0f, 1.25f, -28.75f}},
    {{31.3043f, 30.0f, -10.0f}, {38.75f, 30.0f, -10.0f}, {31.3043f, 1.5f, -10.0f}, {38.75f, 1.5f, -10.0f}},
    {{10.0f, 22.0f, 58.6957f}, {10.0f, 22.0f, 51.25f}, {10.0f, 0.0f, 58.6957f}, {10.0f, 0.0f, 51.25f}},
    {{-8.6957f, 22.0f, 70.0f}, {-1.25f, 22.0f, 70.0f}, {-8.6957f, 0.0f, 70.0f}, {-1.25f, 0.0f, 70.0f}},
};

static Vec3 view_position(const Mat4 *v);

/* the characters on the eye's side drawn mirrored (`mvp`), faded by k, only on quad q (unless
 * over the whole picture) */
static void reflect_into(Engine *e, const Mat4 *proj, const Mat4 *view, const Mat4 *vp, const Mat4 *mvp, Vec3 eye,
                         Vec3 n, float d, float side, float k, const Vec3 *q, int only) {
    int i;

    if (q != NULL) {
        render_reflect_begin(vp, q);
    }
    render_camera(proj, view, eye, e->camera.znear, e->camera.zfar);
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &e->actors[i];
        float alpha = a->alpha;
        int off = a->shadow_off;

        if (!actor_shown(a) || (vec3_dot(n, a->pos) - d) * side <= 0.0f || (only >= 0 && i != only)) {
            continue;
        }
        a->alpha = alpha * fminf(k, 1.0f);
        a->shadow_off = 1;
        use_lights(e, a);
        actor_draw(a, mvp);
        a->alpha = alpha;
        a->shadow_off = off;
    }
    render_camera(proj, view, view_position(view), e->camera.znear, e->camera.zfar);
    if (q != NULL) {
        render_reflect_end(vp, q);
    }
}

/* a point's screen x in the game's pixels (512 across); 0 if behind the eye */
static float screen_x(const Mat4 *vp, Vec3 p) {
    const float *m = vp->m;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];

    return w > 0.0f ? 256.0f * (x / w + 1.0f) : 0.0f;
}

static int in_view(const Mat4 *vp, const Vec3 *q) {   /* (all four corners) */
    const float *m = vp->m;
    int k;

    for (k = 0; k < 4; k++) {
        float x = m[0] * q[k].x + m[4] * q[k].y + m[8] * q[k].z + m[12], y = m[1] * q[k].x + m[5] * q[k].y + m[9] * q[k].z + m[13];
        float z = m[2] * q[k].x + m[6] * q[k].y + m[10] * q[k].z + m[14], w = m[3] * q[k].x + m[7] * q[k].y + m[11] * q[k].z + m[15];

        if (x > w || x < -w || y > w || y < -w || z > w || z < -w) {
            return 0;
        }
    }
    return 1;
}

static void draw_reflections(Engine *e, const Mat4 *proj, const Mat4 *view, const Mat4 *vp) {
    int r;

    for (r = 0; r < 4; r++) {
        const Vec3 *q = e->refl[r].q;
        Vec3 n = vec3_norm(vec3_cross(vec3_sub(q[1], q[0]), vec3_sub(q[2], q[0])));
        Vec3 at = view_position(view);
        float d = vec3_dot(n, q[0]), side = vec3_dot(n, at) - d, k = (float)e->refl[r].strength / 128.0f;
        Mat4 m = mat4_identity(), mvp;
        Vec3 eye;
        int c, w;

        if (e->refl[r].strength <= 0 || side == 0.0f) {
            continue;
        }
        for (c = 0; c < 3; c++) {   /* p' = p - 2 (n.p - d) n */
            const float nc[3] = {n.x, n.y, n.z};

            for (w = 0; w < 3; w++) {
                m.m[c * 4 + w] -= 2.0f * nc[w] * nc[c];
            }
            m.m[12 + c] = 2.0f * d * nc[c];
        }
        mvp = mat4_mul(*vp, m);
        eye = vec3_sub(at, vec3_scale(n, 2.0f * side));
        reflect_into(e, proj, view, vp, &mvp, eye, n, d, side, k, e->refl[r].screen ? NULL : q, e->refl[r].only);
        if (e->refl[r].panels != 0 && !e->refl[r].screen) {
            /* the panels: the reflection again at half, moved by the quad's width on screen */
            float dx = (float)((int)(screen_x(vp, q[1]) * 16.0f) - (int)(screen_x(vp, q[0]) * 16.0f)) / 16.0f;
            int s;

            for (s = 0; s < 2 && dx != 0.0f; s++) {
                float sx = s == 0 ? dx : -dx, t = sx / 256.0f;   /* (in clip x, times w) */
                const Vec3 *p = kReflPanels[(e->refl[r].panels == 2 ? 0 : 2) + (sx < 0.0f ? 0 : 1)];
                Mat4 moved = mvp;

                if (!in_view(vp, p)) {
                    continue;
                }
                for (c = 0; c < 4; c++) {   /* (row 0 += t x row 3) */
                    moved.m[c * 4] += t * moved.m[c * 4 + 3];
                }
                reflect_into(e, proj, view, vp, &moved, eye, n, d, side, k * 0.5f, p, e->refl[r].only);
            }
        }
    }
}

/* how far a character has faded going out through a doorway (Character_RegionFade): in an
 * exit's area, by how far past its middle toward the outside it stands - none on the room's side
 * of the middle, all at the area's outer edge (its depth along the exit's way through) */
float doorway_faded(const Engine *e, const Actor *a) {
    int exit;

    if (!a->doorway_fade || e->room.id < 0 || e->room.id >= WORLD_ROOMS) {
        return 0.0f;
    }
    for (exit = 0; exit < ROOM_EXITS; exit++) {
        int area = e->world.exits[e->room.id][exit].camera & 0xFFFF;
        Vec3 out, in, way, c0, c1, c2, edge, middle;
        float half, past;

        if (area == 0xFFFF || !area_inside(&e->room, area, a->pos)) {
            continue;
        }
        if (exit_spot(&e->room, &e->world, exit, 0, &out) < 0 || exit_spot(&e->room, &e->world, exit, 1, &in) < 0 ||
            !area_corner(&e->room, area, 0, &c0) || !area_corner(&e->room, area, 1, &c1) ||
            !area_corner(&e->room, area, 2, &c2) || !area_middle(&e->room, area, &middle)) {
            continue;
        }
        way = vec3_norm(vec3(in.x - out.x, 0.0f, in.z - out.z));   /* (from outside in) */
        edge = vec3(c1.x - c0.x, 0.0f, c1.z - c0.z);               /* (the area's side along the way) */
        if (fabsf(vec3_dot(way, vec3_norm(edge))) < 0.70710677f) {
            edge = vec3(c1.x - c2.x, 0.0f, c1.z - c2.z);
        }
        half = 0.5f * vec3_len(edge);
        edge = vec3_norm(edge);
        if (vec3_dot(way, edge) < 0.0f) {
            edge = vec3_scale(edge, -1.0f);
        }
        past = vec3_dot(vec3_sub(middle, a->pos), edge);   /* (how far outward of the middle) */
        return half > 0.0f ? fminf(fmaxf(past / half, 0.0f), 1.0f) : 0.0f;
    }
    return 0.0f;
}

static Actor *sFp;   /* her model, hidden while the headset is inside it */
/* a mod's props in the room being played (or held in none): each lit by the room's lights about
 * its middle, as a character is */
static void draw_props(Engine *e, const Mat4 *vp) {
    static const uint32_t kAll[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    int i;

    for (i = 0; i < MAX_PROPS; i++) {
        Prop *p = props_get(i);
        int idx[3] = {-1, -1, -1};
        Vec3 c;
        Mat4 mvp;

        if (p == NULL || !p->shown || (p->room >= 0 && p->room != e->room.id)) {
            continue;
        }
        c = props_center(p);
        if (e->nstage > 0) {
            idx[0] = 0;
            idx[1] = e->nstage > 1 ? 1 : -1;
            idx[2] = e->nstage > 2 ? 2 : -1;
        } else {
            room_lights_at(&e->room, c, idx);
        }
        lights_for(e, idx, c);
        mvp = mat4_mul(*vp, props_matrix(p));
        render_mesh(&p->gpu, &mvp, p->d, p->nd, p->tex, p->ntex, kAll);
    }
}

static void draw_actor(Engine *e, Actor *a, const Mat4 *vp) {
    float alpha = a->alpha, faded = doorway_faded(e, a);

    if (a == sFp && !vr_hands_tracked() && gVrOptions.body_fp_head) {
        /* no controller tracked: her arms are still the animation's, so her body is left out of
         * the first-person view rather than drawn in it unposed. The head option off means the
         * player wants her own body in the picture: it is drawn then. */
        return;
    }

    a->alpha = alpha * (1.0f - faded);
    use_lights(e, a);
    actor_draw(a, vp);
    a->alpha = alpha;
}

/* the original's renderer layers: drawn in order (Renderer_AddPacket's 53) */
#define LAST_LAYER 53
static int actor_layer(const Actor *a) {
    return a->layer == 0 ? 0xA : a->layer < 0 ? 1 : a->layer >= LAST_LAYER ? LAST_LAYER - 1 : a->layer;
}

/* drawn in a layer after the room's fog (Fog_Draw: 9 when drawn early, else $21) */
static int fog_after(int layer) {
    return gRoomLook.has_fog && layer > (gRoomLook.fog_early ? 9 : 0x21);
}

/* the frame's blockers of the lights: the room's scripts' this tick, then the open doors' in view
 * (Door_Draw's door_portal) - for the characters' shadows (render_blockers) */
static void use_blockers(Engine *e, const RoomView *v) {
    float q[ROOM_MAX_BLOCKERS * 18];
    RoomBlocker b[ROOM_MAX_BLOCKERS];
    int n = e->nblockers, i, k;

    memcpy(b, e->blockers, sizeof(RoomBlocker) * (size_t)n);
    n = room_door_blockers(&e->room, v, b, n, ROOM_MAX_BLOCKERS);
    for (i = 0; i < n; i++) {
        const Vec3 *p[6] = {&b[i].q[0], &b[i].q[1], &b[i].q[2], &b[i].q[3], &b[i].facing, &b[i].top};

        for (k = 0; k < 6; k++) {
            q[i * 18 + k * 3] = p[k]->x;
            q[i * 18 + k * 3 + 1] = p[k]->y;
            q[i * 18 + k * 3 + 2] = p[k]->z;
        }
    }
    render_blockers(q, n);
}

/* layer 6, the original's shadow buffer: the open doors' shadows (DoorShadow_Draw) - each box's
 * sides facing the eye, shading from the door's strength to nothing - taken off the room drawn so
 * far (the characters come after). With the characters' shadows (gfx.shadow-maps) */
static void draw_door_shades(Engine *e, const RoomView *v) {
    static const int kSides[4][4] = {{0, 1, 4, 5}, {1, 3, 5, 7}, {3, 2, 7, 6}, {2, 0, 6, 4}};
    RoomShade s[ROOM_MAX_SHADES];
    int n, i, k, j;

    if (!gRender.shadow_maps || e->nstage > 0 || (n = room_door_shades(&e->room, v, s, ROOM_MAX_SHADES)) == 0 ||
        !render_shade_begin()) {
        return;
    }
    for (i = 0; i < n; i++) {
        for (k = 0; k < 4; k++) {
            Vec3 p[4];

            for (j = 0; j < 4; j++) {
                p[j] = s[i].p[kSides[k][j]];
            }
            /* (the PS2 draws the sides wound one way on the screen: those facing the eye) */
            if (vec3_dot(vec3_cross(vec3_sub(p[1], p[0]), vec3_sub(p[2], p[0])), vec3_sub(v->eye, p[0])) > 0.0f) {
                render_shade_quad(&v->vp, p, s[i].alpha);
            }
        }
    }
    render_shade_end();
}

/* layer `layer`: the room's draws in it, then the characters' ($1C, drawn softened, apart), then
 * the effects' (their drawers' layer: most $19 - so before the fog's pass at $21 and under what
 * the later layers draw, as the original's packets go out layer by layer) */
static void draw_layer(Engine *e, const Mat4 *vp, const Mat4 *view, int layer) {
    int i;

    render_fog_after(fog_after(layer));
    room_draw_layer(&e->room, vp, layer);
    for (i = 0; i < MAX_ACTORS; i++) {
        if (e->actors[i].used && actor_layer(&e->actors[i]) == layer && layer != 0x1C) {
            draw_actor(e, &e->actors[i], vp);
        }
    }
    sprites_draw_layer(vp, view, layer, LAST_LAYER - 1);
}

/* the eye, from a view matrix (its translation is -R * eye) */
static Vec3 view_position(const Mat4 *v) {
    float x = v->m[12], y = v->m[13], z = v->m[14];

    return vec3(-(v->m[0] * x + v->m[1] * y + v->m[2] * z),
                -(v->m[4] * x + v->m[5] * y + v->m[6] * z),
                -(v->m[8] * x + v->m[9] * y + v->m[10] * z));
}

/* camera_view stores -forward in the third row */
static Vec3 view_forward(const Mat4 *v) {
    return vec3(-v->m[2], -v->m[6], -v->m[10]);
}

/* the passes DEL tries in turn: each one switched off, then all of them ('' = none off) */
static const char *const kSkipModes[] = {"",        "things",  "props", "vrhands", "sprites",
                                         "glow",    "shine",   "refl",  "loot",
                                         "things,props,vrhands,sprites,glow,shine,refl,loot"};
static int sSkipMode = 0;

int engine_skip(const char *what) {
    const char *list = getenv("OPENHUEY_SKIP");

    if (list == NULL) {
        list = kSkipModes[sSkipMode];
    }
    return list != NULL && what != NULL && strstr(list, what) != NULL;
}

void engine_skip_cycle(void) {
    char title[160];
    const char *mode = kSkipModes[0];

    sSkipMode = (sSkipMode + 1) % (int)(sizeof(kSkipModes) / sizeof(kSkipModes[0]));
    mode = kSkipModes[sSkipMode];
    snprintf(title, sizeof(title), "openhuey  -  skip: %s", mode[0] != 0 ? mode : "none");
    platform_title(title);
    printf("skip: %s\n", mode[0] != 0 ? mode : "none");
}

/* one picture: the window, or one eye. Shadows were already drawn. */
static void draw_scene(Engine *e, const Mat4 *proj, const Mat4 *view, Vec3 eye, Vec3 forward) {
    Mat4 vp = mat4_mul(*proj, *view);
    RoomView rv;
    int i, layer;

    render_camera(proj, view, eye, e->camera.znear, e->camera.zfar);
    render_blockers(NULL, 0);
    if (e->alone > 0) {   /* (one model on the background, nothing else: the extras' model gallery) */
        if (e->alone - 1 < MAX_ACTORS && e->actors[e->alone - 1].used) {
            draw_actor(e, &e->actors[e->alone - 1], &vp);
        }
        render_post();
        return;
    }
    room_draw_begin(&e->room, eye, forward);
    rv = room_view(proj, view, eye, e->camera.znear, e->camera.zfar);
    use_blockers(e, &rv);
    for (layer = 0; layer < 0x1E; layer++) {   /* the room's layers and the characters' in order */
        if (layer == 6) {
            draw_door_shades(e, &rv);
        }
        draw_layer(e, &vp, view, layer);
    }
    for (i = 0; i < MAX_ACTORS && !(e->actors[i].used && e->actors[i].layer == 0x1C); i++) {
    }
    if (i < MAX_ACTORS && post_soft_begin()) {   /* layer $1C: drawn softened (at half size) */
        for (i = 0; i < MAX_ACTORS; i++) {
            if (e->actors[i].used && e->actors[i].layer == 0x1C) {
                int off = e->actors[i].shadow_off;

                e->actors[i].shadow_off = 1;   /* (its shadow stays in the scene: not softened) */
                draw_actor(e, &e->actors[i], &vp);
                e->actors[i].shadow_off = off;
            }
        }
        post_soft_end();
    }
    if (!engine_skip("refl")) {
        draw_reflections(e, proj, view, &vp);
    }
    for (layer = 0x1E; layer < LAST_LAYER; layer++) {   /* (the later layers: in front of a reflection over everything) */
        draw_layer(e, &vp, view, layer);
    }
    render_fog_after(0);
    if (!engine_skip("things")) {
        things_draw(e->room.id, &vp);   /* the things lying about (their sprites written for the next) */
    }
    if (!engine_skip("props")) {
        draw_props(e, &vp);   /* a mod's own models (props.h) */
    }
    if (!engine_skip("vrhands")) {
        vrhands_defense_draw(&vp);      /* an equipped magnesia or antimony, still in the hand */
    }
    if (!engine_skip("sprites")) {
        sprites_draw(&vp, view);   /* the effects (after the opaque scene: they don't write depth) */
    }
    if (!engine_skip("glow") && sprites_glowing() && post_glow_begin()) {   /* the glow pass: the glowing ones again */
        sprites_draw_glow(&vp);
        post_glow_end();
    }
    if (!engine_skip("shine") && post_shine_begin()) {   /* layer $14 again, for its shine */
        for (i = 0; i < MAX_ACTORS; i++) {
            if (e->actors[i].used && e->actors[i].layer == 0x14) {
                use_lights(e, &e->actors[i]);
                actor_draw(&e->actors[i], &vp);
            }
        }
        post_shine_end();
    }
    render_post();
}

/* menus, prompts, the console: one layer, in the picture's pixels */
static void draw_flat(Engine *e) {
    engine_draw_2d(e);   /* scripts' 2D (on-draw hooks) */
    if (e->hud[0] != 0) {
        float scale = e->height >= 900 ? 3.0f : 2.0f;

        render_text(16, (float)e->height - render_line_height(scale) - 16, scale, 0xF0E8C0FF, e->hud, (int)strlen(e->hud));
    }
    render_end();
    if (e->screenshot[0] != 0) {   /* (the game's picture: before the console goes over it) */
        if (render_screenshot(e->screenshot)) {
            forth_printf(e->forth, "saved %s\n", e->screenshot);
        } else {
            forth_printf(e->forth, "screenshot: can't write %s\n", e->screenshot);
        }
        e->screenshot[0] = 0;
    }
    console_draw(&e->console, e->width, e->height);
    render_end();
}

static void draw_eye(Engine *e, const Mat4 *proj, const Mat4 *view) {
    draw_scene(e, proj, view, view_position(view), view_forward(view));
    draw_flat(e);
}

/* the menu's picture: a 4:3 panel, laid out once. The headset shows that one picture. */
enum { UI_W = 1280, UI_H = 960 };

static void prepare_shadows(Engine *e) {
    int i;

    render_shadows_begin();
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &e->actors[i];

        a->lights[0] = a->lights[1] = a->lights[2] = -1;
        if (!actor_shown(a)) {
            /* Fiona's pose is not run at all when she is not drawn: the VR body and the arms
             * would be left where they were. Say why once a model, in VR. */
            if (vr_active() && a->used && strncmp(a->name, "O_FI", 4) == 0) {
                vrhands_note_skip(a, a->visible ? "she is not drawn this frame (the world is frozen)" :
                                                  "she is not shown");
            }
            continue;
        }
        {
            actor_prepare(a);
            /* The headset is at her eyes: her head, face and hair are collapsed in the drawn mesh
             * so the view is not inside them. The VR body's own shrink (game/vrbody.c) does the
             * same at the bone level, and both are the one option: "Body: Hide Head in First
             * Person" (vr.body-fp-head). Off, the head is there - for a mirror, a shadow or a
             * player who wants to see it, at the price of the eye being inside it. */
            if (a == sFp && gVrOptions.body_fp_head) {
                actor_hide_head(a);
            }
            light_actor(e, a);
        }
    }
    render_shadows_end();
}

static int cutscene_on_screen(void) {
    return gCamDir.scene || (movie_status() == MOVIE_PLAYING && !movie_paused());
}

/* where "looking ahead" sits. her yaw 0 faces +Z and a camera yaw of 0 faces -Z, so the
 * camera yaw along her facing is pi minus her yaw. a right-stick turn adds to her yaw and
 * subtracts here: the headset keeps its direction in the room while she turns. lean and
 * look are applied on top. taken again after a cutscene, the free camera, or a recenter. */
static float sAlign;
static int sAlignSet;
static int sRecenter;

/* the look the blend took this frame (draw): her eyes on the blended place, the height of her head
 * as the last posed skeleton has it - the same the hands' anchor was set on. draw_frame uses it for
 * the played camera instead of looking again, so the two cannot disagree (cleared on every other
 * frame: then draw_frame looks for itself, as it always has). */
static Camera sBlendCam;
static int sBlendCamHave;

/* her eyes. the headset's own orientation is applied on top of sAlign, so looking around does not
 * go through her body yaw. a cutscene and the free camera keep the game's camera. */
static int fiona_view(Engine *e, Camera *out) {
    int i;
    Actor *pick = NULL;

    sFp = NULL;
    vr_anchor_set(0, vec3(0.0f, 0.0f, 0.0f), 0.0f);
    if (gCamDir.scene || freecam_active()) {
        sRecenter = 1;
        return 0;
    }
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &e->actors[i];

        if (!a->used || !a->visible || strncmp(a->name, "O_FI", 4) != 0) {
            continue;
        }
        pick = a;
        break;
    }
    if (pick == NULL || !vr_tracking()) {
        return 0;
    }
    if (vr_take_recenter()) {
        sRecenter = 1;
        vr_crouch_forget_anchor();
    }
    sAlign -= vr_take_turn();
    if (!sAlignSet || sRecenter) {
        /* upright neutral already faces her. The headset's tilt at this moment is not stored. */
        sAlign = 3.14159265f - pick->yaw;
        sAlignSet = 1;
        sRecenter = 0;
    }
    *out = e->camera;
    out->yaw = sAlign;
    out->pitch = 0.0f;
    out->roll = 0.0f;
    {
        Vec3 bone = pick->head.head > 0 ? actor_bone(pick, pick->head.head)
                                        : vec3(pick->pos.x, pick->pos.y + 14.0f, pick->pos.z);
        float hy = bone.y;

        /* The VR body (game/vrbody.c) moves her head with her body and shrinks it from her own
         * eyes: the camera keeps the head it found before that, so the pose never feeds back into
         * the view. Without a body (a cutscene, no headset pose) it is her head as it is posed. */
        vrbody_eye_y(pick, &hy);
        /* height from the head, but not its walk-cycle sway: that yaw is the old camera's fight.
         * While a headset crouch is active, her standing eye height is the anchor: the body
         * crouches or walks under it, so the view and the hands stay at the player's head. */
        out->pos.x = pick->pos.x + sinf(sAlign);
        out->pos.y = vr_crouch_anchor(hy + 1.5f);
        out->pos.z = pick->pos.z - cosf(sAlign);
    }
    vr_anchor_set(1, out->pos, out->yaw);
    sFp = pick;
    return 1;
}

/* an angle's way to another, wrapped into -pi..pi: a turn past the seam is the short one */
static float angle_delta(float to, float from) {
    float d = to - from;

    while (d > 3.14159265f) {
        d -= 6.28318531f;
    }
    while (d < -3.14159265f) {
        d += 6.28318531f;
    }
    return d;
}

/* an angle wrapped into -pi..pi */
static float angle_wrap(float a) {
    while (a > 3.14159265f) {
        a -= 6.28318531f;
    }
    while (a < -3.14159265f) {
        a += 6.28318531f;
    }
    return a;
}

/* Her place and her turn as the last two ticks left them, so the headset's picture can draw her
 * between them (draw, below). The game still moves and turns her at its 30 ticks a second and its
 * own values are never left changed: only the picture is eased. Her motion's frame and the height
 * of her head are not blended, and no other actor. */
#define POSE_SNAP 3.0f     /* room units: further than this in one tick is a cut, not a walk */
#define TURN_SNAP 1.05f    /* radians (60 degrees): a bigger turn in one tick is a cut, not a turn */

static struct {
    int have;        /* prev and cur are hers: a tick has run with her in this room */
    int room;        /* the room they were taken in (e->room.id) */
    Actor *who;      /* the actor they are of (her) */
    Vec3 prev, cur;  /* where she was at the tick before, and where the last tick left her */
    float prev_yaw, cur_yaw;       /* her body's yaw at the same two ticks */
    float prev_align, cur_align;   /* the camera's yaw (sAlign) as each tick left it */
    float alpha;     /* how far from prev towards cur the picture draws (0 prev .. 1 cur) */
} sPose;

/* the Fiona in the room: the first actor used, shown and named O_FI..., as fiona_view finds her */
static Actor *fiona_pick(Engine *e) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &e->actors[i];

        if (a->used && a->visible && strncmp(a->name, "O_FI", 4) == 0) {
            return a;
        }
    }
    return NULL;
}

/* before a tick: where the last one left her becomes the one before this */
static void fiona_pose_before(Engine *e) {
    (void)e;
    if (sPose.have) {
        sPose.prev = sPose.cur;
        sPose.prev_yaw = sPose.cur_yaw;
        sPose.prev_align = sPose.cur_align;
    }
}

/* after a tick: where she is now, and which way. A new room, another her, a jump further than a
 * step (POSE_SNAP) or a turn further than TURN_SNAP (hers or the camera's) starts again from here:
 * no blend that tick. */
static void fiona_pose_after(Engine *e) {
    Actor *p = fiona_pick(e);
    float dx, dy, dz;
    float yaw, align, dyaw, dalign;
    int cut;

    if (p == NULL) {
        sPose.have = 0;   /* (no her: nothing to blend) */
        return;
    }
    yaw = p->yaw;
    /* the camera's yaw at the tick's end: the turn waiting has not been taken off sAlign yet
     * (fiona_view does that), so take it off here */
    align = sAlign - vr_peek_turn();
    dx = p->pos.x - sPose.cur.x;
    dy = p->pos.y - sPose.cur.y;
    dz = p->pos.z - sPose.cur.z;
    dyaw = angle_delta(yaw, sPose.cur_yaw);
    dalign = angle_delta(align, sPose.cur_align);
    cut = !sPose.have || sPose.room != e->room.id || sPose.who != p ||
          dx * dx + dy * dy + dz * dz > POSE_SNAP * POSE_SNAP ||
          dyaw > TURN_SNAP || dyaw < -TURN_SNAP || dalign > TURN_SNAP || dalign < -TURN_SNAP;
    if (cut) {
        sPose.prev = p->pos;   /* (a cut: from here, not between the two) */
        sPose.prev_yaw = yaw;
        sPose.prev_align = align;
    }
    sPose.cur = p->pos;
    sPose.cur_yaw = yaw;
    sPose.cur_align = align;
    sPose.room = e->room.id;
    sPose.who = p;
    sPose.have = 1;
}

/* ---- the comfort vignette (scripts/subscreen/vr.fs: vr.comfort, vr.comfort-strength) ----
 * How much of the picture's corners darken while she is moved or turned: her movement as the last
 * tick left it - the step across, up or ahead and the turn of her body and the camera - so a walk,
 * a run, a ladder and a script's step all count, and a still one counts nothing. Measured once a
 * tick (a frame between two ticks sees the same figure; measuring it a frame would read 0 on most
 * of them and never grow), then eased in quickly and out slowly, a frame at a time. Nothing while
 * the world is held or a scene has the camera. */
#define COMFORT_SPEED_START 4.0f    /* room units a second: a crawl, under it */
#define COMFORT_SPEED_FULL 15.0f    /* her walk (about 13) is nearly full comfort; a run past it */
#define COMFORT_TURN_START 0.4f     /* radians a second */
#define COMFORT_TURN_FULL 1.6f      /* a smooth turn at full stick */
#define COMFORT_IN 0.12f            /* seconds to darken the corners */
#define COMFORT_OUT 0.45f           /* ... and to let them go again */
static float sComfort;       /* what is drawn now */
static float sComfortWant;   /* what the last tick asked for */
static Uint64 sComfortAt;

/* after a tick: how hard she was moved and turned in that one */
static void comfort_measure(Engine *e) {
    float target = 0.0f, k;

    sComfortWant = 0.0f;
    if (!gVrOptions.comfort || !sPose.have || e->paused || cutscene_on_screen() ||
        freecam_active() || gCamDir.scene) {
        return;
    }
    {
        float dx = sPose.cur.x - sPose.prev.x;
        float dy = sPose.cur.y - sPose.prev.y;
        float dz = sPose.cur.z - sPose.prev.z;
        float step = sqrtf(dx * dx + dy * dy + dz * dz) * (float)TICKS_PER_SECOND;
        float turn = (fabsf(angle_delta(sPose.cur_align, sPose.prev_align)) +
                      fabsf(angle_delta(sPose.cur_yaw, sPose.prev_yaw))) * (float)TICKS_PER_SECOND;

        target = (step - COMFORT_SPEED_START) / (COMFORT_SPEED_FULL - COMFORT_SPEED_START);
        k = (turn - COMFORT_TURN_START) / (COMFORT_TURN_FULL - COMFORT_TURN_START);
        if (k > target) {
            target = k;
        }
        sComfortWant = target < 0.0f ? 0.0f : target > 1.0f ? 1.0f : target;
    }
}

/* a frame: the last tick's want, eased, and given to the picture */
static void comfort_tick(Engine *e) {
    Uint64 now = SDL_GetTicksNS();
    float dt = sComfortAt != 0 ? (float)(now - sComfortAt) / 1.0e9f : 0.0f;
    float k;

    (void)e;
    sComfortAt = now;
    if (dt > 0.1f) {
        dt = 0.1f;   /* (a stall is not movement) */
    }
    k = dt / (sComfortWant > sComfort ? COMFORT_IN : COMFORT_OUT);
    if (k > 1.0f) {
        k = 1.0f;
    }
    sComfort += (sComfortWant - sComfort) * k;
    /* the options page's preview: the ring at the strength chosen, the moment the row is changed */
    render_comfort(vr_comfort_previewing() ? gVrOptions.comfort_strength
                                           : sComfort * gVrOptions.comfort_strength);
}

static void hear_from_her(Engine *e) {
    Camera cam;
    Mat4 body;

    if (vr_active() && fiona_view(e, &cam) && vr_body_view(&body)) {
        Mat4 view = mat4_mul(body, camera_view(&cam));
        Vec3 fwd = view_forward(&view);
        float flat = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);

        /* the same forward the eyes will render, levelled. the stick moves along it. */
        if (flat > 0.25f) {
            vr_set_move_yaw(atan2f(fwd.x, -fwd.z));   /* camera heading: the stick turns by this */
            vr_set_face_yaw(atan2f(fwd.x, fwd.z));    /* body heading: 0 along +z, where she is looking */
        }
        engine_set_ear(&view);
        return;
    }
    sFp = NULL;
    engine_set_ear(NULL);
}

#if VR_STEP_LOG
/* (temporary, for the headset smoothing test; remove later) how long the picture's parts take, in
 * microseconds: prepare_shadows, and a whole blended draw_frame as its wrapper times it. Both are
 * the last draw's - the DRAW line runs before this draw's own are known. */
static unsigned long pre_us;
static unsigned long total_us;

/* a performance-counter span in microseconds (0 when the counter has no frequency) */
static unsigned long perf_us(Uint64 from, Uint64 to) {
    Uint64 f = SDL_GetPerformanceFrequency();

    return f > 0 ? (unsigned long)((to - from) * 1000000ull / f) : 0;
}

/* (temporary, for the headset smoothing test; remove later) where she is and where the anchor is,
 * once a draw, as the headset sees them: pos and yaw are the Fiona in the room, the anchor and its
 * yaw are what vr_anchor_get gives at that moment. Only the draws that find her print, 400 of them. */
static void vr_step_log_draw(Engine *e) {
    static int n;
    Actor *pick = NULL;
    Vec3 eye = vec3(0.0f, 0.0f, 0.0f);
    float yaw = 0.0f;
    float stick_x = 0.0f, stick_y = 0.0f;
    unsigned pose_us = 0, stop_us = 0;
    char hy[32];
    int i;

    if (n >= 400) {
        return;   /* (enough: the test has what it needs) */
    }
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &e->actors[i];

        if (a->used && a->visible && strncmp(a->name, "O_FI", 4) == 0) {   /* (the same search fiona_view does) */
            pick = a;
            break;
        }
    }
    if (pick == NULL) {
        return;   /* (nothing to report while she is not there) */
    }
    n++;   /* (the lines printed, not the draws) */
    if (!vr_anchor_get(&eye, &yaw)) {   /* (no anchor this frame: no eyes yet, so nothing to report) */
        eye = vec3(0.0f, 0.0f, 0.0f);
        yaw = 0.0f;
    }
    if (pick->head.head > 0) {
        snprintf(hy, sizeof(hy), "%.3f", actor_bone(pick, pick->head.head).y);   /* (her head bone's height) */
    } else {
        snprintf(hy, sizeof(hy), "none");   /* (no head bone to read) */
    }
    vrhands_take_times(&pose_us, &stop_us);   /* (the hands' time since the last line, then cleared) */
    vr_stick(&stick_x, &stick_y);
    fprintf(stderr, "DRAW %d ticks=%ld pos=%.3f %.3f %.3f yaw=%.4f align=%.4f anchor=%.3f %.3f %.3f anchor_yaw=%.4f hy=%s fr=%.2f mo=%d pre_us=%lu tot_us=%lu pose_us=%u stop_us=%u stick=%.2f %.2f\n",
            n, e->ticks, pick->pos.x, pick->pos.y, pick->pos.z, pick->yaw, sAlign,
            eye.x, eye.y, eye.z, yaw, hy, pick->frame, (int)pick->motion, pre_us, total_us,
            pose_us, stop_us, stick_x, stick_y);
}
#endif

static void draw_frame(Engine *e) {
#if VR_STEP_LOG
    if (vr_active()) {
        vr_step_log_draw(e);
    }
    {   /* (temporary: how long the shadows take, for the DRAW line) */
        Uint64 t0 = SDL_GetPerformanceCounter();

        prepare_shadows(e);
        pre_us = perf_us(t0, SDL_GetPerformanceCounter());
    }
#else
    prepare_shadows(e);
#endif
    if (vr_active()) {
        int32_t aspect = gRender.aspect;
        int i, n = vr_eye_count();
        int screen = cutscene_on_screen();

        loot_draw_from_hands();   /* (a thing in a hand: drawn from the frame its hand is drawn from) */

        {
            static int menu_was, screen_was;
            int menu = e->paused || e->console.open;

            if ((menu && !menu_was) || (screen && !screen_was)) {
                vr_screen_face();   /* a menu or a cutscene opens in front of the current look, then stays put */
            }
            menu_was = menu;
            screen_was = screen;
        }
        if (screen) {
            vr_screen_loose(0);
            sFp = NULL;   /* the scene shows her; the picture is the projected screen */
            /* the cutscene is drawn once, then put on a screen in front of the player. The
             * eyes keep the real head pose, so looking around is looking around the screen. */
            Mat4 proj, view;
            Vec3 corners[4];
            unsigned stage = render_stage_fbo(1280, 960);

            gRender.aspect = 1;
            e->width = 1280;
            e->height = 960;
            render_set_present(stage);
            render_begin(1280, 960, e->clear);
            proj = camera_proj(&e->camera, 4.0f / 3.0f);
            view = camera_view(&e->camera);
            draw_eye(e, &proj, &view);
            vr_cutscene_screen(1, corners);
            vr_lock_pose(0);
            for (i = 0; i < n; i++) {
                Mat4 eye_proj, eye_view, vp;
                int w, h;
                unsigned fbo;

                if (!vr_eye_begin(i, 1.0f, 2000.0f, &w, &h, &eye_proj, &eye_view, &fbo)) {
                    vr_eye_end(i);
                    continue;
                }
                e->width = w;
                e->height = h;
                {
                    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};

                    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                    glViewport(0, 0, w, h);
                    glDisable(GL_FRAMEBUFFER_SRGB);
                    glClearNamedFramebufferfv(fbo, GL_COLOR, 0, black);
                }
                vp = mat4_mul(eye_proj, eye_view);
                render_show_stage(render_stage_tex(), &vp, corners);
                if (i == 0) {
                    platform_mirror(fbo, w, h);
                }
                vr_eye_end(i);
            }
        } else {
            vr_screen_loose(1);
            /* a menu's panel is placed once, when it opens, in front of where the player is looking
             * then - and left there, in the room, like a cutscene's screen (vr_screen_face is called
             * as the sub screen opens: subscreen.fs) */
            /* menus and prompts share one picture. It is placed when something opens it, then
             * stays in the room: turning the head does not slide it. */
            Vec3 corners[4];
            unsigned ui = render_stage_fbo(UI_W, UI_H);

            gRender.aspect = 1;
            e->width = UI_W;
            e->height = UI_H;
            render_overlay_begin(UI_W, UI_H, ui);
            draw_flat(e);
            vr_cutscene_screen(1, corners);
            gRender.aspect = 0;   /* an eye is the whole picture; the headset's field of view replaces the window's */
            vr_lock_pose(gCamDir.event);
            {
                Camera played;
                Mat4 body;
                int fp;

                if (sBlendCamHave) {   /* (her eyes' own look, taken before the shadows: the same head height as the hands) */
                    played = sBlendCam;
                    fp = vr_body_view(&body);
                } else {
                    fp = fiona_view(e, &played) && vr_body_view(&body);
                }

                if (!fp) {
                    sFp = NULL;
                }
                for (i = 0; i < n; i++) {
                    Mat4 proj, offset, view, head, vp;
                    int w, h;
                    unsigned fbo;

                    /* the near plane is this frame's value (the camera's): the body lowers it as
                     * she looks down, so her own body does not clip away under her */
                    if (!vr_eye_begin(i, vrbody_near(e->camera.znear), e->camera.zfar, &w, &h, &proj, &offset, &fbo)) {
                        vr_eye_end(i);
                        continue;
                    }
                    e->width = w;
                    e->height = h;
                    render_set_present(fbo);
                    render_begin(w, h, e->clear);
                    {
                        Mat4 eye_body;

                        if (fp && vr_eye_body(i, &eye_body)) {
                            view = mat4_mul(eye_body, camera_view(&played));
                        } else {
                            view = mat4_mul(fp ? body : offset, camera_view(fp ? &played : &e->camera));
                        }
                    }
                    draw_scene(e, &proj, &view, view_position(&view), view_forward(&view));
                if (vr_head_view(i, &head)) {
                    vp = mat4_mul(proj, head);
                    render_show_stage(render_stage_tex(), &vp, corners);
                }
                    if (i == 0) {
                        platform_mirror(fbo, w, h);
                    }
                    vr_eye_end(i);
                }
            }
            vr_lock_pose(0);
        }
        platform_swap();
        render_set_present(0);
        gRender.aspect = aspect;
    } else {
        Mat4 proj, view;

        platform_size(&e->width, &e->height);
        render_set_present(0);
        render_begin(e->width, e->height, e->clear);
        proj = camera_proj(&e->camera, render_aspect());
        view = camera_view(&e->camera);
        draw_eye(e, &proj, &view);
        platform_swap();
    }
}

/* the picture. On a headset, mid-step or mid-turn, she is drawn where the last two ticks left her
 * - as far from the tick before as this frame's lag is past the last one - her body's yaw and the
 * camera's eased the same way, and at once put back where the game has her: only the picture moves,
 * never the game's own values (pos and yaw and sAlign are restored on every path). */
static void draw(Engine *e) {
    Actor *p = fiona_pick(e);

    comfort_tick(e);   /* (the corners darken with how she is moved: before anything is drawn) */
    sBlendCamHave = 0;   /* (only a blend sets it: otherwise draw_frame looks for itself) */
    if (vr_active() && sPose.have && p != NULL && p == sPose.who && p->drive == NULL &&
        !cutscene_on_screen() && !freecam_active() && !gCamDir.scene) {
        Vec3 saved_pos = p->pos;
        float saved_yaw = p->yaw;
        float saved_align;
#if VR_STEP_LOG
        Uint64 t0;   /* (temporary: the blended frame's own time) */
#endif
        Camera blended;

        p->pos = vec3(sPose.prev.x + (sPose.cur.x - sPose.prev.x) * sPose.alpha,
                      sPose.prev.y + (sPose.cur.y - sPose.prev.y) * sPose.alpha,
                      sPose.prev.z + (sPose.cur.z - sPose.prev.z) * sPose.alpha);
        p->yaw = sPose.prev_yaw + angle_delta(sPose.cur_yaw, sPose.prev_yaw) * sPose.alpha;
        /* the first look, on her blended place and turn: it takes the waiting turn off sAlign (and
         * a recenter), so sAlign is the tick's end from here - the value the game is kept at (her
         * eyes on it, before the hands are posed) */
        fiona_view(e, &blended);
        saved_align = sAlign;   /* (taken here: the turn it just took stays taken - the frame only blends) */
        sAlign = angle_wrap(sPose.prev_align + angle_delta(sPose.cur_align, sPose.prev_align) * sPose.alpha);
        if (fiona_view(e, &blended)) {   /* (again, so the eyes and the camera use the blended yaw) */
            sBlendCam = blended;   /* (the camera the hands' anchor was set on: draw_frame uses this one) */
            sBlendCamHave = 1;
        }
#if VR_STEP_LOG
        t0 = SDL_GetPerformanceCounter();   /* (temporary: the whole blended frame, timed) */
#endif
        draw_frame(e);
#if VR_STEP_LOG
        total_us = perf_us(t0, SDL_GetPerformanceCounter());
#endif
        p->pos = saved_pos;   /* (the game's own place and turn: no frame leaves them moved) */
        p->yaw = saved_yaw;
        sAlign = saved_align;
        return;
    }
    draw_frame(e);
}

/* the game's Forth and its scripts (game.fs spawns the actors: the title first) */
static void start_scripts(Engine *e, int hidden) {
    e->forth = forth_new(16 << 20);   /* (the converted event scripts take most) */
    forth_set_output(e->forth, console_output, &e->console);
    bind_engine(e->forth);
    bind_state(e->forth);
    bind_hactor(e->forth);
    bind_mods(e->forth);
    forth_report_crashes(e->forth);   /* (a crash names the Forth words running) */
    bind_update(e->forth);
    forth_set_effect(e->forth, forth_prim(e->forth, "exit-status", p_exit_status), "( n -- )");
    forth_set_effect(e->forth, forth_prim(e->forth, "error-count", p_error_count), "( -- n )");
    forth_constant(e->forth, "hidden?", hidden ? -1 : 0);   /* (no window: tests, screenshots - no title, a new game at once; --title: the title even so) */
    forth_constant(e->forth, "vr?", vr_active() ? -1 : 0);
    forth_constant(e->forth, "full-tests?", sFullTests ? -1 : 0);   /* (a full test run: the long tests too) */
    if (!load_scripts(e->forth)) {
        fprintf(stderr, "openhuey: the scripts didn't load (see above); the console is open\n");
        e->console.open = 1;
    }
}

/* a fresh game (`soft-reset`: the game over's return to the title, as the original makes its
 * title scene anew): everything the scripts made let go - the actors and their models, the
 * room, the effects and particles, the 2D, sounds, music, movie; the progress cleared but for its unlocks - and the scripts
 * started again */
static void soft_reset(Engine *e, int hidden) {
    int i;

    fprintf(stderr, "openhuey: a fresh game\n");
    hactor_reset();
    post_trail_off();   /* (the renderer made anew: no frame blend) */
    post_panic_reset();   /* (nor a panic screen of the game that was) */
    for (i = 0; i < MAX_ACTORS; i++) {
        if (e->actors[i].used) {
            actor_free(&e->actors[i]);
        }
    }
    room_free(&e->room);
    e->room.id = -1;
    particles_free_all();   /* (before the sprites: each system lets go of its own batches) */
    sprites_free_all();
    bind_engine_reset();
    progress_clear();   /* (the unlocks kept) */
    world_rebuild_exits(&e->world);   /* (Rooms_SetState with no save: no door closed off, the exits anew) */
    things_clear();
    props_clear();   /* (a mod's props: its scripts load them again) */
    e->nhooks = e->ndraw_hooks = 0;
    e->nstage = 0;
    memset(&e->extra, 0, sizeof(e->extra));
    e->hud[0] = 0;
    e->paused = 0;
    e->still = 0;
    e->clear = vec3(0, 0, 0);
    memset(e->held, 0, sizeof(e->held));
    e->camera = (Camera){vec3(0, 0, 0), 0, 0, 1.0f, 5.0f, 20000.0f, 1.0f, 0.0f};
    forth_free(e->forth);
    start_scripts(e, hidden);
}

int main(int argc, char **argv) {
    static const bool kNoKeys[SDL_SCANCODE_COUNT];
    Engine *e = &gEngine;
    Options opt;
    Uint64 last, lag = 0;
    int typing = 0;   /* SDL's text input on (the console open) */
    const Uint64 tick_ns = 1000000000ull / TICKS_PER_SECOND;
    long frame = 0;
    int fullscreen = 0;   /* (the window as it is) */
    int i;

    if (!parse_options(argc, argv, &opt)) {
        return 2;
    }
    if (opt.update) {
        return update_run();
    }
    update_cleanup();   /* (an update's leftovers) */
    if (opt.import != NULL) {   /* (hga --import <disc>: the library made, nothing played) */
        return library_import_from(opt.import, opt.hidden) ? 0 : 1;
    }
    if (!library_choose(opt.data, HG2_DEFAULT_DATA, opt.hidden)) {
        return 1;
    }
    {   /* (tests: only --mod's, or a folder named with --mods) */
        char folder[1100];

        if (opt.mods_folder != NULL) {
            snprintf(folder, sizeof(folder), "%s/", opt.mods_folder);
        }
        mods_find(opt.mods_folder != NULL ? folder : opt.test != NULL ? NULL : library_mods_dir(), opt.mods, opt.nmods);
    }
    if (!platform_open("OpenHuey", 1280, 720, opt.hidden)) {
        return 1;
    }
    if (!opt.hidden && opt.test == NULL && !vr_open()) {   /* the picture is the headset; tests stay headless */
        platform_close();
        return 1;
    }
    if (vr_active()) {
        platform_swap_interval(0);   /* the runtime paces the frames */
    }
    if (!render_init()) {
        vr_close();
        platform_close();
        return 1;
    }

    memset(e, 0, sizeof(*e));
    sprites_init();
    platform_size(&e->width, &e->height);   /* (frames ticked before the first draw - tests, --eval - see it) */
    e->room.id = -1;
    e->hand_item = -1;
    e->camera = (Camera){vec3(0, 0, 0), 0, 0, 1.0f, 5.0f, 20000.0f, 1.0f, 0.0f};
    load_world(&e->world);
    if (!sound_open()) {   /* (none: the game is silent) */
        fprintf(stderr, "openhuey: no sound (%s)\n", SDL_GetError());
    }
    {   /* the reverb's presets: libsd's, from the game's modules beside the data folder */
        size_t n = 0;
        uint8_t *libsd = files_read("../MODULES/LIBSD.IRX", &n);

        if (!snddrv_reverb_setup(libsd, n)) {
            fprintf(stderr, "openhuey: no MODULES/LIBSD.IRX on the disc: no reverb\n");
        }
        free(libsd);
    }
    e->testing = opt.test != NULL;
    movie_game_clock(opt.test != NULL || opt.frames > 0);   /* (headless runs: the same frames each run) */
    sFullTests = opt.full;
    start_scripts(e, opt.hidden && !opt.title);
    for (i = 0; i < opt.nevals; i++) {
        forth_eval(e->forth, opt.evals[i], strlen(opt.evals[i]), "--eval");
    }
    if (opt.test != NULL) {   /* (its folder a root too: its USING:s, e.g. tester) */
        char root[1024], *slash;
        int failed;

        snprintf(root, sizeof(root), "%s", opt.test);
        slash = strrchr(root, '/');
        if (slash != NULL) {
            *slash = 0;
            forth_add_root(e->forth, root);
            slash = strrchr(root, '/');   /* and the folder above (tests/: shared helpers) */
            if (slash != NULL) {
                *slash = 0;
                forth_add_root(e->forth, root);
            }
        }
        if (!opt.full) {   /* (a quick run: to the file's full-suite) */
            static const char quick[] = "USING: tester ; -1 quick-tests !";
            forth_eval(e->forth, quick, strlen(quick), "--test");
        }
        failed = forth_include(e->forth, opt.test) != 0;
        return failed ? 1 : sTestStatus;
    }
    if (vr_active()) {
        voice_init();   /* after tests: they stay headless and do not open the microphone */
    }

    last = SDL_GetTicksNS();
    while (!forth_bye(e->forth)) {
        Uint64 now = SDL_GetTicksNS();
        int ticks = 0;
        int vr_frame = 0;

        if (vr_active()) {
            vr_menu_mode(e->paused || e->console.open);   /* (the triggers page, they do not kick) */
            if (!vr_begin_frame()) {
                break;
            }
            vr_frame = 1;
            voice_tick(vr_talk_held());
            now = SDL_GetTicksNS();
        }

        platform_poll(&e->input);
        if (e->input.quit) {
            if (vr_frame) {
                vr_end_frame();
            }
            break;
        }
        if (e->input.fullscreen_toggle) {
            gRender.fullscreen = !gRender.fullscreen;
        }
        if (e->input.debug_skip_cycle) {
            engine_skip_cycle();   /* (F9: the next draw pass switched off - see engine_skip) */
        }
        if (!opt.hidden && gRender.fullscreen != fullscreen) {   /* (the setting: F11, the menu, the saved one) */
            fullscreen = gRender.fullscreen;
            platform_fullscreen(fullscreen);
        }
        if (vr_active() && !freecam_active() && e->input.pressed[SDL_SCANCODE_PAGEUP]) {
            float h, s;

            e->input.pressed[SDL_SCANCODE_PAGEUP] = false;
            if (e->forth != NULL && vr_calibrate(&h, &s)) {
                forth_printf(e->forth, "vr: height %.2f m, scale %.2f\n", h, s);
            }
        }
        hear_from_her(e);
        if (e->input.pressed[SDL_SCANCODE_GRAVE]) {
            e->console.open = !e->console.open;
            e->input.pressed[SDL_SCANCODE_GRAVE] = false;   /* (taken here, once) */
        }
        if (e->console.open != typing) {   /* (opened or shut by ` or by a script: console!) */
            typing = e->console.open;
            platform_text_input(typing);
        }
        if (e->console.open) {
            console_update(&e->console, &e->input, e->forth);
            /* the game sees no keys while typing */
            e->input.keys = kNoKeys;
            memset(e->input.pressed, 0, sizeof(e->input.pressed));
            e->input.mouse_dx = e->input.mouse_dy = 0;
        }

        if (vr_active()) {   /* scripts lay the menus out for the panel, not for one eye */
            e->width = UI_W;
            e->height = UI_H;
        }
        /* fixed ticks: as many as real time asks for (at least one in --frames runs) */
        lag += now - last;
        last = now;
        if (opt.frames > 0 && lag < tick_ns) {
            lag = tick_ns;
        }
        while (lag >= tick_ns && ticks < 4) {
#if VR_STEP_LOG
            fprintf(stderr, "TICK %ld\n", e->ticks);   /* (temporary: before the tick, so the number of the tick about to run) */
#endif
            fiona_pose_before(e);   /* (her place, for the headset to draw her between the two ticks) */
            engine_tick(e);
            fiona_pose_after(e);
            comfort_measure(e);   /* (the vignette's want: one tick's movement, not a frame's) */
            memset(e->input.pressed, 0, sizeof(e->input.pressed));   /* presses count once */
            vr_pad_clear();
            e->input.mouse_dx = e->input.mouse_dy = 0;
            lag -= tick_ns;
            ticks++;
        }
        if (ticks == 4) {
            lag = 0;   /* far behind (a stall): don't try to catch up */
        }
        {   /* how far past the last tick this frame's picture falls: 0 on it, 1 at the next
             * (read only: lag and tick_ns go on as they were) */
            float a = tick_ns > 0 ? (float)lag / (float)tick_ns : 0.0f;

            sPose.alpha = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
        }
        if (bind_engine_reset_wanted()) {   /* (between frames: no handler running) */
            if (vr_frame) {
                vr_end_frame();
            }
            soft_reset(e, opt.hidden && !opt.title);
            continue;
        }

        if (!vr_frame && ticks == 0 && opt.frames == 0) {   /* (the window's picture changes only with a tick: 30 a second) */
            SDL_DelayNS(tick_ns - lag > 2000000ull ? tick_ns - lag - 1000000ull : 0);
            continue;
        }
        if (!vr_frame || vr_should_render()) {   /* a headset redraws every refresh, so the head is not stuck at 30 */
            draw(e);
        }
        if (vr_frame) {
            vr_end_frame();
        }
        if (opt.frames > 0 && ++frame >= opt.frames) {
            break;
        }
    }

    for (i = 0; i < MAX_ACTORS; i++) {
        if (e->actors[i].used) {
            actor_free(&e->actors[i]);
        }
    }
    room_free(&e->room);
    forth_free(e->forth);
    voice_shutdown();
    vr_close();
    render_shutdown();
    platform_close();
    return 0;
}

