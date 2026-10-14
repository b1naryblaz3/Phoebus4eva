#!/usr/bin/env python3
"""Package an explicit public source set and unsigned preview downloads."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_archive(destination: Path, members: dict[str, bytes], metadata: dict) -> None:
    manifest = dict(metadata)
    manifest['files'] = {name: {'bytes': len(data), 'sha256': sha(data)}
                         for name, data in sorted(members.items())}
    complete = dict(members)
    complete['MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(complete.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('Archive CRC verification failed')
        if set(archive.namelist()) != set(complete):
            raise RuntimeError('Unexpected archive members')
        for name, data in complete.items():
            if archive.read(name) != data:
                raise RuntimeError('Packaged bytes differ: ' + name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-commit', default=None, help='Full 40-character public Git commit')
    args = parser.parse_args()
    if args.source_commit and not re.fullmatch(r'[0-9a-fA-F]{40}', args.source_commit):
        raise ValueError('Source commit must be a full hexadecimal Git SHA')
    project = Path(__file__).resolve().parent.parent
    publication = json.loads((project / 'scripts/publication-files.json').read_text())
    version = publication['version']
    if not re.fullmatch(r'[0-9A-Za-z._-]+', version):
        raise ValueError('Unsafe version')
    names = publication['files']
    if len(set(names)) != len(names):
        raise ValueError('Duplicate publication paths')
    source: dict[str, bytes] = {}
    for name in names:
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or ':' in name or '\\' in name:
            raise ValueError('Unsafe source path')
        if relative.parts[0] in {'vendor', 'reports', 'dist', '.git'}:
            raise ValueError('Private/generated root is not publishable')
        path = (project / name).resolve()
        if not path.is_relative_to(project):
            raise ValueError('Source path escapes repository')
        source[name] = path.read_bytes()
        source[name].decode('utf-8')  # Public source entries are reviewable UTF-8 text.
    for name in ('README.md', 'LICENSE', 'SECURITY.md', 'THIRD_PARTY_NOTICES.md',
                 'scripts/verify-release.py', 'scripts/Collect-Phoebus.ps1'):
        if name not in source:
            raise ValueError('Required public file missing: ' + name)
    binary = {
        'PhoebusW11.sys': (project / 'dist/driver/PhoebusW11.sys').read_bytes(),
        'phoebus.inf': (project / 'dist/driver/phoebus.inf').read_bytes(),
        'dist/PhoebusProbe.exe': (project / 'dist/PhoebusProbe.exe').read_bytes(),
        'verify-release.py': source['scripts/verify-release.py'],
    }
    if binary['phoebus.inf'] != source['driver/phoebus.inf']:
        raise ValueError('Built INF differs from published source')
    for name in ('README.md', 'LICENSE', 'SECURITY.md', 'THIRD_PARTY_NOTICES.md',
                 'docs/build-driver.md', 'docs/driver-package.md', 'docs/driver-status.md',
                 'docs/testing.md', 'docs/validation.md', 'scripts/Collect-Phoebus.ps1'):
        binary[name] = source[name]
    metadata = {
        'version': version,
        'repository': publication['repository'],
        'source_commit': args.source_commit,
        'status': 'unsigned-experimental-not-hardware-tested',
        'signed_catalog_included': False,
        'vendor_payloads_included': False,
        'kernel_binary_sha256': sha(binary['PhoebusW11.sys']),
        'kernel_source_sha256': sha(source['driver/phoebus.cpp']),
        'source_files': {name: sha(data) for name, data in sorted(source.items())},
        'toolchain': {'msvc': '19.44.35229', 'sdk_wdk': '10.0.26100.6584'},
        'local_validation': {'protocol_checks': 9315, 'dma_checks': 34,
                             'position_tests': 'passed', 'kernel_compile': 'passed /W4 /WX',
                             'infverif': 'passed /w /v',
                             'inf2cat': 'passed /nocat 21H2/22H2/24H2 identifiers',
                             'hardware_runtime': 'not tested'},
        'limitations': ['unsigned; no normal Windows installation',
                        'no target playback, game fix, receiver, resume or kernel-stress validation',
                        'missing multichannel/capture/encoder and full board controls'],
    }
    destination = project / 'dist/releases'
    destination.mkdir(parents=True, exist_ok=True)
    base = 'Phoebus4eva-v' + version
    archives = [destination / (base + '-source.zip'), destination / (base + '-win11-x64.zip')]
    make_archive(archives[0], source, metadata)
    make_archive(archives[1], binary, metadata)
    sums = ''.join(sha(path.read_bytes()) + '  ' + path.name + '\n' for path in archives)
    (destination / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    (destination / 'release-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    for path in archives:
        print(json.dumps({'path': str(path), 'bytes': path.stat().st_size,
                          'sha256': sha(path.read_bytes())}))


if __name__ == '__main__':
    main()
