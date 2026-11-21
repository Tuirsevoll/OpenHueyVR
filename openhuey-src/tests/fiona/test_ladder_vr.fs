\ The headset at the top of a ladder (scripts/fiona/ladder.fs, docs/subsystems/fiona.md F5): mounting
\ there lowers her onto the first rung and turns her to face the rungs, with the view turned with her
\ (a 180 when it was looking away), so the rungs can be grabbed. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/fiona/test_ladder_vr.fs
IN: test-ladder-vr
USING: tester engine game-state actors messages room-names game debug keys facts flag-names fiona.state fiona.model fiona.ladder ;

: hers ( xt -- x )  fiona enter  execute  leave-actor ;
: height ( -- n )  [: her-at 4 + sf@ f>s ;] hers ;
: ladder ( -- link side )  [: her-ladder @ her-ladder-side @ ;] hers ;
: on? ( -- flag )  [: her-mode @ 3 = ;] hers ;
: lowering? ( -- flag )  [: her-act @ ['] fiona.ladder:lower-on = ;] hers ;
: faces-rungs? ( -- flag )  [: her-yaw f@ 0 1 nav-link-yaw f- fabs 0.02e f< ;] hers ;
\ Fiona at an end's spot, facing its way (or turned from it)
: at-end ( side turn -- )
    >r  0 over  dup if  0e 6.7e  else  0e -7.1068e  then  nav-link-front drop
    0 swap nav-link-yaw  180e f* pi f/  r> s>f f+  tp-facing  10 run-frames ;

2 run-frames
0 fiona send costume  1 run-frames
game:story send story-stop  deliver  progress pr.state 8 0 fill
castle-1f-2 room!  20 run-frames

testing in the headset, mounting at the top lowers her onto the first rung
1 0 at-end
-1 vr!
circle press
T{ ladder  lowering?  on? -> 0 1 -1 -1 }T

testing ... there, holding on and facing the rungs (the view turned with her)
45 run-frames
T{ height 5 >  height 20 <  and -> -1 }T
T{ lowering?  on? -> 0 -1 }T
T{ faces-rungs? -> -1 }T

test-summary
