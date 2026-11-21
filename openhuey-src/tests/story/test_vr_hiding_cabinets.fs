\ The headset's physical hiding spots in the old mansion (game/vrspots.c): the cabinets' marker spots
\ (PROPS/hidespots.txt, injected as the check parses them) - one closure shuts both doors and hides
\ her, opening it exposes her - and the gallery curtain, open at entry so she can walk in. The
\ cabinet of old-mansion-1f-22 has no marker: it keeps the place's typed box and the hands' leaves.
\ Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_hiding_cabinets.fs
IN: test-vr-hiding-cabinets
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    floor-flags fiona.state ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: put-at ( F: x y z -- )  [: her-at vec! ;] as-her  [: her-at vec@ vr-spot-here ;] as-her ;
: hidden? ( -- flag )  fiona-hidden state-flag? ;
: half? ( -- flag )  fiona-half-hidden state-flag? ;
: marker ( addr len -- )  0 vr-markers-line drop ;
: shut-cab ( -- )  s" l_door0" 53 placed-frame!  s" r_door0" 53 placed-frame! ;
: open-cab ( -- )  s" l_door0" 30 placed-frame!  s" r_door0" 30 placed-frame! ;
: closure! ( pct -- )  vr-spot-closure!  4 run-frames ;

\ (old-mansion-1f-22, no marker: the typed box and the hands' leaves, as they were)
: cabinet ( room F: x z -- )
    {: F: x z :}  true vr!  room!  60 run-frames  x 0e z put-at
    shut-cab  20 run-frames
    T{ vr-spot-id 0< 0=  hidden?  half? -> -1 -1 0 }T
    open-cab  20 run-frames
    T{ hidden?  half? -> 0 0 }T ;

\ (a marker spot: its one closure)
: marker-cab ( room F: x z -- )
    {: F: x z :}  true vr!  room!  60 run-frames  x 0e z put-at  2 run-frames
    100 closure!
    T{ vr-spot-id 0< 0=  hidden?  half? -> -1 -1 0 }T
    30 closure!
    T{ hidden?  half? -> 0 0 }T ;

free-play  10 run-frames
s" ST_050 wardrobe bound 17.9540 12.2715 33.4434 3.8153 7.5129 11.7158 0.500000 -0.500000 0.500000 0.500000" marker
s" ST_050 wardrobe grab 23.9365 12.2715 32.0313 3.8153 1.3535 11.7158 0.500000 -0.500000 0.500000 0.500000" marker
s" ST_051 wardrobe bound 17.9999 12.2715 -33.2768 3.8153 7.5129 11.7158 0.500000 -0.500000 0.500000 0.500000" marker
s" ST_051 wardrobe grab 11.9648 12.2715 -31.7358 3.8153 1.3535 11.7158 0.500000 -0.500000 0.500000 0.500000" marker
s" ST_05C curtain bound -69.9979 14.8218 -128.9355 4.9799 5.7811 13.8571 0.500000 -0.500000 0.500000 0.500000" marker
s" ST_05C curtain grab -72.7273 14.8218 -124.8852 2.7067 1.9261 13.8571 0.500000 -0.500000 0.500000 0.500000" marker

old-mansion-1f-22  152e 70e cabinet
80  18e 33.4e marker-cab
81  18e -33.3e marker-cab

testing the curtain: open at entry, so she may walk into its BOUND
true vr!  92 room!  60 run-frames
-70e 0e -123.5e put-at  2 run-frames
T{ vr-spot-id 0< 0=  vr-spot-closure 50e f< -> -1 -1 }T
T{ -70e 0e -123.5e 0e -2e vr-spot-step-at  fswap fdrop 1.9e fnegate f< -> -1 }T

test-summary
