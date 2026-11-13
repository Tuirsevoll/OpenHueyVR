\ subscreen/vr.fs - the VR options page (docs/subsystems/comfort.md): the comfort and accessibility
\ choices C keeps (vr.h VrOptions) - the turning, the vignette, teleporting, which hand does what,
\ and her height - seven rows. hga's own page, in the sub screen's pages after the first list (one
\ R1 from the menu); the title's VR OPTIONS opens it too.
\ A row each: the stick picks, left / right (or the cross) changes it, the triangle - or Select -
\ leaves. Written as it changes (vr-options.fs), read back at start-up.
IN: subscreen.vr
USING: engine ps2-screen subscreen.state subscreen.art subscreen.input vr-options ;

enum: vr-row  vr-turn vr-hand vr-comfort vr-teleport vr-cal vr-seated vr-offset ;
: #vr-rows ( -- n )  vr-offset 1+ ;

variable vr-at   0 vr-at !

\ the rows that are choices are levels - 0 the first the page offers - read from the options and
\ written back to them: turning 0 smooth / 1 snap 45 / 2 snap 90; the vignette 0 off / 1 low /
\ 2 med / 3 high (a quarter of the corners each, 75 the most); teleport 0 off / 1 fixed / 2 dynamic
: turn-level ( -- n )
    vr-settings vr.turn-snap l@ 0= if  0
    else  vr-settings vr.snap-degrees sf@ 75e f> if  2  else  1  then  then ;
: comfort-level ( -- n )
    vr-settings vr.comfort l@ 0= if  0
    else  vr-settings vr.comfort-strength sf@ 0.26e f< if  1
          else  vr-settings vr.comfort-strength sf@ 0.51e f< if  2  else  3  then  then  then ;
: teleport-level ( -- n )
    vr-settings vr.teleport l@ 0= if  0
    else  vr-settings vr.teleport-mode l@ if  2  else  1  then  then ;
: turn-set ( n -- )
    dup 0= if  drop  0 vr-settings vr.turn-snap l!  exit  then
    1 vr-settings vr.turn-snap l!
    1 = if  45e  else  90e  then  vr-settings vr.snap-degrees sf! ;
: comfort-set ( n -- )
    dup 0= if  drop  0 vr-settings vr.comfort l!  exit  then
    1 vr-settings vr.comfort l!
    s>f 0.25e f*  vr-settings vr.comfort-strength sf! ;
: teleport-set ( n -- )
    {: l :}
    l 0> 1 and  vr-settings vr.teleport l!
    l 2 = 1 and  vr-settings vr.teleport-mode l! ;

: turn$ ( -- addr len )  turn-level case 0 of s" Smooth" endof 1 of s" Snap 45" endof >r s" Snap 90" r> endcase ;
: comfort$ ( -- addr len )  comfort-level case 0 of s" Off" endof 1 of s" Low" endof 2 of s" Med" endof >r s" High" r> endcase ;
: teleport$ ( -- addr len )  teleport-level case 0 of s" Off" endof 1 of s" Fixed" endof >r s" Dynamic" r> endcase ;
: on-off$ ( flag -- addr len )  if  s" On"  else  s" Off"  then ;

\ its label, its value and the change a step (dir -1 or 1) makes
: vr-row-label ( i -- addr len )
    case  vr-turn of  s" Turning"  endof
          vr-hand of  s" Left-handed"  endof
          vr-comfort of  s" Comfort vignette"  endof
          vr-teleport of  s" Teleport movement"  endof
          vr-cal of  s" Calibrate height"  endof
          vr-seated of  s" Seated mode"  endof
          vr-offset of  s" View height offset"  endof
          >r s" Calibrate height" r>  endcase ;
: vr-row-value ( i -- addr len )
    case
        vr-turn of  turn$  endof
        vr-hand of  vr-settings vr.left-handed l@ on-off$  endof
        vr-comfort of  comfort$  endof
        vr-teleport of  teleport$  endof
        vr-cal of  vr-settings vr.calibrated l@ if  vr-settings vr.eye-height sf@ 2 f>s$  else  s" Not set"  then  endof
        vr-seated of  vr-settings vr.seated l@ on-off$  endof
        vr-offset of  vr-settings vr.seated-offset sf@ 2 f>s$  endof
        >r  vr-settings vr.snap-degrees sf@ f>s n>s  r>
    endcase ;
: vr-row-y ( i -- y )  $20 * $50 + ;
fvariable vr-was
variable vr-step
\ a change: a choice steps on a level (round, the last back to the first), a flag turns over, a
\ number steps (the view's offset by 5 cm)
: vr-row-change ( dir -- )   ( dir -1 or 1: a level goes back or on; the flags only turn over )
    vr-step !
    vr-at @ case
        vr-turn of
            turn-level vr-step @ + 3 + 3 mod turn-set
            se-decide  save-vr-options endof
        vr-hand of
            vr-settings vr.left-handed l@ 0= 1 and vr-settings vr.left-handed l!
            se-decide  save-vr-options endof
        vr-comfort of
            comfort-level vr-step @ + 4 + 4 mod comfort-set
            vr-preview   \ (the ring at the strength chosen, so it can be seen here)
            se-decide  save-vr-options endof
        vr-teleport of
            teleport-level vr-step @ + 3 + 3 mod teleport-set
            se-decide  save-vr-options endof
        vr-cal of
            vr-tracking? if
                vr-calibrate  vr-settings vr.calibrated l@ if  se-decide  save-vr-options  then
            else  se-buzzer  then endof
        vr-seated of
            vr-settings vr.seated l@ 0= 1 and vr-settings vr.seated l!
            se-decide  save-vr-options endof
        vr-offset of
            vr-settings vr.seated-offset sf@ vr-was f!
            vr-was f@ vr-step @ s>f 0.05e f* f+  -0.3e fmax  0.3e fmin
            vr-settings vr.seated-offset sf!
            vr-settings vr.seated-offset sf@ vr-was f@ f= 0= if  se-cursor  save-vr-options  then endof
        >r  vr-settings vr.turn-snap l@ 0= if  1  else  0  then  vr-settings vr.turn-snap l!
            se-decide  save-vr-options
        r>
    endcase ;

: vr-options-keys ( -- )
    up? if  vr-at @ 1- #vr-rows wrap vr-at !  se-cursor  then
    down? if  vr-at @ 1+ #vr-rows wrap vr-at !  se-cursor  then
    left? if  -1 vr-row-change  then
    right? if  1 vr-row-change  then
    confirm? if  1 vr-row-change  then
    page-keys ;

: vr-options-view ( -- )
    3 page-art
    \ the strip's last tab says VR on this page: the art's own word covered with the paper it sits
    \ on, and VR drawn in its place (a tab of its own would need art the game has not)
    art-layer  by-alpha 0 ui-blend
    art-layer  $176 sx  $11 sy  $3B sw  $1B sy  $736856FF ui-rect
    word-layer  s" VR"  $18C sx  $17 sy  menu-ink  3  glyphs
    s" VIRTUAL REALITY"  256 $34 centred
    #vr-rows 0 do
        i vr-at @ = if
            art-layer  $30 sx  i vr-row-y 2 - sy  $1C0 sw  $20 sy
            $40 openness @ * 128 /  $40405000 or  ui-rect
        then
        i vr-row-label  $48  i vr-row-y 6 +  i vr-at @ =  text
        i vr-row-value  $1B0  i vr-row-y 6 +  false  text
    loop
    s" Stick: choose   Left / Right: change   Triangle: back"  $40 $18C false text ;
