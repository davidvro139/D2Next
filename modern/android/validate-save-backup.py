"""Read-only D2S envelope/checksum validation; writes a local backup manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    if len(data) < 44:
        raise ValueError(f'{path.name}: short D2S header')
    magic, version, length, stored = struct.unpack_from('<4I', data)
    if magic != 0xAA55AA55 or version != 96 or length != len(data):
        raise ValueError(f'{path.name}: unsupported header or incomplete file')
    checksum = 0
    for i, value in enumerate(data):
        checksum = (((checksum << 1) | (checksum >> 31)) + (0 if 12 <= i < 16 else value)) & 0xFFFFFFFF
    if checksum != stored:
        raise ValueError(f'{path.name}: checksum mismatch')
    return dict(file=path.name, bytes=len(data), level=data[43],
                sha256=hashlib.sha256(data).hexdigest(), checksum_valid=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path, help='directory containing Save/')
    args = parser.parse_args()
    files = sorted((args.backup / 'Save').glob('*.d2s'))
    if not files:
        raise SystemExit('No character saves in backup/Save')
    records = [inspect(path) for path in files]
    text = json.dumps(records, indent=2) + '\n'
    (args.backup / 'manifest.json').write_text(text, encoding='utf-8')
    print(text, end='')
