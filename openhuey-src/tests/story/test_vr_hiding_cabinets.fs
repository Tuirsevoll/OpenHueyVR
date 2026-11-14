\ The headset's physical hiding spots in the old mansion's cabinets (story/kit.fs, game/vrspots.c):
\ each registers, shuts its two doors and hides her, then opens and exposes her. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_hiding_cabinets.fs
IN: test-vr-hiding-cabinets
USING: tester engine game-state actors messages keys room-names game debug facts flag-names story.kit
    floor-flags fiona.state ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: put-at ( F: x y z -- )  [: her-at vec! ;] as-her  [: her-at vec@ vr-spot-here ;] as-her ;
: hidden? ( -- flag )  fiona-hidden state-flag? ;
: half? ( -- flag )  fiona-half-hidden state-flag? ;
: shut-cab ( -- )  s" l_door0" 53 placed-frame!  s" r_door0" 53 placed-frame! ;
: open-cab ( -- )  s" l_door0" 30 placed-frame!  s" r_door0" 30 placed-frame! ;
: cabinet ( room F: x z -- )
    {: F: x z :}  true vr!  room!  60 run-frames  x 0e z put-at
    shut-cab  20 run-frames
    T{ vr-spot-id 0< 0=  hidden?  half? -> -1 -1 0 }T
    open-cab  20 run-frames
    T{ hidden?  half? -> 0 0 }T ;

free-play  10 run-frames
old-mansion-1f-22  152e 70e cabinet
old-mansion-b1-4  14.5e 33e cabinet
old-mansion-b1-5  21.5e -33e cabinet
test-summary
