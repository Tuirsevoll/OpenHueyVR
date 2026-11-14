\ story/crouch.fs - crouching in the headset: the height of the headset is the crouch. The original
\ L1 crouch (story/common.fs, $81 / $82 / $87) is left out in VR: lowering the headset past
\ vr-crouch-engage starts this, and the animation is scrubbed by how far it is down - down and up
\ both - until her crouch's lowest point. `fiona-half-hidden` is set while she is down, as the L1
\ crouch sets it, so the stalker's sight and reach treat her as crouched. The eyes are lifted by
\ the same fall (vr-crouch-lift!): her head takes the view with it, rather than the view sinking
\ through her upright body.
IN: story.crouch
USING: engine actors messages vectors facts flag-names story.kit fiona.anims ;

0.12e fconstant vr-crouch-engage    \ (metres below her standing eyes: deeper crouches her)
0.10e fconstant vr-crouch-release   \ (... and up to this she stands again: below it the scrub lets go)
0.55e fconstant vr-crouch-full      \ (a full crouch's fall: the animation's lowest point)
0.04e fconstant vr-crouch-step      \ (how fast the scrub follows, a tick: about a second)

variable vr-crouch-drop      \ metres below her standing eyes (the headset's, or the tests')
variable vr-crouch-view      \ metres the eyes have lowered from the neutral pose (what the view adds)
variable vr-crouch-k         \ 0..1: where her crouch animation is (0 standing, 1 its lowest point)
variable vr-crouch-frames    \ that animation's last frame
variable vr-crouch-model     \ her model actor (the engine's: the motions are played on it)
variable vr-crouch-was       \ a crouch of ours ran: its flags are ours to put right if it was stopped

\ the headset's fall, both its measures. A headless run (tests) leaves the two where they are put.
: read-crouch ( -- )
    vr? 0= if  0e vr-crouch-drop f!  0e vr-crouch-view f!  exit  then
    testing? if  exit  then
    vr-crouch  vr-crouch-view f!  vr-crouch-drop f! ;

: crouch-target ( F: drop -- frac )
    fdup vr-crouch-release f<= if  fdrop 0e exit  then
    vr-crouch-full f/ 0e fmax 1e fmin ;
\ how far to the target this tick: slowly, a crouch-step at a time
: toward ( F: k target -- k' )
    {: F: k t :}
    t k f- fabs vr-crouch-step f> if
        t k f> if  k vr-crouch-step f+  else  k vr-crouch-step f-  then
    else  t  then ;

: crouch-scrub ( -- )
    vr-crouch-model @ crouch-down-anim
    vr-crouch-frames @ s>f vr-crouch-k f@ f*  motion-hold ;

: crouch-start ( -- )
    fiona model-of dup 0< if  drop -1 vr-crouch-model !  exit  then  vr-crouch-model !
    vr-crouch-model @ crouch-down-anim has-motion? 0= if  -1 vr-crouch-model !  exit  then
    fiona hold-posed
    0 pose-her!   \ (hers now: her own motions and a hand's pickup are played as usual)
    fiona -1 start-anim   \ (no animation, but her own idle step would play the idle over the crouch: this takes her act)
    fiona wait-move
    vr-crouch-model @ crouch-down-anim 0e motion-hold
    vr-crouch-model @ motion-frames 1- vr-crouch-frames !
    read-crouch
    vr-crouch-drop f@ crouch-target  vr-crouch-k f!   \ (where the head already is: no jump)
    crouch-scrub
    vr-crouch-view f@ vr-crouch-lift! ;

\ the way out: back up, her own again, a panic or a scene, or a doing that has taken her
: crouch-out? ( -- flag )
    vr-crouch-k f@ 0e f<=  vr-crouch-drop f@ vr-crouch-release f<=  and
    fiona-p mine? 0= or  in-panic? or
    struck-by-now @ 0< 0= or
    fading @ 0<> or  world-held state? or  world-frozen state? or
    scene-step @ scene-running = or  fiona-hidden state? or ;

: vr-crouches ( -- )
    crouch-start
    vr-crouch-model @ 0< if  exit  then
    begin
        yield
        read-crouch
        vr-crouch-k f@  vr-crouch-drop f@ crouch-target  toward  vr-crouch-k f!
        crouch-scrub
        vr-crouch-view f@ vr-crouch-lift!
        vr-crouch-k f@ 0.15e f> if  fiona-half-hidden state!  else  fiona-half-hidden state-off  then
        crouch-out?
    until
    0e vr-crouch-lift!
    fiona-half-hidden state-off
    fiona-p holder-of @ acting @ = if  0 pose-her!  then
    0 vr-crouch-was ! ;

\ a crouch starts when the headset is down far enough and she is free to crouch: standing on her
\ own, not held, no panic, no scene, no fade, the world going on
: may-vr-crouch? ( -- flag )
    vr? 0= if  false exit  then
    read-crouch
    vr-crouch-drop f@ vr-crouch-engage f> 0= if  false exit  then
    hewie-played? 0=  no-flee state? 0= and
    request-now @ spirit-request <> and
    fiona-doing-now @ backstep-doing <> and
    fiona-idle? and  fiona-sub-now @ 0= and  fiona here? and
    struck-by-now @ 0< and
    fiona-p holder-of @ 0< and  hand-pick @ 0= and  skipped-in-vr @ 0= and
    fading @ 0= and  world-held state? 0= and  world-frozen state? 0= and
    fiona-hidden state? 0= and  in-panic? 0= and
    scene-step @ scene-running <> and ;

\ each frame (story/common.fs's crouching, in the headset): one crouch at a time. A crouch stopped
\ by a room change (its task killed, not its own return) leaves its flag and the view's lift behind:
\ the first frame here puts them right.
: vr-crouching ( -- )
    ['] vr-crouches running? if  exit  then
    0e vr-crouch-lift!
    vr-crouch-was @ if
        0 vr-crouch-was !  fiona-half-hidden state-off
    then
    may-vr-crouch? if
        ['] vr-crouches start dup 0< if  drop  else  -1 vr-crouch-was !  drop  then
    then ;
