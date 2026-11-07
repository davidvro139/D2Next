"""Compares two TableDump outputs (D2MOO's tables loaded from .bin and from .txt): compare-tables.py <a> <b>.

For each table: record counts, then the byte offsets within a record that differ, with how many records differ there.
Pointer fields (8 bytes on x64) differ in every record by nature; they show as 8-byte runs differing in almost all
records with non-null values. Everything else is a real difference between the two loading paths.
"""
import sys
from collections import Counter
from pathlib import Path


def tables(folder):
    result = {}
    for line in (Path(folder) / "tables.txt").read_text().split("\n"):
        if line.strip():
            name, count, size = line.split()
            result[name] = (int(count), int(size))
    return result


def runs(offsets):
    """Groups sorted offsets into (start, end) runs of consecutive bytes."""
    out = []
    for o in sorted(offsets):
        if out and o == out[-1][1] + 1:
            out[-1][1] = o
        else:
            out.append([o, o])
    return out


def main(a, b):
    ta, tb = tables(a), tables(b)
    clean = 0
    for name in ta:
        ca, size = ta[name]
        cb, sizeb = tb.get(name, (None, None))
        if cb != ca or sizeb != size:
            print(f"{name}: records {ca} vs {cb}, size {size} vs {sizeb}")
            continue
        da = (Path(a) / f"{name}.dat").read_bytes()
        db = (Path(b) / f"{name}.dat").read_bytes()
        diff = Counter()
        records = set()
        for i, (x, y) in enumerate(zip(da, db)):
            if x != y:
                diff[i % size] += 1
                records.add(i // size)
        if not diff:
            clean += 1
            continue
        parts = []
        for start, end in runs(diff):
            n = max(diff[o] for o in range(start, end + 1))
            parts.append(f"0x{start:X}-0x{end:X} ({end - start + 1} B, {n} rec)")
        print(f"{name}: {len(records)}/{ca} records differ at " + ", ".join(parts))
    print(f"{clean} of {len(ta)} tables identical")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
