\ The curtained mirrors a hand draws in the headset (game/vrmirror.c and the three rooms' own
\ vr-bare / vr-cover / vr-mirror-ok?): the room's open and shut branches set its flag and its glass
\ exactly as the flat screen's `the-curtain` does, and its gate says when a pull is blocked. The room
\ registers the curtain in came-in (vr? on). Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/story/test_vr_mirror.fs
IN: test-vr-mirror
USING: tester engine game-state actors messages room-names game debug facts flag-names story-names story.kit fiona.state story.places.old-mansion-1f-11 ;

free-play  10 run-frames
true vr!   \ (the headset: the curtain is the hands', and came-in registers it)
old-mansion-1f-11 room!  60 run-frames

\ (the room's own words, run as they are in a frame: the effects are the place's fields)
: room-actor ( -- id )  room-id room-name actor-named ;
: in-room ( xt -- )  room-actor enter  execute  leave-actor ;

testing old-mansion-1f-11: a whole mirror, its curtain drawn across (mirror-shown's own state)
T{ om1f-11-breakable-broken fact?  om1f-11-flag1 fact? -> 0 0 }T

testing the room's open branch: the flag bare
[: vr-bare ;] in-room
T{ om1f-11-flag1 fact? -> -1 }T

testing its shut branch: covered again
[: vr-cover ;] in-room
T{ om1f-11-flag1 fact? -> 0 }T

testing its gate: free to pull while the mirror is whole; broken is no
T{ [: 0 vr-mirror-ok? ;] in-room -> 0 }T
om1f-11-breakable-broken fact!
T{ [: 0 vr-mirror-ok? ;] in-room -> 2 }T
om1f-11-breakable-broken fact-off
T{ [: 0 vr-mirror-ok? ;] in-room -> 0 }T

test-summary
