#!/usr/bin/env python3
"""The tiny exhaustive oracle of spec 20 (detailed plan WP0.3; docs/slices/M0a.md 3).

Usage: oracle.py [--max-order N] [--verbose] CASEFILE RECORDS

For every case of CASEFILE (refs/CASES.md) whose group has at most --max-order elements
(default 200000), the FORMAT record of RECORDS (refs/compare/FORMAT.md) with the same case id
is checked against the definitions of spec 3, 3.1, 4.1-4.4, 8.2, 8.4 and 9.4 by explicit
enumeration of the group:

  CANONICAL_IMAGE      bytes encode an element of the orbit x^G; the witness w is in G with
                       x^w = c; in deterministic mode w is the least such g; across the file,
                       cases with the same group and objects in the same orbit have the same
                       bytes and trace (C_G(x^h) = C_G(x)).
  LEX_MIN_IMAGE        bytes encode the least image under the named order (CDAG-BYTE-1 on the
                       canonical stream, SIMPLE-UPPER-1 on the spec 4.4 key, also returned);
                       the witness attains it (the least attaining g in deterministic mode).
  TRANSPORTER_ONE      x^w = y with w in G, or `-` exactly when y is not in x^G; deterministic
                       mode: the least solution.
  STABILISER           group_hex is Group(A), A = {g : x^g = x}, by the spec 9.4 rules.
  TRANSPORTER_COSET    Group(A) || Perm(r0) with r0 the least solution, the witness r0 (or the
                       first hit in "any" mode, a solution), or `-`, `-` when none.
  CANONICAL_LABELING_COSET  lambda in G rho, x^lambda = c, group_hex = Group(A) || Perm(least of
                       A lambda); deterministic mode: lambda is that least element; across the
                       file, cases with the same group, object and coset G rho agree on bytes
                       and trace (spec 3.1: representative change).
  SIGNED_CANONICAL_IMAGE  the character is validated by the lift (spec 8.4); an inconsistent one
                       must be INVALID_INPUT; zero iff an odd element fixes x, with the witness
                       an odd automorphism; nonzero: bytes in the orbit, witness t in G with
                       x^t = c and sign chi(t), group_hex = Group(A); deterministic mode: the
                       least of A t; across the file, s(x^h) = chi(h) s(x) with c unchanged.
  A signed labeling problem must be UNSUPPORTED_ACTION.  CAPACITY_LIMIT records are skipped
  (not checkable here); any other non-COMPLETE status is a failure.

Traces are not checked beyond invariance: they are defined by P1, not by the orbit.  The
encoder, normaliser and Group(H) writer below are written from the spec text; this file reuses
only closure, mul and inverse of checks/review_checks.py (WP0.3), never its p1, dag_bytes,
graph_bytes or group_bytes.  Standard library only.  Exit 0 when every checked case passes,
1 otherwise; every failure names the case and the rule.
"""
import argparse
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "checks"))
from review_checks import closure, inverse, mul  # noqa: E402

FIELDS = ("case_id", "objective", "status", "trace_hex", "bytes_hex", "witness", "group_hex")


# ---- spec 4.1 wire primitives ----

def u32(v):
    return struct.pack(">I", v)


def blob(b):
    return u32(len(b)) + b


def nat(k):
    body = k.to_bytes((k.bit_length() + 7) // 8, "big") if k else b""
    return u32(len(body)) + body


def perm_bytes(p):
    moved = [(i, v) for i, v in enumerate(p) if v != i]
    return u32(len(moved)) + b"".join(u32(i) + u32(v) for i, v in moved)


def header(n):
    return b"CN\x02" + struct.pack(">HH", 1, 1) + u32(n)


# ---- spec 9.4: Group(H) and coset payloads ----

def orbits(group, n):
    seen, out = set(), []
    for a in range(n):
        if a in seen:
            continue
        orb = sorted({g[a] for g in group})
        seen.update(orb)
        out.append(orb)
    return out


def factorial(k):
    r = 1
    for i in range(2, k + 1):
        r *= i
    return r


def group_bytes(group, n):
    """spec 9.4: rule 1 (a product of symmetric groups on the orbits) else rule 2 (the greedy
    least-outside generators)."""
    orbs = orbits(group, n)
    prod = 1
    for o in orbs:
        prod *= factorial(len(o))
    if len(group) == prod:
        blocks = [o for o in orbs if len(o) > 1]
        return b"\x01" + u32(len(blocks)) + b"".join(
            u32(len(o)) + b"".join(u32(p) for p in o) for o in blocks)
    known, gens = closure((), n), []
    while known != group:
        g = min(group - known)
        gens.append(g)
        known = closure(tuple(gens), n)
    return b"\x00" + u32(len(gens)) + b"".join(perm_bytes(g) for g in gens)


def coset_bytes(group, r, n):
    return group_bytes(group, n) + perm_bytes(min(mul(a, r) for a in group))


# ---- objects: decoding of the case schema, action, canonical encoding (spec 4.1, 4.2) ----

class Obj:
    """A value: ('subset', frozenset), ('tuple', tuple), ('graph', (colours, arcs)) with arcs a
    dict (s, t, label) -> m, or ('dag', node) with node a nested value: ('atom', a),
    ('lit', bytes), ('tuple', (nodes)), ('set', frozenset(nodes)), ('mset', frozenset((node,
    m))), ('perm', p), ('group', frozenset), ('coset', (frozenset, r))."""


def parse(obj, n):
    kind = obj["kind"]
    if kind == "subset":
        return ("subset", frozenset(obj["atoms"]))
    if kind == "tuple":
        return ("tuple", tuple(obj["points"]))
    if kind == "graph":
        return ("graph", parse_graph(obj, n))
    records = obj["records"]
    memo = {}

    def node(i, stack=()):
        if i in stack:
            raise ValueError("cyclic dag")
        if i in memo:
            return memo[i]
        rec = records[i]
        tag = rec["tag"]
        if tag == 1:
            v = ("atom", rec["atom"])
        elif tag == 2:
            v = ("lit", bytes.fromhex(rec["literal_hex"]))
        elif tag == 3:
            v = ("tuple", tuple(node(c, stack + (i,)) for c in rec["children"]))
        elif tag == 4:
            v = ("set", frozenset(node(c, stack + (i,)) for c in rec["children"]))
        elif tag == 5:
            counts = {}
            for p in rec["pairs"]:
                c = node(p["child"], stack + (i,))
                counts[c] = counts.get(c, 0) + p["multiplicity"]
            v = ("mset", frozenset(counts.items()))
        elif tag == 6:
            v = ("perm", tuple(rec["perm"]))
        elif tag == 7:
            v = ("group", closure(tuple(tuple(g) for g in rec["group_generators"]), n))
        elif tag == 8:
            v = ("coset", (closure(tuple(tuple(g) for g in rec["group_generators"]), n),
                           tuple(rec["r"])))
        elif tag == 9:
            v = ("graph", parse_graph(rec, n))
        else:
            raise ValueError("unsupported tag %d" % tag)
        memo[i] = v
        return v

    return ("dag", node(obj["root"]))


def parse_graph(obj, n):
    colours = tuple(bytes.fromhex(c) for c in obj["vertex_colours_hex"])
    arcs = {}
    for a in obj["arcs"]:
        key = (a["source"], a["target"], bytes.fromhex(a["label_hex"]))
        arcs[key] = arcs.get(key, 0) + a["multiplicity"]
    return (colours, tuple(sorted(arcs.items())))


def shape(value):
    """The structural kind a reference may distinguish at input validation: a set of atoms (the
    subset object), a graph, an atom tuple, or a nested object by the kind of its root."""
    kind, v = value
    if kind == "subset":
        return "set-of-atoms"
    if kind in ("tuple", "graph"):
        return kind
    rk, rv = v
    if rk == "set" and all(c[0] == "atom" for c in rv):
        return "set-of-atoms"
    if rk == "graph":
        return "graph"
    if rk == "tuple" and all(c[0] == "atom" for c in rv):
        return "tuple"
    return "nested-" + rk


def act(value, g):
    """spec 2.1 ATOM-TRANSPORT-1 on every kind; permutation, subgroup and coset leaves by
    conjugation (spec 4.1)."""
    kind, v = value
    if kind == "subset":
        return (kind, frozenset(g[a] for a in v))
    if kind == "tuple" and isinstance(v, tuple) and all(isinstance(a, int) for a in v):
        return (kind, tuple(g[a] for a in v))
    if kind == "graph":
        colours, arcs = v
        n = len(colours)
        nc = [b""] * n
        for u in range(n):
            nc[g[u]] = colours[u]
        return (kind, (tuple(nc), tuple(sorted(((g[s], g[t], lab), m) for (s, t, lab), m in arcs))))
    if kind == "dag":
        return (kind, act_node(v, g))
    raise ValueError(kind)


def act_node(node, g):
    kind, v = node
    if kind == "atom":
        return (kind, g[v])
    if kind == "lit":
        return node
    if kind == "tuple":
        return (kind, tuple(act_node(c, g) for c in v))
    if kind == "set":
        return (kind, frozenset(act_node(c, g) for c in v))
    if kind == "mset":
        counts = {}
        for c, m in v:
            c2 = act_node(c, g)
            counts[c2] = counts.get(c2, 0) + m
        return (kind, frozenset(counts.items()))
    gi = inverse(g)
    if kind == "perm":
        return (kind, mul(mul(gi, v), g))
    if kind == "group":
        return (kind, frozenset(mul(mul(gi, h), g) for h in v))
    if kind == "coset":
        h, r = v
        return (kind, (frozenset(mul(mul(gi, x), g) for x in h), mul(mul(gi, r), g)))
    if kind == "graph":
        return act(node, g)
    raise ValueError(kind)


def graph_record(colours, arcs):
    """spec 4.1 tag 09: colours, then the arcs sorted by (source, target, B(label)), combined."""
    body = b"".join(blob(c) for c in colours)
    ordered = sorted(arcs, key=lambda item: (item[0][0], item[0][1], len(item[0][2]), item[0][2]))
    body += u32(len(ordered))
    for (s, t, lab), m in ordered:
        body += u32(s) + u32(t) + blob(lab) + nat(m)
    return b"\x09" + body


def encode(value, n):
    """The canonical CDAG-2 stream (spec 4.1 grammar, 4.2 normal form)."""
    kind, v = value
    if kind == "subset":
        root = ("set", frozenset(("atom", a) for a in v))
    elif kind == "tuple":
        root = ("tuple", tuple(("atom", a) for a in v))
    elif kind == "graph":
        root = ("graph", v)
    else:
        root = v
    # distinct normalised nodes, their heights
    height = {}

    def visit(node):
        if node in height:
            return height[node]
        k, val = node
        if k == "tuple":
            h = 1 + max((visit(c) for c in val), default=-1)
        elif k == "set":
            h = 1 + max((visit(c) for c in val), default=-1)
        elif k == "mset":
            h = 1 + max((visit(c) for c, _ in val), default=-1)
        else:
            h = 0
        height[node] = h
        return h

    visit(root)
    index = {}
    records = []
    for h in range(max(height.values()) + 1):
        level = [nd for nd, hh in height.items() if hh == h]
        encoded = sorted((record_bytes(nd, index, n), nd) for nd in level)
        for rec, nd in encoded:
            index[nd] = len(records)
            records.append(rec)
    return header(n) + u32(len(records)) + b"".join(records) + u32(index[root])


def record_bytes(node, index, n):
    k, v = node
    if k == "atom":
        return b"\x01" + u32(v)
    if k == "lit":
        return b"\x02" + blob(v)
    if k == "tuple":
        return b"\x03" + u32(len(v)) + b"".join(u32(index[c]) for c in v)
    if k == "set":
        refs = sorted(index[c] for c in v)
        return b"\x04" + u32(len(refs)) + b"".join(u32(c) for c in refs)
    if k == "mset":
        pairs = sorted((index[c], m) for c, m in v)
        return b"\x05" + u32(len(pairs)) + b"".join(u32(c) + nat(m) for c, m in pairs)
    if k == "perm":
        return b"\x06" + perm_bytes(v)
    if k == "group":
        return b"\x07" + group_bytes(v, n)
    if k == "coset":
        h, r = v
        return b"\x08" + coset_bytes(h, r, n)
    if k == "graph":
        return graph_record(*v)
    raise ValueError(k)


def simple_upper_key(value, n):
    """spec 4.4: U32(n) and the upper-triangle bits in the order (0,1),(0,2),(1,2),(0,3),...,
    MSB first, zero padding; None when the graph is not in the class."""
    kind, (colours, arcs) = value
    if kind != "graph" or any(c != b"" for c in colours):
        return None
    edges = set()
    for (s, t, lab), m in arcs:
        if lab != b"" or s == t or m != 1:
            return None
        edges.add((s, t))
    if any((t, s) not in edges for s, t in edges):
        return None
    bits = []
    for j in range(n):
        for i in range(j):
            bits.append(1 if (i, j) in edges else 0)
    out = bytearray(u32(n))
    for k in range(0, len(bits), 8):
        chunk = bits[k:k + 8] + [0] * (8 - len(bits[k:k + 8]))
        out.append(int("".join(map(str, chunk)), 2))
    return bytes(out)


# ---- characters (spec 8.4) ----

def character_table(gens, signs, n, group):
    """chi on every element, or None when the signs are inconsistent (the lift's kernel over
    Omega is nontrivial)."""
    lifted = [tuple(g) + ((n + 1, n) if s < 0 else (n, n + 1)) for g, s in zip(gens, signs)]
    ident = tuple(range(n + 2))
    found, todo = {ident}, [ident]
    while todo:
        a = todo.pop()
        for b in lifted:
            c = mul(a, b)
            if c not in found:
                found.add(c)
                todo.append(c)
    table = {}
    for e in found:
        g, s = e[:n], 1 if e[n] == n else -1
        if g in table and table[g] != s:
            return None
        table[g] = s
    assert set(table) == set(group)
    return table


# ---- the checks ----

def parse_witness(text, n):
    if text == "-":
        return tuple(range(n)) if n == 0 else None
    return tuple(int(v) for v in text.split(","))


class Oracle:
    def __init__(self, max_order, verbose):
        self.max_order, self.verbose = max_order, verbose
        self.failures, self.checked, self.skipped = [], 0, 0
        self.images = {}      # (n, group, orbit key) -> (bytes, trace, case) for CANONICAL_IMAGE
        self.labelings = {}   # (n, group, enc(x), coset) -> (bytes, trace, case)
        self.signed = {}      # (n, group, chi, orbit key) -> (bytes, sign, value, case)
        self.groups = {}
        self.orbit_cache = {}  # (n, group, x) -> (images, enc, ex, stab); the last few only

    def fail(self, case, rule):
        self.failures.append((case, rule))
        print(f"FAIL     {case}: {rule}")

    def group_of(self, n, gens):
        key = (n, tuple(sorted(set(tuple(g) for g in gens))))
        if key not in self.groups:
            found, todo = {tuple(range(n))}, [tuple(range(n))]
            over = False
            while todo and not over:
                a = todo.pop()
                for b in key[1]:
                    c = mul(a, b)
                    if c not in found:
                        found.add(c)
                        if len(found) > self.max_order:
                            over = True
                            break
                        todo.append(c)
            self.groups[key] = None if over else frozenset(found)
        return self.groups[key]

    def check(self, case, rec):
        cid, n, gens = case["id"], case["n"], [tuple(g) for g in case["group_generators"]]
        group = self.group_of(n, gens)
        if group is None:
            self.skipped += 1
            return
        status, trace, data, wtext, ghex = (rec["status"], rec["trace_hex"], rec["bytes_hex"],
                                            rec["witness"], rec["group_hex"])
        objective = case["objective"]
        det = case.get("witness_mode", "deterministic") == "deterministic"
        if rec["objective"] != "%04x" % (case["objective_tag"] & 0xFFFF):
            return self.fail(cid, "record objective differs from the case's tag")
        # out-of-scope and malformed cases: the status the schema prescribes (refs/CASES.md)
        tags = {"CANONICAL_IMAGE": 1, "LEX_MIN_IMAGE": 2, "TRANSPORTER_ONE": 3, "STABILISER": 4,
                "CANONICAL_LABELING_COSET": 5, "TRANSPORTER_COSET": 6, "SIGNED_CANONICAL_IMAGE": 7}
        if objective not in tags:
            if status != "UNSUPPORTED_ACTION":
                self.fail(cid, "an objective outside M0 must be UNSUPPORTED_ACTION")
            self.checked += 1
            return
        if tags[objective] != case["objective_tag"]:
            if status != "INVALID_INPUT":
                self.fail(cid, "objective name and tag disagree: must be INVALID_INPUT")
            self.checked += 1
            return
        if objective in ("TRANSPORTER_ONE", "TRANSPORTER_COSET"):
            try:
                shapes = (shape(parse(case["object"], n)), shape(parse(case["target"], n)))
            except ValueError as e:
                return self.fail(cid, f"object not parseable: {e}")
            if shapes[0] != shapes[1]:
                # a set of atoms is never the image of a tuple, a tuple never that of a graph:
                # the reference may refuse the pair (spec 17 "mismatched target") or report the
                # exhausted empty result
                if status == "INVALID_INPUT" or (status == "COMPLETE" and wtext == "-" and
                                                 ghex == "-"):
                    self.checked += 1
                    return
                return self.fail(cid, "target of another shape: INVALID_INPUT or an empty result")
        if case["object"]["kind"] == "dag" and status == "UNSUPPORTED_ACTION" and any(
                r["tag"] >= 6 for r in case["object"]["records"]):
            self.checked += 1  # leaves of tags 06-0a are outside the M0 scope (refs/BRIEF.md 2)
            return
        if objective == "SIGNED_CANONICAL_IMAGE" and "labeling" in case:
            if status != "UNSUPPORTED_ACTION":
                self.fail(cid, "signed labeling problem must be UNSUPPORTED_ACTION (spec 3.1)")
            self.checked += 1
            return
        chi = None
        if objective == "SIGNED_CANONICAL_IMAGE":
            chi = character_table(gens, case["character"], n, group)
            if chi is None:
                if status != "INVALID_INPUT":
                    self.fail(cid, "inconsistent character must be INVALID_INPUT (spec 8.4)")
                self.checked += 1
                return
        if status == "CAPACITY_LIMIT":
            self.skipped += 1
            return
        if status != "COMPLETE":
            return self.fail(cid, f"status {status} on a checkable case")
        self.checked += 1
        try:
            x = parse(case["object"], n)
        except ValueError as e:
            return self.fail(cid, f"object not parseable: {e}")
        elements = sorted(group)
        ckey = (n, group, x)
        if ckey not in self.orbit_cache:
            if len(self.orbit_cache) > 8:
                self.orbit_cache.clear()
            images = {g: act(x, g) for g in elements}
            enc = {g: encode(images[g], n) for g in elements}
            ex = encode(x, n)
            self.orbit_cache[ckey] = (images, enc, ex,
                                      frozenset(g for g in elements if enc[g] == ex))
        images, enc, ex, stab = self.orbit_cache[ckey]
        witness = parse_witness(wtext.split(";")[0], n) if wtext else None

        def member(w, what):
            if w is None or w not in group:
                self.fail(cid, f"{what} is not an element of G")
                return False
            return True

        if objective == "CANONICAL_IMAGE":
            c = bytes.fromhex(data)
            if c not in enc.values():
                return self.fail(cid, "bytes are not the stream of an element of the orbit")
            if not trace:
                self.fail(cid, "empty trace")
            if ghex != "-":
                self.fail(cid, "group_hex must be -")
            if member(witness, "witness"):
                if enc[witness] != c:
                    self.fail(cid, "x^w is not the returned image")
                elif det and witness != min(g for g in elements if enc[g] == c):
                    self.fail(cid, "deterministic witness is not the least g with x^g = c")
            key = (n, group, min(enc.values()))
            prev = self.images.get(key)
            if prev is None:
                self.images[key] = (c, trace, cid)
            elif prev[0] != c or prev[1] != trace:
                self.fail(cid, f"C_G(x^h) differs from C_G(x) of case {prev[2]} (bytes or trace)")
        elif objective == "LEX_MIN_IMAGE":
            if case["order"] == "CDAG-BYTE-1":
                least = min(enc.values())
                attaining = [g for g in elements if enc[g] == least]
                if bytes.fromhex(data) != least:
                    self.fail(cid, "bytes are not the least stream of the orbit (CDAG-BYTE-1)")
                if ghex != "-":
                    self.fail(cid, "group_hex must be - under CDAG-BYTE-1")
            else:
                keys = {g: simple_upper_key(images[g], n) for g in elements}
                if any(k is None for k in keys.values()):
                    return self.fail(cid, "SIMPLE-UPPER-1 on a graph outside the spec 4.4 class "
                                          "must be UNSUPPORTED_ACTION")
                least_key = min(keys.values())
                attaining = [g for g in elements if keys[g] == least_key]
                if bytes.fromhex(ghex) != least_key:
                    self.fail(cid, "group_hex is not the least SIMPLE-UPPER-1 key")
                if bytes.fromhex(data) not in {enc[g] for g in attaining}:
                    self.fail(cid, "bytes are not the stream of a graph with the least key")
            if member(witness, "witness"):
                if witness not in attaining:
                    self.fail(cid, "witness does not attain the minimum")
                elif det and witness != min(attaining):
                    self.fail(cid, "deterministic witness is not the least attaining g")
            if trace:
                self.fail(cid, "a minimum has no trace")
        elif objective in ("TRANSPORTER_ONE", "TRANSPORTER_COSET"):
            y = parse(case["target"], n)
            ey = encode(y, n)
            solutions = [g for g in elements if enc[g] == ey]
            if trace or data:
                self.fail(cid, "a transporter has no trace or bytes")
            if not solutions:
                if wtext != "-" or ghex != "-":
                    self.fail(cid, "y is not in x^G: witness and group_hex must be -")
                return
            if n > 0 and wtext == "-":
                return self.fail(cid, "y is in x^G but no witness was reported")
            if member(witness, "witness"):
                if witness not in solutions:
                    self.fail(cid, "x^w is not y")
                elif det and witness != solutions[0]:
                    self.fail(cid, "deterministic witness is not the least solution")
            if objective == "TRANSPORTER_ONE":
                if ghex != "-":
                    self.fail(cid, "TRANSPORTER_ONE reports no group")
            elif bytes.fromhex(ghex) != coset_bytes(stab, solutions[0], n):
                self.fail(cid, "group_hex is not Group(A) || Perm(least of A g)")
        elif objective == "STABILISER":
            if bytes.fromhex(ghex) != group_bytes(stab, n):
                self.fail(cid, "group_hex is not Group(A) by spec 9.4")
            if wtext != "-" or trace or data:
                self.fail(cid, "a stabiliser has no witness, trace or bytes")
        elif objective == "CANONICAL_LABELING_COSET":
            rho = tuple(case["labeling"]["rho"])
            lam = witness
            if lam is None or sorted(lam) != list(range(n)):
                return self.fail(cid, "the labeling is not a bijection")
            if mul(lam, inverse(rho)) not in group:
                return self.fail(cid, "lambda is not in G rho")
            c = bytes.fromhex(data)
            if encode(act(x, lam), n) != c:
                self.fail(cid, "x^lambda is not the returned c")
            coset = sorted(mul(a, lam) for a in stab)
            if bytes.fromhex(ghex) != group_bytes(stab, n) + perm_bytes(coset[0]):
                self.fail(cid, "group_hex is not Group(A) || Perm(least of A lambda)")
            if det and lam != coset[0]:
                self.fail(cid, "deterministic labeling is not the least of A lambda")
            if not trace:
                self.fail(cid, "empty trace")
            key = (n, group, ex, frozenset(mul(g, rho) for g in elements))
            prev = self.labelings.get(key)
            if prev is None:
                self.labelings[key] = (c, trace, cid)
            elif prev[0] != c or prev[1] != trace:
                self.fail(cid, f"representative change rho -> k rho changed c or the trace "
                               f"(case {prev[2]})")
        elif objective == "SIGNED_CANONICAL_IMAGE":
            odd = [a for a in sorted(stab) if chi[a] < 0]
            if ";sign=" not in wtext:
                return self.fail(cid, "signed witness lacks ;sign=")
            sign = int(wtext.split(";sign=")[1])
            if odd:
                if sign != 0 or data != "00" or ghex != "-" or trace:
                    return self.fail(cid, "an odd automorphism exists: expected a zero "
                                          "certificate (00, sign 0, no group, no trace)")
                if member(witness, "zero witness") and (witness not in stab or chi[witness] >= 0):
                    self.fail(cid, "the zero witness is not an odd automorphism of x")
                return
            if sign == 0:
                return self.fail(cid, "no odd automorphism, but a zero was certified")
            c = bytes.fromhex(data)
            if c not in enc.values():
                self.fail(cid, "bytes are not the stream of an element of the orbit")
            if member(witness, "witness"):
                if enc[witness] != c:
                    self.fail(cid, "x^t is not the returned c")
                elif chi[witness] != sign:
                    self.fail(cid, "sign is not chi(t)")
                elif det and witness != min(mul(a, witness) for a in stab):
                    self.fail(cid, "deterministic witness is not the least of A t")
            if bytes.fromhex(ghex) != group_bytes(stab, n):
                self.fail(cid, "group_hex is not Group(A)")
            if not trace:
                self.fail(cid, "empty trace")
            key = (n, group, frozenset(chi.items()), min(enc.values()))
            prev = self.signed.get(key)
            if prev is None:
                self.signed[key] = (c, sign, x, cid)
            else:
                pc, ps, px, pid = prev
                if pc != c:
                    self.fail(cid, f"c differs from case {pid} in the same signed orbit")
                h = next((g for g in elements if encode(act(px, g), n) == ex), None)
                if h is None or chi[h] * ps != sign:
                    self.fail(cid, f"s(x^h) != chi(h) s(x) against case {pid}")
        else:
            self.fail(cid, f"objective {objective} is outside the oracle's scope")


def load_records(path):
    out = {}
    for no, line in enumerate(pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 7:
            raise SystemExit(f"{path}:{no}: expected 7 fields")
        out[parts[0]] = dict(zip(FIELDS, parts))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cases")
    ap.add_argument("records")
    ap.add_argument("--max-order", type=int, default=200_000)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)
    data = json.loads(pathlib.Path(args.cases).read_text(encoding="utf-8"))
    cases = data.get("cases") if isinstance(data.get("cases"), list) else data.get("p1_cases")
    records = load_records(args.records)
    o = Oracle(args.max_order, args.verbose)
    missing = 0
    for case in cases:
        rec = records.get(case["id"])
        if rec is None:
            missing += 1
            print(f"MISSING  {case['id']}")
            continue
        before = len(o.failures)
        o.check(case, rec)
        if args.verbose and len(o.failures) == before:
            print(f"OK       {case['id']}")
    print(f"{o.checked} checked, {o.skipped} skipped (beyond oracle range or capacity), "
          f"{len(o.failures)} failed, {missing} missing")
    return 1 if o.failures or missing else 0


if __name__ == "__main__":
    sys.exit(main())
