# Drop-in executable launch

Delivery is Linux and Windows drop-in executables with their required libraries.
Use matching builds for the host and every client.
The current build uses **protocol 59**; update everyone together. Existing
campaign saves, player identities and personal settings remain readable.
The prebuilt Linux bundle requires glibc 2.44 or newer on x86-64. For an older
Linux distribution, [build from source](BUILDING.md). The Windows bundle is x86-64.

Use a writable copy of your Arcanum game folder, with the same game data and
patches as the other players. No preparation command is required.
Each player needs a legal game installation; bundles include no retail assets.

Close the game and copy the **contents of the bundle's `bin/` directory** into
the game folder. Keep the directory structure: Linux needs `arcanum-mp`
plus `lib/`; Windows needs `arcanum-mp.exe` plus the decoder DLLs beside it.
Preserve `arcmp-player.id`, saves, character drafts and existing `arcmp-*`
preferences/profile files. These include `arcmp-display.cfg`, `arcmp-ui.cfg`,
`arcmp-play.cfg` and per-player settings. Replacing the supplied executables and
libraries does not require deleting personal files. All players need the same
protocol and matching game data.

Launch `./arcanum-mp -window` or `arcanum-mp.exe -window`. File-manager and
absolute-path launches find the game data beside the executable when the
working directory does not contain it. An explicit game-data working directory
keeps precedence. Missing data produces a clear startup error.

Choose Multiplayer in the main menu. On first use, the game checks its files in
the background and displays progress. Host/Join waits for that check and then
continues automatically; the menu remains responsive. The game writes its own
`arcmp-content.id`, `arcmp-content.json` and a local `arcmp-content.cache`.
Later launches scan asset names, sizes and modification times in the background;
changed data is rehashed automatically. Old fingerprints are refreshed once.
If an external tool changes bytes while preserving both size and modification
time, remove `arcmp-content.cache` to force a full check. Make data changes while
the game is closed. Preserve `arcmp-player.id`.

Host a party or join the host's IPv4 address.
Discovery uses UDP 35740; the game uses TCP 35741 by default. Select or create a
character, mark Ready, and let the host Start campaign. F12 opens party chat and
shared controls. Chat wraps long messages; use Page Up/Page Down or the wheel
over chat to read history, and End to return to new messages. Each local player
uses a separate writable runtime.

Retain the licenses and dependency/source archives when redistributing a bundle.
Linux requires a compatible x86-64 glibc environment and system display/audio
support. Windows executable evidence uses Wine. The connected original main
quest and shared ending pass with retained checkpoints and strengthened combat
stats; see [release status](STATUS.md) for evidence and limits. See
[LICENSE.md](../LICENSE.md) and [NOTICE.md](../NOTICE.md) for terms and attribution.
