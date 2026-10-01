"""End-to-end test of the slice S1 C path (docs/slices/S1.md section 5).

Drives tools/canon-cli (located through the CANON_CLI environment variable, else
build/make/canon-cli) and compares its refs/compare/FORMAT.md records with the finite Python
model in checks/review_checks.py over the exhaustive T1 subset tier: every subgroup of Sym(n)
for n <= 4 and every subset, with two generating sets per group (the full element list and the
greedy generating sequence of review_checks.run_v2), which must also agree with each other
(metamorphic test, spec section 5).  Also runs every subset entry of refs/vectors/golden.json
and checks the spec 11.1 node quota.  Standard library only; requires a prior build.
"""
import json
import os
import pathlib
import subprocess
import sys
import unittest
from itertools import permutations

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "checks"))
import review_checks as rc  # noqa: E402  (no side effects on import)

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


def greedy_generators(group, n):
    """review_checks.run_v2: repeatedly add the least element not yet generated."""
    known, chosen = rc.closure((), n), []
    while known != group:
        chosen.append(min(group - known))
        known = rc.closure(chosen, n)
    return chosen


def witness_field(n, witness):
    return "-" if n == 0 else ",".join(str(v) for v in witness)


class CliAvailable(unittest.TestCase):
    def test_cli_built(self):
        self.assertTrue(CLI.is_file(), f"canon-cli not found at {CLI}; run `make` or set CANON_CLI")


@unittest.skipUnless(CLI.is_file(), "canon-cli not built (reported by CliAvailable)")
class T1SubsetTier(unittest.TestCase):
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


@unittest.skipUnless(CLI.is_file(), "canon-cli not built (reported by CliAvailable)")
class GoldenSubsetCases(unittest.TestCase):
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


@unittest.skipUnless(CLI.is_file(), "canon-cli not built (reported by CliAvailable)")
class CliStatuses(unittest.TestCase):
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
                     ["p1-subset", "--n", "2", "--atoms", "a"]):
            with self.subTest(args):
                proc = subprocess.run([str(CLI)] + args, capture_output=True, text=True,
                                      check=False)
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(proc.stdout, "")


if __name__ == "__main__":
    unittest.main()
