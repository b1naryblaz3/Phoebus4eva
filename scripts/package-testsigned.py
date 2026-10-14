#!/usr/bin/env python3
"""Package an already signed development copy using explicit public allowlists.

No signing, target installation, certificate import or security changes occur.
"""
# SPDX-License-Identifier: MIT
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.0-test.1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def archive(path, contents, metadata):
    if path.exists():
        raise FileExistsError(path)
    manifest = dict(metadata, files={name: {'bytes': len(data), 'sha256': sha(data)}
                                    for name, data in sorted(contents.items())})
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED) as output:
        for name, data in sorted(contents.items()):
            if Path(name).suffix.lower() in {'.pfx', '.p12', '.key', '.pem'}:
                raise ValueError('Private-key format cannot be packaged')
            output.writestr(name, data)
        output.writestr('MANIFEST.json', json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--signed-directory', required=True, type=Path)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--output-directory', required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.source_commit):
        raise ValueError('Expected exact public source commit')
    args.output_directory.mkdir(parents=True, exist_ok=True)
    public = json.loads((ROOT / 'scripts/publication-files.json').read_text())['files']
    source = {name: (ROOT / name).read_bytes() for name in public}
    signed_names = ['PhoebusW11.sys', 'phoebus.inf', 'phoebus.cat',
                    'Phoebus4eva-Test.cer', 'test-signing.json']
    binary = {name: (args.signed_directory / name).read_bytes() for name in signed_names}
    signing = json.loads(binary['test-signing.json'].decode('utf-8-sig'))
    if signing['certificate_sha256'] != sha(binary['Phoebus4eva-Test.cer']):
        raise ValueError('Certificate differs from signing metadata')
    binary.update({name: data for name, data in source.items()
                   if name.startswith('docs/') or name in {
                       'README.md', 'LICENSE', 'SECURITY.md', 'THIRD_PARTY_NOTICES.md',
                       'scripts/New-PhoebusTestPackage.ps1', 'scripts/Verify-PhoebusTestSignature.ps1',
                       'scripts/verify-test-image.py', 'scripts/Collect-Phoebus.ps1'}})
    binary['verify-release.py'] = (ROOT / 'scripts/verify-release.py').read_bytes()
    binary['TESTING.md'] = (ROOT / 'docs/test-install-recovery.md').read_bytes()
    binary['dist/PhoebusProbe.exe'] = (ROOT / 'dist/PhoebusProbe.exe').read_bytes()
    metadata = dict(schema=1, version=VERSION, source_commit=args.source_commit,
                    binary_source_commit=signing['source_commit'],
                    status='self-signed development test only; no hardware validation',
                    signing=signing)
    assets = []
    for suffix, contents in [('win11-x64-testsigned', binary), ('source', source)]:
        output = args.output_directory / f'Phoebus4eva-{VERSION}-{suffix}.zip'
        archive(output, contents, metadata)
        assets.append(dict(name=output.name, bytes=output.stat().st_size,
                           sha256=sha(output.read_bytes())))
    (args.output_directory / 'SHA256SUMS-test.txt').write_text(
        ''.join(item['sha256'] + '  ' + item['name'] + '\n' for item in assets), encoding='utf-8')
    (args.output_directory / 'test-release-metadata.json').write_text(
        json.dumps(dict(metadata, assets=assets), indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(source_files=len(source), binary_files=len(binary), assets=assets), indent=2))


if __name__ == '__main__':
    main()
