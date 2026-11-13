/* A headset session (OpenXR). The game's camera stays where the scripts put it; each eye is that
 * camera plus the headset's pose. With no call to vr_open (tests, --hidden) nothing here runs. */
#ifndef VR_H
#define VR_H

#include "../core/mathx.h"

#include <stdint.h>

/* The VR options (scripts/vr-options.fs: the player's, kept in <user>/vr-options.fs). Each one is a
 * choice the game had no choice about before: it is as the game was until the player picks
 * otherwise. C reads them as it runs; the VR options menu writes them through the `vr.` field
 * words (forth/bind_engine.c). */
typedef struct VrOptions {
    int32_t turn_snap;        /* 0: smooth turning (as before), 1: a snap a push of the turn stick */
    float snap_degrees;       /* a snap's angle: 15 .. 90 */
    int32_t comfort;          /* the comfort vignette while she is moving: 0 off, 1 on */
    float comfort_strength;   /* how dark its corners go: the page's 0.25 low, 0.5 med, 0.75 high */
    int32_t teleport;         /* 0: the stick walks her (as before), 1: a stick flick teleports */
    int32_t teleport_mode;    /* 0: the flick always goes `teleport_reach`; 1: it starts near and
                               * grows the longer the stick is held (up to teleport_reach) */
    float teleport_reach;     /* how far a teleport goes, or the furthest it grows to, metres */
    int32_t seated;           /* seated: her eye height is the seated one, never the floor's */
    float seated_height;      /* the eye height seated play is sized to, metres (1.6: as standing) */
    float seated_offset;      /* the view raised or lowered within her body, metres */
    int32_t left_handed;      /* the sticks and the face buttons swapped between the hands */
    int32_t calibrate_start;  /* the start-up height calibration: a first run, or asked for */
    int32_t cutscene_first_person;   /* retired: kept so an older options file still loads; nothing
                                      * reads it and no row writes it (a cutscene keeps its screen) */
    /* what a height calibration left (no row of its own: the page's calibrate row and the title's
     * first-run step set it, vr-options.fs keeps it) */
    float eye_height;         /* her eyes above the floor, metres */
    float body_scale;         /* a metre of head motion in the world's units (1: as measured) */
    int32_t calibrated;       /* 1 once a calibration has been kept */
} VrOptions;
extern VrOptions gVrOptions;

int vr_open(void);          /* the session, ready to draw; 0 on failure */
void vr_close(void);
/* seated (vr.seated): how far the view rides above (positive) or below her own eyes, in room
 * units - the camera and the hands' anchor together (0: none) */
float vr_seat_lift(void);
int vr_active(void);

/* one headset frame. 0: the session ended (do not draw, do not vr_end_frame) */
int vr_begin_frame(void);
int vr_should_render(void); /* this frame wants both eyes */
void vr_end_frame(void);

int vr_eye_count(void);
/* while locked, an eye is only its offset from the head: a cutscene or story shot keeps the
 * scripted camera in front of the player. Head look applies again once this is cleared. */
void vr_lock_pose(int on);
/* a 4:3 screen in tracking space, level, in front of the player. It is placed when it first
 * appears, and again after vr_screen_face. After that it stays at that spot: looking around
 * does not move it. corners: bottom left, bottom right, top left, top right. */
void vr_cutscene_screen(int active, Vec3 corners[4]);
/* this eye's view of tracking space, head pose included (the UI panel lives there) */
int vr_head_view(int eye, Mat4 *view);
/* 1 once a neutral head pose has been captured. Until then the view is not drawn. */
int vr_tracking(void);
/* gameplay view of the head, parked on the caller’s camera: rotation and position relative
 * to the neutral pose (lean and duck), with a long walk clamped. 0 when there is no pose yet. */
int vr_body_view(Mat4 *view);
/* one eye on that head: her eyes apart by the calibrated scale, rotation from the headset */
int vr_eye_body(int eye, Mat4 *view);
/* stand straight and call this: the headset's height above the floor becomes the eye height,
 * and the world scale becomes Fiona's height over that. 0 if the headset has no height. */
int vr_calibrate(float *height_m, float *scale);
void vr_set_height(float height_m);
void vr_set_scale(float scale);
void vr_body(float *height_m, float *scale);
/* the headset's yaw relative to where it was at startup (radians, right is positive) */
float vr_head_yaw(void);
/* the yaw movement should use: her facing plus where the headset is looking */
void vr_set_move_yaw(float yaw);
float vr_move_yaw(void);
/* game heading of the rendered look (0 along +z), for interaction tests */
void vr_set_face_yaw(float yaw);
float vr_face_yaw(void);
/* the left controller stick: x right, y down, length 1 past the dead zone (0 0 at rest) */
void vr_stick(float *x, float *y);
/* Quest buttons, in the game’s order (circle, square, …, select). down is held; pressed is this tick. */
int vr_pad_down(int button);
int vr_pad_pressed(int button);
void vr_pad_clear(void);   /* a tick took the presses */
int vr_run_on(void);       /* left stick click toggles this */
int vr_talk_held(void);    /* right stick click held, and not the left (both clicks still recenter) */
/* right stick x, -1..1, after the dead zone. Both stick clicks request a yaw reset. */
float vr_right_stick(void);
void vr_add_turn(float yaw);
float vr_take_turn(void);
float vr_peek_turn(void);   /* the turn waiting, taken or not (the picture blends towards it) */
/* the right stick's snap turn waiting, in radians (0: none): a push of the turn stick past its
 * threshold, once, the angle the options' snap has. The scripts take it (vr-snap). */
float vr_snap_take(void);
void vr_request_recenter(void);
int vr_take_recenter(void);
/* her eyes in the world, and the yaw the headset's neutral look uses. Hands and grabs sit on this. */
void vr_anchor_set(int on, Vec3 eye, float yaw);
int vr_anchor_get(Vec3 *eye, float *yaw);
/* one controller in that same space: `at` is metres from the neutral head, already in world units.
 * forward is where the controller points, up is the back of the hand. grip closes the three fingers,
 * index curls the pointing finger, thumb is 1 while it rests in. */
typedef struct VrController {
    int tracked;
    Vec3 at;                 /* world units from the neutral head, camera axes */
    Vec3 right, up, forward; /* grip pose axes in that same space: +X, +Y, and -Z */
    float grip, index, thumb;
} VrController;
int vr_controller(int hand, VrController *out);   /* 0 left, 1 right */
int vr_hands_tracked(void);
/* a short pulse on that controller. amplitude is 0..1. No session: nothing. */
void vr_haptic(int hand, float amplitude, float seconds);
/* the headset in the room, the same point the eyes are drawn from. 0 if it is not tracking. */
int vr_head_at(Vec3 *out);
/* the headset in the same space and units as a controller's `at`. 0 if it is not tracking. */
int vr_head_local(Vec3 *out);
/* horizontal forward in that space, where the headset is looking. 0 if it is not tracking. */
int vr_head_forward(Vec3 *out);
/* the next panel placement is once, in front of the current look, and then stays put */
void vr_screen_face(void);
void vr_screen_loose(int on);   /* kept for callers; the panel does not follow the head */
/* the comfort vignette at the strength chosen, for a moment: the options page asks when its row
 * changes, so a player can see what they picked without walking */
void vr_comfort_preview(void);
int vr_comfort_previewing(void);
/* this eye's image. `view` maps camera space onto the eye (headset pose, in world units);
 * `proj` is its OpenXR field of view over the camera's near and far. `fbo` is the swapchain. */
int vr_eye_begin(int eye, float znear, float zfar, int *w, int *h, Mat4 *proj, Mat4 *view, unsigned *fbo);
void vr_eye_end(int eye);

#endif
