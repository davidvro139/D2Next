"""Exercise the real external helper before any game data or save can load."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('host', type=Path)
parser.add_argument('fixture_parent', type=Path)
args = parser.parse_args()
host = args.host.resolve(strict=True)
with tempfile.TemporaryDirectory(prefix='native-handshake-', dir=args.fixture_parent) as directory:
    missing = str(Path(directory) / 'does-not-exist')
    command = [str(host), missing, missing, missing, '1']

    def run(data):
        result = subprocess.run(command, input=data, capture_output=True, timeout=15)
        if result.returncode != 1:
            raise AssertionError(f'unexpected helper exit {result.returncode}')
        return result

    first = run(bytes(16))
    assert len(first.stdout) == 16 and b'Incompatible native client protocol' in first.stderr
    hello = struct.unpack('<4I', first.stdout)
    assert hello[0] == 0x32484E44 and hello[1] == 20 and hello[2] == 28 and hello[3] > 1000
    for field in range(4):
        changed = list(hello)
        changed[field] ^= 1
        result = run(struct.pack('<4I', *changed))
        assert result.stdout == first.stdout
        assert b'Incompatible native client protocol' in result.stderr
    legacy = run(struct.pack('<4I', 0x44474D4B, 0, 0, 0))
    assert b'Incompatible native client protocol' in legacy.stderr
    short = run(first.stdout[:8])
    assert b'Native session pipe closed' in short.stderr
    accepted = run(first.stdout)
    assert accepted.stdout.startswith(first.stdout)
    assert b'Incompatible native client protocol' not in accepted.stderr
    assert b'NativeGameHost:' in accepted.stderr
    print('PASS real helper: legacy/zero/version/size rejection, short EOF, valid hello reaches missing-data failure without a save')
