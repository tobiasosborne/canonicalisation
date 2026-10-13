"""Harness of the independent checker canon-check (slice S7, docs/slices/S7.md 3.4, 4).

Standard library only.  Certificates are produced by canon-cli (`--certificate FILE`, located
through CANON_CLI, default build/make/canon-cli) and checked by canon-check (CANON_CHECK,
default build/make/canon-check):

  * acceptance: every certificate emitted on the T1 subset tier (every subgroup of Sym(n),
    n <= 4, every subset) and the G1 digraph tier (n <= 2, every multiplicity vector in
    {0,1,2}^(n*n), two colourings), under both work policies, is accepted, and the node count
    the checker reports equals the number of node records;
  * mutation rejection, one per rule of docs/slices/S7.md 3.4: a generator, an automorphism,
    a STAGE_O size, u, M, a rebased chain's orbit, a pruned child's automorphism index, a leaf
    t, the final bytes and the node count are tampered with, an explored child is deleted, the
    work policy of a pruned certificate is set to the unpruned one, a byte is appended and the
    stream is truncated; each must be rejected (exit 1) with the named reason where the rule
    pins it.

The parser below mirrors the CERT-0 layout of src/search/certificate.h (restated in
checker/README.md) only to locate fields; it checks nothing.  Certificate sizes and the
checker's node counts are printed for docs/slices/S7-notes.md.  CANON_E2E_FAST=1 samples the
acceptance tiers.
"""
import os
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from itertools import permutations, product

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "checks"))
import review_checks as rc  # noqa: E402

CLI = pathlib.Path(os.environ.get("CANON_CLI") or (ROOT / "build" / "make" / "canon-cli"))
CHECK = pathlib.Path(os.environ.get("CANON_CHECK") or (ROOT / "build" / "make" / "canon-check"))
FAST = os.environ.get("CANON_E2E_FAST") == "1"
REQUIRE = os.environ.get("CANON_REQUIRE_CLI") == "1"
NONE = 0xFFFFFFFF


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def u8(self):
        v = self.data[self.at]
        self.at += 1
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.data, self.at)[0]
        self.at += 2
        return v

    def u32(self):
        v = struct.unpack_from(">I", self.data, self.at)[0]
        self.at += 4
        return v

    def u32s(self, k):
        return [self.u32() for _ in range(k)]

    def b(self):
        n = self.u32()
        v = self.data[self.at:self.at + n]
        self.at += n
        return v


class Parsed:
    """Offsets of interest in a certificate (first occurrence of each)."""

    def __init__(self, data):
        self.data = data
        r = Reader(data)
        assert data[:4] == b"CNC0"
        r.at = 4
        self.ids_at = r.at
        self.objective, self.profile, self.work_policy, self.encoding, self.order = (
            r.u16(), r.u16(), r.u16(), r.u16(), r.u16())
        self.n = r.u32()
        self.stream = r.b()
        self.k = r.u32()
        self.gens_at = r.at
        self.gens = [r.u32s(self.n) for _ in range(self.k)]
        self.root_chain_at = r.at
        self.root_chain = self.chain(r)
        self.m = r.u32()
        self.auts_at = r.at
        self.auts = [r.u32s(self.n) for _ in range(self.m)]
        self.first = {}
        self.node_records = 0
        self.pruned = 0
        self.nodes_at = r.at
        self.node(r, 0)
        self.final_at = r.at
        self.trace = r.b()
        self.bytes_at = r.at
        self.best_bytes = r.b()
        self.witness_at = r.at
        self.witness = r.u32s(self.n)
        self.count_at = r.at
        self.count = struct.unpack_from(">Q", data, r.at)[0]
        r.at += 8
        assert r.at == len(data), "trailing bytes"

    def chain(self, r):
        info = {"at": r.at}
        n, depth = r.u32(), r.u32()
        levels = []
        for _ in range(depth):
            base, rlen = r.u32(), r.u32()
            orbit_at = r.at
            orbit = r.u32s(rlen)
            parents = r.u32s(rlen)
            parent_gens = r.u32s(rlen)
            s = r.u32()
            gen_ids = r.u32s(s)
            levels.append((base, orbit, orbit_at, parents, parent_gens, gen_ids))
        g = r.u32()
        gens = []
        for _ in range(g):
            node = r.u32()
            gens.append((node, r.u32s(n)))
        p = r.u32()
        prov = [(r.u32(), r.u32(), r.u32()) for _ in range(p)]
        i = r.u32()
        inputs = [r.u32s(n) for _ in range(i)]
        info.update(n=n, depth=depth, levels=levels, gens=gens, prov=prov, inputs=inputs)
        return info

    def node(self, r, depth):
        at = r.at
        self.node_records += 1
        d, atom, sweeps = r.u32(), r.u32(), r.u32()
        assert d == depth
        for _ in range(sweeps):
            self.first.setdefault("stage_o_size", r.at + 4)
            k_o = r.u32()
            r.u32s(k_o)
            f = r.u32()
            r.u32s(f)
            self.first.setdefault("u", r.at)
            r.u32s(self.n)
            if f:
                self.first.setdefault("m", r.at)
            r.u32s(f)
            chain = self.chain(r)
            if chain["levels"] and "rebased_orbit" not in self.first:
                self.first["rebased_orbit"] = chain["levels"][0][2]
            k_g = r.u32()
            r.u32s(k_g)
        kind = r.u8()
        if kind == 0:
            self.first.setdefault("leaf_t", r.at)
            r.u32s(self.n)
        else:
            assert kind == 1
            cell, size = r.u32(), r.u32()
            for _ in range(size):
                entry_at = r.at
                ck = r.u8()
                b = r.u32()
                if ck == 1:
                    child_at = r.at
                    self.node(r, depth + 1)
                    self.first.setdefault("explored_child", (entry_at, r.at))
                else:
                    assert ck == 0
                    rep = r.u32()
                    self.first.setdefault("pruned_aut_index", r.at)
                    r.u32()
                    self.pruned += 1
        return at


def run_cli(args):
    return subprocess.run([str(CLI)] + list(args), capture_output=True, text=True, check=False)


def run_check(data):
    """Return (exit status, first output line) of canon-check on the bytes."""
    with tempfile.NamedTemporaryFile(suffix=".cert", delete=False) as f:
        f.write(data)
        path = f.name
    try:
        proc = subprocess.run([str(CHECK), path], capture_output=True, text=True, check=False)
    finally:
        os.unlink(path)
    return proc.returncode, proc.stdout.strip()


def certificate(cmd_args, prune):
    """Run canon-cli with --certificate; return (record fields, certificate bytes)."""
    with tempfile.NamedTemporaryFile(suffix=".cert", delete=False) as f:
        path = f.name
    try:
        proc = run_cli(list(cmd_args) + ["--prune", "on" if prune else "off",
                                         "--certificate", path])
        assert proc.returncode == 0, proc.stderr + proc.stdout
        data = pathlib.Path(path).read_bytes()
    finally:
        os.unlink(path)
    return proc.stdout.rstrip("\n").split("\t"), data


def gens_arg(n, gens):
    return "" if n == 0 else ";".join(",".join(map(str, g)) for g in gens)


def subset_args(n, gens, atoms, cid):
    return ["p1-subset", "--n", str(n), "--gens", gens_arg(n, gens),
            "--atoms", ",".join(map(str, sorted(atoms))), "--id", cid]


def graph_args(n, gens, colours, arcs, cid):
    colour_arg = "" if all(c == b"" for c in colours) else ";".join(c.hex() for c in colours)
    arc_arg = ";".join(f"{a},{b},{label.hex()},{m}" for a, b, label, m in arcs)
    return ["p1-graph", "--n", str(n), "--gens", gens_arg(n, gens), "--colours", colour_arg,
            "--arcs", arc_arg, "--id", cid]


def greedy_generators(group, n):
    known, chosen = rc.closure((), n), []
    while known != group:
        chosen.append(min(group - known))
        known = rc.closure(chosen, n)
    return chosen


class CheckerTestCase(unittest.TestCase):
    def setUp(self):
        for tool in (CLI, CHECK):
            if not tool.is_file():
                msg = f"{tool} not found: run `make` (or set CANON_CLI / CANON_CHECK)"
                if REQUIRE:
                    self.fail(msg)
                self.skipTest(msg)

    def accept(self, args, prune, stats):
        fields, data = certificate(args, prune)
        self.assertEqual(fields[2], "COMPLETE")
        parsed = Parsed(data)
        self.assertEqual(parsed.work_policy, 2 if prune else 1)
        self.assertEqual(bytes.fromhex(fields[3]), parsed.trace)
        self.assertEqual(bytes.fromhex(fields[4]), parsed.best_bytes)
        self.assertEqual(parsed.count, parsed.node_records)
        if not prune:
            self.assertEqual(parsed.pruned, 0)
        t0 = time.perf_counter()
        code, line = run_check(data)
        stats["check_seconds"] += time.perf_counter() - t0
        self.assertEqual((code, line), (0, f"ACCEPT {parsed.node_records}"), fields[0])
        stats["certificates"] += 1
        stats["bytes"] += len(data)
        stats["max_bytes"] = max(stats["max_bytes"], len(data))
        stats["nodes"] += parsed.node_records
        stats["pruned"] += parsed.pruned
        return parsed


class Acceptance(CheckerTestCase):
    def test_t1_subsets(self):
        stats = dict(certificates=0, bytes=0, max_bytes=0, nodes=0, pruned=0, check_seconds=0.0)
        i = 0
        for n in range(5):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                gens = greedy_generators(group, n)
                for mask in range(1 << n):
                    i += 1
                    if FAST and i % 7 != 0:
                        continue
                    atoms = [a for a in range(n) if mask >> a & 1]
                    for prune in (False, True):
                        self.accept(subset_args(n, gens, atoms, f"t1-n{n}-g{gi}-m{mask}"),
                                    prune, stats)
        print("checker T1 subsets:", stats)

    def test_g1_digraphs(self):
        stats = dict(certificates=0, bytes=0, max_bytes=0, nodes=0, pruned=0, check_seconds=0.0)
        i = 0
        for n in range(3):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                gens = greedy_generators(group, n)
                for mult, distinct in product(product(range(3), repeat=n * n), (False, True)):
                    i += 1
                    if FAST and i % 11 != 0:
                        continue
                    colours = tuple(bytes([a % 2]) if distinct else b"" for a in range(n))
                    arcs = tuple((a, b, b"", mult[a * n + b]) for a in range(n) for b in range(n)
                                 if mult[a * n + b])
                    cid = "g1-n%d-g%d-m%s-c%d" % (n, gi, "".join(map(str, mult)), distinct)
                    for prune in (False, True):
                        self.accept(graph_args(n, gens, colours, arcs, cid), prune, stats)
        print("checker G1 digraphs:", stats)

    def test_larger_sample(self):
        # random groups on 5 and 6 points with random subsets and digraphs, both policies
        stats = dict(certificates=0, bytes=0, max_bytes=0, nodes=0, pruned=0, check_seconds=0.0)
        rng = random.Random(7)
        for n in (5, 6):
            for k in range(3 if FAST else 10):
                gens = [list(rng.sample(range(n), n)) for _ in range(rng.choice((1, 2)))]
                atoms = [a for a in range(n) if rng.random() < 0.5]
                for prune in (False, True):
                    self.accept(subset_args(n, gens, atoms, f"r-n{n}-{k}"), prune, stats)
                arcs = tuple((a, b, rng.choice((b"", b"a")), rng.choice((1, 2)))
                             for a in range(n) for b in range(n) if rng.random() < 0.3)
                colours = tuple(rng.choice((b"", b"c")) for _ in range(n))
                for prune in (False, True):
                    self.accept(graph_args(n, gens, colours, arcs, f"rg-n{n}-{k}"), prune, stats)
        print("checker random sample:", stats)


def put_u32(data, at, value):
    return data[:at] + struct.pack(">I", value) + data[at + 4:]


class Mutation(CheckerTestCase):
    """One rejection per rule (docs/slices/S7.md 3.4, 4)."""

    def pruned_case(self):
        # the empty subset under Sym(3): two pruned children at the root, automorphisms listed
        gens = [(1, 2, 0), (1, 0, 2)]
        _, data = certificate(subset_args(3, gens, [], "mut"), True)
        parsed = Parsed(data)
        self.assertGreater(parsed.pruned, 0)
        self.assertGreater(parsed.m, 0)
        return parsed

    def graph_case(self):
        # the spec 7.4 one-arc graph under Sym(2) plus a 3-cycle digraph under Sym(3)
        gens = [(1, 2, 0), (1, 0, 2)]
        arcs = ((0, 1, b"", 1), (1, 2, b"", 1), (2, 0, b"", 2))
        _, data = certificate(graph_args(3, gens, (b"", b"", b""), arcs, "mg"), False)
        return Parsed(data)

    def reject(self, data, reasons, label):
        code, line = run_check(data)
        self.assertEqual(code, 1, f"{label}: {line}")
        self.assertTrue(line.startswith("REJECT "), line)
        if reasons:
            self.assertIn(line[len("REJECT "):], reasons, label)

    def test_accepts_before_mutation(self):
        for parsed in (self.pruned_case(), self.graph_case(), self.branch_case()):
            self.assertEqual(run_check(parsed.data)[0], 0)

    def test_generator(self):
        p = self.pruned_case()
        # swap two images of generator 0: still a bijection, but not the root chain's input
        at = p.gens_at
        a, b = p.gens[0][0], p.gens[0][1]
        data = put_u32(put_u32(p.data, at, b), at + 4, a)
        self.reject(data, {"ROOT_CHAIN_INPUTS", "CHAIN_INPUT_NOT_MEMBER"}, "generator")
        self.reject(put_u32(p.data, at, 7), {"GENERATOR_NOT_BIJECTION"}, "generator range")

    def test_automorphism(self):
        p = self.pruned_case()
        at = p.auts_at
        self.reject(put_u32(p.data, at, 9), {"AUTOMORPHISM_NOT_MEMBER"}, "automorphism range")
        # the automorphism the first pruned child names, replaced by the identity: a member
        # fixing x, but it no longer maps b' to b
        index = struct.unpack_from(">I", p.data, p.first["pruned_aut_index"])[0]
        at = p.auts_at + 4 * p.n * index
        ident = b"".join(struct.pack(">I", v) for v in range(p.n))
        data = p.data[:at] + ident + p.data[at + 4 * p.n:]
        self.reject(data, {"PRUNED_AUTOMORPHISM_IMAGE"}, "automorphism identity")

    def test_automorphism_moving_x(self):
        # a certificate for x = {0} under Sym(3) lists no automorphism; graft the transposition
        # (1 2), a member of G that moves nothing of x ... use x = {0, 1}: (0 1) fixes x, so
        # replace the listed automorphism by (1 2), a member of G that moves x
        gens = [(1, 2, 0), (1, 0, 2)]
        _, data = certificate(subset_args(3, gens, [0, 1], "mx"), True)
        p = Parsed(data)
        if p.m == 0:
            self.skipTest("no automorphism listed")
        swap12 = struct.pack(">III", 0, 2, 1)
        data = p.data[:p.auts_at] + swap12 + p.data[p.auts_at + 12:]
        self.reject(data, {"AUTOMORPHISM_MOVES_X", "PRUNED_AUTOMORPHISM_IMAGE",
                           "PRUNED_AUTOMORPHISM_MOVES_PREFIX"}, "automorphism moves x")

    def test_stage_o_size(self):
        p = self.graph_case()
        at = p.first["stage_o_size"]
        old = struct.unpack_from(">I", p.data, at)[0]
        self.reject(put_u32(p.data, at, old + 1), {"STAGE_O_SIZES"}, "stage O size")

    def test_u(self):
        p = self.graph_case()
        at = p.first["u"]
        r = Reader(p.data)
        r.at = at
        u = r.u32s(p.n)
        data = put_u32(put_u32(p.data, at, u[1]), at + 4, u[0])
        self.reject(data, {"U_NOT_MEMBER", "M_MISMATCH"}, "u swapped")
        self.reject(put_u32(p.data, at, p.n), {"U_NOT_BIJECTION"}, "u range")

    def test_m(self):
        p = self.graph_case()
        self.assertIn("m", p.first)
        at = p.first["m"]
        old = struct.unpack_from(">I", p.data, at)[0]
        self.reject(put_u32(p.data, at, (old + 1) % p.n), {"M_MISMATCH"}, "M")

    def test_rebased_orbit(self):
        p = self.graph_case()
        at = p.first["rebased_orbit"]
        old = struct.unpack_from(">I", p.data, at)[0]
        self.reject(put_u32(p.data, at, (old + 1) % p.n), {"CHAIN_ORBIT_TREE"}, "orbit point")
        self.reject(put_u32(p.data, at, p.n), {"CHAIN_STRUCTURE"}, "orbit range")

    def test_pruned_automorphism_index(self):
        p = self.pruned_case()
        at = p.first["pruned_aut_index"]
        self.reject(put_u32(p.data, at, p.m), {"PRUNED_AUTOMORPHISM_INDEX"}, "aut index")

    def test_leaf_t(self):
        p = self.graph_case()
        at = p.first["leaf_t"]
        r = Reader(p.data)
        r.at = at
        t = r.u32s(p.n)
        data = put_u32(put_u32(p.data, at, t[1]), at + 4, t[0])
        self.reject(data, {"LEAF_T_MISMATCH"}, "leaf t")

    def test_final_bytes(self):
        p = self.graph_case()
        at = p.bytes_at + 4 + len(p.best_bytes) - 1
        data = bytearray(p.data)
        data[at] ^= 0x01
        self.reject(bytes(data), {"FINAL_BYTES"}, "final bytes")
        at = p.final_at + 4 + len(p.trace) - 1
        data = bytearray(p.data)
        data[at] ^= 0x01
        self.reject(bytes(data), {"FINAL_TRACE"}, "final trace")

    def test_final_witness_and_count(self):
        p = self.graph_case()
        w = p.witness
        data = put_u32(put_u32(p.data, p.witness_at, w[1]), p.witness_at + 4, w[0])
        self.reject(data, {"FINAL_WITNESS"}, "witness")
        data = p.data[:p.count_at] + struct.pack(">Q", p.count + 1)
        self.reject(data, {"NODE_COUNT"}, "node count")

    def branch_case(self):
        # the arc-free graph on three vertices under Sym(3), unpruned: three explored children
        gens = [(1, 2, 0), (1, 0, 2)]
        _, data = certificate(graph_args(3, gens, (b"", b"", b""), (), "mb"), False)
        return Parsed(data)

    def test_delete_explored_child(self):
        p = self.branch_case()
        self.assertIn("explored_child", p.first)
        start, end = p.first["explored_child"]
        self.reject(p.data[:start] + p.data[end:], None, "deleted child")

    def test_policy_header(self):
        p = self.pruned_case()
        data = p.data[:p.ids_at + 4] + struct.pack(">H", 1) + p.data[p.ids_at + 6:]
        self.reject(data, {"PRUNED_UNDER_UNPRUNED_POLICY"}, "policy")
        data = p.data[:p.ids_at + 4] + struct.pack(">H", 3) + p.data[p.ids_at + 6:]
        self.reject(data, {"HEADER"}, "unknown policy")
        self.reject(b"CNC1" + p.data[4:], {"HEADER"}, "magic")

    def test_framing(self):
        p = self.graph_case()
        self.reject(p.data + b"\x00", {"TRAILING_BYTES"}, "trailing")
        self.reject(p.data[:-3], {"TRUNCATED"}, "truncated")
        self.reject(p.data[:p.nodes_at + 5], {"TRUNCATED"}, "truncated in a node")

    def test_usage(self):
        proc = subprocess.run([str(CHECK)], capture_output=True, text=True, check=False)
        self.assertEqual(proc.returncode, 2)
        proc = subprocess.run([str(CHECK), "/nonexistent/certificate"], capture_output=True,
                              text=True, check=False)
        self.assertEqual(proc.returncode, 2)


if __name__ == "__main__":
    unittest.main()
