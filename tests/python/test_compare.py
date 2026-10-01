"""Tests of refs/compare/compare.py with the seven-field FORMAT (slice S4, docs/slices/S4.md 3.6).

Records come from tools/canon-cli (CANON_CLI, else build/make/canon-cli) under both group
backends: they must AGREE; a changed group_hex must be reported as DISAGREE on that field; a
six-field line is a format error (exit 2); --plant must be detected; --help must work.  Without
a built CLI the CLI-based tests skip, unless CANON_REQUIRE_CLI=1.  Standard library only.
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPARE = ROOT / "refs" / "compare" / "compare.py"
CLI = pathlib.Path(os.environ.get("CANON_CLI") or (ROOT / "build" / "make" / "canon-cli"))
REQUIRE_CLI = os.environ.get("CANON_REQUIRE_CLI") == "1"

# One case per subcommand; the transporters have a hit and an empty case.
CASES = [
    ["p1-subset", "--n", "3", "--gens", "1,0,2;1,2,0", "--atoms", "0", "--id", "p1"],
    ["p1-graph", "--n", "2", "--gens", "1,0", "--arcs", "0,1,,1", "--id", "p1g"],
    ["min", "--n", "2", "--gens", "1,0", "--atoms", "0", "--id", "min"],
    ["min", "--n", "3", "--gens", "1,0,2;1,2,0", "--order", "simple-upper",
     "--arcs", "0,1,,1;1,0,,1", "--id", "minsu"],
    ["transporter", "--n", "2", "--gens", "1,0", "--atoms", "0", "--target-atoms", "1",
     "--id", "tr"],
    ["transporter", "--n", "2", "--atoms", "0", "--target-atoms", "1", "--id", "tr-empty"],
    ["stabiliser", "--n", "3", "--gens", "1,2,0", "--id", "stab"],
    ["transporter-coset", "--n", "4", "--gens", "1,0,2,3;1,2,3,0", "--atoms", "0,1",
     "--target-atoms", "2,3", "--id", "coset"],
]


def compare(*args):
    return subprocess.run([sys.executable, str(COMPARE)] + [str(a) for a in args],
                          capture_output=True, text=True, check=False)


class CompareScript(unittest.TestCase):
    def test_help(self):
        proc = compare("--help")
        self.assertEqual(proc.returncode, 0)
        self.assertIn("group_hex", proc.stdout)

    def records(self, backend):
        if not CLI.is_file():
            message = f"canon-cli not found at {CLI}"
            if REQUIRE_CLI:
                self.fail(message)
            self.skipTest(message)
        lines = []
        for case in CASES:
            proc = subprocess.run([str(CLI)] + case + ["--backend", backend],
                                  capture_output=True, text=True, check=False)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(len(proc.stdout.rstrip("\n").split("\t")), 7, proc.stdout)
            lines.append(proc.stdout)
        return "# canon-cli records, backend " + backend + "\n" + "".join(lines)

    def test_backends_agree_and_disagreements_are_found(self):
        chain, explicit = self.records("chain"), self.records("explicit")
        with tempfile.TemporaryDirectory() as tmp:
            a, b = pathlib.Path(tmp, "chain.tsv"), pathlib.Path(tmp, "explicit.tsv")
            a.write_text(chain, encoding="utf-8")
            b.write_text(explicit, encoding="utf-8")
            proc = compare(a, b)
            self.assertEqual(proc.returncode, 0, proc.stdout)
            self.assertIn(f"{len(CASES)} agree, 0 disagree/missing", proc.stdout)
            # a changed group_hex (here the stabiliser's Group(C3)) is a disagreement
            changed = []
            for line in explicit.splitlines():
                fields = line.split("\t")
                if fields[0] == "stab":
                    fields[6] = fields[6][:-1] + ("0" if fields[6][-1] != "0" else "1")
                changed.append("\t".join(fields))
            b.write_text("\n".join(changed) + "\n", encoding="utf-8")
            proc = compare(a, b)
            self.assertEqual(proc.returncode, 1)
            self.assertIn("DISAGREE stab: fields differ: group_hex", proc.stdout)
            # the old six-field format is a format error
            b.write_text("x\t0001\tCOMPLETE\t\t\t-\n", encoding="utf-8")
            self.assertEqual(compare(a, b).returncode, 2)
            # planted corruption is detected for every seed tried
            b.write_text(explicit, encoding="utf-8")
            for seed in range(8):
                proc = compare("--plant", "--seed", seed, a, b)
                self.assertEqual(proc.returncode, 1, proc.stdout)
                self.assertIn("PLANT DETECTED", proc.stdout)


if __name__ == "__main__":
    unittest.main()
