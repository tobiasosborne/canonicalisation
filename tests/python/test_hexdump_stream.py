"""Tests for tools/hexdump_stream.py on the six spec 7.4 streams and malformed variants."""
import contextlib
import io
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import hexdump_stream as hs  # noqa: E402

GOLDEN = json.loads((ROOT / "refs" / "vectors" / "golden.json").read_text(encoding="utf-8"))
HDR = "434e0200010001"


def stream(n, q, body, root):
    return bytes.fromhex(HDR + "%08x%08x" % (n, q) + body + "%08x" % root)


class GoldenStreams(unittest.TestCase):
    def test_six_streams_parse_and_print(self):
        for case in GOLDEN["p1_cases"]:
            with self.subTest(case["id"]):
                data = bytes.fromhex(case["expected_stream_hex"])
                parsed = hs.parse_stream(data)
                self.assertEqual(parsed["n"], case["n"])
                self.assertEqual(parsed["root"], parsed["q"] - 1)
                text = hs.format_stream(parsed)
                self.assertIn("root", text)
                self.assertEqual(text.count("offset"), parsed["q"] + 2)

    def test_tuple_dag_structure(self):
        case = next(c for c in GOLDEN["p1_cases"] if c["id"] == "p1-n0-dag-tuple-bb")
        parsed = hs.parse_stream(bytes.fromhex(case["expected_stream_hex"]))
        self.assertEqual([t for _, t, _ in parsed["records"]], [2, 3])

    def test_trailing_bytes_rejected(self):
        for case in GOLDEN["p1_cases"]:
            with self.subTest(case["id"]), self.assertRaises(hs.StreamError):
                hs.parse_stream(bytes.fromhex(case["expected_stream_hex"]) + b"\x00")

    def test_truncation_rejected(self):
        for case in GOLDEN["p1_cases"]:
            data = bytes.fromhex(case["expected_stream_hex"])
            for cut in range(len(data)):
                with self.assertRaises(hs.StreamError):
                    hs.parse_stream(data[:cut])


class Malformed(unittest.TestCase):
    def bad(self, data):
        with self.assertRaises(hs.StreamError):
            hs.parse_stream(data)

    def test_cases(self):
        self.bad(stream(2, 1, "0100000002", 0))                    # atom out of domain
        self.bad(stream(2, 1, "ff", 0))                            # unknown tag
        self.bad(stream(2, 2, "0100000000" "03000000010000000" "1", 1))  # child ref 1 not < index 1
        self.bad(stream(2, 2, "0100000000" "0100000001", 1))       # record 0 unreachable
        self.bad(stream(2, 2, "0100000000" "0100000001", 0))      # root not last
        self.bad(stream(2, 3, "0100000000" "0100000001" "04000000020000000100000000", 2))  # set order
        self.bad(stream(2, 2, "0100000000" "05" "00000001" "00000000" "00000001" "00", 1))  # Nat leading zero
        self.bad(bytes.fromhex("434e03000100010000000000000001"))  # bad magic/version


class ExtendedTags(unittest.TestCase):
    """Tags 06, 07, 08, 0a are parsed (Perm, Group, relations); tags 01-05, 09 are strict."""

    def test_perm_group_coset(self):
        perm = "00000002" "00000000" "00000001" "00000001" "00000000"  # Perm([1,0])
        parsed = hs.parse_stream(stream(2, 1, "06" + perm, 0))
        self.assertEqual(parsed["records"][0][1], 6)
        sym2 = "01" "00000001" "00000002" "00000000" "00000001"
        hs.parse_stream(stream(2, 1, "07" + sym2, 0))
        c3 = "00" "00000001" "00000003" "00000000" "00000001" "00000001" "00000002" "00000002" "00000000"
        hs.parse_stream(stream(3, 1, "07" + c3, 0))
        hs.parse_stream(stream(2, 1, "08" + "01" "00000000" + perm, 0))

    def test_perm_rejections(self):
        with self.assertRaises(hs.StreamError):  # fixed pair
            hs.parse_stream(stream(2, 1, "06" "00000001" "00000000" "00000000", 0))
        with self.assertRaises(hs.StreamError):  # not a bijection
            hs.parse_stream(stream(2, 1, "06" "00000002" "00000000" "00000001" "00000001" "00000001", 0))

    def test_relations_and_multiset(self):
        # one relation: name B("r"), arity 1, one tuple (atom 1) with Nat(1)
        rel = "0a" "00000001" "00000001" "72" "00000001" "00000001" "00000001" "00000001" "01"
        self.assertEqual(hs.parse_stream(stream(2, 1, rel, 0))["records"][0][1], 10)
        # atom 0 then multiset {record0 x Nat(2)}
        ms = "0100000000" "05" "00000001" "00000000" "00000001" "02"
        self.assertEqual(hs.parse_stream(stream(2, 2, ms, 1))["records"][1][1], 5)
        with self.assertRaises(hs.StreamError):  # Nat(0) multiplicity
            hs.parse_stream(stream(2, 2, "0100000000" "05" "00000001" "00000000" "00000000", 1))


class Cli(unittest.TestCase):
    def test_main_ok_and_bad(self):
        case = GOLDEN["p1_cases"][1]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(hs.main(["x", case["expected_stream_hex"]]), 0)
            self.assertEqual(hs.main(["x", case["expected_stream_hex"] + "00"]), 1)


if __name__ == "__main__":
    unittest.main()
