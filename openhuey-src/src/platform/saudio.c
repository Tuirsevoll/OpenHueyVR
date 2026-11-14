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
#define SAUDIO_HEADROOM 0.8f     /* the HRTF's own gain peaks about 1.37x for a full-scale sound
                                  * (the self-test measures it): this leaves a little room in the
                                  * mix, and keeps 3-D loudness within 1 dB of the original pans */
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
    /* The effect processes exactly frameSize samples a call and nothing less
     * (OverlapAddConvolutionEffect::apply ignores numSamples: it reads and writes mFrameSize).
     * So the voice's samples are gathered until a whole frame is there, and the effect's output
     * waits in a FIFO: 192 samples of latency, constant, and no sample ever lost or stitched. */
    float in_fifo[SAUDIO_FRAME];
    int in_n;
    float out_fifo[2][SAUDIO_FRAME * 2];
    int out_n;
} Source;

static struct {
    SDL_Mutex *lock;
    int tried, ready;
    int enabled;                 /* the option (and not a cutscene's bypass) */
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
    /* the smoothing, as per-sample factors (fixed once, from the constants above) */
    float k_gain, k_occl_in, k_occl_out, k_pos;
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

/* ---- the self-test: a pure 440 Hz sine through one spatial voice ----
 * HGA_AUDIO3D_SELFTEST=<file.wav> writes six seconds of it, driven exactly as spu.c drives the
 * HRTF - the game's call pattern, because an SDL buffer of 1024 frames is five 192-sample slices
 * and one of 64. A buzz is a step between two samples at such a call's edge far larger than the
 * tone's own slope; the largest step inside a call and at its edges are printed, and the stream
 * is saved to listen to. (Runs once, at start-up, before the game makes a sound.) */
static void le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void selftest(const char *path) {
    static const int pattern[6] = {192, 192, 192, 192, 192, 64};   /* (1024 frames a callback) */
    const long frames = (long)SOUND_RATE * 6;
    float mono[SAUDIO_FRAME], out[SAUDIO_FRAME * 2];
    uint8_t head[44];
    double phase = 0.0, step = 6.28318530718 * 440.0 / (double)SOUND_RATE;
    double peak = 0.0, max_in = 0.0, max_edge = 0.0, rms = 0.0;
    long n = 0, stat_from = 0, edges = 0;
    float prev = 0.0f;
    int have_prev = 0, k, i;
    FILE *f;

    if (path == NULL || path[0] == 0 || (f = fopen(path, "wb")) == NULL) {
        fprintf(stderr, "audio3d: selftest: can't write %s\n", path);
        return;
    }
    memset(head, 0, sizeof(head));
    memcpy(head, "RIFF", 4);
    le32(head + 4, (uint32_t)(36 + frames * 4));
    memcpy(head + 8, "WAVEfmt ", 8);
    le32(head + 16, 16);
    head[20] = 1;
    head[22] = 2;
    le32(head + 24, SOUND_RATE);
    le32(head + 28, (uint32_t)SOUND_RATE * 4u);
    head[32] = 4;
    head[34] = 16;
    memcpy(head + 36, "data", 4);
    le32(head + 40, (uint32_t)(frames * 4));
    fwrite(head, 1, sizeof(head), f);

    saudio_set_enabled(1);   /* (the tone itself: its source is in the open) */
    {
        Mat4 view = mat4_identity();

        saudio_listener(&view);
    }
    {
        float at[3] = {120.0f, 20.0f, -40.0f};   /* ~8 m right, a little up and ahead */

        saudio_source(0, 1, at, 1.0f, 1u);
    }
    while (n < frames) {
        for (k = 0; k < 6 && n < frames; k++) {
            int len = pattern[k];

            if (n + len > frames) {
                len = (int)(frames - n);
            }
            for (i = 0; i < len; i++, n++) {
                mono[i] = (float)(0.25 * sin(phase));
                phase += step;
            }
            memset(out, 0, sizeof(float) * 2 * (size_t)len);
            saudio_block(len);
            {
                float w = saudio_voice_weight(0);

                if (w > 0.0f) {
                    saudio_voice_render(0, mono, len, 1.0f, out, w);
                }
            }
            for (i = 0; i < len; i++) {
                float l = out[2 * i], r = out[2 * i + 1];
                int16_t pcm[2];
                double d;

                if (n - len + i < stat_from) {   /* (the effect's first blocks: silence) */
                    prev = l;
                    have_prev = 1;
                    continue;
                }
                if (have_prev) {
                    d = fabs((double)l - (double)prev);
                    if (i == 0) {
                        if (d > max_edge) {
                            max_edge = d;
                        }
                        edges++;
                    } else if (d > max_in) {
                        max_in = d;
                    }
                }
                prev = l;
                have_prev = 1;
                if (fabs((double)l) > peak) {
                    peak = fabs((double)l);
                }
                if (fabs((double)r) > peak) {
                    peak = fabs((double)r);
                }
                rms += (double)l * l + (double)r * r;
                pcm[0] = (int16_t)(l > 1.0f ? 32767 : l < -1.0f ? -32768 : (int)(l * 32767.0f));
                pcm[1] = (int16_t)(r > 1.0f ? 32767 : r < -1.0f ? -32768 : (int)(r * 32767.0f));
                fwrite(pcm, 2, 2, f);
            }
        }
    }
    fclose(f);
    rms = sqrt(rms / (double)(2 * (frames - stat_from)));
    fprintf(stderr, "audio3d: selftest %s: peak %.3f, rms %.3f, step inside a call %.4f, "
                    "at a call's edge %.4f (over %ld edges)\n", path, peak, rms, max_in, max_edge, edges);
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
    hs.normType = IPL_HRTFNORMTYPE_NONE;   /* (the HRTF's own level by direction: its ILD) */
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
    /* the smoothing as a factor a sample (or a block, for the position - the HRTF takes one
     * direction a block, so nothing finer can matter to it) */
    S.k_gain = 1.0f - expf(-1.0f / (SAUDIO_GAIN_TAU * (float)SOUND_RATE));
    S.k_occl_in = 1.0f - expf(-1.0f / (SAUDIO_OCCL_IN * (float)SOUND_RATE));
    S.k_occl_out = 1.0f - expf(-1.0f / (SAUDIO_OCCL_OUT * (float)SOUND_RATE));
    S.k_pos = 1.0f - expf(-(float)SAUDIO_FRAME / (SAUDIO_POS_TAU * (float)SOUND_RATE));
    SDL_LockMutex(S.lock);   /* (the audio thread reads ready under it) */
    S.ready = 1;
    SDL_UnlockMutex(S.lock);
    fprintf(stderr, "audio: Steam Audio HRTF on, up to %d voices (%d Hz)\n", SAUDIO_MAX_BINAURAL, SOUND_RATE);
    {
        const char *self = SDL_getenv("HGA_AUDIO3D_SELFTEST");

        if (self != NULL && self[0] != 0) {   /* (HGA_AUDIO3D_SELFTEST=<file.wav>: a tone, once) */
            selftest(self);
            saudio_source_off(0);
            saudio_set_enabled(0);
        }
    }
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
void saudio_set_enabled(int on) {
    if (on && !S.ready) {
        saudio_start();   /* (the main thread, once: the HRTF is slow to make) */
    }
    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    S.enabled = on != 0;
    S.mix_target = S.enabled && S.ready ? 1.0f : 0.0f;
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
        s->lp = 0.0f;
        s->in_n = 0;
        s->out_n = SAUDIO_FRAME;   /* (the latency: a frame of silence, then the effect's output) */
        memset(s->out_fifo, 0, sizeof(s->out_fifo));
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
        /* the low-pass's coefficient for a sample, from the occlusion as it is now: the occlusion
         * itself is smoothed a sample at a time as the samples go by (saudio_voice_render), and
         * the filter's state is continuous, so nothing here steps. */
        fc = SAUDIO_LP_CLEAR + (SAUDIO_LP_BLOCKED - SAUDIO_LP_CLEAR) * s->occl;
        s->lp_coef = 1.0f - expf(-6.2831853f * fc / (float)SOUND_RATE);
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

/* one whole frame of a source through its effect: exactly SAUDIO_FRAME samples (the effect
 * processes that many and no fewer), its direction from the point it has glided to, and the two
 * ears' result waiting in the voice's output FIFO. Called with the lock held. */
static void apply_block(int voice) {
    Source *s = &S.src[voice];
    IPLBinauralEffectParams p;
    Vec3 rel;
    float dist;
    int i;
    Uint64 t0 = 0;

    /* (the point glides a block at a time: the HRTF takes one direction a block, so nothing
     * finer than that could reach it) */
    s->pos.x += (s->target.x - s->pos.x) * S.k_pos;
    s->pos.y += (s->target.y - s->pos.y) * S.k_pos;
    s->pos.z += (s->target.z - s->pos.z) * S.k_pos;
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
    for (i = 0; i < SAUDIO_FRAME; i++) {
        S.in.data[0][i] = s->in_fifo[i];
    }
    S.in.numSamples = SAUDIO_FRAME;
    S.out.numSamples = SAUDIO_FRAME;
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
        S.stat_frames += SAUDIO_FRAME;
        if (S.stat_frames >= (long)SOUND_RATE * 5) {
            stats_print();
        }
    }
    for (i = 0; i < SAUDIO_FRAME; i++) {
        s->out_fifo[0][s->out_n + i] = S.out.data[0][i] * SAUDIO_HEADROOM;
        s->out_fifo[1][s->out_n + i] = S.out.data[1][i] * SAUDIO_HEADROOM;
    }
    s->out_n += SAUDIO_FRAME;
}

void saudio_voice_render(int voice, const float *mono, int frames, float gain, float *out, float weight) {
    Source *s;
    int i;

    if (voice < 0 || voice >= SAUDIO_VOICES || mono == NULL || out == NULL || frames <= 0 ||
        frames > SAUDIO_FRAME || weight <= 0.0f) {
        return;
    }
    if (S.lock == NULL) {
        return;
    }
    SDL_LockMutex(S.lock);
    s = &S.src[voice];
    if (!S.ready || s->weight <= 0.0f) {   /* (off since the caller asked: nothing to add) */
        SDL_UnlockMutex(S.lock);
        return;
    }
    s->gain_target = gain;
    for (i = 0; i < frames; i++) {
        float x, og;

        /* a sample at a time: the gain and the occlusion glide (no step at a block's edge to
         * buzz at), and the low-pass follows its input continuously */
        s->gain += (s->gain_target - s->gain) * S.k_gain;
        s->occl += (s->occl_target - s->occl) *
                   (s->occl_target > s->occl ? S.k_occl_in : S.k_occl_out);
        og = 1.0f - s->occl * (1.0f - SAUDIO_OCCL_GAIN);
        x = mono[i] * s->gain * og;
        if (s->occl > 0.001f) {
            s->lp += (x - s->lp) * s->lp_coef;
            x = s->lp;
        } else {
            s->lp = x;   /* (following it: no jump when a wall comes between again) */
        }
        s->in_fifo[s->in_n++] = x;
        if (s->in_n >= SAUDIO_FRAME) {   /* (a whole frame: the effect's own size, no less) */
            s->in_n = 0;
            apply_block(voice);
        }
    }
    /* what the effect made, a frame behind what went in (out_n - in_n is always the frame, so
     * there are always this call's samples waiting) */
    for (i = 0; i < frames; i++) {
        out[2 * i] += s->out_fifo[0][i] * weight;
        out[2 * i + 1] += s->out_fifo[1][i] * weight;
    }
    if (s->out_n > frames) {
        memmove(&s->out_fifo[0][0], &s->out_fifo[0][frames], sizeof(float) * (size_t)(s->out_n - frames));
        memmove(&s->out_fifo[1][0], &s->out_fifo[1][frames], sizeof(float) * (size_t)(s->out_n - frames));
        s->out_n -= frames;
    } else {
        s->out_n = 0;
        s->in_n = 0;
    }
    SDL_UnlockMutex(S.lock);
}

#else   /* no Steam Audio SDK: the game as it was */
int saudio_available(void) { return 0; }
int saudio_start(void) { return 0; }
void saudio_stop(void) {}
void saudio_set_enabled(int on) { (void)on; }
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
void saudio_voice_render(int voice, const float *mono, int frames, float gain, float *out, float weight) {
    (void)voice;
    (void)mono;
    (void)frames;
    (void)gain;
    (void)out;
    (void)weight;
}
#endif
