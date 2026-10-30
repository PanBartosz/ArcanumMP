# Party UI and debugging controls — protocol54

Update every participant to protocol54. Existing native campaign save formats
are unchanged. This batch retains the protocol53 player-reported corrections.

- **Dead-host combat controls.** The host can leave turn-based mode with Space
  or the top-panel **Real time** button while dead, incapacitated or off-turn.
  Timing is a host session control; ordinary actions still require a capable
  owned character and the correct turn. Another actor's new turn cannot make
  a timing or debugging request stale.
- **Per-character immortality.** Open F12. The host clicks **Immortal: OFF/ON**
  beside any party member. Damage still applies, but HP cannot fall below 1,
  including native death damage. Settings are replicated and saved with their
  campaign owners. Personal gameplay options cannot change the immortality bit.
  Enable it before death; it does not resurrect a dead character. Fatigue,
  unconsciousness and other effects still apply.
- **Human party portraits.** A persistent left-hand strip shows native character
  portraits, names, health (red), fatigue (blue), death/unconsciousness and an
  immortality marker. Your name is gold. A current-turn marker and border identify
  the active human. Click a portrait to center the camera; while targeting a
  spell, skill or item, click it to select that character through the native
  targeting and host-validation path. Clicking never transfers character ownership.
- **Turn order.** The upper panel expands during turn-based combat to show the
  round, current actor, remaining AP and upcoming initiative entries, with
  **NOW** highlighted. Click an entry to center the camera. Scroll over the panel
  for additional entries (up to 32 upcoming actors). The sequence comes from
  native combat and is replicated to clients, rather than guessed from speed.
  The panel also provides End turn, timing and F12 buttons.
  Both panels sit below the original toolbar so character, map, inventory and
  other native controls remain visible and clickable.
- **Client descriptions.** Fast position updates redraw vitals without rebuilding
  spell/skill buttons. Selecting a targeted spell or skill explicitly shows its
  description; multiplayer ability previews bypass the native three-second
  suppression after other messages. Successful delayed action receipts preserve
  the current preview. Read-only controls remain usable during another actor's turn.
- **Hover help.** Pause the pointer for about half a second over primary controls,
  spells, skills, quickbar/recent abilities, maintained spells, followers, human
  portraits or the turn panel. A separate tooltip explains the action and useful
  shortcuts without replacing the native description window. Tooltips and party
  panels hide for native dialogs and menus.
- **Middle-button panning.** Hold the mouse-wheel button and drag the game view,
  town map or world map; the map follows the pointer. Release to stop. This is a
  local camera action and works while off-turn, with native view/map bounds.
  Drags start only on the map, are canceled by menus/focus loss, and do not
  move characters, select targets, alter notes or plot routes. Rotating the wheel
  retains list/turn-order scrolling. This follow-up keeps protocol54 compatibility.

Verification and current drop-ins are recorded in [STATUS.md](STATUS.md) and
[the campaign TODO list](CAMPAIGN_TODO.md). Full connected campaign
acceptance remains separate from this player-requested batch.

The campaign follow-up corrects a host crash when poisoned characters enter
world travel. Hidden characters skip poison damage; blood effects now also stay
inside the damage branch instead of using an uninitialized target. Native
hidden/visible poison checks pass under ASan/UBSan, followed by a four-engine
original Caladon/Roseborough continuation to story14 with shared save/load.
Combat stats are strengthened in that continuation; no quest/item/position
injections or NPC quieting are used. Protocol54 and native saves are unchanged.
