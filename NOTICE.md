# Attribution and third-party notices

ArcanumMP is a modified fork of
[Arcanum Community Edition](https://github.com/alexbatalov/arcanum-ce), imported
from commit `a7ff41b300ef712f0e7d088183a3d08957110cdd`. This fork adds campaign
multiplayer, engine integration, playability tools, HUD artwork and development
utilities. Imported engine files and their original notices are retained under
`upstream/arcanum-ce/`.

The engine and this project's modifications use the
[Sustainable Use License](LICENSE.md). The imported engine's copy is retained
at [upstream/arcanum-ce/LICENSE.md](upstream/arcanum-ce/LICENSE.md).

SDL, SDL_mixer, zlib and fpattern are fetched at the revisions specified by the
engine's CMake dependency definitions. Their own licenses remain with their
sources and are collected by the release packaging tool. The pinned movie
decoder is built as shared FFmpeg libraries; its original source archive,
configuration record and LGPL notice accompany drop-in packages. See
[decoder provenance](docs/MEDIA.md#decoder-build-and-license).

Preserved generated HUD images, prepared bitmaps and exact prompts are recorded
in [assets/ui/README.md](assets/ui/README.md). They are embedded in the executable;
players do not need separate UI image files.

The project-owned Polish UI catalog is under `assets/localization/`. Eselter's
Polish translation 1.7 and the original Polish game text, font, art and recordings
are separate third-party data. Import support does not relicense or bundle those
assets. [LOCALIZATION.md](docs/LOCALIZATION.md) records the author download,
verified hashes and presentation-only import procedure.

The website uses Cormorant Garamond by the Cormorant Project Authors under
SIL Open Font License 1.1; the font and notice are under `site/assets/fonts/`.
The current public films use excerpts of "Arcadia" and "Five Armies" by Kevin
MacLeod (incompetech.com), licensed under Creative Commons Attribution 4.0
(https://creativecommons.org/licenses/by/4.0/). The recordings were excerpted,
mixed, faded and normalized. Full sources and credits are on the
[website credits page](site/credits.html) and with the video exports.

Arcanum: Of Steamworks & Magick Obscura is the original game by Troika Games /
Sierra Entertainment. Its data files are required separately and are not bundled
in the source or drop-in archives. Documentation/showcase screenshots depict
development gameplay. Trailer soundtrack and presentation credits are retained
in [SHOWCASE.md](docs/SHOWCASE.md) and the accompanying media delivery README.
