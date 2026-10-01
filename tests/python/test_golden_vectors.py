"""Verify refs/vectors/golden.json against the finite model in checks/review_checks.py.

The JSON is a hand transcription of docs/specification.md section 7.4 with the
abbreviations H(n), A(a), S(ids), B0, T(ids) expanded.  This test recomputes every
entry with the independent Python model, so a transcription error fails here.
The model is a sanity model, not one of the blind M0 references (spec section 20).
"""
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "checks"))
import review_checks as rc  # noqa: E402  (no side effects on import)

GOLDEN = json.loads((ROOT / "refs" / "vectors" / "golden.json").read_text(encoding="utf-8"))


def group_of(n, gens):
    return rc.closure(tuple(tuple(g) for g in gens), n)


class P1Cases(unittest.TestCase):
    def test_six_cases_present(self):
        self.assertEqual(len(GOLDEN["p1_cases"]), 6)

    def test_p1_cases(self):
        for case in GOLDEN["p1_cases"]:
            with self.subTest(case["id"]):
                n, obj = case["n"], case["object"]
                group = group_of(n, case["group_generators"])
                self.assertEqual(case["source"], "docs/specification.md §7.4")
                if obj["kind"] == "subset":
                    trace, data, witness = rc.p1(n, group, "subset", frozenset(obj["atoms"]))
                elif obj["kind"] == "graph":
                    colours = tuple(bytes.fromhex(c) for c in obj["vertex_colours_hex"])
                    arcs = tuple((a["source"], a["target"], bytes.fromhex(a["label_hex"]),
                                  a["multiplicity"]) for a in obj["arcs"])
                    trace, data, witness = rc.p1(n, group, "graph", (colours, arcs))
                else:
                    # DAG case: the model has no P1 path for DAG roots.  The spec states that the
                    # trace equals the n=0 empty-subset trace; bytes come from the DAG normaliser.
                    self.assertEqual(obj["kind"], "dag")
                    records = []
                    for r in obj["records"]:
                        if r["tag"] == 2:
                            records.append((2, bytes.fromhex(r["literal_hex"])))
                        else:
                            records.append((r["tag"], tuple(r["children"])))
                    trace, _, witness = rc.p1(n, group, "subset", frozenset())
                    data = rc.dag_bytes(n, records, obj["root"])
                self.assertEqual(trace.hex(), case["expected_trace_hex"])
                self.assertEqual(data.hex(), case["expected_stream_hex"])
                self.assertEqual(list(witness), case["expected_witness"])

    def test_dag_duplicate_storage_same_bytes(self):
        # spec 7.4: shared and duplicate b storage give these same bytes
        case = next(c for c in GOLDEN["p1_cases"] if c["object"]["kind"] == "dag")
        dup = rc.dag_bytes(0, [(2, b""), (2, b""), (3, (1, 0))], 2)
        self.assertEqual(dup.hex(), case["expected_stream_hex"])


class GroupPayloads(unittest.TestCase):
    def test_group_and_coset_payloads(self):
        for case in GOLDEN["group_payload_cases"]:
            with self.subTest(case["id"]):
                group = group_of(case["n"], case["generators"])
                data = rc.group_bytes(group)
                if case["kind"] == "LabelingCoset":
                    data += rc.perm_bytes(tuple(case["r"]))
                self.assertEqual(data.hex(), case["payload_hex"])


class Conventions(unittest.TestCase):
    def test_vectors(self):
        c = GOLDEN["convention_vectors"]
        p, q = tuple(c["p"]), tuple(c["q"])
        self.assertEqual(list(rc.mul(p, q)), c["pq"])
        self.assertEqual(list(rc.mul(q, p)), c["qp"])
        self.assertEqual(list(rc.inverse(p)), c["p_inverse"])
        pq = rc.mul(p, q)
        self.assertEqual([pq[v] for v in c["cycle_conjugation"]["cycle"]],
                         c["cycle_conjugation"]["result"])


class OtherHandChecked(unittest.TestCase):
    def test_labeling(self):
        lab = GOLDEN["other_hand_checked"]["labeling"]
        rho = tuple(lab["rho"])
        x = {rho[a] for a in lab["x_atoms_source_a_is_0"]}
        self.assertEqual(sorted(x), lab["canonical_subset"])  # t = id, c = x^rho
        self.assertEqual(list(rc.mul(tuple(lab["mu"]), rho)), lab["mu_inverse_rho"])
        self.assertEqual(list(rc.mul(rc.inverse(tuple(lab["mu"])), rho)), lab["mu_inverse_rho"])

    def test_signed(self):
        for case in GOLDEN["other_hand_checked"]["signed"]:
            with self.subTest(case["id"]):
                n = case["n"]
                group = group_of(n, case["generators"])
                chars = [chi for chi in rc.characters(group)
                         if chi[tuple(case["generators"][0])] == case["character_of_generator"]]
                self.assertEqual(len(chars), 1)
                chi = chars[0]
                x = frozenset(case["atoms"])
                odd_fixer = any(chi[g] == -1 and rc.act_object("subset", x, g) == x for g in group)
                if case["result"] == "zero":
                    self.assertTrue(odd_fixer)
                else:
                    self.assertFalse(odd_fixer)
                    _, data, witness = rc.p1(n, group, "subset", x)
                    c = case["result"]
                    self.assertEqual(data, rc.subset_bytes(n, frozenset(c["canonical_subset"])))
                    self.assertEqual(chi[tuple(witness)], c["sign"])

    def test_min_order(self):
        m = GOLDEN["other_hand_checked"]["min_order"]
        group = group_of(m["n"], m["generators"])
        x = frozenset(m["atoms"])
        orbit = {rc.subset_bytes(m["n"], rc.act_object("subset", x, g)): g for g in group}
        best = min(orbit)
        self.assertEqual(best, rc.subset_bytes(m["n"], frozenset(m["min_subset"])))
        self.assertEqual(list(orbit[best]), m["witness"])
        _, data, _ = rc.p1(m["n"], group, "subset", x)
        self.assertEqual(data, rc.subset_bytes(m["n"], frozenset(m["p1_subset"])))


if __name__ == "__main__":
    unittest.main()
