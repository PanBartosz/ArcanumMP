# Playing Arcanum MP

Each player needs a writable copy of Arcanum with the same game data and patches,
and the same multiplayer build. Update every player together.
Close the game, then copy the contents of the bundle's `bin/` into that folder.
Linux needs `arcanum-mp` and `lib/`; Windows needs `arcanum-mp.exe` and the supplied
DLLs. Keep `arcmp-player.id`, saves and character drafts when updating.
Keep `arcmp-ui.cfg`, `arcmp-play.cfg` and `arcmp-player-*.cfg` for panel layouts,
transparency, personal controls, junk marks,
extra actions and quest notes.

**Playability tools:** F11 opens the combat log; B opens searchable Items, Actions,
Quests, Craft and Settings. Tap Left Alt for interaction labels, Backslash for
a multiplayer ping, and Grave/backtick for the extra action bar. The native
Options menu also links to the new settings. [Full controls](PROTOCOL57_FEATURES.md).

**HUD and portrait controls:** the journal-and-sword button shows/hides the log; **UI** beside it
enables move/resize handles. Saved layouts and individual transparency are in
B → Settings. Enemy turn-order portraits accept attacks and targeted abilities;
right-click focuses them. [Layout and targeting controls](PROTOCOL58_FEATURES.md).

**UI polish:** previews and hover help use compact cards; the turn strip fits
the roster, and the log leaves more world visible. Narrow action rows use icons
with full names on hover. Saved custom layouts stay in place; use
**B → Settings → Reset HUD positions and sizes** to adopt the new defaults.
[Before/after and details](UI_POLISH.md).

**Protocol 59 QoL update:** update every player together. Campaign saves and
settings remain readable. World transfers use less bandwidth, duplicate poses
no longer delay movement blends, and clipped graphics/scaled text are corrected.
Shortcut hints follow your bindings. [Changes and measurements](QOL_BENCHMARKS.md).

Launch the executable beside `Tig.dat`. An absolute-path or file-manager launch
also finds the data beside the executable. An existing game-data working
directory takes precedence, so separate development runtimes continue to work.
The game reports missing data before starting if neither folder contains it.

Choose your personal language in **B → Settings** or **Options → Audio**, then
restart. Mixed English/Polish parties use the same gameplay data. Polish assets
are a separate presentation pack; see [language setup](LOCALIZATION.md).
Hover over looted bodies or containers to see **Empty / Pusto**.

## Start a party

1. Choose **Multiplayer**. The first content check runs in the background;
   Host/Join continues automatically when it finishes.
2. One player chooses **Host**. Others use discovery or enter the host's LAN IPv4
   address. The defaults are TCP 35741 for the game and UDP 35740 for discovery.
3. Select or create characters, prepare equipment, and mark **Ready**.
   The host chooses **Start campaign**.

A content mismatch means players have different game files or patches. Close
the games and make those files match. Normal file changes are detected on the
next launch. If a tool changed bytes while keeping file size and modification
time unchanged, remove `arcmp-content.cache` to force a fresh check. Do not delete
`arcmp-player.id`: it identifies your saved character.
Settings, logs, saves, language packs, backups and generated map/tile caches do
not need to match. A remaining mismatch can be investigated using
`arcmp-content.json` from both game folders; see [file compatibility](FILE_COMPATIBILITY.md).

## During play

Each human controls their own character using the original game controls.
Multiplayer starts with Always Run enabled on Linux and Windows; hold Ctrl to
walk, or turn Always Run off in Options. Significant encumbrance still prevents
running. Older multiplayer defaults migrate once when loading into protocol53.
A left click attacks an enemy engaged with any human or their followers, even
when your character has not drawn a weapon. After an engaged fight ends, your
character sheathes the weapon and the ordinary cursor returns.
The person starting a conversation chooses replies; the others observe.
Inventory, equipment, advancement and quickbars belong to the local character.
The host runs the shared world and controls game difficulty/combat timing.

Human portraits appear on the left. Red and blue bars show health and fatigue;
your name is gold. Click to center the view. When a spell, skill or item is
waiting for a target, click a portrait to target that character instead.
Normal range, visibility, ownership and turn rules apply. Hover over controls
for help; hovering does not replace a selected ability's description.

**Hold the middle mouse button (press the wheel) and drag** to pan the game
view, town map or world map. The map follows your hand. Release to stop. Camera
panning also works during another character's turn and does not move your
character or plot a journey. Start the drag over the map, away from UI controls.
Normal wheel rotation still scrolls lists and the turn-order panel.
Scroll over the game world to zoom in or out around the pointer. The UI keeps
its size; each player controls their own view. Green circles identify allies,
blue circles neutrals, and red circles enemies.

Portrait badges identify poison, low fatigue, sleep/unconsciousness, death,
stunning, paralysis, blindness, crippling and significant encumbrance. Hover
over a portrait to read its status names. Low fatigue means25% or less remains.

**Options → Video** now includes **Widescreen (restart)**, **UI size (restart)**
and **Approach interactions: Walk/Run**. Restart the executable after changing
widescreen or UI size. Interaction speed changes your own character's approach
to people, items, containers and doors independently of Always Run. It is saved
with that character. Display settings are personal and stored in
`arcmp-display.cfg`; keep that file when updating. The interface fits the
original minimum layout and the engine's maximum view span.

In turn-based combat, the upper panel shows the round, current actor (**NOW**),
remaining action points and upcoming turns. Scroll over it to see more entries;
click one to center the view. **End turn** advances your turn. The host can use
**Space** or **Real time** to leave turn-based combat even after the host dies.

For debugging, the host can open **F12** and click **Immortal: OFF/ON** beside
each character. ON keeps HP at 1 or higher and persists in saves. Enable it
before the character dies; it does not resurrect anyone. Fatigue loss and
unconsciousness still apply.

| Action | Control |
| --- | --- |
| Party chat, votes and shared controls | F12 |
| Native screenshot | Print Screen |
| Older chat / latest messages | Page Up / Page Down; wheel over chat; End |
| Discovered documents | F7 |
| Conversation journal | F8 |
| Shared quick save / propose quick load | F9 / F10 |
| Named save or load | Native Save/Load menu or F12 → Save party / Load campaign |
| Give a party member gold | F12 → Give gold; choose recipient and amount |
| Autosave settings / newest automatic save | F12 → Autosave / Autosaves |
| Propose rest or wait | S; gather the party first |
| Map notes | N in the map |

Rest, travel and loading require party agreement. Answer the vote in F12;
any player can decline or stop a rest/journey. Opening a local menu does not
pause everyone. **Pause** changes turn-based/real-time timing (the host controls
it in multiplayer). If controls appear frozen, check F12 for a vote or missing owner.

**Give gold** transfers an exact amount from your purse. Stand within eight
tiles of a connected, conscious party member, select them and type an amount
or use 25 / 100 / All. The host validates both balances before applying it.

**Load campaign** works during play, even without a quick save. Choose a named
save, quick save, recovery or autosave from the host’s browser. Every player
must agree; a refusal leaves the current campaign running.

The host can set automatic saves to Off or every **2 / 5 / 10 / 15 / 30 minutes**,
and toggle saving before map/world travel. The default is five minutes with
travel saves enabled. Three slots rotate; manual saves remain separate. Saves
wait until the party is safe and actions have settled. Automatic files stay on
the host. Choose **F12 → Autosaves** or the **Autosaves** button in the save
browser to select the newest copy directly; manual shared saves
retain a copy on each participant. Preferences persist across launches.

Travel autosaves capture the departure location after actions have settled,
before the old map is unloaded or world travel starts. Loading one returns to
the departure map without automatically repeating the saved transition.

In **B → Settings**, automatic turn-based triggers cover a spotted enemy,
critical health, incapacitation and a broken weapon. Set their radius to
**5–40 tiles** from a player character (default 20). In co-op the host sets this
shared policy. These events enter the native turn sequence without freezing
the session or opening the party window.

Status gains and endings appear above affected characters using the original
floating-text renderer, including conditions outside combat. Text is translated
locally and expires normally. **Critical-hit screen shake** briefly moves only
the world when your character receives a damaging critical hit. Turn it off in
**B → Settings**; each player has their own saved choice.

**Mute looping spell sounds**, also in **B → Settings**, silences persistent
spell loops, the four elemental attribute buffs, Tempus Fugit and the familiar's
repeating wing sound. Ordinary casting, hits, other creature sounds, ambience,
dialogue and music remain audible. This is a personal choice, saved across
launches; change it during play to mute or restore effects already sounding.
Other players can keep their own choice. No game-data replacement is needed.

Plot world-map journeys using the original waypoint controls. Multiplayer keeps
all plotted waypoints (up to 30), so routes around rivers and mountains follow
your chosen approach. Everyone votes on the same route. Encounters and Stop
interrupt travel normally; reopen the map to review or plot the remaining route.
A rejected submission leaves the plotted map open so you can adjust or retry it.

Town-map routes also retain all six plotted waypoints. They move only your
character and require no party vote. Your Always Run setting and Ctrl/Num
modifiers determine walking or running. A new movement click or Halt replaces
or stops the route; normal combat and shared-operation interruptions still apply.

## Continue and recover

For a planned session, save the party and wait for completion before leaving.
On the next launch, the original players join with their existing profiles,
mark Ready, and the host chooses **Resume save**.

A guest disconnect freezes the shared simulation and reserves their character.
Automatic retries use a two-minute grace period. Restarting with the same
profile and joining the running host restores that owner; a new profile cannot
take their place. The host's live world is synchronized after everyone returns.

After abrupt host loss, reassemble the original profiles and use **Recover
party** for the last completed durable checkpoint, or **Resume save** for a
named save. Recovery may roll back changes since that checkpoint. There is no
automatic migration of a running world to a different host. A terminal engine
error displays its reason and stops retrying; use **Leave** or **Back to title**
and recover the preserved checkpoint.

## Release limits

The original main quest now connects from the opening through the Device ending
on four mixed engines, including shared ending presentation and native menu
return. This check uses retained native checkpoints and strengthened combat
stats, with normal owner inputs steering automated routing. It establishes the
original quest/item/travel chain; it is not one uninterrupted normal-party
playthrough or a check of every side quest. Separate prepared-state checks cover
alternate endings and other multiplayer systems. The accompanying
`FINAL_READINESS.md` records the evidence and limits.

Windows executable checks used Wine. No retail assets are included. Retain the
licenses and source archives when redistributing. See [release preparation](RELEASING.md).
