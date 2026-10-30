#!/usr/bin/env python3
"""Replay retail creation rules and drive the native wizard in private runtimes."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def run(runtime, output, label):
    runtime = runtime.resolve()
    binary = runtime / 'arcanum-mp'
    command = [str(binary)] if binary.exists() else ['wine', str(runtime / 'arcanum-mp.exe')]
    environment = {**os.environ, 'WINEDEBUG': '-all', 'SDL_VIDEODRIVER': 'dummy',
                   'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software',
                   'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    results = {}
    for mode in ('characters', 'character-ui', 'character-edit'):
        log_path = output / f'{label}-{mode}.log'
        with log_path.open('wb') as log:
            result = subprocess.run(command + ['-window', f'--coop-engine-probe={mode}', '--coop-probe-framebuffer'],
                                    cwd=runtime, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=45)
        text = log_path.read_text(errors='replace')
        assert result.returncode == 0 and f'ARCMP engine probe PASS mode={mode}' in text, text[-2500:]
        assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text and 'LeakSanitizer' not in text
        if mode == 'characters':
            data = json.loads((runtime / 'arcmp-character-result.json').read_text())
            assert data['verified'] and data['local_pc_preserved'] and data['invalid_party_preflight_rejected']
            assert data['race_gender_combinations'] == 12 and data['native_recipes_replayed'] >= 1000
            assert data['native_recipes_replayed'] == data['forged_point_balances_denied']
            results.update(data)
        elif mode == 'character-ui':
            data = (runtime / 'arcmp-created-character.bin').read_bytes()
            assert len(data) == 176 and data[:4] == b'ACH2'
            (output / f'{label}-created.bin').write_bytes(data)
            from PIL import Image
            for stage in ('identity', 'allocation'):
                with Image.open(runtime / f'arcmp-character-{stage}.bmp') as frame:
                    assert frame.size == (800, 600)
                    frame.save(output / f'{label}-{stage}.png')
            results['native_wizard_cancel_name_allocation_and_accept'] = True
        else:
            data = json.loads((runtime / 'arcmp-character-edit-result.json').read_text())
            assert all(data.values()), data
            results.update(data)
            for name in ('arcmp-edited-character.bin', 'arcmp-character-edit-fixture.draft'):
                (output / f'{label}-{name}').write_bytes((runtime / name).read_bytes())
            from PIL import Image
            for stage in ('edit', 'identity-reset'):
                with Image.open(runtime / f'arcmp-character-{stage}.bmp') as frame:
                    assert frame.size == (800, 600)
                    frame.save(output / f'{label}-{stage}.png')
    (output / f'{label}.json').write_text(json.dumps(results, indent=2) + '\n')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linux-runtime', type=Path, required=True)
    parser.add_argument('--windows-runtime', type=Path, required=True)
    parser.add_argument('--sanitized-runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = [run(runtime, args.output, label) for runtime, label in
               ((args.linux_runtime, 'linux'), (args.windows_runtime, 'windows-wine'), (args.sanitized_runtime, 'asan-ubsan'))]
    assert results[0] == results[1] == results[2]
    drafts = [(args.output / f'{label}-arcmp-character-edit-fixture.draft').read_bytes() for label in ('linux', 'windows-wine', 'asan-ubsan')]
    assert drafts[0] == drafts[1] == drafts[2]
    print('PASS: native creation rules, editable drafts and portable persistence on Linux, Windows/Wine and whole-engine ASan/UBSan')
