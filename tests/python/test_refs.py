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


    def test_manifest_reproduced(self):
        """refs/vectors/generated/MANIFEST.json retains the corpus (spec 20 "seeds must be
        retained"): the generator is the recorded one and the T1 and T3 files regenerate to the
        recorded hashes (T2, about 4 s, is checked by make corpus).  Independent review of
        M0a: the hashes recorded at the M0a landing did not match the generator's files."""
        import hashlib
        manifest = json.loads((ROOT / "refs/vectors/generated/MANIFEST.json")
                              .read_text(encoding="utf-8"))
        self.assertEqual(manifest["generator_sha256"],
                         hashlib.sha256(GEN.read_bytes()).hexdigest())
        for entry in manifest["files"]:
            if entry["tier"] == "T2":
                continue
            proc = run(GEN, *entry["arguments"].split())
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = proc.stdout.encode("utf-8")
            self.assertEqual((len(data), hashlib.sha256(data).hexdigest()),
                             (entry["bytes"], entry["sha256"]), entry["file"])
            self.assertEqual(json.loads(data)["count"], entry["cases"])

    def test_invariance_cases_appended(self):
        """The transported copies and representative changes follow the tier's cases: on T2
        every object has its CANONICAL_IMAGE twice (x and x^h, same group, same object kind)
        and two labelings whose rho differ by an element of G (spec 3, 3.1)."""
        sys.path.insert(0, str(ROOT / "checks"))
        from review_checks import closure, inverse, mul
        proc = run(GEN, "--tier", "T2", "--seed", "5", "--count", "4")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        cases = json.loads(proc.stdout)["cases"]
        images = [c for c in cases if c["objective"] == "CANONICAL_IMAGE"]
        labelings = [c for c in cases if c["objective"] == "CANONICAL_LABELING_COSET"]
        half = len(images) // 2
        self.assertEqual(len(images), 2 * half)
        self.assertEqual(len(labelings), 2 * half)
        self.assertGreater(cases.index(images[half]), cases.index(labelings[half - 1]))
        for a, b in zip(images[:half], images[half:]):
            self.assertEqual((a["n"], a["group_generators"], a["object"]["kind"]),
                             (b["n"], b["group_generators"], b["object"]["kind"]))
        for a, b in zip(labelings[:half], labelings[half:]):
            self.assertEqual((a["object"], a["group_generators"]), (b["object"], b["group_generators"]))
            g = closure(tuple(tuple(x) for x in a["group_generators"]), a["n"])
            k = mul(tuple(b["labeling"]["rho"]), inverse(tuple(a["labeling"]["rho"])))
            self.assertIn(k, g)  # rho_b = k rho_a with k in G


class OracleRules(unittest.TestCase):
    """Rules of the oracle on hand-made records (independent review of M0a); no CLI needed."""

    def check(self, cases, lines):
        with tempfile.TemporaryDirectory() as tmp:
            cpath, rpath = pathlib.Path(tmp, "c.json"), pathlib.Path(tmp, "r.tsv")
            cpath.write_text(json.dumps({"cases": cases}), encoding="utf-8")
            rpath.write_text("".join("\t".join(f) + "\n" for f in lines), encoding="utf-8")
            return run(ORACLE, cpath, rpath)

    @staticmethod
    def case(cid, objective, tag, profile, atoms, **extra):
        c = {"id": cid, "n": 2, "group_generators": [[1, 0]],
             "object": {"kind": "subset", "atoms": atoms}, "objective": objective,
             "objective_tag": tag, "profile": profile, "encoding": 2}
        c.update(extra)
        return c

    def test_invariance_across_cases(self):
        """Each record alone is valid (c in the orbit, x^w = c, w the least such g); together
        they break C_G(x^h) = C_G(x) (spec 3) or the representative change of spec 3.1."""
        sys.path.insert(0, str(ROOT / "refs" / "oracle"))
        import oracle
        s0 = oracle.encode(("subset", frozenset([0])), 2).hex()
        s1 = oracle.encode(("subset", frozenset([1])), 2).hex()
        trivial = oracle.group_bytes(frozenset([(0, 1)]), 2).hex()
        cases = [self.case("x0", "CANONICAL_IMAGE", 1, 1, [0]),
                 self.case("x1", "CANONICAL_IMAGE", 1, 1, [1])]
        ok = [["x0", "0001", "COMPLETE", "10", s0, "0,1", "-"],
              ["x1", "0001", "COMPLETE", "10", s0, "1,0", "-"]]
        self.assertEqual(self.check(cases, ok).returncode, 0)
        bad = [ok[0], ["x1", "0001", "COMPLETE", "10", s1, "0,1", "-"]]
        o = self.check(cases, bad)
        self.assertEqual(o.returncode, 1, o.stdout)
        self.assertIn("FAIL     x1: C_G(x^h) differs", o.stdout)
        cases = [self.case("l0", "CANONICAL_LABELING_COSET", 5, 1, [0], labeling={"rho": [0, 1]}),
                 self.case("l1", "CANONICAL_LABELING_COSET", 5, 1, [0], labeling={"rho": [1, 0]})]
        bad = [["l0", "0005", "COMPLETE", "10", s0, "0,1", trivial + oracle.perm_bytes((0, 1)).hex()],
               ["l1", "0005", "COMPLETE", "10", s1, "1,0", trivial + oracle.perm_bytes((1, 0)).hex()]]
        o = self.check(cases, bad)
        self.assertEqual(o.returncode, 1, o.stdout)
        self.assertIn("FAIL     l1: representative change", o.stdout)

    def test_deterministic_zero_witness(self):
        """Spec v2.2 3 (maintainer decision D1, 19 October 2026): in the deterministic mode a
        certified zero reports the least odd element of A.  n = 3, G = Sym(3) generated by
        [2,1,0] and [1,0,2], chi the parity, x = {}: A = G, whose odd elements are [0,2,1],
        [1,0,2] and [2,1,0]; the first odd generator [2,1,0] is accepted only in "any" mode."""
        def zero(cid, mode):
            return {"id": cid, "n": 3, "group_generators": [[2, 1, 0], [1, 0, 2]],
                    "character": [-1, -1], "object": {"kind": "subset", "atoms": []},
                    "objective": "SIGNED_CANONICAL_IMAGE", "objective_tag": 7, "profile": 1,
                    "encoding": 2, "witness_mode": mode}
        cases = [zero("least", "deterministic"), zero("first", "deterministic"),
                 zero("any", "any")]
        lines = [["least", "0007", "COMPLETE", "", "00", "0,2,1;sign=0", "-"],
                 ["first", "0007", "COMPLETE", "", "00", "2,1,0;sign=0", "-"],
                 ["any", "0007", "COMPLETE", "", "00", "2,1,0;sign=0", "-"]]
        o = self.check(cases, lines)
        self.assertEqual(o.returncode, 1, o.stdout)
        self.assertIn("FAIL     first: deterministic zero witness is not the least odd element",
                      o.stdout)
        self.assertNotIn("FAIL     least", o.stdout)
        self.assertNotIn("FAIL     any", o.stdout)
        self.assertIn("1 failed", o.stdout)

    def test_statuses(self):
        """SIMPLE-UPPER-1 outside the spec 4.4 class must be UNSUPPORTED_ACTION (the oracle
        demanded COMPLETE first, so no record passed); CAPACITY_LIMIT is refused when the
        quota covers every reference traversal (spec 11.1) and skipped otherwise."""
        cases = [self.case("su", "LEX_MIN_IMAGE", 2, 0, [0], order="SIMPLE-UPPER-1"),
                 self.case("cap", "LEX_MIN_IMAGE", 2, 0, [0], order="CDAG-BYTE-1"),
                 self.case("cap1", "LEX_MIN_IMAGE", 2, 0, [0], order="CDAG-BYTE-1",
                           capacity={"max_search_nodes": 1})]
        good = [["su", "0002", "UNSUPPORTED_ACTION", "", "", "-", "-"],
                ["cap1", "0002", "CAPACITY_LIMIT", "", "", "-", "-"]]
        o = self.check(cases[:1] + cases[2:], good)
        self.assertEqual(o.returncode, 0, o.stdout)
        self.assertIn("1 checked, 1 skipped", o.stdout)
        bad = [["su", "0002", "COMPLETE", "", "", "0,1", "-"],
               ["cap", "0002", "CAPACITY_LIMIT", "", "", "-", "-"]]
        o = self.check(cases[:2], bad)
        self.assertEqual(o.returncode, 1, o.stdout)
        self.assertIn("FAIL     su: SIMPLE-UPPER-1 on an object outside", o.stdout)
        self.assertIn("FAIL     cap: CAPACITY_LIMIT, but the quota", o.stdout)


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

    def test_coset_leaf_and_out_of_class_statuses(self):
        """Independent review of M0a: two coset leaves H r and H r' with r' in H r are one
        value (spec 4.2; tag 08 carries the least element), so the set holding them has one
        child (the oracle kept both and rejected ref-a's correct stream); SIMPLE-UPPER-1 on a
        subset is UNSUPPORTED_ACTION in ref-a and accepted by the oracle, and on a nested object
        whose root is a simple graph it is that graph's minimum (the oracle refused both); a
        quota too small for the reference traversal gives CAPACITY_LIMIT, which the oracle
        skips."""
        cases = {"cases": [
            {"id": "cs", "n": 3, "group_generators": [[1, 2, 0]],
             "object": {"kind": "dag", "root": 2, "records": [
                 {"tag": 8, "group_generators": [[1, 0, 2]], "r": [0, 1, 2]},
                 {"tag": 8, "group_generators": [[1, 0, 2]], "r": [1, 0, 2]},
                 {"tag": 4, "children": [0, 1]}]},
             "objective": "CANONICAL_IMAGE", "objective_tag": 1, "profile": 1, "encoding": 2},
            {"id": "su", "n": 2, "group_generators": [[1, 0]],
             "object": {"kind": "subset", "atoms": [0]}, "order": "SIMPLE-UPPER-1",
             "objective": "LEX_MIN_IMAGE", "objective_tag": 2, "profile": 0, "encoding": 2},
            {"id": "q1", "n": 3, "group_generators": [[1, 2, 0], [1, 0, 2]],
             "object": {"kind": "subset", "atoms": [0]}, "capacity": {"max_search_nodes": 1},
             "objective": "STABILISER", "objective_tag": 4, "profile": 0, "encoding": 2},
            {"id": "sd", "n": 3, "group_generators": [[1, 2, 0], [1, 0, 2]],
             "object": {"kind": "dag", "root": 0, "records": [
                 {"tag": 9, "vertex_colours_hex": ["", "", ""], "arcs": [
                     {"source": 0, "target": 1, "label_hex": "", "multiplicity": 1},
                     {"source": 1, "target": 0, "label_hex": "", "multiplicity": 1}]}]},
             "order": "SIMPLE-UPPER-1",
             "objective": "LEX_MIN_IMAGE", "objective_tag": 2, "profile": 0, "encoding": 2}]}
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp, "c.json")
            path.write_text(json.dumps(cases), encoding="utf-8")
            proc = run(RUNNER, "--cli", CLI, path)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            recs = records(proc.stdout)
            self.assertEqual([recs[c][2] for c in ("cs", "su", "q1", "sd")],
                             ["COMPLETE", "UNSUPPORTED_ACTION", "CAPACITY_LIMIT", "COMPLETE"])
            o = self.oracle(path, proc.stdout)
        self.assertEqual(o.returncode, 0, o.stdout + o.stderr)
        self.assertIn("3 checked, 1 skipped", o.stdout)

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
