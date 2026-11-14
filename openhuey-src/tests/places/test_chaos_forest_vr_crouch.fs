\ L1 by a Chaos Forest bush (story/places/forest.fs, bush-frame): in the headset a tap starts no
\ bush crouch there either - no bush script holds her, none of crouch-down-anim, crouching-anim or
\ crouch-up-anim, and not her hiding (holding L1 is the VR crouch of story/crouch.fs, not the bush);
\ out of it, L1 puts her in the bush as before. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/places/test_chaos_forest_vr_crouch.fs
IN: test-chaos-forest-vr-crouch
USING: tester engine game-state actors messages keys room-names game debug facts flag-names
    fiona.state fiona.model fiona.anims story.kit story.places.forest story.places.chaos-forest-4 ;

: as-her ( xt -- x )  game:fiona enter  execute  leave-actor ;
: in-place ( xt -- x )  room-id room-name actor-named enter  execute  leave-actor ;
: doing-busy? ( -- flag )  [: acts @ ;] in-place 0<> ;
: free? ( -- flag )  [: her-scripted @ ;] as-her 0=  world-held state-flag? 0= and ;
: soon ( xt n -- flag )  0 ?do  dup execute if  drop true unloop exit  then  game-tick  loop  drop false ;
: on-floor ( F: x y z -- x y' z )  {: F: x y z :}  x  x y z nav-floor drop  z ;
: put ( deg F: x y z -- )   \ (on the floor, on a triangle she walks - else the nearest the mesh has)
    on-floor s>f tp-facing  [: her-at $28020018 v-tri-in  dup 0< if  drop her-at vec@ nav-tri  then  her-tri ! ;] as-her  10 run-frames ;
: by-bush ( -- )  0 -61.3e 9e 0.24e put ;
: hidden? ( -- flag )  fiona-hidden state-flag? ;
: crouch-anim? ( -- flag )   \ (one of the bush crouch's animations, just now)
    [: fiona.model:anim@ {: a :}  a crouch-down-anim =  a crouching-anim = or  a crouch-up-anim = or ;] as-her ;
: crouch-seen? ( n -- flag )   \ (run n frames: any of them, or her hidden)
    false swap 0 ?do  game-tick  crouch-anim? or  hidden? or  loop ;

variable was-vr   vr? was-vr !
free-play  10 run-frames  0 game:fiona send costume  stalkers-blind state-flag-set  summoner-on state-flag-clear
chaos-forest-4 room!  60 run-frames

testing in the headset: L1 by the bush starts no crouch - no animation, not hidden, nothing doing
-1 vr!
by-bush
l1 press
T{ 60 crouch-seen?  doing-busy? -> 0 0 }T
testing ... and out of it, as the original: L1 puts her in the bush, and circle gets her out
was-vr @ vr!
by-bush
l1 press
T{ 60 crouch-seen? -> -1 }T
60 run-frames  circle press  [: hidden? 0= ;] 300 soon drop  [: doing-busy? 0= ;] 300 soon drop
T{ hidden?  free? -> 0 -1 }T

test-summary
