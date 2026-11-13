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
    0 vr-settings vr.left-handed l!
    1 vr-settings vr.calibrate-start l!
    0 vr-settings vr.cutscene-first-person l! ;

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
    vr-settings vr.calibrate-start l@ n>s type       ."  vr-settings vr.calibrate-start l!" cr
    vr-settings vr.cutscene-first-person l@ n>s type ."  vr-settings vr.cutscene-first-person l!" cr
    vr-settings vr.eye-height sf@ 6 f>s$ type        ."  vr-settings vr.eye-height sf!" cr
    vr-settings vr.body-scale sf@ 6 f>s$ type        ."  vr-settings vr.body-scale sf!" cr
    vr-settings vr.calibrated l@ n>s type            ."  vr-settings vr.calibrated l!" cr
    end-file  ." saved " settings-file type cr ;

: load-vr-options ( -- )  settings-file file-exists? if  settings-file included  then ;

\ a fresh start: the defaults, then the player's
vr-defaults
' load-vr-options catch drop
