"""Exercise an original crash-site enemy, party combat XP and host-character death."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def run(runtime, output, label):
    runtime = runtime.resolve()
    result = runtime / 'arcmp-encounter-engine-result.json'
    result.unlink(missing_ok=True)
    binary = runtime / 'arcanum-mp'
    command = [str(binary)] if binary.exists() else ['wine', str(runtime / 'arcanum-mp.exe')]
    environment = {**os.environ, 'WINEDEBUG': '-all', 'SDL_VIDEODRIVER': 'dummy',
                   'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software',
                   'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                   'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    log_path = output / f'{label}.log'
    with log_path.open('wb') as log:
        process = subprocess.run(command + ['-window', '--coop-engine-probe=encounter-engine', '--coop-probe-framebuffer'],
                                 cwd=runtime, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=130)
    text = log_path.read_text(errors='replace')
    assert process.returncode == 0 and 'ARCMP engine probe PASS mode=encounter-engine' in text, text[-4000:]
    assert not any(error in text for error in ('ERROR: AddressSanitizer', 'runtime error:', 'LeakSanitizer'))
    data = json.loads(result.read_text())
    assert data['verified'] and data['native_prototype'] == 28447 and data['off_host_distance'] > 30
    assert data['initial_worth'] > 0 and data['initial_pool'] > 0 and data['rewarded_players'] == 4
    assert all(data[key] for key in ('native_ai_focus', 'native_ai_damage', 'enemy_died', 'native_xp_modifiers',
               'follower_combat_xp_credit', 'replica_xp_guard', 'corpse_xp_replay_guard',
               'host_death_preserves_campaign', 'dead_pc_excluded_from_xp', 'owned_corpse_persists', 'blocked_corpse_cleanup_guard', 'dead_owner_full_snapshot', 'local_anchor_boundary'))
    shutil.copy2(result, output / f'{label}.json')
    from PIL import Image
    for stage in ('encounter-ai', 'encounter-survivor'):
        with Image.open(runtime / f'arcmp-combat-{stage}.bmp') as image:
            assert image.size == (800, 600)
            image.save(output / f'{label}-{stage}.png')
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linux-runtime', type=Path, required=True)
    parser.add_argument('--windows-runtime', type=Path, required=True)
    parser.add_argument('--sanitized-runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for runtime, label in ((args.linux_runtime, 'linux'), (args.windows_runtime, 'windows-wine'),
                           (args.sanitized_runtime, 'asan-ubsan')):
        run(runtime, args.output, label)
    print('PASS: original enemy AI beyond host range, native party/follower XP, corpse/dead/replica guards and shared world after host-character death on Linux, Windows/Wine and whole-engine sanitizers')
