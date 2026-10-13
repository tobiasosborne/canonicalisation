#!/usr/bin/env python3
"""ref-a: run M0 case files through the production C path (`canon-cli`) and emit FORMAT records.

ref-a is the sealed snapshot of the unpruned production path (detailed plan 0a item 6,
refs/README.md); this script is its case-file adapter (docs/slices/M0a.md 3).  It reads the JSON
case schema of refs/CASES.md (the `p1_cases` array of golden.json or the `cases` array of a
generated file), turns every case into one `canon-cli` invocation and prints one record of
refs/compare/FORMAT.md per case, in file order.  It computes no answer itself: objects of kind
`tuple` and `dag` are written as raw (non-canonical) CDAG-2 streams of the spec 4.1 grammar, an
input encoding the library normalises on import (spec 4.2).

Usage: run_cases.py [--cli PATH] [--backend chain|explicit] [--out FILE] CASEFILE...
  --cli      canon-cli binary (default: $CANON_CLI, else build/make/canon-cli)
  --backend  group backend passed to every call (default: the CLI's default, the chain)
  --out      write the records there instead of stdout
Exit 0 when every case produced a record, 2 on a usage error.  Standard library only.

Case handling (refs/CASES.md Part B): the work policy is 0x0001 (the unpruned reference
traversal, `--prune off`) unless `capacity.work_policy` names 2 or 3; `witness_mode` absent or
"deterministic" passes `--witness deterministic`; `capacity.max_search_nodes` is passed as
`--max-nodes` (the CLI reads 0 as its default, so a quota of 0 cannot be requested through it).
Without calling the CLI: a signed labeling problem, objectives 8 and 9 and an unknown work
policy are UNSUPPORTED_ACTION; a case that violates the schema (missing or forbidden keys,
objective name and tag that disagree, a profile or encoding other than the table's) is
INVALID_INPUT or UNSUPPORTED_ACTION as spec 3.2 and 4.1 prescribe.
"""
import argparse
import json
import os
import pathlib
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OBJECTIVES = {
    # name: (tag, profile, CLI subcommand, required keys, forbidden keys)
    "CANONICAL_IMAGE": (1, 1, None, (), ("target", "character", "order", "labeling")),
    "LEX_MIN_IMAGE": (2, 0, "min", ("order",), ("target", "character", "labeling")),
    "TRANSPORTER_ONE": (3, 0, "transporter", ("target",), ("character", "order", "labeling")),
    "STABILISER": (4, 0, "stabiliser", (), ("target", "character", "order", "labeling")),
    "CANONICAL_LABELING_COSET": (5, 1, "labeling", ("labeling",), ("target", "character", "order")),
    "TRANSPORTER_COSET": (6, 0, "transporter-coset", ("target",), ("character", "order", "labeling")),
    "SIGNED_CANONICAL_IMAGE": (7, 1, "signed", ("character",), ("target", "order")),
}
STATUS_NAMES = ("COMPLETE", "CANCELLED", "CAPACITY_LIMIT", "RESOURCE_LIMIT", "INVALID_INPUT",
                "UNSUPPORTED_ACTION", "OUTPUT_ERROR", "INTERNAL_ERROR")
POLICIES = {1: "off", 2: "explicit", 3: "on"}


class CaseError(Exception):
    """A case the runner answers without the CLI: (status, message)."""

    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


# ---- the spec 4.1 wire grammar, input side ----

def u32(value):
    if not 0 <= value <= 0xFFFFFFFF:
        raise CaseError("CAPACITY_LIMIT", "a count does not fit U32")
    return struct.pack(">I", value)


def blob(data):
    return u32(len(data)) + data


def nat(value):
    """Nat(k) = U32(b) || big_endian_bytes(k, b), shortest b (spec 4.1)."""
    if value < 0:
        raise CaseError("INVALID_INPUT", "a negative natural")
    body = value.to_bytes((value.bit_length() + 7) // 8, "big") if value else b""
    return u32(len(body)) + body


def perm(images, n):
    """Perm(p): U32(s) and the s moved points as (i, p[i]) pairs in increasing i."""
    if sorted(images) != list(range(n)):
        raise CaseError("INVALID_INPUT", "a permutation field is not a bijection of 0..n-1")
    moved = [(i, p) for i, p in enumerate(images) if p != i]
    return u32(len(moved)) + b"".join(u32(i) + u32(p) for i, p in moved)


def group_presentation(generators, n):
    """Group(H) as given: rule 1 `01 00000000` for no generators, else the rule-2 layout with the
    generators listed (a presentation the library normalises on import, spec 4.2, 9.4)."""
    gens = [g for g in generators if list(g) != list(range(n))]
    if not gens:
        return b"\x01" + u32(0)
    return b"\x00" + u32(len(gens)) + b"".join(perm(g, n) for g in gens)


def hex_bytes(text, what):
    try:
        return bytes.fromhex(text)
    except (ValueError, TypeError):
        raise CaseError("INVALID_INPUT", f"{what} is not a hex string") from None


def graph_payload(obj, n):
    colours = obj.get("vertex_colours_hex")
    arcs = obj.get("arcs", [])
    if not isinstance(colours, list) or len(colours) != n:
        raise CaseError("INVALID_INPUT", "vertex_colours_hex must list exactly n colours")
    out = b"".join(blob(hex_bytes(c, "a colour")) for c in colours) + u32(len(arcs))
    for arc in arcs:
        out += u32(arc["source"]) + u32(arc["target"]) + blob(hex_bytes(arc["label_hex"], "a label"))
        out += nat(arc["multiplicity"])
    return out


def dag_records(obj, n):
    """The reachable records of a `dag` object in an order where every child precedes its
    parent (the wire needs child indices below the parent's; the input may list them in any
    order, refs/CASES.md Part B), with the root last."""
    records, root = obj["records"], obj["root"]
    if not isinstance(records, list) or not 0 <= root < len(records):
        raise CaseError("INVALID_INPUT", "dag records or root malformed")

    def children(rec):
        tag = rec["tag"]
        if tag in (3, 4):
            return list(rec["children"])
        if tag == 5:
            return [p["child"] for p in rec["pairs"]]
        return []

    order, state = [], {}  # state: 1 on the stack, 2 done

    def visit(i):
        if not 0 <= i < len(records):
            raise CaseError("INVALID_INPUT", "a child index is out of range")
        if state.get(i) == 1:
            raise CaseError("INVALID_INPUT", "the dag records are cyclic")
        if state.get(i) == 2:
            return
        state[i] = 1
        for c in children(records[i]):
            visit(c)
        state[i] = 2
        order.append(i)

    visit(root)
    index = {old: new for new, old in enumerate(order)}
    out = []
    for old in order:
        rec = records[old]
        tag = rec["tag"]
        if tag == 1:
            body = u32(rec["atom"])
        elif tag == 2:
            body = blob(hex_bytes(rec["literal_hex"], "a literal"))
        elif tag == 3:
            body = u32(len(rec["children"])) + b"".join(u32(index[c]) for c in rec["children"])
        elif tag == 4:
            refs = sorted({index[c] for c in rec["children"]})
            body = u32(len(refs)) + b"".join(u32(c) for c in refs)
        elif tag == 5:
            counts = {}
            for p in rec["pairs"]:
                counts[index[p["child"]]] = counts.get(index[p["child"]], 0) + p["multiplicity"]
            body = u32(len(counts)) + b"".join(u32(c) + nat(counts[c]) for c in sorted(counts))
        elif tag == 6:
            body = perm(rec["perm"], n)
        elif tag == 7:
            body = group_presentation(rec["group_generators"], n)
        elif tag == 8:
            body = group_presentation(rec["group_generators"], n) + perm(rec["r"], n)
        elif tag == 9:
            body = graph_payload(rec, n)
        elif tag == 10:
            rels = rec["relations"]
            body = u32(len(rels))
            for rel in rels:
                body += blob(hex_bytes(rel["name_hex"], "a relation name")) + u32(rel["arity"])
                body += u32(len(rel["tuples"]))
                for t in rel["tuples"]:
                    if len(t["atoms"]) != rel["arity"]:
                        raise CaseError("INVALID_INPUT", "a relation tuple has the wrong arity")
                    body += b"".join(u32(a) for a in t["atoms"]) + nat(t["multiplicity"])
        else:
            raise CaseError("INVALID_INPUT", f"unknown record tag {tag}")
        out.append(bytes([tag]) + body)
    return out


def stream_of(obj, n):
    """A raw CDAG-2 stream (spec 4.1) of an object of any kind."""
    kind = obj.get("kind")
    if kind == "subset":
        atoms = sorted(set(obj["atoms"]))
        recs = [b"\x01" + u32(a) for a in atoms]
        recs.append(b"\x04" + u32(len(atoms)) + b"".join(u32(i) for i in range(len(atoms))))
    elif kind == "tuple":
        points = list(obj["points"])
        recs = [b"\x01" + u32(a) for a in points]
        recs.append(b"\x03" + u32(len(points)) + b"".join(u32(i) for i in range(len(points))))
    elif kind == "graph":
        recs = [b"\x09" + graph_payload(obj, n)]
    elif kind == "dag":
        recs = dag_records(obj, n)
    else:
        raise CaseError("INVALID_INPUT", f"unknown object kind {kind!r}")
    header = b"CN\x02" + struct.pack(">HH", 1, 1) + u32(n)
    return header + u32(len(recs)) + b"".join(recs) + u32(len(recs) - 1)


# ---- case to command line ----

def object_args(obj, n, prefix, force_stream):
    """CLI options for an object (`prefix` '' or 'target-')."""
    kind = obj.get("kind")
    if kind not in ("subset", "graph", "tuple", "dag"):
        raise CaseError("INVALID_INPUT", f"unknown object kind {kind!r}")
    if force_stream or kind in ("tuple", "dag"):
        return ["--%sstream" % prefix, stream_of(obj, n).hex()], "stream"
    if kind == "subset":
        atoms = obj["atoms"]
        if any(not isinstance(a, int) or not 0 <= a < n for a in atoms):
            raise CaseError("INVALID_INPUT", "an atom is out of the domain")
        return ["--%satoms" % prefix, ",".join(str(a) for a in sorted(set(atoms)))], "subset"
    colours = obj.get("vertex_colours_hex")
    if not isinstance(colours, list) or len(colours) != n:
        raise CaseError("INVALID_INPUT", "vertex_colours_hex must list exactly n colours")
    for c in colours:
        hex_bytes(c, "a colour")
    colour_arg = "" if all(c == "" for c in colours) else ";".join(colours)
    arcs = []
    for arc in obj.get("arcs", []):
        hex_bytes(arc["label_hex"], "a label")
        arcs.append("%d,%d,%s,%d" % (arc["source"], arc["target"], arc["label_hex"],
                                     arc["multiplicity"]))
    return ["--%scolours" % prefix, colour_arg, "--%sarcs" % prefix, ";".join(arcs)], "graph"


def command(case, backend):
    """The canon-cli argument list for a case, or raise CaseError."""
    for key in ("id", "n", "group_generators", "object", "objective", "objective_tag", "profile",
                "encoding"):
        if key not in case:
            raise CaseError("INVALID_INPUT", f"missing key {key}")
    name, tag = case["objective"], case["objective_tag"]
    if tag in (8, 9) or name in ("CONSTRAINT_ONE", "CONSTRAINT_ENUM"):
        raise CaseError("UNSUPPORTED_ACTION", "constraint objectives are outside M0")
    if name not in OBJECTIVES:
        raise CaseError("UNSUPPORTED_ACTION", f"unknown objective {name!r}")
    want_tag, want_profile, cmd, required, forbidden = OBJECTIVES[name]
    if tag != want_tag:
        raise CaseError("INVALID_INPUT", "objective name and tag disagree")
    if case["profile"] != want_profile or case["encoding"] != 2:
        raise CaseError("UNSUPPORTED_ACTION", "profile or encoding not the table's")
    if name == "SIGNED_CANONICAL_IMAGE" and "labeling" in case:
        raise CaseError("UNSUPPORTED_ACTION", "signed labeling problem (spec 3.1: sigma_rho deferred)")
    for key in required:
        if key not in case:
            raise CaseError("INVALID_INPUT", f"missing key {key}")
    for key in forbidden:
        if key in case:
            raise CaseError("INVALID_INPUT", f"key {key} is forbidden for {name}")
    n = case["n"]
    if not isinstance(n, int) or n < 0:
        raise CaseError("INVALID_INPUT", "n must be a non-negative integer")
    gens = case["group_generators"]
    for g in gens:
        if len(g) != n:
            raise CaseError("INVALID_INPUT", "a generator has the wrong length")
    capacity = case.get("capacity", {})
    policy = capacity.get("work_policy", 1)
    if policy not in POLICIES:
        raise CaseError("UNSUPPORTED_ACTION", f"unknown work policy {policy}")
    mode = case.get("witness_mode", "deterministic")
    if mode not in ("any", "deterministic"):
        raise CaseError("INVALID_INPUT", "witness_mode must be any or deterministic")
    obj = case["object"]
    target = case.get("target")
    force = target is not None and (target.get("kind") != obj.get("kind"))
    args = []
    if cmd is None:  # CANONICAL_IMAGE: the P1 subcommands
        oargs, kind = object_args(obj, n, "", False)
        args = ["p1-" + kind] + oargs
    else:
        oargs, kind = object_args(obj, n, "", force)
        args = [cmd] + oargs + (["--kind", kind] if kind != "stream" else [])
        if target is not None:
            targs, _ = object_args(target, n, "target-", force or kind == "stream")
            args += targs
    if "order" in case:
        orders = {"CDAG-BYTE-1": "cdag", "SIMPLE-UPPER-1": "simple-upper"}
        if case["order"] not in orders:
            raise CaseError("INVALID_INPUT", "unknown order")
        args += ["--order", orders[case["order"]]]
    if "labeling" in case:
        rho = case["labeling"].get("rho") if isinstance(case["labeling"], dict) else None
        if rho is None or len(rho) != n:
            raise CaseError("INVALID_INPUT", "labeling.rho must be an image array of length n")
        args += ["--rho", ",".join(str(v) for v in rho)]
    if "character" in case:
        chi = case["character"]
        if len(chi) != len(gens) or any(c not in (1, -1) for c in chi):
            raise CaseError("INVALID_INPUT", "character must list 1 or -1 per generator")
        args += ["--signs", "".join("+" if c == 1 else "-" for c in chi)]
    args += ["--n", str(n), "--gens", ";".join(",".join(str(v) for v in g) for g in gens)]
    args += ["--id", str(case["id"]), "--prune", POLICIES[policy], "--witness", mode]
    if "max_search_nodes" in capacity:
        args += ["--max-nodes", str(capacity["max_search_nodes"])]
    if backend:
        args += ["--backend", backend]
    return args


def record_without_cli(case, status):
    tag = case.get("objective_tag", 0) if isinstance(case.get("objective_tag"), int) else 0
    return "\t".join([str(case.get("id", "?")), "%04x" % (tag & 0xFFFF), status, "", "", "-", "-"])


def run_case(cli, case, backend):
    try:
        args = command(case, backend)
    except CaseError as e:
        return record_without_cli(case, e.status), str(e)
    except (KeyError, TypeError, AttributeError) as e:
        return record_without_cli(case, "INVALID_INPUT"), f"malformed case: {e!r}"
    proc = subprocess.run([cli] + args, capture_output=True, text=True, check=False)
    lines = proc.stdout.splitlines()
    if len(lines) == 1 and len(lines[0].split("\t")) == 7 and lines[0].split("\t")[2] in STATUS_NAMES:
        return lines[0], None
    return record_without_cli(case, "INTERNAL_ERROR"), (
        f"canon-cli exit {proc.returncode}: {proc.stderr.strip() or proc.stdout.strip()}")


def load_cases(path):
    data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    if isinstance(data, dict):
        for key in ("cases", "p1_cases"):
            if isinstance(data.get(key), list):
                return data[key]
    raise SystemExit(f"{path}: no `cases` or `p1_cases` array")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", metavar="CASEFILE")
    ap.add_argument("--cli", default=os.environ.get("CANON_CLI") or str(ROOT / "build" / "make" / "canon-cli"))
    ap.add_argument("--backend", choices=("chain", "explicit"), default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)
    if not pathlib.Path(args.cli).is_file():
        ap.error(f"canon-cli not found at {args.cli} (run make, or pass --cli)")
    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    try:
        print("# ref-a records (refs/ref-a/run_cases.py over canon-cli); FORMAT.md seven fields",
              file=out)
        for path in args.files:
            for case in load_cases(path):
                record, note = run_case(args.cli, case, args.backend)
                print(record, file=out)
                if note:
                    print(f"{path}: {case.get('id', '?')}: {note}", file=sys.stderr)
    finally:
        if out is not sys.stdout:
            out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
