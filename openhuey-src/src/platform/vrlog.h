/* The VR side's own console lines - the headset's, the body's and the hands', the grips', the
 * physical items', the hiding spots' and the levers', the probes' under them - are for whoever
 * debugs the VR build, not for a player: a release has no console to show them in, and they are a
 * flood in one that has. Quiet by default; `OPENHUEY_VR_LOG=1` (or any value not starting '0')
 * brings them back while the VR side is being worked on.
 *
 * Only the readouts go through here. Nothing is made conditional and nothing is dropped from the
 * build: VRLOG(...) is fprintf(stderr, ...) behind the gate, VRLOGP(...) the same on stdout, and
 * the work each line reports on runs exactly as before. The lines that say something failed -
 * no headset, no runtime, openxr_loader.dll missing, no microphone, a model that will not load -
 * are not debug lines and do not go through this: they print as they always did. */
#ifndef HGA_VRLOG_H
#define HGA_VRLOG_H

#include <stdio.h>
#include <stdlib.h>

#define VR_LOG_ON() (getenv("OPENHUEY_VR_LOG") != NULL && getenv("OPENHUEY_VR_LOG")[0] != '0')
#define VRLOG(...)         \
    do {                   \
        if (VR_LOG_ON()) { \
            fprintf(stderr, __VA_ARGS__); \
        }                  \
    } while (0)
#define VRLOGP(...)        \
    do {                   \
        if (VR_LOG_ON()) { \
            printf(__VA_ARGS__); \
        }                  \
    } while (0)

#endif
