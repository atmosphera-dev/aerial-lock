#!/usr/bin/env python3
"""Exercise the real supervisor against a fake Hyprland option boundary."""
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
from private_runtime import PrivateRuntime

SUPERVISOR = Path(sys.argv[1]).resolve(strict=True)

HYPRCTL = r'''#!/usr/bin/python3
import json, os, signal, sys, time
from pathlib import Path
root = Path(os.environ['HYPR_FIXTURE'])
case = json.loads((root / 'case.json').read_text())
state = json.loads((root / 'state.json').read_text())
args = sys.argv[1:]
with (root / 'calls.jsonl').open('a') as log:
    log.write(json.dumps(args) + '\n')
def reply(text, code=0):
    print(text)
    raise SystemExit(code)
if args == ['-j', 'status']:
    if case['name'] == 'status-absent': reply('unknown request')
    if case['name'] == 'provider-field-absent': reply('{}')
    if case['name'] == 'malformed-status': reply('{broken')
    if case['name'] == 'unknown-provider': reply('{"configProvider":"other"}')
    reply(json.dumps({'configProvider': case['provider']}))
if args == ['-j', 'getoption', 'misc:allow_session_lock_restore']:
    if case['name'] == 'malformed-readback': reply('{broken')
    if case['name'] == 'wrong-option': reply('{"option":"misc:unrelated","bool":true}')
    if case['name'] == 'wrong-readback-type': reply('{"option":"misc:allow_session_lock_restore","bool":"true"}')
    key = 'bool' if case['provider'] == 'lua' else 'int'
    value = state['value'] if key == 'bool' else int(state['value'])
    reply(json.dumps({'option':'misc:allow_session_lock_restore', key:value}))
if args[:1] == ['eval']:
    assert case['provider'] == 'lua', args
    assert args[1] in ('hl.config({misc={allow_session_lock_restore=true}})',
                       'hl.config({misc={allow_session_lock_restore=false}})'), args
    on = '=true' in args[1]
elif args[:2] == ['keyword', 'misc:allow_session_lock_restore']:
    if case['provider'] == 'lua':
        reply('unknown request' if case['name'] == 'unknown-request' else
              "keyword can't work with non-legacy parsers. Use eval.")
    assert args[2] in ('1', '0'), args
    on = args[2] == '1'
else:
    raise AssertionError('unexpected command: ' + repr(args))
if on:
    state['enables'] += 1
    (root / 'state.json').write_text(json.dumps(state))
name = case['name']
if on and name == 'lua-rejection': reply("keyword can't work with non-legacy parsers. Use eval.")
if on and name == 'unknown-request': reply('unknown request')
if on and name == 'empty-response': reply('')
if on and name == 'nonzero-exit': reply('ok', 1)
if on and name == 'crashed-command': os.kill(os.getpid(), signal.SIGKILL)
if on and name == 'timeout-command': time.sleep(10)
if on and name == 'retry-after-failure' and state['enables'] == 1: reply('unknown request')
if not on and name == 'cleanup-failure': reply('unknown request')
if not (on and name == 'wrong-readback') and not (not on and name == 'cleanup-wrong-readback'):
    state['value'] = on
(root / 'state.json').write_text(json.dumps(state))
reply('ok')
'''

RUNNER = r'''#!/usr/bin/python3
import json, os, sys
from pathlib import Path
root = Path(os.environ['HYPR_FIXTURE'])
case = json.loads((root / 'case.json').read_text())
state = json.loads((root / 'state.json').read_text())
with (root / 'launches.jsonl').open('a') as log:
    log.write(json.dumps({'value':state['value']}) + '\n')
launches = (root / 'launches.jsonl').read_text().splitlines()
if len(launches) == 1 and case['name'] in ('retry-after-failure', 'reset-between-attempts'):
    if case['name'] == 'reset-between-attempts':
        state['value'] = False
        (root / 'state.json').write_text(json.dumps(state))
    raise SystemExit(70)
'''

CASES = (
    ('legacy-ok', 'hyprlang', True),
    ('lua-ok', 'lua', True),
    ('status-absent', 'hyprlang', True),
    ('provider-field-absent', 'hyprlang', True),
    ('lua-rejection', 'lua', False),
    ('unknown-request', 'lua', False),
    ('malformed-status', 'lua', False),
    ('unknown-provider', 'lua', False),
    ('malformed-readback', 'lua', False),
    ('wrong-readback', 'lua', False),
    ('wrong-option', 'lua', False),
    ('wrong-readback-type', 'lua', False),
    ('empty-response', 'lua', False),
    ('nonzero-exit', 'lua', False),
    ('crashed-command', 'lua', False),
    ('timeout-command', 'lua', False),
    ('cleanup-failure', 'lua', True),
    ('cleanup-wrong-readback', 'lua', True),
    ('retry-after-failure', 'lua', True),
    ('reset-between-attempts', 'lua', True),
)


def records(path):
    return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []


with tempfile.TemporaryDirectory(prefix='aerial-hyprland-') as directory, \
        PrivateRuntime('bus', 'wayland') as runtime:
    root = Path(directory)
    bin_dir = root / 'bin'
    bin_dir.mkdir()
    hyprctl = bin_dir / 'hyprctl'
    hyprctl.write_text(HYPRCTL)
    hyprctl.chmod(0o700)
    runner = root / 'runner'
    runner.write_text(RUNNER)
    runner.chmod(0o700)
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(str(runtime / 'wayland'))
    listener.listen(64)
    bus = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1',
                            '--address=unix:path=' + str(runtime / 'bus')],
                           stdout=subprocess.PIPE, text=True)
    failures = []
    try:
        assert select.select([bus.stdout], [], [], 10)[0], 'private bus did not start'
        address = bus.stdout.readline().strip()
        for name, provider, success in CASES:
            fixture = root / name
            fixture.mkdir()
            (fixture / 'case.json').write_text(json.dumps(dict(name=name, provider=provider)))
            (fixture / 'state.json').write_text(json.dumps(dict(value=False, enables=0)))
            env = {key: value for key, value in os.environ.items()
                   if key not in ('WAYLAND_SOCKET', 'AERIAL_LOCK_VIDEO_POOL', 'AERIAL_LOCK_ENGAGEMENT_SOCKET')}
            env.update(PATH=str(bin_dir) + os.pathsep + env['PATH'], HYPR_FIXTURE=str(fixture),
                       HYPRLAND_INSTANCE_SIGNATURE='fixture', WAYLAND_DISPLAY='wayland',
                       XDG_RUNTIME_DIR=str(runtime), DBUS_SESSION_BUS_ADDRESS=address,
                       AERIAL_LOCK_RUNNER=str(runner), AERIAL_LOCK_SHELL=str(root),
                       AERIAL_LOCK_PLUGIN_ROOT=str(root))
            process = subprocess.run([str(SUPERVISOR)], env=env, capture_output=True, text=True, timeout=25)
            output = process.stdout + process.stderr
            try:
                assert process.returncode == 0, (process.returncode, output)
                calls = records(fixture / 'calls.jsonl')
                launches = records(fixture / 'launches.jsonl')
                final = json.loads((fixture / 'state.json').read_text())
                assert final['value'] == (name in ('cleanup-failure', 'cleanup-wrong-readback')), final
                assert launches, output
                assert all(call[0] in ('-j', 'eval', 'keyword') for call in calls), calls
                enabled = 'allow_session_lock_restore enabled and verified' in output
                assert 'allow_session_lock_restore set just-in-time' not in output, output
                cleared = 'allow_session_lock_restore cleared and verified' in output
                assert enabled == success, output
                if not success or name == 'retry-after-failure':
                    assert 'cannot enable misc:allow_session_lock_restore' in output, output
                    assert 'TTY' in output, output
                if name in ('cleanup-failure', 'cleanup-wrong-readback', 'malformed-readback',
                            'wrong-option', 'wrong-readback-type', 'malformed-status', 'unknown-provider'):
                    assert not cleared, output
                    assert 'cannot clear misc:allow_session_lock_restore' in output, output
                else:
                    assert cleared, output
                if success and name != 'retry-after-failure':
                    assert all(launch['value'] for launch in launches), launches
                if name == 'retry-after-failure':
                    assert [launch['value'] for launch in launches] == [False, True], launches
                if name == 'reset-between-attempts':
                    assert len(launches) == 2 and all(launch['value'] for launch in launches), launches
                if name in ('retry-after-failure', 'reset-between-attempts'):
                    assert sum(call == ['-j', 'status'] for call in calls) == 3, calls
                if name in ('lua-rejection', 'unknown-request', 'empty-response', 'nonzero-exit',
                            'crashed-command', 'timeout-command', 'wrong-readback'):
                    assert not launches[0]['value'], launches
                print('PASS ' + name, flush=True)
            except AssertionError as error:
                failures.append(name)
                print('FAIL ' + name + ': ' + str(error), flush=True)
    finally:
        bus.terminate()
        bus.wait(timeout=10)
        listener.close()
    assert not failures, failures
    print(f'{len(CASES)} Hyprland supervisor contract cases passed')
