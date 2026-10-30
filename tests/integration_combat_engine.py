#!/usr/bin/env python3
"""Exercise native four-controller initiative and presentation-only active combat loading."""
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
                   'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                   'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    log_path = output / f'{label}.log'
    with log_path.open('wb') as log:
        result = subprocess.run(command + ['-window', '--coop-engine-probe=combat-engine', '--coop-probe-framebuffer'],
                                cwd=runtime, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    text = log_path.read_text(errors='replace')
    assert result.returncode == 0 and 'ARCMP engine probe PASS mode=combat-engine' in text, text[-3000:]
    assert not any(error in text for error in ('ERROR: AddressSanitizer', 'runtime error:', 'LeakSanitizer'))
    data = json.loads((runtime / 'arcmp-combat-engine-result.json').read_text())
    assert data['verified'] and data['distant_players_in_initiative'] == 4 and data['native_overexertion_players'] == 4
    assert data['remote_starts_turn_based'] and data['stance_preserves_other_turn']
    assert data['other_actor_end_turn_ignored'] and data['shared_combat_ends_after_last_stance'] and data['real_time_stance']
    assert data['optional_resource_cache'] and data['active_combat_replica_load']
    assert data['single_player_rule_boundary']
    assert data['network_combat_verified'] is False
    (output / f'{label}.json').write_text(json.dumps(data, indent=2) + '\n')
    from PIL import Image
    for stage in ('host-turn', 'remote-turn', 'ended'):
        with Image.open(runtime / f'arcmp-combat-{stage}.bmp') as frame:
            assert frame.size == (800, 600)
            frame.save(output / f'{label}-{stage}.png')
    return data


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
    print('PASS: native four-controller initiative and AP parity on Linux, Windows/Wine and whole-engine sanitizers; this probe does not test network combat')
