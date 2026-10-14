#!/usr/bin/env python3
"""Acquire pinned Microsoft native build components locally; installs no drivers."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import urllib.parse
import urllib.request
import zipfile
import zlib


def checksum(path: Path) -> str:
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def acquire(asset: dict, downloads: Path) -> Path:
    url = urllib.parse.urlparse(asset['url'])
    if url.scheme != 'https' or url.hostname not in {
        'download.visualstudio.microsoft.com', 'api.nuget.org'
    }:
        raise ValueError('Only pinned HTTPS Microsoft/NuGet origins are permitted')
    name = asset['name']
    if Path(name).name != name or ':' in name or '\\' in name:
        raise ValueError('Unsafe asset filename')
    target = downloads / name
    if target.exists() and target.stat().st_size == asset['size'] and checksum(target) == asset['sha256']:
        print(f'Verified cached {name}', flush=True)
        return target
    temporary = target.with_suffix(target.suffix + '.partial')
    request = urllib.request.Request(asset['url'], headers={'User-Agent': 'PhoebusW11-toolchain/1'})
    print(f'Downloading {name} ({asset["size"]:,} bytes)', flush=True)
    with urllib.request.urlopen(request, timeout=90) as response, temporary.open('wb') as output:
        shutil.copyfileobj(response, output, length=1024 * 1024)
    if temporary.stat().st_size != asset['size'] or checksum(temporary) != asset['sha256']:
        raise ValueError(f'Integrity mismatch: {name}; unverified partial file retained')
    temporary.replace(target)
    return target


def extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    destination = destination.resolve()
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            normalized = item.filename.replace('\\', '/')
            relative = Path(normalized)
            if relative.is_absolute() or ':' in normalized or '..' in relative.parts:
                raise ValueError(f'Unsafe archive member: {item.filename}')
            target = (destination / relative).resolve()
            if not target.is_relative_to(destination):
                raise ValueError(f'Archive member escapes toolchain directory: {item.filename}')
            if item.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                # Skip byte-identical files so reruns also work with compiler DLLs in use.
                if target.is_file() and target.stat().st_size == item.file_size:
                    existing_crc = 0
                    with target.open('rb') as existing:
                        while chunk := existing.read(1024 * 1024):
                            existing_crc = zlib.crc32(chunk, existing_crc)
                    if existing_crc == item.CRC:
                        continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with package.open(item) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, help='Destination; defaults to project vendor/native-tools')
    parser.add_argument('--verify-only', action='store_true', help='Verify cached package hashes without network or extraction')
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    lock_path = Path(__file__).with_name('native-toolchain.lock.json')
    lock = json.loads(lock_path.read_text(encoding='utf-8'))
    root = (args.root or project / 'vendor' / 'native-tools').resolve()
    downloads = root / 'downloads'
    downloads.mkdir(parents=True, exist_ok=True)
    if args.verify_only:
        for asset in lock['assets']:
            path = downloads / asset['name']
            if path.stat().st_size != asset['size'] or checksum(path) != asset['sha256']:
                raise ValueError(f'Integrity mismatch: {path}')
        print(f'PASS: {len(lock["assets"])} pinned Microsoft component packages')
        return
    # Extraction stays sequential because packages share the compiler/SDK trees.
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as workers:
        archives = list(workers.map(lambda asset: acquire(asset, downloads), lock['assets']))
    for asset, archive in zip(lock['assets'], archives):
        if asset['extract'] not in {'msvc', 'sdk', 'wdk'}:
            raise ValueError('Unknown extraction destination')
        print(f'Extracting {archive.name}', flush=True)
        extract(archive, root / asset['extract'])
    required = [
        root / lock['compiler_directory'] / 'bin/Hostx64/x64/cl.exe',
        root / lock['compiler_directory'] / 'bin/Hostx64/x64/link.exe',
        root / 'wdk/c/Include' / lock['kit_directory_version'] / 'km/hdaudio.h',
        root / 'wdk/c/Include' / lock['kit_directory_version'] / 'km/portcls.h',
        root / 'sdk/c/Include' / lock['kit_directory_version'] / 'shared/ksmedia.h',
    ]
    for path in required:
        if not path.is_file():
            raise ValueError(f'Extracted toolchain incomplete: {path}')
    print(f'Toolchain ready at {root}; no global PATH changes, system registration or driver installation.')


if __name__ == '__main__':
    main()
