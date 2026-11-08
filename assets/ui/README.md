# Arcanum MP HUD art

Generated using the built-in imagegen tool and the imagegen skill on 2026-10-29.
The style reference was the original in-game toolbar in
`evidence/modern-p57/modern-spell-preview.png`; it was not an edit target.

`iron-panel.png` is the original generated frame texture. `combat-journal.png`
is the original generated log-toggle icon. Runtime versions are palette BMPs at
256×256 and64×64, prepared with Pillow (Lanczos resize and256-color quantization).
CMake embeds these in the executable, so drop-ins require no loose UI files.
The frame is drawn as nine slices; corners retain their shape when panels resize.

## Dedicated component set — 2026-10-30

The follow-up art pass adds21 separately generated components, for23 source
images in total. All are embedded and used by the HUD:

| Artwork | Runtime use |
| --- | --- |
| `status-dead`, `status-unconscious`, `status-poisoned`, `status-tired`, `status-stunned`, `status-paralyzed`, `status-blinded`, `status-injured`, `status-encumbered` | Condition badges on multiplayer portraits |
| `turn-card`, `turn-card-active` | Upcoming and current turn-order entries |
| `portrait-bezel` | Player/NPC portrait surrounds |
| `action-slot` | The twelve additional personal action slots |
| `button-face` | Panel buttons, tabs and turn-order controls |
| `control-close`, `control-compact`, `control-expand`, `control-wide`, `control-opacity`, `control-move`, `control-resize` | Actual window and layout controls |

Every component has a preserved `.png` and an indexed `.bmp` derivative.
[Exact generation prompts](art-update-prompts.md) record the built-in imagegen
calls and the two style references. [manifest.json](manifest.json) records the
prepared sizes. Recreate derivatives with `python3 tools/prepare_ui_art.py` from
the repository root (Pillow required only for this optional preparation step).
Game builds consume the committed BMPs and require no image-generation service.

Assigned action slots use Arcanum's original spell, skill and item icons inside
the new slot surrounds. This retains the original ability/item meanings.

## Frame prompt

Use case: stylized-concept. Asset type: one production-ready reusable nine-slice game UI panel texture, square 1024x1024. Input image 1 is a STYLE REFERENCE ONLY: the original Arcanum bottom toolbar with blackened iron, scratched silver steel, thin aged brass/gold inlay and dark brown leather. Make a NEW empty panel skin matching that restrained late-Victorian industrial fantasy aesthetic. Front-on orthographic flat raster game art, pixel-crisp details, absolutely no perspective. Full-bleed square panel: a narrow continuous 48-pixel-wide metal border around all four edges, compact squared mechanical corner plates and tiny rivets entirely within the border, subtle warm brass inner bevel. The central 928x928 area must be an extremely dark nearly-black brown leather/slate field with only subtle fine texture, empty and suitable for high contrast text. Straight uninterrupted repeatable middle edges, corners aligned precisely, all four outer edges straight and flush to the canvas. Material should resemble the aged metal and muted gold accents of the reference, not the black flat custom rectangles in the middle of that screenshot. No text, no lettering, no icons, no extra frames, no scene, no people, no decorative central design, no oversized gears, no glows, no watermark. One complete usable square panel asset, opaque.

## Icon prompt

Use case: stylized-concept. Asset type: one production-ready game HUD button icon, square. Input image 1 is a STYLE REFERENCE ONLY: original Arcanum toolbar, scratched silver metal, blackened iron, muted antique brass, warm bone paper. Create a NEW single combat journal toggle icon fitting that original UI aesthetic. Front-on embossed antique open ledger with two parchment pages and a small diagonally laid steel sword; the silhouette must remain very legible at 32x32 pixels. Center within a small square aged metal button plate with a thin brass bevel, dark black-brown background. Restrained Victorian industrial fantasy, hand-painted pre-rendered 2001 PC RPG asset with crisp strong edges; not modern flat art, not cartoon, not glossy polished gold. One icon only, no text or lettering, no other buttons, no scene, no people, no watermark, opaque full square image.

## Quick-save and quick-load controls — 2026-11-08

`quick-save.png` uses a closed leather journal and quill; `quick-load.png` uses
an open ledger and a backward arrow. Both were generated separately with the
built-in imagegen tool to match the existing journal/control references.
[Exact prompts](quick-save-load-prompts.md) and source PNGs are retained. Their
48×48 BMPs are embedded and replace shortcut lettering on the two controls
beside the combat-log button; F9/F10 appear only in hover help.

Prepare only these derivatives with
`python3 tools/prepare_ui_art.py quick-save quick-load`.
