\ story/crouch.fs - crouching by the headset: its height is the crouch. The original L1 crouch is
\ left out in VR (story/common.fs may-crouch? and the Chaos Forest's bush-frame ask vr?); here the
\ headset's drop from the calibrated standing height is the measure.
\
\ The drop is a share of that height, so it is the same for every player: below
\ vr.crouch-deadzone nothing happens; from there to vr.crouch-on the crouch animation scrubs 0..1,
\ so she is in her full crouch pose when the hide flag turns on; rising, the flag stays on until
\ vr.crouch-off (the hysteresis). While it is on she may walk at vr.sneak-speed and the animation
\ is the slow walk (no crouch-walk exists), or the crouch pose when still. The original's "moving
\ cancels the crouch" is the L1 doing's own (crouch-held?/shook-now); this path does not use it.
\
\ The state itself is in fiona/sneak.fs: her movement and model read it from there.
IN: story.crouch
USING: engine actors messages vectors facts flag-names story.kit fiona.sneak keys ;

fvariable vr-crouch-frac     \ the headset's drop, a share of her standing eyes (0..1)
fvariable vr-key-frac        \ the crouch trigger's ramp, a share of that same height
fvariable vr-crouch-test     \ testing: a drop set by hand (a test has no headset)
variable vr-sneak-hide       \ the hide flag's state (the hysteresis)
variable vr-sneak-releasing  \ just rose out of hiding: the animation scrubs off -> deadzone
variable vr-sneak-had        \ we set fiona-half-hidden: it is ours to clear

: crouch-deadzone ( F: -- share )  vr-settings vr.crouch-deadzone sf@ ;
: crouch-on ( F: -- share )  vr-settings vr.crouch-on sf@ ;
: crouch-off ( F: -- share )  vr-settings vr.crouch-off sf@ ;

\ the crouch trigger (the left trigger, the right in left-handed play: C gives it as the pad's
\ L1). It ramps into the same shares the headset's drop uses, so the hide flag, the scrubbed
\ animation and the slow sneak walk are exactly the rules below.
: crouch-key-held? ( -- flag )
    vr?  l1 button-down? and  vr-menu? 0= and  world-held state? 0= and ;
: crouch-key-step ( -- )
    crouch-key-held? if  vr-key-frac f@ 0.05e f+  else  vr-key-frac f@ 0.05e f-  then
    0e fmax  crouch-on fmin  vr-key-frac f! ;

: read-crouch ( -- )
    vr? 0= if  0e vr-crouch-frac f!  0e vr-key-frac f!  0e vr-crouch-key!  exit  then
    crouch-key-step
    vr-key-frac f@ vr-crouch-key!   \ (the view's own drop, less the headset's: vr.c)
    testing? if  vr-crouch-test f@  else  vr-crouch  then
    vr-key-frac f@ fmax  vr-crouch-frac f! ;

\ the hide flag: on at the on share, off only at the off share
: update-hide ( -- )
    vr-sneak-hide @ if
        vr-crouch-frac f@ crouch-off f<= if
            0 vr-sneak-hide !  -1 vr-sneak-releasing !
        then
    else
        vr-crouch-frac f@ crouch-on f>= if
            -1 vr-sneak-hide !  0 vr-sneak-releasing !
        then
    then
    vr-crouch-frac f@ crouch-deadzone f<= if  0 vr-sneak-releasing !  then ;

\ the animation's place: deadzone -> on is 0 -> 1; hiding is 1; rising out, off -> deadzone is
\ 1 -> 0 (so the body does not jump when the flag lets go)
: crouch-k ( -- ) ( F: -- k )
    vr-sneak-hide @ if  1e exit  then
    vr-crouch-frac f@ crouch-deadzone f-
    vr-sneak-releasing @ if  crouch-off  else  crouch-on  then  crouch-deadzone f-
    fdup 0.0001e f< if  fdrop fdrop 0e exit  then
    f/ 0e fmax 1e fmin ;

\ the L1 crouch's own gate, and only while the headset is down far enough to mean it
: may-vr-crouch? ( -- flag )
    vr? 0= if  false exit  then
    hewie-played? 0=  no-flee state? 0= and
    request-now @ spirit-request <> and
    fiona-doing-now @ backstep-doing <> and
    fiona-idle? and  fiona-p holder-of @ 0< and
    fading @ 0= and  world-held state? 0= and  world-frozen state? 0= and  vr-menu? 0= and ;

\ a scripted move's mode or a hold left with no doing in it (a scene's teleport, a cutscene, a
\ stopped door): put right when a crouch is asked for, so it is not lost for good
: stale-led? ( -- flag )
    fiona-mode-now @ led-mode =  fiona-p holder-of @ 0< and
    fiona-act? 0= and  in-panic? 0= and ;
: stale-held? ( -- flag )
    fiona-p holder-of @ 0>=  fiona-act? 0= and ;

: sneak-off ( -- )
    0 vr-sneak-hide !  0 vr-sneak-releasing !
    0e fiona.sneak:sneak-k f!  0 fiona.sneak:sneak-hide !
    vr-sneak-had @ if  fiona-half-hidden state-off  0 vr-sneak-had !  then
    0 vr-crouch-on! ;

: vr-crouching ( -- )
    read-crouch
    vr-crouch-frac f@ crouch-deadzone f> if
        stale-led? if  fiona-p 0 tell-held  then
        stale-held? if  fiona let-go  then
    then
    update-hide
    may-vr-crouch? 0= if  sneak-off exit  then
    crouch-k fiona.sneak:sneak-k f!
    vr-settings vr.sneak-speed sf@ fiona.sneak:sneak-speed f!
    vr-sneak-hide @ fiona.sneak:sneak-hide !
    vr-sneak-hide @ if
        fiona-half-hidden state!  -1 vr-sneak-had !
    else
        fiona-half-hidden state-off  0 vr-sneak-had !
    then
    fiona.sneak:sneak? if  -1  else  0  then  vr-crouch-on! ;
