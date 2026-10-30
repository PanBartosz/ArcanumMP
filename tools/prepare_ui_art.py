#!/usr/bin/env python3
"""Prepare embedded HUD bitmaps from preserved imagegen PNG source art.

This is optional asset preparation, not an image-generation/API client. Game
builds consume the committed BMP files and do not need Python or Pillow.
"""
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/ui'


def main():
    manifest = json.loads((ART / 'manifest.json').read_text())
    for asset in manifest['assets']:
        name = asset['name']
        assert name.replace('-', '').isalnum(), name
        with Image.open(ART / (name + '.png')) as original:
            resized = original.convert('RGB').resize(tuple(asset['size']), Image.Resampling.LANCZOS)
            resized.quantize(colors=256).save(ART / (name + '.bmp'))
    print(f"Prepared {len(manifest['assets'])} embedded HUD bitmaps.")


if __name__ == '__main__':
    main()
