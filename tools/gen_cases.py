#!/usr/bin/env python3
"""Seeded generator of M0 case files (detailed plan WP0.6; refs/CASES.md; docs/slices/M0a.md 3).

Usage: gen_cases.py --tier T1|T2|T3 [--seed S] [--count N] [--out FILE]

  T1  exhaustive: every subgroup of Sym(n), n <= 4 (greedy generators), with every subset, every
      3-tuple of atoms and, for n <= 2, every digraph with multiplicities <= 2 under both
      colourings (all empty, or vertex v coloured by v mod 2); every objective on each object
      (both orders where the graph is simple undirected; a transporter target in the orbit and
      one that may be outside; one labeling; every character of the group and, where one
      exists, one inconsistent sign vector).  --count and --seed only choose the random
      transporter targets and labelings.
  T2  random: --count groups (default 200; WP0.6 target 2000 cases per objective) with 1-3
      random generators on n <= 8 points and |G| <= 2 * 10^5, each with one random subset,
      tuple, labelled coloured digraph and nested DAG (tags 01-05, depth <= 4, sharing); every
      objective on each object; characters: the trivial one, the sign of the permutation (a
      character of every permutation group), one random consistent vector where found and one
      inconsistent vector where found (checked by the lifted closure of spec 8.4).
  T3  structured, n <= 48: cyclic, dihedral, direct products, wreath products Sym(a) wr Sym(b),
      grids C_a x C_b and intransitive sums, --count groups (default 50; WP0.6 target 500 cases
      per objective), objects as T2 plus identical-factor tensor skeletons as labelled digraphs
      (slots of f identical factors with s slots each, chained by arcs labelled 66 ("f") and
      contracted in pairs by arcs labelled 63 ("c"), under Sym(s) wr Sym(f)); characters: the
      trivial one and the sign (no closure beyond oracle range); capacity 10^6 nodes.  WP0.6's
      COMPLETE filter is applied by the runner's records, not here.

Every case requests the deterministic witness (refs/BRIEF.md answer 3) and names work policy 1.
The output is {"schema": "cases-1", "tier": ..., "seed": ..., "generator": ..., "cases": [...]};
the same arguments always give the same file.  Inputs only: no expected answer is written.
Standard library plus checks/review_checks.py (closure, mul, inverse, subgroups).
"""
import argparse
import hashlib
import json
import pathlib
import random
import sys
from itertools import permutations, product

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "checks"))
import review_checks as rc  # noqa: E402

ORDER_CAP = 200_000
OBJECTIVES = {"CANONICAL_IMAGE": (1, 1), "LEX_MIN_IMAGE": (2, 0), "TRANSPORTER_ONE": (3, 0),
              "STABILISER": (4, 0), "CANONICAL_LABELING_COSET": (5, 1),
              "TRANSPORTER_COSET": (6, 0), "SIGNED_CANONICAL_IMAGE": (7, 1)}


# ---- groups ----

def identity(n):
    return tuple(range(n))


def closure_capped(gens, n, cap):
    """The group generated, or None when it has more than `cap` elements."""
    found, todo = {identity(n)}, [identity(n)]
    while todo:
        a = todo.pop()
        for b in gens:
            c = rc.mul(a, b)
            if c not in found:
                found.add(c)
                if len(found) > cap:
                    return None
                todo.append(c)
    return frozenset(found)


def random_word(gens, n, rng, length=6):
    """A random element of <gens> as a product of a random word (no closure)."""
    g = identity(n)
    for _ in range(rng.randrange(length + 1)):
        g = rc.mul(g, rng.choice(gens)) if gens else g
    return g


def sign_of(p):
    """The sign of a permutation (parity of its cycle structure)."""
    seen, sign = [False] * len(p), 1
    for i in range(len(p)):
        if not seen[i]:
            j, length = i, 0
            while not seen[j]:
                seen[j] = True
                j = p[j]
                length += 1
            if length % 2 == 0:
                sign = -sign
    return sign


def lifted_consistent(gens, signs, n, cap):
    """spec 8.4: the sign vector is a character iff the lift to Omega + {+,-} has a trivial
    kernel over Omega.  Returns True, False, or None when the lift exceeds `cap` elements."""
    lifted = []
    for g, s in zip(gens, signs):
        lifted.append(tuple(g) + ((n + 1, n) if s < 0 else (n, n + 1)))
    found, todo = {identity(n + 2)}, [identity(n + 2)]
    while todo:
        a = todo.pop()
        for b in lifted:
            c = rc.mul(a, b)
            if c not in found:
                if c[:n] == identity(n) and c[n] != n:
                    return False
                found.add(c)
                if len(found) > cap:
                    return None
                todo.append(c)
    return True


def greedy_generators(group, n):
    known, chosen = rc.closure((), n), []
    while known != group:
        chosen.append(min(group - known))
        known = rc.closure(chosen, n)
    return chosen


# ---- objects and their action (input construction only) ----

def act(obj, g):
    kind = obj["kind"]
    if kind == "subset":
        return {"kind": "subset", "atoms": sorted(g[a] for a in obj["atoms"])}
    if kind == "tuple":
        return {"kind": "tuple", "points": [g[a] for a in obj["points"]]}
    if kind == "graph":
        n = len(obj["vertex_colours_hex"])
        colours = [""] * n
        for v in range(n):
            colours[g[v]] = obj["vertex_colours_hex"][v]
        arcs = [{"source": g[a["source"]], "target": g[a["target"]], "label_hex": a["label_hex"],
                 "multiplicity": a["multiplicity"]} for a in obj["arcs"]]
        return {"kind": "graph", "vertex_colours_hex": colours, "arcs": arcs}
    records = []
    for rec in obj["records"]:
        rec = dict(rec)
        if rec["tag"] == 1:
            rec["atom"] = g[rec["atom"]]
        records.append(rec)
    return {"kind": "dag", "records": records, "root": obj["root"]}


def random_subset(n, rng):
    return {"kind": "subset", "atoms": sorted(a for a in range(n) if rng.random() < 0.5)}


def random_tuple(n, rng):
    return {"kind": "tuple", "points": [rng.randrange(n) for _ in range(rng.randrange(n + 2))]}


def random_graph(n, rng, labels=("", "61", "62"), colours=("", "", "63")):
    arcs = []
    for _ in range(rng.randrange(2 * n + 1)):
        arcs.append({"source": rng.randrange(n), "target": rng.randrange(n),
                     "label_hex": rng.choice(labels), "multiplicity": rng.choice((1, 1, 2, 3))})
    return {"kind": "graph", "vertex_colours_hex": [rng.choice(colours) for _ in range(n)],
            "arcs": arcs}


def random_dag(n, rng, depth=4):
    """A nested object of tags 01-05 with sharing and duplicate storage; records in an order
    that may list a child after its parent (refs/CASES.md Part B allows it)."""
    records = []
    leaves = []
    for _ in range(rng.randrange(1, 4)):
        records.append({"tag": 1, "atom": rng.randrange(n)})
        leaves.append(len(records) - 1)
    for _ in range(rng.randrange(0, 3)):
        records.append({"tag": 2, "literal_hex": rng.choice(("", "00", "61", "6162"))})
        leaves.append(len(records) - 1)
    layer = list(leaves)
    for _ in range(rng.randrange(1, depth + 1)):
        new = []
        for _ in range(rng.randrange(1, 3)):
            kind = rng.choice((3, 4, 5))
            picks = [rng.choice(layer + leaves) for _ in range(rng.randrange(0, 4))]
            if kind == 5:
                rec = {"tag": 5, "pairs": [{"child": c, "multiplicity": rng.choice((1, 1, 2))}
                                           for c in picks]}
            else:
                rec = {"tag": kind, "children": picks}
            records.append(rec)
            new.append(len(records) - 1)
        layer = new
    root = rng.choice(layer)
    # shuffle the storage order, keeping a map for the references
    order = list(range(len(records)))
    rng.shuffle(order)
    position = {old: new for new, old in enumerate(order)}
    shuffled = []
    for old in order:
        rec = dict(records[old])
        if rec["tag"] in (3, 4):
            rec["children"] = [position[c] for c in rec["children"]]
        elif rec["tag"] == 5:
            rec["pairs"] = [{"child": position[p["child"]], "multiplicity": p["multiplicity"]}
                            for p in rec["pairs"]]
        shuffled.append(rec)
    return {"kind": "dag", "records": shuffled, "root": position[root]}


def is_simple_undirected(obj):
    """spec 4.4 class: empty colours and labels, no loops, exactly one unit arc each way."""
    if obj["kind"] != "graph" or any(c != "" for c in obj["vertex_colours_hex"]):
        return False
    arcs = {}
    for a in obj["arcs"]:
        if a["label_hex"] != "" or a["source"] == a["target"]:
            return False
        key = (a["source"], a["target"])
        arcs[key] = arcs.get(key, 0) + a["multiplicity"]
    return all(m == 1 and (t, s) in arcs for (s, t), m in arcs.items())


# ---- cases ----

class Builder:
    def __init__(self, tier, seed, rng, quota):
        self.tier, self.seed, self.rng, self.quota = tier, seed, rng, quota
        self.cases = []
        self.counter = 0

    def case(self, n, gens, obj, objective, **extra):
        tag, profile = OBJECTIVES[objective]
        self.counter += 1
        c = {"id": "%s-s%d-%06d-%s" % (self.tier.lower(), self.seed, self.counter, objective.lower()),
             "source": "tools/gen_cases.py tier %s seed %d" % (self.tier, self.seed),
             "n": n, "group_generators": [list(g) for g in gens], "object": obj,
             "objective": objective, "objective_tag": tag, "profile": profile, "encoding": 2,
             "witness_mode": "deterministic",
             "capacity": {"max_search_nodes": self.quota, "work_policy": 1}}
        c.update(extra)
        self.cases.append(c)

    def all_objectives(self, n, gens, obj, characters, other_target=None):
        """Every objective on (G, x): both orders where the graph is simple undirected, two
        transporter targets (one in the orbit by a random word, one other object or the same
        object when none is given), one random labeling, one signed case per character."""
        rng = self.rng
        self.case(n, gens, obj, "CANONICAL_IMAGE")
        self.case(n, gens, obj, "LEX_MIN_IMAGE", order="CDAG-BYTE-1")
        if is_simple_undirected(obj):
            self.case(n, gens, obj, "LEX_MIN_IMAGE", order="SIMPLE-UPPER-1")
        hit = act(obj, random_word(gens, n, rng))
        for target in (hit, other_target) if other_target is not None else (hit,):
            self.case(n, gens, obj, "TRANSPORTER_ONE", target=target)
            self.case(n, gens, obj, "TRANSPORTER_COSET", target=target)
        self.case(n, gens, obj, "STABILISER")
        rho = list(range(n))
        rng.shuffle(rho)
        self.case(n, gens, obj, "CANONICAL_LABELING_COSET", labeling={"rho": rho})
        for chi in characters:
            self.case(n, gens, obj, "SIGNED_CANONICAL_IMAGE", character=list(chi))


def characters_t1(group, gens, n):
    """Every consistent sign vector on gens (by the lift), and one inconsistent one if any."""
    good, bad = [], None
    for bits in product((1, -1), repeat=len(gens)):
        ok = lifted_consistent(gens, bits, n, 4 * len(group) + 4)
        if ok:
            good.append(bits)
        elif bad is None:
            bad = bits
    return good + ([bad] if bad is not None else [])


def tier1(b):
    rng = b.rng
    for n in range(5):
        symmetric = tuple(permutations(range(n)))
        for group in rc.subgroups(symmetric, n):
            gens = greedy_generators(group, n)
            chars = characters_t1(group, gens, n)
            objects = []
            for mask in range(1 << n):
                objects.append({"kind": "subset", "atoms": [a for a in range(n) if mask >> a & 1]})
            for points in product(range(n), repeat=3):
                objects.append({"kind": "tuple", "points": list(points)})
            if n <= 2:
                for mult, distinct in product(product(range(3), repeat=n * n), (False, True)):
                    arcs = [{"source": a, "target": c, "label_hex": "", "multiplicity": mult[a * n + c]}
                            for a in range(n) for c in range(n) if mult[a * n + c]]
                    colours = ["%02x" % (v % 2) if distinct else "" for v in range(n)]
                    objects.append({"kind": "graph", "vertex_colours_hex": colours, "arcs": arcs})
            for i, obj in enumerate(objects):
                same_kind = [o for o in objects if o["kind"] == obj["kind"]]
                other = rng.choice(same_kind)
                b.all_objectives(n, gens, obj, chars, other_target=other)


def random_group(rng):
    while True:
        n = rng.randrange(1, 9)
        gens = []
        for _ in range(rng.randrange(1, 4)):
            p = list(range(n))
            rng.shuffle(p)
            gens.append(tuple(p))
        group = closure_capped(gens, n, ORDER_CAP)
        if group is not None:
            return n, gens, group


def characters_random(gens, n, group, rng):
    chars = [tuple(1 for _ in gens), tuple(sign_of(g) for g in gens)]
    if chars[1] == chars[0]:
        chars.pop()
    cap = 4 * len(group) + 4
    found_good = found_bad = False
    for _ in range(8):
        bits = tuple(rng.choice((1, -1)) for _ in gens)
        if bits in chars:
            continue
        ok = lifted_consistent(gens, bits, n, cap)
        if ok and not found_good:
            chars.append(bits)
            found_good = True
        elif ok is False and not found_bad:
            chars.append(bits)
            found_bad = True
        if found_good and found_bad:
            break
    return chars


def tier2(b, count):
    rng = b.rng
    for _ in range(count):
        n, gens, group = random_group(rng)
        chars = characters_random(gens, n, group, rng)
        objects = [random_subset(n, rng), random_tuple(n, rng), random_graph(n, rng),
                   random_dag(n, rng)]
        for obj in objects:
            other = {"subset": random_subset, "tuple": random_tuple, "graph": random_graph,
                     "dag": random_dag}[obj["kind"]](n, rng)
            b.all_objectives(n, gens, obj, chars, other_target=other)


# ---- T3 families ----

def cycle(n, points):
    p = list(range(n))
    for i, a in enumerate(points):
        p[a] = points[(i + 1) % len(points)]
    return tuple(p)


def transposition(n, a, c):
    p = list(range(n))
    p[a], p[c] = c, a
    return tuple(p)


def family_cyclic(rng):
    n = rng.randrange(3, 49)
    return n, [cycle(n, range(n))]


def family_dihedral(rng):
    n = rng.randrange(3, 49)
    reflect = tuple((n - v) % n for v in range(n))
    return n, [cycle(n, range(n)), reflect]


def family_wreath(rng):
    a = rng.randrange(2, 7)
    bb = rng.randrange(2, max(3, 48 // a + 1))
    n = a * bb
    gens = [transposition(n, 0, 1)]
    if a > 2:
        gens.append(cycle(n, range(a)))
    # the block shift: slot i of block j -> slot i of block j+1
    shift = [0] * n
    for j in range(bb):
        for i in range(a):
            shift[j * a + i] = ((j + 1) % bb) * a + i
    gens.append(tuple(shift))
    if bb > 2:
        swap = list(range(n))
        for i in range(a):
            swap[i], swap[a + i] = a + i, i
        gens.append(tuple(swap))
    return n, gens


def family_grid(rng):
    a = rng.randrange(2, 8)
    bb = rng.randrange(2, max(3, 48 // a + 1))
    n = a * bb
    row = tuple(((v // bb + 1) % a) * bb + v % bb for v in range(n))
    col = tuple((v // bb) * bb + (v % bb + 1) % bb for v in range(n))
    return n, [row, col]


def family_product(rng, intransitive=False):
    (n1, g1), (n2, g2) = pick_small(rng), pick_small(rng)
    n = n1 + n2
    if n > 48:
        n2 = max(2, 48 - n1)
        n2, g2 = n2, [cycle(n2, range(n2))]
        n = n1 + n2
    gens = [tuple(g) + tuple(range(n1, n)) for g in g1]
    gens += [tuple(range(n1)) + tuple(v + n1 for v in g) for g in g2]
    return n, gens


def pick_small(rng):
    return rng.choice((family_cyclic, family_dihedral))(random.Random(rng.random()))


def tensor_skeleton(rng):
    """f identical factors with s slots each: chain arcs within a factor labelled "f", random
    slot pairs contracted by arcs labelled "c" (both directions); group Sym(s) wr Sym(f)."""
    f, s = rng.randrange(2, 5), rng.randrange(2, 5)
    n = f * s
    arcs = []
    for j in range(f):
        for i in range(s - 1):
            arcs.append({"source": j * s + i, "target": j * s + i + 1, "label_hex": "66",
                         "multiplicity": 1})
    slots = list(range(n))
    rng.shuffle(slots)
    for k in range(rng.randrange(0, n // 2 + 1)):
        u, v = slots[2 * k], slots[2 * k + 1]
        arcs.append({"source": u, "target": v, "label_hex": "63", "multiplicity": 1})
        arcs.append({"source": v, "target": u, "label_hex": "63", "multiplicity": 1})
    obj = {"kind": "graph", "vertex_colours_hex": [""] * n, "arcs": arcs}
    gens = [transposition(n, 0, 1)]
    if s > 2:
        gens.append(cycle(n, range(s)))
    shift = tuple(((v // s + 1) % f) * s + v % s for v in range(n))
    gens.append(shift)
    return n, gens, obj


def tier3(b, count):
    rng = b.rng
    families = (family_cyclic, family_dihedral, family_wreath, family_grid,
                lambda r: family_product(r), lambda r: family_product(r, True))
    for i in range(count):
        if i % 6 == 5:
            n, gens, obj = tensor_skeleton(rng)
            chars = [tuple(1 for _ in gens)]
            signs = tuple(sign_of(g) for g in gens)
            if signs != chars[0]:
                chars.append(signs)
            b.all_objectives(n, gens, obj, chars, other_target=random_graph(n, rng))
            continue
        n, gens = families[i % 6](rng)
        chars = [tuple(1 for _ in gens)]
        signs = tuple(sign_of(g) for g in gens)
        if signs != chars[0]:
            chars.append(signs)
        objects = [random_subset(n, rng), random_tuple(n, rng), random_graph(n, rng),
                   random_dag(n, rng)]
        for obj in objects:
            other = {"subset": random_subset, "tuple": random_tuple, "graph": random_graph,
                     "dag": random_dag}[obj["kind"]](n, rng)
            b.all_objectives(n, gens, obj, chars, other_target=other)


def generate(tier, seed, count):
    rng = random.Random(seed)
    quota = 1_000_000 if tier == "T3" else 1 << 20
    b = Builder(tier, seed, rng, quota)
    if tier == "T1":
        tier1(b)
    elif tier == "T2":
        tier2(b, count if count is not None else 200)
    else:
        tier3(b, count if count is not None else 50)
    me = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    return {"schema": "cases-1", "tier": tier, "seed": seed, "count": len(b.cases),
            "generator": "tools/gen_cases.py sha256 " + me,
            "note": "M0 inputs only (refs/CASES.md); every case requests the deterministic "
                    "witness under work policy 1", "cases": b.cases}


def check_manifest(path):
    """refs/vectors/generated/MANIFEST.json: every listed file exists next to it with the recorded
    size and SHA-256, and the generator is the recorded one.  Exit 1 on any difference."""
    manifest = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    folder = pathlib.Path(path).parent
    me = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    bad = 0
    if manifest.get("generator_sha256") != me:
        print("generator differs from the manifest's", file=sys.stderr)
        bad += 1
    for entry in manifest["files"]:
        f = folder / entry["file"]
        if not f.is_file():
            print(f"{f}: missing", file=sys.stderr)
            bad += 1
            continue
        digest = hashlib.sha256(f.read_bytes()).hexdigest()
        if digest != entry["sha256"] or f.stat().st_size != entry["bytes"]:
            print(f"{f}: differs from the manifest", file=sys.stderr)
            bad += 1
        else:
            print(f"{f}: ok ({entry['cases']} cases)")
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tier", choices=("T1", "T2", "T3"))
    ap.add_argument("--seed", type=int, default=20261013)
    ap.add_argument("--count", type=int, default=None, help="groups (T2, T3)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--check-manifest", metavar="MANIFEST", default=None,
                    help="verify the generated files against refs/vectors/generated/MANIFEST.json")
    args = ap.parse_args(argv)
    if args.check_manifest:
        return check_manifest(args.check_manifest)
    if args.tier is None:
        ap.error("--tier is required")
    data = generate(args.tier, args.seed, args.count)
    text = json.dumps(data, separators=(",", ":"), sort_keys=False) + "\n"
    if args.out:
        pathlib.Path(args.out).write_text(text, encoding="utf-8")
        print(f"{args.out}: {data['count']} cases", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
