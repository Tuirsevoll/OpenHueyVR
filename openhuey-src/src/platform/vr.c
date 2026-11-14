/* OpenXR, one OpenGL swapchain per eye. The SDL window keeps the GL context and the keyboard;
 * the picture is the headset's. A headset is required: there is no desktop fallback.
 *
 * Fiona stands about 20 world units tall (docs/game_design.md), about 1.6 m, so a metre of
 * head motion is 12.5 units. OpenXR's Y-up, -Z-forward space matches the game's camera. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif
#include "vr.h"

#include "gl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VR_UNITS_PER_METER 12.5f
#define VR_MAX_EYES 2
#define VR_MAX_IMAGES 16

/* the options as the game was before there was a choice (scripts/vr-options.fs: the player's on
 * top, read back at start-up) */
VrOptions gVrOptions = {
    0,        /* turn_snap: smooth turning */
    45.0f,    /* snap_degrees */
    0,        /* comfort */
    0.5f,     /* comfort_strength: the page's Med, off until it is chosen */
    0,        /* teleport */
    0,        /* teleport_mode: fixed */
    2.5f,     /* teleport_reach: metres */
    0,        /* seated */
    1.6f,     /* seated_height: as standing */
    0.0f,     /* seated_offset */
    0,        /* left_handed */
    0,        /* whisper_open: the right stick holds the microphone */
    0,        /* whisper_debug: subtitles off */
    1,        /* calibrate_start: a first run is asked (the title's step, once) */
    0,        /* cutscene_first_person */
    1.6f,     /* eye_height: her eyes, before anything measures them */
    1.0f,     /* body_scale: a metre of head motion, as it is */
    0,        /* calibrated: nothing kept yet */
    0.13f,    /* crouch_deadzone: a look down does nothing */
    0.25f,    /* crouch_on: the hide flag's drop */
    0.18f,    /* crouch_off: ... and its release */
    0.35f     /* sneak_speed: 35% of a walk */
};

/* seated (vr.seated): how far the view rides above (positive) or below her own eyes, in room units -
 * the camera and the hands' anchor together, so the first-person rig stays one piece */
float vr_seat_lift(void) {
    if (!gVrOptions.seated || gVrOptions.seated_offset == 0.0f) {
        return 0.0f;
    }
    return gVrOptions.seated_offset * VR_UNITS_PER_METER;
}

#if !defined(_WIN32)

int vr_open(void) {
    fprintf(stderr, "vr: OpenXR is built for Windows\n");
    return 0;
}
void vr_close(void) {}
int vr_active(void) { return 0; }
int vr_begin_frame(void) { return 0; }
int vr_should_render(void) { return 0; }
void vr_end_frame(void) {}
int vr_eye_count(void) { return 0; }
int vr_head_view(int eye, Mat4 *view) { (void)eye; (void)view; return 0; }
void vr_comfort_preview(void) {}
int vr_comfort_previewing(void) { return 0; }
int vr_tracking(void) { return 0; }
float vr_crouch_depth(void) { return 0.0f; }
float vr_crouch_fraction(void) { return 0.0f; }
float vr_crouch_anchor(float eye_y) { return eye_y; }
void vr_set_crouch_active(int on) { (void)on; }
void vr_crouch_forget_anchor(void) {}
int vr_body_view(Mat4 *view) { (void)view; return 0; }
int vr_eye_body(int eye, Mat4 *view) { (void)eye; (void)view; return 0; }
float vr_head_yaw(void) { return 0.0f; }
void vr_set_move_yaw(float yaw) { (void)yaw; }
float vr_move_yaw(void) { return 0.0f; }
void vr_set_face_yaw(float yaw) { (void)yaw; }
float vr_face_yaw(void) { return 0.0f; }
void vr_stick(float *x, float *y) {
    if (x != NULL) {
        *x = 0.0f;
    }
    if (y != NULL) {
        *y = 0.0f;
    }
}
int vr_pad_down(int button) { (void)button; return 0; }
int vr_pad_pressed(int button) { (void)button; return 0; }
void vr_pad_clear(void) {}
int vr_run_on(void) { return 0; }
int vr_talk_held(void) { return 0; }
float vr_right_stick(void) { return 0.0f; }
void vr_add_turn(float yaw) { (void)yaw; }
float vr_take_turn(void) { return 0.0f; }
float vr_peek_turn(void) { return 0.0f; }
float vr_snap_take(void) { return 0.0f; }
void vr_request_recenter(void) {}
int vr_take_recenter(void) { return 0; }
void vr_anchor_set(int on, Vec3 eye, float yaw) { (void)on; (void)eye; (void)yaw; }
int vr_anchor_get(Vec3 *eye, float *yaw) { (void)eye; (void)yaw; return 0; }
int vr_controller(int hand, VrController *out) { (void)hand; (void)out; return 0; }
int vr_hands_tracked(void) { return 0; }
void vr_haptic(int hand, float amplitude, float seconds) { (void)hand; (void)amplitude; (void)seconds; }
int vr_head_at(Vec3 *out) { (void)out; return 0; }
int vr_head_local(Vec3 *out) { (void)out; return 0; }
int vr_head_forward(Vec3 *out) { (void)out; return 0; }
void vr_screen_face(void) {}
void vr_screen_loose(int on) { (void)on; }
int vr_calibrate(float *height_m, float *scale) { (void)height_m; (void)scale; return 0; }
void vr_set_height(float height_m) { (void)height_m; }
void vr_set_scale(float scale) { (void)scale; }
void vr_body(float *height_m, float *scale) {
    if (height_m != NULL) {
        *height_m = 1.6f;
    }
    if (scale != NULL) {
        *scale = 1.0f;
    }
}
int vr_eye_begin(int eye, float znear, float zfar, int *w, int *h, Mat4 *proj, Mat4 *view, unsigned *fbo) {
    (void)eye; (void)znear; (void)zfar; (void)w; (void)h; (void)proj; (void)view; (void)fbo;
    return 0;
}
void vr_eye_end(int eye) { (void)eye; }

#else

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <unknwn.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

static HMODULE sLoader;
static XrInstance sInstance;
static XrSession sSession;
static XrSpace sLocal;   /* LOCAL, identity: where a neutral pose is measured */
static XrSpace sSpace;   /* LOCAL posed at the neutral head: views and the submitted layer */
static XrSpace sStage;   /* the floor, when the runtime has one */
static XrSystemId sSystem;
static XrEnvironmentBlendMode sBlend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
static XrFrameState sFrame;
static XrView sViews[VR_MAX_EYES];
static XrCompositionLayerProjectionView sLayerViews[VR_MAX_EYES];
static XrSwapchain sSwap[VR_MAX_EYES];
static uint32_t sImageCount[VR_MAX_EYES];
static GLuint sFbo[VR_MAX_EYES][VR_MAX_IMAGES];
static int sW[VR_MAX_EYES], sH[VR_MAX_EYES];
static int sViewCount;
static int sRunning, sActive, sFrameOn, sRender, sSubmit;
static int sAcquired[VR_MAX_EYES];
static int sQuit;
static int sLogged;   /* the once-only console line: what this session was told to do */
static int sHaveOrigin;
static int sLockPose;   /* a cutscene or story shot: eyes stay on the scripted camera */
static XrViewStateFlags sViewFlags;
static XrPosef sOrigin;   /* identity: sSpace is already the neutral pose */

static PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
static PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties;
static PFN_xrCreateInstance xrCreateInstance;
static PFN_xrDestroyInstance xrDestroyInstance;
static PFN_xrResultToString xrResultToString;
static PFN_xrGetInstanceProperties xrGetInstanceProperties;
static PFN_xrGetSystem xrGetSystem;
static PFN_xrGetSystemProperties xrGetSystemProperties;
static PFN_xrEnumerateEnvironmentBlendModes xrEnumerateEnvironmentBlendModes;
static PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews;
static PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR;
static PFN_xrCreateSession xrCreateSession;
static PFN_xrDestroySession xrDestroySession;
static PFN_xrBeginSession xrBeginSession;
static PFN_xrEndSession xrEndSession;
static PFN_xrPollEvent xrPollEvent;
static PFN_xrWaitFrame xrWaitFrame;
static PFN_xrBeginFrame xrBeginFrame;
static PFN_xrEndFrame xrEndFrame;
static PFN_xrLocateViews xrLocateViews;
static PFN_xrLocateSpace xrLocateSpace;
static PFN_xrCreateActionSet xrCreateActionSet;
static PFN_xrDestroyActionSet xrDestroyActionSet;
static PFN_xrCreateAction xrCreateAction;
static PFN_xrStringToPath xrStringToPath;
static PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings;
static PFN_xrAttachSessionActionSets xrAttachSessionActionSets;
static PFN_xrSyncActions xrSyncActions;
static PFN_xrGetActionStateVector2f xrGetActionStateVector2f;
static PFN_xrGetActionStateBoolean xrGetActionStateBoolean;
static PFN_xrGetActionStateFloat xrGetActionStateFloat;
static PFN_xrCreateActionSpace xrCreateActionSpace;
static PFN_xrApplyHapticFeedback xrApplyHapticFeedback;
static XrActionSet sActions;
static XrAction sMove, sRunClick, sInteract, sMenu, sKick, sTurnStick, sRightClick;
static XrAction sIndexL, sCancel, sPause;
/* the same parts of the other hand, for left-handed play: OpenXR will not re-bind a live session,
 * so both hands' actions are bound once and the options pick which the game reads */
static XrAction sMoveR, sRunClickR, sTurnStickL, sResetClickL;
static XrAction sInteractL, sCancelL, sPauseR, sMenuR;
typedef struct VrHandSlot {
    XrAction pose, squeeze, thumb, haptic;
    XrSpace space;
    int tracked;
    Vec3 at, right, up, forward;
    float grip, index, thumbv;
} VrHandSlot;
static VrHandSlot sHand[2];
static int sAnchorOn;
static Vec3 sAnchorEye;
static float sAnchorYaw;
static int sCrouchOn;             /* the headset crouch is active (story/crouch.fs) */
static int sCrouchHave;            /* sCrouchStandY is her standing eye height, kept */
static float sCrouchStandY;
static float sStickX, sStickY, sRightX;
static float sMoveYaw, sFaceYaw, sTurn;
static int sDown[8], sPressed[8], sRun, sClickWas, sRecenterReq, sAlignRecenter, sTalk;
/* the options' snap turning: the turn stick's pull that snaps (1 at the stick's edge), the pull it
 * must fall back under before another, and the stick's centred state */
#define SNAP_AT 0.70f
#define SNAP_REARM 0.35f
static int sSnapArmed = 1;
static float sSnapTurn;   /* a snap waiting for the tick, radians */
static XrTime sSnapAt;    /* when it was pushed: a tick takes it at once, or it is dropped */
static PFN_xrCreateReferenceSpace xrCreateReferenceSpace;
static PFN_xrDestroySpace xrDestroySpace;
static PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats;
static PFN_xrCreateSwapchain xrCreateSwapchain;
static PFN_xrDestroySwapchain xrDestroySwapchain;
static PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages;
static PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage;
static PFN_xrWaitSwapchainImage xrWaitSwapchainImage;
static PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage;

static int load_proc(XrInstance inst, const char *name, void *dst) {
    PFN_xrVoidFunction fn = NULL;

    if (XR_FAILED(xrGetInstanceProcAddr(inst, name, &fn)) || fn == NULL) {
        fprintf(stderr, "vr: %s is not in this runtime\n", name);
        return 0;
    }
    memcpy(dst, &fn, sizeof(fn));
    return 1;
}

#define LOAD(inst, name) if (!load_proc(inst, #name, &(name))) { vr_close(); return 0; }

static void xr_fail(XrResult r, const char *what) {
    char text[XR_MAX_RESULT_STRING_SIZE];

    text[0] = 0;
    if (sInstance != XR_NULL_HANDLE && xrResultToString != NULL) {
        xrResultToString(sInstance, r, text);
    }
    fprintf(stderr, "vr: %s failed (%d %s)\n", what, (int)r, text);
}

static int xr_ok(XrResult r, const char *what) {
    if (XR_FAILED(r)) {
        xr_fail(r, what);
        return 0;
    }
    return 1;
}

static HMODULE open_loader(void) {
    HMODULE m = LoadLibraryA("openxr_loader.dll");
    char exe[MAX_PATH];
    const char *steam[] = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll",
        "C:\\Program Files\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll",
    };
    int i;

    if (m != NULL) {
        return m;
    }
    if (GetModuleFileNameA(NULL, exe, MAX_PATH) > 0) {
        char *slash = strrchr(exe, '\\');

        if (slash != NULL) {
            snprintf(slash + 1, (size_t)(exe + MAX_PATH - (slash + 1)), "openxr_loader.dll");
            m = LoadLibraryA(exe);
            if (m != NULL) {
                return m;
            }
        }
    }
    for (i = 0; i < 2; i++) {
        m = LoadLibraryA(steam[i]);
        if (m != NULL) {
            return m;
        }
    }
    return NULL;
}

static int current_context(HDC *dc, HGLRC *rc) {
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    typedef HDC (WINAPI *PFN_dc)(void);
    typedef HGLRC (WINAPI *PFN_rc)(void);
    PFN_dc get_dc;
    PFN_rc get_rc;

    if (gl == NULL) {
        fprintf(stderr, "vr: no OpenGL context yet\n");
        return 0;
    }
    get_dc = (PFN_dc)GetProcAddress(gl, "wglGetCurrentDC");
    get_rc = (PFN_rc)GetProcAddress(gl, "wglGetCurrentContext");
    if (get_dc == NULL || get_rc == NULL) {
        return 0;
    }
    *dc = get_dc();
    *rc = get_rc();
    if (*dc == NULL || *rc == NULL) {
        fprintf(stderr, "vr: the GL context is not current\n");
        return 0;
    }
    return 1;
}

static XrQuaternionf quat_mul(XrQuaternionf a, XrQuaternionf b) {
    XrQuaternionf r;

    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return r;
}

static XrVector3f quat_rotate(XrQuaternionf q, XrVector3f v) {
    XrQuaternionf p = {v.x, v.y, v.z, 0.0f};
    XrQuaternionf qi = {-q.x, -q.y, -q.z, q.w};
    XrQuaternionf r = quat_mul(quat_mul(q, p), qi);
    XrVector3f o = {r.x, r.y, r.z};

    return o;
}

/* a lean or a duck, not a walk across the room. The origin is the neutral head. */
static XrVector3f clamp_lean(XrVector3f p) {
    float h = sqrtf(p.x * p.x + p.z * p.z);
    const float reach = 0.75f;

    if (h > reach) {
        p.x *= reach / h;
        p.z *= reach / h;
    }
    if (p.y > 0.50f) {
        p.y = 0.50f;
    }
    if (p.y < -1.20f) {
        p.y = -1.20f;
    }
    return p;
}

/* pose relative to the neutral head (sSpace's origin, so this is usually the pose itself) */
static XrPosef pose_delta(XrPosef pose) {
    XrQuaternionf inv = {-sOrigin.orientation.x, -sOrigin.orientation.y, -sOrigin.orientation.z, sOrigin.orientation.w};
    XrVector3f d = {pose.position.x - sOrigin.position.x, pose.position.y - sOrigin.position.y,
                    pose.position.z - sOrigin.position.z};
    XrVector3f p = quat_rotate(inv, d);
    XrPosef out;

    out.orientation = quat_mul(inv, pose.orientation);
    out.position = p;
    return out;
}

/* inverse of a pose: camera space -> eye space. Position is scaled into world units.
 * The upper 3x3 is R transposed, so the translation is -R^T * p (the rows, not the columns). */
static Mat4 pose_to_view(XrPosef pose) {
    float x, y, z, w;
    float px, py, pz;
    float r00, r01, r02, r10, r11, r12, r20, r21, r22;
    Mat4 v;
    x = pose.orientation.x;
    y = pose.orientation.y;
    z = pose.orientation.z;
    w = pose.orientation.w;
    px = pose.position.x * VR_UNITS_PER_METER;
    py = pose.position.y * VR_UNITS_PER_METER;
    pz = pose.position.z * VR_UNITS_PER_METER;
    r00 = 1.0f - 2.0f * (y * y + z * z);
    r01 = 2.0f * (x * y - w * z);
    r02 = 2.0f * (x * z + w * y);
    r10 = 2.0f * (x * y + w * z);
    r11 = 1.0f - 2.0f * (x * x + z * z);
    r12 = 2.0f * (y * z - w * x);
    r20 = 2.0f * (x * z - w * y);
    r21 = 2.0f * (y * z + w * x);
    r22 = 1.0f - 2.0f * (x * x + y * y);
    v = mat4_identity();

    v.m[0] = r00; v.m[1] = r01; v.m[2] = r02;
    v.m[4] = r10; v.m[5] = r11; v.m[6] = r12;
    v.m[8] = r20; v.m[9] = r21; v.m[10] = r22;
    v.m[12] = -(r00 * px + r10 * py + r20 * pz);
    v.m[13] = -(r01 * px + r11 * py + r21 * pz);
    v.m[14] = -(r02 * px + r12 * py + r22 * pz);
    return v;
}

/* the eye, with the head kept inside the lean. Stereo offset from the head is unchanged. */
static XrPosef head_pose(void);
static XrPosef stable_pose(XrPosef pose) {
    XrPosef head = pose_delta(head_pose());
    XrVector3f held = clamp_lean(head.position);

    pose = pose_delta(pose);
    pose.position.x += held.x - head.position.x;
    pose.position.y += held.y - head.position.y;
    pose.position.z += held.z - head.position.z;
    return pose;
}

/* inverse of the eye's pose in the neutral space */
static Mat4 pose_view(XrPosef pose) {
    return pose_to_view(stable_pose(pose));
}

/* the eyes' midpoint; orientation is the first eye's (they share the head) */
static XrPosef head_pose(void) {
    XrPosef h = sViews[0].pose;
    int i;

    for (i = 1; i < sViewCount; i++) {
        h.position.x += sViews[i].pose.position.x;
        h.position.y += sViews[i].pose.position.y;
        h.position.z += sViews[i].pose.position.z;
    }
    h.position.x /= (float)sViewCount;
    h.position.y /= (float)sViewCount;
    h.position.z /= (float)sViewCount;
    return h;
}

/* Fiona is about 1.6 m. The scale makes a measured player that tall in the world. Both live in
 * the options (vr.eye-height, vr.body-scale), so a calibration kept is a calibration the next run
 * has (scripts/vr-options.fs). */
static int sEyeSet;      /* a calibration, or the first pose, chose the eye height */
static int sEyeManual;   /* the console set it; the first pose must not replace it */

/* a metre of head motion in the world's units: 1 when nothing sensible is set (defined below,
 * after clamp_scale) */
static float body_scale(void);

static int floor_height(float *out) {
    XrPosef head;

    if (sViewCount <= 0) {
        return 0;
    }
    head = head_pose();
    if (sStage != XR_NULL_HANDLE && xrLocateSpace != NULL && sFrame.predictedDisplayTime != 0) {
        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        XrVector3f p;

        if (XR_SUCCEEDED(xrLocateSpace(sSpace, sStage, sFrame.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
            p = quat_rotate(loc.pose.orientation, head.position);
            *out = p.y + loc.pose.position.y;
            return 1;
        }
    }
    *out = head.position.y;
    return 1;
}

static float clamp_scale(float s) {
    return s < 0.4f ? 0.4f : s > 2.0f ? 2.0f : s;
}

/* a metre of head motion in the world's units. Seated (vr.seated) the world is sized to the seated
 * height - the floor's measurement is the standing one and is left alone - so the virtual floor
 * stays at the player's own; otherwise what a calibration measured, or 1 with nothing measured. */
static float body_scale(void) {
    if (gVrOptions.seated) {
        float h = gVrOptions.seated_height;

        return (h >= 0.8f && h <= 2.3f) ? clamp_scale(1.6f / h) : 1.0f;
    }
    return gVrOptions.body_scale > 0.001f ? gVrOptions.body_scale : 1.0f;
}

static XrPosef pose_from_head(XrPosef pose);

/* headset since the neutral pose: its look, and a lean or a duck. A long walk is clamped. */
static XrPosef head_look(void) {
    XrPosef d = pose_delta(head_pose());

    d.position = clamp_lean(d.position);
    return d;
}

int vr_body_view(Mat4 *view) {
    float h;
    static float sScaleWas;

    if (view == NULL || !sHaveOrigin || sViewCount <= 0 || !floor_height(&h)) {
        return 0;
    }
    if (!sEyeSet && !sEyeManual && !gVrOptions.calibrated) {
        gVrOptions.eye_height = h;   /* standing as the session started: her eyes, no crouch yet */
        sEyeSet = 1;
    }
    {
        XrPosef d = head_look();
        float s = body_scale();

        if (sScaleWas != 0.0f && s != sScaleWas) {
            /* the world's size changed under the player (a calibration, or seated play on or off):
             * the same head motion now means another place, so the origin is taken again where they
             * stand - else the camera would jump by their own offset from it. */
            sRecenterReq = 1;
        }
        sScaleWas = s;
        /* the same scale as the eye separation, so a lean and the stereo agree */
        d.position.x /= s;
        d.position.y /= s;
        d.position.z /= s;
        *view = pose_to_view(d);
    }
    return 1;
}

int vr_eye_body(int eye, Mat4 *view) {
    XrPosef local;
    Mat4 head, eye_m;
    float s;

    if (view == NULL || eye < 0 || eye >= sViewCount || !vr_body_view(&head)) {
        return 0;
    }
    local = pose_from_head(sViews[eye].pose);
    s = body_scale();
    /* a taller player (scale below 1) needs the eyes further apart, or the world feels huge up close */
    local.position.x /= s;
    local.position.y /= s;
    local.position.z /= s;
    eye_m = pose_to_view(local);
    *view = mat4_mul(eye_m, head);
    return 1;
}

int vr_calibrate(float *height_m, float *scale) {
    float h;

    if (!floor_height(&h)) {
        return 0;
    }
    gVrOptions.eye_height = h;
    sEyeSet = 1;
    sEyeManual = 1;
    /* a real floor height sizes the world to her. Anything else (a seated origin) keeps scale 1. */
    gVrOptions.body_scale = (h >= 0.8f && h <= 2.3f) ? clamp_scale(1.6f / h) : 1.0f;
    gVrOptions.calibrated = 1;
    /* and the origin is taken again where they stand, so the world's new size applies from here
     * instead of shifting the camera by their own offset from the old one. One action: the height
     * measured, the world sized, the view anchored (VRIK's, as far as this game's rig allows). */
    sRecenterReq = 1;
    if (height_m != NULL) {
        *height_m = gVrOptions.eye_height;
    }
    if (scale != NULL) {
        *scale = gVrOptions.body_scale;
    }
    return 1;
}

void vr_set_height(float height_m) {
    gVrOptions.eye_height = height_m;
    sEyeSet = 1;
    sEyeManual = 1;
}

void vr_set_scale(float scale) {
    gVrOptions.body_scale = clamp_scale(scale);
    sEyeManual = 1;
}

void vr_body(float *height_m, float *scale) {
    if (height_m != NULL) {
        *height_m = gVrOptions.seated ? gVrOptions.seated_height : gVrOptions.eye_height;
    }
    if (scale != NULL) {
        *scale = body_scale();   /* the one in use: seated's, or the calibration's */
    }
}

/* The crouch (scripts/story/crouch.fs). Depth is the headset's own height lost against the
 * standing calibration, measured above the runtime's floor so a recenter does not lose it. */
float vr_crouch_depth(void) {
    float h;

    if (gVrOptions.seated || !(gVrOptions.calibrated || sEyeSet || sEyeManual) || !floor_height(&h)) {
        return 0.0f;
    }
    h = gVrOptions.eye_height - h;
    return h > 0.0f ? h : 0.0f;
}

float vr_crouch_fraction(void) {
    float d = vr_crouch_depth();
    float h = gVrOptions.eye_height;

    if (gVrOptions.seated || h < 0.5f || h > 3.0f) {
        return 0.0f;
    }
    d /= h;
    if (d < 0.0f) {
        d = 0.0f;
    }
    if (d > 1.0f) {
        d = 1.0f;
    }
    return d;
}

/* Her standing eye height, kept while the crouch is active: the body crouches or walks under it,
 * so the camera and the hands' anchor stay at the player's head (vr_body_view adds the headset's
 * own fall on top). A recenter forgets it and takes the current pose as the standing one. */
void vr_set_crouch_active(int on) {
    sCrouchOn = on ? 1 : 0;
}

void vr_crouch_forget_anchor(void) {
    sCrouchHave = 0;
}

float vr_crouch_anchor(float eye_y) {
    if (!sHaveOrigin) {
        return eye_y;
    }
    if (!sCrouchOn) {
        sCrouchStandY = eye_y;
        sCrouchHave = 1;
        return eye_y;
    }
    if (!sCrouchHave) {
        sCrouchStandY = eye_y;
        sCrouchHave = 1;
    }
    if (fabsf(eye_y - sCrouchStandY) > 12.0f) {   /* a teleport or a new room: take her head again */
        sCrouchStandY = eye_y;
    }
    return sCrouchStandY;
}

static float quat_yaw(XrQuaternionf q);

int vr_tracking(void) { return sHaveOrigin; }

float vr_head_yaw(void) {
    if (!sHaveOrigin || sViewCount <= 0) {
        return 0.0f;
    }
    return quat_yaw(pose_delta(head_pose()).orientation);
}

void vr_set_move_yaw(float yaw) { sMoveYaw = yaw; }
float vr_move_yaw(void) { return sMoveYaw; }
void vr_set_face_yaw(float yaw) { sFaceYaw = yaw; }
float vr_face_yaw(void) { return sFaceYaw; }

void vr_stick(float *x, float *y) {
    if (x != NULL) {
        *x = sStickX;
    }
    if (y != NULL) {
        *y = sStickY;
    }
}

static int read_bool(XrAction action) {
    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};

    if (action == XR_NULL_HANDLE || xrGetActionStateBoolean == NULL) {
        return 0;
    }
    get.action = action;
    get.subactionPath = XR_NULL_PATH;
    if (XR_FAILED(xrGetActionStateBoolean(sSession, &get, &state)) || !state.isActive) {
        return 0;
    }
    return state.currentState ? 1 : 0;
}

static void note_button(int button, int down) {
    if (button < 0 || button >= 8) {
        return;
    }
    if (down && !sDown[button]) {
        sPressed[button] = 1;
    }
    sDown[button] = down;
}

static Vec3 quat_dir(XrQuaternionf q, float x, float y, float z) {
    XrVector3f v;

    v.x = x;
    v.y = y;
    v.z = z;
    v = quat_rotate(q, v);
    return vec3(v.x, v.y, v.z);
}

static float read_float(XrAction action) {
    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};

    if (action == XR_NULL_HANDLE || xrGetActionStateFloat == NULL) {
        return 0.0f;
    }
    get.action = action;
    get.subactionPath = XR_NULL_PATH;
    if (XR_FAILED(xrGetActionStateFloat(sSession, &get, &state)) || !state.isActive) {
        return 0.0f;
    }
    return state.currentState;
}

/* grip poses live in the same recentered space as the head, so a hand stays put relative to it */
static void sync_hands(void) {
    int i;
    float s = body_scale();

    for (i = 0; i < 2; i++) {
        XrActionSpaceCreateInfo space = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        VrHandSlot *h = &sHand[i];

        h->tracked = 0;
        h->grip = read_float(h->squeeze);
        h->thumbv = read_bool(h->thumb) ? 1.0f : 0.0f;
        h->index = i == 1 ? read_float(sKick) : read_float(sIndexL);
        if (h->pose == XR_NULL_HANDLE || xrLocateSpace == NULL || !sHaveOrigin || sSpace == XR_NULL_HANDLE) {
            continue;
        }
        if (h->space == XR_NULL_HANDLE && xrCreateActionSpace != NULL) {
            space.action = h->pose;
            space.subactionPath = XR_NULL_PATH;
            space.poseInActionSpace.orientation.w = 1.0f;
            if (XR_FAILED(xrCreateActionSpace(sSession, &space, &h->space))) {
                h->space = XR_NULL_HANDLE;
                continue;
            }
        }
        if (h->space == XR_NULL_HANDLE) {
            continue;
        }
        if (XR_FAILED(xrLocateSpace(h->space, sSpace, sFrame.predictedDisplayTime, &loc))) {
            continue;
        }
        if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
            continue;
        }
        h->at = vec3(loc.pose.position.x * VR_UNITS_PER_METER / s,
                     loc.pose.position.y * VR_UNITS_PER_METER / s,
                     loc.pose.position.z * VR_UNITS_PER_METER / s);
        h->right = quat_dir(loc.pose.orientation, 1.0f, 0.0f, 0.0f);
        h->up = quat_dir(loc.pose.orientation, 0.0f, 1.0f, 0.0f);
        h->forward = quat_dir(loc.pose.orientation, 0.0f, 0.0f, -1.0f);
        h->tracked = 1;
    }
}

static void sync_stick(void) {
    XrActiveActionSet active;
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
    XrActionStateFloat kick = {XR_TYPE_ACTION_STATE_FLOAT};
    /* the hand each part belongs to: the usual one, or its twin for left-handed play. The hands
     * themselves (grips, triggers, the arms) never move: only what the sticks and buttons mean. */
    int leftie = gVrOptions.left_handed;
    XrAction move = leftie ? sMoveR : sMove;
    XrAction runClick = leftie ? sRunClickR : sRunClick;
    XrAction turnStick = leftie ? sTurnStickL : sTurnStick;
    XrAction resetClick = leftie ? sResetClickL : sRightClick;
    XrAction interact = leftie ? sInteractL : sInteract;
    XrAction cancel = leftie ? sCancelL : sCancel;
    XrAction menu = leftie ? sMenuR : sMenu;
    XrAction pause = leftie ? sPauseR : sPause;
    float x, y, len;
    int click;

    sStickX = sStickY = sRightX = 0.0f;
    sTalk = 0;
    if (move == XR_NULL_HANDLE || xrSyncActions == NULL) {
        return;
    }
    active.actionSet = sActions;
    active.subactionPath = XR_NULL_PATH;
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(sSession, &sync))) {
        return;
    }
    get.subactionPath = XR_NULL_PATH;
    if (xrGetActionStateVector2f != NULL) {
        get.action = move;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(sSession, &get, &state)) && state.isActive) {
            x = state.currentState.x;
            y = -state.currentState.y;   /* stick up is forward: the game's stick z grows downward */
            len = sqrtf(x * x + y * y);
            if (len >= 0.2f) {
                if (len > 1.0f) {
                    len = 1.0f;
                }
                sStickX = x / len;
                sStickY = y / len;
            }
        }
    }
    click = read_bool(runClick);
    {
        int right = read_bool(resetClick);

        /* the two clicks are the run's and the talk's; nothing else. Both of them at once is not a
         * gesture of its own: the height calibration is the VR options page's, one way to set it. */
        if (click && !sClickWas) {
            sRun = !sRun;
        }
        sTalk = right && !click;   /* the turn stick's click alone, held: talk */
    }
    sClickWas = click;
    if (turnStick != XR_NULL_HANDLE && xrGetActionStateVector2f != NULL) {
        get.action = turnStick;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(sSession, &get, &state)) && state.isActive) {
            float rx = state.currentState.x;

            if (rx > 0.2f || rx < -0.2f) {
                /* stick right is positive. Her yaw's positive direction is a left turn, so this is negated. */
                sRightX = (rx > 0.0f ? -1.0f : 1.0f) * (fabsf(rx) - 0.2f) / 0.8f;
            }
        }
    }
    /* the options' snap turning: one turn a push of the turn stick, its angle chosen. Read here,
     * every headset frame, so a flick that begins and ends between two ticks is still seen: the
     * turn waits for the tick (vr_snap_take) - the scripts turn her by it. */
    if (gVrOptions.turn_snap) {
        float pull = fabsf(sRightX);
        float deg = gVrOptions.snap_degrees;

        if (deg < 15.0f) {
            deg = 15.0f;
        }
        if (deg > 90.0f) {
            deg = 90.0f;
        }
        if (!sSnapArmed && pull < SNAP_REARM) {
            sSnapArmed = 1;   /* (centred again: the next push snaps) */
        }
        if (sSnapArmed && pull > SNAP_AT) {
            float snap = deg * 3.14159265f / 180.0f;

            /* sRightX is negative for the stick right: her yaw grows to the left */
            sSnapTurn += (sRightX < 0.0f ? -1.0f : 1.0f) * snap;
            /* two pushes at most: pushes with no tick to take them (a script has her) must not add up */
            if (sSnapTurn > 2.0f * snap) {
                sSnapTurn = 2.0f * snap;
            }
            if (sSnapTurn < -2.0f * snap) {
                sSnapTurn = -2.0f * snap;
            }
            sSnapAt = sFrame.predictedDisplayTime;
            sSnapArmed = 0;
        }
    } else {
        sSnapTurn = 0.0f;   /* (turned off with a push waiting: it is not taken) */
        sSnapArmed = 1;
    }
    note_button(0, read_bool(interact));                  /* A (left X): use, and accept in a menu */
    note_button(5, read_bool(interact));
    note_button(3, read_bool(cancel));                    /* B (left Y): back */
    note_button(7, read_bool(menu));                      /* Y (right A): the status menu */
    note_button(6, read_bool(pause));                     /* X (right B): pause */
    if (sKick != XR_NULL_HANDLE && xrGetActionStateFloat != NULL) {
        get.action = sKick;
        if (XR_SUCCEEDED(xrGetActionStateFloat(sSession, &get, &kick)) && kick.isActive) {
            note_button(1, kick.currentState >= 0.6f);    /* right trigger: kick */
        } else {
            note_button(1, 0);
        }
    }
    note_button(4, read_float(sIndexL) >= 0.6f);          /* left trigger: step back */
    sync_hands();
}

int vr_pad_down(int button) {
    if (button < 0 || button >= 8) {
        return 0;
    }
    return sDown[button] ? 1 : 0;
}

int vr_pad_pressed(int button) {
    if (button < 0 || button >= 8) {
        return 0;
    }
    return sPressed[button] ? 1 : 0;
}

void vr_pad_clear(void) {
    int i;

    for (i = 0; i < 8; i++) {
        sPressed[i] = 0;
    }
}

int vr_run_on(void) { return sRun ? 1 : 0; }
int vr_talk_held(void) { return sTalk ? 1 : 0; }

float vr_right_stick(void) { return sRightX; }

void vr_add_turn(float yaw) { sTurn += yaw; }

float vr_take_turn(void) {
    float t = sTurn;

    sTurn = 0.0f;
    return t;
}

/* the turn waiting, still waiting: the picture blends towards it (main.c), taking it or not */
float vr_peek_turn(void) {
    return sTurn;
}

/* a snap the turn stick gave this frame (sync_stick), taken by the tick that turns her */
float vr_snap_take(void) {
    float t = sSnapTurn;

    sSnapTurn = 0.0f;
    /* a menu up, no tick with her: a snap not taken at once is not wanted any more (half a second) */
    if (t != 0.0f && sFrame.predictedDisplayTime != 0 && sSnapAt != 0 &&
        sFrame.predictedDisplayTime - sSnapAt > 500000000) {
        t = 0.0f;
    }
    return t;
}

void vr_request_recenter(void) { sRecenterReq = 1; }

int vr_take_recenter(void) {
    int r = sAlignRecenter;

    sAlignRecenter = 0;
    return r;
}

void vr_anchor_set(int on, Vec3 eye, float yaw) {
    sAnchorOn = on ? 1 : 0;
    sAnchorEye = eye;
    sAnchorYaw = yaw;
}

int vr_anchor_get(Vec3 *eye, float *yaw) {
    if (!sAnchorOn) {
        return 0;
    }
    if (eye != NULL) {
        *eye = sAnchorEye;
    }
    if (yaw != NULL) {
        *yaw = sAnchorYaw;
    }
    return 1;
}

int vr_head_local(Vec3 *out) {
    XrPosef h;
    float s;

    if (out == NULL || sViewCount <= 0) {
        return 0;
    }
    h = head_pose();
    s = body_scale();
    *out = vec3(h.position.x * VR_UNITS_PER_METER / s, h.position.y * VR_UNITS_PER_METER / s,
                h.position.z * VR_UNITS_PER_METER / s);
    return 1;
}

int vr_head_forward(Vec3 *out) {
    XrPosef h;
    Vec3 f;
    float xz;

    if (out == NULL || sViewCount <= 0) {
        return 0;
    }
    h = head_pose();
    f = quat_dir(h.orientation, 0.0f, 0.0f, -1.0f);
    xz = sqrtf(f.x * f.x + f.z * f.z);
    if (xz < 0.25f) {
        *out = vec3(0.0f, 0.0f, -1.0f);
        return 1;
    }
    *out = vec3(f.x / xz, 0.0f, f.z / xz);
    return 1;
}

int vr_head_at(Vec3 *out) {
    Vec3 eye, p;
    float yaw, s, sy, cy;
    XrPosef d;

    if (out == NULL || !vr_anchor_get(&eye, &yaw) || sViewCount <= 0) {
        return 0;
    }
    d = head_look();
    s = body_scale();
    p = vec3(d.position.x * VR_UNITS_PER_METER / s, d.position.y * VR_UNITS_PER_METER / s,
             d.position.z * VR_UNITS_PER_METER / s);
    sy = sinf(yaw);
    cy = cosf(yaw);
    *out = vec3(eye.x + cy * p.x - sy * p.z, eye.y + p.y, eye.z + sy * p.x + cy * p.z);
    return 1;
}

int vr_controller(int hand, VrController *out) {
    const VrHandSlot *h;

    if (out == NULL || hand < 0 || hand > 1) {
        return 0;
    }
    h = &sHand[hand];
    out->tracked = h->tracked;
    out->at = h->at;
    out->right = h->right;
    out->up = h->up;
    out->forward = h->forward;
    out->grip = h->grip;
    out->index = h->index;
    out->thumb = h->thumbv;
    return h->tracked;
}

void vr_haptic(int hand, float amplitude, float seconds) {
    XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};
    XrHapticVibration vib = {XR_TYPE_HAPTIC_VIBRATION};

    if (hand < 0 || hand > 1 || sSession == XR_NULL_HANDLE || xrApplyHapticFeedback == NULL) {
        return;
    }
    if (sHand[hand].haptic == XR_NULL_HANDLE || amplitude <= 0.0f || seconds <= 0.0f) {
        return;
    }
    info.action = sHand[hand].haptic;
    vib.amplitude = amplitude > 1.0f ? 1.0f : amplitude;
    vib.duration = (XrDuration)(seconds * 1000000000.0f);
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(sSession, &info, (const XrHapticBaseHeader *)&vib);
}

int vr_hands_tracked(void) {
    return (sHand[0].tracked || sHand[1].tracked) ? 1 : 0;
}

static int make_action(XrAction *out, XrActionType type, const char *name, const char *local) {
    XrActionCreateInfo act = {XR_TYPE_ACTION_CREATE_INFO};

    act.actionType = type;
    strcpy(act.actionName, name);
    strcpy(act.localizedActionName, local);
    return XR_SUCCEEDED(xrCreateAction(sActions, &act, out));
}

static int bind_path(XrAction action, const char *path, XrActionSuggestedBinding *out) {
    XrPath p;

    if (action == XR_NULL_HANDLE || XR_FAILED(xrStringToPath(sInstance, path, &p))) {
        return 0;
    }
    out->action = action;
    out->binding = p;
    return 1;
}

static void suggest(const char *profile, XrActionSuggestedBinding *binds, uint32_t n) {
    XrPath path;
    XrInteractionProfileSuggestedBinding sug = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};

    if (n == 0 || XR_FAILED(xrStringToPath(sInstance, profile, &path))) {
        return;
    }
    sug.interactionProfile = path;
    sug.countSuggestedBindings = n;
    sug.suggestedBindings = binds;
    xrSuggestInteractionProfileBindings(sInstance, &sug);
}

/* the left stick, and the Quest 3 buttons (Touch and Touch Plus) */
static void bind_stick(void) {
    static const char *const sticks[] = {
        "/interaction_profiles/valve/index_controller",
        "/interaction_profiles/microsoft/motion_controller",
        "/interaction_profiles/hp/mixed_reality_controller",
        "/interaction_profiles/htc/vive_cosmos_controller",
        "/interaction_profiles/htc/vive_focus3_controller",
    };
    XrActionSetCreateInfo set = {XR_TYPE_ACTION_SET_CREATE_INFO};
    XrSessionActionSetsAttachInfo attach = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    XrActionSuggestedBinding one;
    size_t i;

    if (xrCreateActionSet == NULL || xrCreateAction == NULL || xrStringToPath == NULL ||
        xrSuggestInteractionProfileBindings == NULL || xrAttachSessionActionSets == NULL) {
        return;
    }
    strcpy(set.actionSetName, "gameplay");
    strcpy(set.localizedActionSetName, "Gameplay");
    if (XR_FAILED(xrCreateActionSet(sInstance, &set, &sActions))) {
        return;
    }
    if (!make_action(&sMove, XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move")) {
        return;
    }
    make_action(&sRunClick, XR_ACTION_TYPE_BOOLEAN_INPUT, "run", "Run");
    make_action(&sInteract, XR_ACTION_TYPE_BOOLEAN_INPUT, "interact", "Interact");
    make_action(&sCancel, XR_ACTION_TYPE_BOOLEAN_INPUT, "cancel", "Back");
    make_action(&sMenu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu");
    make_action(&sPause, XR_ACTION_TYPE_BOOLEAN_INPUT, "pause", "Pause");
    make_action(&sKick, XR_ACTION_TYPE_FLOAT_INPUT, "kick", "Kick");
    make_action(&sTurnStick, XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn");
    make_action(&sRightClick, XR_ACTION_TYPE_BOOLEAN_INPUT, "reset", "Reset");
    /* the other hand's, for left-handed play: never read at once with the first set */
    make_action(&sMoveR, XR_ACTION_TYPE_VECTOR2F_INPUT, "move_r", "Move (right hand)");
    make_action(&sRunClickR, XR_ACTION_TYPE_BOOLEAN_INPUT, "run_r", "Run (right hand)");
    make_action(&sTurnStickL, XR_ACTION_TYPE_VECTOR2F_INPUT, "turn_l", "Turn (left hand)");
    make_action(&sResetClickL, XR_ACTION_TYPE_BOOLEAN_INPUT, "reset_l", "Reset (left hand)");
    make_action(&sInteractL, XR_ACTION_TYPE_BOOLEAN_INPUT, "interact_l", "Interact (left hand)");
    make_action(&sCancelL, XR_ACTION_TYPE_BOOLEAN_INPUT, "cancel_l", "Back (left hand)");
    make_action(&sPauseR, XR_ACTION_TYPE_BOOLEAN_INPUT, "pause_r", "Pause (right hand)");
    make_action(&sMenuR, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_r", "Menu (right hand)");
    make_action(&sHand[0].pose, XR_ACTION_TYPE_POSE_INPUT, "grip_l", "Left hand");
    make_action(&sHand[1].pose, XR_ACTION_TYPE_POSE_INPUT, "grip_r", "Right hand");
    make_action(&sHand[0].squeeze, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_l", "Left grip");
    make_action(&sHand[1].squeeze, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_r", "Right grip");
    make_action(&sIndexL, XR_ACTION_TYPE_FLOAT_INPUT, "index_l", "Left index");
    make_action(&sHand[0].thumb, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_l", "Left thumb");
    make_action(&sHand[1].thumb, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_r", "Right thumb");
    make_action(&sHand[0].haptic, XR_ACTION_TYPE_VIBRATION_OUTPUT, "buzz_l", "Left buzz");
    make_action(&sHand[1].haptic, XR_ACTION_TYPE_VIBRATION_OUTPUT, "buzz_r", "Right buzz");
    for (i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++) {
        if (bind_path(sMove, "/user/hand/left/input/thumbstick", &one)) {
            suggest(sticks[i], &one, 1);
        }
        /* the two sticks' parts the other way round, for left-handed play */
        if (bind_path(sMoveR, "/user/hand/right/input/thumbstick", &one)) {
            suggest(sticks[i], &one, 1);
        }
        if (bind_path(sRunClickR, "/user/hand/right/input/thumbstick/click", &one)) {
            suggest(sticks[i], &one, 1);
        }
        if (bind_path(sTurnStickL, "/user/hand/left/input/thumbstick", &one)) {
            suggest(sticks[i], &one, 1);
        }
        if (bind_path(sResetClickL, "/user/hand/left/input/thumbstick/click", &one)) {
            suggest(sticks[i], &one, 1);
        }
    }
    if (bind_path(sMove, "/user/hand/left/input/trackpad", &one)) {
        suggest("/interaction_profiles/htc/vive_controller", &one, 1);
    }
    {
        XrActionSuggestedBinding quest[28];
        uint32_t n = 0;
        static const char *const quest_profiles[] = {
            "/interaction_profiles/oculus/touch_controller",
            "/interaction_profiles/meta/touch_controller_plus",
        };

        n += bind_path(sMove, "/user/hand/left/input/thumbstick", &quest[n]);
        n += bind_path(sRunClick, "/user/hand/left/input/thumbstick/click", &quest[n]);
        n += bind_path(sInteract, "/user/hand/right/input/a/click", &quest[n]);
        n += bind_path(sCancel, "/user/hand/right/input/b/click", &quest[n]);
        n += bind_path(sMenu, "/user/hand/left/input/y/click", &quest[n]);
        n += bind_path(sPause, "/user/hand/left/input/x/click", &quest[n]);
        n += bind_path(sKick, "/user/hand/right/input/trigger/value", &quest[n]);
        n += bind_path(sTurnStick, "/user/hand/right/input/thumbstick", &quest[n]);
        n += bind_path(sRightClick, "/user/hand/right/input/thumbstick/click", &quest[n]);
        /* left-handed: the face buttons and sticks of the other hand. On a Touch controller the
         * left hand has X (where the right has A), Y (B), the left stick and its click */
        n += bind_path(sInteractL, "/user/hand/left/input/x/click", &quest[n]);
        n += bind_path(sCancelL, "/user/hand/left/input/y/click", &quest[n]);
        n += bind_path(sPauseR, "/user/hand/right/input/b/click", &quest[n]);
        n += bind_path(sMenuR, "/user/hand/right/input/a/click", &quest[n]);
        n += bind_path(sMoveR, "/user/hand/right/input/thumbstick", &quest[n]);
        n += bind_path(sRunClickR, "/user/hand/right/input/thumbstick/click", &quest[n]);
        n += bind_path(sTurnStickL, "/user/hand/left/input/thumbstick", &quest[n]);
        n += bind_path(sResetClickL, "/user/hand/left/input/thumbstick/click", &quest[n]);
        n += bind_path(sHand[0].pose, "/user/hand/left/input/grip/pose", &quest[n]);
        n += bind_path(sHand[1].pose, "/user/hand/right/input/grip/pose", &quest[n]);
        n += bind_path(sHand[0].squeeze, "/user/hand/left/input/squeeze/value", &quest[n]);
        n += bind_path(sHand[1].squeeze, "/user/hand/right/input/squeeze/value", &quest[n]);
        n += bind_path(sIndexL, "/user/hand/left/input/trigger/value", &quest[n]);
        n += bind_path(sHand[0].thumb, "/user/hand/left/input/thumbrest/touch", &quest[n]);
        n += bind_path(sHand[1].thumb, "/user/hand/right/input/thumbrest/touch", &quest[n]);
        n += bind_path(sHand[0].haptic, "/user/hand/left/output/haptic", &quest[n]);
        n += bind_path(sHand[1].haptic, "/user/hand/right/output/haptic", &quest[n]);
        for (i = 0; i < sizeof(quest_profiles) / sizeof(quest_profiles[0]); i++) {
            suggest(quest_profiles[i], quest, n);
        }
    }
    attach.countActionSets = 1;
    attach.actionSets = &sActions;
    xrAttachSessionActionSets(sSession, &attach);
}

/* this eye relative to the head: stereo, without the head's look */
static XrPosef pose_from_head(XrPosef pose) {
    XrPosef head = head_pose();
    XrQuaternionf inv = {-head.orientation.x, -head.orientation.y, -head.orientation.z, head.orientation.w};
    XrVector3f d = {pose.position.x - head.position.x, pose.position.y - head.position.y,
                    pose.position.z - head.position.z};
    XrPosef out;

    out.orientation = quat_mul(inv, pose.orientation);
    out.position = quat_rotate(inv, d);
    return out;
}

/* OpenGL clip space, the OpenXR field of view (asymmetric, so the eyes converge) */
static Mat4 proj_fov(XrFovf fov, float znear, float zfar) {
    float l = tanf(fov.angleLeft), r = tanf(fov.angleRight);
    float b = tanf(fov.angleDown), t = tanf(fov.angleUp);
    float iw = r - l, ih = t - b;
    Mat4 m = {{0}};

    /* the headset's own field of view, unscaled. A world scale (vr.body-scale, body_scale in
     * vr_body_view) is the *camera's* motion over the real one, with the eye separation scaled the
     * same: a metre of head motion and the stereo both say "the world is s times the size", so the
     * two agree. Turning the focal length the other way as well used to make an object's size and
     * its parallax disagree, which the eye reads as the picture warping as the head moves. */
    m.m[0] = 2.0f / iw;
    m.m[5] = 2.0f / ih;
    m.m[8] = (r + l) / iw;
    m.m[9] = (t + b) / ih;
    m.m[10] = -(zfar + znear) / (zfar - znear);
    m.m[11] = -1.0f;
    m.m[14] = -(2.0f * zfar * znear) / (zfar - znear);
    return m;
}

static void on_state(XrSessionState state) {
    XrSessionBeginInfo begin = {XR_TYPE_SESSION_BEGIN_INFO};

    if (state == XR_SESSION_STATE_READY) {
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (xr_ok(xrBeginSession(sSession, &begin), "xrBeginSession")) {
            sRunning = 1;
        }
    } else if (state == XR_SESSION_STATE_STOPPING) {
        if (sRunning) {
            xrEndSession(sSession);
            sRunning = 0;
        }
        sQuit = 1;
    } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
        sQuit = 1;
    }
}

static int poll_events(void) {
    XrEventDataBuffer ev;

    for (;;) {
        XrResult r;

        memset(&ev, 0, sizeof(ev));
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        r = xrPollEvent(sInstance, &ev);
        if (r == XR_EVENT_UNAVAILABLE) {
            return 1;
        }
        if (XR_FAILED(r)) {
            xr_fail(r, "xrPollEvent");
            return 0;
        }
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)&ev;

            on_state(changed->state);
        }
    }
}

static int has_extension(void) {
    uint32_t n = 0, i;
    XrExtensionProperties *p;
    int found = 0;

    if (!xr_ok(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL), "counting extensions")) {
        return 0;
    }
    p = calloc(n > 0 ? n : 1, sizeof(*p));
    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        p[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    if (n > 0 && xr_ok(xrEnumerateInstanceExtensionProperties(NULL, n, &n, p), "extensions")) {
        for (i = 0; i < n; i++) {
            if (strcmp(p[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0) {
                found = 1;
            }
        }
    }
    free(p);
    if (!found) {
        fprintf(stderr, "vr: this runtime has no OpenGL (XR_KHR_opengl_enable). Use a runtime that does, such as SteamVR.\n");
    }
    return found;
}

static int make_swapchain(int eye, uint32_t w, uint32_t h, int64_t format) {
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    XrSwapchainImageOpenGLKHR images[VR_MAX_IMAGES];
    uint32_t i, n = 0;

    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = w;
    info.height = h;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!xr_ok(xrCreateSwapchain(sSession, &info, &sSwap[eye]), "xrCreateSwapchain")) {
        return 0;
    }
    if (!xr_ok(xrEnumerateSwapchainImages(sSwap[eye], 0, &n, NULL), "counting swapchain images")) {
        return 0;
    }
    if (n == 0 || n > VR_MAX_IMAGES) {
        fprintf(stderr, "vr: unexpected swapchain image count %u\n", n);
        return 0;
    }
    for (i = 0; i < n; i++) {
        images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        images[i].next = NULL;
    }
    if (!xr_ok(xrEnumerateSwapchainImages(sSwap[eye], n, &n, (XrSwapchainImageBaseHeader *)images), "swapchain images")) {
        return 0;
    }
    sImageCount[eye] = n;
    sW[eye] = (int)w;
    sH[eye] = (int)h;
    for (i = 0; i < n; i++) {
        glCreateFramebuffers(1, &sFbo[eye][i]);
        glNamedFramebufferTexture(sFbo[eye][i], GL_COLOR_ATTACHMENT0, images[i].image, 0);
        glNamedFramebufferDrawBuffer(sFbo[eye][i], GL_COLOR_ATTACHMENT0);
        if (glCheckNamedFramebufferStatus(sFbo[eye][i], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "vr: eye %d image %u is not a complete framebuffer\n", eye, i);
            return 0;
        }
    }
    return 1;
}

static int pick_format(int64_t *format) {
    int64_t formats[128];
    uint32_t n = 0, i;
    int64_t fallback = 0;

    if (!xr_ok(xrEnumerateSwapchainFormats(sSession, 0, &n, NULL), "counting formats") || n == 0) {
        return 0;
    }
    if (n > 128) {
        n = 128;
    }
    if (!xr_ok(xrEnumerateSwapchainFormats(sSession, n, &n, formats), "formats")) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (formats[i] == GL_SRGB8_ALPHA8) {   /* the picture is already display-encoded */
            *format = formats[i];
            return 1;
        }
        if (fallback == 0 && formats[i] == GL_RGBA8) {
            fallback = formats[i];
        }
    }
    if (fallback == 0) {
        fallback = formats[0];
    }
    *format = fallback;
    return 1;
}

int vr_open(void) {
    XrInstanceCreateInfo create = {XR_TYPE_INSTANCE_CREATE_INFO};
    XrSystemGetInfo system = {XR_TYPE_SYSTEM_GET_INFO};
    XrSystemProperties sys = {XR_TYPE_SYSTEM_PROPERTIES};
    XrInstanceProperties inst = {XR_TYPE_INSTANCE_PROPERTIES};
    XrGraphicsRequirementsOpenGLKHR glreq = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    XrGraphicsBindingOpenGLWin32KHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    XrSessionCreateInfo session = {XR_TYPE_SESSION_CREATE_INFO};
    XrReferenceSpaceCreateInfo space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    XrViewConfigurationView config[VR_MAX_EYES];
    XrEnvironmentBlendMode blends[4];
    const char *ext = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
    uint32_t nviews = 0, nblends = 0, i;
    int64_t format = 0;
    HDC dc;
    HGLRC rc;
    int spins;

    memset(&sFrame, 0, sizeof(sFrame));
    sLoader = open_loader();
    if (sLoader == NULL) {
        fprintf(stderr, "vr: openxr_loader.dll was not found beside openhuey.exe or in SteamVR\n");
        return 0;
    }
    xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(sLoader, "xrGetInstanceProcAddr");
    if (xrGetInstanceProcAddr == NULL) {
        fprintf(stderr, "vr: openxr_loader.dll has no xrGetInstanceProcAddr\n");
        vr_close();
        return 0;
    }
    LOAD(XR_NULL_HANDLE, xrEnumerateInstanceExtensionProperties);
    LOAD(XR_NULL_HANDLE, xrCreateInstance);
    if (!has_extension()) {
        vr_close();
        return 0;
    }
    strcpy(create.applicationInfo.applicationName, "OpenHuey");
    strcpy(create.applicationInfo.engineName, "OpenHuey");
    create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    create.enabledExtensionCount = 1;
    create.enabledExtensionNames = &ext;
    if (!xr_ok(xrCreateInstance(&create, &sInstance), "xrCreateInstance")) {
        vr_close();
        return 0;
    }
    LOAD(sInstance, xrDestroyInstance);
    LOAD(sInstance, xrResultToString);
    LOAD(sInstance, xrGetInstanceProperties);
    LOAD(sInstance, xrGetSystem);
    LOAD(sInstance, xrGetSystemProperties);
    LOAD(sInstance, xrEnumerateEnvironmentBlendModes);
    LOAD(sInstance, xrEnumerateViewConfigurationViews);
    LOAD(sInstance, xrGetOpenGLGraphicsRequirementsKHR);
    LOAD(sInstance, xrCreateSession);
    LOAD(sInstance, xrDestroySession);
    LOAD(sInstance, xrBeginSession);
    LOAD(sInstance, xrEndSession);
    LOAD(sInstance, xrPollEvent);
    LOAD(sInstance, xrWaitFrame);
    LOAD(sInstance, xrBeginFrame);
    LOAD(sInstance, xrEndFrame);
    LOAD(sInstance, xrLocateViews);
    load_proc(sInstance, "xrLocateSpace", &xrLocateSpace);   /* the floor height; the session still runs without it */
    load_proc(sInstance, "xrCreateActionSet", &xrCreateActionSet);
    load_proc(sInstance, "xrDestroyActionSet", &xrDestroyActionSet);
    load_proc(sInstance, "xrCreateAction", &xrCreateAction);
    load_proc(sInstance, "xrStringToPath", &xrStringToPath);
    load_proc(sInstance, "xrSuggestInteractionProfileBindings", &xrSuggestInteractionProfileBindings);
    load_proc(sInstance, "xrAttachSessionActionSets", &xrAttachSessionActionSets);
    load_proc(sInstance, "xrSyncActions", &xrSyncActions);
    load_proc(sInstance, "xrGetActionStateVector2f", &xrGetActionStateVector2f);
    load_proc(sInstance, "xrGetActionStateBoolean", &xrGetActionStateBoolean);
    load_proc(sInstance, "xrGetActionStateFloat", &xrGetActionStateFloat);
    load_proc(sInstance, "xrCreateActionSpace", &xrCreateActionSpace);
    load_proc(sInstance, "xrApplyHapticFeedback", &xrApplyHapticFeedback);
    LOAD(sInstance, xrCreateReferenceSpace);
    LOAD(sInstance, xrDestroySpace);
    LOAD(sInstance, xrEnumerateSwapchainFormats);
    LOAD(sInstance, xrCreateSwapchain);
    LOAD(sInstance, xrDestroySwapchain);
    LOAD(sInstance, xrEnumerateSwapchainImages);
    LOAD(sInstance, xrAcquireSwapchainImage);
    LOAD(sInstance, xrWaitSwapchainImage);
    LOAD(sInstance, xrReleaseSwapchainImage);

    system.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xr_ok(xrGetSystem(sInstance, &system, &sSystem), "xrGetSystem")) {
        fprintf(stderr, "vr: no headset is connected\n");
        vr_close();
        return 0;
    }
    if (!xr_ok(xrGetOpenGLGraphicsRequirementsKHR(sInstance, sSystem, &glreq), "OpenGL requirements")) {
        vr_close();
        return 0;
    }
    if (!current_context(&dc, &rc)) {
        vr_close();
        return 0;
    }
    binding.hDC = dc;
    binding.hGLRC = rc;
    session.next = &binding;
    session.systemId = sSystem;
    if (!xr_ok(xrCreateSession(sInstance, &session, &sSession), "xrCreateSession")) {
        vr_close();
        return 0;
    }
    for (i = 0; i < VR_MAX_EYES; i++) {
        config[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    if (!xr_ok(xrEnumerateViewConfigurationViews(sInstance, sSystem, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                 VR_MAX_EYES, &nviews, config), "view configuration") ||
        nviews == 0) {
        vr_close();
        return 0;
    }
    sViewCount = (int)nviews;
    if (!pick_format(&format)) {
        vr_close();
        return 0;
    }
    for (i = 0; i < nviews; i++) {
        if (!make_swapchain((int)i, config[i].recommendedImageRectWidth, config[i].recommendedImageRectHeight, format)) {
            vr_close();
            return 0;
        }
    }
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space.poseInReferenceSpace.orientation.w = 1.0f;
    if (!xr_ok(xrCreateReferenceSpace(sSession, &space, &sLocal), "xrCreateReferenceSpace")) {
        vr_close();
        return 0;
    }
    if (!xr_ok(xrCreateReferenceSpace(sSession, &space, &sSpace), "xrCreateReferenceSpace")) {
        vr_close();
        return 0;
    }
    sOrigin.orientation.w = 1.0f;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (xrLocateSpace == NULL || XR_FAILED(xrCreateReferenceSpace(sSession, &space, &sStage))) {
        sStage = XR_NULL_HANDLE;   /* no floor space: height is the local tracking Y */
    }
    if (xr_ok(xrEnumerateEnvironmentBlendModes(sInstance, sSystem, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                               4, &nblends, blends), "blend modes") && nblends > 0) {
        sBlend = blends[0];
        for (i = 0; i < nblends; i++) {
            if (blends[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
                sBlend = blends[i];
            }
        }
    }
    bind_stick();   /* before the session begins: the left controller's stick */
    for (spins = 0; spins < 1000 && !sRunning && !sQuit; spins++) {
        if (!poll_events()) {
            vr_close();
            return 0;
        }
        if (!sRunning && !sQuit) {
            Sleep(10);
        }
    }
    if (!sRunning) {
        fprintf(stderr, "vr: the headset session did not start\n");
        vr_close();
        return 0;
    }
    glDisable(GL_FRAMEBUFFER_SRGB);   /* the renderer already writes display-encoded colour */
    xrGetInstanceProperties(sInstance, &inst);
    xrGetSystemProperties(sInstance, sSystem, &sys);
    fprintf(stderr, "vr: %s, %s, %d eyes, %dx%d\n", inst.runtimeName, sys.systemName, sViewCount, sW[0], sH[0]);
    sActive = 1;
    return 1;
}

void vr_close(void) {
    int eye, i;

    if (sFrameOn && sSession != XR_NULL_HANDLE && xrEndFrame != NULL) {
        XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};

        end.displayTime = sFrame.predictedDisplayTime;
        end.environmentBlendMode = sBlend;
        xrEndFrame(sSession, &end);
        sFrameOn = 0;
    }
    if (sRunning && xrEndSession != NULL) {
        xrEndSession(sSession);
        sRunning = 0;
    }
    for (eye = 0; eye < VR_MAX_EYES; eye++) {
        for (i = 0; i < (int)sImageCount[eye]; i++) {
            if (sFbo[eye][i] != 0) {
                glDeleteFramebuffers(1, &sFbo[eye][i]);
                sFbo[eye][i] = 0;
            }
        }
        if (sSwap[eye] != XR_NULL_HANDLE && xrDestroySwapchain != NULL) {
            xrDestroySwapchain(sSwap[eye]);
            sSwap[eye] = XR_NULL_HANDLE;
        }
        sImageCount[eye] = 0;
    }
    if (sStage != XR_NULL_HANDLE && xrDestroySpace != NULL) {
        xrDestroySpace(sStage);
        sStage = XR_NULL_HANDLE;
    }
    if (sSpace != XR_NULL_HANDLE && xrDestroySpace != NULL) {
        xrDestroySpace(sSpace);
        sSpace = XR_NULL_HANDLE;
    }
    if (sLocal != XR_NULL_HANDLE && xrDestroySpace != NULL) {
        xrDestroySpace(sLocal);
        sLocal = XR_NULL_HANDLE;
    }
    if (xrDestroySpace != NULL) {
        int h;

        for (h = 0; h < 2; h++) {
            if (sHand[h].space != XR_NULL_HANDLE) {
                xrDestroySpace(sHand[h].space);
                sHand[h].space = XR_NULL_HANDLE;
            }
        }
    }
    if (sSession != XR_NULL_HANDLE && xrDestroySession != NULL) {
        xrDestroySession(sSession);
        sSession = XR_NULL_HANDLE;
    }
    if (sActions != XR_NULL_HANDLE && xrDestroyActionSet != NULL) {
        xrDestroyActionSet(sActions);
        sActions = XR_NULL_HANDLE;
        sMove = XR_NULL_HANDLE;
    }
    if (sInstance != XR_NULL_HANDLE && xrDestroyInstance != NULL) {
        xrDestroyInstance(sInstance);
        sInstance = XR_NULL_HANDLE;
    }
    if (sLoader != NULL) {
        FreeLibrary(sLoader);
        sLoader = NULL;
    }
    sActive = 0;
    sQuit = 0;
    sHaveOrigin = 0;
    sLockPose = 0;
}

int vr_active(void) { return sActive; }

void vr_lock_pose(int on) { sLockPose = on != 0; }

static int sScreenOn, sScreenSnap;
static Vec3 sScreen[4];

void vr_screen_face(void) { sScreenSnap = 1; }
void vr_screen_loose(int on) { (void)on; }

/* ---- the comfort vignette's preview: the ring, at the strength chosen, for a moment after the
 * options page changes the row - so the setting can be seen where it is set, still standing ---- */
static XrTime sPreviewAt;

void vr_comfort_preview(void) { sPreviewAt = 1; }

int vr_comfort_previewing(void) {
    if (sPreviewAt == 0 || sFrame.predictedDisplayTime <= 0) {
        return 0;
    }
    /* (the first frame after the request starts the clock: the page's change is a tick, not a frame) */
    if (sPreviewAt == 1) {
        sPreviewAt = sFrame.predictedDisplayTime;
        return 1;
    }
    return sFrame.predictedDisplayTime - sPreviewAt < (XrTime)1200000000;   /* 1.2 s */
}

static float quat_yaw(XrQuaternionf q) {
    float siny = 2.0f * (q.w * q.y + q.x * q.z);
    float cosy = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);

    return atan2f(siny, cosy);
}

/* one placement: level, 1.8 m in front of the look at this moment. Stored, not rebuilt.
 * The look's forward in play space is R_y(yaw)·(0,0,-1) = (-sin yaw, 0, -cos yaw) and its right is
 * R_y(yaw)·(1,0,0) = (cos yaw, 0, -sin yaw): the x of each was taken positive, which put the panel
 * at the *opposite* yaw - a menu would open as far to one side as the player had turned to the
 * other. */
static void place_screen(Vec3 out[4]) {
    XrPosef head = head_look();
    float yaw = quat_yaw(head.orientation);
    float dist = 1.8f * VR_UNITS_PER_METER;
    float half_h = dist * 0.36f;
    float half_w = half_h * (4.0f / 3.0f);
    float fx = -sinf(yaw);
    float fz = -cosf(yaw);
    float rx = cosf(yaw);
    float rz = -sinf(yaw);
    Vec3 center = vec3(head.position.x * VR_UNITS_PER_METER + fx * dist,
                       head.position.y * VR_UNITS_PER_METER - 0.08f * VR_UNITS_PER_METER,
                       head.position.z * VR_UNITS_PER_METER + fz * dist);
    Vec3 right = vec3(rx * half_w, 0.0f, rz * half_w);
    Vec3 up = vec3(0.0f, half_h, 0.0f);

    out[0] = vec3_sub(vec3_sub(center, right), up);
    out[1] = vec3_sub(vec3_add(center, right), up);
    out[2] = vec3_add(vec3_sub(center, right), up);
    out[3] = vec3_add(vec3_add(center, right), up);
}

void vr_cutscene_screen(int active, Vec3 corners[4]) {
    int i;

    if (!active || !sHaveOrigin || sViewCount <= 0) {
        sScreenOn = 0;
        return;
    }
    if (!sScreenOn || sScreenSnap) {
        place_screen(sScreen);   /* in front of the current look (a menu: every frame) */
        sScreenOn = 1;
        sScreenSnap = 0;
    }
    if (corners != NULL) {
        for (i = 0; i < 4; i++) {
            corners[i] = sScreen[i];
        }
    }
}

static int locate_views(XrSpace space) {
    XrViewLocateInfo locate = {XR_TYPE_VIEW_LOCATE_INFO};
    XrViewState view_state = {XR_TYPE_VIEW_STATE};
    uint32_t n = 0;
    int i;

    if (space == XR_NULL_HANDLE) {
        return 0;
    }
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = sFrame.predictedDisplayTime;
    locate.space = space;
    for (i = 0; i < sViewCount; i++) {
        sViews[i].type = XR_TYPE_VIEW;
        sViews[i].next = NULL;
    }
    if (!xr_ok(xrLocateViews(sSession, &locate, &view_state, (uint32_t)sViewCount, &n, sViews), "xrLocateViews") ||
        (int)n != sViewCount) {
        return 0;
    }
    sViewFlags = view_state.viewStateFlags;
    for (i = 0; i < sViewCount; i++) {
        sLayerViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        sLayerViews[i].next = NULL;
        sLayerViews[i].pose = sViews[i].pose;
        sLayerViews[i].fov = sViews[i].fov;
        sLayerViews[i].subImage.swapchain = sSwap[i];
        sLayerViews[i].subImage.imageRect.offset.x = 0;
        sLayerViews[i].subImage.imageRect.offset.y = 0;
        sLayerViews[i].subImage.imageRect.extent.width = sW[i];
        sLayerViews[i].subImage.imageRect.extent.height = sH[i];
        sLayerViews[i].subImage.imageArrayIndex = 0;
    }
    return 1;
}

static int pose_tracked(void) {
    XrQuaternionf q = head_pose().orientation;
    float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

    return (sViewFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
           (sViewFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 && n > 0.5f;
}

/* yaw about Y only. Pitch and roll stay on the headset, so a tilted click does not become the horizon. */
static XrQuaternionf yaw_only(float yaw) {
    XrQuaternionf q;

    q.x = 0.0f;
    q.y = sinf(yaw * 0.5f);
    q.z = 0.0f;
    q.w = cosf(yaw * 0.5f);
    return q;
}

/* the play space's origin is the head, turned to the facing yaw and level. It is not touched again. */
static int recenter_play_space(void) {
    XrReferenceSpaceCreateInfo info = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    XrSpace play = XR_NULL_HANDLE;
    XrPosef head = head_pose();

    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    info.poseInReferenceSpace.position = head.position;
    info.poseInReferenceSpace.orientation = yaw_only(quat_yaw(head.orientation));
    if (XR_FAILED(xrCreateReferenceSpace(sSession, &info, &play))) {
        return 0;
    }
    if (sSpace != XR_NULL_HANDLE && xrDestroySpace != NULL) {
        xrDestroySpace(sSpace);
    }
    sSpace = play;
    sScreenSnap = 1;   /* (a menu's panel was placed in the old space: put it in front again) */
    sOrigin.orientation.x = sOrigin.orientation.y = sOrigin.orientation.z = 0.0f;
    sOrigin.orientation.w = 1.0f;
    sOrigin.position.x = sOrigin.position.y = sOrigin.position.z = 0.0f;
    sHaveOrigin = 1;
    return 1;
}

int vr_begin_frame(void) {
    XrFrameWaitInfo wait = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameBeginInfo begin = {XR_TYPE_FRAME_BEGIN_INFO};

    if (!sActive || sQuit) {
        return 0;
    }
    if (!poll_events() || sQuit || !sRunning) {
        return 0;
    }
    sFrame.type = XR_TYPE_FRAME_STATE;
    if (!xr_ok(xrWaitFrame(sSession, &wait, &sFrame), "xrWaitFrame")) {
        return 0;
    }
    if (!xr_ok(xrBeginFrame(sSession, &begin), "xrBeginFrame")) {
        return 0;
    }
    sFrameOn = 1;
    sync_stick();
    if (!sLogged) {
        /* once, to the console: what a headset session was told to do. A support line - the VR
         * options are otherwise silent, and a setting that never reached the engine looks the
         * same as one that did nothing. */
        sLogged = 1;
        printf("vr: comfort %d (%.2f), scenes from her eyes %d, world scale %.3f (eye %.2f m), seated %d\n",
               (int)gVrOptions.comfort, (double)gVrOptions.comfort_strength,
               (int)gVrOptions.cutscene_first_person, (double)body_scale(),
               (double)gVrOptions.eye_height, (int)gVrOptions.seated);
    }
    sRender = 0;
    sSubmit = 0;
    if (!sFrame.shouldRender) {
        return 1;
    }
    /* the first tracked pose, and both stick clicks, set the yaw origin once. Views after
     * that are the offset from that level facing, and the layer is submitted in that same
     * space. Pitch and roll are not stored, and the origin is not adjusted again. */
    if (!sHaveOrigin || sRecenterReq) {
        if (locate_views(sLocal) && pose_tracked() && recenter_play_space()) {
            sAlignRecenter = 1;
            sRecenterReq = 0;
            vr_screen_face();
        } else if (!sHaveOrigin) {
            return 1;
        }
    }
    if (!sHaveOrigin || !locate_views(sSpace)) {
        return 1;   /* the frame is begun: end it with nothing to present */
    }
    sRender = 1;
    sSubmit = 1;
    return 1;
}

int vr_should_render(void) { return sRender; }

void vr_end_frame(void) {
    XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader *layers[1];
    XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};

    if (!sFrameOn) {
        return;
    }
    layer.space = sSpace;
    layer.viewCount = (uint32_t)sViewCount;
    layer.views = sLayerViews;
    layers[0] = (const XrCompositionLayerBaseHeader *)&layer;
    end.displayTime = sFrame.predictedDisplayTime;
    end.environmentBlendMode = sBlend;
    if (sSubmit) {
        end.layerCount = 1;
        end.layers = layers;
    }
    xrEndFrame(sSession, &end);
    sFrameOn = 0;
    sRender = 0;
}

int vr_eye_count(void) { return sViewCount; }

int vr_head_view(int eye, Mat4 *view) {
    if (view == NULL || eye < 0 || eye >= sViewCount) {
        return 0;
    }
    *view = pose_view(sViews[eye].pose);
    return 1;
}

int vr_eye_begin(int eye, float znear, float zfar, int *w, int *h, Mat4 *proj, Mat4 *view, unsigned *fbo) {
    XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    uint32_t index = 0;

    if (eye < 0 || eye >= sViewCount || !sRender) {
        return 0;
    }
    wait.timeout = XR_INFINITE_DURATION;
    if (!xr_ok(xrAcquireSwapchainImage(sSwap[eye], &acquire, &index), "xrAcquireSwapchainImage") ||
        index >= sImageCount[eye]) {
        sSubmit = 0;
        return 0;
    }
    if (!xr_ok(xrWaitSwapchainImage(sSwap[eye], &wait), "xrWaitSwapchainImage")) {
        XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

        xrReleaseSwapchainImage(sSwap[eye], &release);
        sSubmit = 0;
        return 0;
    }
    sAcquired[eye] = 1;
    *w = sW[eye];
    *h = sH[eye];
    *proj = proj_fov(sViews[eye].fov, znear, zfar);
    *view = sLockPose ? pose_to_view(pose_from_head(sViews[eye].pose)) : pose_view(sViews[eye].pose);
    *fbo = sFbo[eye][index];
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glDisable(GL_FRAMEBUFFER_SRGB);
    return 1;
}

void vr_eye_end(int eye) {
    XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

    if (eye < 0 || eye >= VR_MAX_EYES || !sAcquired[eye]) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
    xrReleaseSwapchainImage(sSwap[eye], &release);
    sAcquired[eye] = 0;
}

#endif
