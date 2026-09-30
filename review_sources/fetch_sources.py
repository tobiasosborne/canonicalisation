#!/usr/bin/env python3
"""Rebuild or verify the third-party evidence files listed in SOURCES.json.

The reviews cite local primary sources (arXiv TeX/PDFs, a scanned paper,
vendor documents, pinned isocert and mathlib code). Their licences do not
permit redistribution under this repository's licence, so Git keeps only
their provenance: URL or archive member, byte count and SHA-256.

Usage (from anywhere):

    python3 review_sources/fetch_sources.py            # verify local files, no network
    python3 review_sources/fetch_sources.py --fetch    # download + extract missing files, then verify
    python3 review_sources/fetch_sources.py --only algorithms   # restrict to a path substring

Downloads are written only when their SHA-256 matches the recorded value,
except for entries marked hash_stable=false (vendor/arXiv web pages and
locally derived text/images), which are fetched and reported as CHANGED if
they differ. Derived files are never generated automatically; their
recorded command is printed instead. No downloaded content is executed.
"""
import argparse
import gzip
import hashlib
import json
import pathlib
import sys
import tarfile
import urllib.request

REPO = pathlib.Path(__file__).resolve().parent.parent
INVENTORY = REPO / "review_sources" / "SOURCES.json"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def local(path):
    return REPO / path


def status_of(row):
    p = local(row["path"])
    if not p.is_file():
        return "MISSING"
    return "OK" if sha256(p.read_bytes()) == row["sha256"] else ("CHANGED" if not row["hash_stable"] else "MISMATCH")


def safe_write(row, data):
    target = local(row["path"]).resolve()
    if REPO.resolve() not in target.parents:
        raise ValueError(f"refusing to write outside the repository: {row['path']}")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def download(row):
    req = urllib.request.Request(row["url"], headers={"User-Agent": "canonicalisation-review-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=60) as response:
        data = response.read()
    if sha256(data) != row["sha256"] and row["hash_stable"]:
        raise ValueError(f"SHA-256 mismatch for {row['url']}; not written (upstream changed or wrong version)")
    safe_write(row, data)


def read_members(archive_path, members):
    """Return {member: bytes} for the wanted regular-file members, in one streaming pass."""
    wanted, found = set(members), {}
    try:
        with tarfile.open(local(archive_path), mode="r|*") as tar:
            for info in tar:
                if info.name in wanted and info.isfile():
                    found[info.name] = tar.extractfile(info).read()
                    if len(found) == len(wanted):
                        break
    except tarfile.ReadError:
        # arXiv serves single-file submissions as a bare gzip stream.
        if len(wanted) == 1:
            found[next(iter(wanted))] = gzip.decompress(local(archive_path).read_bytes())
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fetch", action="store_true", help="download and extract missing files (uses the network)")
    ap.add_argument("--only", default="", help="restrict to inventory paths containing this substring")
    args = ap.parse_args()

    rows = [r for r in json.loads(INVENTORY.read_text())["files"] if args.only in r["path"]]
    by_path = {r["path"]: r for r in json.loads(INVENTORY.read_text())["files"]}

    if args.fetch:
        for row in rows:
            if row["kind"] == "download" and status_of(row) == "MISSING":
                try:
                    download(row)
                    print("fetched ", row["path"], flush=True)
                except Exception as exc:  # report and continue with other sources
                    print("FAILED  ", row["path"], exc, flush=True)
        pending = {}
        for row in rows:
            if row["kind"] == "archive_member" and status_of(row) == "MISSING":
                pending.setdefault(row["archive"], []).append(row)
        for archive_path, members in sorted(pending.items()):
            archive = by_path.get(archive_path)
            if archive is None or status_of(archive) != "OK":
                for row in members:
                    print("SKIPPED ", row["path"], "(archive missing or unverified:", archive_path + ")")
                continue
            data = read_members(archive_path, [r["member"] for r in members])
            for row in members:
                blob = data.get(row["member"])
                if blob is None or sha256(blob) != row["sha256"]:
                    print("FAILED  ", row["path"], "member absent or hash differs; not written")
                    continue
                safe_write(row, blob)
                print("extracted", row["path"], flush=True)

    counts = {}
    bad = False
    for row in rows:
        s = status_of(row)
        counts[s] = counts.get(s, 0) + 1
        if s == "MISMATCH":
            bad = True
            print("MISMATCH", row["path"])
        elif s == "MISSING" and row["kind"] == "derived":
            cmd = row["command"].replace("FROM", row["from"] or "?").replace("PATH", row["path"])
            print("MISSING ", row["path"], "(derived: " + cmd + ")")
        elif s != "OK":
            print(f"{s:8}", row["path"])
    print("summary:", ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
