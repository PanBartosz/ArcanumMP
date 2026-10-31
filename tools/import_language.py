#!/usr/bin/env python3
"""Import extracted translation text/art without modifying the common campaign.

Extract Eselter's package with innoextract --collisions=rename --lowercase.
Scripts, maps, prototypes and executables are discarded; dialogue logic is
stripped. The language folder is local and is never part of multiplayer identity.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile


def fields(data, width):
    rows, values, at = {}, [], 0
    while at < len(data):
        if data[at:at + 2] == b'//':
            at = data.find(b'\n', at)
            if at < 0:
                break
            continue
        if data[at:at + 1] != b'{':
            at += 1
            continue
        end = data.find(b'}', at + 1)
        if end < 0:
            raise ValueError('Unterminated text field')
        values.append(data[at + 1:end])
        at = end + 1
        if len(values) == width:
            if not re.fullmatch(rb'\s*\d+\s*', values[0]):
                raise ValueError('Invalid text identifier')
            key = int(values[0])
            if key in rows or key > 2147483647:
                raise ValueError('Duplicate or oversized text identifier')
            rows[key] = values
            values = []
    if values:
        raise ValueError('Incomplete text record')
    return rows


def component_matches(component, variant):
    tokens = re.findall(r'\(|\)|[a-z_][a-z0-9_\\]*', component)
    expression = []
    for token in tokens:
        if token in {'(', ')', 'not', 'and', 'or'}:
            expression.append(token)
        else:
            enabled = token in {'main', 'main\\' + ('uap' if variant == 'uap' else 'stock')}
            expression.append(str(enabled))
    # Only fixed boolean literals/operators reach eval; no source code or
    # arbitrary names from the downloaded package are executed.
    try:
        return bool(eval(' '.join(expression), {'__builtins__': {}}, {}))
    except (SyntaxError, ValueError):
        return False


def candidate(source, path, variant):
    raw = path.relative_to(source).as_posix().lower()
    component = raw.split('#', 1)[1] if '#' in raw else ''
    raw = raw.split('#', 1)[0]
    if component and not component_matches(component, variant):
        return None
    if raw.startswith('app/'):
        raw = raw[4:]
    if raw.startswith('data/'):
        raw = raw[5:]
    elif raw.startswith('modules/arcanum/'):
        raw = raw[len('modules/arcanum/'):]
    elif raw.startswith('modules/'):
        return None
    duplicate = '$' in raw
    raw = raw.split('$', 1)[0]
    suffix = Path(raw).suffix
    allowed = ((raw.startswith('dlg/') and suffix == '.dlg')
               or (raw.startswith('mes/') and suffix == '.mes')
               or (raw.startswith('art/interface/') and suffix == '.art')
               or (raw.startswith('movies/') and suffix in {'.bik', '.srt'})
               or (raw.startswith('sound/speech/') and suffix in {'.mp3', '.wav'}))
    if not allowed or '..' in Path(raw).parts or ':' in raw:
        return None
    return raw, (20 if component else 0) + (1 if duplicate else 0)


def import_pack(source, game, code, name, variant, encoding):
    source, game = source.resolve(), game.resolve()
    if not source.is_dir() or not game.is_dir():
        raise ValueError('Source and game directories must already exist')
    if not re.fullmatch(r'[a-z][a-z-]{0,14}', code) or code == 'original':
        raise ValueError('Use a language identifier such as pl, de or fr')
    parent = game / 'languages'
    parent.mkdir(exist_ok=True)
    target = parent / code
    if target.exists():
        raise FileExistsError(f'Preserve or move the existing pack first: {target}')
    selected = {}
    for path in sorted(source.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        result = candidate(source, path, variant)
        if result:
            key, priority = result
            if key not in selected or priority > selected[key][0]:
                selected[key] = priority, path
    counts = Counter(Path(key).suffix for key in selected)
    if not counts['.dlg'] or not counts['.mes']:
        raise ValueError('No campaign translation found in the extracted pack')
    staging = Path(tempfile.mkdtemp(prefix=code + '-import-', dir=parent))
    try:
        manifest = []
        for key, (_, path) in sorted(selected.items()):
            dst = staging / key
            dst.parent.mkdir(parents=True, exist_ok=True)
            data = path.read_bytes()
            if key.endswith('.dlg'):
                rows = fields(data, 7)
                data = b''.join(b'{' + str(i).encode() + b'}{' + row[1]
                                + b'}{' + row[2] + b'}{}{}{}{}\n'
                                for i, row in sorted(rows.items()))
            elif key.endswith('.mes'):
                fields(data, 2)
            dst.write_bytes(data)
            manifest.append({'path': key, 'bytes': len(data),
                             'sha256': hashlib.sha256(data).hexdigest()})
        (staging / 'name.txt').write_text(name + '\n', encoding='utf-8')
        (staging / 'encoding.txt').write_text(encoding + '\n', encoding='ascii')
        report = {'language': code, 'name': name, 'base_variant': variant,
                  'encoding': encoding, 'presentation_only': True, 'files': manifest}
        (staging / 'pack.json').write_text(json.dumps(report, indent=2) + '\n')
        (staging / 'SOURCE.txt').write_text(
            'Third-party translation data, kept separate from ArcanumMP.\n' +
            ('Eselter Arcanum UAP2.0.2PL 1.7 uses the original Polish localization.\n'
             'https://www.moddb.com/mods/spolszczenie-arcanum/downloads/arcanum-uap20pl\n' if code == 'pl'
             else 'Imported local translation; see the source pack for its authors and terms.\n') +
            'Original translation and art retain their respective authors/terms.\n'
            'The engine license does not relicense these assets.\n'
            'Presentation resources only; no translated gameplay is used.\n')
        staging.rename(target)
    except BaseException:
        shutil.rmtree(staging)
        raise
    print(json.dumps({'path': str(target), 'resources': len(manifest),
                      'by_type': dict(counts), 'base_data_unchanged': True}))
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--language', default='pl')
    parser.add_argument('--name', default='Polski')
    parser.add_argument('--variant', choices=['stock', 'uap'], default='stock')
    parser.add_argument('--encoding', choices=['cp1250', 'cp1251', 'cp1252'], default='cp1250')
    args = parser.parse_args()
    import_pack(args.source, args.game, args.language, args.name, args.variant, args.encoding)
