#!/usr/bin/env python3
"""Freeze, execute and merge master acceptance slices in disposable KVM VMs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time
from types import SimpleNamespace
import xml.etree.ElementTree as ET

COMPOSITORS = ('niri',)
GROUPS = ('smoke', 'pam', 'emergency-recovery', 'emergency-failures')
FAULTS = ('missing-qs', 'missing-plugin', 'emergency-retry', 'helper-missing', 'external-release',
          'emergency-missing', 'emergency-exhausted', 'compositor-death')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name('.' + path.name + '.pending')
    pending.write_text(json.dumps(value, indent=2) + '\n')
    pending.replace(path)


def cases(backend, group):
    if backend not in COMPOSITORS: raise ValueError('unsupported CI compositor')
    return {'smoke': ('unlock',), 'pam': ('wrong-right',),
            'emergency-recovery': ('missing-runner', 'main-crash'),
            'emergency-failures': FAULTS}[group]


def matrix():
    return [dict(slice=f'{backend}-{group}', backend=backend, group=group,
                 capability='nested-one-output-software',
                 cells=[f'{backend}/{group}/{case}' for case in cases(backend, group)])
            for backend in COMPOSITORS for group in GROUPS]


def freeze(source, revision, run, attempt):
    import vm_platform
    source = Path(source).resolve(strict=True)
    inventory = vm_platform.source_files(source)
    tools = {name: digest(source / 'Tests/vm' / name) for name in ('vm_platform.py', 'provision.sh', 'guest.py')}
    return dict(schema=1, source_revision=revision, source_identity=identity(inventory), run=str(run), attempt=str(attempt),
                image_specification=identity(dict(tools=tools, lock=digest(source / 'Tests/vm/image.lock.json'))),
                image_tools=tools, image_lock_sha256=digest(source / 'Tests/vm/image.lock.json'),
                matrix=matrix())


def validate_plan(plan):
    if not isinstance(plan, dict) or plan.get('schema') != 1 or plan.get('matrix') != matrix():
        raise ValueError('unknown schema or changed acceptance matrix')
    for key in ('source_revision', 'source_identity', 'image_specification', 'run', 'attempt'):
        if not isinstance(plan.get(key), str) or not plan[key]:
            raise ValueError('missing plan identity: ' + key)


def require(value, *keys):
    if any(value.get(key) is not True for key in keys):
        raise ValueError('missing consumer observation: ' + ', '.join(keys))


def validate_outcome(backend, group, case, value):
    if case not in cases(backend, group): raise ValueError('unplanned consumer case')
    if value.get('status') != 'PASS':
        raise ValueError('non-PASS consumer outcome')
    if group in ('smoke', 'pam'):
        require(value, 'unlock_and_destroy', 'desktop_input')
        if value.get('exit') != 0: raise ValueError('abnormal unlock exit')
        if group == 'pam': require(value, 'wrong_rejected', 'lock_retained')
    elif group.startswith('emergency-'):
        if case in ('emergency-missing', 'emergency-exhausted'):
            require(value, 'no_automatic_unlock', 'protection_previously_confirmed')
            if value.get('exit') != 3: raise ValueError('wrong exhausted-recovery exit')
        elif case == 'compositor-death':
            require(value, 'compositor_dead')
            if value.get('exit') != 0: raise ValueError('wrong compositor-death exit')
        else:
            require(value, 'unlock_and_destroy', 'sync_confirmed', 'desktop_input', 'no_lock_time_desktop_input')
            if value.get('exit') != 0: raise ValueError('abnormal emergency exit')
            if case != 'external-release': require(value, 'real_pam')
            if group == 'emergency-recovery': require(value, 'rejected')
            if case == 'main-crash':
                if len(set(value.get('main_pids', []))) != 3: raise ValueError('three distinct crashes absent')
                frames = value.get('sampled_frames') or {}
                if frames.get('error') is not None or not isinstance(frames.get('samples'), int) or frames['samples'] < 1:
                    raise ValueError('recovery frame observations absent')
                if frames.get('samples_with_desktop_marker') != 0:
                    raise ValueError('niri desktop exposed during recovery')


def validate_group(summary, request):
    backend, group = request['backend'], request['group']
    expected = {f'{backend}/{group}/{case}' for case in cases(backend, group)}
    cells = summary.get('cells', [])
    if summary.get('status') != 'PASS' or summary.get('backend') != backend or summary.get('group') != group \
            or summary.get('planned') != len(expected) or summary.get('exercised') != len(expected) \
            or summary.get('no_surviving_locker') is not True or len(cells) != len(expected) \
            or {cell.get('id') for cell in cells} != expected:
        raise ValueError('incomplete/mixed acceptance group')
    for cell in cells:
        case = cell['id'].split('/')[-1]
        validate_outcome(backend, group, case, cell['outcome'])
        if group.startswith('emergency-'):
            outcome = cell['outcome']
            if outcome.get('backend') != backend or outcome.get('instance_uuid') != request.get('uuid') \
                    or outcome.get('source_identity') != request.get('source_freeze_identity'):
                raise ValueError('stale/mixed emergency consumer identity')


def validate_image(plan, root, image_sha256):
    manifest = json.loads((Path(root) / 'image-manifest.json').read_text())
    lock = Path(root) / 'resolved-lock.json'
    if manifest.get('image_sha256') != image_sha256 or manifest.get('lock_sha256') != digest(lock) \
            or digest(lock) != plan['image_lock_sha256'] or manifest.get('tools') != plan['image_tools']:
        raise ValueError('image provenance differs from frozen tools/lock/bytes')


def collect_image(image, results):
    """Retain preparation failures without uploading images, keys or seeds."""
    target = Path(results) / 'image-build'
    target.mkdir(parents=True, exist_ok=True)
    for name in ('provision.log', 'provision-observation.json', 'failure.json', 'instance.json',
                 'qemu.log', 'serial.log', 'ssh-errors.log', 'image-manifest.json', 'resolved-lock.json'):
        source = Path(image) / name
        if source.is_file() and not source.is_symlink():
            shutil.copyfile(source, target / name)


def result(plan, slice_id, directory, output):
    validate_plan(plan)
    rows = [row for row in plan['matrix'] if row['slice'] == slice_id]
    if len(rows) != 1: raise ValueError('unplanned slice')
    row = rows[0]
    root = Path(directory)
    value = dict(schema=1, plan_identity=identity(plan), slice=slice_id, status='FAIL', cells=[], error=None)
    try:
        summary = json.loads((root / 'summary.json').read_text())
        receipt = json.loads((root / 'instance.json').read_text())
        request = json.loads((root / 'request.json').read_text())
        if request.get('uuid') != receipt['uuid'] or request.get('image_sha256') != receipt.get('image_sha256') \
                or request.get('source_freeze_identity') != plan['source_identity'] \
                or request.get('backend') != row['backend'] or request.get('group') != row['group']:
            raise ValueError('mixed request/instance identity')
        import vm_platform
        vm_platform.validate_result_identity(summary, request)
        if summary.get('source_revision') != plan['source_revision'] or summary.get('acceptance_plan') != identity(plan):
            raise ValueError('stale plan/source revision')
        validate_group(summary, request)
        validate_image(plan, root, summary['image_sha256'])
        if receipt.get('forced_shutdown') or receipt.get('qemu_exit') != 0:
            raise ValueError('guest disposal incomplete')
        if (root / 'failure.json').exists() or (root / 'export-error.json').exists() or not (root / 'diagnostics.tar.gz').is_file():
            raise ValueError('slice infrastructure/export failed')
        value.update(status='PASS', cells=summary['cells'], image_sha256=summary['image_sha256'],
                     instance_uuid=receipt['uuid'], elapsed_seconds=summary['elapsed_seconds'])
    except Exception as error:
        value['error'] = str(error)
    files = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink(): raise ValueError('evidence symlink refused')
        if path.is_file() and path.resolve() != Path(output).resolve():
            files[str(path.relative_to(root))] = digest(path)
    value['evidence'] = files
    write(output, value)
    return value


def merge(plan, inputs, output, matrix_status='success'):
    validate_plan(plan)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    expected = {row['slice']: row for row in plan['matrix']}
    found, errors, cells, uuids = {}, [], [], set()
    for path in sorted(Path(inputs).rglob('slice.json')):
        value = None
        try:
            value = json.loads(path.read_text())
            name = value['slice']
            if name not in expected or name in found: raise ValueError('unexpected/duplicate slice: ' + name)
            found[name] = value
            if value.get('schema') != 1 or value.get('plan_identity') != identity(plan): raise ValueError('stale plan identity')
            if value.get('status') != 'PASS': raise ValueError('non-PASS slice: ' + str(value.get('error')))
            if not value.get('evidence'): raise ValueError('missing evidence')
            required = {'summary.json', 'request.json', 'instance.json', 'diagnostics.tar.gz',
                        'image-manifest.json', 'resolved-lock.json'}
            if required - value['evidence'].keys() or {'failure.json', 'export-error.json', 'stop-error.json',
                    'diagnostic-error.json'} & value['evidence'].keys():
                raise ValueError('incomplete or failed slice evidence')
            for relative, expected_hash in value['evidence'].items():
                evidence = path.parent / relative
                if not evidence.resolve().is_relative_to(path.parent.resolve()) or evidence.is_symlink() or digest(evidence) != expected_hash:
                    raise ValueError('evidence checksum/containment mismatch: ' + relative)
            # Revalidate the actual evidence, not just a caller's PASS claim.
            observed = json.loads((path.parent / 'summary.json').read_text())
            request = dict(backend=expected[name]['backend'], group=expected[name]['group'],
                           uuid=value['instance_uuid'], source_freeze_identity=plan['source_identity'])
            validate_group(observed, request)
            if observed.get('acceptance_plan') != identity(plan) or observed.get('source_identity') != plan['source_identity'] \
                    or observed.get('source_revision') != plan['source_revision'] or observed.get('cells') != value.get('cells') \
                    or observed.get('instance_uuid') != value.get('instance_uuid') or observed.get('image_sha256') != value.get('image_sha256'):
                raise ValueError('stale/mixed source/image/instance evidence')
            receipt = json.loads((path.parent / 'instance.json').read_text())
            validate_image(plan, path.parent, value['image_sha256'])
            request = json.loads((path.parent / 'request.json').read_text())
            if receipt.get('uuid') != value['instance_uuid'] or receipt.get('image_sha256') != value['image_sha256'] \
                    or receipt.get('forced_shutdown') or receipt.get('qemu_exit') != 0 \
                    or request.get('uuid') != receipt['uuid'] or request.get('image_sha256') != receipt['image_sha256'] \
                    or request.get('acceptance_plan') != identity(plan) or request.get('source_freeze_identity') != plan['source_identity'] \
                    or request.get('source_revision') != plan['source_revision'] \
                    or request.get('backend') != expected[name]['backend'] or request.get('group') != expected[name]['group']:
                raise ValueError('incomplete instance disposal')
            if value['instance_uuid'] in uuids: raise ValueError('shared instance between slices')
            uuids.add(value['instance_uuid'])
            if [cell['id'] for cell in value['cells']] != expected[name]['cells']: raise ValueError('wrong cell coverage/order')
            cells.extend(value['cells'])
        except Exception as error:
            errors.append(f'{path.parent.name}: {error}')
            if isinstance(value, dict) and found.get(value.get('slice')) is value:
                value['validation_status'] = 'INVALID'
    for missing in sorted(expected.keys() - found.keys()): errors.append('missing slice: ' + missing)
    if matrix_status != 'success': errors.append('matrix job status: ' + matrix_status)
    planned = [cell for row in plan['matrix'] for cell in row['cells']]
    if len(cells) != len(planned) or {cell['id'] for cell in cells} != set(planned): errors.append('incomplete exact cell coverage')
    summary = dict(schema=1, status='FAIL' if errors else 'PASS', plan_identity=identity(plan),
                   source_revision=plan['source_revision'], planned=len(planned), exercised=len(cells),
                   slices=len(found), expected_slices=len(expected), errors=errors, cells=cells,
                   durations={name: value.get('elapsed_seconds') for name, value in found.items()},
                   images={name: value.get('image_sha256') for name, value in found.items()})
    write(output / 'summary.json', summary)
    suite = ET.Element('testsuite', name='aerial-lock.acceptance', tests=str(len(planned)))
    valid = {cell['id'] for cell in cells}
    for name in planned:
        test = ET.SubElement(suite, 'testcase', name=name, classname='aerial-lock.acceptance')
        if name not in valid: ET.SubElement(test, 'failure', message='Missing or invalid slice').text = '\n'.join(errors)
    if errors and len(valid) == len(planned):
        ET.SubElement(suite, 'testcase', name='merge-contract').append(ET.Element('failure', message='; '.join(errors)))
    suite.set('failures', str(len(suite.findall('./testcase/failure'))))
    ET.ElementTree(suite).write(output / 'junit.xml', encoding='utf-8', xml_declaration=True)
    text = f'# VM acceptance: {summary["status"]}\n\nSource: `{plan["source_revision"]}`\n\n'
    text += f'Slices: {len(found)}/{len(expected)}; valid cells: {len(cells)}/{len(planned)}.\n\n'
    text += '| # | Slice | Status | Guest seconds |\n|---|---|---|---|\n'
    for index, name in enumerate(expected, 1):
        value = found.get(name, {})
        text += f'| {index} | {name} | {value.get("validation_status", value.get("status", "MISSING"))} | {value.get("elapsed_seconds", "-")} |\n'
    if errors: text += '\nErrors:\n\n' + '\n'.join('- ' + error for error in errors) + '\n'
    text += '\nSoftware nested niri only. Native niri output/power groups and Hyprland require separate manual pre-release acceptance on GPU-backed Ark VMs. No IME or physical power proof.\n'
    (output / 'summary.md').write_text(text)
    return summary


def run_slice(args, plan):
    import vm_platform
    row = next(row for row in plan['matrix'] if row['slice'] == args.slice)
    if freeze(args.source, plan['source_revision'], plan['run'], plan['attempt']) != plan:
        raise ValueError('source/image/tool drift after planning')
    args.group, args.backend, args.source_revision = row['group'], row['backend'], plan['source_revision']
    args.acceptance_plan = identity(plan)
    image_root = Path(args.image).resolve().parent
    validate_image(plan, image_root, args.sha256)
    cfg = vm_platform.config(args.config)
    Path(cfg['storage']).mkdir(parents=True, exist_ok=True)
    try:
        vm_platform.run_group(args, cfg)
    finally:
        if Path(args.results).is_dir():
            for name in ('image-manifest.json', 'resolved-lock.json'):
                (Path(args.results) / name).write_bytes((image_root / name).read_bytes())


def local(args):
    """Serial local reproduction, with one new instance per planned slice."""
    root = Path(args.workspace).resolve(strict=True) / ('local-' + str(time.time_ns()))
    root.mkdir(mode=0o700)
    plan = freeze(args.source, args.revision, root.name, '1')
    write(root / 'plan.json', plan)
    image = Path(args.image).resolve(strict=True)
    checksums = [line.split() for line in Path(args.checksums).read_text().splitlines()]
    matches = [parts[0] for parts in checksums if len(parts) == 2 and parts[1] == image.name]
    if len(matches) != 1 or digest(image) != matches[0]: raise ValueError('image checksum mismatch')
    config = json.loads(Path(args.config).read_text())
    errors = []
    for index, row in enumerate(plan['matrix']):
        name = row['slice']
        cfg = dict(config, instance='acceptance-' + name,
                   storage=str(Path(args.workspace).resolve() / 'v' / identity(str(root))[:8] / str(index)))
        cfg_path = root / (name + '.json')
        write(cfg_path, cfg)
        result_path = root / 'slices' / name
        try:
            run_slice(SimpleNamespace(source=args.source, slice=name, config=str(cfg_path), image=str(image),
                sha256=matches[0], results=str(result_path)), plan)
        except Exception as error:
            errors.append(name + ': ' + str(error))
        result_path.mkdir(parents=True, exist_ok=True)
        result(plan, name, result_path, result_path / 'slice.json')
        interim = merge(plan, root / 'slices', root / 'merged', 'failure' if errors else 'success')
        print(f'LOCAL_PROGRESS {name}: {interim["exercised"]}/{interim["planned"]} valid cells; {root}', flush=True)
    return merge(plan, root / 'slices', root / 'merged', 'failure' if errors else 'success')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    plan = commands.add_parser('plan')
    for name in ('source', 'revision', 'run', 'attempt', 'output'): plan.add_argument('--' + name, required=True)
    run = commands.add_parser('run-slice')
    for name in ('plan', 'slice', 'source', 'config', 'image', 'sha256', 'results'): run.add_argument('--' + name, required=True)
    record = commands.add_parser('result')
    for name in ('plan', 'slice', 'results'): record.add_argument('--' + name, required=True)
    collect = commands.add_parser('collect-image')
    for name in ('image', 'results'): collect.add_argument('--' + name, required=True)
    join = commands.add_parser('merge')
    for name in ('plan', 'inputs', 'output'): join.add_argument('--' + name, required=True)
    join.add_argument('--matrix-status', default='success')
    reproduce = commands.add_parser('local')
    for name in ('workspace', 'source', 'config', 'image', 'checksums'): reproduce.add_argument('--' + name, required=True)
    reproduce.add_argument('--revision', default='local-dirty')
    args = parser.parse_args()
    if args.command == 'collect-image':
        collect_image(args.image, args.results)
        return 0
    if args.command == 'plan':
        value = freeze(args.source, args.revision, args.run, args.attempt)
        write(args.output, value)
        print(json.dumps(dict(include=[{k: row[k] for k in ('slice', 'backend', 'group')} for row in value['matrix']])))
        return 0
    if args.command == 'local':
        return 0 if local(args)['status'] == 'PASS' else 1
    try:
        plan = json.loads(Path(args.plan).read_text())
        validate_plan(plan)
    except (OSError, ValueError, KeyError, TypeError) as error:
        if args.command != 'merge': raise
        plan = freeze(Path.cwd(), os.environ.get('GITHUB_SHA', 'invalid-plan'),
                      os.environ.get('GITHUB_RUN_ID', 'missing-run'), os.environ.get('GITHUB_RUN_ATTEMPT', 'missing-attempt'))
        args.matrix_status = 'invalid-plan: ' + type(error).__name__
    if args.command == 'run-slice': run_slice(args, plan); return 0
    if args.command == 'result':
        value = result(plan, args.slice, args.results, Path(args.results) / 'slice.json')
    else:
        value = merge(plan, args.inputs, args.output, args.matrix_status)
        print((Path(args.output) / 'summary.md').read_text())
        if os.environ.get('GITHUB_STEP_SUMMARY'):
            with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream: stream.write((Path(args.output) / 'summary.md').read_text())
    return 0 if value['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
