\ The menu's Equip for key things (hga's own): Use, Equip, Cancel; the item in her right hand,
\ and Remove back to the list. A map or a sketch has no Equip. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/items/test_hand_equip.fs
IN: test-hand-equip
USING: tester engine game-state actors messages keys facts flag-names story-names game items items.names items.tables subscreen subscreen.state subscreen.lists debug ;

: tap ( key -- )  dup true key-hold  game-tick  false key-hold  game-tick ;
: menu@ ( xt -- x )  game:subscreen enter  execute  leave-actor ;
: stage@ ( -- n )  [: stage @ ;] menu@ ;
: mode@ ( -- n )  [: mode @ ;] menu@ ;
: action@ ( -- n )  [: action @ ;] menu@ ;
: page@ ( -- n )  [: page @ ;] menu@ ;
: cursor-item ( -- id )  [: page @  dup cells cursors + @  item-at ;] menu@ ;
: gives ( id n -- )  game:items send give-item  deliver ;
: acts@ ( -- n )  [: #acts @ ;] menu@ ;
: act1@ ( -- a )  [: 1 act@ ;] menu@ ;
: act2@ ( -- a )  [: 2 act@ ;] menu@ ;

100 run-frames
study-key 1 gives  castle-sketch 1 gives  word-plate-key 1 gives

testing what a hand may hold: a key thing or a word plate, not a map or a sketch
T{ study-key hand-equippable?  garden-key hand-equippable? -> -1 -1 }T
T{ word-plate-key hand-equippable? -> -1 }T
T{ castle-sketch hand-equippable?  forest-map hand-equippable?  tower-sketch hand-equippable? -> 0 0 0 }T
T{ camomile hand-equippable?  feather-boots hand-equippable? -> 0 0 }T

testing the key's actions: Use, Equip, Cancel (use first)
key: Tab tap  10 run-frames
T{ stage@ page@ cursor-item -> 2 0 study-key }T
key: X tap  2 run-frames
T{ mode@ action@ cursor-item -> 1 0 study-key }T
T{ acts@ act1@ act2@ -> 3 can-equip go-back }T

testing Equip: in her right hand, still in its place in the list
key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  study-key items-of -> 0 study-key 1 }T
testing Remove: out of her hand, still in the list
key: X tap  key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  study-key items-of -> 0 -1 1 }T

testing a map or a sketch: no Equip (Use, Cancel)
key: Down tap  key: X tap  2 run-frames
T{ mode@ cursor-item -> 1 castle-sketch }T
T{ acts@ act1@ -> 2 go-back }T
key: V tap  2 run-frames

testing the word plate: Equip among Use and Discard (Use, Equip, Discard, Cancel)
key: Down tap  key: X tap  2 run-frames
T{ mode@ cursor-item -> 1 word-plate-key }T
T{ acts@ act1@ -> 4 can-equip }T
key: V tap  5 run-frames

testing a plate's word: two plates told apart (every plate is item $3F, the word is the plate)
word-plate-key 1 gives   word-plate-key 1 gives
: set-word ( addr len list i -- )  {: a n l k :}  l k held-at letters 8 0 fill  n 0 do  a i + c@  l k held-at letters i + c!  loop ;
s" METH" 0 2 set-word   s" EMETH" 0 3 set-word
0 2 hand-equip
T{ find-hand-place  0 2 held-at = -> -1 }T
T{ 0 2 held-at letters c@ -> 'M' }T
0 3 hand-equip
T{ find-hand-place  0 3 held-at = -> -1 }T
T{ 0 3 held-at letters c@ -> 'E' }T
T{ hand-item -> word-plate-key }T
hand-unequip
T{ find-hand-place -> 0 }T

test-summary
