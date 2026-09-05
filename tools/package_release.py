#!/usr/bin/env python3
"""Build and package the committed firmware, without flashing or including device data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'firmware/sparkdash'


def output(*args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path, help='new bundle directory (typically releases/...)')
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error('destination already exists; refusing overwrite')
    if output('git', 'status', '--porcelain'):
        parser.error('commit reviewed changes first; release source must be clean')
    sdk = Path(os.environ.get('IDF_PATH', ''))
    if not (sdk / 'tools/idf.py').is_file():
        parser.error('activate ESP-IDF 5.5.3 first')
    sdk_revision = output('git', 'rev-parse', 'HEAD', cwd=sdk)
    if sdk_revision != '2c211b236707889e8400c4dc5644dd5c4ee071e0':
        parser.error('ESP-IDF revision does not match the pinned 5.5.3 SDK')
    revision = output('git', 'rev-parse', 'HEAD')
    subprocess.run(['idf.py', '-C', str(PROJECT), 'build'], check=True)
    if output('git', 'status', '--porcelain'):
        parser.error('build changed tracked source/dependencies; review and commit before packaging')
    if 'CONFIG_SPARKDASH_TEST_COMMANDS=y' in (PROJECT / 'sdkconfig').read_text():
        parser.error('test commands must be disabled in a release bundle')
    build = PROJECT / 'build'
    description = json.loads((build / 'project_description.json').read_text())
    flash = json.loads((build / 'flasher_args.json').read_text())
    if description['target'] != 'esp32c6' or flash['flash_settings']['flash_size'] != '16MB':
        parser.error('unexpected build target or flash size')
    sources = {Path(name): build / name for name in flash['flash_files'].values()}
    for name in ['flash_args', 'flasher_args.json']:
        sources[Path(name)] = build / name
    for name in ['dependencies.lock', 'partitions.csv', 'sdkconfig', 'sdkconfig.defaults',
                 'PROVENANCE.md', 'README.md']:
        sources[Path(name)] = PROJECT / name
    sources[Path('validation.md')] = ROOT / 'docs/sparkdash-validation.md'
    sources[Path('interaction.md')] = ROOT / 'docs/interaction.md'
    for relative, source in sources.items():
        if relative.is_absolute() or '..' in relative.parts or not source.is_file():
            parser.error(f'invalid or missing build artifact: {relative}')
    destination.mkdir(parents=True)
    for relative, source in sources.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    metadata = {
        'application_revision': revision, 'sdk_revision': sdk_revision,
        'sdk_version': '5.5.3', 'application_version': description['project_version'],
        'target': description['target'], 'created_utc': datetime.now(timezone.utc).isoformat(),
        'soak_test': 'Excluded by user; not performed',
        'validation': 'See validation.md; packaging does not imply acceptance gates passed',
        'compiler': output(description['c_compiler'], '--version').splitlines()[0],
    }
    (destination / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (destination / 'FLASH.txt').write_text('''Activate ESP-IDF 5.5.3 and enumerate the known board from the source repository.
Close all serial monitors. Run from this bundle directory with the discovered PORT:
python -m esptool --chip esp32c6 --port PORT --before default_reset --after hard_reset write_flash @flash_args
The arguments and offsets come from the build, not the factory image. NVS is preserved.
Verify checksums first with: shasum -a 256 -c SHA256SUMS
This bundle contains no device credentials, NVS partition, raw logs, or factory backup.
The full factory backup remains separately stored in the source repository's ignored backups directory.
''')
    files = sorted(p for p in destination.rglob('*') if p.is_file())
    checksums = [f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(destination)}' for p in files]
    (destination / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
    print(f'Packaged {revision} to {destination}')


if __name__ == '__main__':
    main()
