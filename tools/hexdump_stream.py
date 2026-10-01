#!/usr/bin/env python3
"""Strict CDAG-2 stream parser and pretty-printer (docs/specification.md section 4.1).

Usage:  hexdump_stream.py HEX            (spaces allowed)
        hexdump_stream.py -f FILE        (FILE holds hex text; '-' reads stdin)
        hexdump_stream.py --raw FILE     (FILE holds raw bytes)

Coverage: tags 01-05 and 09 are parsed and validated strictly.  Tags 06, 07, 08 and 0a
are also parsed: Perm(p) per section 4.1, Group(H) per section 9.4 (structure only, no
group-theoretic validation), Perm for 08, relation records for 0a (section 4.1).
Checks: header, q>=1, atoms < n, child references < parent index, sets/multisets
strictly increasing, positive multiplicities, shortest Nat, every record reachable from
the root, root = last record, no trailing bytes.  This is a syntax checker: it does not
re-derive the canonical DAG numbering (section 4.2) and is not the certificate checker.
Standard library only.
"""
import sys

HEADER = bytes.fromhex("43 4e 02")
TAG_NAMES = {1: "atom", 2: "literal", 3: "tuple", 4: "set", 5: "multiset", 6: "perm",
             7: "group", 8: "coset", 9: "graph", 10: "relations"}


class StreamError(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, k):
        if k < 0 or self.pos + k > len(self.data):
            raise StreamError(f"truncated at offset {self.pos}: need {k} bytes")
        out = self.data[self.pos:self.pos + k]
        self.pos += k
        return out

    def u16(self):
        return int.from_bytes(self.take(2), "big")

    def u32(self):
        return int.from_bytes(self.take(4), "big")

    def blob(self):
        return self.take(self.u32())

    def nat(self):
        b = self.u32()
        raw = self.take(b)
        if raw[:1] == b"\x00":
            raise StreamError(f"Nat with leading zero byte at offset {self.pos - b}")
        return int.from_bytes(raw, "big")

    def atom(self, n):
        a = self.u32()
        if a >= n:
            raise StreamError(f"atom {a} out of domain n={n} at offset {self.pos - 4}")
        return a


def read_perm(r, n):
    s = r.u32()
    pairs, last = [], -1
    for _ in range(s):
        i, j = r.atom(n), r.atom(n)
        if i <= last:
            raise StreamError("Perm sources not strictly increasing")
        if i == j:
            raise StreamError("Perm lists a fixed point")
        last = i
        pairs.append((i, j))
    if sorted(i for i, _ in pairs) != sorted(j for _, j in pairs):
        raise StreamError("Perm is not a bijection on its support")
    return pairs


def read_group(r, n):
    mode = r.take(1)[0]
    k = r.u32()
    if mode == 1:      # symmetric product of orbits (section 9.4 rule 1)
        blocks, prev = [], -1
        for _ in range(k):
            size = r.u32()
            pts = [r.atom(n) for _ in range(size)]
            if size < 2 or pts != sorted(set(pts)) or pts[0] <= prev:
                raise StreamError("bad orbit block (size>=2, increasing, ordered by least point)")
            prev = pts[0]
            blocks.append(pts)
        return ("symmetric-product", blocks)
    if mode == 0:      # greedy canonical generators (rule 2)
        return ("generators", [read_perm(r, n) for _ in range(k)])
    raise StreamError(f"unknown Group mode {mode:#04x}")


def read_record(r, n, index):
    tag = r.take(1)[0]
    def child():
        c = r.u32()
        if c >= index:
            raise StreamError(f"child reference {c} not smaller than record index {index}")
        return c
    if tag == 1:
        return tag, r.atom(n), []
    if tag == 2:
        return tag, r.blob(), []
    if tag == 3:
        kids = [child() for _ in range(r.u32())]
        return tag, kids, kids
    if tag == 4:
        kids = [child() for _ in range(r.u32())]
        if kids != sorted(set(kids)):
            raise StreamError("set children not increasing and distinct")
        return tag, kids, kids
    if tag == 5:
        pairs = [(child(), r.nat()) for _ in range(r.u32())]
        refs = [c for c, _ in pairs]
        if refs != sorted(set(refs)):
            raise StreamError("multiset references not increasing")
        if any(m == 0 for _, m in pairs):
            raise StreamError("multiset multiplicity 0")
        return tag, pairs, refs
    if tag == 6:
        return tag, read_perm(r, n), []
    if tag == 7:
        return tag, read_group(r, n), []
    if tag == 8:
        return tag, (read_group(r, n), read_perm(r, n)), []
    if tag == 9:
        colours = [r.blob() for _ in range(n)]
        arcs = []
        for _ in range(r.u32()):
            a, b = r.atom(n), r.atom(n)
            label, m = r.blob(), r.nat()
            if m == 0:
                raise StreamError("arc multiplicity 0")
            arcs.append((a, b, label, m))
        key = [(a, b, len(lab).to_bytes(4, "big") + lab) for a, b, lab, _ in arcs]
        if key != sorted(set(key)):
            raise StreamError("arcs not sorted/unique by (source, target, B(label))")
        return tag, (colours, arcs), []
    if tag == 10:
        rels = []
        for _ in range(r.u32()):
            name, arity, k = r.blob(), r.u32(), r.u32()
            tuples = []
            for _ in range(k):
                atoms = tuple(r.atom(n) for _ in range(arity))
                m = r.nat()
                if m == 0:
                    raise StreamError("relation multiplicity 0")
                tuples.append((atoms, m))
            if [t for t, _ in tuples] != sorted({t for t, _ in tuples}):
                raise StreamError("relation tuples not sorted/unique")
            rels.append((name, arity, tuples))
        names = [(len(nm).to_bytes(4, "big") + nm) for nm, _, _ in rels]
        if names != sorted(set(names)):
            raise StreamError("relation names not sorted/unique")
        return tag, rels, []
    raise StreamError(f"unknown record tag {tag:#04x} at offset {r.pos - 1}")


def parse_stream(data):
    """Return dict(n, q, records=[(offset, tag, payload)], root); raise StreamError."""
    r = Reader(data)
    if r.take(3) != HEADER:
        raise StreamError("bad magic (expected 43 4e 02)")
    schema, action = r.u16(), r.u16()
    if (schema, action) != (1, 1):
        raise StreamError(f"unsupported schema/action {schema}/{action}")
    n, q = r.u32(), r.u32()
    if q < 1:
        raise StreamError("q must be at least 1")
    records, children = [], []
    for i in range(q):
        off = r.pos
        tag, payload, kids = read_record(r, n, i)
        records.append((off, tag, payload))
        children.append(kids)
    root_off, root = r.pos, r.u32()
    if root != q - 1:
        raise StreamError(f"root {root} is not the last record {q - 1}")
    if r.pos != len(data):
        raise StreamError(f"{len(data) - r.pos} trailing byte(s) at offset {r.pos}")
    seen, todo = set(), [root]
    while todo:
        i = todo.pop()
        if i not in seen:
            seen.add(i)
            todo.extend(children[i])
    if len(seen) != q:
        raise StreamError(f"unreachable records: {sorted(set(range(q)) - seen)}")
    return {"n": n, "q": q, "records": records, "root": root, "root_offset": root_off}


def format_stream(s):
    lines = [f"offset 0x0000  header   n={s['n']} schema=1 action=1 q={s['q']}"]
    for i, (off, tag, payload) in enumerate(s["records"]):
        text = repr(payload) if tag != 2 else f"hex={payload.hex() or '(empty)'}"
        if tag == 9:
            colours, arcs = payload
            text = "colours=" + repr([c.hex() for c in colours]) + " arcs=" + repr(
                [(a, b, lab.hex(), m) for a, b, lab, m in arcs])
        lines.append(f"offset 0x{off:04x}  [{i}] {TAG_NAMES[tag]:<8} {text}")
    lines.append(f"offset 0x{s['root_offset']:04x}  root     {s['root']}")
    return "\n".join(lines)


def main(argv):
    if len(argv) == 3 and argv[1] == "--raw":
        data = open(argv[2], "rb").read()
    elif len(argv) == 3 and argv[1] == "-f":
        text = sys.stdin.read() if argv[2] == "-" else open(argv[2]).read()
        data = bytes.fromhex("".join(text.split()))
    elif len(argv) >= 2 and not argv[1].startswith("-"):
        data = bytes.fromhex("".join("".join(argv[1:]).split()))
    else:
        print(__doc__)
        return 2
    try:
        print(format_stream(parse_stream(data)))
    except (StreamError, ValueError) as exc:
        print(f"hexdump_stream: invalid stream: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
