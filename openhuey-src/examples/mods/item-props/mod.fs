\ item-props - the items lying about shown as models, not sparkles (mod API 1.5: props, item-placed;
\ docs/modding.md "Props", "Items lying about"). In a headset, an item a hand has taken also draws
\ its grip's model (its own item-<id>.gltf, or a shared model= file) at that hand, placed by
\ PROPS/grips.txt (docs/modding.md "Held items"). Try it:
\   openhuey --mod examples/mods/item-props
mod: item-props
version: 1 0
requires: openhuey 1 5
