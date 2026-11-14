\ The VR options and a turn (scripts/vr-options.fs, scripts/fiona/controls.fs): every option as the
\ game was until the player chooses otherwise; and a turn by an angle - her body's yaw, the heading
\ she walks by and the view together - which is what a snap is. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/controls/test_vr_options.fs
IN: test-vr-options
USING: tester engine game-state game actors messages keys facts flag-names subscreen subscreen.state fiona.state fiona.controls vr-options panic-menu opening debug ;

testing the options a fresh game has: smooth turning, a 45 degree snap, the rest as the game was
T{ vr-settings vr.turn-snap l@  vr-settings vr.snap-degrees sf@ f>s -> 0 45 }T
T{ vr-settings vr.comfort l@  vr-settings vr.comfort-strength sf@ 1000e f* fround f>s -> 0 500 }T
T{ vr-settings vr.teleport l@  vr-settings vr.seated l@  vr-settings vr.left-handed l@ -> 0 0 0 }T
T{ vr-settings vr.calibrate-start l@ -> 1 }T
testing the microphone and subtitles: PTT, the subtitles off until the page asks
T{ vr-settings vr.whisper-open l@  vr-settings vr.whisper-debug l@ -> 0 0 }T
testing the headset crouch's defaults: deadzone 13%, on 25%, off 18%, sneak 35%
T{ vr-settings vr.crouch-deadzone sf@ 100e f* fround f>s
   vr-settings vr.crouch-on sf@ 100e f* fround f>s
   vr-settings vr.crouch-off sf@ 100e f* fround f>s
   vr-settings vr.sneak-speed sf@ 100e f* fround f>s -> 13 25 18 45 }T

testing vr-defaults puts them back after a change
1 vr-settings vr.turn-snap l!  90e vr-settings vr.snap-degrees sf!  1 vr-settings vr.teleport l!
1 vr-settings vr.seated l!  1 vr-settings vr.whisper-open l!  1 vr-settings vr.whisper-debug l!  vr-defaults
T{ vr-settings vr.turn-snap l@  vr-settings vr.snap-degrees sf@ f>s  vr-settings vr.teleport l@  vr-settings vr.seated l@ -> 0 45 0 0 }T
T{ vr-settings vr.whisper-open l@  vr-settings vr.whisper-debug l@ -> 0 0 }T

testing a turn by an angle: her yaw and her heading the same, the view asked for the same turn
fvariable yaw-after   fvariable head-after
: turn-by ( F: radians -- )
    game:fiona enter
    0e her-yaw sf!  0e her-heading sf!
    turn-view
    her-yaw sf@ yaw-after f!   her-heading sf@ head-after f!
    leave-actor ;
45e deg>rad turn-by
T{ yaw-after f@ 1000e f* fround f>s  head-after f@ 1000e f* fround f>s -> 785 785 }T
testing ... to the other side, as a snap to the right
-45e deg>rad turn-by
T{ yaw-after f@ 1000e f* fround f>s  head-after f@ 1000e f* fround f>s -> -785 -785 }T

testing no snap waits without a headset (the turn stick reads 0)
T{ vr-snap f>s -> 0 }T

testing the first run asks for a calibration: a headset, nothing kept yet (vr-defaults leaves it)
variable was-vr   vr? was-vr !
-1 vr!
T{ opening:calibrate-wanted? -> -1 }T
1 vr-settings vr.calibrated l!  vr-defaults
T{ opening:calibrate-wanted?  vr-settings vr.calibrated l@ -> 0 1 }T
0 vr-settings vr.calibrated l!
testing ... and not in seated play, whose own height sizes the world
1 vr-settings vr.seated l!
T{ opening:calibrate-wanted? -> 0 }T
0 vr-settings vr.seated l!  was-vr @ vr!
testing nothing to track without a headset
T{ vr-tracking? -> 0 }T

testing a save's load calibrates at once: a headset with no tracked floor measures nothing
-1 vr!
0 vr-settings vr.calibrated l!
T{ vr-calibrate-now -> 0 }T
T{ vr-settings vr.calibrated l@ -> 0 }T
testing ... and seated play is left alone, its own height sizing the world
1 vr-settings vr.seated l!
T{ vr-calibrate-now -> 0 }T
0 vr-settings vr.seated l!  was-vr @ vr!

\ ---- the first run's calibration screen (scripts/opening.fs): it leaves by itself once the result has
\ been said - the bug this guards was the wait that no one counted down, the screen never leaving
testing a measurement's wait: three seconds, then the menu (opening.fs calibrate-frame)
behaviour keeps-quiet
    on tick ( -- ) ;
end-behaviour
keeps-quiet opening-state s" cal-screen" spawn constant cal-screen
deliver   \ (its spawned message taken by the quiet behaviour: nothing done - and no handler to run one later)
variable cal-got
cal-screen enter  opening:on-calibrate opening:stage !  0 opening:cal-t !  opening:calibrate-take
opening:cal-t @ cal-got !  leave-actor
T{ cal-got @ -> 90 }T
testing ... one tick in, still the screen
cal-screen enter  opening:on-calibrate opening:stage !  90 opening:cal-t !  opening:calibrate-frame
opening:stage @ cal-got !  leave-actor
T{ cal-got @ opening:on-calibrate -> 1 1 }T
testing ... its ninety out, the menu
: cal-wait ( n -- )  0 ?do  opening:calibrate-frame  loop ;   \ (the wait, a tick at a time)
cal-screen enter  opening:on-calibrate opening:stage !  90 opening:cal-t !  90 cal-wait
opening:stage @ cal-got !  leave-actor
T{ cal-got @ opening:on-menu -> 0 0 }T
testing ... and the title's own tick carries it on: a measured screen leaves for the menu by itself
cal-screen enter  opening-the-game become  leave-actor   \ (the title's own handler, its screen up)
cal-screen subscribe tick
cal-screen enter  opening:on-calibrate opening:stage !  2 opening:cal-t !  leave-actor
2 run-frames   \ (two of the game's ticks: the wait counted down through the stage's handler)
T{ cal-screen enter  opening:stage @  leave-actor -> 0 }T   \ (on-menu)

cal-screen kill   \ (done with it: the game's own actors are not touched)

\ ---- the page in the sub screen (the title's VR OPTIONS, mode 6) ----
: tap ( key -- )  dup true key-hold  game-tick  false key-hold  game-tick ;
: menu@ ( xt -- x )  game:subscreen enter  execute  leave-actor ;
: stage@ ( -- n )  [: subscreen.state:stage @ ;] menu@ ;
testing the VR options page opens in the sub screen, its first row the turning
6 game:subscreen send menu-mode  subscreen-wanted state-flag-set  deliver  20 run-frames
T{ stage@  [: mode @ ;] menu@  [: page @ ;] menu@  [: options-only @ ;] menu@ -> 2 on-page 6 -1 }T
testing the cross puts the turning at Snap 45; right goes to 90, right again to Smooth
key: X tap
T{ vr-settings vr.turn-snap l@  vr-settings vr.snap-degrees sf@ f>s -> 1 45 }T
key: Right tap
T{ vr-settings vr.snap-degrees sf@ f>s -> 90 }T
key: Right tap
T{ vr-settings vr.turn-snap l@ -> 0 }T
key: Left tap
T{ vr-settings vr.turn-snap l@  vr-settings vr.snap-degrees sf@ f>s -> 1 90 }T
testing down to left-handed: the cross turns the sticks and buttons over
key: Down tap
key: X tap
T{ vr-settings vr.left-handed l@ -> 1 }T
testing down to the vignette: off, low, med, high - a quarter of the corners each, 75 the most
key: Down tap
key: Right tap
T{ vr-settings vr.comfort l@  vr-settings vr.comfort-strength sf@ 100e f* fround f>s -> 1 25 }T
key: Right tap
T{ vr-settings vr.comfort-strength sf@ 100e f* fround f>s -> 50 }T
key: X tap
T{ vr-settings vr.comfort-strength sf@ 100e f* fround f>s -> 75 }T
key: Right tap
T{ vr-settings vr.comfort l@ -> 0 }T
key: Left tap
T{ vr-settings vr.comfort l@  vr-settings vr.comfort-strength sf@ 100e f* fround f>s -> 1 75 }T
testing down to teleport: off, fixed, dynamic
key: Down tap
key: Right tap
T{ vr-settings vr.teleport l@  vr-settings vr.teleport-mode l@ -> 1 0 }T
key: Right tap
T{ vr-settings vr.teleport l@  vr-settings vr.teleport-mode l@ -> 1 1 }T
key: Right tap
T{ vr-settings vr.teleport l@ -> 0 }T
key: Left tap
T{ vr-settings vr.teleport l@  vr-settings vr.teleport-mode l@ -> 1 1 }T
testing down to the calibration: without a headset it measures nothing
key: Down tap
key: X tap
T{ vr-settings vr.calibrated l@ -> 0 }T
testing seated play: the cross turns it on
key: Down tap
key: X tap
T{ vr-settings vr.seated l@ -> 1 }T
testing the whisper rows below it: PTT to open mic, then the subtitles off, on and debug
key: Down tap
T{ vr-settings vr.whisper-open l@  vr-settings vr.whisper-debug l@ -> 0 0 }T
key: X tap
T{ vr-settings vr.whisper-open l@ -> 1 }T
key: Down tap
key: X tap
T{ vr-settings vr.whisper-debug l@ -> 1 }T
key: X tap
T{ vr-settings vr.whisper-debug l@ -> 2 }T
key: X tap
T{ vr-settings vr.whisper-debug l@ -> 0 }T
testing down to the panic preset: Reduced by default - the tint, the stumbles, the full beat
key: Down tap   \ (the 3D Audio row, left as it is)
key: Down tap   \ (the Hiding door row, left as it is)

key: Down tap   \ (the panic preset)
T{ panic-preset  vr-settings vr.panic-tint l@  vr-settings vr.panic-stumbles l@  vr-settings vr.panic-haptics l@ -> 1 1 1 2 }T
testing ... right again Minimal (no screen effect, no stumbles, half the beat)
key: Right tap
T{ vr-settings vr.panic-tint l@ vr-settings vr.panic-smear l@ vr-settings vr.panic-zoom l@
   vr-settings vr.panic-negative l@ vr-settings vr.panic-shake l@ vr-settings vr.panic-stumbles l@
   vr-settings vr.panic-haptics l@ panic-preset -> 0 0 0 0 0 0 1 2 }T
testing ... right again Full (every screen effect, the stumbles, the full beat)
key: Right tap
T{ vr-settings vr.panic-tint l@ vr-settings vr.panic-smear l@ vr-settings vr.panic-zoom l@
   vr-settings vr.panic-negative l@ vr-settings vr.panic-shake l@ vr-settings vr.panic-stumbles l@
   vr-settings vr.panic-haptics l@ panic-preset -> 1 1 1 1 1 1 2 0 }T
testing ... right again Reduced; Custom is never one of the steps
key: Right tap
T{ vr-settings vr.panic-tint l@ vr-settings vr.panic-smear l@ vr-settings vr.panic-zoom l@
   vr-settings vr.panic-negative l@ vr-settings vr.panic-shake l@ vr-settings vr.panic-stumbles l@
   vr-settings vr.panic-haptics l@ panic-preset -> 1 0 0 0 0 1 2 1 }T
testing ... round: right again Minimal
key: Right tap
T{ panic-preset -> 2 }T
testing down to the Panic effects button: the cross opens its own page, the status screen's look
key: Down tap
key: X tap
T{ [: mode @ ;] menu@  [: page @ ;] menu@ -> on-panic 6 }T
testing ... its rows are the settings: the tint turned over makes the set Custom
key: X tap
T{ panic-preset -> -1 }T
testing ... down to the heartbeat's haptics: Full, Off, Half
key: Down tap key: Down tap key: Down tap key: Down tap key: Down tap key: Down tap
key: Right tap
T{ vr-settings vr.panic-haptics l@ -> 2 }T
key: Right tap
T{ vr-settings vr.panic-haptics l@ -> 0 }T
key: Right tap
T{ vr-settings vr.panic-haptics l@ -> 1 }T
testing ... up to the panic's stumbles: the row turns it over, both ways
key: Up tap
T{ vr-settings vr.panic-stumbles l@ -> 0 }T
key: X tap
T{ vr-settings vr.panic-stumbles l@ -> 1 }T
key: X tap
T{ vr-settings vr.panic-stumbles l@ -> 0 }T
testing the triangle is back to the VR options page, the preset row now Custom
key: V tap
T{ [: mode @ ;] menu@  [: page @ ;] menu@  panic-preset -> on-page 6 -1 }T
testing ... left from Custom picks Minimal (the last preset)
key: Up tap
key: Left tap
T{ vr-settings vr.panic-tint l@ vr-settings vr.panic-stumbles l@ vr-settings vr.panic-haptics l@ panic-preset -> 0 0 1 2 }T
testing the seated height sizes the world: 1.6 / 1.28 for the scale in use
1.28e vr-settings vr.seated-height sf!
T{ vr-body 1000e f* fround f>s  1000e f* fround f>s -> 1250 1280 }T
vr-defaults
T{ vr-settings vr.whisper-open l@  vr-settings vr.whisper-debug l@ -> 0 0 }T
testing the triangle leaves the page
key: V tap  20 run-frames
T{ stage@ -> 0 }T

testing teleport: a flick aims a place she may stand on, and letting go puts her there
free-play  30 run-frames
1 vr-settings vr.teleport l!
: tp-ok? ( -- flag )  fiona:tp-ok @ -1 = ;
: tp-try ( F: x z -- flag )  stick-z f!  stick-x f!  fiona:tp-target  tp-ok? ;
variable tp-found
: tp-scan ( -- )   \ (the first of four ways she may land: its stick direction left in the fields)
    0 tp-found !  fiona:tp-set-distance   \ (a flick's length: the aim walks out by it)
    4 0 do
        i case 0 of 0e -1e endof 1 of 1e 0e endof 2 of 0e 1e endof >r -1e 0e r> endcase
        tp-try if  1 tp-found !  leave  then
    loop ;
: tp-step ( -- )  fiona:teleport-step ;
create tp-was 12 allot
game:fiona enter  tp-scan  leave-actor
T{ tp-found @ -> 1 }T
game:fiona body-pos tp-was vec!
game:fiona enter  tp-step  leave-actor                       \ (the stick pushed: the aim up)
game:fiona enter  0e stick-x f!  0e stick-z f!  tp-step  leave-actor   \ (let go: there)
T{ tp-was game:fiona body-at vec-dist 20e f> -> -1 }T
testing the distance of the flick, taken: 2.5 m (31 room units) give or take the floor
T{ tp-was game:fiona body-at vec-dist 40e f< -> -1 }T
testing a long flick stops at what holds her back - a wall, or the edge of her floor
8e vr-settings vr.teleport-reach sf!   1 vr-settings vr.teleport l!
game:fiona enter  tp-scan  fiona:tp-set-distance  fiona:tp-target  leave-actor
T{ fiona:tp-ok @  game:fiona body-at fiona:tp-at vec-dist  8e fiona:room-metre f* f< -> -1 -1 }T
2.5e vr-settings vr.teleport-reach sf!
testing growing: a flick held longer reaches further - near at once, the reach by the hold
1 vr-settings vr.teleport-mode l!
: tp-hold ( n -- )   \ (aim that many ticks more)
    0 ?do  tp-step  loop ;
fvariable tp-got
game:fiona enter  -1e stick-z f!  0e stick-x f!  tp-step  fiona:tp-dist f@ tp-got f!  leave-actor
T{ tp-got f@ 20e f< -> -1 }T
game:fiona enter  30 tp-hold  fiona:tp-dist f@ tp-got f!  leave-actor
T{ tp-got f@ 30e f> -> -1 }T
game:fiona enter  0e stick-x f!  0e stick-z f!  tp-step  leave-actor   \ (let it go)
vr-defaults
testing the keyboard's panic menu (P) edits the same saved values as the page
key: P tap
T{ panic-menu:panic-open @ -> -1 }T
key: Down tap   \ (the heartbeat smear)
key: Return tap
T{ vr-settings vr.panic-smear l@  panic-preset -> 1 -1 }T
key: P tap
T{ panic-menu:panic-open @ -> 0 }T
vr-defaults
T{ panic-preset -> 1 }T

testing the keyboard's panic menu: the stumbles row too
key: P tap
key: Down tap key: Down tap key: Down tap key: Down tap
key: Return tap
T{ vr-settings vr.panic-stumbles l@ -> 0 }T
key: Return tap
T{ vr-settings vr.panic-stumbles l@ -> 1 }T
key: P tap
vr-defaults
test-summary
