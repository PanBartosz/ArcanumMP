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
