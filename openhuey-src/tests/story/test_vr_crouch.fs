\ The headset's crouch (scripts/story/crouch.fs, scripts/fiona/moving.fs): the drop is a share of
\ the calibrated standing height, never a head tilt; the crouch trigger (L1 in the pad's terms)
\ ramps the same share. Below 13% nothing; 13% -> 25% scrubs the crouch animation 0 -> 1; the
\ hide flag (fiona-half-hidden, the L1 crouch's) comes on at 25% and goes off only at 18%. While
\ it is on she is not held: the stick walks her at 35% of a walk (the slowed walk), turning as
\ usual, and letting go returns her to the crouch pose. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_crouch.fs
IN: test-vr-crouch
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    fiona.state fiona.model fiona.anims vectors story.crouch fiona.sneak ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: in-place ( xt -- x )  room-id room-name actor-named enter  execute  leave-actor ;
: her-anim ( -- id )  [: fiona.model:anim@ ;] as-her ;
: her-frame ( F: -- f )  [: her-model @ actor act.frame sf@ ;] as-her ;
: her-frames ( -- n )  [: her-model @ motion-frames ;] as-her ;
: her-rate ( F: -- f )  [: her-model @ actor act.rate sf@ ;] as-her ;
: her-settled? ( -- flag )  [: fiona.model:settled? ;] as-her ;
: crouch-held? ( -- flag )  [: fiona held? ;] in-place ;
: free? ( -- flag )  [: her-scripted @ ;] as-her 0=  crouch-held? 0= and ;
: half? ( -- flag )  fiona-half-hidden state-flag? ;
: crouch-anim? ( -- flag )  her-anim crouch-down-anim = ;
: walk-group? ( -- flag )  [: fiona.model:group walking-group = ;] as-her ;
: sneak-k@ ( F: -- k )  fiona.sneak:sneak-k f@ ;
: sneak-hide? ( -- flag )  fiona.sneak:sneak-hide @ 0<> ;
create was-pos 3 floats allot
create now-pos 3 floats allot
: her-pos! ( -- )  game:fiona body-pos was-pos vec! ;
: moved? ( -- flag )  game:fiona body-pos now-pos vec!  now-pos was-pos vec-dist 0.1e f> ;
fvariable was-frame

variable was-vr   vr? was-vr !
free-play  60 run-frames
no-flee state-flag-clear
0.13e vr-settings vr.crouch-deadzone sf!
0.25e vr-settings vr.crouch-on sf!
0.18e vr-settings vr.crouch-off sf!
0.45e vr-settings vr.sneak-speed sf!
-1 vr!
0e vr-crouch-test f!
60 run-frames

testing with the headset up: no crouch
T{ crouch-anim? half? crouch-held? sneak-hide? sneak-k@ 0e f> -> 0 0 0 0 0 }T

testing a 10% drop (below the 13% deadzone): no crouch animation, no hide flag
0.10e vr-crouch-test f!
60 run-frames
T{ crouch-anim? half? sneak-hide? sneak-k@ 0e f> -> 0 0 0 0 }T

testing a 20% drop: the crouch animation scrubs, no hide flag yet
0.20e vr-crouch-test f!
30 run-frames
T{ crouch-anim? half? sneak-hide? free? her-settled? -> -1 0 0 -1 -1 }T
T{ sneak-k@ 0.5833e f- fabs 0.01e f< -> -1 }T
T{ her-frame 0e f> -> -1 }T

testing a 30% drop: the hide flag is on, the animation full, still free (not held)
0.30e vr-crouch-test f!
30 run-frames
T{ crouch-anim? half? sneak-hide? free? her-settled? -> -1 -1 -1 -1 -1 }T
T{ sneak-k@ 1e f- fabs 0.001e f< -> -1 }T
T{ her-frame her-frames 1- s>f f- fabs 0.01e f< -> -1 }T

testing rising to 20%: the hide flag stays on (hysteresis), the pose stays full
0.20e vr-crouch-test f!
30 run-frames
T{ half? sneak-hide? -> -1 -1 }T
T{ sneak-k@ 1e f- fabs 0.001e f< -> -1 }T

testing rising to 16%: the hide flag lets go, the animation scrubs down (off -> deadzone)
0.16e vr-crouch-test f!
30 run-frames
T{ half? sneak-hide? crouch-anim? -> 0 0 -1 }T
T{ sneak-k@ 0.6e f- fabs 0.01e f< -> -1 }T

testing back to 10%: nothing
0.10e vr-crouch-test f!
60 run-frames
T{ crouch-anim? half? sneak-hide? -> 0 0 0 }T

testing a stale scripted move (mode led, no doing) put right: the crouch works again
[: led-mode her-mode !  anim-doing her-doing ! ;] as-her
0.30e vr-crouch-test f!
60 run-frames
T{ crouch-anim? half? free? -> -1 -1 -1 }T
0e vr-crouch-test f!
60 run-frames

testing a stale hold (holder set, no doing) put right: the crouch works again
[: fiona-p 0 holder-of ! ;] in-place
0.30e vr-crouch-test f!
60 run-frames
T{ crouch-anim? half? free? -> -1 -1 -1 }T
0e vr-crouch-test f!
60 run-frames

testing the crouch trigger: held, the same shares bring her down; let go, up again
key: Q true key-hold
8 run-frames
T{ crouch-anim? half? sneak-hide? free? -> -1 -1 -1 -1 }T
T{ sneak-k@ 1e f- fabs 0.001e f< -> -1 }T
key: Q false key-hold
30 run-frames
T{ crouch-anim? half? sneak-hide? -> 0 0 0 }T

testing out of the headset, the sneak walk: the stick moves her at 35%, the hide flag stays
was-vr @ vr!
1 fiona.sneak:sneak-hide !  1e fiona.sneak:sneak-k f!  0.35e fiona.sneak:sneak-speed f!
fiona-half-hidden state!
her-pos!
key: W true key-hold
30 run-frames
T{ moved? walk-group? crouch-anim? fiona-half-hidden state-flag? fiona.sneak:sneak-hide @ her-settled? -> -1 -1 0 -1 1 -1 }T
T{ her-rate 0.35e f- fabs 0.05e f< -> -1 }T
key: W false key-hold
0 fiona.sneak:sneak-hide !  0e fiona.sneak:sneak-k f!
fiona-half-hidden state-off

test-summary
