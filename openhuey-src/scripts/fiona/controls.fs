\ fiona/controls.fs - the stick as a direction in the room (Fiona_MoveInput). Camera-relative;
\ after a camera cut the old camera keeps steering while the stick is held (3 frames for
\ sure, then until it moves), then she turns toward the old camera's heading while the stick
\ holds within 15 degrees. On the keyboard: W A S D the stick, Shift the run button; a gamepad's left stick as it is pushed.
IN: fiona.controls
USING: engine keys common vectors messages facts fiona.state fiona.anims ;

: hands-off? ( -- flag )  her-scripted @ her-in-scene @ or ;   \ (a script or a cutscene has her)
\ (a message up doesn't: nothing of hers reads the window - a script reading one to her holds her;
\ a remark offered she reads looking about, free - Progress_PlayerButtons' $80000005)
: sx ( F: -- x )  stick-x f@ ;   : sz ( F: -- z )  stick-z f@ ;
: nx ( F: -- x )  way-x f@ ;     : nz ( F: -- z )  way-z f@ ;
: read-stick ( -- )
    0e stick-x f!  0e stick-z f!
    hands-off? if  exit  then
    stick@ stick-z f!  stick-x f! ;
: run-held? ( -- flag )   \ (Fiona_Move: cross held; the headset: the left stick click toggles it)
    hands-off? 0=  vr? if  vr-run?  else  cross button-down?  then  and ;
: turned ( F: cam-yaw nx nz -- dx dz )  stick-turned ;
\ in the headset, the stick points where she is looking, not where the old camera sits
: steer-yaw ( F: -- a )  vr? if  vr-look-yaw  else  cam-yaw  then ;
: small? ( F: x z -- flag )  fabs 0.5e f<= fabs 0.5e f<= and ;
: dir! ( F: x z -- )  her-dir v.z sf!  0e her-dir v.y sf!  her-dir v.x sf! ;
: dir-by ( F: yaw -- )  nx nz turned dir! ;
: stick-heading ( F: -- a )  her-dir v.x sf@ her-dir v.z sf@ fatan2 ;
\ how she turns to the stick (her-turn-mode): by the camera now; the old camera steering after a
\ cut; turning round to the old camera's heading; through a door with the stick held (fiona.fs)
enum: turn-mode  camera-relative steered turning-round door-steered ;
\ (a cut: she faces where she was going, and with the stick held the old camera steers)
: after-cut ( -- )
    0 her-turn-mode !
    her-mode @ dup own-mode = swap down-mode = or 0= if  exit  then
    her-heading f@ her-yaw f!  0 her-still !
    nx nz small? if  exit  then
    3 her-lock !  steered her-turn-mode !  steer-yaw her-steer-yaw f!
    steer-yaw nx nz turned fatan2 her-heading f! ;
defer settled-for-stand ( -- how )   \ (2 once her animation has faded in, else 1: fiona.moving)
\ how the direction is taken: the camera now, her facing, none, the steering camera
enum: direction-from  by-camera by-facing by-none by-steering ;
: how ( moving -- moving how )
    her-turn-mode @ case
        camera-relative of  dup if  by-camera  else  settled-for-stand  then  endof
        steered of  dup 0= if
                  by-none  her-still @ 6 = if  camera-relative her-turn-mode !  then
              else
                  by-steering
                  her-lock @ if  -1 her-lock +!  her-cam-yaw f@ her-steer-yaw f!
                  else
                      sx her-stick v.x sf@ f-  sz her-stick v.z sf@ f-  vlen 0.01e f< if
                          turning-round her-turn-mode !  0.5e deg>rad her-turn-rate f!  steer-yaw her-steer-yaw f!
                      then
                  then
                  her-steer-yaw f@ nx nz turned fatan2 her-turn-to f!
              then  endof
        turning-round of  dup if
                  by-camera  nx nz fatan2  her-last v.x sf@ her-last v.z sf@ fatan2 f- angle-wrap fabs
                  15e deg>rad f> if  camera-relative her-turn-mode !  then
              else
                  by-none  her-still @ 6 = if  camera-relative her-turn-mode !  then
              then  endof
        >r by-camera r>
    endcase ;
\ the headset is the only steering. the old camera's cuts and its "keep the last camera" turn
\ are not used, and she moves the way the stick points without waiting to face it.
\ right stick: turn her in the world, and the view with her, so the room turns while
\ the headset keeps facing the same way in the room
: vr-smooth-turn ( -- )
    hands-off? if  exit  then
    vr-right fdup f0= if  fdrop exit  then
    3.5e deg>rad f*
    fdup vr-turn!
    fdup her-yaw field+!
    her-heading field+! ;
\ the stick in the room, relative to where she looks (not the body, which follows the walk):
\ x right, z down on the stick
: vr-aim ( F: -- x z )
    {: | F: yaw :}
    vr-face to yaw
    yaw fcos fnegate sx f*  yaw fsin sz f* f-
    yaw fsin sx f*          yaw fcos sz f* f- ;
: vr-move ( -- )
    read-stick
    vr-smooth-turn
    sx sz vlen fdup f0> if  fdup sx fswap f/ way-x f!  sz fswap f/ way-z f!  else  fdrop 0e way-x f!  0e way-z f!  then
    \ a light tilt still counts as moving; only a dead stick is "let go"
    sx sz vlen 0.12e f< dup if  1 her-still +!  her-still @ 6 min her-still !  else  0 her-still !  then  drop
    \ looking around does not turn her body: she faces the way she walks (moving.fs's
    \ turn-to-stick). A story having her turns her by her animation, the view turning with her
    \ (story-turned, fiona.fs's frame after her doing)
    vr-aim dir!
    camera-relative her-turn-mode !
    0 her-cut !
    sx her-stick v.x sf!  0e her-stick v.y sf!  sz her-stick v.z sf!
    steer-yaw her-cam-yaw f!
    nx her-last v.x sf!  0e her-last v.y sf!  nz her-last v.z sf!
    run-held? her-run? ! ;
\ the headset: a story having her turns her by her own animation, and the view turns with her (by
\ the turn she took since the frame's start: her-yaw-was) - she crawls in where the motion takes her
: story-turned ( -- )
    vr? her-scripted @ and if  her-yaw f@ her-yaw-was f@ f- angle-wrap vr-turn!  then ;
: read-controls ( -- )
    vr? if  vr-move exit  then
    read-stick
    sx sz vlen fdup f0> if  fdup sx fswap f/ way-x f!  sz fswap f/ way-z f!  else  fdrop 0e way-x f!  0e way-z f!  then
    her-turn-mode @ door-steered =  her-cut @ or if  after-cut  then  0 her-cut !
    sx sz small? dup if  1 her-still +!  her-still @ 6 min her-still !  else  0 her-still !  then  0=
    how nip
    case
        by-steering of  her-cam-yaw f@ dir-by  endof
        by-none of  0e 0e dir!  endof
        by-facing of  her-yaw f@ fdup fsin fswap fcos dir!  endof
        by-camera of  steer-yaw dir-by  endof
    endcase
    sx her-stick v.x sf!  0e her-stick v.y sf!  sz her-stick v.z sf!
    her-turn-mode @ camera-relative = if  steer-yaw her-cam-yaw f!  then
    her-turn-mode @ turning-round <> if  nx her-last v.x sf!  0e her-last v.y sf!  nz her-last v.z sf!  then
    run-held? her-run? ! ;

\ ---- her struggling (Fiona_Shakes): this frame's shakes - the stick swung past 120 degrees, or out
\ from rest; the buttons pressed (keys.fs) ----
: shakes-now ( -- n )   \ (Fiona_Shakes: the stick swung past 120 degrees, or out from rest; the buttons - not in the run)
    hewie-played? if  0 exit  then
    0  her-stick v.x sf@ her-stick v.z sf@ vlen                               ( n ) ( F: len )
    fdup 0.8e f<= if  0.2e f< if  -1 her-shake-rest !  then
    else
        fdrop  her-stick v.x sf@ her-stick v.z sf@ fatan2
        her-shake-rest @ if  1+  0 her-shake-rest !  her-shake-a f!
        else  fdup her-shake-a f@ f- angle-wrap fabs 2.0943952e f> if  1+  her-shake-a f!  else  fdrop  then  then
    then
    buttons-pressed 0<> 1 and + ;   \ (any of the buttons: one)
\ the story's test of her shaking (Progress_FionaFlag: Fiona_Shakes as from rest) - the stick out
\ past 0.8, or a button; her own count left as it is
: shook-from-rest ( -- n )  hewie-played? if  0 exit  then  her-stick v.x sf@ her-stick v.z sf@ vlen 0.8e f> 1 and  buttons-pressed 0<> 1 and + ;

