\ voice.fs - a phrase from the in-process recogniser. Fiona sends Hewie the command
\ (voice-order); she does not stop or play the gesture. Home hides both boxes. F8 is the open mic.
IN: voice
USING: engine actors messages keys hewie-names ;

13 constant voice-layer
90 constant voice-shown
$0A0A12D8 constant voice-fill
$8C7A5AC0 constant voice-edge
$F0EBE0FF constant voice-ink

state: voice-state
  cell field seen
  cell field box-for
  cell field act-for
  cell field doing
end-state

: action-name ( id -- addr len )
    dup heard-her-act = if  drop s" bark" exit  then
    dup sit-then-stay-act = if  drop s" sit" exit  then
    dup stay-act = if  drop s" stay" exit  then
    dup stand-then-stay-act = if  drop s" stay" exit  then
    dup come-to-her-act = if  drop s" come" exit  then
    dup come-back-act = if  drop s" come" exit  then
    dup hurry-to-her-act = if  drop s" come" exit  then
    dup watch-act = if  drop s" watch" exit  then
    dup go-there-act = if  drop s" go" exit  then
    dup fetch-act = if  drop s" fetch" exit  then
    dup wont-act = if  drop s" won't" exit  then
    dup scolded-afar-act = if  drop s" scold" exit  then
    dup scolded-close-act = if  drop s" scold" exit  then
    dup sit-trick-act = if  drop s" shake" exit  then
    dup sit-trick-2-act = if  drop s" shake" exit  then
    dup sit-for-her-act = if  drop s" sit" exit  then
    drop s" hewie" ;
: keys ( -- )
    key: F8 pressed? if  voice-open-mic? 0= voice-open-mic  then
    key: Home pressed? if  voice-debug? 0= voice-debug  then ;
: on-phrase ( -- )
    voice-gen seen @ = if  exit  then
    voice-gen seen !
    voice-debug? if  voice-text nip if  voice-shown box-for !  screen-face  then  then
    voice-heard dup dup voice-come >= swap voice-shake <= and if  voice-shown act-for !  then
    fiona-id send voice-order ;
: fill-box ( x y w h -- )
    {: x y w h :}
    voice-layer x y w h voice-fill ui-rect
    voice-layer x y w 1 voice-edge ui-rect
    voice-layer x y h + 1- w 1 voice-edge ui-rect
    voice-layer x y 1 h voice-edge ui-rect
    voice-layer x w + 1- y 1 h voice-edge ui-rect ;
: draw-line ( addr len y -- )
    {: a n y | w h sc tw bw bh x :}
    screen-size to h to w
    h 224 / 1 max to sc
    a n sc text-width to tw
    tw sc 16 * + to bw
    sc 22 * to bh
    w bw - 2/ to x
    x y bw bh fill-box
    voice-layer a n  x sc 8 * +  y sc 4 * +  voice-ink sc 0 ui-text-in ;
: draw-box ( -- )
    voice-debug? 0= if  voice-layer ui-clear  exit  then
    box-for @ 0> act-for @ 0> or 0= if  voice-layer ui-clear  exit  then
    voice-layer ui-clear
    box-for @ 0> if
        voice-text  screen-size drop 16 /  draw-line
        -1 box-for +!
    then
    act-for @ 0> doing @ -1 <> and if
        doing @ action-name  screen-size nip dup 16 /  swap 8 / +  draw-line
        -1 act-for +!
    then ;
behaviour hearing
  on spawned ( -- )
      0 seen !  0 box-for !  0 act-for !  -1 doing !
      self subscribe tick  self subscribe frame-end  self subscribe hewie-doing ;
  on tick ( -- )  keys  on-phrase ;
  on hewie-doing ( here action mode sub cond mood group commandable near reachable breakable way trust -- )
      drop drop drop drop drop drop drop drop drop drop drop  doing !  drop ;
  on frame-end ( -- )  draw-box ;
end-behaviour

: voice-spawn ( -- id )  hearing voice-state s" voice" spawn ;
