# First public release

The public repository starts at version 0.1.0 with one source snapshot. The
original local development history, game data, identities, saves and recordings
are preserved separately. No earlier development build is presented as a public
release.

## License basis

Arcanum Community Edition uses **Sustainable Use License 1.0**, both in the
[imported revision](https://github.com/alexbatalov/arcanum-ce/blob/a7ff41b300ef712f0e7d088183a3d08957110cdd/LICENSE.md)
and the [current upstream license](https://github.com/alexbatalov/arcanum-ce/blob/main/LICENSE.md),
checked on 2026-10-30. Our root and imported copies retain that text.

The license expressly permits modification and distribution, provided the
software is supplied free of charge for noncommercial purposes. It requires
preserving copyright/licensing notices, providing the terms with copies and
prominently identifying modifications. ArcanumMP is published on that basis:
the README and NOTICE identify it as a modified fork, both license copies remain,
and the repository, site and downloads are free and noncommercial.

This is a source-available license with a noncommercial restriction; do not
relabel the project MIT/GPL or describe this license as OSI open source.
Third-party dependencies remain under their respective licenses. Drop-ins retain
compiler runtime notices, the pinned SDL/SDL_mixer/zlib/fpattern notices and
sources, and the shared LGPL FFmpeg build's corresponding sources/configuration.

The engine license does not grant rights to the original game data or soundtrack.
Game data remains separately required. Public trailers use credited CC BY 4.0
music instead of the original soundtrack: [Arcadia](https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1100326)
and [Five Armies](https://incompetech.com/music/royalty-free/index.html?Search=Search&isrc=USUAN1100875)
by Kevin MacLeod. Credits, source URLs, license and editing notice accompany the
films and appear on the website. Native game images/effects are credited to
Troika/Sierra. The site's font retains its SIL OFL notice.

## Distribution

- Repository and issues: https://github.com/PanBartosz/ArcanumMP
- Release: https://github.com/PanBartosz/ArcanumMP/releases/tag/v0.1.0
- Website: https://panbartosz.github.io/ArcanumMP/
- [Player guide](PLAYER_GUIDE.md), [building](BUILDING.md), [video provenance](SHOWCASE.md)

The game binaries correspond to the accepted polished interface implementation;
subsequent capture/site/publication edits do not change gameplay compilation
inputs. The public bundle metadata records executable hashes and the exact
public source commit. The original development checkout is retained for ongoing
work; it is not reset or rewritten to create the initial public history.
