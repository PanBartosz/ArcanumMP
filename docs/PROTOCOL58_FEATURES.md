# Protocol58: HUD controls and smoothness fixes

Update the host and every client together. Existing campaign saves and personal
playability settings remain readable. The UI art is embedded in the executable;
there are no extra loose image files to install.

## Combat log and layout

Click the **journal-and-sword icon** near the right edge of the world view to
show or hide the combat log. **F11** still works. The log remembers whether it
was open, its compact/expanded mode, position and size across game launches.
The compact default leaves space for the native AP strip, extra action rows
and combat-preview text.

Drag the log's **title strip** to move it. Drag its **bottom-right grip** to
resize it. The expanded tools and conversation panel have the same controls.

Click **UI** beside the journal icon to enter **Edit UI** mode. Other custom
HUD panels show a move strip and a resize grip: party portraits, turn order,
extra action rows, pinned objectives, pause reason and combat preview. The
log button itself can also be moved and resized. Click **OK** beside the
journal icon to finish editing. Panel actions are disabled while you edit
that panel, so dragging does not cast a spell or spend an action.

Each panel has its own saved geometry. Positions are kept within the current
screen when changing resolution. **B → Settings → Reset HUD positions and
sizes** restores the defaults without changing transparency or other settings.
Settings also offers the Edit HUD layout switch.

## Transparency

**B → Settings** lists individual transparency controls for the compact log,
expanded log, play tools, extra actions, objectives, turn order, party portraits,
pause reason, preview, log button, hover help and conversation panel.
Click an entry to cycle **0%,25%,50%,75% transparency**. These choices are personal
and apply immediately. Log/tool title bars also have a percentage button showing
their current **opacity** (100% is solid).

Layout, log visibility and transparency use **`arcmp-ui.cfg`** beside the game
executable. Keep it, along with `arcmp-play.cfg`, `arcmp-player-*.cfg`, your
identity and saves, when updating.

## Turn-order targeting and speech

**Left-click an enemy's turn-order portrait to attack.** Select a spell, skill
or targeted item first and left-click any valid portrait to use it on that
character. It follows the same native targeting and multiplayer authority path
as clicking in the world: turn, range, visibility, resources and target rules
still apply. An off-turn click gives feedback and submits no action.
**Right-click a turn-order portrait to center the camera** without targeting.
An ordinary left-click on an ally still centers when no ability is selected.

Speech bubbles retain the original player/NPC color meanings and centering.
Scaled fonts now use the native color-key-safe rendering, removing colored
fringes around letters. Blue speech text is distinct from the blue transparency
key, so it remains visible.

## Rendering and appearance

Previews, labels and pings restore their previous screen regions instead of
invalidating the entire world every frame. Normal object rendering skips the
new relationship/label work when those features are inactive. Status logging
resolves names only when a relevant condition changes. Unchanged action slots
and pause/preview text no longer redraw on every update.

Custom panels use a shared worn-iron, muted-brass and dark-leather skin, with
an embossed combat-journal button. The completed component pass also includes
nine condition badges, current/upcoming turn cards, portrait bezels, action-slot
surrounds, button faces and seven window/layout control icons. Assigned slots
show the original game spell, skill or item icon alongside their label. Window
control icons retain their labels or hover explanations and existing click areas.
The art was generated with the built-in
imagegen tool using the original in-game toolbar as a style reference.
[Source assets and exact prompts](../assets/ui/README.md) are included in the
source archive; runtime images are compiled into the drop-ins.

Evidence and timings are under `evidence/ui-p58/`. Rendering timings are scoped
native measurements, not a claim of fixed frame rates on every machine.
Windows runtime checks use Wine.

The dedicated art pass is covered by `evidence/ui-art/acceptance.json`: all23
embedded components, native HUD sizes, live party conditions, assigned action
icons and existing layout/targeting controls. It retains protocol58 and existing
saved settings. Earlier network, campaign and rendering evidence is reused.
