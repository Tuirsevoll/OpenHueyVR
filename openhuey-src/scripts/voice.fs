\ voice.fs - a phrase from the in-process recogniser. Fiona sends Hewie the command.
\ She does not stop or play the gesture. The transcript is drawn by the engine onto the world panel.
\ Home hides it. F8 is the open mic.
IN: voice
USING: engine actors messages keys ;

state: voice-state
  cell field seen
end-state

: keys ( -- )
    key: F8 pressed? if  voice-open-mic? 0= voice-open-mic  then
    key: Home pressed? if  voice-debug? 0= voice-debug  then ;
: on-phrase ( -- )
    voice-gen seen @ = if  exit  then
    voice-gen seen !
    voice-heard fiona-id send voice-order ;
behaviour voice-hearing
  on spawned ( -- )  0 seen !  self subscribe tick ;
  on tick ( -- )  keys  on-phrase ;
end-behaviour

: voice-spawn ( -- id )  voice-hearing voice-state s" voice" spawn ;
