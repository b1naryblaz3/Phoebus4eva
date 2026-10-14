#!/usr/bin/env python3
"""Verify an extracted release manifest without executing its contents."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, nargs='?', default=Path.cwd())
    args = parser.parse_args()
    root = args.directory.resolve()
    manifest = json.loads((root / 'MANIFEST.json').read_text(encoding='utf-8'))
    entries = manifest['files']
    if not isinstance(entries, dict) or not entries:
        raise ValueError('Manifest has no file entries')
    actual_files = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual_files != set(entries) | {'MANIFEST.json'}:
        raise ValueError('Extracted files differ from the manifest; verify a fresh extraction')
    for name, expected in entries.items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or ':' in name or '\\' in name:
            raise ValueError('Unsafe manifest path')
        path = (root / name).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError('Missing or escaping file: ' + name)
        if path.stat().st_size != expected['bytes']:
            raise ValueError('Size mismatch: ' + name)
        with path.open('rb') as source:
            actual = hashlib.file_digest(source, 'sha256').hexdigest()
        if actual != expected['sha256']:
            raise ValueError('SHA-256 mismatch: ' + name)
    print(f'PASS: {len(entries)} listed files match the unsigned manifest. No code executed.')


if __name__ == '__main__':
    main()
