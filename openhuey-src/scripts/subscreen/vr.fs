\ subscreen/vr.fs - the VR options page (docs/subsystems/comfort.md): the comfort and accessibility
\ choices C keeps (vr.h VrOptions) - the turning, the vignette, teleporting, which hand does what,
\ her height, the microphone and its subtitles - and the panic's effects: the preset they add up to
\ and the page that sets each one. hga's own page, in the sub screen's pages after the first list
\ (one R1 from the menu); the title's VR OPTIONS opens it too. A row each: the stick picks, left /
\ right (or the cross) changes it, the triangle - or Select - leaves. Written as it changes
\ (vr-options.fs), read back at start-up.
IN: subscreen.vr
USING: engine ps2-screen subscreen.state subscreen.art subscreen.input vr-options ;

enum: vr-row  vr-turn vr-hand vr-comfort vr-teleport vr-cal vr-seated vr-whisper vr-whisper-debug vr-audio3d vr-panic vr-panic-open ;
: #vr-rows ( -- n )  vr-panic-open 1+ ;

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
: whisper$ ( -- addr len )  vr-settings vr.whisper-open l@ if  s" Open Mic"  else  s" PTT"  then ;
: subtitles-level ( -- n )  vr-settings vr.whisper-debug l@ 0 max 2 min ;
: subtitles$ ( -- addr len )
    subtitles-level case 0 of s" Off" endof 1 of s" On" endof >r s" Debug" r> endcase ;
: on-off$ ( flag -- addr len )  if  s" On"  else  s" Off"  then ;

\ ---- the panic's effects (panic.fs): a row each, and the preset they add up to ----
enum: panic-row  panic-tint-row panic-smear-row panic-zoom-row panic-negative-row panic-shake-row panic-stumbles-row panic-haptics-row ;
: #panic-rows ( -- n )  panic-haptics-row 1+ ;
variable panic-page-at   0 panic-page-at !
: panic-page-row-y ( i -- y )  $20 * $70 + ;
: panic-page-haptics ( -- n )  panic-haptics ;
: panic-page-haptics$ ( -- addr len )
    panic-page-haptics case 0 of s" Off" endof 1 of s" Half" endof >r s" Full" r> endcase ;
\ the preset the set adds up to, as the page's title line says it (live with each change)
: panic-preset-line$ ( -- addr len )  spell-reset  s" PRESET: " +spell  panic-preset$ +spell  spelled ;
: panic-page-label ( i -- addr len )
    case
        panic-tint-row of      s" Grey/darkening tint"  endof
        panic-smear-row of     s" Heartbeat smear (photosensitivity warning)"  endof
        panic-zoom-row of      s" Zoom blur (photosensitivity warning)"  endof
        panic-negative-row of  s" Negative flash (photosensitivity warning)"  endof
        panic-shake-row of     s" Camera shake"  endof
        panic-stumbles-row of  s" Panic stumbles"  endof
        panic-haptics-row of   s" Heartbeat haptics"  endof
        >r s" Camera shake" r>  endcase ;
: panic-page-value ( i -- addr len )
    case
        panic-tint-row of      vr-settings vr.panic-tint l@ on-off$  endof
        panic-smear-row of     vr-settings vr.panic-smear l@ on-off$  endof
        panic-zoom-row of      vr-settings vr.panic-zoom l@ on-off$  endof
        panic-negative-row of  vr-settings vr.panic-negative l@ on-off$  endof
        panic-shake-row of     vr-settings vr.panic-shake l@ on-off$  endof
        panic-stumbles-row of  vr-settings vr.panic-stumbles l@ on-off$  endof
        panic-haptics-row of   panic-page-haptics$  endof
        >r vr-settings vr.panic-tint l@ on-off$ r>  endcase ;
\ a change (dir -1 or 1): a flag turns over, the haptics step Off, Half, Full; the preset the set
\ is no longer one of is Custom, which has no row here - the preset row above says so
: panic-page-change ( dir -- )
    {: d :}
    panic-page-at @ case
        panic-tint-row of      vr-settings vr.panic-tint l@ 0= 1 and vr-settings vr.panic-tint l!  endof
        panic-smear-row of     vr-settings vr.panic-smear l@ 0= 1 and vr-settings vr.panic-smear l!  endof
        panic-zoom-row of      vr-settings vr.panic-zoom l@ 0= 1 and vr-settings vr.panic-zoom l!  endof
        panic-negative-row of  vr-settings vr.panic-negative l@ 0= 1 and vr-settings vr.panic-negative l!  endof
        panic-shake-row of     vr-settings vr.panic-shake l@ 0= 1 and vr-settings vr.panic-shake l!  endof
        panic-stumbles-row of  vr-settings vr.panic-stumbles l@ 0= 1 and vr-settings vr.panic-stumbles l!  endof
        panic-haptics-row of   panic-page-haptics d + 3 + 3 mod vr-settings vr.panic-haptics l!  endof
    endcase
    se-decide  save-vr-options ;
: panic-page-keys ( -- )
    up? if  panic-page-at @ 1- #panic-rows wrap panic-page-at !  se-cursor  then
    down? if  panic-page-at @ 1+ #panic-rows wrap panic-page-at !  se-cursor  then
    left? if  -1 panic-page-change  then
    right? if  1 panic-page-change  then
    confirm? if  1 panic-page-change  then
    back? if  back-to-page  se-cancel  then ;
\ the status screen's own look (options.fs' paper and rows), one row each; the triangle is back to
\ the VR options page it came from
: panic-page-view ( -- )
    3 page-art
    word-layer page-words !
    s" PANIC EFFECTS" 256 $34 centred
    s" PHOTOSENSITIVITY WARNING" 256 $46 centred
    panic-preset-line$ 256 $58 centred
    #panic-rows 0 do
        i panic-page-at @ = if
            art-layer  $30 sx  i panic-page-row-y 2 - sy  $1C0 sw  $20 sy
            $40 openness @ * 128 /  $40405000 or  ui-rect
        then
        i panic-page-label  $48  i panic-page-row-y 6 +  i panic-page-at @ =  text
        i panic-page-value  $1B0  i panic-page-row-y 6 +  false  text
    loop
    s" Up / Down: choose   Left / Right: change   Triangle: back"  $40 $1B4 false text ;

\ its label, its value and the change a step (dir -1 or 1) makes
: vr-row-label ( i -- addr len )
    case  vr-turn of  s" Turning"  endof
          vr-hand of  s" Left-handed"  endof
          vr-comfort of  s" Comfort vignette"  endof
          vr-teleport of  s" Teleport movement"  endof
          vr-cal of  s" Calibrate height"  endof
          vr-seated of  s" Seated mode"  endof
          vr-whisper of  s" Whisper"  endof
          vr-whisper-debug of  s" Whisper Subtitles"  endof
          vr-audio3d of  s" 3D Audio"  endof
          vr-panic of  s" Panic Effects (PHOTOSENSITIVITY WARNING)"  endof
          vr-panic-open of  s" Panic effects"  endof
          >r s" Calibrate height" r>  endcase ;
: vr-row-value ( i -- addr len )
    case
        vr-turn of  turn$  endof
        vr-hand of  vr-settings vr.left-handed l@ on-off$  endof
        vr-comfort of  comfort$  endof
        vr-teleport of  teleport$  endof
        vr-cal of  vr-settings vr.calibrated l@ if  vr-settings vr.eye-height sf@ 2 f>s$  else  s" Not set"  then  endof
        vr-seated of  vr-settings vr.seated l@ on-off$  endof
        vr-whisper of  whisper$  endof
        vr-whisper-debug of  subtitles$  endof
        vr-audio3d of  vr-settings vr.audio3d l@ on-off$  endof
        vr-panic of  panic-preset$  endof
        vr-panic-open of  s" Open"  endof
        >r  vr-settings vr.snap-degrees sf@ f>s n>s  r>
    endcase ;
: vr-row-y ( i -- y )  $20 * $48 + ;
variable vr-step
\ a change: a choice steps on a level (round, the last back to the first), a flag turns over; the
\ preset writes its seven settings over the individual ones; the last row opens their page
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
        vr-whisper of
            vr-settings vr.whisper-open l@ 0= 1 and vr-settings vr.whisper-open l!
            se-decide  save-vr-options endof
        vr-whisper-debug of
            subtitles-level vr-step @ + 3 + 3 mod vr-settings vr.whisper-debug l!
            se-decide  save-vr-options endof
        vr-audio3d of
            vr-settings vr.audio3d l@ 0= 1 and vr-settings vr.audio3d l!
            se-decide  save-vr-options endof
        vr-panic of
            vr-step @ panic-preset panic-preset-step panic-preset!
            se-decide  save-vr-options endof
        vr-panic-open of   0 panic-page-at !  on-panic mode !  se-decide endof
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
    s" Stick: choose   Left / Right: change   Triangle: back"  $40 $1A8 false text ;