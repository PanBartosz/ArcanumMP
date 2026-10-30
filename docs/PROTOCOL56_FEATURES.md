# Protocol56: display and party feedback

Update every participant together. Existing campaign saves remain readable.

Character circles use green for allies (including multiplayer humans and their
followers), blue for neutrals and red for enemies. Hover and combat circles use
the same relationship colors. An NPC attacking another human is also an enemy
to you. Rendering never executes an NPC's hostility scripts.

Scroll the wheel over the game world to zoom in or out. The point beneath the
pointer stays in place. Portraits, toolbars and other UI keep their size. Wheel
input over a list, menu, map or turn-order panel keeps that control's normal
behavior. Middle-button dragging still pans the view at the current zoom.
Zoom range is 50–200%; very large views limit zooming out to the engine's safe
sector span. Zoom does not change any other player's view.

In **Options → Video**, the added controls are:

| Setting | Behavior |
| --- | --- |
| Widescreen (restart) | On uses the display's aspect ratio in full screen and a wide window when windowed. Off uses the classic4:3 view, letterboxed when necessary. |
| UI size (restart) | 75–200% in25% steps. Scales the interface uniformly. Sizes are fitted to the original minimum800×600 layout and the engine's maximum view span. |
| Approach interactions | Walk or run toward characters, containers, doors and items. Independent of Always Run. Applies to your own character immediately and is retained with that character in campaign saves. |

Widescreen and UI size apply after closing and restarting the executable. Their
preferences are personal to each game folder; different players can use
different displays and UI sizes. An explicit `-geometry WIDTHxHEIGHT` overrides
automatic display sizing for that launch. Keep `arcmp-display.cfg` when updating.

Portrait badges show dead, sleeping/unconscious, poisoned, low fatigue, stunned,
paralyzed, blinded, crippled and encumbered states. Low fatigue means25% or less
of the character's maximum fatigue; magic also consumes this resource. Hover
over a portrait for the current status names. The existing health/fatigue bars,
turn indication, immortality marker and spell/skill/item portrait targeting remain.

New parties default to running toward interactions. Existing saves retain their
previous walking behavior until the preference is changed. Native encumbrance
and other movement rules still apply.
