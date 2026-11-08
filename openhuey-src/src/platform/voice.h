#ifndef VOICE_H
#define VOICE_H

/* In-process speech for Hewie. 0 is none. The rest are phrase kinds; distance and
 * pose gates stay in the scripts. */
enum {
    VOICE_NONE = 0,
    VOICE_COME = 1,
    VOICE_GO = 2,
    VOICE_STAY = 3,
    VOICE_SIT = 4,
    VOICE_PRAISE = 5,
    VOICE_SCOLD = 6,
    VOICE_SHAKE = 7,
    VOICE_ATTACK = 8
};

void voice_init(void);          /* no model, or not VR: stays quiet */
void voice_shutdown(void);
void voice_tick(int talk_held); /* right stick click held */
int voice_take(void);           /* the phrase kind, then cleared */
int voice_gen(void);            /* bumps when a new phrase is stored, match or not */
int voice_kind(void);           /* last phrase's command kind; not cleared by voice_take */
void voice_copy(char *dst, int cap);
void voice_set_debug(int on);
int voice_debug(void);
void voice_set_open_mic(int on);
int voice_open_mic(void);

#endif
