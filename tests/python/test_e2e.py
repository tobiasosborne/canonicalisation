"""End-to-end test of the slice S1 to S4 C paths (docs/slices/S1.md section 5, S2.md section 4,
S4.md section 4).

Drives tools/canon-cli (located through the CANON_CLI environment variable, else
build/make/canon-cli) and compares its refs/compare/FORMAT.md records with the finite Python
model in checks/review_checks.py over the exhaustive T1 subset tier: every subgroup of Sym(n)
for n <= 4 and every subset, with two generating sets per group (the full element list and the
greedy generating sequence of review_checks.run_v2), which must also agree with each other
(metamorphic test, spec section 5).  Also runs every subset entry of refs/vectors/golden.json
and checks the spec 11.1 node quota.

Slice S2 adds the graph tiers, compared field by field with
review_checks.p1(n, group, "graph", (colours, arcs)):
  G1 (exhaustive): n <= 2, every subgroup, every multiplicity vector in {0,1,2}^(n*n) (loops
     included), colours all empty and distinct (bytes([a % 2])): the loop of
     review_checks.run_v2;
  G2 (sampled): n = 3 and 4, every subgroup, 40 seeded random digraphs per group with
     multiplicities in {0,1,2}, labels from {"", "a"}, colours from {"", "c"};
both with the full element list and the greedy generating sequence, which must agree; the
CLI receives the arcs shuffled, with some multiplicities split into duplicate arcs (spec 4.1,
5: insertion order and duplicate storage have no meaning).  Every graph stream the CLI emits
must parse with tools/hexdump_stream.py.  Also the spec 7.4 graph entry of golden.json and a
--max-nodes capacity case.

Slice S3: the group backend is a verified stabiliser chain by default.  Setting
CANON_BACKEND=explicit (or chain) passes `--backend` to every CLI call, so the whole file runs
against either backend; `make check` runs it with both, and both runs must agree with the model
byte for byte.

Slice S4: every record has seven fields (refs/compare/FORMAT.md: group_hex last).  The
EnumerationObjectives class checks LEX_MIN_IMAGE (both orders), TRANSPORTER_ONE, STABILISER,
TRANSPORTER_COSET and the deterministic witness against brute force over explicit groups
(review_checks closure, mul, act_object, subset_bytes, graph_bytes, group_bytes, perm_bytes) on
the T1 subsets, the G1 digraphs and 30 random groups on n <= 6 points, plus SIMPLE-UPPER-1 on
random simple graphs; the witness of the "any" mode is pinned to the spec 8.1 traversal order,
which enumerate_81 below models from the spec text.

Standard library only.  Without a built CLI the tests skip, unless CANON_REQUIRE_CLI=1, which
makes them fail.
"""
import json
import os
import pathlib
import random
import subprocess
import sys
import unittest
from itertools import permutations, product

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "checks"))
import review_checks as rc  # noqa: E402  (no side effects on import)

sys.path.insert(0, str(ROOT / "tools"))
import hexdump_stream as hs  # noqa: E402  (no side effects on import)

GOLDEN = json.loads((ROOT / "refs" / "vectors" / "golden.json").read_text(encoding="utf-8"))
CLI = pathlib.Path(os.environ.get("CANON_CLI") or (ROOT / "build" / "make" / "canon-cli"))
# Slice S3: the group backend under test ("" = the CLI default, the chain).
BACKEND = os.environ.get("CANON_BACKEND", "")
if BACKEND not in ("", "chain", "explicit"):
    raise SystemExit(f"CANON_BACKEND must be chain or explicit, not {BACKEND!r}")


def cli(args):
    """Run canon-cli with the selected backend appended (options may come in any order)."""
    extra = ["--backend", BACKEND] if BACKEND else []
    return subprocess.run([str(CLI)] + list(args) + extra, capture_output=True, text=True,
                          check=False)


def gens_arg(n, gens):
    # Degree 0: every generator is the empty permutation, i.e. the trivial group.
    if n == 0:
        return ""
    return ";".join(",".join(str(v) for v in g) for g in gens)


def run_cli(n, gens, atoms, case_id="e2e", max_nodes=None):
    args = ["p1-subset", "--n", str(n), "--gens", gens_arg(n, gens),
            "--atoms", ",".join(str(a) for a in sorted(atoms)), "--id", case_id]
    if max_nodes is not None:
        args += ["--max-nodes", str(max_nodes)]
    proc = cli(args)
    lines = proc.stdout.splitlines()
    fields = lines[0].split("\t") if len(lines) == 1 else None
    return proc.returncode, fields, proc


def run_graph_cli(n, gens, colours, arcs, case_id="e2e", max_nodes=None):
    """canon-cli p1-graph; colours is a tuple of n byte strings, arcs (s, t, label, m)."""
    colour_arg = "" if all(c == b"" for c in colours) else ";".join(c.hex() for c in colours)
    arc_arg = ";".join(f"{a},{b},{label.hex()},{m}" for a, b, label, m in arcs)
    args = ["p1-graph", "--n", str(n), "--gens", gens_arg(n, gens),
            "--colours", colour_arg, "--arcs", arc_arg, "--id", case_id]
    if max_nodes is not None:
        args += ["--max-nodes", str(max_nodes)]
    proc = cli(args)
    lines = proc.stdout.splitlines()
    fields = lines[0].split("\t") if len(lines) == 1 else None
    return proc.returncode, fields, proc


def scramble_arcs(arcs, rng):
    """Representation change with no meaning (spec 4.1, 5): split some multiplicities into
    duplicate arcs and shuffle the insertion order."""
    out = []
    for a, b, label, m in arcs:
        if m >= 2 and rng.random() < 0.5:
            out += [(a, b, label, 1), (a, b, label, m - 1)]
        else:
            out.append((a, b, label, m))
    rng.shuffle(out)
    return out


def greedy_generators(group, n):
    """review_checks.run_v2: repeatedly add the least element not yet generated."""
    known, chosen = rc.closure((), n), []
    while known != group:
        chosen.append(min(group - known))
        known = rc.closure(chosen, n)
    return chosen


def witness_field(n, witness):
    return "-" if n == 0 else ",".join(str(v) for v in witness)


# A fresh checkout has no CLI: skip with a clear message so that a standalone
# `python3 -m unittest discover -s tests/python` passes.  CANON_REQUIRE_CLI=1 (set by
# `make check`, CMake's test_e2e and CI) turns the skip into a failure.
REQUIRE_CLI = os.environ.get("CANON_REQUIRE_CLI") == "1"
MISSING_CLI = (f"canon-cli not found at {CLI}: run `make` (or set CANON_CLI); "
               "set CANON_REQUIRE_CLI=1 to make this a failure")


class CliTestCase(unittest.TestCase):
    def setUp(self):
        if not CLI.is_file():
            if REQUIRE_CLI:
                self.fail(MISSING_CLI)
            self.skipTest(MISSING_CLI)


class T1SubsetTier(CliTestCase):
    def test_all_subgroups_all_subsets(self):
        cases = 0
        for n in range(5):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                generating_sets = {"full": sorted(group), "greedy": greedy_generators(group, n)}
                for mask in range(1 << n):
                    atoms = frozenset(a for a in range(n) if mask >> a & 1)
                    trace, data, witness = rc.p1(n, group, "subset", atoms)
                    # spec 3 deterministic witness: the least g in G with x^g = c.  By tree
                    # equivariance (spec 7.2) the leaves attaining the least key carry exactly
                    # the coset Aut_G(x) t, so the least leaf witness reported in S1 is it.
                    image = rc.act_object("subset", atoms, witness)
                    self.assertEqual(witness, min(g for g in group
                                                  if rc.act_object("subset", atoms, g) == image))
                    expected = ["0001", "COMPLETE", trace.hex(), data.hex(),
                                witness_field(n, witness), "-"]
                    records = {}
                    for label, gens in generating_sets.items():
                        case_id = f"t1-n{n}-g{gi}-m{mask}-{label}"
                        with self.subTest(case_id):
                            code, fields, proc = run_cli(n, gens, atoms, case_id)
                            self.assertEqual(code, 0, proc.stderr)
                            self.assertIsNotNone(fields, proc.stdout)
                            self.assertEqual(fields[0], case_id)
                            self.assertEqual(fields[1:], expected)
                            records[label] = fields[1:]
                            cases += 1
                    # Metamorphic (spec 5): generator choice must not change any field.
                    self.assertEqual(records.get("full"), records.get("greedy"))
        # 1 + 2 + 8 + 48 + 480 subsets over the 1, 1, 2, 6, 30 subgroups, two generating sets.
        self.assertEqual(cases, 2 * (1 * 1 + 1 * 2 + 2 * 4 + 6 * 8 + 30 * 16))


class GoldenSubsetCases(CliTestCase):
    def test_golden_subsets(self):
        subset_cases = [c for c in GOLDEN["p1_cases"] if c["object"]["kind"] == "subset"]
        self.assertEqual(len(subset_cases), 4)
        for case in subset_cases:
            with self.subTest(case["id"]):
                n = case["n"]
                code, fields, proc = run_cli(n, case["group_generators"],
                                             case["object"]["atoms"], case["id"])
                self.assertEqual(code, 0, proc.stderr)
                self.assertEqual(fields, [case["id"], "%04x" % case["objective_tag"], "COMPLETE",
                                          case["expected_trace_hex"],
                                          case["expected_stream_hex"],
                                          witness_field(n, case["expected_witness"]), "-"])


class GraphTiers(CliTestCase):
    """Slice S2 graph tiers G1 and G2 (docs/slices/S2.md section 4)."""

    def check_graph(self, n, group, generating_sets, colours, arcs, case_id, rng):
        trace, data, witness = rc.p1(n, group, "graph", (colours, arcs))
        # spec 3 deterministic witness: the least g in G with x^g = c (S1 notes, reading 1).
        self.assertEqual(witness, min(g for g in group if rc.graph_bytes(
            n, rc.act_object("graph", (colours, arcs), g)) == data))
        expected = ["0001", "COMPLETE", trace.hex(), data.hex(), witness_field(n, witness), "-"]
        records = {}
        for label, gens in generating_sets.items():
            cid = f"{case_id}-{label}"
            with self.subTest(cid):
                code, fields, proc = run_graph_cli(n, gens, colours, scramble_arcs(arcs, rng),
                                                   cid)
                self.assertEqual(code, 0, proc.stderr)
                self.assertIsNotNone(fields, proc.stdout)
                self.assertEqual(fields[0], cid)
                self.assertEqual(fields[1:], expected)
                # S2 definition of done: every emitted graph stream parses strictly.
                parsed = hs.parse_stream(bytes.fromhex(fields[4]))
                self.assertEqual((parsed["n"], parsed["q"]), (n, 1))
                records[label] = fields[1:]
        # Metamorphic (spec 5): generator choice must not change any field.
        self.assertEqual(records.get("full"), records.get("greedy"))
        return len(records)

    def test_g1_exhaustive(self):
        # Exactly the n <= 2 graph loop of review_checks.run_v2.
        rng = random.Random(20261001)
        cases = 0
        for n in range(3):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                generating_sets = {"full": sorted(group), "greedy": greedy_generators(group, n)}
                for multiplicities, distinct in product(product(range(3), repeat=n * n),
                                                        (False, True)):
                    colours = tuple(bytes([a % 2]) if distinct else b"" for a in range(n))
                    arcs = tuple((a, b, b"", multiplicities[a * n + b])
                                 for a in range(n) for b in range(n)
                                 if multiplicities[a * n + b])
                    case_id = "g1-n%d-g%d-m%s-c%d" % (n, gi, "".join(map(str, multiplicities)),
                                                     distinct)
                    cases += self.check_graph(n, group, generating_sets, colours, arcs,
                                              case_id, rng)
        # (1 + 1 * 3 + 2 * 81) multiplicity vectors x 2 colourings x 2 generating sets.
        self.assertEqual(cases, 2 * 2 * (1 + 3 + 2 * 81))

    def test_g2_sampled(self):
        cases = 0
        for n in (3, 4):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                generating_sets = {"full": sorted(group), "greedy": greedy_generators(group, n)}
                for k in range(40):
                    rng = random.Random(1000003 * n + 1009 * gi + k)
                    density = rng.choice((0.25, 0.5, 1.0))
                    arcs = tuple((a, b, label, rng.choice((1, 2)))
                                 for a in range(n) for b in range(n) for label in (b"", b"a")
                                 if rng.random() < density * 2 / 3)
                    colours = tuple(rng.choice((b"", b"c")) for _ in range(n))
                    case_id = f"g2-n{n}-g{gi}-r{k}"
                    cases += self.check_graph(n, group, generating_sets, colours, arcs,
                                              case_id, rng)
        self.assertEqual(cases, 2 * 40 * (6 + 30))

    def test_golden_graph(self):
        graph_cases = [c for c in GOLDEN["p1_cases"] if c["object"]["kind"] == "graph"]
        self.assertEqual(len(graph_cases), 1)
        for case in graph_cases:
            with self.subTest(case["id"]):
                n, obj = case["n"], case["object"]
                colours = tuple(bytes.fromhex(c) for c in obj["vertex_colours_hex"])
                arcs = [(a["source"], a["target"], bytes.fromhex(a["label_hex"]),
                         a["multiplicity"]) for a in obj["arcs"]]
                code, fields, proc = run_graph_cli(n, case["group_generators"], colours, arcs,
                                                   case["id"])
                self.assertEqual(code, 0, proc.stderr)
                self.assertEqual(fields, [case["id"], "%04x" % case["objective_tag"], "COMPLETE",
                                          case["expected_trace_hex"],
                                          case["expected_stream_hex"],
                                          witness_field(n, case["expected_witness"]), "-"])
                hs.parse_stream(bytes.fromhex(fields[4]))

    def test_graph_node_quota(self):
        # spec 11.1: the arc-free graph on two vertices under Sym(2) has three NODE tokens.
        code, fields, _ = run_graph_cli(2, [(1, 0)], (b"", b""), [], "gquota", max_nodes=1)
        self.assertEqual(code, 3)
        self.assertEqual(fields, ["gquota", "0001", "CAPACITY_LIMIT", "", "", "-", "-"])
        code, fields, _ = run_graph_cli(2, [(1, 0)], (b"", b""), [], "gquota3", max_nodes=3)
        self.assertEqual((code, fields[2]), (0, "COMPLETE"))

    def test_trace_pruned_leaves(self):
        # Review item 1: a directed 2-cycle plus a directed 3-cycle under Sym(5); the C search
        # skips the image of every leaf whose trace exceeds the best one (spec 7.2: trace first)
        # and must still agree with the model, which materialises every leaf.
        n = 5
        group = rc.closure(((1, 0, 2, 3, 4), (1, 2, 3, 4, 0)), n)
        arcs = ((0, 1, b"", 1), (1, 0, b"", 1), (2, 3, b"", 1), (3, 4, b"", 1), (4, 2, b"", 1))
        colours = (b"",) * n
        self.check_graph(n, group, {"full": sorted(group), "greedy": greedy_generators(group, n)},
                         colours, arcs, "pruned", random.Random(5))

    def test_huge_degree(self):
        # Review item 5: the CLI allocates nothing per vertex for the default colours, so a
        # degree above the context's max_n is a deterministic CAPACITY_LIMIT, as for p1-subset.
        for args in (["p1-graph", "--n", "4294967295", "--id", "huge"],
                     ["p1-graph", "--n", "4294967295", "--arcs", "0,1,,1", "--id", "huge"],
                     ["p1-subset", "--n", "4294967295", "--id", "huge"]):
            with self.subTest(args):
                proc = cli(args)
                self.assertEqual(proc.returncode, 3)
                self.assertEqual(proc.stdout.splitlines(),
                                 ["huge\t0001\tCAPACITY_LIMIT\t\t\t-\t-"])

    def test_graph_invalid_and_usage(self):
        # spec 4.1: zero multiplicities and out-of-domain vertices are invalid input.
        for arcs in ([(0, 1, b"", 0)], [(0, 2, b"", 1)]):
            code, fields, _ = run_graph_cli(2, [], (b"", b""), arcs, "gbad")
            self.assertEqual((code, fields[2], fields[3:]),
                             (3, "INVALID_INPUT", ["", "", "-", "-"]))
        for args in (["p1-graph", "--n", "2", "--colours", "00"],          # not N colours
                     ["p1-graph", "--n", "2", "--colours", "0;00"],        # odd hex
                     ["p1-graph", "--n", "2", "--colours", "zz;00"],       # not hex
                     ["p1-graph", "--n", "2", "--arcs", "0,1,,"],          # empty multiplicity
                     ["p1-graph", "--n", "2", "--arcs", "0,1,1"],          # three fields
                     ["p1-graph", "--n", "2", "--arcs", "0,1,,1,5"],       # five fields
                     ["p1-graph", "--n", "2", "--arcs", "0,1,x,1"],        # label not hex
                     ["p1-graph", "--n", "2", "--atoms", "0"],             # subset option
                     ["p1-subset", "--n", "2", "--arcs", "0,1,,1"],        # graph option
                     ["p1-graph", "--n", "1", "--id", "#c"]):
            with self.subTest(args):
                proc = cli(args)
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(proc.stdout, "")


class Backends(CliTestCase):
    """Slice S3: the --backend option and a group beyond the explicit backend's table."""

    def run_backend(self, backend, args):
        return subprocess.run([str(CLI)] + args + ["--backend", backend], capture_output=True,
                              text=True, check=False)

    def test_backend_option(self):
        for bad in ("nope", "", "Chain"):
            proc = self.run_backend(bad, ["p1-subset", "--n", "1"])
            self.assertEqual((proc.returncode, proc.stdout), (2, ""))
        # Both backends give the same record (here Sym(3) and a two-point subset).
        args = ["p1-subset", "--n", "3", "--gens", "1,0,2;1,2,0", "--atoms", "0,2", "--id", "b"]
        chain, explicit = self.run_backend("chain", args), self.run_backend("explicit", args)
        self.assertEqual((chain.returncode, explicit.returncode), (0, 0))
        self.assertEqual(chain.stdout, explicit.stdout)

    def test_group_beyond_explicit_table(self):
        # C_2^17 generated by the 17 transpositions (2i 2i+1) on 34 points has order
        # 131072 > 65536, the default max_group_order of the explicit backend, which therefore
        # refuses it; the chain backend admits it.  The even and the odd points form one
        # orbit of subsets, so both get the same canonical trace and bytes (spec 7.2).
        n = 34
        gens = []
        for i in range(17):
            g = list(range(n))
            g[2 * i], g[2 * i + 1] = 2 * i + 1, 2 * i
            gens.append(g)
        records = []
        for atoms in (range(0, n, 2), range(1, n, 2)):
            args = ["p1-subset", "--n", str(n), "--gens", gens_arg(n, gens),
                    "--atoms", ",".join(map(str, atoms)), "--id", "big"]
            proc = self.run_backend("explicit", args)
            self.assertEqual(proc.returncode, 3)
            self.assertEqual(proc.stdout.splitlines(),
                             ["big\t0001\tCAPACITY_LIMIT\t\t\t-\t-"])
            proc = self.run_backend("chain", args)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            fields = proc.stdout.splitlines()[0].split("\t")
            self.assertEqual(fields[2], "COMPLETE")
            witness = [int(v) for v in fields[5].split(",")]
            # the witness is in G (it maps every pair to itself) and sends x to the image,
            # which is the odd points (the least key, see S3 notes)
            self.assertTrue(all(witness[2 * i] // 2 == i for i in range(17)))
            self.assertEqual(sorted(witness[a] for a in atoms), list(range(1, n, 2)))
            records.append(fields[3:5])
        self.assertEqual(records[0], records[1])


class CliStatuses(CliTestCase):
    def test_node_quota(self):
        # spec 11.1: the n=2 empty subset under Sym(2) has three NODE tokens.
        code, fields, _ = run_cli(2, [(1, 0)], [], "quota", max_nodes=1)
        self.assertEqual(code, 3)
        self.assertEqual(fields, ["quota", "0001", "CAPACITY_LIMIT", "", "", "-", "-"])
        code, fields, _ = run_cli(2, [(1, 0)], [], "quota3", max_nodes=3)
        self.assertEqual(code, 0)
        self.assertEqual(fields[2], "COMPLETE")

    def test_invalid_input(self):
        code, fields, _ = run_cli(2, [(1, 1)], [], "badgen")
        self.assertEqual((code, fields[2], fields[3:]), (3, "INVALID_INPUT", ["", "", "-", "-"]))
        code, fields, _ = run_cli(2, [], [2], "badatom")
        self.assertEqual((code, fields[2]), (3, "INVALID_INPUT"))

    def test_usage_errors(self):
        for args in (["p1-subset"], ["nope", "--n", "1"], ["p1-subset", "--n", "x"],
                     ["p1-subset", "--n", "2", "--gens", "0"],
                     ["p1-subset", "--n", "2", "--atoms", "a"],
                     # FORMAT.md treats '#' lines as comments; ids must be nonempty.
                     ["p1-subset", "--n", "1", "--id", "#c"],
                     ["p1-subset", "--n", "1", "--id", ""],
                     ["p1-subset", "--n", "1", "--id", "a\tb"]):
            with self.subTest(args):
                proc = cli(args)
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(proc.stdout, "")



# ---- Slice S4: the enumeration objectives (docs/slices/S4.md section 4) ----


def enumerate_81(group, n):
    """spec 8.1, the disjoint coset enumerator over an explicit group, written from the spec:
    visit(H, r): H = {id}: yield r; a = least moved point; for b in sorted(a^H):
    t_b = least element of H with a^t_b = b; visit(H_a, t_b r) (t_b first).  Returns the leaves
    in traversal order and the number of visit calls."""
    leaves, nodes = [], [0]

    def visit(h, r):
        nodes[0] += 1
        if len(h) == 1:
            leaves.append(r)
            return
        a = min(v for v in range(n) if any(g[v] != v for g in h))
        h_a = [g for g in h if g[a] == a]
        for b in sorted({g[a] for g in h}):
            t_b = min(g for g in h if g[a] == b)
            visit(h_a, rc.mul(t_b, r))

    visit(sorted(group), tuple(range(n)))
    return leaves, nodes[0]


def object_bytes(n, kind, obj):
    return rc.subset_bytes(n, obj) if kind == "subset" else rc.graph_bytes(n, obj)


def object_args(kind, obj, prefix=""):
    """CLI options for a subset or a graph (colours, arcs); prefix "target-" for a target."""
    if kind == "subset":
        return [f"--{prefix}atoms", ",".join(str(a) for a in sorted(obj))]
    colours, arcs = obj
    colour_arg = "" if all(c == b"" for c in colours) else ";".join(c.hex() for c in colours)
    arc_arg = ";".join(f"{a},{b},{label.hex()},{m}" for a, b, label, m in arcs)
    return [f"--{prefix}colours", colour_arg, f"--{prefix}arcs", arc_arg]


def run_objective(cmd, n, gens, kind, x, y=None, extra=(), case_id="e"):
    args = [cmd, "--n", str(n), "--gens", gens_arg(n, gens), "--id", case_id]
    if cmd.startswith("p1-"):
        args += object_args(kind, x)
    else:
        args += ["--kind", kind] + object_args(kind, x)
    if y is not None:
        args += object_args(kind, y, "target-")
    proc = cli(args + list(extra))
    lines = proc.stdout.splitlines()
    fields = lines[0].split("\t") if len(lines) == 1 else None
    return proc.returncode, fields, proc


def parse_witness(n, text):
    return tuple(range(n)) if n == 0 and text == "-" else tuple(int(v) for v in text.split(","))


class Brute:
    """Brute-force answers for one (G, x, y) by explicit enumeration (review_checks helpers)."""

    def __init__(self, n, group, kind, x, y):
        self.n, self.group, self.kind, self.x, self.y = n, group, kind, x, y
        self.leaves, self.nodes = enumerate_81(group, n)
        self.assertion = sorted(self.leaves) == sorted(group) and len(self.leaves) == len(group)
        enc = {g: object_bytes(n, kind, rc.act_object(kind, x, g)) for g in group}
        self.enc = enc
        # minimum (CDAG-BYTE-1): spec 4.3 unsigned bytes, the first attaining leaf and the least
        self.cmin = min(enc.values())
        self.min_first = next(g for g in self.leaves if enc[g] == self.cmin)
        self.min_least = min(g for g in group if enc[g] == self.cmin)
        # stabiliser A and the transporters to y
        ex = object_bytes(n, kind, x)
        self.stab = frozenset(g for g in group if enc[g] == ex)
        ey = object_bytes(n, kind, y)
        self.solutions = sorted(g for g in group if enc[g] == ey)
        self.first_hit = next((g for g in self.leaves if enc[g] == ey), None)

    def coset_payload(self):
        g = self.solutions[0]
        coset = {rc.mul(a, g) for a in self.stab}
        assert coset == set(self.solutions)  # spec 8.2: all solutions lie in A g
        return rc.group_bytes(self.stab) + rc.perm_bytes(min(coset))


class EnumerationObjectives(CliTestCase):
    """S4: LEX_MIN_IMAGE, TRANSPORTER_ONE, STABILISER, TRANSPORTER_COSET and the deterministic
    witness against brute force, with the witness of the "any" mode pinned to the spec 8.1
    traversal order."""

    def check_case(self, n, group, gens, kind, x, y, cid, full_gens=None):
        b = Brute(n, group, kind, x, y)
        self.assertTrue(b.assertion)  # the model traversal itself: exactly G, each once
        runs = 0

        def run(cmd, *extra, target=None, case=""):
            nonlocal runs
            runs += 1
            code, fields, proc = run_objective(cmd, n, gens, kind, x, target, extra,
                                               f"{cid}-{case or cmd}")
            self.assertEqual(code, 0, proc.stderr)
            self.assertIsNotNone(fields, proc.stdout)
            self.assertEqual(fields[2], "COMPLETE")
            return fields

        # LEX_MIN_IMAGE: the least stream; the witness is the first attaining leaf, or the
        # least attaining g in deterministic mode (spec 3)
        f = run("min")
        self.assertEqual(f[1:], ["0002", "COMPLETE", "", b.cmin.hex(),
                                 witness_field(n, b.min_first), "-"])
        f = run("min", "--witness", "deterministic", case="min-det")
        self.assertEqual(f[4:], [b.cmin.hex(), witness_field(n, b.min_least), "-"])
        if full_gens is not None:
            # metamorphic (spec 5): the generators do not change the traversal or the answer
            code, g2, _ = run_objective("min", n, full_gens, kind, x, None, (), f"{cid}-full")
            self.assertEqual(g2[1:], f[1:4] + [b.cmin.hex(), witness_field(n, b.min_first), "-"])
            runs += 1
        # STABILISER: canonical Group(A) bytes (spec 9.4)
        f = run("stabiliser")
        self.assertEqual(f[1:], ["0004", "COMPLETE", "", "", "-", rc.group_bytes(b.stab).hex()])
        # TRANSPORTER_ONE: exists iff y in x^G; the first hit of the traversal
        f = run("transporter", target=y)
        if b.solutions:
            self.assertEqual(f[3:], ["", "", witness_field(n, b.first_hit), "-"])
        else:
            self.assertEqual(f[3:], ["", "", "-", "-"])
        # TRANSPORTER_COSET: empty, or Group(A) || Perm(r0)
        f = run("transporter-coset", target=y)
        fd = run("transporter-coset", "--witness", "deterministic", target=y, case="coset-det")
        if b.solutions:
            payload = b.coset_payload().hex()
            self.assertEqual(f[3:], ["", "", witness_field(n, b.first_hit), payload])
            self.assertEqual(fd[3:], ["", "", witness_field(n, b.solutions[0]), payload])
        else:
            self.assertEqual(f[3:], ["", "", "-", "-"])
            self.assertEqual(fd[3:], ["", "", "-", "-"])
        # CANONICAL_IMAGE with the deterministic witness: the least g with x^g = c, which for
        # the unpruned tree is also the least attaining leaf witness (S1 notes, reading 1)
        trace, data, witness = rc.p1(n, group, kind, x)
        f = run("p1-" + kind, "--witness", "deterministic", case="p1-det")
        least = min(g for g in group if b.enc[g] == data)
        self.assertEqual(least, witness)
        self.assertEqual(f[1:], ["0001", "COMPLETE", trace.hex(), data.hex(),
                                 witness_field(n, least), "-"])
        if kind == "graph":
            hs.parse_stream(bytes.fromhex(f[4]))
        return runs, b

    def test_t1_subsets(self):
        # every subgroup of Sym(n), n <= 4, every subset; the target is x^h for an h of Sym(n)
        # chosen by the case number, in the orbit or not
        runs = cases = with_solution = 0
        group_bytes_checked = set()
        for n in range(5):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                gens = greedy_generators(group, n)
                for mask in range(1 << n):
                    x = frozenset(a for a in range(n) if mask >> a & 1)
                    h = symmetric[(31 * gi + 7 * mask) % len(symmetric)]
                    y = rc.act_object("subset", x, h)
                    full = sorted(group) if mask % 4 == 0 else None
                    r, b = self.check_case(n, group, gens, "subset", x, y,
                                           f"s4t1-n{n}-g{gi}-m{mask}", full)
                    runs += r
                    cases += 1
                    with_solution += bool(b.solutions)
                    if not x:
                        group_bytes_checked.add((n, gi))  # Stab(empty) = G: Group(G) bytes
        self.assertEqual(cases, 1 + 2 + 8 + 48 + 480)
        self.assertEqual(len(group_bytes_checked), 40)  # every T1 group's Group(G) bytes
        self.assertTrue(0 < with_solution < cases)

    def test_g1_digraphs(self):
        # the G1 digraphs (n <= 2, every subgroup, every multiplicity vector in {0,1,2}^(n*n),
        # colours all empty and distinct); the target is x^h for a random h of Sym(n), or a
        # random other graph
        rng = random.Random(4004)
        cases = 0
        for n in range(3):
            symmetric = tuple(permutations(range(n)))
            for gi, group in enumerate(rc.subgroups(symmetric, n)):
                gens = greedy_generators(group, n)
                for multiplicities, distinct in product(product(range(3), repeat=n * n),
                                                        (False, True)):
                    colours = tuple(bytes([a % 2]) if distinct else b"" for a in range(n))
                    arcs = tuple((a, b, b"", multiplicities[a * n + b])
                                 for a in range(n) for b in range(n)
                                 if multiplicities[a * n + b])
                    x = (colours, arcs)
                    if rng.random() < 0.8:
                        y = rc.act_object("graph", x, rng.choice(symmetric))
                    else:
                        y = (colours, tuple((a, b, b"", rng.choice((1, 2)))
                                            for a in range(n) for b in range(n)
                                            if rng.random() < 0.5))
                    cid = "s4g1-n%d-g%d-m%s-c%d" % (n, gi, "".join(map(str, multiplicities)),
                                                   distinct)
                    self.check_case(n, group, gens, "graph", x, y, cid)
                    cases += 1
        self.assertEqual(cases, 2 * (1 + 3 + 2 * 81))

    def test_random_groups(self):
        # 30 seeded random groups on n <= 6 points (1-3 generators, each a uniform permutation,
        # a transposition or a cycle), each with random subsets and random digraphs
        cases = 0
        for k in range(30):
            rng = random.Random(77000 + k)
            n = rng.randint(1, 6)
            gens = []
            for _ in range(rng.randint(1, 3)):
                choice = rng.random()
                p = list(range(n))
                if choice < 0.4:
                    rng.shuffle(p)
                else:
                    pts = rng.sample(range(n), min(n, rng.randint(2, 3) if n > 1 else 1))
                    for i, a in enumerate(pts):
                        p[a] = pts[(i + 1) % len(pts)]
                gens.append(tuple(p))
            group = rc.closure(gens, n)
            symmetric = tuple(permutations(range(n)))
            for j in range(3):
                x = frozenset(a for a in range(n) if rng.random() < 0.5)
                y = rc.act_object("subset", x, rng.choice(symmetric))
                self.check_case(n, group, gens, "subset", x, y, f"s4r{k}-s{j}")
                cases += 1
            for j in range(2):
                arcs = tuple((a, b, label, rng.choice((1, 2)))
                             for a in range(n) for b in range(n) for label in (b"", b"a")
                             if rng.random() < 0.25)
                colours = tuple(rng.choice((b"", b"c")) for _ in range(n))
                x = (colours, arcs)
                y = rc.act_object("graph", x, rng.choice(symmetric))
                self.check_case(n, group, gens, "graph", x, y, f"s4r{k}-d{j}")
                cases += 1
        self.assertEqual(cases, 150)

    def test_simple_upper(self):
        # spec 4.4: SIMPLE-UPPER-1 minimum on random simple undirected graphs against the
        # brute-force minimum of the upper-triangle bit tuples, under Sym(n) and random groups
        rng = random.Random(4404)
        pairs = lambda n: [(i, j) for j in range(n) for i in range(j)]  # (0,1),(0,2),(1,2),...
        for k in range(40):
            n = rng.randint(0, 6)
            edges = [e for e in pairs(n) if rng.random() < 0.45]
            if k % 2 == 0:
                gens = [tuple([1, 0] + list(range(2, n))), tuple(list(range(1, n)) + [0])] \
                    if n >= 2 else []
            else:
                gens = []
                for _ in range(rng.randint(1, 2)):
                    p = list(range(n))
                    rng.shuffle(p)
                    gens.append(tuple(p))
            group = rc.closure(gens, n)
            arcs = tuple((a, b, b"", 1) for i, j in edges for a, b in ((i, j), (j, i)))
            x = ((b"",) * n, arcs)

            def bits(g):
                renamed = {tuple(sorted((g[i], g[j]))) for i, j in edges}
                return tuple(int(e in renamed) for e in pairs(n))

            best = min(bits(g) for g in group)
            least = min(g for g in group if bits(g) == best)
            key = bytearray(rc.u32(n) + bytes((len(pairs(n)) + 7) // 8))
            for i, bit in enumerate(best):
                if bit:
                    key[4 + i // 8] |= 0x80 >> (i % 8)  # most significant bit first
            image = rc.graph_bytes(n, rc.act_object("graph", x, least))
            for mode in ("any", "deterministic"):
                code, f, proc = run_objective("min", n, gens, "graph", x, None,
                                              ("--order", "simple-upper", "--witness", mode),
                                              f"su{k}-{mode}")
                self.assertEqual(code, 0, proc.stderr)
                self.assertEqual(f[1:5], ["0002", "COMPLETE", "", image.hex()])
                self.assertEqual(f[6], bytes(key).hex())
                w = parse_witness(n, f[5])
                self.assertIn(w, group)
                self.assertEqual(bits(w), best)
                if mode == "deterministic":
                    self.assertEqual(w, least)
                hs.parse_stream(bytes.fromhex(f[4]))

    def test_golden(self):
        # spec 7.4: "For the second case CDAG-BYTE-1 minimum is {0}, with identity witness,
        # whereas P1 returns {1}"
        case = GOLDEN["other_hand_checked"]["min_order"]
        n = case["n"]
        code, f, _ = run_objective("min", n, case["generators"], "subset",
                                   frozenset(case["atoms"]), None, (), case["id"])
        self.assertEqual(code, 0)
        self.assertEqual(f[4:], [rc.subset_bytes(n, frozenset(case["min_subset"])).hex(),
                                 witness_field(n, case["witness"]), "-"])
        code, f, _ = run_cli(n, case["generators"], case["atoms"], "p1")
        self.assertEqual(f[4], rc.subset_bytes(n, frozenset({1})).hex())
        # spec 7.4 group and labeling-coset payloads through the stabiliser of the empty
        # subset (Stab = G) and the coset {0} -> {1} under <[1,0]> (A = 1, r0 = [1,0])
        for gp in GOLDEN["group_payload_cases"]:
            with self.subTest(gp["id"]):
                if gp["kind"] == "Group":
                    code, f, _ = run_objective("stabiliser", gp["n"], gp["generators"], "subset",
                                               frozenset(), None, (), gp["id"])
                else:
                    code, f, _ = run_objective("transporter-coset", gp["n"], [gp["r"]],
                                               "subset", frozenset({0}), frozenset({1}), (),
                                               gp["id"])
                    self.assertEqual(f[5], witness_field(gp["n"], gp["r"]))
                self.assertEqual((code, f[6]), (0, gp["payload_hex"]))

    def test_quota(self):
        # spec 11.1: the quota counts the visit calls of the spec 8.1 traversal(s); the outcome
        # is CAPACITY_LIMIT iff they exceed it.  Transporter coset: both enumerations count.
        rng = random.Random(1101)
        for k in range(12):
            n = rng.randint(1, 5)
            p = list(range(n))
            rng.shuffle(p)
            gens = [tuple(p), tuple(list(range(1, n)) + [0])]
            group = rc.closure(gens, n)
            x = frozenset(a for a in range(n) if rng.random() < 0.5)
            _, nodes = enumerate_81(group, n)
            for cmd, total in (("min", nodes), ("stabiliser", nodes)):
                for quota, status in ((total, "COMPLETE"), (total - 1, "CAPACITY_LIMIT")):
                    if quota == 0:
                        continue  # 0 selects the context default
                    code, f, _ = run_objective(cmd, n, gens, "subset", x, None,
                                               ("--max-nodes", str(quota)), "q")
                    self.assertEqual(f[2], status, (cmd, n, quota))
                    if status != "COMPLETE":
                        self.assertEqual((code, f[3:]), (3, ["", "", "-", "-"]))
        # coset: x = {0} to y = {1} under Sym(2): the first traversal stops at its hit
        # (root, leaf id, leaf [1,0]: 3 visits), then the stabiliser traversal visits 3 more
        for quota, status in ((6, "COMPLETE"), (5, "CAPACITY_LIMIT"), (3, "CAPACITY_LIMIT")):
            code, f, _ = run_objective("transporter-coset", 2, [(1, 0)], "subset",
                                       frozenset({0}), frozenset({1}),
                                       ("--max-nodes", str(quota)), "qc")
            self.assertEqual(f[2], status, quota)
        # canonical image with the deterministic witness: P1 NODE tokens (3 here) plus the
        # stabiliser traversal (3 visits) share one quota
        for quota, status in ((6, "COMPLETE"), (5, "CAPACITY_LIMIT")):
            code, f, _ = run_objective("p1-subset", 2, [(1, 0)], "subset", frozenset(), None,
                                       ("--max-nodes", str(quota), "--witness", "deterministic"),
                                       "qp")
            self.assertEqual(f[2], status, quota)

    def test_statuses_and_usage(self):
        # spec 4.4: SIMPLE-UPPER-1 only for the uncoloured simple undirected class
        for kind, x in (("subset", frozenset({0})),
                        ("graph", ((b"", b""), ((0, 1, b"", 1),))),        # one-way arc
                        ("graph", ((b"", b"c"), ())),                      # coloured
                        ("graph", ((b"", b""), ((0, 1, b"", 2), (1, 0, b"", 2))))):
            code, f, _ = run_objective("min", 2, [(1, 0)], kind, x, None,
                                       ("--order", "simple-upper"), "su")
            self.assertEqual((code, f), (3, ["su", "0002", "UNSUPPORTED_ACTION", "", "", "-",
                                             "-"]))
        # no deterministic witness for TRANSPORTER_ONE and STABILISER (canon.h)
        code, f, _ = run_objective("stabiliser", 2, [(1, 0)], "subset", frozenset(), None,
                                   ("--witness", "deterministic"), "sd")
        self.assertEqual((code, f[2]), (3, "UNSUPPORTED_ACTION"))
        code, f, _ = run_objective("transporter", 2, [(1, 0)], "subset", frozenset(),
                                   frozenset(), ("--witness", "deterministic"), "td")
        self.assertEqual((code, f[2]), (3, "UNSUPPORTED_ACTION"))
        # a degree beyond the context limit
        proc = cli(["stabiliser", "--n", "4294967295", "--id", "huge"])
        self.assertEqual((proc.returncode, proc.stdout), (3, "huge\t0004\tCAPACITY_LIMIT\t\t\t-\t-\n"))
        # invalid objects are INVALID_INPUT
        code, f, _ = run_objective("transporter", 2, [], "subset", frozenset(), frozenset({2}),
                                   (), "bad")
        self.assertEqual((code, f[2]), (3, "INVALID_INPUT"))
        for args in (["min", "--n", "2", "--target-atoms", "0"],           # no target for min
                     ["stabiliser", "--n", "2", "--order", "cdag"],        # order only for min
                     ["min", "--n", "2", "--order", "nope"],
                     ["min", "--n", "2", "--kind", "tree"],
                     ["min", "--n", "2", "--witness", "some"],
                     ["min", "--n", "2", "--kind", "subset", "--arcs", ""],
                     ["min", "--n", "2", "--kind", "graph", "--atoms", "0"],
                     ["transporter", "--n", "2", "--atoms", "0", "--target-arcs", ""],
                     ["p1-subset", "--n", "2", "--kind", "subset"],        # S4 options only
                     ["p1-graph", "--n", "2", "--target-arcs", ""],
                     ["transporter-coset", "--n", "1", "--id", "#c"]):
            with self.subTest(args):
                proc = cli(args)
                self.assertEqual((proc.returncode, proc.stdout), (2, ""))


if __name__ == "__main__":
    unittest.main()
