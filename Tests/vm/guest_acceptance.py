#!/usr/bin/env python3
"""Software niri acceptance adapters for the existing real-PAM drivers."""
import json
import os
import subprocess
import sys
import time
from types import SimpleNamespace

import acceptance


def run(summary, source, root, paths, manifest, env, request, driver):
    backend, group = request['backend'], request['group']
    if backend != 'niri' or group not in acceptance.GROUPS:
        raise RuntimeError('software niri acceptance group required')
    summary.update(backend=backend, acceptance_plan=request['acceptance_plan'], cells=[])

    def record(case, outcome):
        acceptance.validate_outcome(backend, group, case, outcome)
        summary['cells'].append(dict(id=f'{backend}/{group}/{case}', outcome=outcome))
        summary['exercised'] = len(summary['cells'])
        acceptance.write(paths['results'] / 'acceptance-progress.json', summary)

    if group in ('smoke', 'pam'):
        args = SimpleNamespace(root=str(root), vm_record=str(manifest), backend=backend, case=group,
            outputs=1, lane='debug', pointer_helper=None, recover_owned_failures=False)
        session = driver.Session(args)
        old = dict(os.environ)
        os.environ.update(env)
        try:
            session.start()
            session.config(media=False)
            if session.keyboard is None:
                session.keyboard = subprocess.Popen(['wtype', '-s', '600000'], env=session.env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            session.launch(1)
            time.sleep(2)  # Same mapped-editor settling leg as remediation unlock.
            if group == 'pam':
                stamp = session.type_and_submit(wrong=True)
                if session.locker.wait_for(lambda line: ' -> failed reason=pam-failed' in line, after=stamp) is None:
                    raise RuntimeError('bounded wrong-password rejection absent')
                if session.locker.process.poll() is not None or '.unlock_and_destroy()' in session.locker.log.read_text():
                    raise RuntimeError('wrong password did not retain lock')
            session.unlock(1)
            outcome = dict(session.results[-1], status='PASS')
            if group == 'pam': outcome.update(wrong_rejected=True, lock_retained=True)
            record('wrong-right' if group == 'pam' else 'unlock', outcome)
        finally:
            session.cleanup()
            os.environ.clear(); os.environ.update(old)
    else:
        case = 'recovery' if group == 'emergency-recovery' else 'failures'
        before = set(paths['results'].glob(f'{case}-{backend}-*/outcomes.json'))
        argv = [sys.executable, str(source / 'Tests/emergency-integration.py'), '--root', str(root),
                '--vm-record', str(manifest), '--backend', backend, '--case', case]
        with (paths['results'] / (group + '.log')).open('w') as stream:
            subprocess.run(argv, env=env, check=True, timeout=request['group_seconds'], stdout=stream, stderr=subprocess.STDOUT)
        fresh = set(paths['results'].glob(f'{case}-{backend}-*/outcomes.json')) - before
        if len(fresh) != 1: raise RuntimeError('missing fresh emergency result')
        outcomes = json.loads(next(iter(fresh)).read_text())
        if [value['case'] for value in outcomes] != list(acceptance.cases(backend, group)):
            raise RuntimeError('wrong emergency case coverage')
        for value in outcomes:
            if value.get('instance_uuid') != request['uuid'] or value.get('source_identity') != request['source_freeze_identity']:
                raise RuntimeError('stale emergency outcome')
            record(value['case'], value)
    summary.update(status='PASS', phase='complete', no_surviving_locker=True)
