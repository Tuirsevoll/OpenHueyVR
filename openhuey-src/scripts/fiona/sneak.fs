\ fiona/sneak.fs - the headset crouch's state: the story driver (story/crouch.fs) sets it from the
\ headset, and her movement and model (fiona/moving.fs) are animated and walked by it. The hide
\ flag is the original's fiona-half-hidden; this state only says how far down the crouch is.
IN: fiona.sneak

variable sneak-hide     \ 0/1: the hide flag is on, and the slow sneak walk hers
fvariable sneak-k       \ 0..1: the crouch animation's place (0 standing, 1 its lowest point)
fvariable sneak-speed   \ the sneak's share of a walk, and the walk's animation rate
: sneak? ( -- flag )  sneak-hide @ 0<>  sneak-k f@ 0e f> or ;
: sneak-hiding? ( -- flag )  sneak-hide @ 0<> ;
