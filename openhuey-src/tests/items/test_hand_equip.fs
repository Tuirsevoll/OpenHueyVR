\ The menu's Equip for the key things and for the remedies and baits she carries (hga's own): Use,
\ Equip, Discard, Cancel; the item in her right hand with its grip, and Remove back to the list. A
\ map or a sketch has no Equip, and holding a remedy or a bait spends nothing. Headless:
\   build/openhuey-src/openhuey --test openhuey-src/tests/items/test_hand_equip.fs
IN: test-hand-equip
USING: tester engine game-state actors messages keys facts flag-names story-names room-names game items items.names items.tables subscreen subscreen.state subscreen.lists hewie.state hewie.body things debug ;

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
camomile 1 gives  chicken-jerky 1 gives

testing what a hand may hold: a key thing, a word plate, a remedy or a bait - not a map or a
\ sketch, what she wears, what she throws, a document or a medallion
T{ study-key hand-equippable?  garden-key hand-equippable? -> -1 -1 }T
T{ word-plate-key hand-equippable? -> -1 }T
T{ camomile hand-equippable?  lavender hand-equippable?  paptisma hand-equippable? -> -1 -1 -1 }T
T{ chicken-jerky hand-equippable?  esca hand-equippable?  bennenam-esca hand-equippable? -> -1 -1 -1 }T
T{ castle-sketch hand-equippable?  forest-map hand-equippable?  tower-sketch hand-equippable? -> 0 0 0 }T
T{ camomile hand-equippable?  chicken-jerky hand-equippable?  feather-boots hand-equippable? -> -1 -1 0 }T
T{ ball hand-equippable?  old-parchment hand-equippable?  album-medallion hand-equippable? -> 0 0 0 }T

testing a trigger box: a key thing's Use or a word plate's - not a remedy's or a bait's (no drink, no feed)
T{ study-key trigger-use?  word-plate-key trigger-use? -> -1 -1 }T
T{ camomile trigger-use?  paptisma trigger-use?  chicken-jerky trigger-use?  bennenam-esca trigger-use? -> 0 0 0 0 }T

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

testing a remedy's actions: Use, Equip, Discard, Cancel (the menu is left on the list: Select shuts it)
key: Tab tap  20 run-frames
T{ stage@ -> 0 }T
key: Tab tap  10 run-frames
T{ stage@ page@ -> 2 0 }T
key: E tap
T{ page@ cursor-item -> 1 camomile }T
key: X tap  2 run-frames
T{ mode@ cursor-item -> 1 camomile }T
T{ acts@ act1@ act2@ -> 4 can-equip 4 }T

testing Equip a remedy: in her hand, still in its place with its count
key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  camomile items-of -> 0 camomile 1 }T
testing Remove: out of her hand, still in the list with its count
key: X tap  key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  camomile items-of -> 0 -1 1 }T

testing a bait's actions too: Use, Equip, Discard, Cancel
key: Down tap  key: X tap  2 run-frames
T{ mode@ cursor-item -> 1 chicken-jerky }T
T{ acts@ act1@ act2@ -> 4 can-equip 4 }T
testing Equip a bait: in her hand; frames pass and nothing is fed, nothing spent
key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  chicken-jerky items-of -> 0 chicken-jerky 1 }T
50 run-frames
T{ hand-item  chicken-jerky items-of -> chicken-jerky 1 }T
testing Remove a bait: out of her hand, still in the list with its count
key: X tap  key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  chicken-jerky items-of -> 0 -1 1 }T

testing a used item leaves the hand: Camomile x3, Use once - 2 left, the hand empty (Part U)
camomile 2 gives
key: Up tap  key: X tap  2 run-frames
T{ mode@ cursor-item -> 1 camomile }T
key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item  camomile items-of -> 0 camomile 3 }T
key: X tap  key: X tap  2 run-frames
T{ hand-item  camomile items-of -> -1 2 }T
key: X tap  2 run-frames
T{ mode@ -> 0 }T
testing ... Equip again for each: the next and the last both leave the hand
key: X tap  key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item -> 0 camomile }T
key: X tap  key: X tap  2 run-frames
T{ hand-item  camomile items-of -> -1 1 }T
key: X tap  2 run-frames
key: X tap  key: Down tap  key: X tap  2 run-frames
T{ mode@ hand-item -> 0 camomile }T
key: X tap  key: X tap  2 run-frames
T{ hand-item  camomile items-of -> -1 0 }T
key: X tap  2 run-frames
T{ mode@ -> 0 }T

: list-place-of ( id -- list i )   \ (where it is: the list, and its place in it)
    find-item  progress pr.item-lists -  /held /  #places /mod  swap ;
: equip-id ( id -- )  list-place-of hand-equip ;   \ (into her hand, by its place)
: wear-id ( id -- )  list-place-of  game:subscreen enter  game:items send equip-item  leave-actor  deliver ;

testing Lavender x2: Use on the hand's place leaves the hand, one left
lavender 2 gives
lavender equip-id
T{ hand-item -> lavender }T
lavender find-item use-place 2drop
T{ hand-item  lavender items-of -> -1 1 }T

testing a key thing whose rule does nothing here: its Use leaves it in her hand
study-key equip-id
T{ hand-item -> study-key }T
study-key find-item use-place 2drop
T{ hand-item  study-key items-of -> study-key 1 }T

testing a set-down magnesia in slot 2: the VR hand's let-go spends one and empties the slot
magnesia 2 gives
magnesia wear-id
T{ 2 equipped -> magnesia }T
2 game:items send slot-let-go deliver
T{ 2 equipped  magnesia items-of -> -1 1 }T

testing a remedy at her mouth: the same Use - the rule, one spent, the hand emptied (Part B)
lavender 1 gives
lavender equip-id
T{ hand-item -> lavender }T
mouth-use
T{ hand-item  lavender items-of  calm-boost boost-on? -> -1 1 -1 }T

testing ... Weltigo while its hush is on: the rule says nothing - nothing spent, it stays in hand
weltigo 2 gives
weltigo equip-id
mouth-use
T{ hand-item  weltigo items-of -> -1 1 }T
weltigo equip-id
mouth-use
T{ hand-item  weltigo items-of -> weltigo 1 }T

testing a treat on the floor: the bait let go, Hewie eats it - his rule, one spent (Part A)
key: Tab tap  20 run-frames   \ (the menu shut: the world runs, and the items' tick with it)
T{ stage@ -> 0 }T
game:hewie send join-fiona  20 run-frames   \ (his body: the story's $8F can play its eating)
: his@ ( xt -- x )  game:hewie enter  execute  leave-actor ;
: his! ( xt -- )  game:hewie enter  execute  leave-actor ;
: items! ( xt -- )  game:items enter  execute  leave-actor ;
chicken-jerky 1 gives
[: 50 his-hp ! ;] his!
[: chicken-jerky game:fiona body-pos treat-drop ;] items!
T{ chicken-jerky items-of -> 2 }T   \ (lying there: not spent yet)
chicken-jerky game:items send treat-eaten  deliver
T{ chicken-jerky items-of  [: his-hp @ ;] his@ -> 1 70 }T

testing ... a treat nobody eats: the room left, it goes - and is not spent
[: chicken-jerky game:fiona body-pos treat-drop ;] items!
T{ chicken-jerky items-of -> 1 }T
front-garden-2 -1 game:rooms send go-to-room  20 run-frames
T{ chicken-jerky items-of -> 1 }T

testing ... and it lies there: 60 frames on, the thing is still used and nothing is spent
game:hewie send part-from-fiona  20 run-frames   \ (no Hewie to eat it)
: floor-treats ( -- n )   \ (the kind-11 things there are)
    0 #things 0 do  i thing-used? if  i thing-kind treat-thing = if  1+  then  then  loop ;
[: chicken-jerky game:fiona body-pos treat-drop ;] items!
2 run-frames   \ (the things actor's message takes a tick)
T{ floor-treats -> 1 }T
60 run-frames
T{ floor-treats  chicken-jerky items-of -> 1 1 }T

test-summary
