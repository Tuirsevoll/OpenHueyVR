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
    0 vr-settings vr.room-loading l!            \ (room loading: Original - as the game was)
    0 vr-settings vr.seated l!
    1.6e vr-settings vr.seated-height sf!
    0 vr-settings vr.left-handed l!                \ (the microphone and its transcript: vr.h VrOptions)
    0 vr-settings vr.whisper-open l!               \ (B holds it, as the game had it)
    0 vr-settings vr.whisper-debug l!              \ (the subtitles off: the page steps Off, On, Debug)
    1 vr-settings vr.calibrate-start l!
    0 vr-settings vr.cutscene-first-person l!
    0.13e vr-settings vr.crouch-deadzone sf!   \ (a drop below 13% of standing height: no crouch)
    0.25e vr-settings vr.crouch-on sf!         \ (the hide flag on at 25% ...)
    0.18e vr-settings vr.crouch-off sf!        \ (... and off only above 18%)
    0.45e vr-settings vr.sneak-speed sf!       \ (the crouch walk: 45% of a walk - 55% slower)
    \ the panic's effects (panic.fs, fiona/fear.fs): the page's Reduced preset - the grey tint
    \ alone, the stumbles and the full beat kept. The world's freeze and her scream at full panic
    \ are not settings: they are always the game's.
    1 vr-settings vr.panic-tint l!
    0 vr-settings vr.panic-smear l!
    0 vr-settings vr.panic-zoom l!
    0 vr-settings vr.panic-negative l!
    0 vr-settings vr.panic-shake l!
    1 vr-settings vr.panic-stumbles l!
    2 vr-settings vr.panic-haptics l!
    1 vr-settings vr.audio3d l!              \ (placed sounds through the headset's HRTF, occluded: on)
    1 vr-settings vr.hide-door l!            \ (a hiding spot's door see-through while fully hidden: on)
    0 vr-settings vr.item-prop-debug l!      \ (small RGB axes at a held item's model and wrist: off)
    1 vr-settings vr.body-fp-head l!       \ (the VR body's head goes from her own first-person view)
    0e vr-settings vr.arm-reach sf!        \ (the player's reach, metres: 0 until a calibration measures it)
    0.06e vr-settings vr.cam-back sf!      \ (the camera 6 cm behind her eyes, in her head: vrbody.c
    0.01e vr-settings vr.cam-up sf! ;      \  EYE_BACK_M/EYE_UP_M; a touch higher)

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
    vr-settings vr.room-loading l@ n>s type          ."  vr-settings vr.room-loading l!" cr

    vr-settings vr.seated l@ n>s type                ."  vr-settings vr.seated l!" cr
    vr-settings vr.seated-height sf@ 6 f>s$ type     ."  vr-settings vr.seated-height sf!" cr
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
    vr-settings vr.audio3d l@ n>s type               ."  vr-settings vr.audio3d l!" cr
    vr-settings vr.hide-door l@ n>s type             ."  vr-settings vr.hide-door l!" cr
    vr-settings vr.item-prop-debug l@ n>s type       ."  vr-settings vr.item-prop-debug l!" cr
    vr-settings vr.body-fp-head l@ n>s type          ."  vr-settings vr.body-fp-head l!" cr
    vr-settings vr.arm-reach sf@ 6 f>s$ type          ."  vr-settings vr.arm-reach sf!" cr
    vr-settings vr.cam-back sf@ 6 f>s$ type          ."  vr-settings vr.cam-back sf!" cr
    vr-settings vr.cam-up sf@ 6 f>s$ type          ."  vr-settings vr.cam-up sf!" cr

    vr-settings vr.panic-tint l@ n>s type            ."  vr-settings vr.panic-tint l!" cr
    vr-settings vr.panic-smear l@ n>s type           ."  vr-settings vr.panic-smear l!" cr
    vr-settings vr.panic-zoom l@ n>s type            ."  vr-settings vr.panic-zoom l!" cr
    vr-settings vr.panic-negative l@ n>s type        ."  vr-settings vr.panic-negative l!" cr
    vr-settings vr.panic-shake l@ n>s type           ."  vr-settings vr.panic-shake l!" cr
    vr-settings vr.panic-stumbles l@ n>s type        ."  vr-settings vr.panic-stumbles l!" cr
    vr-settings vr.panic-haptics l@ n>s type         ."  vr-settings vr.panic-haptics l!" cr
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

\ ---- the panic's effects (docs/subsystems/panic.md): each one an option, and the preset their set
\ matches. Full: every screen effect, the stumbles and the full beat. Reduced: the grey tint alone,
\ the stumbles and the full beat. Minimal: no screen effect, no stumbles, half the beat. A set that
\ is none of those is Custom (-1) - shown, never stepped to. Picking a preset writes its settings
\ over the individual ones; the label is worked out from them, so there is nothing else to keep.
0 constant panic-full
1 constant panic-reduced
2 constant panic-minimal
-1 constant panic-custom
0 constant haptics-off
1 constant haptics-half
2 constant haptics-full

: panic-tint? ( -- flag )      vr-settings vr.panic-tint l@ 0<> ;
: panic-smear? ( -- flag )     vr-settings vr.panic-smear l@ 0<> ;
: panic-zoom? ( -- flag )      vr-settings vr.panic-zoom l@ 0<> ;
: panic-negative? ( -- flag )  vr-settings vr.panic-negative l@ 0<> ;
: panic-shake? ( -- flag )     vr-settings vr.panic-shake l@ 0<> ;
: panic-stumbles? ( -- flag )  vr-settings vr.panic-stumbles l@ 0<> ;
: panic-haptics ( -- level )   vr-settings vr.panic-haptics l@ 0 max 2 min ;
\ the beat's strength: none, half, full
: panic-haptics-scale ( F: -- k )
    panic-haptics case  haptics-off of  0e  endof  haptics-half of  0.5e  endof  >r 1e r>  endcase ;

\ the seven settings as bits - the tint, the smear, the zoom, the negative, the shake, the
\ stumbles, then the haptics in bits 6 and 7 - so a preset is one comparison
: panic-bits ( -- bits )
    panic-tint? 1 and  panic-smear? 2 and or  panic-zoom? 4 and or  panic-negative? 8 and or
    panic-shake? 16 and or  panic-stumbles? 32 and or  panic-haptics 6 lshift or ;
1 2 or 4 or 8 or 16 or 32 or haptics-full 6 lshift or constant panic-full-bits
1 32 or haptics-full 6 lshift or constant panic-reduced-bits
haptics-half 6 lshift constant panic-minimal-bits

\ a preset's seven settings (the stack's top is the beat's)
: panic-set ( tint smear zoom negative shake stumbles haptics -- )
    vr-settings vr.panic-haptics l!   vr-settings vr.panic-stumbles l!
    vr-settings vr.panic-shake l!     vr-settings vr.panic-negative l!
    vr-settings vr.panic-zoom l!      vr-settings vr.panic-smear l!
    vr-settings vr.panic-tint l! ;
: panic-preset! ( preset -- )
    dup 0 3 within 0= if  drop exit  then
    case
        panic-full of     1 1 1 1 1 1 haptics-full panic-set  endof
        panic-reduced of  1 0 0 0 0 1 haptics-full panic-set  endof
        >r                0 0 0 0 0 0 haptics-half panic-set  r>
    endcase ;
: panic-preset ( -- preset )
    panic-bits
    dup panic-full-bits = if  drop panic-full exit  then
    dup panic-reduced-bits = if  drop panic-reduced exit  then
    panic-minimal-bits = if  panic-minimal  else  panic-custom  then ;
: panic-preset$ ( -- addr len )
    panic-preset case
        panic-full of     s" Full"  endof
        panic-reduced of  s" Reduced"  endof
        panic-minimal of  s" Minimal"  endof
        >r s" Custom" r>  endcase ;
\ the preset a step of the row goes to (a Custom set: the first, or the last, either way)
: panic-preset-step ( dir preset -- preset' )
    dup panic-custom = if  drop 0> if panic-full else panic-minimal then exit  then
    + 3 + 3 mod ;

\ a fresh start: the defaults, then the player's
vr-defaults
' load-vr-options catch drop
