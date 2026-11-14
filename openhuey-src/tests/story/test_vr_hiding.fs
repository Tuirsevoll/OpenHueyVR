\ The headset's physical hiding spots (scripts/story/kit.fs, game/vrspots.c): the nursery wardrobe's
\ doors, their closure, the full/half/exposed flags and the step the box lets through. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_hiding.fs
IN: test-vr-hiding
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    story.places.castle-2f-12 floor-flags fiona.state ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: put-at ( F: x y z -- )  [: her-at vec! ;] as-her  [: her-at vec@ vr-spot-here ;] as-her ;
: hidden? ( -- flag )  fiona-hidden state-flag? ;
: half? ( -- flag )  fiona-half-hidden state-flag? ;
: frame-both ( n -- )  {: n :}  s" left_door" n placed-frame!  s" right_door" n placed-frame! ;
: shut-both ( -- )  53 frame-both ;
: open-both ( -- )  30 frame-both ;
: inside ( -- )  16.8e 0e 45e put-at ;
: outside ( -- )  16.8e 0e 35e put-at ;
variable px  variable pz
: walk-in ( -- )   \ (a virtual +z walk from the flat screen's entry, the step clamped each time)
    16.729e px sf!  33.985e pz sf!
    40 0 do
        px sf@  0e  pz sf@  0e 0.5e  vr-spot-step-at
        pz sf@ f+  pz sf!
        px sf@ f+  px sf!
    loop ;

free-play  10 run-frames
true vr!
castle-2f-12 room!  60 run-frames
testing shut doors, inside: the spot is registered
inside  shut-both  20 run-frames
T{ vr-spot-id 0< 0= -> -1 }T

testing shut doors, inside: full, enclosed, 100%
T{ hidden?  half?  vr-spot-enclosed?  vr-spot-closure 100e f- fabs 0.5e f< -> -1 0 -1 -1 }T

testing a peek to 81%: full stays
inside  50 frame-both  20 run-frames
T{ hidden?  half? -> -1 0 }T

testing below 80%: half
inside  49 frame-both  20 run-frames

T{ hidden?  half? -> 0 -1 }T

testing back to 87%: full again
inside  51 frame-both  20 run-frames
T{ hidden?  half? -> -1 0 }T

testing open doors: exposed
inside  30 frame-both  20 run-frames
T{ hidden?  half? -> 0 0 }T

testing one door shut, one open: exposed
inside  s" left_door" 53 placed-frame!  s" right_door" 30 placed-frame!  20 run-frames
T{ hidden?  half? -> 0 0 }T

testing leaving the box: exposed at once
inside  shut-both  20 run-frames
outside  2 run-frames
T{ hidden?  half?  vr-spot-inside? -> 0 0 0 }T


testing the box walls: the back and a side stop a step, not the doorway
T{ 23.165e 0e 42.381e 0e 2e vr-spot-step-at fswap fdrop 1.5e f< -> -1 }T
T{ 24.933e 0e 35.663e 2e 0e vr-spot-step-at fdrop 1.7e f< -> -1 }T

testing the step: a shut door stops it, an open one lets it through
shut-both  10 run-frames
T{ 16.8e 0e 42e 0e 2e fnegate vr-spot-step-at fswap fdrop 1.8e fnegate f> -> -1 }T
open-both  10 run-frames
T{ 16.8e 0e 42e 0e 2e fnegate vr-spot-step-at fswap fdrop 1.9e fnegate f< -> -1 }T


testing the front approach reaches the spot and walks in through the open doors
open-both  10 run-frames
16.729e 0e 33.985e put-at  2 run-frames
T{ vr-spot-noclip? -> -1 }T
walk-in
T{ pz sf@ 42e f> -> -1 }T
20.69e 0e 39.91e put-at  2 run-frames
T{ vr-spot-inside? -> -1 }T

test-summary

