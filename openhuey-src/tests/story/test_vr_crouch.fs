\ The headset's crouch (scripts/story/crouch.fs): the headset lowered past 0.12 m crouches her -
\ crouch-down-anim, scrubbed by how far it is down, and fiona-half-hidden set as the L1 crouch sets
\ it. Deeper, the scrub goes on to the animation's last frame; back up, she stands and is free
\ again. Out of the headset L1 crouches her as before (tests/story/test_crouch.fs). Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_crouch.fs
IN: test-vr-crouch
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    fiona.state fiona.model fiona.anims vectors story.crouch ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: in-place ( xt -- x )  room-id room-name actor-named enter  execute  leave-actor ;
: her-anim ( -- id )  [: fiona.model:anim@ ;] as-her ;
: her-frame ( F: -- f )  [: her-model @ actor act.frame sf@ ;] as-her ;
: her-frames ( -- n )  [: her-model @ motion-frames ;] as-her ;
: crouch-held? ( -- flag )  [: fiona held? ;] in-place ;
: free? ( -- flag )  [: her-scripted @ ;] as-her 0=  crouch-held? 0= and ;
: half? ( -- flag )  fiona-half-hidden state-flag? ;
: crouch-anim? ( -- flag )  her-anim crouch-down-anim = ;
create was-pos 3 floats allot
create now-pos 3 floats allot
: her-pos! ( -- )  game:fiona body-pos was-pos vec! ;
: moved? ( -- flag )  game:fiona body-pos now-pos vec!  now-pos was-pos vec-dist 0.1e f> ;
fvariable was-frame

variable was-vr   vr? was-vr !
free-play  60 run-frames
no-flee state-flag-clear
-1 vr!
0e vr-crouch-drop f!  0e vr-crouch-view f!
60 run-frames

testing with the headset up she does not crouch
T{ half? crouch-held? -> 0 0 }T

testing the headset down 0.30 m: she crouches, the animation at that depth
her-pos!
0.30e vr-crouch-drop f!  0.30e vr-crouch-view f!
30 run-frames
T{ crouch-anim? half? crouch-held? -> -1 -1 -1 }T
T{ vr-crouch-lift  0.30e 12.5e f*  f- fabs 0.01e f< -> -1 }T
T{ her-frame 0e f> -> -1 }T
her-frame was-frame f!

testing ... and deeper, the scrub goes on until its lowest frame
0.55e vr-crouch-drop f!  0.55e vr-crouch-view f!
40 run-frames
T{ her-frame was-frame f@ f> -> -1 }T
T{ her-frame her-frames 1- s>f f- fabs 0.01e f< -> -1 }T
T{ moved? -> 0 }T

testing ... back up, she stands and is free again
0e vr-crouch-drop f!  0e vr-crouch-view f!
60 run-frames
T{ half? free? crouch-anim? -> 0 -1 0 }T
T{ vr-crouch-lift fabs 0.01e f< -> -1 }T

was-vr @ vr!
0e vr-crouch-drop f!  0e vr-crouch-view f!
test-summary
