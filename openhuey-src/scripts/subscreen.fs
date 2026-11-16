\ subscreen.fs - the sub screen, the in-game menu (docs/subsystems/items.md; src/game/subscreen.c).
\ Select opens it while Fiona is free (SceneGame_SubPlay): the world is held (`pause!`) and only
\ this actor has frames (`held-tick`). Its pages - the item lists, the map, the files, the
\ options - and the scripts' machines (the word plate, the slot machine) are in subscreen/; this
\ file opens and closes it, gives the keys to what is showing and draws it. Also the tab that
\ slides in when the story hands her an item (the scripts' 0xBE, SubScreen_DrawTab).
IN: subscreen
USING: engine game-state actors messages keys facts flag-names items items.tables ps2-screen subscreen.state subscreen.art subscreen.input subscreen.box subscreen.lists subscreen.map subscreen.options subscreen.vr subscreen.plate subscreen.slots subscreen.saving subscreen.costumes subscreen.extras subscreen.galleries ;

<PRIVATE
\ ---- which art is in the background group: the menu's, the plate's, the slot machine's ----
: background ( n file len -- )  back-group load-bank  back-bank ! ;
: menu-art ( -- )
    art-loaded @ 0= if  s" SUBSCR/SUBBASE" base-group load-bank  -1 art-loaded !  then
    back-bank @ if  0 s" SUBSCR/SUBBACK" background  then ;

\ ---- opening and closing ----
: may-open? ( -- flag )   \ Select, she free, the panic below 4, no scene, nothing held
    select-button button-pressed?
    subscreen-locked state-flag? 0= and  world-held state-flag? 0= and
    her-mode @ 0= and  the-panic @ 4 < and  cutscene-active? 0= and   \ (her mode only: out of breath, pushing - it opens)
    her-reach @ 24 and 0= and ;   \ (not a script's, not held: Fiona +0xE0, +0x14E8 5 - her plight's reach bits 8 / 16)
: opened ( -- )
    menu-art  opening stage !  0 openness !  back-to-page  0 quiet !  0 opt-editing !  back-to-her  frames @ opened-at !  0 tab !  1 pause!
    vr-page-main
    screen-face ;   \ (the panel is put where the player is looking now, and left there: a menu is
                    \ in front of them when it opens, and stays in the room after)
: open-menu ( -- )  opened  -1 the-menu !  se-open ;
\ one of the other screens: Select isn't theirs; its sound as it opens only the word plate's, the slot
\ machine's and the options' (SubScreen_Open: modes 0, 2..5 - the save point, the wardrobe, the
\ mini-game's and the galleries' none)
: open-screen ( sound? -- )  opened  0 the-menu !  if  se-open  then ;
: open-plate ( -- )  true open-screen  1 s" SUBSCR/PLATE" background  on-plate mode !  start-plate ;
: open-slots ( -- )  true open-screen  2 s" SUBSCR/SYN_SLOT" background  on-slots mode !  start-slots ;
: open-saving ( -- )  false open-screen  on-saving mode !  start-saving ;
: open-costumes ( -- )  false open-screen  on-costumes mode !  start-costumes ;
: open-options ( -- )  true open-screen  pg-options page !  -1 options-only ! ;   \ (the title's: SceneTitle_SeqOptions)
: open-vr-options ( -- )  true open-screen  pg-vr page !  -1 options-only ! ;   \ (the title's VR OPTIONS: hga's own page)
: open-extras ( -- )  false open-screen  3 s" SUBSCR/SUBMG" background  on-extras mode !  start-extras ;   \ (the mini-game's menu: mode $A)
: open-clear-save ( -- )  false open-screen  on-clear-save mode !  start-clear-save ;   \ (the ending's: mode 9)
: open-results ( -- )  false open-screen  on-results mode !  start-results ;   \ (its results: mode $C)
: open-gallery ( kind -- )   \ (the extras room's galleries: modes 8, $B, $D, $E, $F)
    false open-screen  4 over gallery-file background  start-gallery ;
: open-played ( -- )  hewie-played? if  open-options  else  open-menu  then ;   \ (in the run, the options alone)
: asked-open ( -- )   \ (the scripts' 0x6D: its mode; the rest - not built - shut at once)
    opened-as @ case
        0 of  open-played  endof
        1 of  open-saving  endof
        $A of  open-extras  endof
        $C of  open-results  endof
        5 of  open-options  endof
        6 of  open-vr-options  endof
        7 of  open-costumes  endof
        2 of  open-plate  endof
        3 of  open-slots  endof
        8 of  gal-movies open-gallery  endof
        9 of  -1 clear-asker !  open-clear-save  endof
        $B of  gal-models open-gallery  endof
        $D of  gal-music open-gallery  endof
        $E of  gal-art open-gallery  endof
        $F of  gal-types open-gallery  endof
        >r subscreen-wanted state-flag-clear r>
    endcase ;
: shut-menu ( -- )   \ back to play: the world's frames again, the scripts' wait over
    options-only @ if  0 options-only !  pg-special page !  then
    gal-kind @ if  gallery-shut  then
    mode @ on-clear-save = if  on-page mode !  clear-asker @ dup alive? if  send clear-saved  else  drop  then  then
    shut stage !  0 openness !  0 pause!  subscreen-wanted state-flag-clear  art-layer ui-clear  word-layer ui-clear ;

\ ---- what is showing: its keys and its drawing ----
: menu-keys ( -- )
    read-arrows
    mode @ case
        on-actions of  action-keys  endof
        on-box of  box-keys  endof
        on-plate of  plate-keys-read  endof
        on-slots of  slots-keys  endof
        on-saving of  saving-keys  endof
        on-costumes of  costumes-keys  endof
        on-extras of  extras-keys  endof
        on-results of  results-keys  endof
        on-clear-save of  clear-keys  endof
        on-gallery of  gallery-keys  endof
        on-page of
            page @ case  pg-files of  files-keys  endof  pg-map of  map-keys  endof
                         pg-vr of  vr-options-keys  endof
                         pg-options of  options-keys  endof  >r list-keys r>  endcase  endof
    endcase ;
\ the page's own paper, on its own layer under the art (subscreen/art.fs: the original composed the
\ menu from pieces that mix with what is under them; on a headset's panel that was the room, so
\ every word and edge showed it through). Faded in with the page, and opaque once it is up.
: page-ground ( -- )
    openness @ 0> if
        9  0 0 screen-size  $72655200  openness @ 2* 255 min  or  ui-rect
    then ;
\ the machines' pages are the whole screen (nothing of the world behind): beyond the 4:3 frame
\ on a wider window, black - faded in with the page (subscreen/galleries.fs `page-black`)
: draw-page ( -- )
    gal-kind @ if  gallery-view  mode @ on-box = if  box-view  then  exit  then
    mode @ on-plate = if  page-black  plate-view exit  then
    mode @ on-slots = if  page-black  slots-view exit  then
    mode @ on-saving = if  saving-view exit  then
    mode @ on-costumes = if  costumes-view exit  then
    mode @ on-extras = if  page-black  extras-view exit  then
    mode @ on-results = if  page-black  results-view exit  then
    mode @ on-clear-save = if  clear-view exit  then
    mode @ on-box = if  art-layer page-words !  then   \ (the page under the box's shade)
    page @ case  pg-files of  files-view  endof  pg-map of  map-view  endof
                 pg-vr of  vr-options-view  endof
                 pg-options of  options-view  endof  >r list-view r>  endcase
    word-layer page-words !
    mode @ on-actions = if  actions-view  then
    mode @ on-box = if  box-view  then ;
: draw-tab ( -- )   \ the tab and the item's picture, sliding in from the left (fading with it)
    tab-slide @ {: s :}
    tab @ case
        2 of  $AC $40 - s +  $60 $C s 2* part   $C0 $40 - s +  $8C $B s part  endof
        3 of  $AC $60 $C $80 part   $C0 $8C $B $40 part  endof
        4 of  $EC s -  $60 $C s 2* part   $100 s -  $8C $B s part  endof
    endcase ;
: draw ( -- )
    frame!  art-layer ui-clear  word-layer ui-clear

    \ layer 3 is the options page's own (its "Page Up: set height", options.fs): cleared here, every
    \ frame, so a line it drew cannot outlive the page - nothing else draws on it
    3 ui-clear
    \ the page's own paper, on its own layer under the art: the menu is composed as the PS2 composed
    \ it (its words and pieces mix with what is under them), and on a headset's panel what was under
    \ them was the room - every word and every edge showed it through. A paper ground makes the panel
    \ a page again, and the panel is opaque.
    9 ui-clear
    stage @ shut <> if  page-ground  draw-page
    else  tab @ if  $80 openness !  draw-tab  0 openness !  then  then
    \ ... and the panel's own alpha over all of it (panel-opaque): the PS2 composed this from
    \ pieces blended by its ALPHA register - arithmetic on a screen whose alpha nobody read. On a
    \ panel that alpha is what holds the room out, so the words, the icons and the highlight were
    \ holes. The colour stays exactly as drawn; only the alpha is put back, faded in with the page.
    openness @ 0> if  openness @ s>f 128e f/ panel-opaque  then ;

\ ---- the sound turned down as it opens, back up as it closes (sub_set_volume: by the fade -
\ 1 - fade / 255 - the effects' master, the stream, the stage music) ----
: sub-volume ( -- )
    255 openness @ - s>f 255e f/  {: F: v :}
    1e v sound-volume!  v f>cell music-id send music-duck  v stage-music-volume! ;

\ ---- a frame with the world held ----
: held-frame ( -- )
    vr-arm-save? if  vr-options:save-vr-options  then   \ (a body calibration's reach, measured while the menu is up)
    1 frames +!  stage @ shut <> if  broadcast menu-frame  then
    stage @ case
        opening of  16 openness +!  openness @ 128 >= if  128 openness !  up stage !  then  sub-volume  endof
        up of  menu-keys  select-button button-pressed? the-menu @ and if  close-menu  then  endof   \ (the menu's alone: SubScreen_StateRun)
        closing of
            openness @ 128 = quiet @ 0= and if  se-close  then
            -32 openness +!  openness @ 0 max openness !  sub-volume  openness @ 0> 0= if  shut-menu exit  then  endof
    endcase
    draw ;

\ ---- the tab: told who asked once it is in or out (the place may have gone meanwhile) ----
: tell-asker ( -- )  tab-asker @ alive? if  tab-asker @ send tab-done  then ;
: tab-frame ( -- )
    tab @ 2 = if  tab-slide @ $3D < if  4 tab-slide +!  else  3 tab !  tell-asker  then  then
    tab @ 4 = if  tab-slide @ 4 >= if  -4 tab-slide +!  else  0 tab !  tell-asker  then  then ;
: tab-item ( id -- )   \ its picture, and the menu's art for the tab
    -1 shown-picture !  menu-art  item-picture dup if  picture-group load-bank  else  2drop  then ;
PRIVATE>

behaviour menu
  on spawned ( -- )
      shut stage !  pg-special page !  -1 shown !  -1 shown-picture !  0 tab !  -1 stalker-room !  defaults
      1 back-bank !  -1 her-map !  -1 her-pg !  -1 her-room !  -1 map-loaded !
      self subscribe tick  self subscribe held-tick  self subscribe frame-end  self subscribe arrived
      self subscribe fiona-doing  self subscribe fiona-plight  self subscribe danger  self subscribe panic  self subscribe stalker-doing  self subscribe stalker-ties  self subscribe write-save ;
  on tick ( -- )
      1 frames +!  tab-frame
      stage @ shut = if
          may-open? if  open-played exit  then
          subscreen-wanted state-flag? if  asked-open  then
      then ;
  on held-tick ( -- )  held-frame ;
  on frame-end ( -- )  draw ;
  on menu-mode ( mode -- )  opened-as ! ;
  on game-saved ( -- )  saved-now ;
  on save-clear ( -- )  sender clear-asker !  open-clear-save ;
  on arrived ( room exit -- )  drop find-room ;        \ (Map_FindRoom as each room comes in)
  on map-turn-to ( page -- )  show-on-page ;            \ (the scripts' 0xD1; a save loaded: SceneGame_RoomIn)
  on write-save ( -- )  her-pg @ .  ." map-page-was" cr ;   \ (saves.fs) the map's page she is on (the save's +0x18F0)
  on options-restore ( vib out vol x y layout -- )  subscreen.options:restore ;   \ (saves.fs: the system data's)
  on item-used ( id result -- )  mode @ awaiting-use = if  used  else  2drop  then ;
  on fiona-doing ( mode sub cond cmd doing -- )  her-doing !  2drop drop  her-mode ! ;
  on fiona-plight ( state ladder reach -- )  her-reach !  2drop ;
  on danger ( level -- )  the-danger ! ;
  on panic ( stage level -- )  drop the-panic ! ;
  on stalker-doing ( room mode sub cond hp hp-max kind alert came -- )  2drop 2drop 2drop 2drop  stalker-room !  frames @ stalker-told ! ;
  on stalker-ties ( scripted apart linked -- )  2drop stalker-held ! ;
  on tab-op ( op item -- )   \ (the scripts' 0xBE: 0 the item's, 1 its picture in, 2 slide in, 3 shown, 4 slide out)
      sender tab-asker !
      swap case
          0 of  tab-item  0 tab-slide !  tab-asker @ send tab-done  endof
          2 of  drop  screen-face  2 tab !  endof
          4 of  drop  4 tab !  endof
          3 of  drop  3 tab !  $40 tab-slide !  tab-asker @ send tab-done  endof
          >r drop  tab-asker @ send tab-done  r>
      endcase ;
end-behaviour

: subscreen-spawn ( -- id )  menu subscreen-state s" subscreen" spawn ;
