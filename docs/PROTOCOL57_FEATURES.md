# Protocol57: playability tools

Update the host and every client together. Existing campaign saves remain
readable. The additions work in ordinary single-player as well as campaign
co-op; pings and the shared pause policy apply to co-op.

## Opening the tools

**F11** opens the combat log. **B** opens the play tools, with Log, Items,
Actions, Quests, Craft and Settings tabs. **Options → Playability settings**
opens the same Settings page from the native Options menu. Escape closes the
panel. Click a search box to type; Enter finishes typing. Scroll over the list
to browse, or over the details pane to read a long description.

The log has a compact mode and a wider layout. Filters select all events,
your character, combat or effects. Selecting an older entry holds that entry
while new events arrive; **Latest** follows the newest entries again.
**Export log** writes `arcmp-combat-log.txt` in the game folder.

## Combat and exploration

The combat log records native attack and Dodge rolls, critical results,
situational penalties, actual health and fatigue damage, healing, deaths and
status feedback. Expand an entry by selecting it. Multiplayer clients receive
the host's events; the retained history also travels with campaign saves.
The history retains the latest512 entries. Older saves start with an empty log.

Turn-based targeting previews movement paths, AP costs and remaining AP,
weapon/spell range, blocked line of sight and spell area. Aggressive area spells
warn when an ally is within the area. The original targeting, action costs,
fatigue overexertion and damage rules still decide the action.

Tap **Left Alt** to toggle interaction labels and highlighting. Settings can
restore hold-to-highlight behavior or hide labels on empty containers/bodies.
Labels distinguish doors, containers, bodies and loose loot. Rendering decides
which objects are visible; labels do not reveal invisible objects or traps.
Hover relationship text adds `[+] Ally`, `[=] Neutral` or `[!] Enemy` to the
existing circle colors.

**Backslash** pings the position under the pointer, in the world or on the world
or local map. Pings identify their player and expire after ten seconds. They do
not become permanent map notes. Directions at the edge of the world view help
locate distant human teammates.

## Items, actions and crafting

Items can be searched by name or filtered to equipment, consumables, miscellaneous
and marked junk. Select equipment to compare it with the item equipped in the
corresponding slot. **Use / equip** uses the native item action.

**Mark / unmark** remembers individual junk items. Open an NPC merchant's
barter window, then open Items and choose **Sell junk** to sell marked eligible
items through normal merchant quotes. Click it again to stop. Equipped items,
gold, keys, documents and protected items are excluded. A merchant can refuse
an item; refused items are reported. Selling does not bypass scripts, prices,
ownership or the merchant's available gold.

Actions lists known spells with fatigue costs and descriptions, plus the native
selectable skills. Tool-based skills continue to use their original tools.
Choose **Use action** to enter ordinary targeting. Select a slot and choose
**Assign** to put a spell, skill or item in the extra bar. The bar has two rows
of six clickable slots; **Grave / backtick** toggles it. Empty slots open Actions.
Each character keeps its own assignments. Unavailable spells are dimmed.

Craft lists learned recipes and their original component alternatives. Its
**Craftable now** filter checks components and required expertise. Select a
recipe to see what is missing, then **Craft selected** to use original crafting.
Item details also list uses in recipes that the character already knows.

## Quests, readability and controls

Quests lists only discovered objectives. Pin up to five for the world-view
tracker, keep an editable personal next-step note, and use **Recent changes**
to review changes since **Mark reviewed** was last used. Map and Journal open
the original known information. The tracker can shorten objectives to fit;
click it to read their complete descriptions.

Settings offers independent100%,125% and150% text size, higher contrast,
relationship symbols, tooltip delay, label behavior, combat previews and the
extra action bar. Text changes apply immediately, including native speech and
single-player dialogue choices. Existing whole-UI scaling and widescreen
settings remain under Options → Video.

Current behavior (0.4.0): a spotted enemy, critical health, incapacitation or a
broken weapon can enter native turn-based combat. The host selects events and
a 5–40-tile radius from a human PC; single-player uses personal settings. These
triggers do not create pause holds or open F12. Pause changes combat timing.
Settings also include personal critical-hit screen shake; native status floaters
expire above characters in each player's language.

Select a control binding in Settings and press its replacement key. A conflict
swaps assignments; Escape cancels. **Reset key bindings** restores defaults.
Bindings cover the tools, inventory, character, map/logbook, spells/skills,
combat stance/timing, rest, schematics, Fate, party/save commands and native
quick slots. **E** ends the current character's turn; **Space** changes combat
timing. Normal typing in dialogues and quantity fields retains its input keys.
F1–F6 retain the original companion commands. Some original UI hints still show
default keys after rebinding; the commands themselves use the saved bindings.

Personal preferences use `arcmp-play.cfg`. Junk marks, extra slots, pins,
reviewed quest states and the next-step note use `arcmp-player-*.cfg` beside the
executable. Keep these files when updating. These personal choices do not edit
other players' preferences.

Runtime evidence is recorded under `evidence/modern-p57/`. Windows runtime
checks use Wine; installers, Windows VMs and real-Windows testing are excluded.
