"""Check a PE32+ image's Authenticode digest against verified CMS content.

The caller must first cryptographically verify the CMS signature; this script
alone does not authenticate the supplied content or establish certificate trust.
This intentionally accepts only the contiguous section layout used by this build.
"""
import hashlib
from pathlib import Path
import struct
import sys


def der_items(data):
    offset = 0
    result = []
    while offset < len(data):
        tag, size = data[offset:offset + 2]
        offset += 2
        if size & 128:
            count = size & 127
            if not 1 <= count <= 4:
                raise ValueError('Unsupported DER length')
            size = int.from_bytes(data[offset:offset + count], 'big')
            offset += count
        end = offset + size
        if end > len(data):
            raise ValueError('Truncated DER')
        result.append((tag, data[offset:end]))
        offset = end
    return result


def check(image, cms_content):
    data = Path(image).read_bytes()
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Not a PE')
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected PE32+')
    headers_size = struct.unpack_from('<I', data, optional + 60)[0]
    checksum = optional + 64
    security = optional + 112 + 4 * 8
    cert_offset, cert_size = struct.unpack_from('<II', data, security)
    if not (security + 8 <= headers_size <= cert_offset and cert_offset + cert_size == len(data)):
        raise ValueError('Unsupported certificate/header layout')
    digest = hashlib.sha256()
    digest.update(data[:checksum])
    digest.update(data[checksum + 4:security])
    digest.update(data[security + 8:headers_size])
    section_count = struct.unpack_from('<H', data, pe + 6)[0]
    section_table = optional + struct.unpack_from('<H', data, pe + 20)[0]
    sections = []
    for index in range(section_count):
        size, start = struct.unpack_from('<II', data, section_table + index * 40 + 16)
        if size:
            sections.append((start, size))
    end = headers_size
    for start, size in sorted(sections):
        if start != end or start + size > cert_offset:
            raise ValueError('Unsupported noncontiguous/overlapping section layout')
        digest.update(data[start:start + size])
        end = start + size
    digest.update(data[end:cert_offset])
    content = der_items(Path(cms_content).read_bytes())
    # SignedCms returns the SEQUENCE contents for this Authenticode content type.
    if len(content) == 1 and content[0][0] == 0x30:
        content = der_items(content[0][1])
    if len(content) != 2 or content[-1][0] != 0x30:
        raise ValueError('Unexpected indirect-data content')
    info = der_items(content[-1][1])
    algorithm = der_items(info[0][1])
    if algorithm[0] != (6, bytes.fromhex('608648016503040201')):
        raise ValueError('Expected SHA-256 digest algorithm')
    if info[1] != (4, digest.digest()):
        raise ValueError('Signed image digest mismatch')
    print('PASS: verified CMS content binds image SHA-256 ' + digest.hexdigest())


if __name__ == '__main__':
    check(*sys.argv[1:])
