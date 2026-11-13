\ fiona/ladder.fs - Fiona on a ladder (src/game/fiona.c Fiona_StateLadder / LadderWalk / OnLadder /
\ LadderOff; Progress_CharRequests' nav door regions, Actor_FacingDoor, Actor_DoorFront). A
\ ladder is a nav link of the room (`nav-link-*`: its two ends' triangles, facings and points;
\ side 0 its foot, 1 its top). Free, in a ladder's region on one of its sides, nobody else there
\ but Hewie, facing along that side - the action button: she walks to its end's spot (at the top
\ 1.7 back from it, at the foot 2.1 out), faces the ladder and gets on ($700 from the top, $704 from the
\ foot). On it, as each move comes round, the stick (Fiona_StateOnLadder: the vertical plus the
\ pad, pushed up negative): up - down a rung ($701, its end $702), off at the foot within 18
\ of it ($703); down - up a rung ($705, its end $706), off at the top
\ within 3 of it ($707); let go - holding ($708 / $709). The stalker at its foot keeps her from
\ getting off there, at its top (within his height above her) from going up. Off: at the move's
\ event $20 she is put by that end and free. Her moves on it are her animations' own,
\ up and down too.
\ In the headset (docs/subsystems/hands.md, "Pull to climb") the stick does nothing on a ladder and
\ her root motion is not used: her hands' pull moves her up, and an end is taken when her height has
\ reached it - the get-off moves below are then the same ones.
IN: fiona.ladder
USING: engine vectors game-state actors messages common keys facts flag-names rumble fiona.state fiona.anims fiona.sounds fiona.model fiona.moving fiona.spots fiona.controls fiona.slam fiona.fear fiona.voice ;

: motion-flags ( -- bits )  her act.mflags l@ ;   \ (MOTION_EVENTS: the motion's flags; $20 come round)
: off-ladder ( -- )  -1 her-ladder !  to-idle ;

\ ---- moved by her animation's root motion, free of the floor (up and down too) ----
: climb-move ( -- )
    root-turn {: F: x y z :}  x z her-yaw f@ rotate-by {: F: dx dz :}
    her-at v.x sf@ dx f+ her-at v.x sf!  her-at v.z sf@ dz f+ her-at v.z sf!
    her-at v.y sf@ y f+ her-at v.y sf! ;
\ where she stands off it at an end (kFionaMeetOffsets: the foot 2.1 out, the top 1.7 back from it)
: off-offset ( side -- side ) ( F: -- ox oz )  0e  dup if  6.6709e  else  -7.1132e  then ;
\ her triangle at an end (after each move: off at the foot or on from it - the foot's; else the top's)
: end-tri ( side -- )
    her-ladder @ swap off-offset nav-link-front  fdrop fdrop fdrop
    dup 0< if  drop  else  her-tri !  then ;
\ the end's point (Rooms' nav +0x5C: 0 the foot, 1 the top)
: end-y ( side -- ) ( F: -- y )  her-ladder @ swap nav-link-spot  fdrop fswap fdrop ;

\ ---- the stalker in a ladder's region (Progress_WhoIsWhere's region bits: 4 side 0, its foot;
\ 2 side 1, its top): at the foot he keeps her from getting off there; at the top, within his
\ height above her, from going up ----
: stalker-side ( link -- side | -1 )   \ (his side of it; -1 not in its region)
    {: link :}  stalker-in-room {: him :}
    him 0< if  -1 exit  then
    him body-at link nav-link-at? 0= if  -1 exit  then
    him body-at him body-tri link nav-link-side ;
: stalker-on ( side -- flag )
    {: side :}  her-ladder @ stalker-side side <> if  false exit  then
    side 0= if  true exit  then
    her-at v.y sf@ stalker-in-room body-at v.y sf@ f-  stalker-in-room body-dims fswap fdrop f< ;

\ ---- knocked off it (a blow while on it, Fiona_Reaction 8: Fiona_StateCaughtCrawling): to the
\ spot 10 out from its top - near it (under 20 below) pulled off ($F02), else dropped there
\ ($70A, then $70B as it comes round), falling 0.5 faster each frame over as many frames as that
\ takes; there her cry ($7D pulled, $81 fallen); fallen, down ($70C) and up ($100D) - her
\ recovery reset (CrawlDrop / CrawlPull / CrawlFall / CrawlDown) ----
: crawl-up ( -- )   \ (standing again: out of the ladder)
    ended? if  her-drop-far @ if  reset-recovery  then  own-mode her-mode !  off-ladder exit  then  root-move ;
: crawl-down ( -- )  ended? if  up-from-back-anim play-cut  ['] crawl-up her-act !  then  root-move ;
: crawl-fall ( -- )  settled? if  ladder-fallen-anim -1 play-table  ['] crawl-down her-act !  then ;
: crawl-drop ( -- )
    -1 her-drop-n +!
    her-drop-n @ if
        her-drop-far @  anim@ ladder-fall-anim <> and  ended? and if  ladder-fall-anim -1 play-table  then
        her-at v.x sf@ her-drop-dx f@ f- her-at v.x sf!
        her-at v.y sf@ her-drop-dy f@ f- her-at v.y sf!
        her-at v.z sf@ her-drop-dz f@ f- her-at v.z sf!
        her-drop-dy f@ 0.5e f+ her-drop-dy f!  exit   \ (falling faster)
    then
    her-at her-drop-at vec-copy  her-drop-tri @ dup 0< if  drop  else  her-tri !  then
    $D0 12 fiona-jolt   \ (landed)
    her-drop-far @ if  long-fall-cry voice  ['] crawl-fall  else  pull-free-cry voice  ['] crawl-up  then  her-act ! ;
: frames-to-fall ( F: h -- ) ( -- n )   \ (0, 0.5, 1 ... a frame: how many frames to fall h)
    {: F: h | F: step :}  1
    begin  h step f- to h  h f0< 0= while  step 0.5e f+ to step  1+  repeat ;
: knocked-off ( -- )
    her-ladder @ 1 0e 10e nav-link-front her-drop-tri !  her-drop-at vec!
    her-at v.y sf@ her-drop-at v.y sf@ f- fdup 20e f< if
        0 her-drop-far !  react-mode her-mode !  pull-free-anim -1 play-table  struck-hard-sound 0 her-sound
    else
        -1 her-drop-far !  ladder-slip-anim -1 play-table  fall-sound 0 her-sound
    then
    frames-to-fall dup her-drop-n !  s>f  {: F: n :}
    her-at v.x sf@ her-drop-at v.x sf@ f- n f/ her-drop-dx f!
    her-at v.z sf@ her-drop-at v.z sf@ f- n f/ her-drop-dz f!
    0e her-drop-dy f!  ['] crawl-drop her-act ! ;

\ ---- off (Fiona_StateLadderOff): at the event $20 put by that end, free ----
\ where a dismount puts her: by the end's front, her triangle there, free
: place-off ( -- )
    anim@ off-ladder-top-anim = 1 0 ?  {: side :}
    her-ladder @ side off-offset nav-link-front                      ( tri ) ( F: x y z )
    dup 0< if  drop fdrop fdrop fdrop  else  her-tri !  her-at vec!  then
    own-mode her-mode !  off-ladder ;
: ladder-off ( -- )
    motion-flags $20 and 0= if  climb-move exit  then
    place-off ;
\ ---- walking off the top in the headset (docs/subsystems/fiona.md, F5): her hands' climb ends with
\ her stepping onto the floor beyond the top - no root motion and no step-off animation (its pose still
\ shows a climb) ----
30 constant WALK_OFF_TICKS   \ (a second at the game's 30 ticks)
: walk-off-top ( -- )   \ (one tick of her walk off the top: the step)
    ladder-pull fdrop                                          \ (a hand's pull is not what walks her now)
    her-drop-n @ 0= if                                         \ (there: put her at the top's front, and free her)
        her-at her-drop-at vec-copy
        her-drop-tri @ dup 0< if  drop  else  her-tri !  then
        own-mode her-mode !  off-ladder exit
    then
    her-drop-dx f@ her-at v.x sf@ f+ her-at v.x sf!
    her-drop-dy f@ her-at v.y sf@ f+ her-at v.y sf!
    her-drop-dz f@ her-at v.z sf@ f+ her-at v.z sf!
    -1 her-drop-n +! ;
: walk-off-top-start ( side -- )   \ (a hand's climb to the top, with the hands let go: she is placed at the top's edge and walks onto the floor)
    {: side :}
    her-ladder @ side off-offset nav-link-front                   ( tri ) ( F: x y z )
    dup 0< if                                                   \ (no floor beyond it: she steps off where she is)
        drop fdrop fdrop fdrop  own-mode her-mode !  off-ladder exit
    then
    her-drop-tri !  her-drop-at vec!
    side end-y her-at v.y sf!      \ (raised to this end's floor height before the walk; her place across the floor is left as it is)
    WALK_OFF_TICKS her-drop-n !
    WALK_OFF_TICKS s>f {: F: n :}
    her-drop-at v.x sf@ her-at v.x sf@ f- n f/  her-drop-dx f!
    her-drop-at v.y sf@ her-at v.y sf@ f- n f/  her-drop-dy f!
    her-drop-at v.z sf@ her-at v.z sf@ f- n f/  her-drop-dz f!
    walk-anim play-cut
    ['] walk-off-top her-act ! ;

\ ---- on it (Fiona_StateOnLadder): as each move comes round, the stick picks the next ----
\ (the stick's down plus the pad's down less its up - as the original reads it, pushed up is
\ down the ladder: S or the stick pulled back climbs, W or pushed forward goes down)
: stick-climb ( F: -- v )   \ (above 0: up the ladder)
    sz  hands-off? if  exit  then  key: Down key-held? 1 and key: Up key-held? 1 and - s>f f+ ;
: from-end? ( -- flag )  anim@ dup on-ladder-top-anim = swap rung-down-end-anim = or ;   \ (a move's end: cut in)
: rung-down ( -- )   \ (from the top, a rung's end or holding: off at the foot within 18, else the next rung)
    0 end-y her-at v.y sf@ f- 18e f< if
        0 stalker-on if  anim@ ladder-hold-anim <> if  ladder-hold-anim play-own  then  exit  then
        from-end? if  off-ladder-foot-anim play-cut  else  off-ladder-foot-anim play-own  then
        ['] ladder-off her-act !  exit
    then
    from-end? if  rung-down-anim play-cut  else  rung-down-anim play-own  then ;
: going-down ( anim -- moved? )
    case
        on-ladder-top-anim of  rung-down  endof   rung-down-end-anim of  rung-down  endof
        rung-up-end-anim of  rung-down  endof     ladder-hold-anim of  rung-down  endof
        rung-down-anim of  rung-down-end-anim play-cut  endof
        on-ladder-foot-anim of  0 stalker-on if  ladder-hold-anim play-own  else  off-ladder-foot-anim play-own  ['] ladder-off her-act !  then  endof
        rung-up-anim of  rung-down-end-anim play-own  endof   ladder-hold-2-anim of  rung-down-end-anim play-own  endof
        drop false exit
    endcase  true ;
: on-from-foot? ( -- flag )  anim@ dup on-ladder-foot-anim = swap rung-up-end-anim = or ;
: rung-up ( -- )   \ (from the foot, a rung's end or holding: off at the top within 3, else the next rung)
    her-at v.y sf@ 1 end-y f- 3e f< if
        on-from-foot? if  off-ladder-top-anim play-cut  else  off-ladder-top-anim play-own  then
        ['] ladder-off her-act !  exit
    then
    on-from-foot? if  rung-up-anim play-cut  else  rung-up-anim play-own  then ;
: going-up ( anim -- moved? )
    case
        on-ladder-top-anim of  off-ladder-top-anim play-own  ['] ladder-off her-act !  endof
        rung-down-anim of  rung-up-end-anim play-own  endof   ladder-hold-2-anim of  rung-up-end-anim play-own  endof
        rung-up-anim of  rung-up-end-anim play-cut  endof
        rung-down-end-anim of  rung-up  endof   on-ladder-foot-anim of  rung-up  endof
        rung-up-end-anim of  rung-up  endof     ladder-hold-anim of  rung-up  endof
        drop false exit
    endcase  true ;
: hold-on ( anim -- )
    case
        on-ladder-top-anim of  ladder-hold-anim play-own  endof   rung-down-end-anim of  ladder-hold-anim play-own  endof
        on-ladder-foot-anim of  ladder-hold-anim play-own  endof   rung-up-end-anim of  ladder-hold-anim play-own  endof
        rung-down-anim of  ladder-hold-2-anim play-own  endof   rung-up-anim of  ladder-hold-2-anim play-own  endof
    endcase ;
: move-done? ( anim -- flag )  motion-flags $20 and 0<>  swap ladder-hold-anim ladder-slip-anim within or ;   \ (a move come round, or holding)
: stick-move ( anim -- moved? )   \ (the stick: down, up - not with him above her: as let go - or let go)
    stick-climb fdup fabs 0.5e f> 0= if  fdrop hold-on true exit  then
    f0< if  going-down exit  then
    1 stalker-on if  hold-on true  else  going-up  then ;
: nearer-end ( -- side )  anim@ dup off-ladder-foot-anim = swap on-ladder-foot-anim = or 0 1 ? ;   \ (after a move: the foot's for getting off or on there)
\ in the panic she lets go - fallen off as if knocked (Fiona_StateCaughtCrawling: mode 4, sub 8)
: panicked-off ( -- )  thrown-doing her-doing !  thrown-mode her-mode !  off-ladder-r her-sub !  knocked-off ;
\ ---- in the headset she climbs by her hands (docs/subsystems/hands.md, "Pull to climb"): the stick
\ does nothing there and her root motion is not used - the hands' pull moves her up (down the hand is
\ up her, the game's y running down), and an end is taken when her height has reached it ----
: hold-still ( -- )   \ (the hold animation kept: any other is replaced by the first hold)
    anim@ {: anim :}
    anim ladder-hold-anim = anim ladder-hold-2-anim = or 0= if  ladder-hold-anim play-own  then ;
\ side 0 is the physical top on this ladder (the end at the larger height); side 1 is the physical bottom.
: at-top-window? ( -- flag )   \ (her height at the top's floor, within 6 of it or past it)
    her-at v.y sf@ 0 end-y 6e f- f>= ;
variable ladder-free-ticks                 \ (ticks with no hand on a mid-ladder hold: the let-go's count)
variable ladder-gripped                     \ (1 once a hand has gripped the ladder since she mounted: a fall needs this first)
variable fall-breath                        \ (the breath frames a fall gives, set when she falls, used when she lands)
9 constant LET_GO_TICKS                    \ (0.3 s at the game's 30 ticks: both hands off this long mid-ladder, and she falls)
: at-bottom? ( -- flag )                   \ (within 10 of the bottom's floor, or past it: the bottom is side 1)
    her-at v.y sf@ 1 end-y 10e f+ f<= ;
: let-go-fall ( -- )                       \ (both hands let go mid-ladder: she falls off, the same reaction as the panic's let-go)
    her-at v.y sf@ 1 end-y f- 0e fmax  {: F: h :}               \ (the height fallen: her y less the bottom floor's, never below 0)
    65e  1.2e h f*  fmin  add-fear                              \ (fear: 1.2 a unit, at most 65)
    1.6e h f*  f>s  85 min  dup her-run-t !  fall-breath !      \ (breath: the frames it lasts, at most 85)
    thrown-doing her-doing !  thrown-mode her-mode !  off-ladder-r her-sub !  knocked-off ;
: bottom-off ( -- )   \ (placed on the ground at the bottom: the end's front, free)
    her-ladder @ 1 off-offset nav-link-front                      ( tri ) ( F: x y z )
    dup 0< if  drop fdrop fdrop fdrop  else  her-tri !  her-at vec!  then
    own-mode her-mode !  off-ladder ;
: vr-climb ( -- )
    hold-still
    ladder-both? 0= at-top-window? 0 stalker-on 0= and and if   \ (at the top, and not both hands on the ladder: she steps off)
        0 ladder-free-ticks !
        0 walk-off-top-start exit
    then
    ladder-hands? 0= if                                         \ (no hand on the ladder)
        at-top-window? 0= at-bottom? and 0 stalker-on 0= and if   \ (at the bottom, not at the top: she is put on the ground there)
            bottom-off exit
        then
        at-top-window? 0= at-bottom? 0= and ladder-gripped @ and if                  \ (mid-ladder: she falls once both hands have been off for LET_GO_TICKS)
            1 ladder-free-ticks +!
            ladder-free-ticks @ LET_GO_TICKS >=
            if  0 ladder-free-ticks !  let-go-fall  then
        else
            0 ladder-free-ticks !                               \ (at either end: no fall, and the count starts again)
        then
        exit
    then
    0 ladder-free-ticks !                                       \ (a hand on it: the count is cleared)
    1 ladder-gripped !
    ladder-pull {: F: pull :}                                   \ (this tick's pull: down the hand is up her)
    pull her-at v.y sf@ f+ her-at v.y sf!
    pull 0e f<  her-at v.y sf@ 1 end-y f- 3e f<= and  0 stalker-on 0= and if   \ (up this tick, at the bottom: placed there)
        bottom-off exit
    then ;
: on-ladder ( -- )   \ (Fiona_StateOnLadder; until the move has faded in: still)
    settled? 0= if  exit  then
    her-fear-bits @ 2 and if  panicked-off exit  then
    vr? if  vr-climb exit  then
    anim@ {: anim :}
    anim move-done? if  anim stick-move if  nearer-end end-tri  then  then
    climb-move ;

\ ---- getting on (Fiona_StateLadder, LadderWalk): to its end's spot, facing it; there, on ----
6 constant getting-on   7 constant on-rungs   \ (her sub on the ladder)
\ ---- lowering onto the first rung from the top (VR): her height goes from the top's floor down to the first rung, over LOWER_ON_TICKS ticks, along the same line; then she holds on ----
30 constant LOWER_ON_TICKS    \ (about a second at the game's 30 ticks)
: lower-drop ( -- ) ( F: -- d )   12e ;     \ (the drop from the top's floor to the first rung, in game units: a first guess, tuned in play)
: lower-on ( -- )   \ (her lowering onto the first rung, from the top: the per-tick step, then she holds on)
    ladder-pull fdrop                                               \ (a hand's pull is not what moves her here)
    her-drop-n @ 0= if
        her-at her-drop-at vec-copy                                 \ (the first rung: put her there exactly)
        on-rungs her-sub !
        ['] on-ladder her-act !  exit
    then
    her-yaw f@ {: F: before :}                                  \ (the view turns with her: her yaw before the step)
    10e deg>rad turn-to-heading fdrop                           \ (toward the rungs, 10 degrees a tick, over the lowering)
    her-yaw f@ before f- angle-wrap vr-turn-view                 \ (the step she took, given to the view)
    her-drop-dx f@ her-at v.x sf@ f+ her-at v.x sf!
    her-drop-dy f@ her-at v.y sf@ f+ her-at v.y sf!
    her-drop-dz f@ her-at v.z sf@ f+ her-at v.z sf!
    -1 her-drop-n +! ;
: lower-start ( -- )   \ (a mounting at the top: she moves to the rung side of the top's edge, turns to face the rungs, and lowers onto the first rung, one step per tick)
    her-ladder @ 0 nav-link-spot  {: F: x0 F: y0 F: z0 :}        \ (the top spot)
    her-ladder @ 1 nav-link-spot  {: F: x1 F: y1 F: z1 :}        \ (the bottom spot: its height is only the drop's way)
    her-drop-at her-at vec-copy                                  \ (dst src: her place, where the steps start)
    x0  x1 x0 f- 1.2e f* f+  her-drop-at v.x sf!                 \ (1.2 of the way to the bottom spot: about 2 units past it toward the rungs, on the rung side)
    z0  z1 z0 f- 1.2e f* f+  her-drop-at v.z sf!
    y0  y1 y0 f- fdup fabs f/ lower-drop f* f+  her-drop-at v.y sf!   \ (the top's height, the drop toward the bottom's)
    her-ladder @ 0 nav-link-yaw pi f+ her-heading f!             \ (the rung side's facing: the nav yaw turned round)
    her-drop-at 0 v-tri-in  dup 0< if  drop her-tri @  then  her-drop-tri !   \ (the triangle there; hers kept if there is none)
    LOWER_ON_TICKS her-drop-n !
    her-drop-at v.x sf@ her-at v.x sf@ f-  LOWER_ON_TICKS s>f f/  her-drop-dx f!
    her-drop-at v.y sf@ her-at v.y sf@ f-  LOWER_ON_TICKS s>f f/  her-drop-dy f!
    her-drop-at v.z sf@ her-at v.z sf@ f-  LOWER_ON_TICKS s>f f/  her-drop-dz f!
    ['] lower-on her-act ! ;
: ladder-walk ( -- )
    walk-to-spot dup 0< if  drop off-ladder exit  then  if  exit  then
    on-rungs her-sub !
    0 ladder-gripped !
    her-ladder-side @ if  on-ladder-top-anim  else  on-ladder-foot-anim  then
    play-own
    her-ladder-side @ 0= if  lower-start  else  ['] on-ladder her-act !  then ;
: ladder-start ( -- )
    her-ladder @ her-ladder-side @  dup if  0e 6.7e  else  0e -7.1068e  then  nav-link-front   ( tri ) ( F: x y z )
    dup 0< if  drop fdrop fdrop fdrop off-ladder exit  then
    her-ladder @ her-ladder-side @ nav-link-yaw  walk-spot
    ['] ladder-walk her-act ! ;

\ ---- the action button by a ladder (Progress_CharRequests' regions, Progress_PlayerButtons) ----
: facing-along? ( link side -- flag )   \ (Actor_FacingDoor: her forward against its facing)
    nav-link-yaw her-yaw f@ f- fcos f0> ;
: side-here ( link -- side | -1 )   \ (her side of it: 0 the foot, 1 the top; -1 not in its region)
    {: link :}  her-at link nav-link-at? 0= if  -1 exit  then
    her-at her-tri @ link nav-link-side  dup 0 2 within 0= if  drop -1  then ;
: ladder-here ( -- link side | -1 )   \ (on a side of one with the stalker not on it, facing along it)
    {: | side :}
    nav-links 0 ?do
        i side-here to side
        side 0< 0=  i stalker-side side <> and  if
            i side facing-along? if  i side unloop exit  then
        then
    loop  -1 ;
: try-ladders ( -- )
    her-mode @ own-mode <>  her-doing @ free-doing <> or  her-panic-stage @ panicking >= or if  exit  then
    circle button-pressed? 0= if  exit  then
    ladder-here dup 0< if  drop exit  then                              ( link side )
    stalker-touching? if  2drop exit  then
    her-ladder-side !  her-ladder !
    getting-on her-sub !  ladder-doing her-doing !  ladder-mode her-mode !  ladder-start ;
: grab-on ( -- )   \ (a grab mounts her at once, with no mounting animation: she is put at the end's front, facing the ladder, holding on, so she can climb straight away)
    0 ladder-gripped !
    her-ladder @ her-ladder-side @  dup if  0e 6.7e  else  0e -7.1068e  then  nav-link-front   ( tri ) ( F: x y z )
    dup 0< if  drop fdrop fdrop fdrop off-ladder exit  then
    her-tri !  her-at vec!
    her-ladder @ her-ladder-side @ nav-link-yaw her-yaw f!
    on-rungs her-sub !
    ladder-hold-anim play-own
    her-ladder-side @ 0= if  lower-start  else  ['] on-ladder her-act !  then ;
: try-ladder-grab ( -- )   \ (a hand just grabbed a ladder's air: she gets on there, as the button does - no button needed)
    ladder-grab {: link side :}                              \ (always taken, so a grab is not kept for later)
    link 0< if  exit  then
    her-mode @ own-mode <>  her-doing @ free-doing <> or  her-panic-stage @ panicking >= or if  exit  then
    stalker-touching? if  exit  then
    link stalker-side side = if  exit  then                  \ (the stalker is on that side: as the button path does)
    link her-ladder !  side her-ladder-side !
    ladder-doing her-doing !  ladder-mode her-mode !  grab-on ;

