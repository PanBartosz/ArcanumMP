#!/usr/bin/env python3
"""Prepare embedded HUD bitmaps from preserved imagegen PNG source art.

This is optional asset preparation, not an image-generation/API client. Game
builds consume the committed BMP files and do not need Python or Pillow.
"""
import argparse
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/ui'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('names',nargs='*',help='Prepare only these asset names; default: all.')
    args=parser.parse_args()
    manifest = json.loads((ART / 'manifest.json').read_text())
    known={asset['name'] for asset in manifest['assets']}
    if set(args.names)-known: parser.error('Unknown asset name')
    prepared=0
    for asset in manifest['assets']:
        if args.names and asset['name'] not in args.names: continue
        name = asset['name']
        assert name.replace('-', '').isalnum(), name
        with Image.open(ART / (name + '.png')) as original:
            resized = original.convert('RGB').resize(tuple(asset['size']), Image.Resampling.LANCZOS)
            resized.quantize(colors=256).save(ART / (name + '.bmp'))
        prepared+=1
    print(f"Prepared {prepared} embedded HUD bitmaps.")


if __name__ == '__main__':
    main()
