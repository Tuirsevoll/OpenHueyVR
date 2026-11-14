\ vr-options.fs - the VR comfort and accessibility options (docs/subsystems/comfort.md): how she is
\ turned, the comfort vignette while she moves, teleporting, seated play, the hands' controls, the
\ start-up height calibration and an in-game scene watched from her eyes. C keeps them (vr.h
\ VrOptions), reached through the vr. fields; each one is as the game was until the player chooses
\ otherwise. The player's are vr-options.fs in the player's directory, written when the VR options
\ menu asks (until it is built: `vr-options:save-vr-options` in the console) and read back at
\ start-up, as graphics.fs and controls.fs are.
IN: vr-options
USING: engine ;

<PRIVATE
create (options-path) 256 allot
variable (options-n)
: +path ( addr len -- )  {: a n :}  a (options-path) (options-n) @ +  n move  n (options-n) +! ;
: settings-file ( -- addr len )
    0 (options-n) !  user-dir +path  s" vr-options.fs" +path  (options-path) (options-n) @ ;
PRIVATE>

\ as the game was before there was a choice: smooth turning, every comfort option off
: vr-defaults ( -- )
    0 vr-settings vr.turn-snap l!
    45e vr-settings vr.snap-degrees sf!
    0 vr-settings vr.comfort l!
    0.5e vr-settings vr.comfort-strength sf!   \ (the page's Med: off until it is chosen)
    0 vr-settings vr.teleport l!
    0 vr-settings vr.teleport-mode l!
    2.5e vr-settings vr.teleport-reach sf!
    0 vr-settings vr.seated l!
    1.6e vr-settings vr.seated-height sf!
    0e vr-settings vr.seated-offset sf!
    0 vr-settings vr.left-handed l!                \ (the microphone and its transcript: vr.h VrOptions)
    0 vr-settings vr.whisper-open l!               \ (B holds it, as the game had it)
    0 vr-settings vr.whisper-debug l!              \ (the subtitles off: the page steps Off, On, Debug)
    1 vr-settings vr.calibrate-start l!
    0 vr-settings vr.cutscene-first-person l!
    0.13e vr-settings vr.crouch-deadzone sf!   \ (a drop below 13% of standing height: no crouch)
    0.25e vr-settings vr.crouch-on sf!         \ (the hide flag on at 25% ...)
    0.18e vr-settings vr.crouch-off sf!        \ (... and off only above 18%)
    0.45e vr-settings vr.sneak-speed sf! ;     \ (the crouch walk: 45% of a walk - 55% slower)

\ the options written out as Forth, read back at start-up (each line runs)
: save-vr-options ( -- )
    testing? if  exit  then
    settings-file to-file
    ." \ the VR options, saved from the VR options menu (or the console)" cr
    ." USING: engine ;" cr
    vr-settings vr.turn-snap l@ n>s type             ."  vr-settings vr.turn-snap l!" cr
    vr-settings vr.snap-degrees sf@ 6 f>s$ type      ."  vr-settings vr.snap-degrees sf!" cr
    vr-settings vr.comfort l@ n>s type               ."  vr-settings vr.comfort l!" cr
    vr-settings vr.comfort-strength sf@ 6 f>s$ type  ."  vr-settings vr.comfort-strength sf!" cr
    vr-settings vr.teleport l@ n>s type              ."  vr-settings vr.teleport l!" cr
    vr-settings vr.teleport-mode l@ n>s type          ."  vr-settings vr.teleport-mode l!" cr
    vr-settings vr.teleport-reach sf@ 6 f>s$ type     ."  vr-settings vr.teleport-reach sf!" cr
    vr-settings vr.seated l@ n>s type                ."  vr-settings vr.seated l!" cr
    vr-settings vr.seated-height sf@ 6 f>s$ type     ."  vr-settings vr.seated-height sf!" cr
    vr-settings vr.seated-offset sf@ 6 f>s$ type     ."  vr-settings vr.seated-offset sf!" cr
    vr-settings vr.left-handed l@ n>s type           ."  vr-settings vr.left-handed l!" cr
    vr-settings vr.whisper-open l@ n>s type          ."  vr-settings vr.whisper-open l!" cr
    vr-settings vr.whisper-debug l@ n>s type         ."  vr-settings vr.whisper-debug l!" cr

    vr-settings vr.calibrate-start l@ n>s type       ."  vr-settings vr.calibrate-start l!" cr
    vr-settings vr.eye-height sf@ 6 f>s$ type        ."  vr-settings vr.eye-height sf!" cr
    vr-settings vr.body-scale sf@ 6 f>s$ type        ."  vr-settings vr.body-scale sf!" cr
    vr-settings vr.calibrated l@ n>s type            ."  vr-settings vr.calibrated l!" cr
    vr-settings vr.crouch-deadzone sf@ 6 f>s$ type   ."  vr-settings vr.crouch-deadzone sf!" cr
    vr-settings vr.crouch-on sf@ 6 f>s$ type         ."  vr-settings vr.crouch-on sf!" cr
    vr-settings vr.crouch-off sf@ 6 f>s$ type        ."  vr-settings vr.crouch-off sf!" cr
    vr-settings vr.sneak-speed sf@ 6 f>s$ type       ."  vr-settings vr.sneak-speed sf!" cr
    end-file  ." saved " settings-file type cr ;

: load-vr-options ( -- )
    settings-file file-exists? if
        settings-file included
        \ 0.35 was the crouch walk before it was asked to be 55% slower: lift an old default
        vr-settings vr.sneak-speed sf@ 0.3499e f> vr-settings vr.sneak-speed sf@ 0.3501e f< and if
            0.45e vr-settings vr.sneak-speed sf!
        then
    then ;

\ measure and keep the player's height at once, without the title's screen or the options page: a
\ save's load does this (saves.fs), so the world is sized to them as their game comes back. Seated
\ play sizes the world from its own height, and without a tracked headset there is nothing to
\ measure - both are left as they were.
: vr-calibrate-now ( -- calibrated? )
    vr?  vr-settings vr.seated l@ 0= and  vr-tracking? and if
        vr-calibrate
        vr-settings vr.calibrated l@ 0<> if  save-vr-options  -1  else  0  then
    else  0  then ;

\ a fresh start: the defaults, then the player's
vr-defaults
' load-vr-options catch drop
