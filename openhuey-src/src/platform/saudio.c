/* Binaural 3D audio: see saudio.h. Without HGA_STEAMAUDIO (the SDK not installed at configure
 * time) every function here is a no-op and the driver's own left / right placement plays as it
 * always did. */
#include "saudio.h"

#include "sound.h"
#include "spu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#ifdef HGA_STEAMAUDIO

#include "phonon.h"

#define SAUDIO_VOICES SPU_VOICES
#define SAUDIO_MAX_BINAURAL 16   /* how many voices the HRTF is run for at once (the rest keep the
                                  * original placement) */
#define SAUDIO_RAMP 0.10f        /* s: 3D audio on / off, a cutscene's bypass */
#define SAUDIO_POS_TAU 0.045f    /* s: a source glides to its new point (scripts move it at 30 Hz) */
#define SAUDIO_GAIN_TAU 0.030f   /* s: ... and its loudness */
#define SAUDIO_OCCL_IN 0.12f     /* s: a wall between, then gone again */
#define SAUDIO_OCCL_OUT 0.35f
#define SAUDIO_LP_CLEAR 12000.0f /* Hz: unobstructed */
#define SAUDIO_LP_BLOCKED 900.0f /* Hz: through a wall */
#define SAUDIO_OCCL_GAIN 0.55f   /* the muffling as well as the low-pass */
#define SAUDIO_UNITS_PER_METER 12.5f
#define SAUDIO_NEAR (0.75f * SAUDIO_UNITS_PER_METER)   /* at the ears: not placed, as the game had it */

typedef struct Source {
    int on;                      /* a placed sound (the driver's) */
    int spatial;
    uint32_t gen;                /* the driver's key-on count (a new sound snaps, not glides) */
    long seen;                   /* the last block it was playing */
    Vec3 target, pos;            /* world units */
    float gain, gain_target;     /* the placement's amplitude (the left/right pair's energy) */
    float occl, occl_target;
    float lp, lp_coef;
    float weight;                /* this block's binaural share (0: the original left / right) */
} Source;

static struct {
    SDL_Mutex *lock;
    int tried, ready;
    int enabled;                 /* the options (and not a cutscene's bypass) */
    int occlusion;
    float mix, mix_target;       /* the ramp */
    Mat4 ear;                    /* world -> ear space (Steam Audio's axes) */
    Vec3 listener;
    int have_ear;
    long block;
    Source src[SAUDIO_VOICES];
    /* Steam Audio */
    IPLContext ctx;
    IPLHRTF hrtf;
    IPLBinauralEffect effect[SAUDIO_VOICES];
    IPLAudioBuffer in, out;
    /* what it costs (HGA_AUDIO3D_STATS=1: a line to stderr every five seconds of audio) */
    int stats;
    double stat_ticks;
    long stat_applies, stat_frames, stat_voices, stat_blocks;
    double stat_freq;
} S;
static void stats_print(void) {
    double sec = S.stat_freq > 0.0 ? S.stat_ticks / S.stat_freq : 0.0;
    double wall = (double)S.stat_frames / (double)SOUND_RATE;
    int n = (int)(S.stat_blocks > 0 ? (S.stat_voices + S.stat_blocks - 1) / S.stat_blocks : 0);

    if (wall > 0.0) {
        fprintf(stderr, "audio3d: %d voices a block (at most), %ld applies, %.2f ms of CPU a second "
                        "(%.1f%% of a core)\n", n, S.stat_applies, sec * 1000.0, sec / wall * 100.0);
    }
    S.stat_ticks = 0.0;
    S.stat_applies = 0;
    S.stat_frames = 0;
    S.stat_voices = 0;
    S.stat_blocks = 0;
}

static void release_sdk(void) {
    int v;

    if (S.ctx == NULL) {
        S.ready = 0;
        return;
    }
    if (S.in.data != NULL) {
        iplAudioBufferFree(S.ctx, &S.in);
    }
    if (S.out.data != NULL) {
        iplAudioBufferFree(S.ctx, &S.out);
    }
    memset(&S.in, 0, sizeof(S.in));
    memset(&S.out, 0, sizeof(S.out));
    for (v = 0; v < SAUDIO_VOICES; v++) {
        if (S.effect[v] != NULL) {
            iplBinauralEffectRelease(&S.effect[v]);
        }
    }
    if (S.hrtf != NULL) {
        iplHRTFRelease(&S.hrtf);
    }
    iplContextRelease(&S.ctx);
    S.ctx = NULL;
    S.hrtf = NULL;
    S.ready = 0;
}

int saudio_available(void) {
    return 1;
}

int saudio_start(void) {
    IPLContextSettings cs;
    IPLAudioSettings as;
    IPLHRTFSettings hs;
    IPLBinauralEffectSettings bs;
    int v;

    if (S.ready) {
        return 1;
    }
    if (S.tried) {
        return 0;
    }
    S.tried = 1;
    if (S.lock == NULL) {
        S.lock = SDL_CreateMutex();
        if (S.lock == NULL) {
            return 0;
        }
    }
    memset(&cs, 0, sizeof(cs));
    cs.version = STEAMAUDIO_VERSION;
    cs.simdLevel = IPL_SIMDLEVEL_AVX2;   /* a cap: Steam Audio still picks what the CPU has */
    if (iplContextCreate(&cs, &S.ctx) != IPL_STATUS_SUCCESS) {
        fprintf(stderr, "audio: Steam Audio: no context (phonon.dll of another version?)\n");
        S.ctx = NULL;
        return 0;
    }
    as.samplingRate = SOUND_RATE;
    as.frameSize = SAUDIO_FRAME;
    memset(&hs, 0, sizeof(hs));
    hs.type = IPL_HRTFTYPE_DEFAULT;
    hs.volume = 1.0f;
    hs.normType = IPL_HRTFNORMTYPE_NONE;
    if (iplHRTFCreate(S.ctx, &as, &hs, &S.hrtf) != IPL_STATUS_SUCCESS) {
        fprintf(stderr, "audio: Steam Audio: no HRTF\n");
        release_sdk();
        return 0;
    }
    memset(&bs, 0, sizeof(bs));
    bs.hrtf = S.hrtf;
    for (v = 0; v < SAUDIO_VOICES; v++) {
        if (iplBinauralEffectCreate(S.ctx, &as, &bs, &S.effect[v]) != IPL_STATUS_SUCCESS) {
            fprintf(stderr, "audio: Steam Audio: no binaural effect\n");
            release_sdk();
            return 0;
        }
    }
    if (iplAudioBufferAllocate(S.ctx, 1, SAUDIO_FRAME, &S.in) != IPL_STATUS_SUCCESS ||
        iplAudioBufferAllocate(S.ctx, 2, SAUDIO_FRAME, &S.out) != IPL_STATUS_SUCCESS) {
        fprintf(stderr, "audio: Steam Audio: no audio buffers\n");
        release_sdk();
        return 0;
    }
    if (!S.stats) {
        S.stats = SDL_getenv("HGA_AUDIO3D_STATS") != NULL;
        S.stat_freq = (double)SDL_GetPerformanceFrequency();
    }
    SDL_LockMutex(S.lock);   /* (the audio thread reads ready under it) */
    S.ready = 1;
    SDL_UnlockMutex(S.lock);
    fprintf(stderr, "audio: Steam Audio HRTF on, up to %d voices (%d Hz)\n", SAUDIO_MAX_BINAURAL, SOUND_RATE);
    return 1;
}

void saudio_stop(void) {
    if (S.lock != NULL) {
        SDL_LockMutex(S.lock);
    }
    release_sdk();
    S.mix = S.mix_target = 0.0f;
    if (S.lock != NULL) {
        SDL_UnlockMutex(S.lock);
    }
}
void saudio_set_enabled(int on, int occlusion) {
    int v;

    if (on && !S.ready) {
        saudio_start();   /* (the main thread, once: the HRTF is slow to make) */
    }
    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    S.enabled = on != 0;
    S.mix_target = S.enabled && S.ready ? 1.0f : 0.0f;
    S.occlusion = occlusion != 0;
    if (!S.occlusion) {
        for (v = 0; v < SAUDIO_VOICES; v++) {
            S.src[v].occl_target = 0.0f;
        }
    }
    SDL_UnlockMutex(S.lock);
}

int saudio_on(void) {
    return S.ready && S.mix_target > 0.0f;
}

void saudio_listener(const Mat4 *world_to_ear) {
    Vec3 p;

    if (world_to_ear == NULL) {
        return;
    }
    p = mat4_point(world_to_ear, vec3(0.0f, 0.0f, 0.0f));
    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    S.ear = *world_to_ear;
    S.listener = p;
    S.have_ear = 1;
    SDL_UnlockMutex(S.lock);
}

int saudio_listener_at(Vec3 *out) {
    int have;

    if (S.lock == NULL) {
        return 0;
    }
    SDL_LockMutex(S.lock);
    have = S.have_ear;
    if (have && out != NULL) {
        *out = S.listener;
    }
    SDL_UnlockMutex(S.lock);
    return have;
}

void saudio_source(int voice, int spatial, const float world[3], float gain, uint32_t gen) {
    Source *s;

    if (voice < 0 || voice >= SAUDIO_VOICES) {
        return;
    }
    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    s = &S.src[voice];
    if (!spatial || world == NULL) {
        s->on = 0;
        s->spatial = 0;
        s->weight = 0.0f;
        SDL_UnlockMutex(S.lock);
        return;
    }
    s->gain_target = gain < 0.0f ? 0.0f : gain > 4.0f ? 4.0f : gain;
    if (!s->on || s->gen != gen) {   /* a new sound: where it is, not a glide from the last one */
        s->pos = vec3(world[0], world[1], world[2]);
        s->target = s->pos;
        s->gen = gen;
        s->gain = s->gain_target;
        s->occl = s->occl_target;
        if (S.ready && S.effect[voice] != NULL) {
            iplBinauralEffectReset(S.effect[voice]);
        }
    }
    s->on = 1;
    s->spatial = 1;
    s->target = vec3(world[0], world[1], world[2]);
    s->seen = S.block + 1;   /* (played from this block: the 16-voice choice sees it at once) */
    SDL_UnlockMutex(S.lock);
}

void saudio_source_off(int voice) {
    if (voice < 0 || voice >= SAUDIO_VOICES || S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    S.src[voice].on = 0;
    S.src[voice].spatial = 0;
    S.src[voice].weight = 0.0f;
    SDL_UnlockMutex(S.lock);
}

void saudio_occlude(int voice, float blocked) {
    if (voice < 0 || voice >= SAUDIO_VOICES || S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    S.src[voice].occl_target = blocked < 0.0f ? 0.0f : blocked > 1.0f ? 1.0f : blocked;
    SDL_UnlockMutex(S.lock);
}

int saudio_sources(int *voices, float (*world)[3], int max) {
    int v, n = 0;

    if (S.lock == NULL) {
        return 0;
    }
    SDL_LockMutex(S.lock);
    for (v = 0; v < SAUDIO_VOICES && n < max; v++) {
        const Source *s = &S.src[v];

        if (!s->on || !s->spatial || s->seen < S.block - 1) {
            continue;
        }
        voices[n] = v;
        world[n][0] = s->target.x;
        world[n][1] = s->target.y;
        world[n][2] = s->target.z;
        n++;
    }
    SDL_UnlockMutex(S.lock);
    return n;
}
void saudio_block(int frames) {
    float dt = (float)frames / (float)SOUND_RATE;
    float k;
    int v, i;

    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    S.block++;
    k = dt / SAUDIO_RAMP;
    if (k > 1.0f) {
        k = 1.0f;
    }
    S.mix += (S.mix_target - S.mix) * k;
    if (S.mix < 0.0005f) {
        S.mix = 0.0f;
    }
    for (v = 0; v < SAUDIO_VOICES; v++) {
        Source *s = &S.src[v];
        float fc;

        s->weight = 0.0f;
        if (!s->on) {
            continue;
        }
        k = dt / (s->occl_target > s->occl ? SAUDIO_OCCL_IN : SAUDIO_OCCL_OUT);
        if (k > 1.0f) {
            k = 1.0f;
        }
        s->occl += (s->occl_target - s->occl) * k;
        fc = SAUDIO_LP_CLEAR + (SAUDIO_LP_BLOCKED - SAUDIO_LP_CLEAR) * s->occl;
        s->lp_coef = 1.0f - expf(-6.2831853f * fc * dt);
    }
    /* the 16 loudest / nearest of them; the others keep the original placement this block */
    if (S.ready && S.mix > 0.0f && S.have_ear) {
        unsigned long long used = 0;   /* a bit a voice: picked (48 voices) */
        int picked = 0;

        for (i = 0; i < SAUDIO_MAX_BINAURAL; i++) {
            int best = -1;
            float best_score = 0.0f;

            for (v = 0; v < SAUDIO_VOICES; v++) {
                const Source *s = &S.src[v];
                float dx, dy, dz, d, score;

                if (!s->on || (used >> v & 1) || s->seen < S.block - 2) {
                    continue;
                }
                dx = s->pos.x - S.listener.x;
                dy = s->pos.y - S.listener.y;
                dz = s->pos.z - S.listener.z;
                d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d < SAUDIO_NEAR) {   /* (at the ears: heard plainly, as the game had it) */
                    continue;
                }
                score = s->gain * 1000.0f / (100.0f + d);
                if (score > best_score) {
                    best_score = score;
                    best = v;
                }
            }
            if (best < 0) {
                break;
            }
            used |= 1ull << best;
            S.src[best].weight = S.mix;
            picked++;
        }
        S.stat_voices += picked;
        S.stat_blocks++;
    }
    SDL_UnlockMutex(S.lock);
}

float saudio_voice_weight(int voice) {
    float w = 0.0f;

    if (voice < 0 || voice >= SAUDIO_VOICES || S.lock == NULL) {
        return 0.0f;
    }
    SDL_LockMutex(S.lock);
    S.src[voice].seen = S.block;
    if (S.ready && S.src[voice].on) {
        w = S.src[voice].weight;
    }
    SDL_UnlockMutex(S.lock);
    return w;
}

void saudio_voice_render(int voice, const float *mono, int frames, float gain, float *out) {
    Source *s;
    IPLBinauralEffectParams p;
    Vec3 rel;
    float dt, k, dist, og;
    int i;
    Uint64 t0 = 0;

    if (voice < 0 || voice >= SAUDIO_VOICES || mono == NULL || out == NULL || frames <= 0 ||
        frames > SAUDIO_FRAME) {
        return;
    }
    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    s = &S.src[voice];
    if (!S.ready || s->weight <= 0.0f) {
        SDL_UnlockMutex(S.lock);
        return;
    }
    dt = (float)frames / (float)SOUND_RATE;
    s->gain_target = gain;
    k = 1.0f - expf(-dt / SAUDIO_POS_TAU);
    s->pos.x += (s->target.x - s->pos.x) * k;
    s->pos.y += (s->target.y - s->pos.y) * k;
    s->pos.z += (s->target.z - s->pos.z) * k;
    k = 1.0f - expf(-dt / SAUDIO_GAIN_TAU);
    s->gain += (s->gain_target - s->gain) * k;
    /* the ear space is Steam Audio's already: +x right, +y up, +z behind the head */
    rel = mat4_point(&S.ear, s->pos);
    dist = sqrtf(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
    if (dist < 0.001f) {
        p.direction.x = 0.0f;
        p.direction.y = 0.0f;
        p.direction.z = -1.0f;
    } else {
        p.direction.x = rel.x / dist;
        p.direction.y = rel.y / dist;
        p.direction.z = rel.z / dist;
    }
    og = 1.0f - s->occl * (1.0f - SAUDIO_OCCL_GAIN);
    for (i = 0; i < frames; i++) {   /* (a short block at a callback's end: exactly its samples,
                                      * so the filter's history runs on into the next block) */
        float x = mono[i] * s->gain * og;

        if (s->occl > 0.001f) {
            s->lp += (x - s->lp) * s->lp_coef;
            x = s->lp;
        } else {
            s->lp = x;   /* (following it: no jump when a wall comes between again) */
        }
        S.in.data[0][i] = x;
    }
    S.in.numSamples = frames;
    S.out.numSamples = frames;
    p.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
    p.spatialBlend = 1.0f;
    p.hrtf = S.hrtf;
    p.peakDelays = NULL;
    if (S.stats) {
        t0 = SDL_GetPerformanceCounter();
    }
    iplBinauralEffectApply(S.effect[voice], &p, &S.in, &S.out);
    if (S.stats && t0 != 0) {
        S.stat_ticks += (double)(SDL_GetPerformanceCounter() - t0);
        S.stat_applies++;
        S.stat_frames += frames;
        if (S.stat_frames >= (long)SOUND_RATE * 5) {
            stats_print();
        }
    }
    for (i = 0; i < frames; i++) {
        out[2 * i] += S.out.data[0][i] * s->weight;
        out[2 * i + 1] += S.out.data[1][i] * s->weight;
    }
    SDL_UnlockMutex(S.lock);
}

#else   /* no Steam Audio SDK: the game as it was */
int saudio_available(void) { return 0; }
int saudio_start(void) { return 0; }
void saudio_stop(void) {}
void saudio_set_enabled(int on, int occlusion) {
    (void)on;
    (void)occlusion;
}
int saudio_on(void) { return 0; }
void saudio_listener(const Mat4 *world_to_ear) { (void)world_to_ear; }
int saudio_listener_at(Vec3 *out) {
    (void)out;
    return 0;
}
void saudio_source(int voice, int spatial, const float world[3], float gain, uint32_t gen) {
    (void)voice;
    (void)spatial;
    (void)world;
    (void)gain;
    (void)gen;
}
void saudio_source_off(int voice) { (void)voice; }
void saudio_occlude(int voice, float blocked) {
    (void)voice;
    (void)blocked;
}
int saudio_sources(int *voices, float (*world)[3], int max) {
    (void)voices;
    (void)world;
    (void)max;
    return 0;
}
void saudio_block(int frames) { (void)frames; }
float saudio_voice_weight(int voice) {
    (void)voice;
    return 0.0f;
}
void saudio_voice_render(int voice, const float *mono, int frames, float gain, float *out) {
    (void)voice;
    (void)mono;
    (void)frames;
    (void)gain;
    (void)out;
}
#endif
