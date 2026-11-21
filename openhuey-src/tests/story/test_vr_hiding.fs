\ The headset's physical hiding spots (game/vrspots.c, PROPS/hidespots.txt): the nursery's marker
\ spot - one closure for both panels, the hidden thresholds, the BOUND and the doorway rule. The
\ markers are injected the way the check parses them (vrmarkers_parse_line). Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_hiding.fs
IN: test-vr-hiding
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    story.places.castle-2f-12 floor-flags fiona.state ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: put-at ( F: x y z -- )  [: her-at vec! ;] as-her  [: her-at vec@ vr-spot-here ;] as-her ;
: hidden? ( -- flag )  fiona-hidden state-flag? ;
: half? ( -- flag )  fiona-half-hidden state-flag? ;
: marker ( addr len -- )  0 vr-markers-line drop ;
: inside ( -- )  21.5e 0e 40.8e put-at  2 run-frames ;   \ (in the BOUND's footprint, her feet on the floor)
: outside ( -- )  14e 0e 33e put-at  2 run-frames ;
: closure! ( pct -- )  vr-spot-closure!  4 run-frames ;
: frames-both ( -- f f )  s" left_door" placed-frame@  s" right_door" placed-frame@ ;
\ (the hide loop itself is the place's own doing: its start and end are the `vrspots: script vr-hides
\  started|ended` lines castle-2f-12.fs prints, and the loop only starts on vr-spot-full? - so what
\  these checks hold it to is the state it starts and stops by.)
: loop-flag-set ( -- )  5 came-while-hidden ! ;
free-play  10 run-frames
true vr!
s" ST_026 wardrobe bound 21.5715 12.6923 40.8213 3.8153 7.5129 11.7158 0.270598 -0.270598 0.653282 0.653281" marker
s" ST_026 wardrobe grab 24.4114 12.6923 35.2774 3.8153 1.3535 11.7158 0.270598 -0.270598 0.653282 0.653282" marker
castle-2f-12 room!  60 run-frames

testing the marker spot: one closure drives both panels
inside  100 closure!
T{ vr-spot-id 0< 0= -> -1 }T
T{ vr-spot-closure 100e f- fabs 1e f< -> -1 }T
T{ frames-both -> 54 54 }T
30 closure!
T{ frames-both -> 38 38 }T

testing shut: full, enclosed, 100%
inside  100 closure!
T{ hidden?  half?  vr-spot-enclosed? -> -1 0 -1 }T

testing 85%: full; 81%: full holds; 79%: half
inside  85 closure!
T{ hidden?  half? -> -1 0 }T
inside  81 closure!
T{ hidden?  half? -> -1 0 }T
inside  79 closure!
T{ hidden?  half? -> 0 -1 }T

testing 90%: full, the gap not yet shut
inside  90 closure!
T{ hidden?  vr-spot-enclosed? -> -1 0 }T

testing open: exposed
inside  30 closure!
T{ hidden?  half? -> 0 0 }T

testing outside the BOUND: exposed at once
inside  100 closure!
T{ hidden? -> -1 }T
outside
T{ hidden?  half?  vr-spot-inside? -> 0 0 0 }T

testing inside with the doors open: nothing
inside  0 closure!
T{ hidden?  half?  vr-spot-full? -> 0 0 0 }T

testing 86%: full
inside  86 closure!
T{ hidden?  vr-spot-full? -> -1 -1 }T

testing 81%: still full, the gap opened again
inside  81 closure!
T{ hidden?  vr-spot-enclosed? -> -1 0 }T

testing 79%: not full (half still, above the 40% rule)
inside  79 closure!
T{ hidden?  half?  vr-spot-full? -> 0 -1 0 }T

testing outside with the doors shut: nothing
outside  100 closure!
T{ hidden?  half?  vr-spot-full?  vr-spot-inside? -> 0 0 0 0 }T

testing the doorway: shut doors stop a step in, open ones let it through
17.68e 0e 36.93e put-at  2 run-frames
100 closure!
T{ 17.68e 0e 36.93e 1.77e 1.77e vr-spot-step-at  fdrop fabs 1e f< -> -1 }T
30 closure!
T{ 17.68e 0e 36.93e 1.77e 1.77e vr-spot-step-at  fdrop fabs 1e f> -> -1 }T

testing shut doors keep her in; open ones let her out
inside  50 closure!
T{ 21.5e 0e 40.8e -6e -6e vr-spot-step-at  fswap fabs 3e f< -> -1 }T
30 closure!
T{ 21.5e 0e 40.8e -6e -6e vr-spot-step-at  fswap fabs 3e f> -> -1 }T

test-summary
