"""Finite sanity checks for the referee report; no production solver is tested.

Run with Python 3. Checks every subgroup of S_n for 0 <= n <= 4.
These checks supplement the report's arguments; they are not general proofs.
"""

from itertools import combinations, permutations
from math import comb


def mul(p, q):
    """Right-action convention: first p, then q."""
    return tuple(q[v] for v in p)


def inverse(p):
    q = [0] * len(p)
    for v, w in enumerate(p):
        q[w] = v
    return tuple(q)


def closure(generators, n):
    identity = tuple(range(n))
    found, todo = {identity}, [identity]
    while todo:
        a = todo.pop()
        for b in generators:
            c = mul(a, b)
            if c not in found:
                found.add(c)
                todo.append(c)
    return frozenset(found)


def subgroups(symmetric, n):
    trivial = closure((), n)
    found, todo = {trivial}, [trivial]
    while todo:
        group = todo.pop()
        for p in symmetric:
            if p not in group:
                larger = closure(tuple(group) + (p,), n)
                if larger not in found:
                    found.add(larger)
                    todo.append(larger)
    return sorted(found, key=lambda g: (len(g), sorted(g)))


def pointwise_stabilizer(group, points):
    return frozenset(g for g in group if all(g[a] == a for a in points))


def act_list(values, g):
    return tuple(g[v] for v in values)


def normalized_orbits(group, fixed):
    """All transporter choices must give the same ordered pulled-back cells."""
    n = len(next(iter(group)))
    least = min(act_list(fixed, g) for g in group)
    stabilizer = pointwise_stabilizer(group, least)
    cells = sorted({tuple(sorted(g[a] for g in stabilizer)) for a in range(n)})
    # Remove repeated entries within each orbit before comparing/pulling back.
    cells = sorted({tuple(sorted(set(cell))) for cell in cells})
    results = set()
    for u in group:
        if act_list(fixed, u) == least:
            u_inv = inverse(u)
            results.add(tuple(tuple(sorted(u_inv[a] for a in cell)) for cell in cells))
    assert len(results) == 1
    return next(iter(results))


def run():
    totals = {"groups": 0, "leaf_identities": 0, "coset_splits": 0,
              "normalization_cases": 0, "graph_prefix_cases": 0}
    for n, expected_count in enumerate((1, 1, 2, 6, 30)):
        symmetric = tuple(permutations(range(n)))
        groups = subgroups(symmetric, n)
        assert len(groups) == expected_count
        totals["groups"] += len(groups)
        for group in groups:
            for values in symmetric:
                t = min(group, key=lambda g: act_list(values, g))
                for h in group:
                    renamed = act_list(values, h)
                    t_renamed = min(group, key=lambda g: act_list(renamed, g))
                    assert t_renamed == mul(inverse(h), t)
                    totals["leaf_identities"] += 1
            for a in range(n):
                stabilizer = pointwise_stabilizer(group, (a,))
                orbit = sorted({g[a] for g in group})
                representatives = [min(g for g in group if g[a] == b) for b in orbit]
                for r in symmetric:
                    children = [{mul(mul(h, t), r) for h in stabilizer}
                                for t in representatives]
                    assert set.union(*children) == {mul(g, r) for g in group}
                    assert sum(map(len, children)) == len(group)
                    totals["coset_splits"] += 1
            for k in range(n + 1):
                for fixed in permutations(range(n), k):
                    cells = normalized_orbits(group, fixed)
                    for h in group:
                        renamed_cells = normalized_orbits(group, act_list(fixed, h))
                        assert renamed_cells == tuple(
                            tuple(sorted(h[a] for a in cell)) for cell in cells)
                    totals["normalization_cases"] += 1

        # The graph encoding used in the report's Independent Set reduction.
        edge_order = tuple((i, j) for j in range(n) for i in range(j))
        for mask in range(1 << len(edge_order)):
            edges = {e for bit, e in enumerate(edge_order) if mask & (1 << bit)}
            encodings = []
            for g in symmetric:
                renamed = {tuple(sorted((g[i], g[j]))) for i, j in edges}
                encodings.append(tuple(int(e in renamed) for e in edge_order))
            least = min(encodings)
            for k in range(n + 1):
                exists = any(all((i, j) not in edges for i, j in combinations(s, 2))
                             for s in combinations(range(n), k))
                assert (all(v == 0 for v in least[:comb(k, 2)])) == exists
                totals["graph_prefix_cases"] += 1
    print("PASS:", totals)
    for n in (1_000, 10_000, 100_000):
        print(f"Sym({n}) full-transversal output, uint32: {2 * n * n * (n - 1):,} bytes")
    print("Shared tuple DAG depth 60: 61 nodes, 2^60 unfolded literal occurrences")


if __name__ == "__main__":
    run()
