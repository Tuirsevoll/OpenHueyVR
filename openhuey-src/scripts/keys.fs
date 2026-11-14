\ keys.fs - reading the keyboard: held now, or pressed this tick (scancodes from `key: Name`).
IN: keys
USING: engine ;

: key-held? ( scancode -- flag ) key-down? ;
: pressed? ( scancode -- flag ) key-pressed? ;

\ ---- the pad's buttons on the keyboard (the scripts' pad?: 0 circle, 1 square, 2 L1, 3 triangle,
\ 4 R1, 5 cross, 6 start; 7 select, the menu). The keys the player presses for them are their own
\ (the controls, in the settings menu - platform.c: by default Space is cross too, Backspace circle);
\ the scripts read these. ----
enum: button  circle square l1 triangle r1 cross start-button select-button ;
: button-keys ( b -- key1 key2 )   \ (0: none)
    case
        circle of  key: C 0  endof
        square of  key: Z 0  endof
        l1 of  key: Q 0  endof
        triangle of  key: V 0  endof
        r1 of  key: E 0  endof
        cross of  key: X 0  endof
        start-button of  key: Return 0  endof
        select-button of  key: Tab 0  endof   \ (as hg)
        0 0 rot   \ (none: the button stays on top for endcase)
    endcase ;
: key-so? ( key xt -- flag )  over if  execute  else  2drop false  then ;   \ (a key 0 is none)
: either ( k1 k2 xt -- flag )  {: k1 k2 xt :}  k1 xt key-so?  k2 xt key-so?  or ;   \ (either key so)
: button-down? ( b -- flag )  dup vr-down? swap button-keys ['] key-down? either or ;
: button-pressed? ( b -- flag )  dup vr-pressed? swap button-keys ['] key-pressed? either or ;

\ the left stick as the menu's arrows. one direction at a time, repeating like a held key.
\ movement reads the same stick on its own.
variable nav-tick
variable nav-hold
variable nav-bits
: nav-repeat? ( n -- flag )
    dup 1 = if  drop true exit  then
    dup 13 < if  drop false exit  then
    13 - 2 mod 0= ;
: sample-nav ( -- )
    {: | F: x F: y :}
    ticks nav-tick @ = if  exit  then
    ticks nav-tick !
    0 nav-bits !
    vr? if  vr-stick  else  pad-stick  then  to y to x
    x fabs y fabs fmax 0.55e f< if  0 nav-hold !  exit  then
    1 nav-hold +!
    nav-hold @ nav-repeat? 0= if  exit  then
    x fabs y fabs f> if  x f0> if  2  else  8  then  else  y f0> if  4  else  1  then  then  nav-bits ! ;
: nav-up? ( -- flag )  sample-nav nav-bits @ 1 and ;
: nav-right? ( -- flag )  sample-nav nav-bits @ 2 and ;
: nav-down? ( -- flag )  sample-nav nav-bits @ 4 and ;
: nav-left? ( -- flag )  sample-nav nav-bits @ 8 and ;
\ held, for a menu that counts its own repeat (0 up, 1 right, 2 down, 3 left)
: stick-way? ( i -- flag )
    {: i | F: x F: y :}
    vr? if  vr-stick  else  pad-stick  then  to y to x
    i case
        0 of  y -0.55e f<  endof
        1 of  x 0.55e f>  endof
        2 of  y 0.55e f>  endof
        >r  x -0.55e f<  r>
    endcase ;
: any-button? ( -- flag )   \ (any but Start and Select pressed this tick)
    false  select-button 1+ 0 do  i start-button <> if  i button-pressed? or  then  loop ;
\ the arrows as two keys each: 0 up, 1 right, 2 down, 3 left (the arrow keys, or W A S D)
: arrow-keys ( i -- k1 k2 )
    case  0 of  key: Up key: W  endof  1 of  key: Right key: D  endof  2 of  key: Down key: S  endof
          >r key: Left key: A r>  endcase ;

\ ---- the sticks ----
\ the left stick: W A S D (and a gamepad's left stick as far as it is pushed), x right, z down
: stick@ ( F: -- x z )
    {: | F: vx F: vy F: x F: z :}
    vr? if
        vr-stick to vy to vx
        vx f0= vy f0= and 0= if  vx vy exit  then
    then
    key: D key-held? if  1e to x  then  key: A key-held? if  x 1e f- to x  then
    key: S key-held? if  1e to z  then  key: W key-held? if  z 1e f- to z  then
    pad-stick  fover f0= fdup f0= and if  fdrop fdrop x z exit  then ;
\ the right stick's gesture this frame (Gesture_Update): I up, K down, L right, J left, F as R3
\ (0 up, 1 down, 2 R3, 3 right, 4 left; -1 none - messages.fs stick-gesture); one a press, as
\ the stick's are one a push (gesture_step) - before 0.2.42 L held was right every frame, a praise
\ repeated each time she was free again. In a headset the turn stick gives 0 / 1 / 2 itself
\ (vr-gesture: a quick straight flick up or down, or its click), and never its right or left.
: vr-gesture@ ( -- g )  vr? if  vr-gesture  else  -1  then ;
: gesture@ ( -- g )
    vr-gesture@ dup -1 <> if  exit  then  drop
    key: I pressed? if  0 exit  then  key: K pressed? if  1 exit  then
    key: F pressed? if  2 exit  then  key: L pressed? if  3 exit  then
    key: J pressed? if  4 exit  then  -1 ;
\ the face buttons and the shoulders pressed this frame (circle .. cross: the pad's bits 10..15)
: buttons-pressed ( -- n )  0  start-button 0 do  i button-pressed? if  1+  then  loop ;

