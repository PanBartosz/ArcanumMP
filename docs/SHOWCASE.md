# Gameplay trailer and feature showcase

Revision 3, prepared 2026-10-30 from the current polished native engine.
The filming and website work changes no gameplay code.

- `dist/showcase-v3/ArcanumMP-trailer-1080p.mp4`: 58.6-second trailer.
- `dist/showcase-v3/ArcanumMP-showcase-1080p.mp4`: 1:56.8 feature showcase.
- `dist/showcase-v3/ArcanumMP-trailer-720p-small.mp4`: 8.6 MB attachment copy.
- Cover, title image, captions, credits and file/codec/audio manifest alongside.

The new takes use the polished HUD. Four actual native Linux processes connected
over TCP follow independent movement routes in Tarant, then fight native orcs
and an ore golem outside Tulla. Spell impacts and targets are framed in the
middle of the action. Cinematic shots omit the overlapping multi-player ping
labels from the old opening. Characters wear equipped clothes or armor.

UI demonstrations show continuous native title dragging and corner resizing,
with held mouse state and a visible cursor. Every shot is selected by recorded
frame number, avoiding debugger pauses in the final film. Native pixel scrolling
provides smooth panoramas in Tarant, Qintarra, Tulla and Vendigroth.

The short film and nine-second move/resize demonstration are embedded in the
static [website source](../site/README.md). Public project:
https://github.com/PanBartosz/ArcanumMP — website:
https://panbartosz.github.io/ArcanumMP/.

## Provenance and limits

Encounters, equipment, statistics and travel were prepared for filming. Gameplay
is rendered by the shipped executable; the filming library is loaded only into
private media runtimes. Four connected engines establish real multiplayer;
separate solo takes demonstrate the native UI. These are staged encounters,
not an uninterrupted normal-party playthrough or a balance claim. Titles,
crops and split views are editorial layers. No generated/interpolated gameplay
or simulated multiplayer panels are used.

All new selected takes record one completed native frame at 30 fps. Continuous
movement and combat cues run inside the frame loop, avoiding per-action debugger
pauses. Source takes and frame markers are retained in
`evidence/showcase/takes/`; the revision-3 decision lists, frame contact sheets,
full-stream decode/audio reports and delivery manifest are under
`evidence/showcase-v3/`. Earlier exports and takes remain preserved.

## Credits and license

Original game, artwork and native effects: Troika Games / Sierra Entertainment.
The engine and project changes retain Arcanum CE's Sustainable Use License 1.0.
The public videos use "Arcadia" and "Five Armies" by Kevin MacLeod
(incompetech.com), under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The tracks were excerpted, mixed, faded and normalized. Credits and original
track links appear in the film end card, delivery README and
[website credits](../site/credits.html). The original soundtrack is not used in
these public exports. Original game data and loose native effects/music are
not included in the source or drop-ins.

See [production tooling](../tools/showcase/README.md) and
[publication preparation](RELEASING.md) for reproducibility.
