#!/usr/bin/env python3
"""Drive owned game instances on Hyprland and capture just their windows.

This exercises real input, native UI and real TCP together. Inspect every output
image; input delivery and a screenshot alone are not visual pass assertions.
"""
import argparse
import json
import os
import signal
from pathlib import Path
import subprocess
import struct
import time

X11_WINDOWS = {}


def run(*args):
    return subprocess.check_output(args, text=True)


def dispatch(expression):
    result = run("hyprctl", "dispatch", expression)
    if result.strip() != "ok":
        raise RuntimeError(result)


def send_key(address, value):
    if address in X11_WINDOWS:
        run('xdotool', 'key', '--window', X11_WINDOWS[address], value)
        time.sleep(.05)
        return
    dispatch(f"hl.dsp.send_shortcut({{mods='', key={json.dumps(value)}, window='address:{address}'}})")
    time.sleep(0.05)


def tab(address, count):
    for _ in range(count):
        send_key(address, "Tab")


def type_text(address, value):
    if address in X11_WINDOWS:
        run('xdotool', 'type', '--window', X11_WINDOWS[address], '--delay', '15', value)
    else:
        run('wtype', '-d', '15', value)


def focus(address):
    dispatch(f"hl.dsp.focus({{window='address:{address}'}})")
    dispatch('hl.dsp.window.bring_to_top()')
    for client in json.loads(run('hyprctl', 'clients', '-j')):
        if client['address'] == address:
            x, y = client['at']
            dispatch(f'hl.dsp.cursor.move({{x={x + 400}, y={y + 300}}})')
    time.sleep(0.1)


def client_for_pid(pid):
    for client in json.loads(run("hyprctl", "clients", "-j")):
        if client["pid"] == pid:
            return client
    return None


def wait_log(game, value, timeout=15, after=0):
    deadline = time.monotonic() + timeout
    path = game['log_path']
    while time.monotonic() < deadline:
        if value in path.read_text(errors='replace')[after:]:
            return
        if game['process'].poll() is not None:
            break
        time.sleep(.05)
    raise AssertionError(f"Expected {value!r} in {path}: {path.read_text(errors='replace')[-1200:]}")


def read_state(game):
    deadline = time.monotonic() + 2
    while True:
        try:
            return json.loads((Path(game['runtime']) / 'arcmp-live-state.json').read_text())
        except (json.JSONDecodeError, FileNotFoundError):
            if time.monotonic() >= deadline: raise
            time.sleep(.01)


def shared_view(games, minimum=1):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            views = [(Path(game['runtime']) / 'arcmp-live-dialogue.bin').read_bytes() for game in games]
            raw = views[0]
            if len(raw) >= 101 and raw[:4] == b'ACD1' and struct.unpack_from('>I', raw, 8)[0] >= minimum and all(view == raw for view in views):
                return raw, {'revision': struct.unpack_from('>I', raw, 8)[0],
                             'active': bool(struct.unpack_from('>I', raw, 12)[0]),
                             'chooser': struct.unpack_from('>I', raw, 20)[0]}
        except (FileNotFoundError, struct.error):
            pass
        assert all(game['process'].poll() is None for game in games)
        time.sleep(.02)
    raise AssertionError('four game windows did not publish the same dialogue page')


def capture(game, path):
    focus(game['client']['address'])
    time.sleep(.2)
    client = client_for_pid(game['process'].pid)
    x, y = client['at']; width, height = client['size']
    run('grim', '-g', f'{x},{y} {width}x{height}', str(path))


def click_game(game, x, y):
    address = game['client']['address']
    focus(address)
    client = client_for_pid(game['process'].pid)
    sx = client['at'][0] + round(x * client['size'][0] / 800)
    sy = client['at'][1] + round(y * client['size'][1] / 600)
    dispatch(f'hl.dsp.cursor.move({{x={sx}, y={sy}}})')
    # A compositor warp can remain pending until the next input frame. Flush a
    # real pointer motion before pressing, so SDL sees both edges at this point.
    pointer_environment = {**os.environ, 'YDOTOOL_SOCKET': f'/run/user/{os.getuid()}/.ydotool_socket'}
    subprocess.check_output(['ydotool', 'mousemove', '--', '1', '0'], env=pointer_environment)
    subprocess.check_output(['ydotool', 'mousemove', '--', '-1', '0'], env=pointer_environment)
    time.sleep(.1)
    assert json.loads(run('hyprctl', 'activewindow', '-j')).get('pid') == game['process'].pid
    subprocess.check_output(['ydotool', 'click', '0xC0'],
                            env={**os.environ, 'YDOTOOL_SOCKET': f'/run/user/{os.getuid()}/.ydotool_socket'})
    time.sleep(.2)


def create_character(game, index, output):
    address = game['client']['address']
    tab(address, 1)  # Pick preset → Create character
    send_key(address, 'Return')
    wait_log(game, 'ARCMP character creation opened')
    click_game(game, 110, 232)
    type_text(address, f'Desktop hero {index}')
    send_key(address, 'Return')
    for _ in range(index): click_game(game, 263, 340)
    capture(game, output / f'character-identity-{index}.png')
    click_game(game, 704, 523)
    click_game(game, 475, 194)
    capture(game, output / f'character-allocation-{index}.png')
    click_game(game, 704, 523)
    wait_log(game, f'ARCMP character submitted name=Desktop hero {index}')
    # Opening the lobby resets keyboard focus to the player name field.
    tab(address, 12)


def saved_draft(game):
    import zlib
    runtime = Path(game['runtime'])
    owner = (runtime / 'arcmp-player.id').read_bytes()
    path = runtime / f'arcmp-character-{owner.hex()}.draft'
    data = path.read_bytes()
    assert len(data) == 236 and data[:8] == b'ACDRAFT2' and data[56:60] == b'ACH2'
    assert data[8:40] == bytes.fromhex((runtime / 'arcmp-content.id').read_text().strip()) and data[40:56] == owner
    assert struct.unpack_from('>I', data, len(data)-4)[0] == zlib.crc32(data[:-4])
    fields = {'name': data[130:162].split(b'\0', 1)[0].decode('ascii'),
              'hp': struct.unpack_from('>H', data, 68)[0], 'points': struct.unpack_from('>H', data, 72)[0],
              'strength': struct.unpack_from('>H', data, 74)[0]}
    return path, data, fields


def edit_draft(game, index, output, cancel):
    offset = len(game['log_path'].read_text(errors='replace'))
    click_game(game, 212, 384)  # Edit selected character.
    wait_log(game, 'ARCMP character creation opened', after=offset)
    if not cancel:
        click_game(game, 110, 232)
        type_text(game['client']['address'], f'Revised hero {index}')
        send_key(game['client']['address'], 'Return')
    click_game(game, 704, 523)
    click_game(game, 418, 194)  # Refund the previous HP purchase.
    click_game(game, 234, 167)  # Spend the refunded point on strength.
    click_game(game, 95, 523)   # Back to identity, retaining allocations.
    if cancel:
        click_game(game, 95, 523)
        wait_log(game, 'ARCMP lobby opened', after=offset)
        return
    click_game(game, 259, 132)  # Portrait changes retain allocations.
    click_game(game, 704, 523)
    capture(game, output / f'character-edited-{index}.png')
    click_game(game, 704, 523)
    wait_log(game, f'ARCMP character submitted name=Revised hero {index}', after=offset)


def check_draft_editing(game, index, output):
    _, original, fields = saved_draft(game)
    assert fields == {'name': f'Desktop hero {index}', 'hp': 1, 'points': 4, 'strength': 8}, fields
    edit_draft(game, index, output, cancel=True)
    assert saved_draft(game)[1] == original, 'cancelling an edit replaced the accepted draft'
    if index == 0:
        # Force replacement failure while keeping the accepted file for recovery.
        path = saved_draft(game)[0]
        backup = path.with_suffix('.draft-failure-backup')
        assert not backup.exists(), 'a previous failure fixture needs inspection'
        path.rename(backup)
        try:
            path.mkdir()
            offset = len(game['log_path'].read_text(errors='replace'))
            edit_draft(game, index, output, cancel=False)
            wait_log(game, 'saved=0', after=offset)
            capture(game, output / 'draft-save-failure.png')
            assert backup.read_bytes() == original
        finally:
            if path.is_dir(): path.rmdir()
            backup.rename(path)
        offset = len(game['log_path'].read_text(errors='replace'))
        click_game(game, 459, 384)
        wait_log(game, f'ARCMP character loaded name=Desktop hero {index}', after=offset)
        assert saved_draft(game)[1] == original
    edit_draft(game, index, output, cancel=False)
    _, revised, fields = saved_draft(game)
    assert fields == {'name': f'Revised hero {index}', 'hp': 0, 'points': 4, 'strength': 9}, fields
    assert revised != original
    click_game(game, 94, 384)  # Select a preset without losing the saved custom build.
    offset = len(game['log_path'].read_text(errors='replace'))
    click_game(game, 459, 384)
    wait_log(game, f'ARCMP character loaded name=Revised hero {index}', after=offset)
    assert saved_draft(game)[1] == revised
    (output / f'draft-{index}.bin').write_bytes(revised)
    return revised


def open_game(games, index, runtime, output, launch, append=False):
    runtime = runtime.resolve()
    for name in ('arcmp-live-state.json', 'arcmp-live-dialogue.bin'):
        (runtime / name).unlink(missing_ok=True)
    binary = runtime / 'arcanum-mp'
    command = [str(binary), '-window'] if binary.exists() else ['wine', str(runtime / 'arcanum-mp.exe'), '-window']
    if launch: command += ['--coop-live-probe']
    log_path = output / f'game-{index}.log'
    offset = len(log_path.read_text(errors='replace')) if append and log_path.exists() else 0
    log = log_path.open('a' if append else 'w')
    process = subprocess.Popen(command, cwd=runtime, stdout=log, stderr=subprocess.STDOUT,
                               env={**os.environ, 'WINEDEBUG': '-all'})
    game = {'process': process, 'log': log, 'log_path': log_path, 'runtime': str(runtime), 'log_start': offset}
    if index < len(games): games[index] = game
    else: games.append(game)
    deadline = time.monotonic() + 20
    client = None
    while time.monotonic() < deadline:
        client = client_for_pid(process.pid)
        if client: break
        if process.poll() is not None: raise RuntimeError(f'game {index} exited: {process.returncode}')
        time.sleep(.1)
    assert client, f'game {index} did not create a window'
    game['client'] = client
    if client['xwayland']:
        xid = run('xdotool', 'search', '--onlyvisible', '--pid', str(process.pid)).splitlines()
        assert len(xid) == 1, xid
        X11_WINDOWS[client['address']] = xid[0]
    focus(client['address'])
    wait_log(game, 'ARCMP main menu ready', after=offset)
    return game


def enter_party(game, index, preset=True):
    address = game['client']['address']; offset = game['log_start']
    send_key(address, 'm'); wait_log(game, 'ARCMP lobby opened', after=offset)
    for _ in 'Traveller': send_key(address, 'BackSpace')
    type_text(address, f"{'Host' if index == 0 else 'Guest'} {index + 1}")
    tab(address, 4 if index == 0 else 5); send_key(address, 'Return')
    wait_log(game, 'ARCMP lobby phase=2', after=offset)
    time.sleep(.2)
    if preset:
        tab(address, 4 if index == 0 else 3); send_key(address, 'Return'); time.sleep(.2)


def restart_saved_game(games, index, output, launch, expected):
    game = games[index]; runtime = Path(game['runtime'])
    offset = len(game['log_path'].read_text(errors='replace'))
    click_game(game, 439, 164)  # Leave the lobby without losing the saved draft.
    wait_log(game, 'ARCMP lobby phase=0', after=offset)
    click_game(game, 336, 384)  # Disabled New character after leaving is harmless.
    assert saved_draft(game)[1] == expected
    assert game['process'].poll() is None
    game['process'].kill(); game['process'].wait(timeout=5); game['log'].close()
    X11_WINDOWS.pop(game['client']['address'], None)
    game = open_game(games, index, runtime, output, launch, append=True)
    enter_party(game, index, preset=False)
    assert saved_draft(game)[1] == expected
    offset = len(game['log_path'].read_text(errors='replace'))
    click_game(game, 459, 384)
    wait_log(game, f'ARCMP character loaded name=Revised hero {index}', after=offset)
    assert saved_draft(game)[1] == expected
    click_game(game, 100, 118); tab(game['client']['address'], 12)


def controls(games, output):
    raw, view = shared_view(games)
    (output / 'opening.bin').write_bytes(raw)
    for game in games:
        if read_state(game)['local_slot'] == view['chooser']:
            continue
        focus(game['client']['address'])
        send_key(game['client']['address'], '1')
    assert shared_view(games)[1]['revision'] == view['revision'], 'an observer advanced the dialogue'
    chooser = next(game for game in games if read_state(game)['local_slot'] == view['chooser'])
    focus(chooser['client']['address'])
    send_key(chooser['client']['address'], '1')
    raw, view = shared_view(games, view['revision'] + 1)
    (output / 'opening-next.bin').write_bytes(raw)
    for index, game in enumerate(games): capture(game, output / f'dialogue-{index}.png')
    focus(chooser['client']['address'])
    send_key(chooser['client']['address'], 'Escape')
    raw, closed = shared_view(games, view['revision'] + 1)
    assert not closed['active']
    (output / 'opening-closed.bin').write_bytes(raw)
    # Ordinary OS mouse clicks. Search a small visible ground region and require
    # the host to execute a movement plus a real owned-PC position change.
    for index, game in enumerate(games):
        before = read_state(game)
        member = before['local_member']
        initial = (before['members'][member]['x'], before['members'][member]['y'])
        moved = False
        attempts = []
        for px, py in ((270,260), (530,280), (330,380), (460,330)):
            address = game['client']['address']; focus(address)
            client = client_for_pid(game['process'].pid)
            sx = client['at'][0] + round(px * client['size'][0] / 800)
            sy = client['at'][1] + round(py * client['size'][1] / 600)
            # XTest coordinates use a different scale from Hyprland geometry.
            # Move through the compositor and deliver the ordinary button on
            # both native Wayland and Wine/Xwayland windows.
            dispatch(f'hl.dsp.cursor.move({{x={sx}, y={sy}}})')
            time.sleep(.05)
            active = json.loads(run('hyprctl', 'activewindow', '-j'))
            attempts.append({'target': [px, py], 'screen': [sx, sy], 'window': client,
                             'active_pid': active.get('pid'), 'cursor': json.loads(run('hyprctl', 'cursorpos', '-j'))})
            assert active.get('pid') == game['process'].pid, f'mouse target belongs to a different window: {attempts[-1]}'
            subprocess.check_output(['ydotool', 'click', '0xC0'],
                env={**os.environ, 'YDOTOOL_SOCKET': f'/run/user/{os.getuid()}/.ydotool_socket'})
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                current = read_state(game)
                position = current['members'][member]
                if (position['x'], position['y']) != initial:
                    moved = True; break
                time.sleep(.03)
            if moved: break
        (output / f'pointer-{index}.json').write_text(json.dumps(attempts, indent=2) + '\n')
        assert moved, f'ordinary movement did not move owned PC {member}'
        (output / f'moved-{index}.json').write_text(json.dumps(current, indent=2) + '\n')
    for index, game in enumerate(games): capture(game, output / f'controls-{index}.png')
    game = games[0]; address = game['client']['address']; focus(address)
    send_key(address, 'F12')
    wait_log(game, 'ARCMP lobby opened')
    tab(address, 3)
    type_text(address, 'Party chat during the campaign.')
    send_key(address, 'Return')
    for participant in games: wait_log(participant, 'chats=5')
    capture(game, output / 'party-chat.png')
    send_key(address, 'F12')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", action="append", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("evidence/desktop"))
    parser.add_argument('--launch', action='store_true', help='start the real campaign after the ready/chat lobby checks')
    parser.add_argument('--hold', type=int, default=0, help='keep launched owned games open for further controlled UI inspection')
    parser.add_argument('--controls', action='store_true', help='exercise opening dialogue authority and OS mouse movement after launch')
    parser.add_argument('--loading-check', action='store_true', help='delay one owned client and verify Escape/F12 cannot leave the loading panel')
    parser.add_argument('--custom-characters', action='store_true', help='create each character through the native wizard using desktop input')
    parser.add_argument('--drafts', action='store_true', help='edit, cancel, save, exercise save failure, restart every game and reload its character')
    args = parser.parse_args()
    assert not args.drafts or args.custom_characters
    assert len(args.runtime) == 4, "four isolated runtimes required"
    assert not args.controls or args.launch
    assert not args.loading_check or args.launch
    assert subprocess.run(['pgrep', '-x', 'hyprlock'], stdout=subprocess.DEVNULL).returncode != 0, 'Desktop is locked.'
    args.output.mkdir(parents=True, exist_ok=True)
    games = []
    original_active = json.loads(run('hyprctl', 'activewindow', '-j')).get('address')
    original_cursor = json.loads(run('hyprctl', 'cursorpos', '-j'))
    try:
        for index, runtime in enumerate(args.runtime):
            game = open_game(games, index, runtime, args.output, args.launch)
            enter_party(game, index)
            if args.custom_characters: create_character(game, index, args.output)
            if args.drafts:
                revised = check_draft_editing(game, index, args.output)
                restart_saved_game(games, index, args.output, args.launch, revised)
        # Joining and character changes invalidate all previous readiness.
        for game in games:
            address = game["client"]["address"]
            focus(address)
            if not args.custom_characters: tab(address, 4)
            send_key(address, "Return")
            time.sleep(.1)
        for index, game in enumerate(games):
            wait_log(game, 'players=4 ready=4')
            address = game['client']['address']
            focus(address)
            tab(address, 6)  # Ready button → chat field
            type_text(address, f'Ready, traveller {index + 1}.')
            send_key(address, 'Return')
        for index, game in enumerate(games):
            wait_log(game, 'chats=4')
            address = game["client"]["address"]
            focus(address)
            time.sleep(.3)
            client = client_for_pid(game["process"].pid)
            x, y = client["at"]
            width, height = client["size"]
            run("grim", "-g", f"{x},{y} {width}x{height}", str(args.output / f"game-{index}.png"))
        if args.launch:
            host = games[0]
            address = host['client']['address']
            delayed = next((game for game in games[1:] if (Path(game['runtime']) / 'arcanum-mp').exists()), None) if args.loading_check else None
            assert not args.loading_check or delayed, 'loading check needs a native Linux client'
            if delayed:
                os.kill(delayed['process'].pid, signal.SIGSTOP)
            focus(address)
            tab(address, 10)  # Chat field → Start campaign.
            send_key(address, 'Return')
            if delayed:
                try:
                    wait_log(host, 'ARCMP runtime phase=3')
                    send_key(address, 'Escape')
                    send_key(address, 'F12')
                    time.sleep(.3)
                    log = host['log_path'].read_text(errors='replace')
                    assert 'ARCMP runtime phase=5' not in log and 'ARCMP runtime phase=6' not in log
                    capture(host, args.output / 'loading.png')
                finally:
                    os.kill(delayed['process'].pid, signal.SIGCONT)
            for game in games:
                wait_log(game, 'ARCMP campaign connected', timeout=30)
            states = []
            for index, game in enumerate(games):
                deadline = time.monotonic() + 10
                state = None
                while time.monotonic() < deadline:
                    try:
                        state = json.loads((Path(game['runtime']) / 'arcmp-live-state.json').read_text())
                        if state['phase'] == 5 and state['world_revision'] >= 1:
                            break
                    except (FileNotFoundError, json.JSONDecodeError):
                        pass
                    time.sleep(.02)
                assert state and state['phase'] == 5 and len(state['members']) == 4
                if args.drafts: assert state['members'][state['local_member']]['strength'] == 9
                assert state['owner'] == (Path(game['runtime']) / 'arcmp-player.id').read_bytes().hex()
                assert len({(m['x'], m['y']) for m in state['members']}) == 4
                (args.output / f'live-initial-{index}.json').write_text(json.dumps(state, indent=2) + '\n')
                states.append(state)
            assert len({state['members'][state['local_member']]['id'] for state in states}) == 4
            for index, game in enumerate(games):
                focus(game['client']['address'])
                time.sleep(.3)
                client = client_for_pid(game['process'].pid)
                x, y = client['at']; width, height = client['size']
                run('grim', '-g', f'{x},{y} {width}x{height}', str(args.output / f'campaign-{index}.png'))
            if args.controls:
                controls(games, args.output)
            if args.hold:
                print(f'Launched party held for {args.hold} seconds in {args.output}', flush=True)
                until = time.monotonic() + args.hold
                while time.monotonic() < until:
                    assert all(game['process'].poll() is None for game in games)
                    time.sleep(.1)
        (args.output / "run.json").write_text(json.dumps({"instances": [
            {"pid": g["process"].pid, "runtime": g["runtime"], "client": g["client"]} for g in games],
            "campaign_launch_checked": args.launch, "dialogue_movement_chat_checked": args.controls,
            "loading_escape_and_f12_checked": args.loading_check,
            "native_character_creation_checked": args.custom_characters,
            "draft_edit_cancel_reload_and_process_restart_checked": args.drafts,
            "draft_save_failure_and_recovery_checked": args.drafts,
            "visual_assertions": "unreviewed; inspect each image"}, indent=2))
        print("Captured four game UIs in", args.output, "(visual inspection required)")
    except BaseException:
        if games and 'client' in games[-1] and games[-1]['process'].poll() is None:
            game = games[-1]
            focus(game['client']['address'])
            time.sleep(.2)
            client = client_for_pid(game['process'].pid)
            x, y = client['at']
            width, height = client['size']
            run('grim', '-g', f'{x},{y} {width}x{height}', str(args.output / 'failure.png'))
        raise
    finally:
        for game in games:
            process = game["process"]
            if process.poll() is None:
                # These are owned, disposable lobby instances. SDL maps TERM to
                # a confirmation dialog; KILL avoids leaving that dialog behind.
                process.kill()
                process.wait(timeout=5)
            game["log"].close()
        if original_active and any(c['address'] == original_active for c in json.loads(run('hyprctl', 'clients', '-j'))):
            dispatch(f"hl.dsp.focus({{window='address:{original_active}'}})")
        dispatch(f"hl.dsp.cursor.move({{x={original_cursor['x']}, y={original_cursor['y']}}})")
