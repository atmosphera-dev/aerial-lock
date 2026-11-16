#!/usr/bin/env python3
"""Stage and publish only the image exercised by a green master VM run."""
import argparse
from datetime import datetime, timezone
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from vm_platform import digest, validate_summary

IMAGE = 'aerial-ci-arch-x86_64.qcow2'
ASSETS = (IMAGE, 'image-manifest.json', 'image.lock.json', 'publication.json')


def require_master(ref):
    if ref != 'refs/heads/master':
        raise RuntimeError('image publication requires refs/heads/master')


def prepare_assets(image_dir, results, lock, revision, run_url):
    image_dir, results, lock = Path(image_dir), Path(results), Path(lock)
    manifest = json.loads((image_dir / 'image-manifest.json').read_text())
    summary = json.loads((results / 'summary.json').read_text())
    receipt = json.loads((results / 'instance.json').read_text())
    if not re.fullmatch('[a-f0-9]{40}', revision) or summary.get('source_revision') != revision:
        raise RuntimeError('tested source revision mismatch')
    image_hash = digest(image_dir / IMAGE)
    if manifest['image_sha256'] != image_hash or summary.get('image_sha256') != image_hash:
        raise RuntimeError('tested image checksum mismatch')
    validate_summary(summary, dict(uuid=receipt['uuid'], image_sha256=image_hash,
                                   source_freeze_identity=summary['source_identity']))
    if receipt.get('qemu_exit') != 0 or receipt.get('forced_shutdown'):
        raise RuntimeError('test VM did not stop cleanly')
    if digest(lock) != manifest['lock_sha256']:
        raise RuntimeError('tested image lock identity mismatch')
    shutil.copyfile(lock, image_dir / 'image.lock.json')
    descriptor = dict(schema=1, source_revision=revision, image_sha256=image_hash,
                      lock_sha256=digest(lock), run_url=run_url, group='niri-smoke',
                      planned=1, exercised=1, status='PASS')
    (image_dir / 'publication.json').write_text(json.dumps(descriptor, indent=2) + '\n')
    (image_dir / 'SHA256SUMS').write_text(''.join(digest(image_dir / name) + '  ' + name + '\n' for name in ASSETS))
    validate_assets(image_dir, revision)


def validate_assets(directory, revision):
    directory = Path(directory)
    for name in (*ASSETS, 'SHA256SUMS'):
        path = directory / name
        if not path.is_file() or path.is_symlink():
            raise RuntimeError('missing or unsafe publication asset: ' + name)
    expected = ''.join(digest(directory / name) + '  ' + name + '\n' for name in ASSETS)
    if (directory / 'SHA256SUMS').read_text() != expected:
        raise RuntimeError('publication asset checksum mismatch')
    descriptor = json.loads((directory / 'publication.json').read_text())
    manifest = json.loads((directory / 'image-manifest.json').read_text())
    if descriptor['source_revision'] != revision or descriptor['status'] != 'PASS' \
            or descriptor['group'] != 'niri-smoke' or descriptor['planned'] != 1 or descriptor['exercised'] != 1:
        raise RuntimeError('publication source/result mismatch')
    if descriptor['image_sha256'] != digest(directory / IMAGE) \
            or manifest['image_sha256'] != descriptor['image_sha256'] \
            or descriptor['lock_sha256'] != digest(directory / 'image.lock.json') \
            or manifest['lock_sha256'] != descriptor['lock_sha256']:
        raise RuntimeError('publication image/lock identity mismatch')
    if (directory / IMAGE).stat().st_size >= 2 * 1024 ** 3:
        raise RuntimeError('image exceeds GitHub release asset limit')
    return descriptor


def run_gh(arguments, check=True):
    result = subprocess.run(['gh', *arguments], capture_output=True, text=True)
    if check and result.returncode:
        raise RuntimeError('GitHub command failed: ' + result.stderr)
    return result


def existing_names(repo, prefix):
    refs = run_gh(['api', '--paginate', f'repos/{repo}/git/matching-refs/tags/{prefix}', '--jq', '.[].ref']).stdout
    releases = run_gh(['api', '--paginate', f'repos/{repo}/releases', '--jq', '.[].tag_name']).stdout
    return {line.removeprefix('refs/tags/') for line in (refs + '\n' + releases).splitlines()}


def reserve_tag(repo, revision, day):
    prefix = 'ci-image-' + day.strftime('%Y%m%d')
    existing = existing_names(repo, prefix)
    for suffix in itertools.count():
        tag = prefix + (f'-{suffix}' if suffix else '')
        if tag in existing:
            continue
        result = run_gh(['api', '--method', 'POST', f'repos/{repo}/git/refs',
                         '-f', 'ref=refs/tags/' + tag, '-f', 'sha=' + revision], check=False)
        if result.returncode == 0:
            return tag
        # Another run may have atomically reserved this name. Never move it.
        existing = existing_names(repo, prefix)
        if tag not in existing:
            raise RuntimeError('image tag reservation failed: ' + result.stderr)


def publish(directory, ref, repo, revision, day=None):
    require_master(ref)  # Before any filesystem read or GitHub write.
    descriptor = validate_assets(directory, revision)
    tag = reserve_tag(repo, revision, day or datetime.now(timezone.utc).date())
    notes = (f'Tested Arch CI image for source {revision}.\n\n'
             f'Image SHA256: {descriptor["image_sha256"]}\n'
             f'image.lock.json SHA256: {descriptor["lock_sha256"]}\n\n'
             f'Build and niri/PAM proof: {descriptor["run_url"]}\n'
             'Verify SHA256SUMS before booting. Not a product release.\n')
    run_gh(['release', 'create', tag, '--repo', repo, '--verify-tag', '--prerelease',
            '--latest=false', '--title', tag, '--notes', notes,
            *[str(Path(directory) / name) for name in (*ASSETS, 'SHA256SUMS')]])
    print('IMAGE_PRERELEASE_PUBLISHED ' + tag, flush=True)
    return tag


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    stage = commands.add_parser('prepare')
    stage.add_argument('--image-dir', required=True)
    stage.add_argument('--results', required=True)
    stage.add_argument('--lock', required=True)
    release = commands.add_parser('publish')
    release.add_argument('--directory', required=True)
    args = parser.parse_args()
    require_master(os.environ.get('GITHUB_REF'))
    revision = os.environ['GITHUB_SHA']
    if args.command == 'prepare':
        prepare_assets(args.image_dir, args.results, args.lock, revision,
            f'https://github.com/{os.environ["GITHUB_REPOSITORY"]}/actions/runs/{os.environ["GITHUB_RUN_ID"]}')
    else:
        publish(args.directory, os.environ['GITHUB_REF'], os.environ['GITHUB_REPOSITORY'], revision)


if __name__ == '__main__':
    main()
