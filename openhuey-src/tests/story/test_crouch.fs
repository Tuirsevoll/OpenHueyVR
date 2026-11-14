\ The shared crouch (story/common.fs, $81 / $82 / $87) started at L1: in the headset the L1
\ (the crouch trigger) ramps the VR crouch of story/crouch.fs instead - the same scrubbed pose
\ and hide flag, without the script holding her; out of it, L1 held crouches her as the original
\ does. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_crouch.fs
IN: test-crouch
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    fiona.state fiona.model fiona.anims ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: in-place ( xt -- x )  room-id room-name actor-named enter  execute  leave-actor ;
: doing-busy? ( -- flag )  [: acts @ ;] in-place 0<> ;
: free? ( -- flag )  [: her-scripted @ ;] as-her 0=  world-held state-flag? 0= and ;
: crouch-anim? ( -- flag )   \ (one of the crouch's animations, just now)
    [: fiona.model:anim@ {: a :}  a crouch-down-anim =  a crouching-anim = or  a crouch-up-anim = or ;] as-her ;
: crouch-seen? ( n -- flag )   \ (run n frames: any of them, or half hidden - L1 held)
    false swap 0 ?do  game-tick  crouch-anim? or  fiona-half-hidden state-flag? or  loop ;
: hold-l1 ( -- )  key: Q true key-hold ;
: let-l1 ( -- )  key: Q false key-hold ;

variable was-vr   vr? was-vr !
60 run-frames
no-flee state-flag-clear   \ (the opening's hold on her - no crouching - over)

testing in the headset: L1 (the crouch trigger) holds her down by the VR rules, still free
-1 vr!   \ (main.c's vr? is a constant: the tests play the headset by writing its body, read as the scripts run)
hold-l1
T{ 60 crouch-seen?  doing-busy? -> -1 0 }T
let-l1  60 run-frames
T{ fiona-half-hidden state-flag? -> 0 }T
testing ... and out of it, as the original: L1 held, she crouches (half hidden); let go, up again
was-vr @ vr!  30 run-frames
hold-l1
T{ 60 crouch-seen?  fiona-half-hidden state-flag? -> -1 -1 }T
let-l1  60 run-frames
T{ fiona-half-hidden state-flag?  free? -> 0 -1 }T

test-summary
