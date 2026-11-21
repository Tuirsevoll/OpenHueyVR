/* Arms follow the controllers. Finger shapes are the model's hand morphs (one blend), and a
 * mod's PROPS/grip_poses.txt can turn its own finger bones to the grip an item names
 * (src/game/gripposes.c, applied last in vrhands_pose). The rest of her follows too, in VR
 * gameplay only: src/game/vrbody.c turns her chest toward the hands and puts her body under
 * the headset (docs/subsystems/vrbody.md). A model's own `vr ...` helper bones take the
 * forearm's and the upper arm's roll, and its first-person head is shrunk away - all of it
 * skipped the moment the arms are.
 * Doors swing in degrees (0 shut, open either way to ±90). A hand moves one only while it
 * is touching it. A shut door follows a grip on the knob. A door off the latch can be pushed
 * from either face, anywhere on the leaf. Letting go eases it by the 45° rule, unless it was
 * already moving, in which case it coasts to the stop it is heading for. A locked door never
 * moves; gripping its knob rattles once. A placed leaf - the nursery wardrobe's doors - is held
 * by its animation's frame instead: the hand's angle about its hinge is the frame its track gives,
 * and a leaf let go while it was turning keeps that speed to the stop it was heading for.
 * A door is held by the door, not by the room's exit (the two rooms it joins number it apart), so
 * a room change carries the hold to the same door in the room now in - the knob under the hand -
 * and any other door the hands were still finishing ends at its stop there and then: the ease runs
 * in the played room only, and an exit index means nothing in the room just left. The leaf's last
 * motion is remembered, so one being dragged shut as she crosses finishes shut (the way it was
 * going); one let go still takes the 45° rule. A knob past the doorway, the leaf swung away, is
 * taken from over the threshold: the reach is measured there, not at the knob. */
/* temporary, for the headset smoothing test; remove later */
#define VR_STEP_LOG 0

#include "vrhands.h"
#include "gripposes.h"
#include "itemprops.h"
#include "loot.h"
#include "things.h"
#include "vrbody.h"
#include "vrbones.h"
#include "vrspots.h"
#include "vrlevers.h"
#include "vrmirror.h"

#include "engine.h"
#include "progress.h"
#include "room.h"
#include "placed.h"

#include "../hactor/hactor.h"
#include "../platform/vr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#if VR_STEP_LOG
/* (temporary, for the headset smoothing test; remove later) what the pose costs, in microseconds:
 * the whole of vrhands_pose, and the hand_stop() calls in it (one a side, so both add up). Added to
 * as she is posed, taken and cleared by vrhands_take_times (main.c, once a DRAW line). */
static unsigned pose_us;
static unsigned stop_us;

/* a performance-counter span in microseconds (0 when the counter has no frequency) */
static unsigned perf_us(Uint64 from, Uint64 to) {
    Uint64 f = SDL_GetPerformanceFrequency();

    return f > 0 ? (unsigned)((to - from) * 1000000ull / f) : 0;
}

/* the times added up since the last call (either pointer may be NULL), both cleared */
void vrhands_take_times(unsigned *pose, unsigned *stop) {
    if (pose != NULL) {
        *pose = pose_us;
    }
    if (stop != NULL) {
        *stop = stop_us;
    }
    pose_us = 0;
    stop_us = 0;
}
#endif

#define HOLD_NONE 0
#define HOLD_DOOR 1
#define HOLD_PROP 2
#define HOLD_LOOT 3
#define HOLD_WARD 4
#define HOLD_SPOT 5   /* a hiding spot's hand: one part's handle scrubs the whole spot (vrspots.c) */
#define HOLD_LEVER 6  /* a lever's hand: pulled along its handle's path (vrlevers.c) */
#define HOLD_MIRROR 7 /* a curtained mirror's hand: the curtain scrubbed along its handle's path (vrmirror.c) */

static int sHold[2], sHoldId[2], sDoorAxis[2];
/* the hiding spot that hand scrubs: the part it took, the offset from that part's handle to the
 * hand (so nothing jumps to it), and the closure it was at with its speed a tick (the coast a
 * let-go hand leaves the spot) */
static int sSpotPart[2];
static Vec3 sSpotOffset[2];
static float sSpotC[2], sSpotSpeed[2];
/* the lever that hand pulls: the offset from its handle to the hand (nothing jumps to it) */
static Vec3 sLeverOffset[2];
/* the mirror curtain that hand draws: the offset from its handle to the hand (nothing jumps to it) */
static Vec3 sMirrorOffset[2];
/* (DEBUG) the reason a spot's hand is ending, for the log line */
static const char *sSpotWhy = "let-go";
/* (DEBUG) the same for a mirror's curtain */
static const char *sMirrorWhy = "let-go";
/* (DEBUG) the spot's hand lines (defined with the other debug logs below) */
static void spot_take_log(int id, const char *how, Vec3 at);
static void spot_release_log(int id, const char *why);
/* the door itself (its id in the world's table), not the room's exit: the two rooms a door joins
 * need not number it alike (215 of the game's 303 doors have another exit on their far side), so
 * an exit index is the old room's once a room change has happened */
static int sHoldDoor[2] = {-1, -1};
/* a hold just carried to the room now in: the hand's place is a point in the room, so it has moved
 * with the room - the leaf is driven from the next tick, not this one */
static int sHoldFresh[2];
/* an equipped magnesia (dropped) or antimony (thrown), shown in the right hand until a grip
 * closes after it appears and then opens (the grip pose asks what that hand is showing). */
static struct {
    int item, kind, drop, side;
    int armed, saw_low, shown;
    int pending, pkind;
    Vec3 at, right, up, fwd;
    Vec3 hist[4];
    int histN;
    Vec3 pat, pvel;
} sDef = {.item = -1, .kind = -1, .side = 1};
/* the key item the scripts put in her right hand (items.fs's `Equip`, gEngine.hand_item): shown
 * in VR while she plays, with the grip pose its grip line names. Never thrown: no arming, and
 * the defense path is left as it is. The magnesia/antimony in slot 2 has the hand instead. */
static struct {
    int item, side, shown;
    Vec3 at, right, up, fwd;
} sHand = {.item = -1, .side = 1};
/* the ladder each hand holds (a nav link of the room, -1 none): the air beside a ladder while
 * Fiona climbs, and the pull of that hand, which is what climbs her (docs/subsystems/hands.md,
 * "Pull to climb") */
static int sLadderHold[2] = {-1, -1};
static int sLadderFresh[2];      /* 1 from the grab until Fiona has taken it (ladder-grab) */
static Vec3 sLadderGrabAt[2];    /* where the hand was when it grabbed */
/* where each hand was in play space - an offset from her eyes, turned by her yaw - which is the
 * anchor its pull is measured from, and whether that anchor is set yet */
static Vec3 sLadderAnchor[2];
static int sLadderAnchorHave[2];
/* the pull both hands have given since the scripts last took it, in the game's y (which runs down,
 * so up is negative): the move her body makes */
static float sLadderPull;
static float sGripWas[2], sGrabYaw[2];
static Vec3 sGrabRot[2];
/* a wardrobe leaf taken by that hand: the angle it took it at, the frame it held, and the span and
 * rate of the leaf's turn track (frames, radians a frame) */
static float sWardAng[2], sWardRate[2];
static int sWardFrame[2], sWardLo[2], sWardHi[2];
/* the frame the hand's drive gave that leaf last tick, before it is rounded */
static float sWardLast[2];
/* the track the hand took that leaf with: a script that plays the leaf after that (placed_anim: a
 * track of its own, `paused` cleared) shows as a change here, and the leaf is taken again */
static const uint8_t *sWardKeys[2];
static int sWardFrames[2];
/* a leaf a hand has let go: the speed it was turning at and the stop it runs to (ward_tick) */
typedef struct WardMove {
    int mode;      /* 0 still - in a hand, or let go slower than WARD_COAST - 1 coasting */
    float at;      /* its frame, not yet rounded */
    float speed;   /* frames a tick */
} WardMove;
static WardMove sWardMove[PLACED_MAX];
static int sWardRoom = -2;   /* (the room the moves were made in: another room's indices are not these) */
#if 0   /* (temporary: the wardrobe grab's diagnostics - the hands that have buzzed for a leaf, and the log's
 * count) */
static int sWardNear[2], sWardLogs;
#endif
/* (the wall sweep's: the hands that have buzzed for a wall - in again buzzes again) */
static int sWallNear[2];
#if 0   /* (temporary: the hiding spot's probe - the last line it gave, and the room it was read in) */
static int sHideSeen, sHideInside, sHideFrame, sHideRoom = -2;
#endif
static int sSettle[ROOM_DOORS];
static int sCoast[ROOM_DOORS];
static float sVel[ROOM_DOORS];
static int sPushNow[ROOM_DOORS];   /* a bare hand moved this leaf this frame */
static int sPushWas[ROOM_DOORS];   /* it did so last frame: contact ending is a release */
static int sPushRoom = -2;
static int sGrace[2], sGraceDoor[2];   /* after a throw, that hand does not shove this door */
static Vec3 sHandAt[2];
static int sHandHave[2];
static Vec3 sHandStop[2];       /* the arm's place, in world space */
static int sHandStopHave[2];    /* 0: no stop yet */
/* the room the stops and the body's smoothed hands were last made in: a drawn frame between two
 * ticks can be the first in the new room, before the tick's own room-change branch has run */
static int sHandRoom = -2;
/* (temporary: the stop's feel signal - the controller's own place, and whether that hand was held
 * back last pose) */
static Vec3 sStopRaw[2];
static int sStopHeld[2];
static int sHandLogDone;   /* (the hand's scales, said once) */
static int sPoseRoom = -2;
/* one pose per door, shared by both rooms it joins */
static float sDoorSwing[DOORS];
static unsigned char sDoorHave[DOORS], sDoorOpen[DOORS];
/* the doors the hands still answer for - driven, pushed or eased, and the doors actor's let-go
 * (written at rest, in the played room) not come back yet. By door, not by exit: a room change
 * leaves the old room's exits behind (sHoldDoor). */
static unsigned char sDoorHand[DOORS];
/* the doors whose held bit this side set (claim_for_letgo), so its own claim can be taken back
 * without touching one a script holds */
static unsigned char sDoorClaim[DOORS];
/* how fast each door's leaf was going, degrees a tick, as the last tick left it (poses_save): a
 * room change takes the room's ease with it, and this is which way the leaf was travelling then -
 * a door dragged shut as she crossed finishes shut, not by the angle alone */
static float sDoorVel[DOORS];

static int sArmBones, sArmVerts;
static char sArmName[64];   /* the model file: the same counts in another costume number their bones differently */
static char sSkipName[64];  /* the model the last skip line was for, and why */
static char sSkipWhy[128];
static char sNoArmName[64]; /* the model whose arm chain was not found (its one log line) */
static int sShoulder[2], sElbow[2], sHandBone[2], sArmFound[2];
/* the bind pose's upper arm and forearm, model units scaled to room units: once per model */
static float sArmLen[2][2];
static int sArmLenHave[2];
static float sArmLenK[2];   /* (the arm scale they were measured at: a new calibration remeasures) */
static const Model *sLenModel;
static int sLenBones, sLenVerts;
/* the palm, the last bone of each arm's chain, which the controller drives; where the wrist
 * sits on elbow->palm as the bind pose has it; and the hand's centre in the palm's space */
static int sPalmBone[2];
/* the bone the hand is measured on (hand_measure: its extent and centre): the geometric chain's
 * wrist, which is what the grip was always measured on, so a known model's hand is the same size
 * and in the same place whether the chain is found by geometry or by name */
static int sMeasureBone[2];
static float sWristT[2];
static Vec3 sGripCenter[2];
/* the elbow's bend direction last frame (a fallback when the pole lies along the arm) and the
 * wrist's twist last frame (so the half-twist does not flip when it passes +-180 degrees) */
static float sBendWas[2][3];
static int sBendHave[2];
static float sTwistWas[2];
static int sTwistHave[2];
/* the last good up vector aim() built for each side, kept for a reference along the bone */
static float sUpWas[2][3];
static int sUpHave[2];
static const Model *sExtentModel;
static int sExtentVerts;
static float sExtent[2];
static Vec3 sCenter[2];

#define VR_UNITS_PER_METER 12.5f
#define HAND_METRES 0.19f   /* wrist to fingertip before HAND_SCALE */
/* the elbows' bend in her body's frame: down and out, a little behind her (her forward is +z) */
#define ELBOW_BACK -0.3f
/* the elbow's bend turns at most this a frame (5 degrees), and a pole nearer than this to the
 * arm's line (sin of the angle, about 11.5 degrees) is blended toward straight down first */
#define ELBOW_STEP 0.087266f
#define ELBOW_ALONG 0.2f
/* how the controller's twist about the forearm is shared: the forearm's aim takes the first
 * share, the wrist the next, and the palm keeps the rest (1 - TWIST_FOREARM - TWIST_WRIST) */
#define TWIST_FOREARM 0.5f
#define TWIST_WRIST 0.25f
/* aim()'s reference nearer than this to the bone's length (sin of the angle, about 2.9°) is
 * taken as degenerate: the last good up for that side is used instead */
#define AIM_PARALLEL 0.05f
/* Both hands, every pose. 1 leaves the fitted size; 1.5 is half again as large. */
#define HAND_SCALE 1.5f
/* The arm stops at the room's mesh where the hand's buzz fires (SWING_SKIN's 0.40): the hand is
 * stopped at the surface it has just been warned of, from the wrist and from the fingertip. */
#define HAND_STOP 0.40f
/* the soft buzz when a hand touches a wall, floor, ceiling, door or placed object; 0 is off, 1 is on */
#define WALL_BUZZ 0
/* the sharp pulse when the arm stop engages; 0 is off, 1 is on */
#define STOP_PULSE 0
/* (temporary: the arm stops letting go when the hand is this far from its stop, about an arm's
 * length) */
#define HAND_RELEASE 5.0f

/* Is this Fiona's actor? The pose hook must be installed for her in every mode, VR or not: the
 * pose is where the VR state is idled and where a skip is logged. VR activity is checked there,
 * not here. The name test takes every outfit (O_FIN, O_FIS, O_FIH, ...). */
int vrhands_wants(const Actor *a) {
    return a != NULL && a->used && strncmp(a->name, "O_FI", 4) == 0;
}

static int leg_bone(const Actor *a, int b) {
    int i, k;

    if (!a->hlegs.on) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 4; k++) {
            if (a->hlegs.bones[i][k] == b) {
                return 1;
            }
        }
    }
    return 0;
}

/* the palm: of the wrist bone's children, the one the hand's vertices sit on most (the last
 * bone of the arm's chain); the wrist itself when the model has none */
static int palm_bone(const Model *m, int wrist) {
    int i, k, v, best = wrist, bestn = 0;

    for (i = 0; i < m->nbones; i++) {
        int n = 0;

        if (m->bones[i].parent != wrist) {
            continue;
        }
        for (v = 0; v < m->nv; v++) {
            for (k = 0; k < 4; k++) {
                if (m->v[v].bones[k] == i && m->v[v].weights[k] >= 0.15f) {
                    n++;
                    break;
                }
            }
        }
        if (n > bestn) {
            bestn = n;
            best = i;
        }
    }
    return best;
}

/* The arm's chain. Each costume's model names its own bones in the fiona-vr-rig mod's
 * `<model>.model/vr_bones.txt` (vrbones.h), so the table is what is taken: the shoulder joint (the
 * upper arm), the elbow hinge and the wrist, the wrist itself the palm the grip is placed on. A
 * model without a table keeps the geometric search: the outermost bone two steps down from a
 * joint, its grandparent the shoulder, its parent the elbow, its child the palm. The hand is
 * measured on the geometric wrist either way, so its size and its seat are exactly what they were.
 * Both answers are logged, once a model (the fingers are a mod's own bones: never an index of
 * ours). */
static void find_arms(Actor *a, const Mat4 *world) {
    const Model *m = &a->model;
    float best[2] = {-1.0f, -1.0f};
    int i, k, near_sh[2], near_el[2], near_ha[2], named[6];

    /* by the model's file, not the actor's address: a second actor wearing the same model (a
     * scene's double) must not re-run this or restart anything keyed to it */
    if (sArmBones == m->nbones && sArmVerts == m->nv &&
        strcmp(sArmName, a->name) == 0 && sArmFound[0] && sArmFound[1]) {
        return;
    }
    sArmBones = m->nbones;
    sArmVerts = m->nv;
    snprintf(sArmName, sizeof(sArmName), "%s", a->name);
    sArmFound[0] = sArmFound[1] = 0;
    sPalmBone[0] = sPalmBone[1] = -1;
    sMeasureBone[0] = sMeasureBone[1] = -1;
    for (i = 0; i < m->nbones; i++) {
        int p = m->bones[i].parent;
        int g = p >= 0 ? m->bones[p].parent : -1;
        float dx, dy, dz, span;
        int side;

        if (g < 0 || leg_bone(a, i) || leg_bone(a, p) || leg_bone(a, g)) {
            continue;
        }
        dx = world[i].m[12] - world[g].m[12];
        dy = world[i].m[13] - world[g].m[13];
        dz = world[i].m[14] - world[g].m[14];
        span = sqrtf(dx * dx + dy * dy + dz * dz);
        if (span < 3.0f || span > 14.0f) {
            continue;
        }
        /* yaw 0 faces +z, and her left is +x. The left controller drives that arm. */
        side = world[i].m[12] >= 0.0f ? 0 : 1;
        if (fabsf(world[i].m[12]) <= best[side]) {
            continue;
        }
        best[side] = fabsf(world[i].m[12]);
        sShoulder[side] = g;
        sElbow[side] = p;
        sHandBone[side] = i;   /* the wrist; the palm is the child the hand's skin sits on */
        sArmFound[side] = 1;
    }
    for (k = 0; k < 2; k++) {
        near_sh[k] = sShoulder[k];
        near_el[k] = sElbow[k];
        near_ha[k] = sHandBone[k];
        if (sArmFound[k]) {
            sMeasureBone[k] = sHandBone[k];
            sPalmBone[k] = palm_bone(m, sHandBone[k]);
        }
    }
    /* by the outfit's own table (vrbones.h: vr_bones.txt names the shoulder, the elbow and the
     * wrist - their numbers differ per costume). With no file the table's fallback is the FIN
     * numbers, exactly as the six hard-coded names were. */
    {
        const VrBones *vb = vrbones_for(a);

        named[0] = vb->ok ? vb->bone[VRB_UPPER_L] : -1;
        named[1] = vb->ok ? vb->bone[VRB_UPPER_R] : -1;
        named[2] = vb->ok ? vb->bone[VRB_ELBOW_L] : -1;
        named[3] = vb->ok ? vb->bone[VRB_ELBOW_R] : -1;
        named[4] = vb->ok ? vb->bone[VRB_WRIST_L] : -1;
        named[5] = vb->ok ? vb->bone[VRB_WRIST_R] : -1;
    }
    if (named[0] >= 0 && named[1] >= 0 && named[2] >= 0 && named[3] >= 0 &&
        named[4] >= 0 && named[5] >= 0) {
        for (k = 0; k < 2; k++) {
            sShoulder[k] = named[k];
            sElbow[k] = named[2 + k];
            sHandBone[k] = named[4 + k];
            sPalmBone[k] = named[4 + k];   /* (the wrist is its own palm: the grip's own bone) */
            if (sMeasureBone[k] < 0) {
                sMeasureBone[k] = named[4 + k];   /* (no geometric chain to measure on) */
            }
            sArmFound[k] = 1;
        }
    }
    fprintf(stderr,
            "hands: arms by geometry - L shoulder %d elbow %d wrist %d;"
            " R shoulder %d elbow %d wrist %d\n",
            near_sh[0], near_el[0], near_ha[0], near_sh[1], near_el[1], near_ha[1]);
    fprintf(stderr,
            "hands: arms in use - L shoulder %d elbow %d wrist %d palm %d;"
            " R shoulder %d elbow %d wrist %d palm %d (%s; the hand measured on L%d/R%d, its grip unchanged)\n",
            sShoulder[0], sElbow[0], sHandBone[0], sPalmBone[0],
            sShoulder[1], sElbow[1], sHandBone[1], sPalmBone[1],
            named[0] >= 0 && named[1] >= 0 ? "by name" : "by geometry",
            sMeasureBone[0], sMeasureBone[1]);
}

/* One line a reason, a model: a pose that was skipped must never be silent. */
static void log_skip_once(const Actor *a, const char *why) {
    if (a == NULL) {
        return;
    }
    if (strcmp(sSkipName, a->name) == 0 && strcmp(sSkipWhy, why) == 0) {
        return;
    }
    snprintf(sSkipName, sizeof(sSkipName), "%s", a->name);
    snprintf(sSkipWhy, sizeof(sSkipWhy), "%s", why);
    fprintf(stderr, "hands: VR arms/body off for %s - %s\n", a->name, why);
}

void vrhands_note_skip(const Actor *a, const char *why) {
    log_skip_once(a, why);
}

static void log_no_arm_once(const Actor *a) {
    if (a == NULL || strcmp(sNoArmName, a->name) == 0) {
        return;
    }
    snprintf(sNoArmName, sizeof(sNoArmName), "%s", a->name);
    fprintf(stderr, "hands: arm IK off for %s - no arm chain found (L %d R %d)\n",
            a->name, sArmFound[0], sArmFound[1]);
}

/* Her model is about to go (actor_free) or has just come (actor_load_textured): everything worked
 * out from the old one is dropped, so the new costume's chain, lengths and hand measure are found
 * afresh. This is also what makes a new game log its table again. */
void vrhands_forget_model(void) {
    sArmBones = sArmVerts = 0;
    sArmName[0] = 0;
    sSkipName[0] = 0;
    sSkipWhy[0] = 0;
    sNoArmName[0] = 0;
    sArmFound[0] = sArmFound[1] = 0;
    sPalmBone[0] = sPalmBone[1] = -1;
    sMeasureBone[0] = sMeasureBone[1] = -1;
    sWristT[0] = sWristT[1] = 0.0f;
    sGripCenter[0] = sGripCenter[1] = vec3(0.0f, 0.0f, 0.0f);
    sBendHave[0] = sBendHave[1] = 0;
    sTwistHave[0] = sTwistHave[1] = 0;
    sUpHave[0] = sUpHave[1] = 0;
    sArmLenHave[0] = sArmLenHave[1] = 0;
    sArmLenK[0] = sArmLenK[1] = 0.0f;
    sLenModel = NULL;
    sLenBones = sLenVerts = 0;
    sExtentModel = NULL;
    sExtentVerts = 0;
    sExtent[0] = sExtent[1] = 0.0f;
    sCenter[0] = sCenter[1] = vec3(0.0f, 0.0f, 0.0f);
    sHandLogDone = 0;
}

/* play space matches the camera: +X right, +Y up, -Z forward. `p` is an offset from her eyes. */
static Vec3 play_to_world(Vec3 eye, float yaw, Vec3 p) {
    float sy = sinf(yaw), cy = cosf(yaw);

    return vec3(eye.x + cy * p.x - sy * p.z, eye.y + p.y, eye.z + sy * p.x + cy * p.z);
}

/* the other way: a room point as play space sees it, an offset from her eyes. The ladder's pull
 * reads a hand's place this way, so her own move - the eyes and the yaw the hands sit on - cannot
 * feed back into the measurement. */
static Vec3 play_from_world(Vec3 eye, float yaw, Vec3 p) {
    float sy = sinf(yaw), cy = cosf(yaw), dx = p.x - eye.x, dz = p.z - eye.z;

    return vec3(cy * dx + sy * dz, p.y - eye.y, -sy * dx + cy * dz);
}

static int hand_world(int side, Vec3 *at, Vec3 *right, Vec3 *up, Vec3 *fwd, float *grip, float *index, float *thumb) {
    VrController c;
    Vec3 eye, zero = vec3(0.0f, 0.0f, 0.0f);
    float yaw;

    if (!vr_controller(side, &c) || !c.tracked || !vr_anchor_get(&eye, &yaw)) {
        return 0;
    }
    *at = play_to_world(eye, yaw, c.at);
    *right = vec3_norm(play_to_world(zero, yaw, c.right));
    *up = vec3_norm(play_to_world(zero, yaw, c.up));
    *fwd = vec3_norm(play_to_world(zero, yaw, c.forward));
    *grip = c.grip;
    *index = c.index;
    *thumb = c.thumb;
    return 1;
}

/* a hand's frame in the room, as the arm is posed from it: the wrist and the hand's axes. The same
 * for a thing in that hand, so it is drawn in the hand (loot_draw_from_hands). */
int vrhands_hand_frame(int side, Vec3 *at, Vec3 *right, Vec3 *up, Vec3 *fwd) {
    float grip = 0.0f, index = 0.0f, thumb = 0.0f;

    return hand_world(side, at, right, up, fwd, &grip, &index, &thumb);
}

/* The hand's length in room units, wrist to fingertip: what hand_scale fits it to (HAND_METRES of
 * hand, HAND_SCALE larger; the measured extent only sets the scale that reaches this). */
static float hand_length(void) {
    float body = 1.0f;

    vr_body(NULL, &body);
    if (body < 0.2f) {
        body = 1.0f;
    }
    return HAND_METRES * VR_UNITS_PER_METER / body * HAND_SCALE;
}

/* The arm's place this frame: the controller's own, stopped at the room's mesh (walls, floors and
 * ceilings), from the wrist and from the fingertip - whichever meets a surface first - and slid
 * along that surface where the hand pushes at an angle. The stop is a point in the room, not an
 * offset from her head, so a hand pressed against a wall stays where it touched while she walks or
 * turns; it is let go once the controller has gone a long way from it. The stop is made from the
 * place the arm already had, so a hand is never pulled through a wall; the first frame after the
 * controller is found takes the hand as it is. */
static Vec3 hand_stop(const Engine *e, int side, Vec3 raw, Vec3 fwd) {
    Vec3 stop, next, tip, tip_stop, off;

    if (!sHandStopHave[side]) {
        sHandStop[side] = raw;
        sHandStopHave[side] = 1;
        return raw;   /* (no sweep on the first frame: no pop) */
    }
    stop = sHandStop[side];
    if (vec3_len(vec3_sub(raw, stop)) > HAND_RELEASE) {
        sHandStopHave[side] = 0;   /* (let go: the next look takes the hand as it is) */
        sHandStop[side] = raw;
        return raw;
    }
    off = vec3_scale(fwd, hand_length());
    next = path_slide(&e->room, stop, raw, HAND_STOP);   /* (the wrist's own slide) */
    tip = path_slide(&e->room, vec3_add(stop, off), vec3_add(raw, off), HAND_STOP);
    tip_stop = vec3_sub(tip, off);   /* (the fingertip's stop, taken back to the wrist) */
    /* whichever stop is nearer the place the hand already had: the one that gives way first, so
     * neither the wrist nor the fingertip can be pushed into a surface */
    if (vec3_len(vec3_sub(stop, tip_stop)) < vec3_len(vec3_sub(stop, next))) {
        next = tip_stop;
    }
    sHandStop[side] = next;
    return next;
}

static void v_sub(float *o, const float *a, const float *b) {
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static float v_len(const float *a) {
    return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

static void v_norm(float *o, const float *a) {
    float n = v_len(a);

    if (n < 1.0e-5f) {
        o[0] = 0.0f;
        o[1] = 1.0f;
        o[2] = 0.0f;
        return;
    }
    o[0] = a[0] / n;
    o[1] = a[1] / n;
    o[2] = a[2] / n;
}

static void v_cross(float *o, const float *a, const float *b) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

/* the middle joint, bent toward `pole` in the plane square to the arm, its direction turned at
 * most ELBOW_STEP a frame so it cannot snap between straight and bent.
 * 1 when the target is out of reach. */
static int place_middle(const float *root, float *mid, const float *end, const float *pole,
                        float l1, float l2, int side) {
    float d[3], target[3], dist, a, h, ll, dot, plen, sin_a;

    v_sub(d, end, root);
    dist = v_len(d);
    if (dist < 1.0e-4f) {
        return 1;
    }
    v_norm(d, d);
    if (dist > l1 + l2) {
        dist = l1 + l2;
    }
    /* the pole is where the elbow goes. Near along the arm it is blended toward straight down
     * first, so its part square to the arm is never left undefined (no jump case) */
    plen = v_len(pole);
    dot = pole[0] * d[0] + pole[1] * d[1] + pole[2] * d[2];
    target[0] = pole[0] - dot * d[0];
    target[1] = pole[1] - dot * d[1];
    target[2] = pole[2] - dot * d[2];
    ll = v_len(target);
    sin_a = plen > 1.0e-5f ? ll / plen : 0.0f;
    if (sin_a < ELBOW_ALONG) {
        float k = 1.0f - sin_a / ELBOW_ALONG;
        float p[3];

        p[0] = pole[0] * (1.0f - k);
        p[1] = pole[1] * (1.0f - k) - k;   /* toward straight down */
        p[2] = pole[2] * (1.0f - k);
        dot = p[0] * d[0] + p[1] * d[1] + p[2] * d[2];
        target[0] = p[0] - dot * d[0];
        target[1] = p[1] - dot * d[1];
        target[2] = p[2] - dot * d[2];
    }
    v_norm(target, target);
    /* at most ELBOW_STEP a frame toward it, from last frame's bend for this side */
    if (sBendHave[side]) {
        float cp = sBendWas[side][0] * target[0] + sBendWas[side][1] * target[1] +
                   sBendWas[side][2] * target[2];
        float ang;

        if (cp > 1.0f) {
            cp = 1.0f;
        } else if (cp < -1.0f) {
            cp = -1.0f;
        }
        ang = acosf(cp);
        if (ang > ELBOW_STEP) {
            float t = ELBOW_STEP / ang;
            float s0 = sinf((1.0f - t) * ang) / sinf(ang);
            float s1 = sinf(t * ang) / sinf(ang);
            float v[3];

            v[0] = sBendWas[side][0] * s0 + target[0] * s1;
            v[1] = sBendWas[side][1] * s0 + target[1] * s1;
            v[2] = sBendWas[side][2] * s0 + target[2] * s1;
            /* last frame's bend was square to last frame's arm: square it to this one */
            dot = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
            v[0] -= dot * d[0];
            v[1] -= dot * d[1];
            v[2] -= dot * d[2];
            ll = v_len(v);
            if (ll > 1.0e-4f) {
                v_norm(target, v);
            }
        }
    }
    sBendWas[side][0] = target[0];
    sBendWas[side][1] = target[1];
    sBendWas[side][2] = target[2];
    sBendHave[side] = 1;
    ll = l1 * l1;
    a = (ll - l2 * l2 + dist * dist) / (2.0f * dist);
    h = ll - a * a;
    h = h > 0.0f ? sqrtf(h) : 0.0f;
    mid[0] = root[0] + a * d[0] + h * target[0];
    mid[1] = root[1] + a * d[1] + h * target[1];
    mid[2] = root[2] + a * d[2] + h * target[2];
    return dist >= l1 + l2 - 1.0e-3f;
}

/* X along the given unit `length` axis, Z from `ref` made square to it. A `ref` nearly along the
 * bone leaves the up undefined, so the last good up for that side is used instead (and a fixed
 * one only when that too lies along the bone). */
static void aim_x(Mat4 *m, const float *from, const float *length, const float *ref, int side) {
    float y[3], z[3], ll, rl;

    *m = mat4_identity();
    rl = v_len(ref);
    v_cross(y, ref, length);
    ll = v_len(y);
    if (rl < 1.0e-5f || ll < AIM_PARALLEL * rl) {
        if (sUpHave[side]) {
            y[0] = sUpWas[side][0];
            y[1] = sUpWas[side][1];
            y[2] = sUpWas[side][2];
        } else {
            y[0] = 0.0f;
            y[1] = 1.0f;
            y[2] = 0.0f;
        }
    }
    v_cross(z, length, y);
    if (v_len(z) < 1.0e-4f) {   /* that up lies along the bone too: any square one will do */
        y[0] = fabsf(length[1]) < 0.9f ? 0.0f : 1.0f;
        y[1] = fabsf(length[1]) < 0.9f ? 1.0f : 0.0f;
        y[2] = 0.0f;
        v_cross(z, length, y);
    }
    v_norm(z, z);
    v_cross(y, z, length);
    v_norm(y, y);
    sUpWas[side][0] = y[0];
    sUpWas[side][1] = y[1];
    sUpWas[side][2] = y[2];
    sUpHave[side] = 1;
    m->m[0] = length[0]; m->m[1] = length[1]; m->m[2] = length[2];
    m->m[4] = y[0]; m->m[5] = y[1]; m->m[6] = y[2];
    m->m[8] = z[0]; m->m[9] = z[1]; m->m[10] = z[2];
    m->m[12] = from[0];
    m->m[13] = from[1];
    m->m[14] = from[2];
}

/* X along from->to, Z from `ref` made square to it */
static void aim(Mat4 *m, const float *from, const float *to, const float *ref, int side) {
    float x[3];

    v_sub(x, to, from);
    v_norm(x, x);
    aim_x(m, from, x, ref, side);
}

/* as aim, with the bone's X the other way. The right arm's bones are mirrored in place: their
 * bind X - the length axis - runs with the model's +x, back toward the shoulder, not out along
 * the limb as the left's does. Aiming it outward mirrored the right arm's skin (a stretch). */
static void aim_rev(Mat4 *m, const float *from, const float *to, const float *ref, int side) {
    float x[3];

    v_sub(x, from, to);
    v_norm(x, x);
    aim_x(m, from, x, ref, side);
}

/* turn a bone about its own X - its length - by `angle` (right-handed about +X) */
static void twist_bone(Mat4 *m, float angle) {
    float ca = cosf(angle), sa = sinf(angle);
    float yx = m->m[4], yy = m->m[5], yz = m->m[6];
    float zx = m->m[8], zy = m->m[9], zz = m->m[10];

    m->m[4] = ca * yx + sa * zx;
    m->m[5] = ca * yy + sa * zy;
    m->m[6] = ca * yz + sa * zz;
    m->m[8] = -sa * yx + ca * zx;
    m->m[9] = -sa * yy + ca * zy;
    m->m[10] = -sa * yz + ca * zz;
}

static Mat4 rigid_inverse(const Mat4 *m) {
    Mat4 r = mat4_identity();

    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2] = m->m[8];
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6] = m->m[9];
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10];
    r.m[12] = -(r.m[0] * m->m[12] + r.m[4] * m->m[13] + r.m[8] * m->m[14]);
    r.m[13] = -(r.m[1] * m->m[12] + r.m[5] * m->m[13] + r.m[9] * m->m[14]);
    r.m[14] = -(r.m[2] * m->m[12] + r.m[6] * m->m[13] + r.m[10] * m->m[14]);
    return r;
}

static void hand_measure(const Model *m, int bone, float *extent, Vec3 *center);

/* the arm's two lengths from the bind pose: the inverse of each bone's inv_bind (rigid) gives
 * its bind world position, and the distances go to room units with the actor's own scale, as
 * the placed bones the solver measures are. Times the calibration's arm scale (vrbody_arm_scale:
 * the player's reach over Fiona's, from the VR options' Calibrate Height & Reach), so the arm
 * reaches the controller as a real one would. Computed once per model, as find_arms caches. */
static void arm_lengths(const Actor *a) {
    const Model *m = &a->model;
    float scale = a->scale != 0.0f ? a->scale : 1.0f;
    float k = vrbody_arm_scale(a);
    int side;

    if (sLenModel != m || sLenBones != m->nbones || sLenVerts != m->nv) {
        sLenModel = m;
        sLenBones = m->nbones;
        sLenVerts = m->nv;
        sArmLenHave[0] = sArmLenHave[1] = 0;
    }
    for (side = 0; side < 2; side++) {
        int wrist = sMeasureBone[side], palm = sPalmBone[side] >= 0 ? sPalmBone[side] : sMeasureBone[side];
        Mat4 sh, el, pa, wr;
        float d[3], dpalm, wext;
        Vec3 wc;

        if (!sArmFound[side] || wrist < 0 || (sArmLenHave[side] && sArmLenK[side] == k)) {
            continue;
        }
        sh = rigid_inverse(&m->bones[sShoulder[side]].inv_bind);
        el = rigid_inverse(&m->bones[sElbow[side]].inv_bind);
        pa = rigid_inverse(&m->bones[palm].inv_bind);
        wr = rigid_inverse(&m->bones[wrist].inv_bind);
        d[0] = sh.m[12] - el.m[12];
        d[1] = sh.m[13] - el.m[13];
        d[2] = sh.m[14] - el.m[14];
        sArmLen[side][0] = v_len(d) * scale * k;
        d[0] = el.m[12] - pa.m[12];
        d[1] = el.m[13] - pa.m[13];
        d[2] = el.m[14] - pa.m[14];
        dpalm = v_len(d);
        sArmLen[side][1] = dpalm * scale * k;
        d[0] = el.m[12] - wr.m[12];
        d[1] = el.m[13] - wr.m[13];
        d[2] = el.m[14] - wr.m[14];
        sWristT[side] = palm != wrist && dpalm > 0.05f ? v_len(d) / dpalm : 0.0f;
        /* hand_scale measures the hand's centre in the wrist's space; that point goes into the
         * palm's space, so the grip still centres the hand on the controller through the palm */
        hand_measure(m, wrist, &wext, &wc);
        {
            Vec3 pm = mat4_point(&wr, wc);

            sGripCenter[side] = mat4_point(&m->bones[palm].inv_bind, pm);
        }
        sArmLenHave[side] = 1;
        sArmLenK[side] = k;
    }
}

static int descends(const Model *m, int bone, int root) {
    int n = 0;

    if (bone < 0 || bone >= m->nbones) {
        return 0;
    }
    for (bone = m->bones[bone].parent; bone >= 0 && n < 64; bone = m->bones[bone].parent, n++) {
        if (bone == root) {
            return 1;
        }
    }
    return 0;
}

static void curl_hand(Actor *a, int side, float grip, float index, float thumb) {
    int nshapes = 0, i;
    float w;

    for (i = 0; i < a->model.nmorphs; i++) {
        const ModelMorph *mo = &a->model.morphs[i];

        if (!mo->face && mo->hand == side && mo->nshapes > nshapes) {
            nshapes = mo->nshapes;
        }
    }
    if (nshapes < 2) {
        return;
    }
    /* shape 0 is the open hand, 1 the fist, and 2 a point when the mesh has it */
    if (index < 0.35f && grip > 0.45f && nshapes > 2) {
        a->face.hand[side].a = 2;
        a->face.hand[side].b = 2;
        a->face.hand[side].w = 0.0f;
        return;
    }
    w = index < 0.35f ? grip * 0.45f : (index > grip ? index : grip);
    if (thumb < 0.5f) {
        w *= 0.75f;   /* thumb off the stick opens the hand back out */
    }
    if (w > 1.0f) {
        w = 1.0f;
    }
    a->face.hand[side].a = 0;
    a->face.hand[side].b = 1;
    a->face.hand[side].w = w;
}

/* wrist-to-fingertip reach, and where the mesh sits in this bone's space. Fiona's left hand
 * runs out along +X and her right along -X; Y is the thin palm axis. */
static void hand_measure(const Model *m, int bone, float *extent, Vec3 *center) {
    Vec3 sum = vec3(0.0f, 0.0f, 0.0f);
    float maxd = 0.0f;
    int i, k, n = 0;

    for (i = 0; i < m->nv; i++) {
        for (k = 0; k < 4; k++) {
            int b = m->v[i].bones[k];
            Vec3 local;
            float d;

            if (m->v[i].weights[k] < 0.15f || b < 0 || b >= m->nbones) {
                continue;
            }
            if (b != bone && !descends(m, b, bone)) {
                continue;
            }
            local = mat4_point(&m->bones[bone].inv_bind, m->v[i].pos);
            d = vec3_len(local);
            if (d > maxd) {
                maxd = d;
            }
            sum = vec3_add(sum, local);
            n++;
            break;
        }
    }
    *extent = maxd;
    *center = n > 0 ? vec3_scale(sum, 1.0f / (float)n) : vec3(0.0f, 0.0f, 0.0f);
}

/* fitted wrist-to-fingertip size, then HAND_SCALE. Recomputed when the mesh is replaced. */
static float hand_scale(const Actor *a, int side, int bone) {
    float body = 1.0f, extent, s;

    if (bone < 0 || bone >= a->model.nbones) {
        return 1.0f;   /* (no bone to measure on: as the model's own hand) */
    }
    if (sExtentModel != &a->model || sExtentVerts != a->model.nv) {
        sExtentModel = &a->model;
        sExtentVerts = a->model.nv;
        sExtent[0] = sExtent[1] = 0.0f;
    }
    if (sExtent[side] <= 0.0f) {
        hand_measure(&a->model, bone, &sExtent[side], &sCenter[side]);
    }
    extent = sExtent[side];
    vr_body(NULL, &body);
    if (body < 0.2f) {
        body = 1.0f;
    }
    if (extent < 0.05f) {
        s = 2.2f;
    } else {
        s = (HAND_METRES * VR_UNITS_PER_METER / body) / extent;
        if (s < 1.0f) {
            s = 1.0f;
        }
    }
    return s * HAND_SCALE;
}

/* grip pose, every frame after the OpenXR grip is written into the skeleton.
 * With the palm facing inward and the fingers forward, grip -Z (fwd) points at the thumb,
 * straight up, and grip +Y points backward. Bone Y is the palm axis and is left alone, so
 * neither palm turns outward or over.
 * The left mesh fingers run along +X, so that axis turns onto -grip +Y (forward) and bone Z
 * takes the old thumb axis (up). The right mesh fingers run along -X and its palm faces the
 * other way, so it turns 180° about grip +Y first, then the opposite quarter turn: bone +X
 * lands on +grip +Y and the fingers still point forward. */
static Mat4 grip_matrix(Vec3 at, Vec3 fwd, Vec3 up, float scale, Vec3 center, int side) {
    Vec3 x = vec3_norm(fwd);
    Vec3 z = vec3_norm(up);
    Vec3 y = vec3_norm(vec3_cross(z, x));
    Vec3 ox, seat;
    Mat4 m = mat4_identity();

    z = vec3_norm(vec3_cross(x, y));
    if (side == 1) {
        x = vec3_scale(x, -1.0f);
        y = vec3_scale(y, -1.0f);
    }
    ox = x;
    if (side == 0) {
        x = vec3_scale(z, -1.0f);
        z = ox;
    } else {
        x = z;
        z = vec3_scale(ox, -1.0f);
    }
    seat = vec3_add(vec3_add(vec3_scale(x, center.x * scale), vec3_scale(y, center.y * scale)),
                    vec3_scale(z, center.z * scale));
    at = vec3_sub(at, seat);
    m.m[0] = x.x * scale; m.m[1] = x.y * scale; m.m[2] = x.z * scale;
    m.m[4] = y.x * scale; m.m[5] = y.y * scale; m.m[6] = y.z * scale;
    m.m[8] = z.x * scale; m.m[9] = z.y * scale; m.m[10] = z.z * scale;
    m.m[12] = at.x;
    m.m[13] = at.y;
    m.m[14] = at.z;
    return m;
}

/* The arm: the shoulder, the elbow and the hand, from the grip. The hand's own pose is the grip
 * matrix, exactly as it was (its descendants carried with it). When the VR body is on (vrbody.h,
 * section 5) the elbow and the wrist's twist come from FRIK's solve; else the body's fixed bend
 * and place_middle's smoothed pole, as before. */
static void pose_arm(Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace, int side,
                     const Mat4 *hand, float hs) {
    const Model *m = &a->model;
    int sh = sShoulder[side], el = sElbow[side];
    int wr = sHandBone[side], pa = sPalmBone[side] >= 0 ? sPalmBone[side] : sHandBone[side];
    Mat4 room[3], grip = *hand, old;
    float root[3], mid[3], pole[3], l1, l2, yaw;
    float k = vrbody_arm_scale(a);    /* (body x arm: the segment's length in the drawn pose) */
    float kb = vrbody_body_scale(a);  /* (her body's own scale: the arm's thickness) */
    VrArm arm;
    int i, solved = 0;

    if (!sArmFound[side] || sh < 0 || el < 0 || wr < 0 || wr >= m->nbones || pa < 0 || pa >= m->nbones) {
        return;
    }
    if (!vr_anchor_get(NULL, &yaw)) {
        return;
    }
    room[0] = mat4_mul(*place, world[sh]);
    room[1] = mat4_mul(*place, world[el]);
    room[2] = mat4_mul(*place, world[wr]);
    root[0] = room[0].m[12]; root[1] = room[0].m[13]; root[2] = room[0].m[14];
    mid[0] = room[1].m[12]; mid[1] = room[1].m[13]; mid[2] = room[1].m[14];
    memset(&arm, 0, sizeof(arm));
    if (vrbody_arm(a, side, root, &grip.m[12], &grip, &arm) && !arm.skip) {
        /* FRIK's elbow: the law of cosines in the plane of the shoulder -> target and the bend's
         * own direction; 27 -> 28 stays a pure hinge, as the PS2 motions have it */
        float reach[3];
        int j;

        for (j = 0; j < 3; j++) {
            reach[j] = arm.reach[j];
            grip.m[12 + j] = reach[j];
        }
        if (side == 0) {
            aim(&room[0], root, arm.elbow, arm.pole, side);
            aim(&room[1], arm.elbow, reach, arm.pole, side);
        } else {
            aim_rev(&room[0], root, arm.elbow, arm.pole, side);
            aim_rev(&room[1], arm.elbow, reach, arm.pole, side);
        }
        /* The drawn arm spans exactly the solved chain: its two bones take the body's uniform scale
         * on Y and Z (their thickness) and the body's scale with the small arm residual on X (their
         * length). No arm bone is stretched beyond that clamp, and the hand is still exactly the
         * controller's - FRIK's arm length, not a stretched one. */
        {   /* (indices: X is 0..2, Y 4..6, Z 8..10 in the column-major Mat4) */
            int c;

            for (c = 0; c < 3; c++) {
                room[0].m[c] *= k;      room[0].m[4 + c] *= kb;   room[0].m[8 + c] *= kb;
                room[1].m[c] *= k;      room[1].m[4 + c] *= kb;   room[1].m[8 + c] *= kb;
            }
        }
        solved = 1;
    }
    l1 = sArmLen[side][0];   /* (the fallback's: the bind pose's, not this animation's) */
    l2 = sArmLen[side][1];
    if (!solved && !arm.skip && l1 > 0.05f && l2 > 0.05f) {
        /* the bend is in her body's frame (yaw 0 along +z, her left +x), not the headset's:
         * down and out on that side, a little behind her, so the elbows point out and back */
        float out_sign = side == 0 ? 1.0f : -1.0f;
        float bend_s = sinf(a->yaw), bend_c = cosf(a->yaw);
        Vec3 bend = vec3(bend_c * out_sign + bend_s * ELBOW_BACK, -1.0f,
                         -bend_s * out_sign + bend_c * ELBOW_BACK);
        float reach[3] = {grip.m[12], grip.m[13], grip.m[14]};
        float d[3];
        float dist;

        pole[0] = bend.x;
        pole[1] = bend.y;
        pole[2] = bend.z;
        /* out of reach: the palm target is pulled onto the arm's length along its own way from
         * the shoulder - the grip's seat-adjusted point, which carries the hand's built-in
         * offset - so the clamp begins exactly where the palm is and nothing jumps. Only the
         * grip's translation moves; its rotation and scale stay as they are. */
        v_sub(d, reach, root);
        dist = v_len(d);
        if (dist > l1 + l2 && dist > 1.0e-4f) {
            float shrink = (l1 + l2) * 0.999f / dist;   /* (not the calibration's k: this is the clamp) */

            reach[0] = root[0] + d[0] * shrink;
            reach[1] = root[1] + d[1] * shrink;
            reach[2] = root[2] + d[2] * shrink;
            grip.m[12] = reach[0];
            grip.m[13] = reach[1];
            grip.m[14] = reach[2];
        }
        place_middle(root, mid, reach, pole, l1, l2, side);
        if (side == 0) {
            aim(&room[0], root, mid, sBendWas[side], side);
            aim(&room[1], mid, reach, sBendWas[side], side);
        } else {
            /* the right arm's bind X runs back toward the shoulder: aiming it outward mirrored
             * its skin (the stretch); the reversed aim keeps the bone's length axis as it was */
            aim_rev(&room[0], root, mid, sBendWas[side], side);
            aim_rev(&room[1], mid, reach, sBendWas[side], side);
        }
        /* the arm is longer than Fiona's model's: lengthen the upper arm and forearm along the
         * bone (X) only; Y and Z, the thickness, stay as they are, so the joints still meet */
        room[0].m[0] *= k; room[0].m[1] *= k; room[0].m[2] *= k;
        room[1].m[0] *= k; room[1].m[1] *= k; room[1].m[2] *= k;
        room[0].m[4] *= kb; room[0].m[5] *= kb; room[0].m[6] *= kb;
        room[0].m[8] *= kb; room[0].m[9] *= kb; room[0].m[10] *= kb;
        room[1].m[4] *= kb; room[1].m[5] *= kb; room[1].m[6] *= kb;
        room[1].m[8] *= kb; room[1].m[9] *= kb; room[1].m[10] *= kb;
        if (pa != wr) {
            /* the wrist on the forearm's line from the elbow to the palm, the bind pose's share */
            float wp[3];

            wp[0] = mid[0] + (reach[0] - mid[0]) * sWristT[side];
            wp[1] = mid[1] + (reach[1] - mid[1]) * sWristT[side];
            wp[2] = mid[2] + (reach[2] - mid[2]) * sWristT[side];
            if (side == 0) {
                aim(&room[2], wp, reach, pole, side);
            } else {
                aim_rev(&room[2], wp, reach, pole, side);
            }
            /* the controller's twist about the forearm, shared along the arm below: the palm
             * keeps the controller's own rotation and the forearm and wrist take their shares */
            {
                float px = grip.m[4], py = grip.m[5], pz = grip.m[6];
                float plen = sqrtf(px * px + py * py + pz * pz);
                float xx = room[2].m[0], xy = room[2].m[1], xz = room[2].m[2];
                float yx = room[2].m[4], yy = room[2].m[5], yz = room[2].m[6];

                if (plen > 1.0e-5f) {
                    float dot, nx, ny, nz, nl;

                    px /= plen; py /= plen; pz /= plen;
                    dot = px * xx + py * xy + pz * xz;   /* the palm's Y square to the forearm */
                    nx = px - dot * xx;
                    ny = py - dot * xy;
                    nz = pz - dot * xz;
                    nl = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nl > 1.0e-4f) {
                        float sin_a = ((yy * nz - yz * ny) * xx + (yz * nx - yx * nz) * xy +
                                       (yx * ny - yy * nx) * xz) / nl;
                        float cos_a = (yx * nx + yy * ny + yz * nz) / nl;
                        float twist = atan2f(sin_a, cos_a);

                        /* the nearest turn to last frame's: the angle does not flip at +-180 */
                        if (sTwistHave[side]) {
                            while (twist - sTwistWas[side] > 3.14159265f) {
                                twist -= 6.28318531f;
                            }
                            while (twist - sTwistWas[side] < -3.14159265f) {
                                twist += 6.28318531f;
                            }
                        } else {
                            sTwistHave[side] = 1;
                        }
                        sTwistWas[side] = twist;
                        /* spread it: the forearm turns by its share through its own aim, the
                         * wrist by its own share next, and the palm keeps the rest (its own
                         * controller rotation) */
                        twist_bone(&room[1], TWIST_FOREARM * twist);
                        twist_bone(&room[2], (TWIST_FOREARM + TWIST_WRIST) * twist);
                    }
                }
            }
            /* The wrist is the hand's, so the arm's and the body's scale stop here: its own local
             * scale is 1 over what it inherited (the arm's and every scale up to it), which leaves
             * its world scale exactly the hand's own - hs, the size the IK has always drawn it - and
             * the palm, the fingers and a held item under it follow it, their own bone-local frames
             * untouched. */
            {
                float nr = sqrtf(room[2].m[0] * room[2].m[0] + room[2].m[1] * room[2].m[1] +
                                room[2].m[2] * room[2].m[2]);
                float f = nr > 1.0e-6f ? hs / nr : 1.0f;
                int c;

                for (c = 0; c < 3; c++) {
                    room[2].m[c] *= f;
                    room[2].m[4 + c] *= f;
                    room[2].m[8 + c] *= f;
                }
            }
            world[wr] = mat4_mul(*unplace, room[2]);
        }
        solved = 1;
    }
    /* The arm's bones go into the pose last, whichever solve made them: the VR body's elbow or the
     * fallback's. (This write used to live inside the fallback's block, so a solved arm was left at
     * the animation's pose - the shoulders by her sides while the hand reached the controller.) */
    if (solved) {
        world[sh] = mat4_mul(*unplace, room[0]);
        world[el] = mat4_mul(*unplace, room[1]);
    }
    old = world[pa];
    world[pa] = mat4_mul(*unplace, grip);
    {
        /* The palm's frame is the controller's, and everything under it - the fingers, their
         * skin, a held item - takes the hand's own size: the delta is a similarity with
         * hs / (the scale the palm's frame had) in it, so every bone below keeps its bone-local
         * frame exactly (rotations and bind offsets) and comes out at hs, whatever the arm's and
         * the body's scales were. */
        Mat4 od = old, nd = world[pa], delta;
        float sb = sqrtf(od.m[0] * od.m[0] + od.m[1] * od.m[1] + od.m[2] * od.m[2]);
        float sn = sqrtf(nd.m[0] * nd.m[0] + nd.m[1] * nd.m[1] + nd.m[2] * nd.m[2]);
        float f;
        int c;

        if (sb < 1.0e-6f) {
            sb = 1.0f;
        }
        if (sn < 1.0e-6f) {
            sn = 1.0f;
        }
        f = sn / sb;
        for (c = 0; c < 3; c++) {   /* both frames as pure rotations */
            od.m[c] /= sb;
            od.m[4 + c] /= sb;
            od.m[8 + c] /= sb;
            nd.m[c] /= sn;
            nd.m[4 + c] /= sn;
            nd.m[8 + c] /= sn;
        }
        delta = mat4_mul(nd, rigid_inverse(&od));   /* the move: a rotation and a place */
        for (c = 0; c < 3; c++) {                    /* and the hand's own size in it */
            delta.m[c] *= f;
            delta.m[4 + c] *= f;
            delta.m[8 + c] *= f;
            delta.m[12 + c] = (1.0f - f) * world[pa].m[12 + c] + f * delta.m[12 + c];
        }
        for (i = 0; i < m->nbones; i++) {
            if (descends(m, i, pa)) {
                world[i] = mat4_mul(delta, world[i]);
            }
        }
    }
}

static void log_hands(float left, float right, int posed) {
    (void)left;
    (void)right;
    (void)posed;   /* (silent: the hand-fit line is off) */
}

/* The real time since the last posed frame, for the grip poses' blend (not the game's fixed
 * tick: a headset draws at its own rate). 0 until there is a previous frame. */
static Uint64 sGripClock;

static float grip_pose_dt(void) {
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 f = SDL_GetPerformanceFrequency();
    float dt = 0.0f;

    if (sGripClock != 0 && f > 0) {
        dt = (float)(now - sGripClock) / (float)f;
    }
    sGripClock = now;
    return dt;
}

static void grip_pose_forget_clock(void) {
    /* (the next posed frame starts a new span: a cutscene or a menu is not a long frame) */
    sGripClock = 0;
}

/* What each hand wants from PROPS/grip_poses.txt this frame: the item in the hand (loot, or the
 * equipped magnesia/antimony shown there) and the pose its grip line names. Nothing else. */
static void grip_pose_want(int side) {
    int item = -1;
    ItemGrip g;

    if (sHold[side] == HOLD_LOOT) {
        item = loot_item_of(sHoldId[side]);
    }
    if (item < 0 && sDef.shown && sDef.side == side) {
        item = sDef.item;
    }
    if (item < 0 && sHand.shown && sHand.side == side) {
        item = sHand.item;   /* (the key item the menu equipped in her hand) */
    }
    if (item >= 0 && itemprops_grip(item, side == 0 ? 30 : 46, &g) && g.pose[0] != 0) {
        vrgrip_want(side, g.pose, item);
    } else {
        vrgrip_want(side, NULL, item);
    }
}

void vrhands_pose(Actor *a, Mat4 *world, const Mat4 *place, const Mat4 *unplace) {
    float applied[2] = {0.0f, 0.0f};
    int posed_side[2] = {0, 0};
    int side, posed = 0;
    float dt;
#if VR_STEP_LOG
    Uint64 t_pose = SDL_GetPerformanceCounter();   /* (temporary: the pose's own time) */
#endif

    if (a == NULL || !a->used || strncmp(a->name, "O_FI", 4) != 0) {
        /* Another actor's model. The VR body and the grip state are Fiona's: idling them here
         * (as this did for every hooked actor - Hewie, a stalker, a figure) turned her body off
         * and restarted her grip pose every frame. Leave them alone. */
        return;
    }
    if (!vr_active()) {
        log_skip_once(a, "VR is not active");
        vrbody_idle();
        vrgrip_idle();
        grip_pose_forget_clock();
#if VR_STEP_LOG
        pose_us += perf_us(t_pose, SDL_GetPerformanceCounter());
#endif
        return;
    }
    if (!a->visible || world == NULL) {
        log_skip_once(a, !a->visible ? "she is not shown" : "the model-space world is missing");
        vrbody_idle();
        vrgrip_idle();
        grip_pose_forget_clock();
#if VR_STEP_LOG
        pose_us += perf_us(t_pose, SDL_GetPerformanceCounter());
#endif
        return;
    }
    if (sHandRoom != gEngine.room.id) {
        /* Another room: the arm stops and the body's smoothed hands are places in the room she
         * left (the tick's room branch may not have run yet on this drawn frame). */
        sHandRoom = gEngine.room.id;
        sHandStopHave[0] = sHandStopHave[1] = 0;
        vrbody_forget_place();
    }
    dt = grip_pose_dt();
    if (!vrbones_for(a)->ok) {
        /* No per-outfit table (no vr_bones.txt, and not the FIN layout): her own animation plays
         * exactly as it does in a cutscene - no arm IK, no VR body, not one matrix twisted. The
         * grip poses still run: they find the finger bones by name. */
        const VrBones *vb = vrbones_for(a);

        log_skip_once(a, vb->from_file ? "its vr_bones.txt does not match the skeleton" :
                                         "no usable vr_bones.txt (and not the FIN layout)");
        vrbody_idle();
        sArmFound[0] = sArmFound[1] = 0;   /* (no stale arm: the key item is not shown at one) */
        for (side = 0; side < 2; side++) {
            Vec3 at, right, up, fwd;
            float grip, index, thumb;

            if (hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
                grip_pose_want(side);
            } else {
                vrgrip_want(side, NULL, -1);
            }
        }
        vrgrip_apply(a, world, dt);
        log_hands(applied[0], applied[1], posed);
        return;
    }
    find_arms(a, world);
    if (!sArmFound[0] || !sArmFound[1]) {
        log_no_arm_once(a);
    }
    arm_lengths(a);
    /* the VR body (vrbody.h): its yaw, and the body under the headset with the feet kept. Off
     * wherever the arms are off (a cutscene, a scene, no anchor): then not one matrix is touched. */
    vrbody_begin(a, world, place, unplace);
    vrbody_body(a, world, place, unplace);
    for (side = 0; side < 2; side++) {
        Vec3 at, right, fwd, up;
        Mat4 hand;
        float grip, index, thumb, hs;

        if (!sArmFound[side] || !hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            continue;
        }
        (void)right;
        sStopRaw[side] = at;                    /* (temporary: the stop is holding the hand back) */
#if VR_STEP_LOG
        {   /* (temporary: how long the stop takes, both hands adding up) */
            Uint64 t_stop = SDL_GetPerformanceCounter();

            at = hand_stop(&gEngine, side, at, fwd);   /* the arm's place: the controller's, stopped at the room */
            stop_us += perf_us(t_stop, SDL_GetPerformanceCounter());
        }
#else
        at = hand_stop(&gEngine, side, at, fwd);   /* the arm's place: the controller's, stopped at the room */
#endif
        {   /* (temporary: the stop is holding the hand back) */
            int held = vec3_len(vec3_sub(sStopRaw[side], at)) > 0.05f;

            if (held && !sStopHeld[side]) {
                if (STOP_PULSE) {
                    vr_haptic(side, 0.6f, 0.02f);
                }
            }
            sStopHeld[side] = held;
        }
        /* FRIK's DampenHands: only the body's pose is smoothed. What a door, a ladder or a grab
         * reads is the raw controller (vrhands_tick), so none of them changes. */
        vrbody_dampen(side, &at, &fwd, &up);
        applied[side] = hs = hand_scale(a, side, sMeasureBone[side]);
        if (!sHandLogDone) {   /* once: the two scales that must never reach the hand, and the one
                                * that is the hand's own (the IK's size, unchanged) */
            float bs = vrbody_body_scale(a), tot = vrbody_arm_scale(a);

            sHandLogDone = 1;
            fprintf(stderr, "hands: body_scale %.3f, arm_scale %.3f, hand world scale %.3f (the original IK's own, unchanged)\n", (double)bs,
                    (double)(bs > 1.0e-6f ? tot / bs : 1.0f), (double)hs);
        }
        hand = grip_matrix(at, fwd, up, hs, sGripCenter[side], side);
        vrbody_shoulders(a, world, place, unplace, side, &hand.m[12]);   /* (her arm's root where the player's is) */
        vrbody_clavicle(a, world, unplace, side, &hand.m[12]);
        pose_arm(a, world, place, unplace, side, &hand, hs);
        curl_hand(a, side, grip, index, thumb);
        posed_side[side] = 1;
        posed++;
    }
    /* the forearm's own roll, the upper arm's twist and the deltoid, the first-person head */
    vrbody_head_camera(a, world, place, unplace);   /* (her eyes at the camera, the neck stretching to them) */
    vrbody_finish(a, world, place, unplace);
    /* last: the mod's finger grip poses on the model's own finger bones. A hand that was not
     * tracked this frame has no pose (its bones are the animation's). */
    for (side = 0; side < 2; side++) {
        if (posed_side[side]) {
            grip_pose_want(side);
        } else {
            vrgrip_want(side, NULL, -1);
        }
    }
    vrgrip_apply(a, world, dt);
    log_hands(applied[0], applied[1], posed);
#if VR_STEP_LOG
    pose_us += perf_us(t_pose, SDL_GetPerformanceCounter());
#endif
}

static float wrap_deg(float d) {
    while (d > 180.0f) {
        d -= 360.0f;
    }
    while (d < -180.0f) {
        d += 360.0f;
    }
    return d;
}

static float wrap_rad(float a) {
    while (a > 3.14159265f) {
        a -= 6.28318531f;
    }
    while (a < -3.14159265f) {
        a += 6.28318531f;
    }
    return a;
}

/* the same yaw the leaf is drawn with: local +X is (cos y, -sin y), local +Z is (sin y, cos y) */
static float swing_of(const RoomDoor *d, Vec3 hand, int axis) {
    float dx = hand.x - d->pos.x, dz = hand.z - d->pos.z;
    float ang = axis ? atan2f(dx, dz) : atan2f(-dz, dx);

    return wrap_deg((ang - d->rot.y) * (180.0f / 3.14159265f));
}

static float clamp_swing(float s) {
    if (s > 90.0f) {
        return 90.0f;
    }
    if (s < -90.0f) {
        return -90.0f;
    }
    return s;
}

#define DOOR_KNOB 10.0f   /* the knob's distance from its hinge, in the leaf's own frame (handle_at) */

static Vec3 handle_at(const RoomDoor *d, int axis) {
    float y = d->rot.y + d->swing * (3.14159265f / 180.0f);

    if (axis) {
        return vec3(d->pos.x + sinf(y) * DOOR_KNOB, d->pos.y + 11.0f, d->pos.z + cosf(y) * DOOR_KNOB);
    }
    return vec3(d->pos.x + cosf(y) * DOOR_KNOB, d->pos.y + 11.0f, d->pos.z - sinf(y) * DOOR_KNOB);
}

static float dist2(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;

    return dx * dx + dy * dy + dz * dz;
}

#define DOOR_TOUCH 4.0f     /* world units: reach for a knob */
#define DOOR_TOUCH_AWAY 6.5f /* ... and over the doorway's line for a knob past it, the leaf swung
                              * away from the hand: the knob is then a step into the room beyond, so
                              * the reach over the threshold is longer */
/* the widest a knob may be taken at: the reach over the line, plus the knob's own way past it */
#define DOOR_REACH_MAX (DOOR_TOUCH_AWAY + DOOR_KNOB)
/* the hand's reach for its own door after a room change: the knob under it, with the arrival's own
 * move allowed for (the room is another frame, and she may have stepped while crossing) */
#define DOOR_CARRY_REACH 10.0f
#define SWING_OPEN 8.0f     /* degrees: a leaf off its latch at all */
#define DOOR_LATCH 1.0f     /* within this of shut, only a gripped knob moves it */
#define DOOR_HALF  45.0f    /* past this far open, either way, a slow release finishes open */
#define DOOR_WIDE  90.0f    /* open stop, both directions */
#define DOOR_SHUT  0.0f
#define COAST_SPEED 1.5f    /* degrees a frame: a release faster than this keeps going */
#define DOOR_MOVING 0.5f    /* degrees a tick: a leaf moving more than this is travelling - its way
                             * decides the stop a room change gives it (a hand's tracking noise is
                             * under it) */
#define PUSH_GRACE  12      /* frames the throwing hand ignores the leaf */
#define RATTLE_SOUND 0x71u  /* Fiona's bank: the locked handle */
#define RATTLE_BANK  5
#define LADDER_RUNG_SOUND 0x14u   /* the base game's rung sound (scripts/fiona/moving.fs, rungs-sound) */
#define LADDER_RUNG_BANK 4        /* Fiona's bank for it (moving.fs: rungs-sound 4 0 0) */

/* the stop a leaf let go at `s` finishes at (the 45° rule): started past 45, it goes on to the open
 * stop that way; else it shuts */
static float door_stop(float s) {
    if (s < -DOOR_HALF) {
        return -DOOR_WIDE;
    }
    if (s > DOOR_HALF) {
        return DOOR_WIDE;
    }
    return DOOR_SHUT;
}

/* The stop a leaf at `s`, last travelling `v` degrees a tick, finishes at: a leaf that was moving
 * goes on the way it was going, as a throw's coast does; a leaf let go still takes the 45° rule. */
static float door_stop_for(float s, float v) {
    if (v <= -DOOR_MOVING) {
        return s > DOOR_SHUT ? DOOR_SHUT : -DOOR_WIDE;
    }
    if (v >= DOOR_MOVING) {
        return s < DOOR_SHUT ? DOOR_SHUT : DOOR_WIDE;
    }
    return door_stop(s);
}

static Mat4 door_place_at(const RoomDoor *d, float swing) {
    float y = d->rot.y + swing * 3.14159265f / 180.0f;
    float cx = cosf(d->rot.x), sx = sinf(d->rot.x), cy = cosf(y), sy = sinf(y);
    float cz = cosf(d->rot.z), sz = sinf(d->rot.z);
    Mat4 rx = mat4_identity(), ry = mat4_identity(), rz = mat4_identity(), t = mat4_identity();

    rx.m[5] = cx;  rx.m[6] = sx;  rx.m[9] = -sx;  rx.m[10] = cx;
    ry.m[0] = cy;  ry.m[2] = -sy; ry.m[8] = sy;   ry.m[10] = cy;
    rz.m[0] = cz;  rz.m[1] = sz;  rz.m[4] = -sz;  rz.m[5] = cz;
    t.m[12] = d->pos.x;
    t.m[13] = d->pos.y;
    t.m[14] = d->pos.z;
    return mat4_mul(t, mat4_mul(rz, mat4_mul(ry, rx)));
}

static Mat4 door_place(const RoomDoor *d) { return door_place_at(d, d->swing); }

static int in_box(Vec3 p, Vec3 lo, Vec3 hi, float margin) {
    return p.x >= lo.x - margin && p.x <= hi.x + margin && p.y >= lo.y - margin && p.y <= hi.y + margin &&
           p.z >= lo.z - margin && p.z <= hi.z + margin;
}

/* the nearer of a door's two knobs within `reach` of the hand; *axis which (0 / 1) */
static int knob_near(const RoomDoor *d, Vec3 hand, float reach, int *axis) {
    float a0 = dist2(hand, handle_at(d, 0));
    float a1 = dist2(hand, handle_at(d, 1));
    float r2 = reach * reach;

    if (a0 > r2 && a1 > r2) {
        return 0;
    }
    *axis = a0 <= a1 ? 0 : 1;
    return 1;
}

/* The nearer knob within this hand's reach - the plain one, unless the leaf is swung away from this
 * side of the doorway and the knob is past it. The reach is then measured over the doorway's line,
 * not the knob: the knob's own way past the line (DOOR_KNOB sin swing) is added, so a leaf wide
 * open is as reachable from the near side as a shut one - no step through the threshold first. */
static int touching_knob(const Engine *e, int exit, const RoomDoor *d, Vec3 hand, int *axis) {
    float a0 = dist2(hand, handle_at(d, 0));
    float a1 = dist2(hand, handle_at(d, 1));
    int ax = a0 <= a1 ? 0 : 1;
    float reach = DOOR_TOUCH;

    if ((d->swing <= -SWING_OPEN || d->swing >= SWING_OPEN) &&
        room_door_side(&e->room, exit, handle_at(d, ax)) != room_door_side(&e->room, exit, hand)) {
        reach = DOOR_TOUCH_AWAY + DOOR_KNOB * fabsf(sinf(d->swing * (3.14159265f / 180.0f)));
    }
    if ((ax == 0 ? a0 : a1) > reach * reach) {
        return 0;
    }
    *axis = ax;
    return 1;
}

static int door_index(const Engine *e, int exit) {
    int room = e->room.id;

    if (room < 0 || room >= WORLD_ROOMS || exit < 0 || exit >= ROOM_EXITS) {
        return -1;
    }
    return e->world.exits[room][exit].door;
}

/* a locked door, a doorway, or one a character already holds: the hands leave it alone */
static int door_blocked(const Engine *e, int exit) {
    int d = door_index(e, exit);
    uint32_t state;

    if (d < 0 || d >= DOORS || d >= e->world.ndoors) {
        return 1;
    }
    if (e->world.doors[d].flags & 1u) {
        return 1;
    }
    state = gProgress.doors[d];
    return (state & 8u) != 0 || (state & 1u) != 0;
}

static int door_locked(const Engine *e, int exit) {
    int d = door_index(e, exit);

    if (d < 0 || d >= DOORS || d >= e->world.ndoors) {
        return 0;
    }
    return (gProgress.doors[d] & 8u) != 0;
}

/* the swing that is about to finish is held, so the doors actor's let-go writes the open bit
 * and the passage when it comes to rest. A stuck door is not marked held on the way open. */
static void claim_for_letgo(Engine *e, int exit, int open) {
    int d = door_index(e, exit);
    uint32_t state;

    if (d < 0 || d >= DOORS) {
        return;
    }
    state = gProgress.doors[d];
    if ((state & 8u) != 0 || ((state & 4u) != 0 && open)) {
        return;
    }
    gProgress.doors[d] = state | 1u;
    sDoorClaim[d] = 1;   /* (this side's own claim: a room change may take it back) */
}

static Vec3 door_local_at(const RoomDoor *d, Vec3 hand, float swing) {
    Mat4 place = door_place_at(d, swing);
    Mat4 inv = rigid_inverse(&place);

    return mat4_point(&inv, hand);
}

static Vec3 door_local(const RoomDoor *d, Vec3 hand) { return door_local_at(d, hand, d->swing); }

/* the segment crosses the leaf, even when a fast hand would skip the wood between frames */
static int seg_leaf(Vec3 a, Vec3 b, float *tHit) {
    float o[3] = {a.x, a.y, a.z};
    float dir[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    float lo[3] = {0.0f, 0.0f, -1.0f};
    float hi[3] = {12.0f, 22.0f, 0.0f};
    float t0 = 0.0f, t1 = 1.0f;
    int i;

    for (i = 0; i < 3; i++) {
        if (dir[i] > -1.0e-5f && dir[i] < 1.0e-5f) {
            if (o[i] < lo[i] || o[i] > hi[i]) {
                return 0;
            }
        } else {
            float inv = 1.0f / dir[i];
            float ta = (lo[i] - o[i]) * inv;
            float tb = (hi[i] - o[i]) * inv;

            if (ta > tb) {
                float s = ta;
                ta = tb;
                tb = s;
            }
            if (ta > t0) {
                t0 = ta;
            }
            if (tb < t1) {
                t1 = tb;
            }
            if (t0 > t1) {
                return 0;
            }
        }
    }
    *tHit = t0;
    return 1;
}

/* a shove moves the leaf with the hand. A fast hand's whole step counts, so it cannot
 * pass through without the door taking that speed. The leaf stops at the latch.
 * `speed` is the hand's own angular step, before the slow-contact minimum, so a
 * release can coast with that speed the way a gripped throw does. */
static int push_step(const RoomDoor *d, Vec3 prev, Vec3 curr, int have_prev, float *delta, float *speed) {
    Vec3 a = have_prev ? door_local(d, prev) : door_local(d, curr);
    Vec3 b = door_local(d, curr);
    float t = 0.0f, x, dz, vel, side, step, next;
    int hit = seg_leaf(a, b, &t);

    if (!hit && !in_box(b, vec3(0.0f, 0.0f, -1.0f), vec3(12.0f, 22.0f, 0.0f), 0.15f)) {
        return 0;
    }
    x = a.x + (b.x - a.x) * t;
    if (x < 2.0f) {
        x = 2.0f;
    }
    dz = b.z - a.z;
    vel = -(dz) * (180.0f / 3.14159265f) / x;
    side = (a.z + (b.z - a.z) * t) + 0.5f;
    if (side > -0.05f && side < 0.05f) {
        side = b.z + 0.5f;
    }
    step = vel;
    if (fabsf(step) < 5.0f && in_box(b, vec3(0.0f, 0.0f, -1.0f), vec3(12.0f, 22.0f, 0.0f), 0.15f)) {
        step = side >= 0.0f ? 5.0f : -5.0f;
    }
    if ((side >= 0.0f && step < 0.0f) || (side < 0.0f && step > 0.0f)) {
        return 0;
    }
    next = d->swing + step;
    if ((d->swing > 0.0f && next < 0.0f) || (d->swing < 0.0f && next > 0.0f)) {
        next = 0.0f;
    }
    if (next > DOOR_WIDE) {
        next = DOOR_WIDE;
    } else if (next < -DOOR_WIDE) {
        next = -DOOR_WIDE;
    }
    *delta = next - d->swing;
    if (speed != NULL) {
        *speed = vel;
    }
    return *delta > 0.4f || *delta < -0.4f;
}

/* ---- the nursery wardrobe's doors ----
 * A leaf is a placed object ("left_door") whose animation turns it about its place. Its track has
 * two flat ends - 0..30 flat open, 53..76 flat shut - and the frames between them are the swing.
 * A grip holds that frame, so the hand's angle about the hinge is the leaf's own turn; the frame
 * its track gives for that angle is where the leaf is put. Let go slowly, it stays there; let go
 * while it is turning, it keeps that speed to the stop it was heading for (ward_release, ward_tick).
 * The hiding plays the same leaves on tracks of their own (doors-anim: placed_anim, which clears
 * `paused` and starts its track at frame 0), so a hand takes the leaf again on its own track when it
 * finds another loaded (ward_own_track), and each tick while it holds it (ward_retake).
 * (any marked hiding leaf is the same path: its direction comes from its own track, not from a
 * sign here.) */
#define WARD_TOUCH 5.0f         /* world units from the leaf's face: a hand on it */
#define WARD_STILL 1.0e-3f      /* radians a frame that barely turns: a flat end of the track */
/* degrees a tick the leaf itself turns: let go faster than this, it keeps that speed to the stop
 * it was heading for (the exit doors' COAST_SPEED, taken in the leaf's own swing so a track that
 * turns more or less a frame than this one is judged by the same throw) */
#define WARD_COAST 1.5f

/* 1 when a placed object is a leaf a hand may turn: a hiding spot marks its doors (vrspots.c) */
static int ward_leaf(const Placed *p) {
    return p->hiding_leaf;
}

/* a frame of the leaf's track: its turn, and for format 7 the place that goes with it (the leaf's
 * hinge moves as it swings) */
typedef struct WardKey {
    Vec3 rot, pos;
    int pos_from_key;
} WardKey;

/* the turn (and, format 7, the place) a frame of the leaf's track gives */
static WardKey ward_key(const Placed *p, int frame) {
    WardKey k;

    k.rot = vec3(0.0f, 0.0f, 0.0f);
    k.pos = p->def_pos;
    k.pos_from_key = 0;
    if (p->keys == NULL || p->frames <= 0) {
        return k;
    }
    if (frame < 0) {
        frame = 0;
    }
    if (frame >= p->frames) {
        frame = p->frames - 1;
    }
    if (p->format == 7) {   /* (Track_Sample's format 7: the turn, then the place) */
        const uint8_t *key = p->keys + frame * 24;

        memcpy(&k.rot, key, 12);
        memcpy(&k.pos, key + 12, 12);
        k.pos_from_key = 1;
    } else if (p->format == 8 || p->format == 9) {
        memcpy(&k.rot, p->keys + frame * 12, 12);
    }
    return k;
}

/* how far apart two turns of the track are: its largest part that differs */
static float ward_gap(Vec3 a, Vec3 b) {
    float dx = fabsf(a.x - b.x), dy = fabsf(a.y - b.y), dz = fabsf(a.z - b.z);

    return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
}

/* the frames the leaf turns through: its track's two flat ends are the stops (30 and 53 here) */
static int ward_span(const Placed *p, int *lo, int *hi) {
    int a, b;

    if (p->keys == NULL || (p->format != 7 && p->format != 8) || p->frames < 3) {
        return 0;
    }
    for (a = 1; a < p->frames - 1 && ward_gap(ward_key(p, a).rot, ward_key(p, 0).rot) < WARD_STILL; a++) {
    }
    for (b = p->frames - 2; b > 0 && ward_gap(ward_key(p, b).rot, ward_key(p, p->frames - 1).rot) < WARD_STILL; b--) {
    }
    if (b - a < 2) {
        return 0;
    }
    *lo = a - 1;
    *hi = b + 1;
    return 1;
}

/* a leaf's bounds in the world the hands are in: its vertices through placed_world, the transform it
 * is drawn with (a placed mesh carries no bounds of its own) */
static void ward_bounds(const Placed *p, Vec3 *lo, Vec3 *hi) {
    Mat4 w = placed_world(p);
    int i;

    *lo = vec3(1.0e30f, 1.0e30f, 1.0e30f);
    *hi = vec3(-1.0e30f, -1.0e30f, -1.0e30f);
    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = mat4_point(&w, vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z));

        *lo = vec3_min(*lo, v);
        *hi = vec3_max(*hi, v);
    }
}

/* the far corner of the leaf in its own space: the point that swings widest about the place (the
 * turn is applied there, so the tip is taken there too) */
static Vec3 ward_tip(const Placed *p) {
    Vec3 lo = vec3(1.0e30f, 1.0e30f, 1.0e30f), hi = vec3(-1.0e30f, -1.0e30f, -1.0e30f);
    int i;

    for (i = 0; i < p->mesh.nv; i++) {
        Vec3 v = vec3(p->mesh.v[i].x, p->mesh.v[i].y, p->mesh.v[i].z);

        lo = vec3_min(lo, v);
        hi = vec3_max(hi, v);
    }
    return vec3(fabsf(lo.x) > fabsf(hi.x) ? lo.x : hi.x, (lo.y + hi.y) * 0.5f,
                fabsf(lo.z) > fabsf(hi.z) ? lo.z : hi.z);
}

/* the hand's angle about the leaf's place with the leaf turned that far: where its tip sits.
 * (the turn's own axes give the hand its direction, whichever way round the track turns) */
static float ward_angle(const Placed *p, WardKey key, Vec3 tip) {
    Placed at = *p;
    Vec3 hinge = key.pos_from_key ? key.pos : p->def_pos;
    Vec3 w;
    Mat4 place;

    at.rot = key.rot;
    at.pos = hinge;
    place = placed_world(&at);
    w = mat4_point(&place, tip);
    return atan2f(w.x - hinge.x, w.z - hinge.z);
}

/* the hand's radians a frame of the swing: the two ends of the track, posed */
static float ward_rate(const Placed *p, int lo, int hi, Vec3 tip) {
    return wrap_rad(ward_angle(p, ward_key(p, hi), tip) - ward_angle(p, ward_key(p, lo), tip)) / (float)(hi - lo);
}

/* the wardrobe leaf a hand is on: how near it is to the leaf's face, and which one it is (-1 none) */
static int nearest_ward(PlacedSet *s, Vec3 hand, int other, float *best) {
    int i, found = -1;

    *best = WARD_TOUCH * WARD_TOUCH;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 lo, hi, gap;
        float reach;

        if (!p->shown || p->mesh.nv <= 0 || !ward_leaf(p)) {
            continue;
        }
        if (other >= 0 && sHold[other] == HOLD_WARD && sHoldId[other] == i) {
            continue;
        }
        ward_bounds(p, &lo, &hi);
        gap = vec3(hand.x - fminf(fmaxf(hand.x, lo.x), hi.x), hand.y - fminf(fmaxf(hand.y, lo.y), hi.y),
                   hand.z - fminf(fmaxf(hand.z, lo.z), hi.z));
        reach = vec3_len(gap);
        if (reach * reach < *best) {
            *best = reach * reach;
            found = i;
        }
    }
    return found;
}

#if 0   /* (temporary: the leaf nearest a hand, out to `reach_max` world units: -1, and `best` left at that,
 * when none is) */
static int ward_near(PlacedSet *s, Vec3 hand, float reach_max, float *best) {
    int i, found = -1;

    *best = reach_max;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 lo, hi, gap;
        float reach;

        if (!p->shown || p->mesh.nv <= 0 || !ward_leaf(p)) {
            continue;
        }
        ward_bounds(p, &lo, &hi);
        gap = vec3(hand.x - fminf(fmaxf(hand.x, lo.x), hi.x), hand.y - fminf(fmaxf(hand.y, lo.y), hi.y),
                   hand.z - fminf(fmaxf(hand.z, lo.z), hi.z));
        reach = vec3_len(gap);
        if (reach < *best) {
            *best = reach;
            found = i;
        }
    }
    return found;
}
#endif

/* a track no hand can scrub: as the room left it, its own to play */
static void ward_unload(Placed *p, int loaded) {
    if (loaded) {
        p->keys = NULL;
        p->frames = p->frame = p->paused = 0;
    }
}

/* the leaf back on its own track: the first its record gives - the one the room poses it with, and
 * the only one whose frames are this swing's. The hiding plays the leaves on tracks of their own
 * (doors-anim: a short open, a short shut), whose frames are no frames of that swing - a hand that
 * took the leaf on one would either scrub the wrong frames or be refused, a script track having no
 * flat ends to know its stops by. 1 when `p` is already on it; else the leaf is put on it, its pose
 * left as it stands (ward_take holds the end that pose sits at). */
static int ward_own_track(PlacedSet *s, Placed *p) {
    Placed own = *p;

    placed_anim(s, &own, 0, 0);
    if (own.keys == NULL || own.frames <= 0 || (own.keys == p->keys && own.frames == p->frames)) {
        return 1;   /* (nothing to take it on but what the room left, or already on its own) */
    }
    p->loop = own.loop;
    p->keys = own.keys;
    p->frames = own.frames;
    p->format = own.format;
    return 0;
}

/* a hand takes the leaf: its track loaded if the room never played it (or left one of its own on
 * it), the leaf held at the frame its pose already shows, so it does not jump into the hand */
static int ward_take(PlacedSet *s, int side, int id, Vec3 hand) {
    Placed *p = &s->p[id];
    Vec3 tip;
    float rate;
    int loaded = 0, lo = 0, hi = 0, frame;

    if (p->keys == NULL || p->frames <= 0) {
        placed_anim(s, p, 0, 0);   /* (its first track: the pose it is defined with) */
        loaded = 1;
    } else if (!ward_own_track(s, p)) {
        loaded = 1;   /* (a track of the room's on it: back to the leaf's own, to be scrubbed) */
    }
    if (p->mesh.nv <= 0 || !ward_span(p, &lo, &hi)) {
        ward_unload(p, loaded);
        return 0;
    }
    tip = ward_tip(p);
    rate = ward_rate(p, lo, hi, tip);
    if (fabsf(rate) < 1.0e-5f) {
        ward_unload(p, loaded);
        return 0;
    }
    if (loaded) {   /* (never played: the end of the swing its own pose sits at) */
        WardKey cur;
        float pose, shut;

        cur.rot = p->rot;
        cur.pos = p->pos;
        cur.pos_from_key = p->format == 7;
        pose = wrap_rad(ward_angle(p, cur, tip) - ward_angle(p, ward_key(p, lo), tip));
        shut = wrap_rad(ward_angle(p, ward_key(p, hi), tip) - ward_angle(p, ward_key(p, lo), tip));

        frame = fabsf(pose) <= fabsf(pose - shut) ? lo : hi;
    } else {
        frame = p->frame < lo ? lo : p->frame > hi ? hi : p->frame;   /* (a flat end holds at its stop) */
    }
    placed_frame_set(s, p, frame);
    sWardFrame[side] = frame;
    /* (the hand may turn it a frame past each stop, where the leaf sits still: its pose does not
     * change there, so a stop reached one frame inside the swing is not left ajar) */
    sWardLo[side] = lo > 1 ? lo - 1 : 0;
    sWardHi[side] = hi + 1 < p->frames - 1 ? hi + 1 : p->frames - 1;
    sWardRate[side] = rate;
    sWardAng[side] = atan2f(hand.x - p->pos.x, hand.z - p->pos.z);
    sWardMove[id].mode = 0;   /* (taken again: what it did on its own ends here) */
    sWardMove[id].speed = 0.0f;
    sWardLast[side] = (float)frame;
    sWardKeys[side] = p->keys;   /* (the track it is held on: a script's own is taken again) */
    sWardFrames[side] = p->frames;
    return 1;
}

/* a hand holds the leaf, but it is no longer the track it took it on: a script has played it since
 * (placed_anim: a track of its own, `paused` cleared) or its frames are being played on - so the
 * hand takes it again where it now stands, and its frames are the hand's once more. 1 when the leaf
 * is held (taken again here, or held where it was), 0 when it cannot be held just now (a script's
 * track with no stops of its own: nothing is driven on it, and the next tick tries again). */
static int ward_retake(PlacedSet *s, int side, int id, Placed *p, Vec3 hand) {
    if (p->paused && p->keys == sWardKeys[side] && p->frames == sWardFrames[side]) {
        return 1;   /* (still held where the hand took it) */
    }
    return ward_take(s, side, id, hand);
}

/* the hand turns the leaf about its hinge: the frame its track gives for the hand's angle. The step
 * a tick it takes is kept, so a hand that lets go while the leaf is turning leaves it its speed
 * (ward_release); a hand held against a stop gives it no step, so the leaf stops there. */
static void ward_drive(PlacedSet *s, int side, int id, Placed *p, Vec3 hand) {
    float ang, frame;

    if (!p->paused || p->keys != sWardKeys[side] || p->frames != sWardFrames[side]) {
        return;   /* (not the track the hand holds: ward_retake takes the leaf again first) */
    }
    ang = atan2f(hand.x - p->pos.x, hand.z - p->pos.z);
    frame = (float)sWardFrame[side] + wrap_rad(ang - sWardAng[side]) / sWardRate[side];

    if (frame < (float)sWardLo[side]) {
        frame = (float)sWardLo[side];
    } else if (frame > (float)sWardHi[side]) {
        frame = (float)sWardHi[side];
    }
    if (id >= 0 && id < PLACED_MAX) {
        sWardMove[id].speed = frame - sWardLast[side];   /* (frames a tick) */
    }
    sWardLast[side] = frame;
    placed_frame_set(s, p, (int)(frame + 0.5f));
}

/* the hand has let the leaf go: turning faster than WARD_COAST a tick, the leaf keeps that speed
 * towards the stop it was heading for (ward_tick carries it there); slower, it stays where the
 * hand left it. */
static void ward_release(PlacedSet *s, int side, int id) {
    WardMove *w = &sWardMove[id];
    Placed *p = &s->p[id];
    float degs;

    w->mode = 0;
    if (!p->shown || p->keys == NULL || p->frames <= 0 || !p->paused ||
        (p->format != 7 && p->format != 8)) {
        return;
    }
    /* the frames a tick in the degrees of the leaf's own swing (radians a frame of its track) */
    degs = fabsf(w->speed) * fabsf(sWardRate[side]) * (180.0f / 3.14159265f);
    if (degs < WARD_COAST) {
        return;
    }
    w->at = (float)p->frame;
    w->mode = 1;
}

/* a leaf no hand holds now: it coasts, as an exit door let go of while moving does, until it meets
 * the stop it was heading for - the flat open end of its track one way, the flat shut end the
 * other. Placed leaves are the room's, so a change of room drops what was running here. */
static void ward_tick(Engine *e) {
    PlacedSet *s = &e->room.placed;
    int i;

    if (sWardRoom != e->room.id) {
        memset(sWardMove, 0, sizeof(sWardMove));
        sWardRoom = e->room.id;
        return;
    }
    for (i = 0; i < s->n; i++) {
        WardMove *w = &sWardMove[i];
        Placed *p = &s->p[i];
        int lo = 0, hi = 0;

        if (w->mode == 0) {
            continue;
        }
        if (!p->shown || p->keys == NULL || !p->paused || (p->format != 7 && p->format != 8) ||
            !ward_leaf(p) || !ward_span(p, &lo, &hi)) {
            w->mode = 0;   /* (a script has the leaf again, or it is put away: hands off) */
            continue;
        }
        w->at += w->speed;
        if (w->speed > 0.0f ? w->at >= (float)hi : w->at <= (float)lo) {
            w->at = w->speed > 0.0f ? (float)hi : (float)lo;   /* (the stop it was heading for) */
            w->mode = 0;
        }
        placed_frame_set(s, p, (int)(w->at + 0.5f));
    }
}

#if 0   /* (temporary: the hiding spot's own line - nothing here moves her or the leaf)
 * Where she is, her eyes (vr_anchor_get: the anchor the hands and the grabs sit on), against the
 * bounds the wardrobe's leaf has in the world: the same ward_bounds a hand reaches for it by, its
 * vertices through placed_world, which is the pose its frame gives. A line to stderr the first time
 * it is read, then only when the answer changes - she is in or out of those bounds, or the leaf
 * stands at another frame - so a session in the nursery says whether those bounds can tell inside
 * from outside at all. A room without the leaf, or no anchor to ask, says nothing and forgets the
 * last line: the next look is read fresh. */
static void hide_probe(Engine *e) {
    PlacedSet *s = &e->room.placed;
    Placed *p = NULL;
    Vec3 eye, lo, hi;
    int i, inside;

    if (sHideRoom != e->room.id) {   /* (another room's leaves and frames are not these) */
        sHideSeen = 0;
        sHideRoom = e->room.id;
    }
    for (i = 0; i < s->n; i++) {
        if (s->p[i].shown && s->p[i].mesh.nv > 0 && ward_leaf(&s->p[i])) {
            p = &s->p[i];
            break;
        }
    }
    if (p == NULL || !vr_anchor_get(&eye, NULL)) {
        sHideSeen = 0;   /* (no leaf to stand in, or no eyes to stand there with: the next look says it) */
        return;
    }
    ward_bounds(p, &lo, &hi);
    inside = eye.x >= lo.x && eye.x <= hi.x && eye.y >= lo.y && eye.y <= hi.y && eye.z >= lo.z && eye.z <= hi.z;
    if (sHideSeen && inside == sHideInside && p->frame == sHideFrame) {
        return;   /* (the same answer as the last look: nothing new to say) */
    }
    sHideSeen = 1;
    sHideInside = inside;
    sHideFrame = p->frame;
    fprintf(stderr, "hide: inside %d frame %d\n", inside, p->frame);
}
#endif

static int nearest_prop(PlacedSet *s, Vec3 hand, int other, float *best) {
    int i, found = -1;

    *best = 7.0f * 7.0f;
    for (i = 0; i < s->n; i++) {
        Placed *p = &s->p[i];
        Vec3 ext;
        float edge, d2;

        if (!p->shown || (other >= 0 && sHold[other] == HOLD_PROP && sHoldId[other] == i) || loot_claims_placed(i) ||
            ward_leaf(p) || vrspot_placed_part(i) || vrlever_placed(i) || vrmirror_placed(i)) {   /* (a leaf, a spot's part, a lever or a mirror's curtain is not carried) */
            continue;
        }
        ext = vec3_sub(p->mesh.hi, p->mesh.lo);
        edge = ext.x > ext.y ? ext.x : ext.y;
        if (ext.z > edge) {
            edge = ext.z;
        }
        if (edge < 0.2f || edge > 28.0f) {
            continue;
        }
        d2 = dist2(hand, p->pos);
        if (d2 < *best) {
            *best = d2;
            found = i;
        }
    }
    return found;
}

static void release_hand(Room *r, int side) {
    int id = sHoldId[side];

    if (sHold[side] == HOLD_DOOR && id >= 0 && id < ROOM_DOORS) {
        sGrace[side] = PUSH_GRACE;
        sGraceDoor[side] = id;
        if (sVel[id] <= -COAST_SPEED || sVel[id] >= COAST_SPEED) {
            sCoast[id] = 1;
            sSettle[id] = 0;
        } else {
            sCoast[id] = 0;
            sSettle[id] = 18;   /* about half a second, then it finishes the swing */
        }
    } else if (sHold[side] == HOLD_WARD && id >= 0 && id < r->placed.n) {
        ward_release(&r->placed, side, id);   /* (a leaf still turning keeps its speed) */
    } else if (sHold[side] == HOLD_SPOT) {
        spot_release_log(id, sSpotWhy);   /* (DEBUG: why the spot's hand ended) */
        vrspot_let_go(id, sSpotSpeed[side]);   /* (a spot still moving keeps its speed to the stop) */
    } else if (sHold[side] == HOLD_LEVER) {
        vrlever_let_go(id);   /* (a lever let go before home springs back to rest) */
    } else if (sHold[side] == HOLD_MIRROR) {
        vrmirror_log_release(id, sMirrorWhy);   /* (DEBUG: why the mirror's hand ended) */
        vrmirror_let_go(id);   /* (a curtain let go eases to the end its flag has) */
    }
    sHold[side] = HOLD_NONE;
    sSpotWhy = "let-go";
    sMirrorWhy = "let-go";
    sHoldDoor[side] = -1;
    sHoldFresh[side] = 0;
    sLadderHold[side] = -1;   /* (a hand let go of its ladder with whatever it held) */
    sLadderFresh[side] = 0;
}

/* a door the hands answer for (sDoorHand): a leaf they drive, push or ease */
static void door_hand(int d) {
    if (d >= 0 && d < DOORS) {
        sDoorHand[d] = 1;
    }
}

static void drive_door(Engine *e, int id, int axis, Vec3 hand) {
    RoomDoor *d;
    float next;

    if (id < 0 || id >= ROOM_DOORS || door_blocked(e, id)) {
        return;
    }
    d = &e->room.doors[id];
    if (!d->present || d->mode == 1) {
        return;
    }
    next = clamp_swing(swing_of(d, hand, axis));
    sVel[id] = next - d->swing;
    sCoast[id] = 0;
    room_door_swing(&e->room, id, next, 1);
    sSettle[id] = 0;
    door_hand(door_index(e, id));
}

/* the 5°-a-frame ease to shut, or to open at -90 / +90, then the doors actor writes the state */
static void ease_door(Engine *e, int id, float goal) {
    RoomDoor *d = &e->room.doors[id];
    int open = goal <= -DOOR_HALF || goal >= DOOR_HALF;

    d->mode = 2;
    d->closing = !open;
    d->target = goal;
    d->sounded = 0;
    d->keys = NULL;
    claim_for_letgo(e, id, open);
    door_hand(door_index(e, id));
}

/* still moving when the grip let go: keep that speed until the stop it is heading for */
static void coast_door(Engine *e, int id) {
    RoomDoor *d = &e->room.doors[id];
    float v = sVel[id];
    float s, goal;
    int hit;

    if (!d->present || d->mode == 1 || door_blocked(e, id) || v == 0.0f) {
        sCoast[id] = 0;
        return;
    }
    s = d->swing + v;
    hit = 0;
    goal = DOOR_SHUT;
    if (v < 0.0f) {
        if (d->swing <= DOOR_SHUT) {
            goal = -DOOR_WIDE;
            hit = s <= goal;
        } else {
            hit = s <= DOOR_SHUT;
        }
    } else {
        if (d->swing >= DOOR_SHUT) {
            goal = DOOR_WIDE;
            hit = s >= goal;
        } else {
            hit = s >= DOOR_SHUT;
        }
    }
    if (hit) {
        room_door_swing(&e->room, id, goal, 1);
        ease_door(e, id, goal);
        sCoast[id] = 0;
        sVel[id] = 0.0f;
        return;
    }
    room_door_swing(&e->room, id, s, 1);
}

static int swing_open(float s) {
    return s <= -SWING_OPEN || s >= SWING_OPEN;
}

static int door_locked_id(int d) {
    return d >= 0 && d < DOORS && (gProgress.doors[d] & 8u) != 0;
}

/* the leaf and the opened bit stay together, so a shut door is shut on the nav mesh too */
static void match_progress(Engine *e, int exit, int d, float swing) {
    int open = swing_open(swing);
    uint32_t state = gProgress.doors[d];
    uint32_t next = open ? (state | 2u) : (state & ~2u);

    if (next != state) {
        gProgress.doors[d] = next;
    }
    room_door_passage(&e->room, exit, open, 0);
}

void vrhands_forget_poses(void) {
    int side;

    memset(sDoorHave, 0, sizeof(sDoorHave));
    memset(sDoorHand, 0, sizeof(sDoorHand));
    memset(sDoorClaim, 0, sizeof(sDoorClaim));
    for (side = 0; side < 2; side++) {   /* (a load's own doors: nothing a hand held is carried in) */
        if (sHold[side] == HOLD_DOOR) {
            sHold[side] = HOLD_NONE;
            sHoldDoor[side] = -1;
            sHoldFresh[side] = 0;
        }
    }
    memset(sSettle, 0, sizeof(sSettle));   /* (the exits' own: the room is the save's now) */
    memset(sCoast, 0, sizeof(sCoast));
    memset(sVel, 0, sizeof(sVel));
    memset(sDoorVel, 0, sizeof(sDoorVel));
    sPoseRoom = -2;
    vrgrip_idle();
}

void vrhands_forget_stops(void) {
    sHandStopHave[0] = sHandStopHave[1] = 0;
    vrbody_forget_place();   /* (and the hands' smoothed frames: they are the old place's too) */
}

/* A hand on a hiding spot lets go at once (a script is about to play the spot's parts): no coast,
 * nothing left held. The parts are the script's from here (vrspots.c reads the closure back when
 * its animation ends). */
void vrhands_spot_let_go(void) {
    int side;

    for (side = 0; side < 2; side++) {
        if (sHold[side] == HOLD_SPOT) {
            spot_release_log(sHoldId[side], "script-anim");   /* (DEBUG) */
            sHold[side] = HOLD_NONE;
            sHoldId[side] = -1;
            sSpotPart[side] = -1;
            sSpotSpeed[side] = 0.0f;
        }
    }
}

/* Both hands let go of what they hold (a teleport: a door, a prop, a leaf or loot is not dragged
 * to the new place). The arm stops and ladder holds go with them, as when a room changes. */
void vrhands_drop_all(void) {
    int side;

    for (side = 0; side < 2; side++) {
        if (sHold[side] == HOLD_LOOT) {
            loot_let_go(sHoldId[side], side);
        } else if (sHold[side] != HOLD_NONE) {
            sSpotWhy = "drop-all";
            sMirrorWhy = "drop-all";
            release_hand(&gEngine.room, side);
        }
        sHold[side] = HOLD_NONE;
        sHoldDoor[side] = -1;
        sHoldFresh[side] = 0;
        sLadderHold[side] = -1;
        sLadderFresh[side] = 0;
        sLadderAnchorHave[side] = 0;
        sHandStopHave[side] = 0;
        sWallNear[side] = 0;
    }
    sLadderPull = 0.0f;
    vrbody_forget_place();   /* (the body's smoothings are the old place's: no arm stretched back to it) */
    vrgrip_idle();
}

/* the angle last seen on this door, on whichever side of it was loaded */
static void poses_restore(Engine *e) {
    int room = e->room.id;
    int i;

    if (room < 0 || room >= WORLD_ROOMS || room == sPoseRoom) {
        return;
    }
    sPoseRoom = room;
    for (i = 0; i < ROOM_DOORS; i++) {
        int d = door_index(e, i);

        if (!e->room.doors[i].present || d < 0 || d >= DOORS || !sDoorHave[d] || door_locked_id(d)) {
            continue;
        }
        room_door_swing(&e->room, i, sDoorSwing[d], 1);
        match_progress(e, i, d, sDoorSwing[d]);
        sDoorOpen[d] = (unsigned char)swing_open(sDoorSwing[d]);
    }
}

static void poses_save(Engine *e) {
    int i;

    if (e->room.id < 0 || e->room.id >= WORLD_ROOMS) {
        return;
    }
    for (i = 0; i < ROOM_DOORS; i++) {
        int d = door_index(e, i);
        float s;

        if (!e->room.doors[i].present || d < 0 || d >= DOORS) {
            continue;
        }
        s = e->room.doors[i].swing;
        /* which way, and how fast, the leaf was going this tick: a room change's finish takes it */
        sDoorVel[d] = sDoorHave[d] ? s - sDoorSwing[d] : 0.0f;
        if (sDoorHave[d] && !door_locked_id(d) && (s - sDoorSwing[d] > 0.5f || sDoorSwing[d] - s > 0.5f)) {
            match_progress(e, i, d, s);   /* a hand moved it: the passage follows the leaf */
        }
        sDoorSwing[d] = s;
        sDoorOpen[d] = (unsigned char)swing_open(s);
        sDoorHave[d] = 1;
        if (sDoorClaim[d] && (gProgress.doors[d] & 1u) == 0) {
            sDoorClaim[d] = 0;   /* (the doors actor let it go at rest: the claim is over) */
        }
        /* the leaves are the hands' no longer: at rest, no hold on it, its let-go written */
        if (sDoorHand[d] && sHoldDoor[0] != d && sHoldDoor[1] != d && sSettle[i] == 0 && sCoast[i] == 0 &&
            e->room.doors[i].mode == 0 && (gProgress.doors[d] & 1u) == 0) {
            sDoorHand[d] = 0;
        }
    }
}

/* ---- a room change: what the hands still had ---- */

/* The stop the doors actor's let-go would have written at rest, written now: a door the hands were
 * still driving, easing or settling when the room changed. The ease (5 a frame, room.c Door_Swing)
 * runs in the played room only, so it dies with the room; the stop its angle and last motion give
 * is applied here with the state word, and the leaf of the room now in (if it has one) is put at
 * it. The held bit is taken back only if this side set it (sDoorClaim): a script's own hold is the
 * script's. */
static void door_finish(Engine *e, int d) {
    float stop = door_stop_for(sDoorSwing[d], sDoorVel[d]);
    int open = swing_open(stop);
    int exit = world_door_exit(&e->world, d, e->room.id);

    sDoorSwing[d] = stop;
    sDoorOpen[d] = (unsigned char)open;
    sDoorVel[d] = 0.0f;
    if (sDoorClaim[d]) {
        gProgress.doors[d] &= ~1u;   /* (only this side's own claim: a script's hold is the script's) */
        sDoorClaim[d] = 0;
    }
    if (!door_locked_id(d)) {   /* (a locked door's state is the scripts': as poses_save has it) */
        gProgress.doors[d] = (gProgress.doors[d] & ~2u) | (open ? 2u : 0u);
    }
    sDoorHand[d] = 0;
    if (exit < 0 || exit >= ROOM_DOORS || !e->room.doors[exit].present) {
        return;
    }
    room_door_swing(&e->room, exit, stop, 1);
    if (!door_locked_id(d)) {
        match_progress(e, exit, d, stop);
    }
}

/* The hand still on its door in the room now in: the same door, at this room's exit, its knob
 * under the hand (a leaf is placed in its own room's frame, so the same knob is another
 * coordinate). It is not carried when the grip opened on the way through, when the hand is not at
 * that door's knob (she left by another way), or when the room has no free leaf for it. */
static int door_carry(Engine *e, int side, int exit) {
    Vec3 at, right, up, fwd;
    float grip, index, thumb;
    int axis = 0;

    if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb) || grip <= 0.4f) {
        return 0;
    }
    if (!knob_near(&e->room.doors[exit], at, DOOR_CARRY_REACH, &axis)) {
        return 0;
    }
    sHoldId[side] = exit;
    sDoorAxis[side] = axis;
    sHoldFresh[side] = 1;   /* (the hand's place has moved with the room: driven from the next tick) */
    return 1;
}

/* A room change, before anything of the new room is read as the old's: a door a hand still holds
 * is carried to the same door here (its knob under the hand) and driven on; every other door the
 * hands were still finishing ends at its stop. The per-exit timers are the old room's exits' and
 * go with it. */
static void doors_room_change(Engine *e, int from) {
    int carried[2] = {-1, -1};
    int side, d;

    for (side = 0; side < 2; side++) {
        int exit;

        if (sHold[side] != HOLD_DOOR || sHoldDoor[side] < 0) {
            continue;
        }
        d = sHoldDoor[side];
        exit = world_door_exit(&e->world, d, e->room.id);
        if (exit >= 0 && exit < ROOM_DOORS && e->room.doors[exit].present && !door_blocked(e, exit) &&
            world_door_leads(&e->world, d, from) == e->room.id && door_carry(e, side, exit)) {
            carried[side] = d;
            continue;
        }
        sHold[side] = HOLD_NONE;
        sHoldDoor[side] = -1;
        sHoldFresh[side] = 0;
        sDoorAxis[side] = 0;
        sGrace[side] = 0;
        sGraceDoor[side] = -1;
    }
    for (d = 0; d < DOORS; d++) {
        if (sDoorHand[d] && d != carried[0] && d != carried[1]) {
            door_finish(e, d);
        }
    }
    memset(sSettle, 0, sizeof(sSettle));
    memset(sCoast, 0, sizeof(sCoast));
    memset(sVel, 0, sizeof(sVel));
}

/* the controller point against the posed mesh. About 6 cm: a touch, not a reach. */
#define PET_SKIN 0.75f

/* closest distance squared from p to triangle abc (Christer Ericson) */
static float tri_dist2(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    Vec3 ab = vec3_sub(b, a), ac = vec3_sub(c, a), ap = vec3_sub(p, a);
    float d1 = vec3_dot(ab, ap), d2 = vec3_dot(ac, ap);

    if (d1 <= 0.0f && d2 <= 0.0f) {
        return vec3_dot(ap, ap);
    }
    {
        Vec3 bp = vec3_sub(p, b);
        float d3 = vec3_dot(ab, bp), d4 = vec3_dot(ac, bp);

        if (d3 >= 0.0f && d4 <= d3) {
            return vec3_dot(bp, bp);
        }
        {
            float vc = d1 * d4 - d3 * d2;

            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                float v = d1 / (d1 - d3);
                Vec3 r = vec3_sub(vec3_add(a, vec3_scale(ab, v)), p);

                return vec3_dot(r, r);
            }
            {
                Vec3 cp = vec3_sub(p, c);
                float d5 = vec3_dot(ab, cp), d6 = vec3_dot(ac, cp);

                if (d6 >= 0.0f && d5 <= d6) {
                    return vec3_dot(cp, cp);
                }
                {
                    float vb = d5 * d2 - d1 * d6;

                    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                        float w = d2 / (d2 - d6);
                        Vec3 r = vec3_sub(vec3_add(a, vec3_scale(ac, w)), p);

                        return vec3_dot(r, r);
                    }
                    {
                        float va = d3 * d6 - d5 * d4;

                        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
                            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                            Vec3 r = vec3_sub(vec3_add(b, vec3_scale(vec3_sub(c, b), w)), p);

                            return vec3_dot(r, r);
                        }
                        {
                            float sum = va + vb + vc;

                            if (sum < 1.0e-8f && sum > -1.0e-8f) {
                                return vec3_dot(ap, ap);
                            }
                            {
                                float denom = 1.0f / sum;
                                float v = vb * denom, w = vc * denom;
                                Vec3 r = vec3_sub(vec3_add(a, vec3_add(vec3_scale(ab, v), vec3_scale(ac, w))), p);

                                return vec3_dot(r, r);
                            }
                        }
                    }
                }
            }
        }
    }
}

static const Actor *hewie_mesh(void) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];

        if (a->used && a->visible && a->posed != NULL && strncmp(a->name, "O_HE", 4) == 0) {
            return a;
        }
    }
    return NULL;
}

static int primary_bone(const SkinVertex *v) {
    int k, best = v->bones[0];
    float w = v->weights[0];

    for (k = 1; k < 4; k++) {
        if (v->weights[k] > w) {
            w = v->weights[k];
            best = v->bones[k];
        }
    }
    return best;
}

/* 1 when the hand is on the mesh. `bone` is the joint that owns the hit. */
static int mesh_touch(const Actor *dog, Vec3 p, int *bone) {
    const Model *m = &dog->model;
    float lim = PET_SKIN * PET_SKIN;
    float reach = actor_radius(dog) + PET_SKIN;
    float best = lim;
    Vec3 d = vec3_sub(p, actor_center(dog));
    int i, hit = 0;

    if (vec3_dot(d, d) > reach * reach) {
        return 0;
    }
    for (i = 0; i < m->nd; i++) {
        const MeshDraw *draw = &m->d[i];
        int v, end;

        if (draw->count < 3) {
            continue;
        }
        end = draw->first + draw->count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (v = draw->first; v + 2 < end; v += 3) {
            Vec3 a = vec3(dog->posed[v].x, dog->posed[v].y, dog->posed[v].z);
            Vec3 b = vec3(dog->posed[v + 1].x, dog->posed[v + 1].y, dog->posed[v + 1].z);
            Vec3 c = vec3(dog->posed[v + 2].x, dog->posed[v + 2].y, dog->posed[v + 2].z);
            float dist = tri_dist2(p, a, b, c);
            float da, db, dc;
            int pick;

            if (dist > best) {
                continue;
            }
            da = vec3_len(vec3_sub(p, a));
            db = vec3_len(vec3_sub(p, b));
            dc = vec3_len(vec3_sub(p, c));
            pick = v;
            if (db < da && db <= dc) {
                pick = v + 1;
            } else if (dc < da && dc < db) {
                pick = v + 2;
            }
            best = dist;
            hit = 1;
            if (bone != NULL && pick < m->nv) {
                *bone = primary_bone(dog->shaped != NULL ? &dog->shaped[pick] : &m->v[pick]);
            }
        }
    }
    return hit;
}

/* a shove on one joint, in room space. Past the part's reach it lets go. */
#define PET_HEAD 0x1F
#define PET_NECK 0x1D
static Vec3 sPetOff[MODEL_MAX_BONES];
static Vec3 sPetWant[MODEL_MAX_BONES];
static int sPetTick = -1;
/* Hewie's "shake" paw: the front paw the animation lifts (0x1C), the hand on it, and the shakes
 * it has given. The paw is let go by the scripts when the action ends. */
#define PAW_BONE 0x1C
#define PAW_REACH 4.5f
#define PAW_GRIP 0.40f
#define PAW_SHAKE_STEP 1.0f
static int sPawSide = -1, sPawBone = -1, sPawFresh, sPawShakes, sPawDir, sPawHave;
static float sPawAt;

static int bone_of(const Model *m, int bone, int anc) {
    int guard = 0;

    while (bone >= 0 && bone < m->nbones && guard++ < MODEL_MAX_BONES) {
        if (bone == anc) {
            return 1;
        }
        bone = m->bones[bone].parent;
    }
    return 0;
}

static int foot_chain(const Model *m, int bone) {
    static const int kFeet[] = {0x0B, 0x0F, 0x17, 0x1C};
    int i;

    for (i = 0; i < 4; i++) {
        if (bone_of(m, bone, kFeet[i])) {
            return 1;
        }
    }
    return 0;
}

/* 1 the head, 2 an ear, 3 the tail, 4 the jaw. 0 stays on the skeleton. */
#define PET_JAW 0x25
static int pet_part(const Actor *dog, int bone) {
    const Model *m = &dog->model;
    Vec3 p, head, rel, fwd;
    float along;

    if (bone < 0 || bone >= m->nbones || bone >= MODEL_MAX_BONES) {
        return 0;
    }
    if (bone_of(m, bone, PET_NECK) && !bone_of(m, bone, PET_HEAD)) {
        return 0;
    }
    if (bone == PET_HEAD) {
        return 1;
    }
    if (bone_of(m, bone, PET_JAW)) {
        return 4;
    }
    if (bone_of(m, bone, PET_HEAD)) {
        return 2;
    }
    if (sPawBone >= 0 && (bone == sPawBone ||
        (sPawBone < m->nbones && m->bones[sPawBone].parent == bone))) {
        return 5;   /* (the voice's "shake": the lifted paw, and the leg just above it) */
    }
    p = dog->bones[bone];
    if (p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) {
        return 0;
    }
    if (m->nbones > PET_HEAD) {
        head = dog->bones[PET_HEAD];
        rel = vec3_sub(p, head);
        /* an ear that is not parented under the skull still sits just above it */
        if (head.y != 0.0f && rel.y > 0.2f && vec3_len(rel) < 5.5f) {
            return 2;
        }
    }
    if (bone == 0 || bone_of(m, bone, 0x10) || foot_chain(m, bone)) {
        return 0;
    }
    fwd = vec3(sinf(dog->yaw), 0.0f, cosf(dog->yaw));
    along = (p.x - dog->pos.x) * fwd.x + (p.z - dog->pos.z) * fwd.z;
    return along < -2.5f ? 3 : 0;
}

/* v turned about a unit axis. The joint itself is not moved. */
static Vec3 pet_spin_vec(Vec3 v, Vec3 axis, float ang) {
    float c = cosf(ang), s = sinf(ang);

    return vec3_add(vec3_add(vec3_scale(v, c), vec3_scale(vec3_cross(axis, v), s)),
                    vec3_scale(axis, vec3_dot(axis, v) * (1.0f - c)));
}

/* last frame's axis, with the pet swing taken back off, so the turn does not pile up */
static Vec3 pet_rest_axis(Vec3 axis, Vec3 spin) {
    float ang = vec3_len(spin);
    Vec3 n = vec3_norm(axis);

    if (ang < 0.001f) {
        return n;
    }
    return vec3_norm(pet_spin_vec(n, vec3_scale(spin, 1.0f / ang), -ang));
}

static void pet_open(void) {
    int i;

    if (sPetTick == (int)gEngine.ticks) {
        return;
    }
    sPetTick = (int)gEngine.ticks;
    for (i = 0; i < MODEL_MAX_BONES; i++) {
        sPetWant[i] = vec3(0.0f, 0.0f, 0.0f);
    }
}

static void pet_push(const Actor *dog, int bone, Vec3 hand) {
    int kind = pet_part(dog, bone);
    int k, bestk = -1;
    Vec3 pivot, to, best, axis, want;
    float d, maxa, letgo, reach, bestd = -1.0f, ang, al;

    if (kind == 0 || bone < 0 || bone >= MODEL_MAX_BONES) {
        return;
    }
    /* head stays a short nod; an ear can fold most of the way over */
    if (kind == 1) {
        maxa = 0.55f; letgo = 1.15f; reach = 6.0f;
    } else if (kind == 2) {
        maxa = 1.90f; letgo = 2.55f; reach = 8.0f;
    } else if (kind == 4) {
        maxa = 0.50f; letgo = 1.20f; reach = 5.5f;
    } else if (kind == 5) {
        maxa = 0.85f; letgo = 1.65f; reach = 8.0f;
    } else {
        maxa = 1.05f; letgo = 1.90f; reach = 9.0f;
    }
    pivot = dog->bones[bone];
    to = vec3_sub(hand, pivot);
    d = vec3_len(to);
    if (d < 0.35f || d > reach) {
        return;   /* off the socket: the part goes back to rest */
    }
    to = vec3_scale(to, 1.0f / d);
    for (k = 0; k < 3; k++) {
        Vec3 ax = pet_rest_axis(dog->bone_axes[bone][k], sPetOff[bone]);
        float dp;

        if (vec3_len(ax) < 0.5f) {
            continue;
        }
        dp = vec3_dot(ax, to);
        if (fabsf(dp) > bestd) {
            bestd = fabsf(dp);
            bestk = k;
            best = dp >= 0.0f ? ax : vec3_scale(ax, -1.0f);
        }
    }
    if (bestk < 0) {
        return;
    }
    ang = acosf(fminf(1.0f, fmaxf(-1.0f, vec3_dot(best, to))));
    if (ang > letgo || ang < 0.02f) {
        return;   /* past the stop, or already aimed: let the joint ease home */
    }
    if (ang > maxa) {
        ang = maxa;
    }
    axis = vec3_cross(best, to);
    al = vec3_len(axis);
    if (al < 1.0e-4f) {
        return;
    }
    want = vec3_scale(axis, ang / al);
    if (vec3_len(sPetWant[bone]) < vec3_len(want)) {
        sPetWant[bone] = want;
    }
}

static void pet_settle(void) {
    int i;

    if (sPetTick != (int)gEngine.ticks) {
        sPetTick = (int)gEngine.ticks;
        for (i = 0; i < MODEL_MAX_BONES; i++) {
            sPetWant[i] = vec3(0.0f, 0.0f, 0.0f);
        }
    }
    for (i = 0; i < MODEL_MAX_BONES; i++) {
        sPetOff[i] = vec3_add(sPetOff[i], vec3_scale(vec3_sub(sPetWant[i], sPetOff[i]), 0.45f));
        if (vec3_len(sPetOff[i]) < 0.02f && vec3_len(sPetWant[i]) < 0.02f) {
            sPetOff[i] = vec3(0.0f, 0.0f, 0.0f);
        }
    }
}

static int bone_depth(const Model *m, int bone) {
    int depth = 0, guard = 0;

    while (bone >= 0 && bone < m->nbones && guard++ < MODEL_MAX_BONES) {
        int parent = m->bones[bone].parent;

        if (parent < 0 || parent >= m->nbones) {
            break;
        }
        depth++;
        bone = parent;
    }
    return depth;
}

/* turn a posed bone about `pivot`. The bone that owns the pivot stays put; its children keep their length. */
static void pet_turn_about(Mat4 *w, Vec3 pivot, Vec3 axis, float ang) {
    Vec3 p = pet_spin_vec(vec3_sub(vec3(w->m[12], w->m[13], w->m[14]), pivot), axis, ang);
    int c;

    for (c = 0; c < 3; c++) {
        Vec3 v = pet_spin_vec(vec3(w->m[c * 4], w->m[c * 4 + 1], w->m[c * 4 + 2]), axis, ang);

        w->m[c * 4] = v.x;
        w->m[c * 4 + 1] = v.y;
        w->m[c * 4 + 2] = v.z;
    }
    w->m[12] = pivot.x + p.x;
    w->m[13] = pivot.y + p.y;
    w->m[14] = pivot.z + p.z;
}

void vrhands_pet_pose(Actor *a, Mat4 *world, int nbones, const Mat4 *unplace) {
    const Model *m;
    int pass, i;

    if (a == NULL || world == NULL || unplace == NULL || strncmp(a->name, "O_HEW", 5) != 0) {
        return;
    }
    m = &a->model;
    if (nbones > m->nbones) {
        nbones = m->nbones;
    }
    if (nbones > MODEL_MAX_BONES) {
        nbones = MODEL_MAX_BONES;
    }
    for (pass = 0; pass < nbones; pass++) {
        for (i = 0; i < nbones; i++) {
            Vec3 spin = sPetOff[i];
            Vec3 axis;
            float ang;
            int j;

            if (bone_depth(m, i) != pass || vec3_len(spin) < 0.02f) {
                continue;
            }
            ang = vec3_len(spin);
            spin = vec3_scale(spin, 1.0f / ang);
            axis = vec3(unplace->m[0] * spin.x + unplace->m[4] * spin.y + unplace->m[8] * spin.z,
                        unplace->m[1] * spin.x + unplace->m[5] * spin.y + unplace->m[9] * spin.z,
                        unplace->m[2] * spin.x + unplace->m[6] * spin.y + unplace->m[10] * spin.z);
            if (vec3_len(axis) < 1.0e-4f) {
                continue;
            }
            axis = vec3_norm(axis);
            {
                Vec3 pivot = vec3(world[i].m[12], world[i].m[13], world[i].m[14]);

                for (j = 0; j < nbones; j++) {
                    if (bone_of(m, j, i)) {
                        pet_turn_about(&world[j], pivot, axis, ang);
                    }
                }
            }
        }
    }
}

/* a hard swing on a surface. About 3 cm, and fast enough that a rest or a pet does not count. */
#define SWING_SKIN 0.40f
#define SWING_SPEED 1.25f
#define SWING_FAST 8.0f
#define ENV_COOL 12
#define VOICE_COOL 12
/* about 1.4 m past the skin, so a shove lands beside him */
#define SHOVE_REACH 18.0f
#define SHOVE_WINDOW 20
#define SHOVE_COOL 24
/* above the 5° ease and a slow push; a slammed door moves at 15° */
#define DOOR_HIT_SPEED 10.0f
#define DOOR_HIT_MARGIN 8.0f
static int sBlowOn, sShoveOn, sSwingOn, sEnvCool, sShoveCool, sVoiceCool;
static Vec3 sBlowAt;
static Vec3 sHandLocal[2], sHeadLocal, sBodyDelta, sFwd;
static int sHeadHave;
static int sStalkTick[2] = {-1, -1};
static int sDoorTick[2] = {-1, -1}, sDoorHit[2] = {-1, -1}, sDoorShove = -1, sDoorWay = -1;
static float sDoorZ[2];
static float sSwingWas[ROOM_DOORS];
static int sLeafArm[ROOM_DOORS], sLeafHits, sLeafRoom = -2;

int vrhands_blow_at(float out[3]) {
    if (!sBlowOn) {
        return 0;
    }
    if (out != NULL) {
        out[0] = sBlowAt.x;
        out[1] = sBlowAt.y;
        out[2] = sBlowAt.z;
    }
    return 1;
}

int vrhands_swing(void) { return sSwingOn; }

int vrhands_shove_ready(void) { return sShoveOn; }

int vrhands_door_shove(void) { return sDoorShove; }

int vrhands_door_way(void) { return sDoorWay < 0 ? -1 : 1; }

void vrhands_door_take(void) {
    sDoorShove = -1;
    sDoorTick[0] = sDoorTick[1] = -1;
    sDoorHit[0] = sDoorHit[1] = -1;
}

int vrhands_door_hits(void) { return sLeafHits; }

void vrhands_shove_take(void) {
    sShoveOn = 0;
    sShoveCool = SHOVE_COOL;
    sStalkTick[0] = sStalkTick[1] = -1;
}

static int group_shown(const Room *r, int group) {
    if (group <= 0) {
        return 1;
    }
    if (group >= 256) {
        return 0;
    }
    return (r->groups[group >> 5] >> (group & 31) & 1u) != 0;
}

static int tris_near(const MeshVertex *v, int nv, const MeshDraw *d, int nd, Vec3 p, float lim2) {
    int i;

    for (i = 0; i < nd; i++) {
        int n, end;

        if (d[i].count < 3 || d[i].mask) {
            continue;
        }
        end = d[i].first + d[i].count;
        if (end > nv) {
            end = nv;
        }
        for (n = d[i].first; n + 2 < end; n += 3) {
            Vec3 a = vec3(v[n].x, v[n].y, v[n].z);
            Vec3 b = vec3(v[n + 1].x, v[n + 1].y, v[n + 1].z);
            Vec3 c = vec3(v[n + 2].x, v[n + 2].y, v[n + 2].z);

            if (tri_dist2(p, a, b, c) <= lim2) {
                return 1;
            }
        }
    }
    return 0;
}

static int room_touch(const Room *r, Vec3 p) {
    const RoomMesh *m = &r->mesh;
    float lim2 = SWING_SKIN * SWING_SKIN;
    int i;

    for (i = 0; i < m->nd; i++) {
        const MeshDraw *d = &m->d[i];
        int n, end;

        if (d->count < 3 || d->mask || !group_shown(r, d->group)) {
            continue;
        }
        end = d->first + d->count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (n = d->first; n + 2 < end; n += 3) {
            Vec3 a = vec3(m->v[n].x, m->v[n].y, m->v[n].z);
            Vec3 b = vec3(m->v[n + 1].x, m->v[n + 1].y, m->v[n + 1].z);
            Vec3 c = vec3(m->v[n + 2].x, m->v[n + 2].y, m->v[n + 2].z);

            if (tri_dist2(p, a, b, c) <= lim2) {
                return 1;
            }
        }
    }
    return 0;
}

/* The path a hand took this frame, against the room's mesh. Every face of it counts - a wall, the
 * floor, the ceiling, a table's top: there is no look at which way a triangle faces. The path is
 * sampled every 20 cm - at most 32 points, the first the hand's last place and the last where it
 * is now - so a fast sweep cannot step over a thin wall. A sliver with no facing at all (its edges'
 * cross product nothing) is no face of anything. A check only: nothing is blocked. */
#define WALL_SAMPLE 0.2f   /* the most a sample point is from the one before */
#define WALL_SAMPLES 32    /* points along the path at most */
#define WALL_BOX_SLOP 0.01f   /* (a hair of slack on the path's box, so rounding cannot skip a hit) */

int hand_path_hits_wall(const Room *r, Vec3 from, Vec3 to, float skin) {
    const RoomMesh *m = &r->mesh;
    float lim2 = skin * skin;
    Vec3 path = vec3_sub(to, from);
    int pts = (int)(vec3_len(path) / WALL_SAMPLE) + 1;
    Vec3 lo, hi;   /* (the box the samples sit in, the skin wider) */
    int i;

    if (pts > WALL_SAMPLES) {
        pts = WALL_SAMPLES;
    }
    /* every sample lies on from..to, so their box is those two places, the skin wider: a triangle
     * whose own box is clear of it cannot be within the skin of any sample */
    lo = vec3(fminf(from.x, to.x) - skin - WALL_BOX_SLOP, fminf(from.y, to.y) - skin - WALL_BOX_SLOP,
              fminf(from.z, to.z) - skin - WALL_BOX_SLOP);
    hi = vec3(fmaxf(from.x, to.x) + skin + WALL_BOX_SLOP, fmaxf(from.y, to.y) + skin + WALL_BOX_SLOP,
              fmaxf(from.z, to.z) + skin + WALL_BOX_SLOP);
    for (i = 0; i < m->nd; i++) {
        const MeshDraw *d = &m->d[i];
        int n, end;

        if (d->count < 3 || d->mask || !group_shown(r, d->group)) {
            continue;
        }
        end = d->first + d->count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (n = d->first; n + 2 < end; n += 3) {
            Vec3 a = vec3(m->v[n].x, m->v[n].y, m->v[n].z);
            Vec3 b = vec3(m->v[n + 1].x, m->v[n + 1].y, m->v[n + 1].z);
            Vec3 c = vec3(m->v[n + 2].x, m->v[n + 2].y, m->v[n + 2].z);
            Vec3 nrm;
            float nl;
            int s;

            if (fmaxf(a.x, fmaxf(b.x, c.x)) < lo.x || fminf(a.x, fminf(b.x, c.x)) > hi.x ||
                fmaxf(a.y, fmaxf(b.y, c.y)) < lo.y || fminf(a.y, fminf(b.y, c.y)) > hi.y ||
                fmaxf(a.z, fmaxf(b.z, c.z)) < lo.z || fminf(a.z, fminf(b.z, c.z)) > hi.z) {
                continue;   /* (its box is clear of the path's: no sample can be within the skin) */
            }
            nrm = vec3_cross(vec3_sub(b, a), vec3_sub(c, a));
            nl = vec3_len(nrm);
            if (nl <= 0.0f) {
                continue;   /* (a sliver with no facing: no face of anything) */
            }
            for (s = 0; s < pts; s++) {
                Vec3 p = pts > 1 ? vec3_add(from, vec3_scale(path, (float)s / (float)(pts - 1))) : from;

                if (tri_dist2(p, a, b, c) <= lim2) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* How many points a hand's path this frame is swept at: every 20 cm, 32 at most (the wall's own
 * rule, shared by the doors and the placed objects) */
static int path_points(Vec3 from, Vec3 to) {
    int pts = (int)(vec3_len(vec3_sub(to, from)) / WALL_SAMPLE) + 1;

    return pts > WALL_SAMPLES ? WALL_SAMPLES : pts;
}

/* the s'th of `pts` points, from `from` (the first) to `to` (the last) */
static Vec3 path_point(Vec3 from, Vec3 to, int s, int pts) {
    if (pts <= 1) {
        return from;
    }
    return vec3_add(from, vec3_scale(vec3_sub(to, from), (float)s / (float)(pts - 1)));
}

/* Where a hand's path may go in the room's mesh: the free point nearest the first face the path
 * comes within `keep` of - the last place before the surface - or `to` when nothing is in the way.
 * A hand already in contact at `from` is let go: it can always move, and is never trapped. The
 * point returned is free, so asking again from it gives the same answer (the arm is posed twice a
 * frame, once a tick and once a drawn frame, and must not creep).
 * The stop keeps a margin: it is `keep` + 0.02 from the face. A point sitting exactly on the
 * boundary is only free by a float comparison's grace, and it is stored in world space and asked
 * again a frame later, so the 0.02 keeps it clearly free.
 * The path's own end is tested as a place in contact, not only the samples: a step shorter than
 * the 0.2 sampling is a single sample - its start - so a slow push into a surface would otherwise
 * never be seen at all (the samples and the path test both stop at `from`). */
Vec3 path_stop(const Room *r, Vec3 from, Vec3 to, float keep) {
    float stop_keep = keep + 0.02f;
    int pts = path_points(from, to), i, hit = -1;
    Vec3 lo = from, hi = to;

    if (hand_path_hits_wall(r, from, from, stop_keep)) {
        return to;   /* (already in contact: the hand may move, it is never trapped) */
    }
    if (!hand_path_hits_wall(r, from, to, stop_keep) && !hand_path_hits_wall(r, to, to, stop_keep)) {
        return to;   /* (nothing along the path is in contact) */
    }
    for (i = 0; i < pts && hit < 0; i++) {
        Vec3 p = path_point(from, to, i, pts);

        if (hand_path_hits_wall(r, p, p, stop_keep)) {
            hit = i;
        }
    }
    if (hit < 0) {
        if (!hand_path_hits_wall(r, to, to, stop_keep)) {
            /* (the path test hit but no sample did: the last free point is where it stays) */
            return hand_path_hits_wall(r, from, to, stop_keep) ? from : to;
        }
        hi = to;   /* (the path's own end: a step shorter than one sample never reached it) */
        lo = from;
    } else {
        hi = path_point(from, to, hit, pts);
        lo = hit > 0 ? path_point(from, to, hit - 1, pts) : from;   /* (the last free sample) */
    }
    for (i = 0; i < 8; i++) {
        Vec3 mid = vec3_scale(vec3_add(lo, hi), 0.5f);

        if (hand_path_hits_wall(r, mid, mid, stop_keep)) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return lo;   /* (free, and as near the surface as the halving gets) */
}

/* The face nearest a point, within a little of `keep`, and the way it faces - turned toward the
 * side the hand came from (`toward`), so the caller can slide along the free side of it. 0 when no
 * face of the room's mesh is that near. */
static int wall_normal_at(const Room *r, Vec3 p, float keep, Vec3 toward, Vec3 *n) {
    const RoomMesh *m = &r->mesh;
    float lim = keep + 0.05f, best = lim * lim;
    Vec3 face = vec3(0.0f, 0.0f, 0.0f);
    int i, found = 0;

    for (i = 0; i < m->nd; i++) {
        const MeshDraw *d = &m->d[i];
        int t, end;

        if (d->count < 3 || d->mask || !group_shown(r, d->group)) {
            continue;
        }
        end = d->first + d->count;
        if (end > m->nv) {
            end = m->nv;
        }
        for (t = d->first; t + 2 < end; t += 3) {
            Vec3 a = vec3(m->v[t].x, m->v[t].y, m->v[t].z);
            Vec3 b = vec3(m->v[t + 1].x, m->v[t + 1].y, m->v[t + 1].z);
            Vec3 c = vec3(m->v[t + 2].x, m->v[t + 2].y, m->v[t + 2].z);
            Vec3 nrm = vec3_cross(vec3_sub(b, a), vec3_sub(c, a));
            float nl = vec3_len(nrm), dist;

            if (nl <= 0.0f) {
                continue;   /* (a sliver with no facing: no face of anything) */
            }
            dist = tri_dist2(p, a, b, c);
            if (dist > best) {
                continue;
            }
            best = dist;
            face = vec3_scale(nrm, 1.0f / nl);
            found = 1;
        }
    }
    if (!found) {
        return 0;
    }
    if (vec3_dot(face, vec3_sub(toward, p)) < 0.0f) {
        face = vec3_scale(face, -1.0f);   /* (the free side is the side the hand came from) */
    }
    *n = face;
    return 1;
}

/* Where a hand may go, sliding: the stop is asked for, and where it met a surface the rest of the
 * motion is taken into that surface only sideways - the part pressing into the face is dropped -
 * and the stop is asked again from there, up to three rounds, so a hand pushing at an angle slides
 * along the wall instead of sticking. Pushing straight in leaves nothing sideways: it stops dead.
 * Each round's answer is free of the surface, and the whole path is swept, so a fast hand cannot
 * pass through a thin wall. */
Vec3 path_slide(const Room *r, Vec3 from, Vec3 to, float keep) {
    Vec3 pos = from, target = to;
    int round;

    for (round = 0; round < 3; round++) {
        Vec3 s = path_stop(r, pos, target, keep), n, rest;

        if (vec3_len(vec3_sub(s, target)) < 0.001f) {
            return target;   /* (the whole step got through) */
        }
        if (!wall_normal_at(r, s, keep, pos, &n)) {
            return s;        /* (no face near the stop: it is the answer) */
        }
        rest = vec3_sub(target, s);
        rest = vec3_sub(rest, vec3_scale(n, vec3_dot(rest, n)));   /* (the sideways part only) */
        pos = s;
        target = vec3_add(s, rest);
    }
    return pos;
}

/* The path a hand took this frame through a door leaf. A leaf is the box its own frame holds (12
 * wide, 22 tall, 1 thick): every face counts, so a shut door's face and an open one's edge are
 * hits alike, and no steep-normal test is needed. The door this hand holds (`skip`) is left out. */
int hand_path_hits_door(const Engine *e, int skip, Vec3 from, Vec3 to, float skin) {
    int pts = path_points(from, to);
    int i, s;

    for (i = 0; i < ROOM_DOORS; i++) {
        const RoomDoor *d = &e->room.doors[i];

        if (i == skip || !d->present) {
            continue;
        }
        for (s = 0; s < pts; s++) {
            Vec3 q = door_local(d, path_point(from, to, s, pts));

            if (in_box(q, vec3(0.0f, 0.0f, -1.0f), vec3(12.0f, 22.0f, 0.0f), skin)) {
                return 1;
            }
        }
    }
    return 0;
}

/* The path a hand took this frame through a placed object. Its mesh is read as placed_touch reads
 * it (shown, with a mesh, in the object's world); every face counts - a table's top as much as its
 * side - so there is no steep-normal test. The object this hand holds (`skip`) is left out. */
int hand_path_hits_placed(const PlacedSet *s, int skip, Vec3 from, Vec3 to, float skin) {
    float lim2 = skin * skin;
    int pts = path_points(from, to);
    int i;

    for (i = 0; i < s->n; i++) {
        const Placed *o = &s->p[i];
        Mat4 w;
        int n;

        if (i == skip || !o->shown || o->mesh.v == NULL || o->mesh.nv < 3) {
            continue;
        }
        w = placed_world(o);
        for (n = 0; n + 2 < o->mesh.nv; n += 3) {
            Vec3 a = mat4_point(&w, vec3(o->mesh.v[n].x, o->mesh.v[n].y, o->mesh.v[n].z));
            Vec3 b = mat4_point(&w, vec3(o->mesh.v[n + 1].x, o->mesh.v[n + 1].y, o->mesh.v[n + 1].z));
            Vec3 c = mat4_point(&w, vec3(o->mesh.v[n + 2].x, o->mesh.v[n + 2].y, o->mesh.v[n + 2].z));
            int k;

            for (k = 0; k < pts; k++) {
                if (tri_dist2(path_point(from, to, k, pts), a, b, c) <= lim2) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int placed_touch(const PlacedSet *s, Vec3 p) {
    float lim2 = SWING_SKIN * SWING_SKIN;
    int i;

    for (i = 0; i < s->n; i++) {
        const Placed *o = &s->p[i];
        Mat4 w;
        int n;

        if (!o->shown || o->mesh.v == NULL || o->mesh.nv < 3) {
            continue;
        }
        w = placed_world(o);
        for (n = 0; n + 2 < o->mesh.nv; n += 3) {
            Vec3 a = mat4_point(&w, vec3(o->mesh.v[n].x, o->mesh.v[n].y, o->mesh.v[n].z));
            Vec3 b = mat4_point(&w, vec3(o->mesh.v[n + 1].x, o->mesh.v[n + 1].y, o->mesh.v[n + 1].z));
            Vec3 c = mat4_point(&w, vec3(o->mesh.v[n + 2].x, o->mesh.v[n + 2].y, o->mesh.v[n + 2].z));

            if (tri_dist2(p, a, b, c) <= lim2) {
                return 1;
            }
        }
    }
    return 0;
}

static int stalker_name(const char *name) {
    return strncmp(name, "O_DB", 4) == 0 || strncmp(name, "O_DN", 4) == 0 || strncmp(name, "O_LR", 4) == 0 ||
           strncmp(name, "O_RC", 4) == 0;
}

static int stalker_touch(Vec3 p) {
    float lim2 = SHOVE_REACH * SHOVE_REACH;
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];

        if (!a->used || !a->visible || a->posed == NULL || a->model.d == NULL || !stalker_name(a->name)) {
            continue;
        }
        if (tris_near(a->posed, a->model.nv, a->model.d, a->model.nd, p, lim2)) {
            return 1;
        }
    }
    return 0;
}

static float box_gap(Vec3 p, Vec3 lo, Vec3 hi) {
    float dx = p.x < lo.x ? lo.x - p.x : p.x > hi.x ? p.x - hi.x : 0.0f;
    float dy = p.y < lo.y ? lo.y - p.y : p.y > hi.y ? p.y - hi.y : 0.0f;
    float dz = p.z < lo.z ? lo.z - p.z : p.z > hi.z ? p.z - hi.z : 0.0f;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* shut, unlocked, not held, not stuck, and not already swinging */
static int door_shut_free(const Engine *e, int exit) {
    const RoomDoor *d;
    int id;

    if (exit < 0 || exit >= ROOM_DOORS) {
        return 0;
    }
    d = &e->room.doors[exit];
    if (!d->present || d->mode != 0 || fabsf(d->swing) > DOOR_LATCH || door_blocked(e, exit)) {
        return 0;
    }
    id = door_index(e, exit);
    return id < 0 || (gProgress.doors[id] & 4u) == 0;
}

static int door_shove_touch(const Engine *e, Vec3 hand, int *exit) {
    int i, found = -1;
    float best = SHOVE_REACH;

    for (i = 0; i < ROOM_DOORS; i++) {
        Vec3 q;
        float dist;

        if (!door_shut_free(e, i)) {
            continue;
        }
        q = door_local(&e->room.doors[i], hand);
        dist = box_gap(q, vec3(0.0f, 0.0f, -0.5f), vec3(12.0f, 22.0f, 0.5f));
        if (dist <= best) {
            best = dist;
            found = i;
        }
    }
    if (found < 0) {
        return 0;
    }
    *exit = found;
    return 1;
}

/* the headset's own step this frame, so a run does not count as a swing */
static void swing_body(void) {
    Vec3 head;

    sBodyDelta = vec3(0.0f, 0.0f, 0.0f);
    sFwd = vec3(0.0f, 0.0f, -1.0f);
    if (!vr_head_local(&head)) {
        sHeadHave = 0;
        return;
    }
    if (!vr_head_forward(&sFwd)) {
        sFwd = vec3(0.0f, 0.0f, -1.0f);
    }
    if (sHeadHave) {
        sBodyDelta = vec3_sub(head, sHeadLocal);
    }
    sHeadLocal = head;
    sHeadHave = 1;
}

/* the script frame just read last tick's blows. A fast hand on a mesh sets the next one. */
static void swing_begin(void) {
    sBlowOn = 0;
    sShoveOn = 0;
    sSwingOn = 0;
    sDoorShove = -1;
    if (sEnvCool > 0) {
        sEnvCool--;
    }
    if (sShoveCool > 0) {
        sShoveCool--;
    }
    if (sVoiceCool > 0) {
        sVoiceCool--;
    }
}

static void swing_hand(int side, Vec3 at) {
    VrController c;
    Vec3 moved;
    float speed, forward;
    int now = (int)gEngine.ticks;
    int door = -1, on_stalker;

    if (!vr_controller(side, &c) || !c.tracked) {
        sHandHave[side] = 0;
        return;
    }
    if (!sHandHave[side]) {
        sHandLocal[side] = c.at;
        return;
    }
    /* play space, with the body's own step taken off, and only a shove toward where she looks */
    moved = vec3_sub(vec3_sub(c.at, sHandLocal[side]), sBodyDelta);
    sHandLocal[side] = c.at;
    forward = vec3_dot(moved, sFwd);
    speed = vec3_len(moved);
    if (forward < SWING_SPEED || speed > SWING_FAST) {
        return;
    }
    if (sVoiceCool <= 0) {
        sSwingOn = 1;
        sVoiceCool = VOICE_COOL;
    }
    if (door_shove_touch(&gEngine, at, &door)) {
        sDoorTick[side] = now;
        sDoorHit[side] = door;
        sDoorZ[side] = door_local(&gEngine.room.doors[door], at).z;
        return;
    }
    on_stalker = stalker_touch(at);
    if (on_stalker && sShoveCool <= 0) {
        sStalkTick[side] = now;
        return;
    }
    if (!on_stalker && sEnvCool <= 0 && (room_touch(&gEngine.room, at) || placed_touch(&gEngine.room.placed, at))) {
        sBlowAt = at;
        sBlowOn = 1;
        sEnvCool = ENV_COOL;
    }
}

static void swing_finish(void) {
    int now = (int)gEngine.ticks;
    int a = sStalkTick[0], b = sStalkTick[1];
    int da = sDoorTick[0], db = sDoorTick[1];

    if (sShoveCool <= 0 && a >= 0 && b >= 0 && now - a <= SHOVE_WINDOW && now - b <= SHOVE_WINDOW) {
        sShoveOn = 1;
        sStalkTick[0] = sStalkTick[1] = -1;
    }
    if (da >= 0 && db >= 0 && sDoorHit[0] == sDoorHit[1] && now - da <= SHOVE_WINDOW && now - db <= SHOVE_WINDOW &&
        door_shut_free(&gEngine, sDoorHit[0])) {
        sDoorShove = sDoorHit[0];
        sDoorWay = (sDoorZ[0] + sDoorZ[1]) >= 0.0f ? 1 : -1;
        sDoorTick[0] = sDoorTick[1] = -1;
        sDoorHit[0] = sDoorHit[1] = -1;
    }
}

static int on_leaf(const RoomDoor *d, float swing, Vec3 p) {
    Vec3 q = door_local_at(d, p, swing);

    return q.x >= -DOOR_HIT_MARGIN && q.x <= 12.0f + DOOR_HIT_MARGIN && q.y >= -DOOR_HIT_MARGIN &&
           q.y <= 22.0f + DOOR_HIT_MARGIN && q.z >= -DOOR_HIT_MARGIN && q.z <= DOOR_HIT_MARGIN;
}

static int leaf_meets_point(const RoomDoor *d, float from, float to, Vec3 p) {
    float span = to - from;
    int n = (int)(fabsf(span) / 8.0f) + 1;
    int i;

    if (n > 6) {
        n = 6;
    }
    for (i = 0; i <= n; i++) {
        if (on_leaf(d, from + span * ((float)i / (float)n), p)) {
            return 1;
        }
    }
    return 0;
}

static int leaf_meets(const RoomDoor *d, float from, float to) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];
        Vec3 p, mid, high;

        if (!a->used || !a->visible || !stalker_name(a->name)) {
            continue;
        }
        p = a->pos;
        mid = vec3(p.x, p.y + 10.0f, p.z);
        high = vec3(p.x, p.y + 18.0f, p.z);
        if (leaf_meets_point(d, from, to, p) || leaf_meets_point(d, from, to, mid) || leaf_meets_point(d, from, to, high)) {
            return 1;
        }
    }
    return 0;
}

/* a leaf that moved fast this frame, however it was driven, and a stalker standing in that sweep */
static void leaf_scan(Engine *e) {
    int i, hits = 0;

    if (e->room.id != sLeafRoom) {
        sLeafRoom = e->room.id;
        for (i = 0; i < ROOM_DOORS; i++) {
            sSwingWas[i] = e->room.doors[i].swing;
            sLeafArm[i] = 0;
        }
        sLeafHits = 0;
        return;
    }
    for (i = 0; i < ROOM_DOORS; i++) {
        RoomDoor *d = &e->room.doors[i];
        float s = d->present ? d->swing : sSwingWas[i];
        float delta = s - sSwingWas[i];

        sSwingWas[i] = s;
        if (!d->present || fabsf(delta) < DOOR_HIT_SPEED) {
            sLeafArm[i] = 0;
            continue;
        }
        if (sLeafArm[i]) {
            continue;
        }
        if (leaf_meets(d, s - delta, s)) {
            sLeafArm[i] = 1;
            hits |= 1 << i;
        }
    }
    sLeafHits = hits;
}

static int defense_kind(int item, int *drop) {
    switch (item) {
    case 0x90: *drop = 0; return 0; /* ball */
    case 0x91: *drop = 0; return 1; /* antimony powder */
    case 0x93: *drop = 0; return 3; /* bisque doll */
    case 0x94: *drop = 0; return 4; /* refined antimony */
    case 0x97: *drop = 0; return 6; /* antimony tube */
    case 0x92: *drop = 1; return 2; /* magnesia */
    case 0x95: *drop = 1; return 5; /* refined magnesia */
    case 0x98: *drop = 1; return 7; /* prima magnesia */
    case 0x9B: *drop = 1; return 8; /* weeping stone */
    default: return -1;
    }
}

static int world_held(void) { return (gProgress.state[0] & (1u << 8)) != 0; }

static void defense_hide(void) {
    sDef.item = -1;
    sDef.kind = -1;
    sDef.armed = 0;
    sDef.saw_low = 0;
    sDef.shown = 0;
    sDef.histN = 0;
}

static void defense_note(Vec3 at) {
    if (sDef.histN < 4) {
        sDef.hist[sDef.histN++] = at;
        return;
    }
    sDef.hist[0] = sDef.hist[1];
    sDef.hist[1] = sDef.hist[2];
    sDef.hist[2] = sDef.hist[3];
    sDef.hist[3] = at;
}

static Vec3 defense_flick(Vec3 fwd, Vec3 up) {
    Vec3 best = vec3(0.0f, 0.0f, 0.0f);
    float bd = 0.0f;
    int i;

    for (i = 1; i < sDef.histN; i++) {
        Vec3 d = vec3_sub(sDef.hist[i], sDef.hist[i - 1]);
        float L = vec3_len(d);

        if (L > bd) {
            bd = L;
            best = d;
        }
    }
    if (bd < 2.0f) {
        return vec3_add(vec3_scale(fwd, 6.0f), vec3_scale(up, 0.4f));
    }
    if (bd > 12.0f) {
        return vec3_scale(best, 12.0f / bd);
    }
    return best;
}

static Vec3 defense_place(Vec3 at, Vec3 up, Vec3 fwd) {
    return vec3_add(at, vec3_add(vec3_scale(up, 0.35f), vec3_scale(fwd, 0.6f)));
}

/* ---- the key item the menu equipped in her hand (gEngine.hand_item) ---- */

static void hand_item_hide(void) {
    sHand.item = -1;
    sHand.shown = 0;
}

/* a tick: is the key item shown, and in which hand (the right, or the left when that is the only
 * one tracked). The magnesia/antimony in slot 2 has the hand instead, and nothing is shown while
 * VR does not play (a menu, a cutscene - her eyes' anchor is cleared then - or flat screen). */
static void hand_item_tick(void) {
    int item = gEngine.hand_item, side;
    Vec3 at, right, up, fwd;
    float grip, index, thumb;

    if (item < 0 || !vr_active() || sDef.shown) {
        hand_item_hide();
        return;
    }
    for (side = 1; side >= 0; side--) {
        if (sArmFound[side] && hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            sHand.item = item;
            sHand.side = side;
            sHand.at = at;
            sHand.right = right;
            sHand.up = up;
            sHand.fwd = fwd;
            sHand.shown = 1;
            loot_preload_held(item, side);   /* (its model, off the render pass) */
            return;
        }
    }
    hand_item_hide();
}

/* where the hand item's model sits in the room - her wrist and the item's grip line, the matrix
 * item_prop_draw draws it from (its origin): 0 when none is shown (a trigger check, and the
 * scripts' `hand-item-at`) */
int vrhands_hand_item_at(Vec3 *out) {
    return sHand.shown && sHand.item >= 0 ? loot_held_origin(sHand.item, sHand.side, out) : 0;
}

/* drawn from the live hand frame, as the equipped thing is, so it sits in the hand on every
 * drawn frame - a menu's frame too (the hand is still there). Nothing to draw in a cutscene
 * (her eyes' anchor is cleared then) or flat screen. */
static void hand_item_draw(const Mat4 *view_proj) {
    Mat4 m;
    Vec3 at, right, up, fwd;
    float grip = 0.0f, index = 0.0f, thumb = 0.0f;

    if (!sHand.shown || sHand.item < 0 || gEngine.hand_item < 0 || view_proj == NULL || !vr_active()) {
        return;
    }
    at = sHand.at;
    right = sHand.right;
    up = sHand.up;
    fwd = sHand.fwd;
    if (!hand_world(sHand.side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
        return;   /* (no tracked hand: a menu, a scene - nothing left hanging in the room) */
    }
    at = defense_place(at, up, fwd);
    m = mat4_identity();
    m.m[0] = right.x;
    m.m[1] = right.y;
    m.m[2] = right.z;
    m.m[4] = up.x;
    m.m[5] = up.y;
    m.m[6] = up.z;
    m.m[8] = fwd.x;
    m.m[9] = fwd.y;
    m.m[10] = fwd.z;
    m.m[12] = at.x;
    m.m[13] = at.y;
    m.m[14] = at.z;
    loot_draw_held(sHand.item, sHand.side, &m, view_proj);
}

static void defense_tick(void) {
    int item, kind, drop = 0, side;
    Vec3 at, right, up, fwd, place;
    float grip, index, thumb;

    if (!vr_active()) {
        defense_hide();
        return;
    }
    item = gProgress.equipped[2];
    kind = defense_kind(item, &drop);
    if (kind < 0) {
        defense_hide();
        return;
    }
    if (item != sDef.item || kind != sDef.kind) {
        sDef.item = item;
        sDef.kind = kind;
        sDef.drop = drop;
        sDef.armed = 0;
        sDef.saw_low = 0;
        sDef.histN = 0;
        sDef.side = 1;
    }
    side = sDef.side;
    if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
        if (!hand_world(side ^ 1, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            return;
        }
        side = side ^ 1;
        if (side != sDef.side) {
            sDef.side = side;
            sDef.armed = 0;
            sDef.saw_low = 0;
            sDef.histN = 0;
        }
    }
    loot_preload_held(item, side);   /* (its model and the debug axes, off the render pass) */
    place = defense_place(at, up, fwd);
    sDef.at = place;
    sDef.right = right;
    sDef.up = up;
    sDef.fwd = fwd;
    sDef.shown = 1;
    sDef.kind = kind;
    sDef.drop = drop;
    defense_note(place);
    if (world_held() || sHold[side] != HOLD_NONE) {
        return;
    }
    if (!sDef.armed) {
        if (grip <= 0.4f) {
            sDef.saw_low = 1;
        }
        if (sDef.saw_low && grip > 0.65f) {
            sDef.armed = 1;
        }
        return;
    }
    if (grip <= 0.4f) {
        sDef.pending = 1;
        sDef.pkind = kind;
        sDef.pat = place;
        sDef.pvel = sDef.drop ? vec3(0.0f, 0.0f, 0.0f) : defense_flick(fwd, up);
        defense_hide();
    }
}

void vrhands_defense_draw(const Mat4 *view_proj) {
    Mat4 m;
    Vec3 at, right, up, fwd;
    float grip = 0.0f, index = 0.0f, thumb = 0.0f;

    hand_item_draw(view_proj);   /* (the key item the menu equipped: never the defense thing) */
    if (!sDef.shown || sDef.kind < 0 || view_proj == NULL) {
        return;
    }
    /* where the hand is drawn this frame, not where the last tick left it: the arms are posed from
     * the same frame, so the thing sits in the hand while she walks (loot.c's held things too) */
    at = sDef.at;
    right = sDef.right;
    up = sDef.up;
    fwd = sDef.fwd;
    if (hand_world(sDef.side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
        at = defense_place(at, up, fwd);
    }
    m = mat4_identity();
    m.m[0] = right.x;
    m.m[1] = right.y;
    m.m[2] = right.z;
    m.m[4] = up.x;
    m.m[5] = up.y;
    m.m[6] = up.z;
    m.m[8] = fwd.x;
    m.m[9] = fwd.y;
    m.m[10] = fwd.z;
    m.m[12] = at.x;
    m.m[13] = at.y;
    m.m[14] = at.z;
    loot_draw_held(sDef.item, sDef.side, &m, view_proj);
}

int vrhands_defense_take(int *kind, float *x, float *y, float *z, float *vx, float *vy, float *vz) {
    if (!sDef.pending) {
        *kind = 0;
        *x = *y = *z = *vx = *vy = *vz = 0.0f;
        return 0;
    }
    *kind = sDef.pkind;
    *x = sDef.pat.x;
    *y = sDef.pat.y;
    *z = sDef.pat.z;
    *vx = sDef.pvel.x;
    *vy = sDef.pvel.y;
    *vz = sDef.pvel.z;
    sDef.pending = 0;
    return 1;
}

int vrhands_petting(int side) {
    const Actor *dog;
    Vec3 at, right, up, fwd;
    float grip, index, thumb;
    int bone = -1;

    if (side < 0 || side > 1 || !vr_active()) {
        return 0;
    }
    pet_open();
    dog = hewie_mesh();
    if (dog == NULL) {
        return 0;
    }
    if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
        return 0;
    }
    (void)right;
    (void)up;
    (void)fwd;
    (void)grip;
    (void)index;
    (void)thumb;
    if (!mesh_touch(dog, at, &bone)) {
        return 0;
    }
    pet_push(dog, bone, at);
    vr_haptic(side, 0.075f, 0.04f);
    return 1;
}

/* The voice's "shake": a hand on Hewie's lifted front paw. The paw the animation holds up is
 * PAW_BONE; a hand within PAW_REACH of it, its grip closed, has it. The paw and the leg above
 * are turned towards the hand (pet_push's kind 5), and the hand's side-to-side turns in the dog's
 * frame are the shakes. A new grab buzzes once; each counted shake buzzes too. The scripts read
 * this while the shake action lasts (scripts/hewie/states.fs, st-shake-paw). */
void vrhands_paw_release(void) {
    sPawSide = -1;
    sPawBone = -1;
    sPawFresh = 0;
    sPawShakes = 0;
    sPawDir = 0;
    sPawHave = 0;
}

int vrhands_paw_shake(void) {
    int n = sPawShakes;

    sPawShakes = 0;
    return n;
}

int vrhands_paw(void) {
    const Actor *dog;
    Vec3 bestAt = vec3(0.0f, 0.0f, 0.0f);
    float bestD = PAW_REACH * PAW_REACH;
    int side, bestSide = -1, parent;

    pet_open();   /* (the paw's push is this tick's, as the petting's is) */
    dog = hewie_mesh();
    if (dog == NULL || dog->model.nbones <= PAW_BONE) {
        sPawSide = -1;
        sPawBone = -1;
        sPawFresh = 0;
        sPawHave = 0;
        return 0;
    }
    for (side = 0; side < 2; side++) {
        Vec3 at, right, up, fwd, paw;
        float grip, index, thumb, dx, dy, dz, d2;

        if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb) || grip < PAW_GRIP) {
            continue;
        }
        paw = dog->bones[PAW_BONE];
        dx = at.x - paw.x;
        dy = at.y - paw.y;
        dz = at.z - paw.z;
        d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= bestD) {
            bestD = d2;
            bestSide = side;
            bestAt = at;
        }
    }
    if (bestSide < 0) {
        /* let go of the paw, keeping the shakes not yet taken (the take is the scripts') */
        sPawSide = -1;
        sPawBone = -1;
        sPawFresh = 0;
        sPawHave = 0;
        return 0;
    }
    if (sPawSide != bestSide) {
        sPawSide = bestSide;
        sPawBone = PAW_BONE;
        sPawFresh = 1;
        sPawShakes = 0;
        sPawDir = 0;
        sPawHave = 0;
    }
    pet_push(dog, PAW_BONE, bestAt);
    parent = dog->model.bones[PAW_BONE].parent;
    if (parent >= 0) {
        pet_push(dog, parent, bestAt);
    }
    {
        Vec3 right = vec3(cosf(dog->yaw), 0.0f, -sinf(dog->yaw));
        float at = (bestAt.x - dog->pos.x) * right.x + (bestAt.z - dog->pos.z) * right.z;

        if (!sPawHave) {
            sPawAt = at;
            sPawHave = 1;
        } else {
            float d = at - sPawAt;

            if (fabsf(d) >= PAW_SHAKE_STEP) {
                int dir = d > 0.0f ? 1 : -1;

                if (sPawDir != 0 && dir != sPawDir) {
                    sPawShakes++;
                    vr_haptic(sPawSide, 0.12f, 0.02f);
                }
                sPawDir = dir;
                sPawAt = at;
            }
        }
    }
    if (sPawFresh) {
        vr_haptic(sPawSide, 0.15f, 0.04f);
        sPawFresh = 0;
    }
    return 1;
}

/* ---- a ladder's grab zone ---- */
#define LADDER_GRAB_REACH 8.0f   /* world units: how near the line a ladder makes across the floor a hand grabs it */
#define LADDER_GRAB_RISE 5.0f    /* world units: how far past its top or its foot a hand still reaches it */
#define LADDER_GRAB_GRIP 0.5f    /* the grip at which a free hand takes a ladder (softer than the door grab's 0.65; the release stays at 0.4) */
#define LADDER_ANIM_FIRST 0x700  /* her first on-ladder animation: on from the top (scripts/fiona/anims.fs) */
#define LADDER_ANIM_LAST 0x709   /* her last: holding on, either kind ($708, $709) */
#define FIONA_MODEL_PREFIX "O_FI"   /* her model's files: O_FIN/FIN_000, O_FIS/FIS_000, ... */

/* how far (x, z) is from the segment a-b, in the horizontal plane */
static float flat_line_dist(Vec3 p, Vec3 a, Vec3 b) {
    float ax = b.x - a.x, az = b.z - a.z, len2 = ax * ax + az * az, t, dx, dz;

    if (len2 <= 0.0f) {   /* (a ladder upright on one spot) */
        dx = p.x - a.x;
        dz = p.z - a.z;
        return sqrtf(dx * dx + dz * dz);
    }
    t = ((p.x - a.x) * ax + (p.z - a.z) * az) / len2;
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    dx = p.x - (a.x + ax * t);
    dz = p.z - (a.z + az * t);
    return sqrtf(dx * dx + dz * dz);
}

int vrhands_ladder_link(const NavMesh *nav, Vec3 at) {
    int i;

    if (nav == NULL) {
        return -1;
    }
    for (i = 0; i < nav->nlinks; i++) {
        const NavLink *l = &nav->links[i];
        float lo = l->spot[0].y, hi = l->spot[1].y;

        if (lo > hi) {   /* (the game's y runs down: the top is the smaller) */
            float swap = lo;

            lo = hi;
            hi = swap;
        }
        if (at.y < lo - LADDER_GRAB_RISE || at.y > hi + LADDER_GRAB_RISE) {
            continue;
        }
        if (flat_line_dist(at, l->spot[0], l->spot[1]) <= LADDER_GRAB_REACH) {
            return i;
        }
    }
    return -1;
}

/* the current room's ladder a hand at `at` is in the grab zone of, -1 none */
static int ladder_grab_link(Vec3 at) {
    return vrhands_ladder_link(&gEngine.room.nav, at);
}

/* Fiona on a ladder: her model is playing one of the ladder's own animations. Her mode (3,
 * ladder-mode) is a field of her Forth state, which only her own actor's words may read
 * (ClankForth's field words give the current actor's), so her model's motion stands in: only her
 * climbing plays these ids (scripts/fiona/ladder.fs, scripts/fiona/anims.fs) */
static int ladder_climbing(void) {
    int i;

    for (i = 0; i < MAX_ACTORS; i++) {
        const Actor *a = &gEngine.actors[i];

        if (a->used && strncmp(a->name, FIONA_MODEL_PREFIX, sizeof(FIONA_MODEL_PREFIX) - 1) == 0) {
            int id = a->motion >= 0 ? model_motion_id(&a->model, (int)a->motion) : -1;

            return id >= LADDER_ANIM_FIRST && id <= LADDER_ANIM_LAST;
        }
    }
    return 0;
}

int vrhands_ladder_hold(int side) {
    return side >= 0 && side < 2 ? sLadderHold[side] : -1;
}

int vrhands_ladder_hands(void) {
    return vrhands_ladder_hold(0) >= 0 || vrhands_ladder_hold(1) >= 0;
}

int vrhands_ladder_both(void) {   /* true when both hands hold a ladder */
    return vrhands_ladder_hold(0) >= 0 && vrhands_ladder_hold(1) >= 0;
}

/* a hand's new ladder grab, taken once: its link and the side nearer the grab (0 the nav foot, 1 the
 * top); link -1 none */
void vrhands_ladder_grab(int *link, int *side) {
    const NavMesh *nav = &gEngine.room.nav;
    int h;

    *link = -1;
    *side = -1;
    for (h = 0; h < 2; h++) {
        const NavLink *l;
        Vec3 a, s0, s1;
        float d0, d1;
        int at;

        if (!sLadderFresh[h]) {
            continue;
        }
        sLadderFresh[h] = 0;   /* (taken: a grab is never kept for later) */
        at = sLadderHold[h];
        if (at < 0 || at >= nav->nlinks) {
            return;   /* (its ladder is gone: nothing to take) */
        }
        l = &nav->links[at];
        a = sLadderGrabAt[h];
        s0 = l->spot[0];
        s1 = l->spot[1];
        d0 = (a.x - s0.x) * (a.x - s0.x) + (a.y - s0.y) * (a.y - s0.y) + (a.z - s0.z) * (a.z - s0.z);
        d1 = (a.x - s1.x) * (a.x - s1.x) + (a.y - s1.y) * (a.y - s1.y) + (a.z - s1.z) * (a.z - s1.z);
        *link = at;
        *side = d1 < d0 ? 1 : 0;
        return;
    }
}

float vrhands_ladder_pull(void) {
    float pulled = sLadderPull;

    sLadderPull = 0.0f;
    return pulled;
}

float ladder_pull_step(Vec3 *anchor, Vec3 play) {
    float down = anchor->y - play.y;   /* (the play-space y grows as the hand is pulled down: this is minus that rise) */

    *anchor = play;   /* (moved on with the hand, so the same motion is never counted twice) */
    return down;      /* (a rise of that y moves her body up: the body's y goes down, the game's y running the other way) */
}

static void spot_log_line(const char *msg) {
    if (gEngine.forth != NULL) {
        forth_printf(gEngine.forth, "%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

/* (DEBUG) the spot that hand takes, and how */
static void spot_take_log(int id, const char *how, Vec3 at) {
    char msg[192];

    snprintf(msg, sizeof(msg), "vrspots: take '%s' by %s c=%.3f hand=(%.2f %.2f %.2f)\n", vrspot_name(id), how,
             (double)vrspot_closure_of(id), at.x, at.y, at.z);
    spot_log_line(msg);
}

/* (DEBUG) the spot that hand lets go of, and why */
static void spot_release_log(int id, const char *why) {
    char msg[192];

    snprintf(msg, sizeof(msg), "vrspots: release '%s' reason=%s c=%.3f\n", vrspot_name(id), why,
             (double)vrspot_closure_of(id));
    spot_log_line(msg);
}

/* (DEBUG: a grip that came near a hiding spot and missed it - the box tuning; `vr-spot-debug!`) */
static void spot_miss_log(int id, Vec3 at, float grab_d, float edge_d) {
    char msg[192];

    snprintf(msg, sizeof(msg), "vrspots: miss %s at(%.2f %.2f %.2f) edge-dist %.2f grab-dist %.2f\n",
             vrspot_name(id), at.x, at.y, at.z, edge_d, grab_d);
    if (gEngine.forth != NULL) {
        forth_printf(gEngine.forth, "%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

/* (DEBUG: a grip that came near a lever and missed it - the box tuning) */
static void lever_miss_log(int id, Vec3 at, float box_d, float handle_d) {
    char msg[192];

    snprintf(msg, sizeof(msg), "vrlevers: miss '%s' at(%.2f %.2f %.2f) box-dist %.2f handle-dist %.2f\n",
             vrlever_name(id), at.x, at.y, at.z, box_d, handle_d);
    if (gEngine.forth != NULL) {
        forth_printf(gEngine.forth, "%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

void vrhands_tick(Engine *e) {
    Vec3 eye;
    float yaw;
    int side, i;

    if (e == NULL) {
        return;
    }
    hactor_set_blow_at(vrhands_blow_at);   /* hactor's `body-point` asks the hands for a hard swing here */
    swing_begin();
    pet_settle();
    poses_restore(e);
    if (sPushRoom != e->room.id) {
        doors_room_change(e, sPushRoom);   /* (a hold must not outlive its room: an exit is not a door) */
    }
    if (!vr_active() || !vr_anchor_get(&eye, &yaw)) {
        poses_save(e);
        leaf_scan(e);
        return;
    }
    swing_body();
    if (sPushRoom != e->room.id) {
        memset(sPushNow, 0, sizeof(sPushNow));
        memset(sPushWas, 0, sizeof(sPushWas));
        sHandHave[0] = sHandHave[1] = 0;        /* (another room's doors and walls are not the old's) */
        sHandStopHave[0] = sHandStopHave[1] = 0;
        vrbody_forget_place();                  /* (nor its smoothed hands: no arm stretched across) */
        sLadderHold[0] = sLadderHold[1] = -1;   /* (another room's links are not these) */
        sLadderFresh[0] = sLadderFresh[1] = 0;
        sLadderAnchorHave[0] = sLadderAnchorHave[1] = 0;
        sLadderPull = 0.0f;
        sDoorShove = -1;                        /* (the two hands' shove names an exit too) */
        sDoorTick[0] = sDoorTick[1] = -1;
        sDoorHit[0] = sDoorHit[1] = -1;
        for (i = 0; i < 2; i++) {
            if (sHold[i] == HOLD_SPOT || sHold[i] == HOLD_LEVER || sHold[i] == HOLD_MIRROR) {
                if (sHold[i] == HOLD_SPOT) {
                    spot_release_log(sHoldId[i], "room");   /* (DEBUG) */
                }
                if (sHold[i] == HOLD_MIRROR) {
                    vrmirror_log_release(sHoldId[i], "room");   /* (DEBUG) */
                }
                sHold[i] = HOLD_NONE;   /* (another room's spots, levers and mirror are not these) */
            }
        }
        sPushRoom = e->room.id;
    }
    for (side = 0; side < 2; side++) {
        Vec3 at, right, fwd, up, off;
        float grip, index, thumb, doorD, propD, wardD;
        int rising, door, axis = 0, prop, ward;

        if (!hand_world(side, &at, &right, &up, &fwd, &grip, &index, &thumb)) {
            if (sHold[side] == HOLD_LOOT) {
                loot_let_go(sHoldId[side], side);
                sHold[side] = HOLD_NONE;
            } else if (sHold[side] != HOLD_NONE) {
                release_hand(&e->room, side);
            }
            sGripWas[side] = 0.0f;
            sHandHave[side] = 0;
            sLadderHold[side] = -1;   /* (no hand, no ladder) */
            sLadderFresh[side] = 0;
            sLadderAnchorHave[side] = 0;   /* (and no anchor: what was pulled and not taken is dropped) */
            sLadderPull = 0.0f;
            sHandStopHave[side] = 0;   /* (no hand, no stop: the next one takes the hand as it is) */
#if 0   /* (temporary: no hand, no leaf in reach) */
            sWardNear[side] = 0;   /* (temporary: no hand, no leaf in reach) */
#endif
            sWallNear[side] = 0;   /* (no hand, no path: a wall is entered anew) */
            if (sGrace[side] > 0) {
                sGrace[side]--;
            }
            continue;
        }
        (void)right;
        (void)index;
        (void)thumb;
        off = vec3_scale(fwd, hand_length());   /* (the fingertip: the wrist's place, plus this) */
        rising = grip > 0.65f && sGripWas[side] <= 0.4f;
        if (rising) {
            int rattled = -1, rattAxis = 0;
            float rattD = DOOR_REACH_MAX * DOOR_REACH_MAX;   /* (the widest reach a knob may be taken at) */

            for (i = 0; i < ROOM_DOORS; i++) {
                RoomDoor *d = &e->room.doors[i];
                int ax = 0;
                float kn;

                if (!d->present || !door_locked(e, i) || !touching_knob(e, i, d, at, &ax)) {
                    continue;
                }
                kn = dist2(at, handle_at(d, ax));
                if (rattled < 0 || kn < rattD) {
                    rattled = i;
                    rattAxis = ax;
                    rattD = kn;
                }
            }
            if (rattled >= 0) {
                engine_sound_at(RATTLE_SOUND, RATTLE_BANK, handle_at(&e->room.doors[rattled], rattAxis));
            }
#if 0   /* (temporary: what a rising grip sees of the wardrobe's leaves) */
            if (sWardLogs < 20) {   /* (temporary: what a rising grip sees of the wardrobe's leaves) */
                float d = 0.0f;

                if (ward_near(&e->room.placed, at, 60.0f, &d) >= 0) {
                    sWardLogs++;
                    fprintf(stderr, "ward: hand %d dist %.1f grip %.2f\n", side, d, grip);
                }
            }
#endif
        }
#if 0   /* (temporary: a buzz the first frame a hand is within reach of a leaf) */
        {   /* (temporary: a buzz the first frame a hand is within reach of a leaf) */
            float d = 0.0f;

            if (ward_near(&e->room.placed, at, WARD_TOUCH, &d) >= 0) {
                if (!sWardNear[side]) {
                    sWardNear[side] = 1;
                    vr_haptic(side, 0.3f, 0.05f);
                }
            } else {
                sWardNear[side] = 0;   /* (out of range: in again buzzes again) */
            }
        }
#endif
        if (grip <= 0.4f) {
            sLadderHold[side] = -1;   /* (let go: of the ladder too) */
            sLadderFresh[side] = 0;
            if (sHold[side] == HOLD_LOOT) {
                loot_let_go(sHoldId[side], side);
                sHold[side] = HOLD_NONE;
            } else if (sHold[side] != HOLD_NONE) {
                release_hand(&e->room, side);
            }
        } else if (sHold[side] == HOLD_NONE && rising) {
            /* a hiding spot's hand first: its GRAB box (grown), or one of its parts' edge wherever
             * that is now - so a spot's door is neither carried as a prop nor taken as an exit door */
            {
                int sp = -1, spPart = -1;
                float dBox = 0.0f, dEdge = 0.0f;
                const char *how = NULL;
                int box = vrspot_grab_box(at, VRSPOT_GRAB_MARGIN, &dBox);
                int edge = vrspot_grab_edge(at, VRSPOT_EDGE_R, &dEdge, &spPart);

                if (edge >= 0) {   /* (the edge first: it follows the door, wherever its handle is) */
                    sp = edge;
                    how = "edge";
                } else if (box >= 0) {
                    sp = box;
                    how = "box";
                }
                if (sp >= 0 && vrspot_take(sp, at, &spPart, &sSpotOffset[side])) {
                    if (sHold[side ^ 1] == HOLD_SPOT && sHoldId[side ^ 1] == sp) {
                        sHold[side ^ 1] = HOLD_NONE;   /* (a second hand on the same spot takes over) */
                    }
                    sHold[side] = HOLD_SPOT;
                    sHoldId[side] = sp;
                    sSpotPart[side] = spPart;
                    sSpotC[side] = vrspot_closure_of(sp);
                    sSpotSpeed[side] = 0.0f;
                    spot_take_log(sp, how, at);   /* (DEBUG: how the spot was taken) */
                    vrspot_log_edge(sp);          /* (DEBUG: every part's handle, in world space) */
                }
                if (sHold[side] == HOLD_NONE && vrspot_debug_on()) {
                    float gd = -1.0f, ed = -1.0f;
                    int epart = -1;
                    int nearBox = vrspot_grab_box(at, 6.0f, &gd);
                    int nearEdge = vrspot_grab_edge(at, 6.0f, &ed, &epart);
                    int near = nearBox >= 0 ? nearBox : nearEdge;

                    if (near >= 0) {   /* (a press that came near a spot and missed: the box tuning) */
                        spot_miss_log(near, at, nearBox >= 0 ? gd : -1.0f, nearEdge >= 0 ? ed : -1.0f);
                    }
                }
            }
            /* then a lever's: its box grown, or its handle wherever it is at the current pose */
            if (sHold[side] == HOLD_NONE) {
                int lv = -1;
                float dLBox = 0.0f, dLEdge = 0.0f;
                int lbox = vrlever_grab_box(at, VRLEVER_GRAB_MARGIN, &dLBox);
                int ledge = vrlever_grab_edge(at, VRLEVER_EDGE_R, &dLEdge);

                if (lbox >= 0 && (ledge < 0 || dLBox <= dLEdge)) {
                    lv = lbox;
                } else if (ledge >= 0) {
                    lv = ledge;
                }
                if (lv >= 0 && vrlever_take(lv, at, &sLeverOffset[side])) {
                    sHold[side] = HOLD_LEVER;
                    sHoldId[side] = lv;
                }
                if (sHold[side] == HOLD_NONE && vrlever_debug_on()) {
                    float bd = -1.0f, hd = -1.0f;
                    int nearLBox = vrlever_grab_box(at, 6.0f, &bd);
                    int nearLEdge = vrlever_grab_edge(at, 6.0f, &hd);
                    int near = nearLBox >= 0 ? nearLBox : nearLEdge;

                    if (near >= 0) {
                        lever_miss_log(near, at, nearLBox >= 0 ? bd : -1.0f, nearLEdge >= 0 ? hd : -1.0f);
                    }
                }
            }
            /* then a curtained mirror's curtain: its handle's edge, wherever it is at the current c.
             * (There is no GRAB box: the curtain itself is the handle.) */
            if (sHold[side] == HOLD_NONE) {
                float dMirror = 0.0f;
                int mr = vrmirror_grab_edge(at, VRMIRROR_EDGE_R, &dMirror);

                if (mr >= 0 && vrmirror_take(mr, at, &sMirrorOffset[side])) {
                    sHold[side] = HOLD_MIRROR;
                    sHoldId[side] = mr;
                    vrmirror_log_take(mr, at);   /* (DEBUG) */
                }
            }
            door = -1;
            doorD = DOOR_REACH_MAX * DOOR_REACH_MAX;   /* (the widest reach a knob may be taken at) */
            for (i = 0; i < ROOM_DOORS; i++) {
                RoomDoor *d = &e->room.doors[i];
                int ax = 0;
                float kn = 0.0f;

                if (!d->present || d->mode == 1 || door_blocked(e, i) || !touching_knob(e, i, d, at, &ax)) {
                    continue;
                }
                kn = dist2(at, handle_at(d, ax));
                if (door < 0 || kn < doorD) {
                    door = i;
                    axis = ax;
                    doorD = kn;
                }
            }
            ward = nearest_ward(&e->room.placed, at, side ^ 1, &wardD);
            prop = nearest_prop(&e->room.placed, at, side ^ 1, &propD);
            {
                int skip = sHold[side ^ 1] == HOLD_LOOT ? sHoldId[side ^ 1] : -1;
                float lootD = 0;
                int loot = loot_nearest(at, skip, &lootD);

                if (sHold[side] == HOLD_NONE && loot >= 0 && (door < 0 || lootD < doorD) && (prop < 0 || lootD <= propD) &&
                    (ward < 0 || lootD <= wardD)) {
                    door = -1;
                    prop = -1;
                    if (loot_grab(loot, side)) {   /* (0: no such prop) */
                        sHold[side] = HOLD_LOOT;
                        sHoldId[side] = loot;
                        loot_follow(loot, at, right, up, fwd);
                    }
                }
            }
            if (sHold[side] == HOLD_NONE && ward >= 0 && (door < 0 || wardD <= doorD) &&
                (prop < 0 || wardD <= propD) && ward_take(&e->room.placed, side, ward, at)) {
                sHold[side] = HOLD_WARD;   /* (the hand has the leaf: its frame follows the hand) */
                sHoldId[side] = ward;
                door = -1;
                prop = -1;
            }
            if (sHold[side] == HOLD_NONE && door >= 0 && (prop < 0 || doorD <= propD)) {
                sHold[side] = HOLD_DOOR;
                sHoldId[side] = door;
                sDoorAxis[side] = axis;
                sHoldDoor[side] = door_index(e, door);
                sHoldFresh[side] = 0;
                drive_door(e, door, axis, at);
            } else if (sHold[side] == HOLD_NONE && prop >= 0) {
                sHold[side] = HOLD_PROP;
                sHoldId[side] = prop;
                sGrabYaw[side] = atan2f(fwd.x, fwd.z);
                sGrabRot[side] = e->room.placed.p[prop].rot;
            }
        } else if (sHold[side] == HOLD_DOOR) {
            if (door_blocked(e, sHoldId[side])) {
                release_hand(&e->room, side);
            } else if (sHoldFresh[side]) {
                sHoldFresh[side] = 0;   /* (carried across a room: the leaf is driven from here on) */
            } else {
                drive_door(e, sHoldId[side], sDoorAxis[side], at);
            }
        } else if (sHold[side] == HOLD_LOOT) {
            loot_follow(sHoldId[side], at, right, up, fwd);
        } else if (sHold[side] == HOLD_PROP) {
            Placed *p = sHoldId[side] >= 0 && sHoldId[side] < e->room.placed.n ? &e->room.placed.p[sHoldId[side]] : NULL;

            if (p == NULL || !p->shown) {
                sHold[side] = HOLD_NONE;
                sLadderHold[side] = -1;
                sLadderFresh[side] = 0;
            } else {
                p->pos = at;
                p->rot = sGrabRot[side];
                p->rot.y = sGrabRot[side].y + (atan2f(fwd.x, fwd.z) - sGrabYaw[side]);
            }
        } else if (sHold[side] == HOLD_WARD) {
            Placed *p = sHoldId[side] >= 0 && sHoldId[side] < e->room.placed.n ? &e->room.placed.p[sHoldId[side]] : NULL;

            if (p == NULL || !p->shown || !ward_leaf(p)) {
                sHold[side] = HOLD_NONE;   /* (gone: a room away from it, or put away) */
                sLadderHold[side] = -1;
                sLadderFresh[side] = 0;
            } else {
                /* (the hiding may have played it since, or left its track to run: the leaf is
                 * taken again where it now stands, and then turned by the hand as before) */
                if (ward_retake(&e->room.placed, side, sHoldId[side], p, at)) {
                    ward_drive(&e->room.placed, side, sHoldId[side], p, at);
                }
            }
        } else if (sHold[side] == HOLD_SPOT) {
            int id = sHoldId[side];

            if (!vrspot_scrubbable(id)) {
                sSpotWhy = vrspot_script_owned(id) ? vrspot_script_reason(id) : "other";
                release_hand(&e->room, side);   /* (a script owns it now, or the spot is gone) */
            } else {
                Vec3 target = vec3_sub(at, sSpotOffset[side]);
                float c = vrspot_scrub(id, sSpotPart[side], target, vrspot_closure_of(id), VRSPOT_WINDOW);

                sSpotSpeed[side] = c - sSpotC[side];
                vrspot_set_closure(id, c);   /* (every part of the spot, from the one closure) */
                if ((c <= 0.0f || c >= 1.0f) && sSpotC[side] != c) {
                    vr_haptic(side, 0.2f, 0.05f);   /* (the stop it reached) */
                }
                sSpotC[side] = c;
            }
        } else if (sHold[side] == HOLD_LEVER) {
            int id = sHoldId[side];

            if (!vrlever_scrubbable(id)) {
                release_hand(&e->room, side);   /* (done, or a script has taken it) */
            } else {
                Vec3 target = vec3_sub(at, sLeverOffset[side]);
                float c = vrlever_scrub(id, target, vrlever_closure(id), VRLEVER_WINDOW);

                vrlever_set(id, c);
                if (c >= VRLEVER_DONE) {   /* (home: it is pulled, the event queued) */
                    vrlever_complete(id);
                    vr_haptic(side, 0.7f, 0.12f);
                    release_hand(&e->room, side);
                }
            }
        } else if (sHold[side] == HOLD_MIRROR) {
            int id = sHoldId[side];

            if (!vrmirror_scrubbable(id)) {
                sMirrorWhy = vrmirror_script_owned(id) ? "script" : "other";
                release_hand(&e->room, side);   /* (a script plays it now, or the room put it away) */
            } else if (vrmirror_ok(id, 0) != 0) {
                sMirrorWhy = "blocked";   /* (no time, broken or a smash: the room's own words say) */
                release_hand(&e->room, side);
            } else {
                Vec3 target = vec3_sub(at, sMirrorOffset[side]);
                float c = vrmirror_scrub(id, target, vrmirror_closure(id), VRMIRROR_WINDOW);

                if (vrmirror_set(id, c) != 0) {   /* (a branch ran: one buzz a crossing) */
                    vr_haptic(side, 0.5f, 0.09f);
                    (void)vrmirror_fire_take(id);
                }
            }
        }
        /* a ladder, when nothing else took the hand: a free hand held at LADDER_GRAB_GRIP takes it, and
         * that controller buzzes once. It sets no hold and moves nothing by itself - her climbing is
         * the scripts' (docs/subsystems/hands.md). The grabs above have already run, so a door, a prop,
         * a leaf or loot taken in this same frame wins. */
        if (sHold[side] == HOLD_NONE && sLadderHold[side] < 0 && grip > LADDER_GRAB_GRIP) {
            int link = ladder_grab_link(at);

            if (link >= 0) {
                sLadderHold[side] = link;
                sLadderFresh[side] = 1;   /* (hers to take: ladder-grab) */
                sLadderGrabAt[side] = at;
                vr_haptic(side, 0.15f, 0.03f);
                engine_sound_at(LADDER_RUNG_SOUND, LADDER_RUNG_BANK, at);
            }
        }
        if (sHold[side] == HOLD_NONE && grip <= 0.4f) {
            for (i = 0; i < ROOM_DOORS; i++) {
                RoomDoor *d = &e->room.doors[i];
                float step;

                if (!d->present || d->mode == 1 || door_blocked(e, i)) {
                    continue;
                }
                if (d->swing > -DOOR_LATCH && d->swing < DOOR_LATCH) {
                    continue;
                }
                if (sHold[side ^ 1] == HOLD_DOOR && sHoldId[side ^ 1] == i) {
                    continue;
                }
                if (sGrace[side] > 0 && sGraceDoor[side] == i) {
                    continue;
                }
                if (!push_step(d, sHandAt[side], at, sHandHave[side], &step, &sVel[i])) {
                    continue;
                }
                sPushNow[i] = 1;
                sCoast[i] = 0;
                door_hand(door_index(e, i));
                room_door_swing(&e->room, i, d->swing + step, 1);
                if (d->swing > -DOOR_LATCH && d->swing < DOOR_LATCH) {
                    sSettle[i] = 0;
                    ease_door(e, i, DOOR_SHUT);
                } else if (d->swing <= -DOOR_WIDE || d->swing >= DOOR_WIDE) {
                    sSettle[i] = 0;
                    ease_door(e, i, d->swing);
                } else {
                    sSettle[i] = 18;
                }
            }
        }
        /* A hand on a ladder, pulled. Its PLAY-SPACE place is what counts - an offset from her
         * eyes, turned by her yaw - so her own move cannot feed back into it: a hand held still
         * cannot move her, and a hand moved the same as her moves her not at all.
         * `ladder_pull_step` gives the body's move in the game's y (down the hand is up her) and
         * moves the anchor on with the hand, so no motion is counted twice. */
        if (sLadderHold[side] >= 0) {
            Vec3 play = play_from_world(eye, yaw, at);

            if (!sLadderAnchorHave[side]) {
                sLadderAnchor[side] = play;   /* (the grab: the anchor from here, nothing pulled yet) */
                sLadderAnchorHave[side] = 1;
            } else {
                sLadderPull += ladder_pull_step(&sLadderAnchor[side], play);
            }
        } else if (sLadderAnchorHave[side]) {
            sLadderAnchorHave[side] = 0;   /* (let go: the anchor dropped, and what it had not taken) */
            sLadderPull = 0.0f;
        }
        if (WALL_BUZZ) {   /* (the buzz is off: no path is swept for it at all) */
            /* the path the hand took this frame through a wall, a door leaf or a placed thing: one
             * buzz as it enters. What that hand holds is not in its own way. The room's walls are
             * asked at the fingertip too, so a hand reaching fingers first is warned. */
            int skipDoor = sHold[side] == HOLD_DOOR ? sHoldId[side] : -1;
            int skipProp = sHold[side] == HOLD_PROP || sHold[side] == HOLD_WARD ? sHoldId[side] : -1;
            int wall = sHandHave[side] &&
                       (hand_path_hits_wall(&e->room, sHandAt[side], at, 0.40f) ||
                        hand_path_hits_wall(&e->room, vec3_add(sHandAt[side], off), vec3_add(at, off), 0.40f) ||
                        hand_path_hits_door(e, skipDoor, sHandAt[side], at, 0.40f) ||
                        hand_path_hits_placed(&e->room.placed, skipProp, sHandAt[side], at, 0.40f));

            if (wall && !sWallNear[side]) {
                if (WALL_BUZZ) {
                    vr_haptic(side, 0.15f, 0.03f);
                }
                sWallNear[side] = 1;
            }
            if (!wall) {
                sWallNear[side] = 0;   /* (out of the wall: in again buzzes again) */
            }
        }
        swing_hand(side, at);
        sHandAt[side] = at;
        sHandHave[side] = 1;
        sGripWas[side] = grip;
        if (sGrace[side] > 0) {
            sGrace[side]--;
        }
    }
    swing_finish();
    for (i = 0; i < ROOM_DOORS; i++) {
        int held = (sHold[0] == HOLD_DOOR && sHoldId[0] == i) || (sHold[1] == HOLD_DOOR && sHoldId[1] == i);
        float sw;

        /* the hand has left the leaf: a fast shove keeps its speed, as a gripped throw does */
        if (sPushNow[i]) {
            sPushWas[i] = 1;
            sPushNow[i] = 0;
        } else if (sPushWas[i] && !held) {
            sPushWas[i] = 0;
            sw = e->room.doors[i].swing;
            if (sw > -DOOR_LATCH && sw < DOOR_LATCH) {
                /* the shove already carried it onto the latch */
            } else if (sw <= -DOOR_WIDE || sw >= DOOR_WIDE) {
                /* against the open stop */
            } else if (sVel[i] <= -COAST_SPEED || sVel[i] >= COAST_SPEED) {
                int h;

                sCoast[i] = 1;
                sSettle[i] = 0;
                for (h = 0; h < 2; h++) {
                    if (sGrace[h] == 0 || sGraceDoor[h] == i) {
                        sGrace[h] = PUSH_GRACE;
                        sGraceDoor[h] = i;
                    }
                }
            }
        } else {
            sPushWas[i] = 0;
        }
        if (held) {
            sSettle[i] = 0;
            sCoast[i] = 0;
        } else if (sCoast[i]) {
            coast_door(e, i);
        } else if (sSettle[i] > 0 && --sSettle[i] == 0 && e->room.doors[i].present && !door_blocked(e, i)) {
            ease_door(e, i, door_stop(e->room.doors[i].swing));
        }
    }
    ward_tick(e);
#if 0   /* (temporary: where she is against the leaf's bounds, and the leaf's frame) */
    hide_probe(e);   /* (temporary: where she is against the leaf's bounds, and the leaf's frame) */
#endif
    poses_save(e);
    leaf_scan(e);
    loot_maintain();
    defense_tick();
    hand_item_tick();   /* (the key item the menu equipped, if slot 2's magnesia has not the hand) */
}
