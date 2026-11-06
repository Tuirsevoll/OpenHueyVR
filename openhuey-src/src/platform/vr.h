/* A headset session (OpenXR). The game's camera stays where the scripts put it; each eye is that
 * camera plus the headset's pose. With no call to vr_open (tests, --hidden) nothing here runs. */
#ifndef VR_H
#define VR_H

#include "../core/mathx.h"

int vr_open(void);          /* the session, ready to draw; 0 on failure */
void vr_close(void);
int vr_active(void);

/* one headset frame. 0: the session ended (do not draw, do not vr_end_frame) */
int vr_begin_frame(void);
int vr_should_render(void); /* this frame wants both eyes */
void vr_end_frame(void);

int vr_eye_count(void);
/* while locked, an eye is only its offset from the head: a cutscene or story shot keeps the
 * scripted camera in front of the player. Head look applies again once this is cleared. */
void vr_lock_pose(int on);
/* a 4:3 screen in tracking space, level, in front of the player. While `active`, its yaw stays
 * where the cutscene began and only eases back if the player turns well away; pitch and roll
 * are not followed. corners: bottom left, bottom right, top left, top right. */
void vr_cutscene_screen(int active, Vec3 corners[4]);
/* this eye's image. `view` maps camera space onto the eye (headset pose, in world units);
 * `proj` is its OpenXR field of view over the camera's near and far. `fbo` is the swapchain. */
int vr_eye_begin(int eye, float znear, float zfar, int *w, int *h, Mat4 *proj, Mat4 *view, unsigned *fbo);
void vr_eye_end(int eye);

#endif
