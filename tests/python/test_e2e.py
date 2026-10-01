"""End-to-end test of the slice S1 and S2 C paths (docs/slices/S1.md section 5, S2.md section 4).

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


def gens_arg(n, gens):
    # Degree 0: every generator is the empty permutation, i.e. the trivial group.
    if n == 0:
        return ""
    return ";".join(",".join(str(v) for v in g) for g in gens)


def run_cli(n, gens, atoms, case_id="e2e", max_nodes=None):
    args = [str(CLI), "p1-subset", "--n", str(n), "--gens", gens_arg(n, gens),
            "--atoms", ",".join(str(a) for a in sorted(atoms)), "--id", case_id]
    if max_nodes is not None:
        args += ["--max-nodes", str(max_nodes)]
    proc = subprocess.run(args, capture_output=True, text=True, check=False)
    lines = proc.stdout.splitlines()
    fields = lines[0].split("\t") if len(lines) == 1 else None
    return proc.returncode, fields, proc


def run_graph_cli(n, gens, colours, arcs, case_id="e2e", max_nodes=None):
    """canon-cli p1-graph; colours is a tuple of n byte strings, arcs (s, t, label, m)."""
    colour_arg = "" if all(c == b"" for c in colours) else ";".join(c.hex() for c in colours)
    arc_arg = ";".join(f"{a},{b},{label.hex()},{m}" for a, b, label, m in arcs)
    args = [str(CLI), "p1-graph", "--n", str(n), "--gens", gens_arg(n, gens),
            "--colours", colour_arg, "--arcs", arc_arg, "--id", case_id]
    if max_nodes is not None:
        args += ["--max-nodes", str(max_nodes)]
    proc = subprocess.run(args, capture_output=True, text=True, check=False)
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
                                witness_field(n, witness)]
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
                                          witness_field(n, case["expected_witness"])])


class GraphTiers(CliTestCase):
    """Slice S2 graph tiers G1 and G2 (docs/slices/S2.md section 4)."""

    def check_graph(self, n, group, generating_sets, colours, arcs, case_id, rng):
        trace, data, witness = rc.p1(n, group, "graph", (colours, arcs))
        # spec 3 deterministic witness: the least g in G with x^g = c (S1 notes, reading 1).
        self.assertEqual(witness, min(g for g in group if rc.graph_bytes(
            n, rc.act_object("graph", (colours, arcs), g)) == data))
        expected = ["0001", "COMPLETE", trace.hex(), data.hex(), witness_field(n, witness)]
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
                                          witness_field(n, case["expected_witness"])])
                hs.parse_stream(bytes.fromhex(fields[4]))

    def test_graph_node_quota(self):
        # spec 11.1: the arc-free graph on two vertices under Sym(2) has three NODE tokens.
        code, fields, _ = run_graph_cli(2, [(1, 0)], (b"", b""), [], "gquota", max_nodes=1)
        self.assertEqual(code, 3)
        self.assertEqual(fields, ["gquota", "0001", "CAPACITY_LIMIT", "", "", "-"])
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
                proc = subprocess.run([str(CLI)] + args, capture_output=True, text=True,
                                      check=False)
                self.assertEqual(proc.returncode, 3)
                self.assertEqual(proc.stdout.splitlines(),
                                 ["huge\t0001\tCAPACITY_LIMIT\t\t\t-"])

    def test_graph_invalid_and_usage(self):
        # spec 4.1: zero multiplicities and out-of-domain vertices are invalid input.
        for arcs in ([(0, 1, b"", 0)], [(0, 2, b"", 1)]):
            code, fields, _ = run_graph_cli(2, [], (b"", b""), arcs, "gbad")
            self.assertEqual((code, fields[2], fields[3:]), (3, "INVALID_INPUT", ["", "", "-"]))
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
                proc = subprocess.run([str(CLI)] + args, capture_output=True, text=True,
                                      check=False)
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(proc.stdout, "")


class CliStatuses(CliTestCase):
    def test_node_quota(self):
        # spec 11.1: the n=2 empty subset under Sym(2) has three NODE tokens.
        code, fields, _ = run_cli(2, [(1, 0)], [], "quota", max_nodes=1)
        self.assertEqual(code, 3)
        self.assertEqual(fields, ["quota", "0001", "CAPACITY_LIMIT", "", "", "-"])
        code, fields, _ = run_cli(2, [(1, 0)], [], "quota3", max_nodes=3)
        self.assertEqual(code, 0)
        self.assertEqual(fields[2], "COMPLETE")

    def test_invalid_input(self):
        code, fields, _ = run_cli(2, [(1, 1)], [], "badgen")
        self.assertEqual((code, fields[2], fields[3:]), (3, "INVALID_INPUT", ["", "", "-"]))
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
                proc = subprocess.run([str(CLI)] + args, capture_output=True, text=True,
                                      check=False)
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(proc.stdout, "")


if __name__ == "__main__":
    unittest.main()
