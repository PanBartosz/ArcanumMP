# ArcanumMP website

A buildless static site for GitHub Pages. HTML, CSS and JavaScript need no framework
or package installation. Cormorant Garamond and its SIL OFL notice are self-hosted.

Preview with `python3 -m http.server 8000 --directory site` from the repository root.
The two video files are distribution artifacts rather than source assets:

- `assets/ArcanumMP-trailer.mp4`: the public 1080p trailer.
- `assets/layout-demo.mp4`: the short, continuous native move/resize demonstration.

The publication workflow copies these from the prepared media delivery. The public
Pages branch contains the complete playable site, while the main branch retains
its source. The same trailer is attached to the GitHub Release. YouTube can be
embedded later if a published video is supplied; the current site uses a native
HTML video player and loads no third-party embed or tracking scripts.

## Responsive checks

After changing layout or typography, review both pages at 320, 360, 390 and
430 CSS pixels, around the 680px layout breakpoint, and on desktop. Check the
title's text bounds as well as document overflow: the decorative hero clips
its background, so an oversized word can be cut off without a horizontal
scrollbar. Check with the local font loaded and the Georgia fallback.

Keep navigation usable on narrow screens, including the return link on credits.
Scroll through lazy-loaded images before reviewing the whole page, and check
both native video players and FAQ controls. Gallery images need `height: auto`
to override their HTML height when their width changes.
