\ subscreen/vr.fs - the VR options page (docs/subsystems/comfort.md): the comfort and accessibility
\ choices C keeps (vr.h VrOptions) - the turning, the vignette, the locomotion, which hand does what,
\ her height, the microphone and its subtitles - and the panic's effects: the preset and the seven
\ settings below it. The main page gathers them into sub menus - Comfort Options, Hewie,
\ VR Enhancements and Panic Effects - so the first page stays short; the triangle goes back a page,
\ and a row is chosen with the stick and changed (or opened) with left / right or the cross. hga's own
\ page, in the sub screen's pages after the first list (one R1 from the menu); the title's VR OPTIONS
\ opens it too. Written as it changes (vr-options.fs), read back at start-up.
IN: subscreen.vr
USING: engine ps2-screen subscreen.state subscreen.art subscreen.input vr-options ;

\ the items a row can be (which page shows it is below)
enum: vr-row  vr-turn vr-hand vr-comfort vr-teleport vr-room-load vr-cal vr-seated vr-whisper vr-whisper-debug vr-audio3d vr-hide-door vr-item-prop-debug vr-fp-head vr-cam-back vr-cam-up
              vr-comfort-open vr-hewie-open vr-enhance-open vr-panic-open vr-panic-preset
              vr-panic-tint vr-panic-smear vr-panic-zoom vr-panic-negative vr-panic-shake vr-panic-stumbles vr-panic-haptics ;

\ the VR page's own sub pages: the main page and the four sub menus it opens
enum: vr-page-id  vp-main vp-comfort vp-hewie vp-enhance vp-panic ;

variable vr-page  vp-main vr-page !
variable vr-at    0 vr-at !
variable vr-return-at  0 vr-return-at !

\ the rows each page shows
create main-rows     vr-cal , vr-hand , vr-comfort-open , vr-hewie-open , vr-enhance-open , vr-panic-open ,
create comfort-rows  vr-turn , vr-comfort , vr-teleport , vr-seated ,
create hewie-rows    vr-whisper , vr-whisper-debug ,
create enhance-rows  vr-audio3d , vr-hide-door , vr-item-prop-debug , vr-fp-head , vr-cam-back , vr-cam-up , vr-room-load ,
create panic-rows    vr-panic-preset , vr-panic-tint , vr-panic-smear , vr-panic-zoom ,
                     vr-panic-negative , vr-panic-shake , vr-panic-stumbles , vr-panic-haptics ,
: page-rows ( -- addr )
    vr-page @ case
        vp-comfort of  comfort-rows  endof
        vp-hewie of    hewie-rows    endof
        vp-enhance of  enhance-rows  endof
        vp-panic of    panic-rows    endof
        >r main-rows r>
    endcase ;
: page-row-n ( -- n )
    vr-page @ case
        vp-comfort of  4  endof
        vp-hewie of    2  endof
        vp-enhance of  7  endof
        vp-panic of    8  endof
        >r 6 r>
    endcase ;
: page-row ( i -- item )  cells page-rows + @ ;
\ every opening starts on the main page; a sub menu remembers the row it came from
: vr-page-main ( -- )  vp-main vr-page !  0 vr-at !  0 vr-return-at ! ;
: open-vr-page ( page -- )  {: p :}  vr-at @ vr-return-at !  0 vr-at !  p vr-page !  se-decide ;
: vr-back ( -- )  vr-return-at @ vr-at !  vp-main vr-page !  se-cancel ;

\ the rows that are choices are levels - 0 the first the page offers - read from the options and
\ written back to them: turning 0 smooth / 1 snap 45 / 2 snap 90; the vignette 0 off / 1 low /
\ 2 med / 3 high (a quarter of the corners each, 75 the most); locomotion 0 continuous (the stick
\ walks her) / 1 teleport fixed / 2 teleport dynamic
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
: room-loading-level ( -- n )  vr-settings vr.room-loading l@ 0 max 1 min ;
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
: room-loading-set ( n -- )  0> 1 and  vr-settings vr.room-loading l! ;

: turn$ ( -- addr len )  turn-level case 0 of s" Smooth" endof 1 of s" Snap 45" endof >r s" Snap 90" r> endcase ;
: comfort$ ( -- addr len )  comfort-level case 0 of s" Off" endof 1 of s" Low" endof 2 of s" Med" endof >r s" High" r> endcase ;
: teleport$ ( -- addr len )  teleport-level case 0 of s" Continuous" endof 1 of s" Teleport Fixed" endof >r s" Teleport Dynamic" r> endcase ;
: room-loading$ ( -- addr len )  room-loading-level case 0 of s" Original" endof >r s" Seamless (test)" r> endcase ;
: whisper$ ( -- addr len )  vr-settings vr.whisper-open l@ if  s" Open Mic"  else  s" PTT"  then ;
: subtitles-level ( -- n )  vr-settings vr.whisper-debug l@ 0 max 2 min ;
: subtitles$ ( -- addr len )
    subtitles-level case 0 of s" Off" endof 1 of s" On" endof >r s" Debug" r> endcase ;
: on-off$ ( flag -- addr len )  if  s" On"  else  s" Off"  then ;

\ ---- the panic's effects (panic.fs): the preset and the seven settings it adds up to, one row each
: haptics$ ( -- addr len )
    panic-haptics case 0 of s" Off" endof 1 of s" Half" endof >r s" Full" r> endcase ;

\ its label, its value and the change a step (dir -1 or 1) makes
: vr-row-label ( i -- addr len )
    case  vr-turn of  s" Turning"  endof
          vr-hand of  s" Left-handed Mode"  endof
          vr-comfort of  s" Comfort Vignette"  endof
          vr-teleport of  s" Locomotion"  endof
          vr-cal of  s" Calibrate Height & Reach"  endof
          vr-seated of  s" Seated Mode"  endof
          vr-whisper of  s" Whisper"  endof
          vr-whisper-debug of  s" Whisper Subtitles"  endof
          vr-audio3d of  s" 3D Audio"  endof
          vr-hide-door of  s" Hiding Spot Door Transparency"  endof
          vr-item-prop-debug of  s" Held Item Debug Axes"  endof
          vr-fp-head of  s" Body: Hide Head in First Person"  endof
          vr-cam-back of  s" Body: Camera Back"  endof
          vr-cam-up of  s" Body: Camera Height"  endof
          vr-room-load of  s" Room Loading"  endof
          vr-comfort-open of  s" Comfort Options"  endof
          vr-hewie-open of  s" Hewie"  endof
          vr-enhance-open of  s" VR Enhancements"  endof
          vr-panic-open of  s" Panic Effects"  endof
          vr-panic-preset of  s" Preset"  endof
          vr-panic-tint of  s" Grey/darkening tint"  endof
          vr-panic-smear of  s" Heartbeat smear (photosensitivity warning)"  endof
          vr-panic-zoom of  s" Zoom blur (photosensitivity warning)"  endof
          vr-panic-negative of  s" Negative flash (photosensitivity warning)"  endof
          vr-panic-shake of  s" Camera shake"  endof
          vr-panic-stumbles of  s" Panic stumbles"  endof
          vr-panic-haptics of  s" Heartbeat haptics"  endof
          >r s" Calibrate Height & Reach" r>  endcase ;
: vr-row-value ( i -- addr len )
    case
        vr-turn of  turn$  endof
        vr-hand of  vr-settings vr.left-handed l@ on-off$  endof
        vr-comfort of  comfort$  endof
        vr-teleport of  teleport$  endof
        vr-cal of
            vr-arm-pending? if  s" Arms out..."  else
            vr-settings vr.calibrated l@ if  vr-settings vr.eye-height sf@ 2 f>s$  else  s" Not set"  then  then  endof
        vr-seated of  vr-settings vr.seated l@ on-off$  endof
        vr-whisper of  whisper$  endof
        vr-whisper-debug of  subtitles$  endof
        vr-audio3d of  vr-settings vr.audio3d l@ on-off$  endof
        vr-hide-door of  vr-settings vr.hide-door l@ on-off$  endof
        vr-item-prop-debug of  vr-settings vr.item-prop-debug l@ on-off$  endof
        vr-fp-head of  vr-settings vr.body-fp-head l@ on-off$  endof
        vr-cam-back of  vr-settings vr.cam-back sf@ 2 f>s$  endof
        vr-cam-up of  vr-settings vr.cam-up sf@ 2 f>s$  endof
        vr-room-load of  room-loading$  endof
        vr-comfort-open of  s" Open"  endof
        vr-hewie-open of  s" Open"  endof
        vr-enhance-open of  s" Open"  endof
        vr-panic-open of  s" Open"  endof
        vr-panic-preset of  panic-preset$  endof
        vr-panic-tint of  vr-settings vr.panic-tint l@ on-off$  endof
        vr-panic-smear of  vr-settings vr.panic-smear l@ on-off$  endof
        vr-panic-zoom of  vr-settings vr.panic-zoom l@ on-off$  endof
        vr-panic-negative of  vr-settings vr.panic-negative l@ on-off$  endof
        vr-panic-shake of  vr-settings vr.panic-shake l@ on-off$  endof
        vr-panic-stumbles of  vr-settings vr.panic-stumbles l@ on-off$  endof
        vr-panic-haptics of  haptics$  endof
        >r  vr-settings vr.snap-degrees sf@ f>s n>s  r>
    endcase ;
: vr-row-y ( i -- y )  $1E * $48 + ;
variable vr-step
\ a change: a choice steps on a level (round, the last back to the first), a flag turns over; the
\ preset writes its seven settings over the individual ones; a sub menu row opens its page
: vr-row-change ( dir -- )   ( dir -1 or 1: a level goes back or on; the flags only turn over )
    vr-step !
    vr-at @ page-row case
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
        vr-hide-door of
            vr-settings vr.hide-door l@ 0= 1 and vr-settings vr.hide-door l!
            se-decide  save-vr-options endof
        vr-item-prop-debug of
            vr-settings vr.item-prop-debug l@ 0= 1 and vr-settings vr.item-prop-debug l!
            se-decide  save-vr-options endof
        vr-fp-head of
            vr-settings vr.body-fp-head l@ 0= 1 and vr-settings vr.body-fp-head l!
            se-decide  save-vr-options endof
        vr-cam-back of   \ (the camera's seat in her head: 0..0.12 m, by 0.01)
            vr-settings vr.cam-back sf@  vr-step @ s>f 0.01e f* f+  0e fmax  0.12e fmin
            vr-settings vr.cam-back sf!
            se-decide  save-vr-options endof
        vr-cam-up of   \ (-0.05..+0.05 m, by 0.01; up is the ceiling)
            vr-settings vr.cam-up sf@  vr-step @ s>f 0.01e f* f+  -0.05e fmax  0.05e fmin
            vr-settings vr.cam-up sf!
            se-decide  save-vr-options endof
        vr-room-load of
            room-loading-level vr-step @ + 2 + 2 mod room-loading-set
            se-decide  save-vr-options endof
        vr-comfort-open of  vp-comfort open-vr-page  endof
        vr-hewie-open of    vp-hewie open-vr-page  endof
        vr-enhance-open of  vp-enhance open-vr-page  endof
        vr-panic-open of    vp-panic open-vr-page  endof
        vr-panic-preset of
            vr-step @ panic-preset panic-preset-step panic-preset!
            se-decide  save-vr-options endof
        vr-panic-tint of
            vr-settings vr.panic-tint l@ 0= 1 and vr-settings vr.panic-tint l!
            se-decide  save-vr-options endof
        vr-panic-smear of
            vr-settings vr.panic-smear l@ 0= 1 and vr-settings vr.panic-smear l!
            se-decide  save-vr-options endof
        vr-panic-zoom of
            vr-settings vr.panic-zoom l@ 0= 1 and vr-settings vr.panic-zoom l!
            se-decide  save-vr-options endof
        vr-panic-negative of
            vr-settings vr.panic-negative l@ 0= 1 and vr-settings vr.panic-negative l!
            se-decide  save-vr-options endof
        vr-panic-shake of
            vr-settings vr.panic-shake l@ 0= 1 and vr-settings vr.panic-shake l!
            se-decide  save-vr-options endof
        vr-panic-stumbles of
            vr-settings vr.panic-stumbles l@ 0= 1 and vr-settings vr.panic-stumbles l!
            se-decide  save-vr-options endof
        vr-panic-haptics of
            panic-haptics vr-step @ + 3 + 3 mod vr-settings vr.panic-haptics l!
            se-decide  save-vr-options endof
        >r  vr-settings vr.turn-snap l@ 0= if  1  else  0  then  vr-settings vr.turn-snap l!
            se-decide  save-vr-options
        r>
    endcase ;

: vr-options-keys ( -- )
    up? if  vr-at @ 1- page-row-n wrap vr-at !  se-cursor  then
    down? if  vr-at @ 1+ page-row-n wrap vr-at !  se-cursor  then
    left? if  -1 vr-row-change  then
    right? if  1 vr-row-change  then
    confirm? if  1 vr-row-change  then
    vr-page @ vp-main = if  page-keys  else  back? if  vr-back  then  then ;

: vr-page-title$ ( -- addr len )
    vr-page @ case
        vp-comfort of  s" COMFORT OPTIONS"  endof
        vp-hewie of    s" HEWIE"            endof
        vp-enhance of  s" VR ENHANCEMENTS"  endof
        vp-panic of    s" PANIC EFFECTS"    endof
        >r s" VIRTUAL REALITY" r>
    endcase ;
: vr-options-view ( -- )
    3 page-art
    \ the strip's last tab says VR on this page: the art's own word covered with the paper it sits
    \ on, and VR drawn in its place (a tab of its own would need art the game has not)
    art-layer  by-alpha 0 ui-blend
    art-layer  $176 sx  $11 sy  $3B sw  $1B sy  $736856FF ui-rect
    word-layer  s" VR"  $18C sx  $17 sy  menu-ink  3  glyphs
    vr-page-title$  256 $34 centred
    page-row-n 0 do
        i vr-at @ = if
            art-layer  $30 sx  i vr-row-y 2 - sy  $1C0 sw  $1E sy
            $40 openness @ * 128 /  $40405000 or  ui-rect
        then
        i page-row vr-row-label  $48  i vr-row-y 6 +  i vr-at @ =  text
        i page-row vr-row-value  $1B0  i vr-row-y 6 +  false  text
    loop
    s" Stick: choose   Left / Right: change   Triangle: back"  $40 $1A8 false text ;
