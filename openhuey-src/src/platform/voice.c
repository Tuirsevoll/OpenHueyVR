/* Hewie's voice, in process: a capture thread, then Whisper (tiny or base) on its own thread.
 * No console window and no whisper.exe. Push-to-talk is the right stick click. The open mic
 * is off until asked. A missing model leaves the commands quiet. */
#include "voice.h"

#include "disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL3/SDL.h>

#define RATE 16000
#define PRE_N (RATE / 4)
#define MAX_N (RATE * 5)
#define MIN_N (RATE * 3 / 10)

static const char *kPrompt = "come here. go. stay. sit. good boy. bad dog. shake.";

static const char *kNames[] = {
    "models/ggml-tiny.en.bin", "models/ggml-tiny.bin", "models/ggml-base.en.bin", "models/ggml-base.bin",
    "ggml-tiny.en.bin", "ggml-tiny.bin", "ggml-base.en.bin", "ggml-base.bin"
};

#if !defined(_WIN32)

void voice_init(void) {}
void voice_shutdown(void) {}
void voice_tick(int talk_held) { (void)talk_held; }
int voice_take(void) { return VOICE_NONE; }
int voice_gen(void) { return 0; }
void voice_copy(char *dst, int cap) { if (dst != NULL && cap > 0) dst[0] = 0; }
void voice_set_debug(int on) { (void)on; }
int voice_debug(void) { return 0; }
void voice_set_open_mic(int on) { (void)on; }
int voice_open_mic(void) { return 0; }

#else

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

/* The current Windows SDK declares these and does not put them in uuid.lib. */
const IID IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const CLSID CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID IID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const IID IID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48a0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

#include "whisper.h"

static CRITICAL_SECTION sMu;
static HANDLE sCapture;
static HANDLE sWorker;
static HANDLE sJobEv;
static volatile LONG sQuit;
static volatile LONG sTalk;
static volatile LONG sOpen;
static int sStarted;
static int sDebug = 1;   /* the transcript box starts on; Home toggles it */

static float sLive[PRE_N];
static int sLiveN, sLiveAt;
static float sClip[MAX_N];
static int sClipN;
static int sRec;
static int sWasTalk;
static int sSpeech;
static int sQuiet;
static double sPos;
static float sPrev;
static int sHavePrev;

static float sJob[MAX_N];
static int sJobN;
static int sJobReady;

static char sText[192];
static int sCmd;
static int sGen;
static char sModel[1024];

static void live_push(float s) {
    sLive[sLiveAt] = s;
    sLiveAt = (sLiveAt + 1) % PRE_N;
    if (sLiveN < PRE_N) {
        sLiveN++;
    }
}

static void clip_add(float s) {
    if (sClipN < MAX_N) {
        sClip[sClipN++] = s;
    }
}

static void preroll(void) {
    int i, n = sLiveN;
    int start = (sLiveAt - n + PRE_N) % PRE_N;

    sClipN = 0;
    for (i = 0; i < n; i++) {
        clip_add(sLive[(start + i) % PRE_N]);
    }
}

static void submit(void) {
    if (sClipN >= MIN_N) {
        EnterCriticalSection(&sMu);
        memcpy(sJob, sClip, (size_t)sClipN * sizeof(float));
        sJobN = sClipN;
        sJobReady = 1;
        LeaveCriticalSection(&sMu);
        SetEvent(sJobEv);
    }
    sClipN = 0;
    sRec = 0;
    sSpeech = 0;
    sQuiet = 0;
}

static void hear(float s) {
    int talk = InterlockedCompareExchange(&sTalk, 0, 0) != 0;
    int open = InterlockedCompareExchange(&sOpen, 0, 0) != 0;

    if (talk && !sWasTalk) {
        preroll();   /* the quarter-second before the click, so the first syllable is kept */
        sRec = 1;
        sSpeech = 0;
        sWasTalk = 1;
    }
    live_push(s);
    if (talk) {
        clip_add(s);
        if (sClipN >= MAX_N) {
            sWasTalk = 0;
            submit();
        }
        return;
    }
    if (sWasTalk) {
        sWasTalk = 0;
        submit();
        return;
    }
    if (!open || !sSpeech) {
        return;
    }
    clip_add(s);
    if (sClipN >= MAX_N) {
        submit();
    }
}

static void energy(const float *mono, int n, int rate) {
    int open = InterlockedCompareExchange(&sOpen, 0, 0) != 0;
    int talk = InterlockedCompareExchange(&sTalk, 0, 0) != 0;
    double a = 0.0;
    int i;
    float rms;

    if (!open || talk || n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        a += (double)mono[i] * (double)mono[i];
    }
    rms = (float)sqrt(a / (double)n);
    if (rms >= 0.02f) {
        if (!sSpeech) {
            preroll();
            sSpeech = 1;
            sRec = 1;
        }
        sQuiet = 0;
    } else if (sSpeech) {
        sQuiet += n * RATE / (rate > 0 ? rate : RATE);
        if (sQuiet >= RATE * 45 / 100) {
            submit();
        }
    }
}

static void feed_rate(const float *mono, int n, int rate) {
    int i;

    if (n <= 0) {
        return;
    }
    energy(mono, n, rate);
    if (rate == RATE) {
        for (i = 0; i < n; i++) {
            hear(mono[i]);
        }
        return;
    }
    {
        double step = (double)rate / (double)RATE;
        double pos = sPos;

        while (pos < (double)n) {
            int i0 = (int)pos;
            float f = (float)(pos - (double)i0);
            float a = (i0 == 0 && sHavePrev) ? sPrev : mono[i0 > 0 ? i0 - 1 : 0];
            float b = mono[i0 < n ? i0 : n - 1];

            hear(a + (b - a) * f);
            pos += step;
        }
        sPrev = mono[n - 1];
        sHavePrev = 1;
        sPos = pos - (double)n;
    }
}

static void feed_packet(const BYTE *data, UINT32 frames, int channels, int rate, int bits, int is_float, int silent) {
    float stack[2048];
    float *mono;
    UINT32 i, c;
    int heap = 0;

    if (frames == 0 || channels <= 0) {
        return;
    }
    if (frames > 2048) {
        mono = (float *)malloc((size_t)frames * sizeof(float));
        heap = 1;
        if (mono == NULL) {
            return;
        }
    } else {
        mono = stack;
    }
    if (silent || data == NULL) {
        memset(mono, 0, (size_t)frames * sizeof(float));
    } else if (is_float && bits == 32) {
        const float *src = (const float *)data;

        for (i = 0; i < frames; i++) {
            float a = 0.0f;

            for (c = 0; c < (UINT32)channels; c++) {
                a += src[i * (UINT32)channels + c];
            }
            mono[i] = a / (float)channels;
        }
    } else if (!is_float && bits == 16) {
        const short *src = (const short *)data;

        for (i = 0; i < frames; i++) {
            float a = 0.0f;

            for (c = 0; c < (UINT32)channels; c++) {
                a += (float)src[i * (UINT32)channels + c] / 32768.0f;
            }
            mono[i] = a / (float)channels;
        }
    } else {
        memset(mono, 0, (size_t)frames * sizeof(float));
    }
    feed_rate(mono, (int)frames, rate);
    if (heap) {
        free(mono);
    }
}

static int format_of(const WAVEFORMATEX *fmt, int *rate, int *channels, int *bits, int *is_float) {
    *rate = (int)fmt->nSamplesPerSec;
    *channels = (int)fmt->nChannels;
    *bits = (int)fmt->wBitsPerSample;
    *is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)fmt;

        *is_float = IsEqualGUID(&ex->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        if (IsEqualGUID(&ex->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)) {
            *is_float = 0;
        }
    }
    return *rate > 0 && *channels > 0 && (*is_float || *bits == 16);
}

static DWORD WINAPI capture_main(LPVOID unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (!sQuit) {
        IMMDeviceEnumerator *en = NULL;
        IMMDevice *dev = NULL;
        IAudioClient *client = NULL;
        IAudioCaptureClient *cap = NULL;
        WAVEFORMATEX *fmt = NULL;
        int rate = 0, channels = 0, bits = 0, is_float = 0;
        HRESULT hr;

        hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
        if (FAILED(hr)) {
            Sleep(1000);
            continue;
        }
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eCapture, eConsole, &dev);
        if (FAILED(hr)) {
            IMMDeviceEnumerator_Release(en);
            fprintf(stderr, "voice: no microphone\n");
            Sleep(2000);
            continue;
        }
        hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
        if (FAILED(hr)) {
            IMMDevice_Release(dev);
            IMMDeviceEnumerator_Release(en);
            Sleep(1000);
            continue;
        }
        hr = IAudioClient_GetMixFormat(client, &fmt);
        if (FAILED(hr) || !format_of(fmt, &rate, &channels, &bits, &is_float)) {
            if (fmt != NULL) {
                CoTaskMemFree(fmt);
            }
            IAudioClient_Release(client);
            IMMDevice_Release(dev);
            IMMDeviceEnumerator_Release(en);
            fprintf(stderr, "voice: microphone format is not PCM\n");
            Sleep(2000);
            continue;
        }
        hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, fmt, NULL);
        CoTaskMemFree(fmt);
        if (FAILED(hr)) {
            IAudioClient_Release(client);
            IMMDevice_Release(dev);
            IMMDeviceEnumerator_Release(en);
            Sleep(1000);
            continue;
        }
        hr = IAudioClient_GetService(client, &IID_IAudioCaptureClient, (void **)&cap);
        if (FAILED(hr) || FAILED(IAudioClient_Start(client))) {
            if (cap != NULL) {
                IAudioCaptureClient_Release(cap);
            }
            IAudioClient_Release(client);
            IMMDevice_Release(dev);
            IMMDeviceEnumerator_Release(en);
            Sleep(1000);
            continue;
        }
        fprintf(stderr, "voice: microphone %d Hz, %d ch\n", rate, channels);
        sPos = 0.0;
        sHavePrev = 0;
        while (!sQuit) {
            UINT32 packet = 0;

            hr = IAudioCaptureClient_GetNextPacketSize(cap, &packet);
            if (FAILED(hr)) {
                break;
            }
            if (packet == 0) {
                int talk = InterlockedCompareExchange(&sTalk, 0, 0) != 0;

                if (!talk && sWasTalk) {
                    sWasTalk = 0;
                    submit();
                }
                Sleep(8);
                continue;
            }
            while (packet > 0 && !sQuit) {
                BYTE *data = NULL;
                UINT32 frames = 0;
                DWORD flags = 0;

                hr = IAudioCaptureClient_GetBuffer(cap, &data, &frames, &flags, NULL, NULL);
                if (FAILED(hr)) {
                    break;
                }
                feed_packet(data, frames, channels, rate, bits, is_float, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                IAudioCaptureClient_ReleaseBuffer(cap, frames);
                hr = IAudioCaptureClient_GetNextPacketSize(cap, &packet);
                if (FAILED(hr)) {
                    packet = 0;
                }
            }
        }
        IAudioClient_Stop(client);
        IAudioCaptureClient_Release(cap);
        IAudioClient_Release(client);
        IMMDevice_Release(dev);
        IMMDeviceEnumerator_Release(en);
    }
    CoUninitialize();
    return 0;
}

static int whole(const char *text, const char *phrase) {
    size_t n = strlen(phrase);
    const char *p = text;

    while ((p = strstr(p, phrase)) != NULL) {
        int start = (p == text) || p[-1] == ' ';
        int end = p[n] == 0 || p[n] == ' ';

        if (start && end) {
            return 1;
        }
        p += n;
    }
    return 0;
}

static int phrase_kind(const char *text) {
    static const struct { const char *phrase; int kind; } table[] = {
        {"come here", VOICE_COME}, {"here boy", VOICE_COME}, {"come boy", VOICE_COME},
        {"come hewie", VOICE_COME}, {"come on", VOICE_COME}, {"come", VOICE_COME},
        {"go ahead", VOICE_GO}, {"go on", VOICE_GO}, {"go there", VOICE_GO}, {"forward", VOICE_GO}, {"go", VOICE_GO},
        {"stay there", VOICE_STAY}, {"stay", VOICE_STAY}, {"wait", VOICE_STAY}, {"heel", VOICE_STAY},
        {"sit down", VOICE_SIT}, {"sit", VOICE_SIT},
        {"good boy", VOICE_PRAISE}, {"good dog", VOICE_PRAISE}, {"good hewie", VOICE_PRAISE},
        {"good hew", VOICE_PRAISE}, {"who's a good", VOICE_PRAISE}, {"whos a good", VOICE_PRAISE},
        {"attaboy", VOICE_PRAISE}, {"well done", VOICE_PRAISE}, {"praise", VOICE_PRAISE},
        {"bad dog", VOICE_SCOLD}, {"bad hewie", VOICE_SCOLD}, {"bad hew", VOICE_SCOLD},
        {"bad boy", VOICE_SCOLD}, {"bad", VOICE_SCOLD}, {"scold", VOICE_SCOLD}, {"shame", VOICE_SCOLD},
        {"give paw", VOICE_SHAKE}, {"shake paw", VOICE_SHAKE}, {"shake", VOICE_SHAKE}, {"paw", VOICE_SHAKE}
    };
    int i;

    if (strcmp(text, "here") == 0) {
        return VOICE_COME;
    }
    if (strcmp(text, "good") == 0) {
        return VOICE_PRAISE;
    }
    if (strcmp(text, "no") == 0) {
        return VOICE_SCOLD;
    }
    for (i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (whole(text, table[i].phrase)) {
            return table[i].kind;
        }
    }
    return VOICE_NONE;
}

static void normalize(char *dst, int cap, const char *src) {
    int j = 0;
    int sp = 1;

    if (cap <= 0) {
        return;
    }
    for (; src != NULL && *src && j < cap - 1; src++) {
        unsigned char c = (unsigned char)*src;

        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (c >= 'a' && c <= 'z' || c == '\'') {
            if (c != '\'') {
                dst[j++] = (char)c;
            }
            sp = 0;
        } else if (c == ' ' || c == '.' || c == ',' || c == '!' || c == '?' || c == '-') {
            if (!sp && j < cap - 1) {
                dst[j++] = ' ';
                sp = 1;
            }
        }
    }
    while (j > 0 && dst[j - 1] == ' ') {
        j--;
    }
    dst[j] = 0;
}

static void quiet_log(enum ggml_log_level level, const char *text, void *user) {
    (void)level;
    (void)text;
    (void)user;
}

static void store_result(const char *raw) {
    char norm[192];
    int kind;

    normalize(norm, (int)sizeof(norm), raw);
    if (norm[0] == 0) {
        return;
    }
    kind = phrase_kind(norm);
    EnterCriticalSection(&sMu);
    snprintf(sText, sizeof(sText), "%s", norm);
    sCmd = kind;
    sGen++;
    LeaveCriticalSection(&sMu);
    if (kind != VOICE_NONE) {
        fprintf(stderr, "voice: \"%s\"\n", norm);
    }
}

static DWORD WINAPI worker_main(LPVOID unused) {
    struct whisper_context_params cp;
    struct whisper_context *ctx;

    (void)unused;
    whisper_log_set(quiet_log, NULL);
    cp = whisper_context_default_params();
    cp.use_gpu = false;
    ctx = whisper_init_from_file_with_params(sModel, cp);
    if (ctx == NULL) {
        fprintf(stderr, "voice: could not load %s\n", sModel);
        return 0;
    }
    fprintf(stderr, "voice: %s ready (right stick click talks, F8 open mic, Home hides the transcript)\n", sModel);
    while (!sQuit) {
        float local[MAX_N];
        int n = 0;
        struct whisper_full_params p;
        int got;

        if (WaitForSingleObject(sJobEv, 100) == WAIT_FAILED) {
            break;
        }
        if (sQuit) {
            break;
        }
        EnterCriticalSection(&sMu);
        if (sJobReady) {
            n = sJobN;
            memcpy(local, sJob, (size_t)n * sizeof(float));
            sJobReady = 0;
        }
        LeaveCriticalSection(&sMu);
        if (n < MIN_N) {
            continue;
        }
        p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.n_threads = 2;
        p.print_progress = false;
        p.print_realtime = false;
        p.print_timestamps = false;
        p.print_special = false;
        p.translate = false;
        p.no_context = true;
        p.no_timestamps = true;
        p.single_segment = true;
        p.suppress_blank = true;
        p.language = "en";
        p.initial_prompt = kPrompt;
        p.temperature = 0.0f;
        p.temperature_inc = 0.0f;
        p.audio_ctx = 512;
        p.max_tokens = 24;
        got = whisper_full(ctx, p, local, n);
        if (got == 0 && whisper_full_n_segments(ctx) > 0) {
            const char *text = whisper_full_get_segment_text(ctx, 0);

            store_result(text != NULL ? text : "");
        } else if (got == 0) {
            store_result("");
        }
    }
    whisper_free(ctx);
    return 0;
}

static int file_ok(const char *path) {
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int find_model(char *out, int cap) {
    const char *base = SDL_GetBasePath();
    const char *user = disc_user_dir();
    int i;

    if (base == NULL) {
        base = ".\\";
    }
    for (i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); i++) {
        snprintf(out, (size_t)cap, "%s%s", base, kNames[i]);
        if (file_ok(out)) {
            return 1;
        }
        if (user != NULL && user[0] != 0) {
            snprintf(out, (size_t)cap, "%s%s", user, kNames[i]);
            if (file_ok(out)) {
                return 1;
            }
        }
    }
    return 0;
}

void voice_init(void) {
    if (sStarted) {
        return;
    }
    if (!find_model(sModel, (int)sizeof(sModel))) {
        fprintf(stderr, "voice: no model (ggml-tiny.en.bin or ggml-base.en.bin beside the game, or in models/ or user/)\n");
        return;
    }
    InitializeCriticalSection(&sMu);
    sJobEv = CreateEvent(NULL, FALSE, FALSE, NULL);
    sQuit = 0;
    sStarted = 1;
    sCapture = CreateThread(NULL, 0, capture_main, NULL, 0, NULL);
    sWorker = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
}

void voice_shutdown(void) {
    if (!sStarted) {
        return;
    }
    InterlockedExchange(&sQuit, 1);
    if (sJobEv != NULL) {
        SetEvent(sJobEv);
    }
    if (sCapture != NULL) {
        WaitForSingleObject(sCapture, 3000);
        CloseHandle(sCapture);
        sCapture = NULL;
    }
    if (sWorker != NULL) {
        WaitForSingleObject(sWorker, 15000);   /* one tiny pass can still be inside whisper */
        CloseHandle(sWorker);
        sWorker = NULL;
    }
    if (sJobEv != NULL) {
        CloseHandle(sJobEv);
        sJobEv = NULL;
    }
    DeleteCriticalSection(&sMu);
    sStarted = 0;
}

void voice_tick(int talk_held) {
    InterlockedExchange(&sTalk, talk_held ? 1 : 0);
}

int voice_take(void) {
    int id = VOICE_NONE;

    if (!sStarted) {
        return VOICE_NONE;
    }
    EnterCriticalSection(&sMu);
    id = sCmd;
    sCmd = VOICE_NONE;
    LeaveCriticalSection(&sMu);
    return id;
}

int voice_gen(void) {
    int g = 0;

    if (!sStarted) {
        return 0;
    }
    EnterCriticalSection(&sMu);
    g = sGen;
    LeaveCriticalSection(&sMu);
    return g;
}

void voice_copy(char *dst, int cap) {
    if (dst == NULL || cap <= 0) {
        return;
    }
    dst[0] = 0;
    if (!sStarted) {
        return;
    }
    EnterCriticalSection(&sMu);
    snprintf(dst, (size_t)cap, "%s", sText);
    LeaveCriticalSection(&sMu);
}

void voice_set_debug(int on) {
    sDebug = on ? 1 : 0;
    fprintf(stderr, "voice: transcript %s\n", sDebug ? "on" : "off");
}

int voice_debug(void) { return sDebug; }

void voice_set_open_mic(int on) {
    InterlockedExchange(&sOpen, on ? 1 : 0);
    fprintf(stderr, "voice: open mic %s\n", on ? "on" : "off");
}

int voice_open_mic(void) {
    return InterlockedCompareExchange(&sOpen, 0, 0) != 0;
}

#endif
