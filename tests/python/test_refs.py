"""Tests of the M0 tooling (docs/slices/M0a.md): the ref-a runner refs/ref-a/run_cases.py, the
case generator tools/gen_cases.py and the exhaustive oracle refs/oracle/oracle.py.

The runner must reproduce every expected field of refs/vectors/golden.json (trace, stream,
witness) through canon-cli; the generator must be deterministic for a seed and write
schema-valid cases; the oracle must accept ref-a's records on golden.json and on a seeded T2
sample, and reject a corrupted record.  Without a built CLI the CLI-based tests skip, unless
CANON_REQUIRE_CLI=1.  Standard library only.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / "refs" / "ref-a" / "run_cases.py"
GEN = ROOT / "tools" / "gen_cases.py"
ORACLE = ROOT / "refs" / "oracle" / "oracle.py"
GOLDEN = ROOT / "refs" / "vectors" / "golden.json"
CLI = pathlib.Path(os.environ.get("CANON_CLI") or (ROOT / "build" / "make" / "canon-cli"))
REQUIRE_CLI = os.environ.get("CANON_REQUIRE_CLI") == "1"
FAST = os.environ.get("CANON_E2E_FAST") == "1"


def run(script, *args):
    return subprocess.run([sys.executable, str(script)] + [str(a) for a in args],
                          capture_output=True, text=True, check=False)


def records(text):
    out = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            f = line.split("\t")
            out[f[0]] = f
    return out


class Generator(unittest.TestCase):
    def test_deterministic_and_schema(self):
        a = run(GEN, "--tier", "T2", "--seed", "7", "--count", "3")
        b = run(GEN, "--tier", "T2", "--seed", "7", "--count", "3")
        self.assertEqual(a.returncode, 0, a.stderr)
        self.assertEqual(a.stdout, b.stdout)
        data = json.loads(a.stdout)
        self.assertEqual((data["schema"], data["tier"], data["seed"]), ("cases-1", "T2", 7))
        self.assertEqual(len(data["cases"]), data["count"])
        self.assertGreater(data["count"], 0)
        ids = [c["id"] for c in data["cases"]]
        self.assertEqual(len(ids), len(set(ids)))
        absent = {1: ("target", "character", "order", "labeling"),
                  2: ("target", "character", "labeling"), 3: ("character", "order", "labeling"),
                  4: ("target", "character", "order", "labeling"),
                  5: ("target", "character", "order"), 6: ("character", "order", "labeling"),
                  7: ("target", "order")}
        required = {2: ("order",), 3: ("target",), 5: ("labeling",), 6: ("target",),
                    7: ("character",)}
        for c in data["cases"]:
            for key in ("id", "n", "group_generators", "object", "objective", "objective_tag",
                        "profile", "encoding", "witness_mode", "capacity"):
                self.assertIn(key, c, c["id"])
            self.assertEqual(c["witness_mode"], "deterministic")
            self.assertEqual(c["capacity"]["work_policy"], 1)
            for key in absent[c["objective_tag"]]:
                self.assertNotIn(key, c, c["id"])
            for key in required.get(c["objective_tag"], ()):
                self.assertIn(key, c, c["id"])
            for g in c["group_generators"]:
                self.assertEqual(sorted(g), list(range(c["n"])))
        # the other tiers run and differ by seed
        t3 = run(GEN, "--tier", "T3", "--seed", "1", "--count", "6")
        self.assertEqual(t3.returncode, 0, t3.stderr)
        self.assertNotEqual(json.loads(t3.stdout)["cases"][0]["group_generators"],
                            json.loads(run(GEN, "--tier", "T3", "--seed", "2", "--count", "6")
                                       .stdout)["cases"][0]["group_generators"])


class CliCase(unittest.TestCase):
    def setUp(self):
        if not CLI.is_file():
            message = f"canon-cli not found at {CLI}"
            if REQUIRE_CLI:
                self.fail(message)
            self.skipTest(message)


class Runner(CliCase):
    def test_golden_reproduced(self):
        proc = run(RUNNER, "--cli", CLI, GOLDEN)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        recs = records(proc.stdout)
        golden = json.loads(GOLDEN.read_text(encoding="utf-8"))["p1_cases"]
        self.assertEqual(len(recs), len(golden))
        for case in golden:
            f = recs[case["id"]]
            witness = "-" if case["n"] == 0 else ",".join(map(str, case["expected_witness"]))
            self.assertEqual(f[1:], ["0001", "COMPLETE", case["expected_trace_hex"],
                                     case["expected_stream_hex"], witness, "-"], case["id"])

    def test_statuses_without_cli(self):
        cases = {"cases": [
            {"id": "a", "n": 2, "group_generators": [[1, 0]], "character": [-1],
             "labeling": {"rho": [0, 1]}, "object": {"kind": "subset", "atoms": [0]},
             "objective": "SIGNED_CANONICAL_IMAGE", "objective_tag": 7, "profile": 1, "encoding": 2},
            {"id": "b", "n": 2, "group_generators": [], "object": {"kind": "subset", "atoms": []},
             "objective": "CONSTRAINT_ONE", "objective_tag": 8, "profile": 0, "encoding": 2},
            {"id": "c", "n": 2, "group_generators": [], "object": {"kind": "subset", "atoms": []},
             "objective": "STABILISER", "objective_tag": 3, "profile": 0, "encoding": 2},
            {"id": "d", "n": 2, "group_generators": [], "object": {"kind": "subset", "atoms": []},
             "objective": "STABILISER", "objective_tag": 4, "profile": 0, "encoding": 2,
             "capacity": {"work_policy": 9}},
            {"id": "e", "n": 3, "group_generators": [[1, 2, 0]],
             "object": {"kind": "dag", "root": 1, "records": [
                 {"tag": 1, "atom": 2}, {"tag": 3, "children": [0, 2, 0]},
                 {"tag": 2, "literal_hex": "61"}]},
             "objective": "CANONICAL_IMAGE", "objective_tag": 1, "profile": 1, "encoding": 2},
        ]}
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp, "c.json")
            path.write_text(json.dumps(cases), encoding="utf-8")
            proc = run(RUNNER, "--cli", CLI, path)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        recs = records(proc.stdout)
        self.assertEqual(recs["a"][2], "UNSUPPORTED_ACTION")
        self.assertEqual(recs["b"][1:3], ["0008", "UNSUPPORTED_ACTION"])
        self.assertEqual(recs["c"][2], "INVALID_INPUT")
        self.assertEqual(recs["d"][2], "UNSUPPORTED_ACTION")
        # a dag whose children are listed after the parent: reordered for the wire, solved
        self.assertEqual(recs["e"][2], "COMPLETE")
        self.assertTrue(recs["e"][4].startswith("434e02"))


class OracleOnRefA(CliCase):
    def oracle(self, cases_path, records_text, *extra):
        with tempfile.TemporaryDirectory() as tmp:
            rec = pathlib.Path(tmp, "r.tsv")
            rec.write_text(records_text, encoding="utf-8")
            return run(ORACLE, cases_path, rec, *extra)

    def test_golden_and_t2_sample_accepted_and_corruption_rejected(self):
        proc = run(RUNNER, "--cli", CLI, GOLDEN)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        o = self.oracle(GOLDEN, proc.stdout)
        self.assertEqual(o.returncode, 0, o.stdout + o.stderr)
        self.assertIn("6 checked, 0 skipped", o.stdout)
        with tempfile.TemporaryDirectory() as tmp:
            cases = pathlib.Path(tmp, "t2.json")
            gen = run(GEN, "--tier", "T2", "--seed", "11", "--count", "2" if FAST else "6",
                      "--out", cases)
            self.assertEqual(gen.returncode, 0, gen.stderr)
            proc = run(RUNNER, "--cli", CLI, cases)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            o = self.oracle(cases, proc.stdout, "--max-order", "5000")
            self.assertEqual(o.returncode, 0, o.stdout + o.stderr)
            self.assertIn(" 0 failed, 0 missing", o.stdout)
            checked = int(o.stdout.split(" checked")[0].split()[-1])
            self.assertGreater(checked, 0)
            # corrupt every COMPLETE canonical-image witness (swap two images), every minimum's
            # bytes and every stabiliser's group (one nibble): each checked one is a failure
            # naming its case and rule
            for field, objective in ((5, "0001"), (4, "0002"), (6, "0004")):
                bad, hit = [], 0
                for line in proc.stdout.splitlines():
                    f = line.split("\t")
                    if len(f) == 7 and f[2] == "COMPLETE" and f[1] == objective:
                        if field == 5:
                            w = f[5].split(",")
                            if len(w) >= 2 and w[0] != w[1]:
                                w[0], w[1] = w[1], w[0]
                                f[5] = ",".join(w)
                                hit += 1
                        else:
                            f[field] = f[field][:-1] + ("0" if f[field][-1] != "0" else "1")
                            hit += 1
                    bad.append("\t".join(f))
                self.assertGreater(hit, 0)
                o = self.oracle(cases, "\n".join(bad) + "\n", "--max-order", "5000")
                self.assertEqual(o.returncode, 1, o.stdout)
                self.assertIn("FAIL", o.stdout)


if __name__ == "__main__":
    unittest.main()
