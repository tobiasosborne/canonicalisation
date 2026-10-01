#!/usr/bin/env python3
"""Compare line-oriented FORMAT files (refs/compare/FORMAT.md) from two or more implementations.

Usage: compare.py [--plant] [--seed N] FILE FILE [FILE ...]
Exit status: 0 all cases agree in every file, 1 any disagreement or missing case, 2 usage/format error.
--plant flips one random hex nibble in the bytes field of one random record of the LAST file
(in memory only) before comparing, to show the comparison is sensitive to corruption
(spec section 20, planted corruption).  Under --plant, exit 1 plus "PLANT DETECTED" is the
expected, healthy outcome; "PLANT MISSED" would mean the comparison cannot be trusted.
"""
import argparse
import random
import sys

FIELDS = ("case_id", "objective", "status", "trace_hex", "bytes_hex", "witness")


def load(path):
    records = {}
    with open(path, encoding="utf-8") as fh:
        for no, line in enumerate(fh, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != len(FIELDS):
                sys.exit(f"{path}:{no}: expected {len(FIELDS)} tab-separated fields, got {len(parts)}")
            rec = dict(zip(FIELDS, parts))
            if rec["case_id"] in records:
                sys.exit(f"{path}:{no}: duplicate case_id {rec['case_id']}")
            records[rec["case_id"]] = rec
    return records


def plant(records, rng):
    """Flip one hex nibble in one record's bytes field; return (case_id, position) or None."""
    candidates = sorted(c for c, r in records.items() if r["bytes_hex"] not in ("", "-"))
    if not candidates:
        return None
    cid = rng.choice(candidates)
    text = records[cid]["bytes_hex"]
    pos = rng.randrange(len(text))
    flipped = format((int(text[pos], 16) ^ rng.randrange(1, 16)), "x")
    records[cid]["bytes_hex"] = text[:pos] + flipped + text[pos + 1:]
    return cid, pos


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", metavar="FILE", help="two or more FORMAT files")
    ap.add_argument("--plant", action="store_true", help="corrupt one nibble of the last file's bytes")
    ap.add_argument("--seed", type=int, default=None, help="seed for --plant (default: random)")
    args = ap.parse_args(argv)
    if len(args.files) < 2:
        ap.error("need at least two files")
    data = [load(f) for f in args.files]
    planted = None
    if args.plant:
        planted = plant(data[-1], random.Random(args.seed))
        if planted is None:
            sys.exit("--plant: no record with non-empty bytes_hex to corrupt")
        print(f"PLANTED nibble {planted[1]} of bytes_hex, case {planted[0]}, file {args.files[-1]}")
    bad, flagged = 0, set()
    for cid in sorted(set().union(*data)):
        present = [d.get(cid) for d in data]
        if any(r is None for r in present):
            missing = [f for f, r in zip(args.files, present) if r is None]
            print(f"MISSING  {cid}: absent from {', '.join(missing)}")
            bad += 1
            flagged.add(cid)
            continue
        diff = [k for k in FIELDS[1:] if len({r[k] for r in present}) > 1]
        if diff:
            print(f"DISAGREE {cid}: fields differ: {', '.join(diff)}")
            bad += 1
            flagged.add(cid)
        else:
            print(f"AGREE    {cid}")
    print(f"{len(set().union(*data)) - bad} agree, {bad} disagree/missing")
    if planted:
        print("PLANT DETECTED" if planted[0] in flagged else "PLANT MISSED")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
