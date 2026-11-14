\ panic-menu.fs - the panic effects from the keyboard (P), drawn as the settings menu (F1) is: the
\ rows of the VR options page's panic page, the same labels and values, in the same dark panel -
\ arrows move, right / Return changes a row, left goes back, P closes. Both menus write the same
\ saved values (vr-options.fs), so a row changed here is what the page shows, and the other way
\ round. It does not hold the game: as F1, the world plays on behind it.
IN: panic-menu
USING: engine vr-options ;

variable panic-open   0 panic-open !
variable panic-at     0 panic-at !
: #panic-rows ( -- n )  7 ;
: panic-label ( i -- addr len )
    case
        0 of  s" Grey/darkening tint"  endof
        1 of  s" Heartbeat smear (photosensitivity warning)"  endof
        2 of  s" Zoom blur (photosensitivity warning)"  endof
        3 of  s" Negative flash (photosensitivity warning)"  endof
        4 of  s" Camera shake"  endof
        5 of  s" Panic stumbles"  endof
        6 of  s" Heartbeat haptics"  endof
        >r s" Camera shake" r>  endcase ;
: panic-value ( i -- addr len )
    case
        0 of  vr-settings vr.panic-tint l@ 0<> if  s" On"  else  s" Off"  then  endof
        1 of  vr-settings vr.panic-smear l@ 0<> if  s" On"  else  s" Off"  then  endof
        2 of  vr-settings vr.panic-zoom l@ 0<> if  s" On"  else  s" Off"  then  endof
        3 of  vr-settings vr.panic-negative l@ 0<> if  s" On"  else  s" Off"  then  endof
        4 of  vr-settings vr.panic-shake l@ 0<> if  s" On"  else  s" Off"  then  endof
        5 of  vr-settings vr.panic-stumbles l@ 0<> if  s" On"  else  s" Off"  then  endof
        6 of  panic-haptics case 0 of s" Off" endof 1 of s" Half" endof >r s" Full" r> endcase  endof
        >r s" Off" r>  endcase ;
\ a change (d -1 or 1): a flag turns over whatever the way, the haptics step Off, Half, Full
: panic-change ( d i -- )
    {: d i :}
    i case
        0 of  vr-settings vr.panic-tint l@ 0= 1 and vr-settings vr.panic-tint l!  endof
        1 of  vr-settings vr.panic-smear l@ 0= 1 and vr-settings vr.panic-smear l!  endof
        2 of  vr-settings vr.panic-zoom l@ 0= 1 and vr-settings vr.panic-zoom l!  endof
        3 of  vr-settings vr.panic-negative l@ 0= 1 and vr-settings vr.panic-negative l!  endof
        4 of  vr-settings vr.panic-shake l@ 0= 1 and vr-settings vr.panic-shake l!  endof
        5 of  vr-settings vr.panic-stumbles l@ 0= 1 and vr-settings vr.panic-stumbles l!  endof
        6 of  vr-settings vr.panic-haptics l@ d + 3 + 3 mod vr-settings vr.panic-haptics l!  endof
    endcase
    save-vr-options ;
: panic-keys ( -- )
    panic-open @ 0= if  exit  then
    key: Down key-pressed? if  panic-at @ 1+ #panic-rows mod panic-at !  then
    key: Up key-pressed? if  panic-at @ 1- #panic-rows + #panic-rows mod panic-at !  then
    key: Right key-pressed? key: Return key-pressed? or if  1 panic-at @ panic-change  then
    key: Left key-pressed? if  -1 panic-at @ panic-change  then ;
: p-key ( -- )  key: P key-pressed? if  panic-open @ 0= panic-open !  then ;
variable row-h  variable col-w
\ the F1 menu's own panel and words (graphics.fs draw-menu), a wider value column for the long
\ warning labels
: draw-panic ( -- )
    panic-open @ 0= if  exit  then
    screen-size nip 900 >= if 3 else 2 then pen-scale
    char-size row-h ! col-w !
    $0A0C10D8 pen-color  16 16  col-w @ 52 *  #panic-rows 3 + row-h @ * 16 +  draw-rect
    $FFE070FF pen-color  s" Panic Effects (PHOTOSENSITIVITY WARNING)" 28 24 draw-text
    #panic-rows 0 do
        i panic-at @ = if  $FFE070FF  else  $C8D0D8FF  then pen-color
        i panic-label  28  i 2 + row-h @ * 16 +  draw-text
        i panic-value  28 col-w @ 45 * +  i 2 + row-h @ * 16 +  draw-text
    loop
    $C8D0D8FF pen-color  s" arrows: choose   left / right: change   P: close"  28  #panic-rows 2 + row-h @ * 16 +  draw-text ;

' p-key on-tick
' panic-keys on-tick
' draw-panic on-draw