\ Fiona's VR bones by outfit (src/game/vrbones.c, the fiona-vr-rig mod's vr_bones.txt): every
\ costume gives her own game-bone numbers for the VR body's roles, and the wrist is the parent of
\ the mod's finger 1a. Loads each costume with `wear` (0, 1, 2, 3, 6, 7, 8), variant 0 and the torn
\ variant 6, prints its table, and checks the table agrees with the skeleton. Headless:
\   openhuey --test openhuey-src/tests/fiona/test_vr_bones.fs
\ With no mod the non-FIN costumes are skipped (their models have no vr_bones.txt); FIN_000's own
\ numbering is still checked.
IN: test-vr-bones
USING: tester engine game-state actors messages room-names game facts fiona.state fiona.looks debug ;

variable current-model

\ the story flags `wear-variant` reads (scripts/fiona.fs): none set is variant 0.
: no-variants ( -- )
    $41 story-flag-clear  $6F story-flag-clear  $82 story-flag-clear
    $88 story-flag-clear  $94 story-flag-clear  $95 story-flag-clear ;
: .model ( -- )  fiona enter  fiona:model-name  leave-actor  13 type ;
: .role ( id addr len -- )  vr-bone . ;
\ the table as the vrbones: log prints it (chest, neck, head, eyes, both arms)
: .roles ( id -- )
    ."  L arm " dup s" L clavicle" .role dup s" L upperarm" .role dup s" L elbow" .role
    dup s" L forearm" .role dup s" L wrist" .role
    ."  R arm " dup s" R clavicle" .role dup s" R upperarm" .role dup s" R elbow" .role
    dup s" R forearm" .role dup s" R wrist" .role
    ."  chest " dup s" chest" .role ." neck " dup s" neck" .role ." head " dup s" head" .role
    ."  eyes " dup s" L eye" .role ." /" dup s" R eye" .role drop ;
\ the mod's finger 1a hangs from the wrist the table names
: lwrist-ok? ( id -- flag )
    dup s" arm left finger 1a" bone-named dup 0< if  drop drop -1 exit  then
    >r dup s" L wrist" vr-bone  swap  r> bone-parent  =  if -1 else 0 then ;
: rwrist-ok? ( id -- flag )
    dup s" arm right finger 1a" bone-named dup 0< if  drop drop -1 exit  then
    >r dup s" R wrist" vr-bone  swap  r> bone-parent  =  if -1 else 0 then ;
\ variant 0 / 6, costume k: one table printed; -1 when it is good (or the mod is not installed)
: wardrobe ( variant k -- flag )
    {: var k :}
    no-variants
    var 6 = if  $95 story-flag-set  then
    k game:fiona send costume
    20 run-frames
    0 cast-model current-model !
    current-model @ 0< if
        ." vrbones: costume " k . ." not loaded" cr  0 exit
    then
    ." vrbones: " .model ."  variant " var . ."  ok " current-model @ vr-bones-ok? .
    current-model @ .roles
    ."  from-file " current-model @ vr-bones-from-file? . cr
    current-model @ vr-bones-from-file? if
        current-model @ lwrist-ok?  current-model @ rwrist-ok?  and
        current-model @ vr-bones-ok? and
    else
        current-model @ vr-bones-ok? 0= if
            ." vrbones: " .model ."  skipped (no vr_bones.txt: install fiona-vr-rig)" cr
        then
        -1
    then ;

free-play  castle-1f-1 room!  20 run-frames

\ her clothes, the slip, O_FIH, O_FIO, O_FIB, O_FIC, O_FIF - variant 0
T{ 0 0 wardrobe -> -1 }T
T{ 0 1 wardrobe -> -1 }T
T{ 0 2 wardrobe -> -1 }T
T{ 0 3 wardrobe -> -1 }T
T{ 0 6 wardrobe -> -1 }T
T{ 0 7 wardrobe -> -1 }T
T{ 0 8 wardrobe -> -1 }T
\ ... and each torn as far as the story goes (the slip has no variants)
T{ 6 0 wardrobe -> -1 }T
T{ 6 2 wardrobe -> -1 }T
T{ 6 3 wardrobe -> -1 }T
T{ 6 6 wardrobe -> -1 }T
T{ 6 7 wardrobe -> -1 }T
T{ 6 8 wardrobe -> -1 }T

test-summary
