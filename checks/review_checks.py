"""Finite sanity checks for the referee report; no production solver is tested.

Run with Python 3. Checks every subgroup of S_n for 0 <= n <= 4.
The v2 additions cover small P1/profile/wire, labeling and signed constructions.
These checks supplement the documents' arguments; they are not general proofs.
"""

from itertools import combinations, permutations, product
from math import comb, factorial, prod


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


def u32(n):
    return n.to_bytes(4, "big")


def blob(value):
    return u32(len(value)) + value


def header(n):
    return bytes.fromhex("43 4e 02 00 01 00 01") + u32(n)


def dag_bytes(n, records, root):
    """Tiny exact normaliser, only tags 1..4; input references are topological.

    Intern child identities, never expanded subtrees (including the depth-60 case).
    This is a finite-check helper, not a complete wire parser/production adapter.
    """
    reachable, todo = set(), [root]
    while todo:
        i = todo.pop()
        if i in reachable:
            continue
        reachable.add(i)
        tag, value = records[i]
        if tag in (3, 4):
            assert all(j < i for j in value)
            todo.extend(value)
    intern, nodes, heights, source_id = {}, [], [], {}
    for i in sorted(reachable):
        tag, value = records[i]
        if tag in (3, 4):
            value = tuple(source_id[j] for j in value)
            if tag == 4:
                value = tuple(sorted(set(value)))
        key = (tag, value)
        if key not in intern:
            intern[key] = len(nodes)
            nodes.append(key)
            heights.append(1 + max((heights[j] for j in value), default=-1)
                           if tag in (3, 4) else 0)
        source_id[i] = intern[key]
    ranks, emitted = {}, []
    def record_bytes(i):
        tag, value = nodes[i]
        if tag == 1:
            payload = u32(value)
        elif tag == 2:
            payload = blob(value)
        else:
            refs = [ranks[j] for j in value]
            if tag == 4:
                refs.sort()
            payload = u32(len(refs)) + b"".join(u32(j) for j in refs)
        return bytes([tag]) + payload
    for height in sorted(set(heights)):
        layer = sorted((record_bytes(i), i) for i in range(len(nodes))
                       if heights[i] == height)
        for code, i in layer:
            ranks[i] = len(emitted)
            emitted.append(code)
    return (header(n) + u32(len(emitted)) + b"".join(emitted)
            + u32(ranks[source_id[root]]))


def subset_bytes(n, subset):
    atoms = [(1, a) for a in sorted(subset)]
    return dag_bytes(n, atoms + [(4, tuple(range(len(atoms))))], len(atoms))


def nat(n):
    return blob(n.to_bytes((n.bit_length() + 7) // 8, "big"))


def graph_bytes(n, graph):
    colours, arcs = graph
    # Arc tuples are (source, target, bytes label, positive multiplicity).
    counts = {}
    for a, b, label, count in arcs:
        assert count > 0
        counts[a, b, label] = counts.get((a, b, label), 0) + count
    keys = sorted(counts, key=lambda e: (e[0], e[1], blob(e[2])))
    record = b"\x09" + b"".join(blob(c) for c in colours) + u32(len(keys))
    for a, b, label in keys:
        record += u32(a) + u32(b) + blob(label) + nat(counts[a, b, label])
    return header(n) + u32(1) + record + u32(0)


def act_object(kind, obj, g):
    if kind == "subset":
        return frozenset(g[a] for a in obj)
    colours, arcs = obj
    renamed = [None] * len(g)
    for a, colour in enumerate(colours):
        renamed[g[a]] = colour
    return tuple(renamed), tuple((g[a], g[b], label, m) for a, b, label, m in arcs)


def p1(n, group, kind, obj):
    """Unpruned finite P1 evaluator; group operations use explicit enumeration."""
    def split(partition, signature):
        output = []
        for cell in partition:
            buckets = {}
            for a in cell:
                buckets.setdefault(signature[a], []).append(a)
            output.extend(tuple(buckets[k]) for k in sorted(buckets))
        return tuple(output)
    initial = {a: (int(a in obj) if kind == "subset" else blob(obj[0][a]))
               for a in range(n)}
    partition = split((tuple(range(n)),) if n else (), initial)
    candidates = []
    def stage(tag, partition):
        return bytes([tag]) + u32(len(partition)) + b"".join(u32(len(c)) for c in partition)
    def visit(partition, depth, trace):
        trace += b"\x10" + u32(depth)
        while True:
            old_count = len(partition)
            signatures = {a: () for a in range(n)}
            if kind == "graph":
                labels = sorted({e[2] for e in obj[1]}, key=blob)
                for a in range(n):
                    signatures[a] = tuple(count for label in labels for cell in partition
                        for count in (sum(m for x, y, l, m in obj[1]
                                          if x == a and y in cell and l == label),
                                      sum(m for x, y, l, m in obj[1]
                                          if y == a and x in cell and l == label)))
            partition = split(partition, signatures)
            trace += stage(0x20, partition)
            fixed = tuple(c[0] for c in partition if len(c) == 1)
            orbits = normalized_orbits(group, fixed)
            signatures = {a: i for i, cell in enumerate(orbits) for a in cell}
            partition = split(partition, signatures)
            trace += stage(0x21, partition)
            if len(partition) == old_count:
                break
        if all(len(c) == 1 for c in partition):
            values = tuple(c[0] for c in partition)
            t = min(group, key=lambda g: act_list(values, g))
            image = act_object(kind, obj, t)
            enc = subset_bytes(n, image) if kind == "subset" else graph_bytes(n, image)
            candidates.append((trace + b"\x00", enc, t))
            return
        _, index = min((len(c), i) for i, c in enumerate(partition) if len(c) > 1)
        cell = partition[index]
        for a in cell:
            child = partition[:index] + ((a,), tuple(b for b in cell if b != a)) + partition[index+1:]
            visit(child, depth + 1, trace)
    visit(partition, 0, b"")
    return min(candidates)   # least witness breaks ties only as metadata


def perm_bytes(g):
    moved = [(a, b) for a, b in enumerate(g) if a != b]
    return u32(len(moved)) + b"".join(u32(a) + u32(b) for a, b in moved)


def group_bytes(group):
    n = len(next(iter(group)))
    orbits = sorted({tuple(sorted({g[a] for g in group})) for a in range(n)})
    if len(group) == prod(factorial(len(o)) for o in orbits):
        blocks = [o for o in orbits if len(o) > 1]
        return b"\x01" + u32(len(blocks)) + b"".join(
            u32(len(o)) + b"".join(u32(a) for a in o) for o in blocks)
    known, chosen = closure((), n), []
    while known != group:
        chosen.append(min(group - known))
        known = closure(chosen, n)
    return b"\x00" + u32(len(chosen)) + b"".join(perm_bytes(g) for g in chosen)


def characters(group):
    elements = sorted(group)
    identity = tuple(range(len(elements[0])))
    others = [g for g in elements if g != identity]
    for bits in product((1, -1), repeat=len(others)):
        chi = {identity: 1, **dict(zip(others, bits))}
        if all(chi[mul(g, h)] == chi[g] * chi[h] for g in group for h in group):
            yield chi


def run_v2():
    totals = {"p1_subset_transports": 0, "p1_graph_transports": 0,
              "labeling_cosets": 0, "signed_cases": 0, "golden_checks": 0,
              "greedy_group_sequences": 0}
    empty_trace = bytes.fromhex("10 00000000 20 00000000 21 00000000 00")
    split_trace = bytes.fromhex("10 00000000 20 00000002 00000001 00000001 "
                               "21 00000002 00000001 00000001 00")
    branch_trace = bytes.fromhex("10 00000000 20 00000001 00000002 21 00000001 00000002 "
                                "10 00000001 20 00000002 00000001 00000001 "
                                "21 00000002 00000001 00000001 00")
    s2 = frozenset(permutations(range(2)))
    golden = [
        (0, closure((), 0), frozenset(), empty_trace,
         "00000001 04 00000000 00000000", ()),
        (2, s2, frozenset({0}), split_trace,
         "00000002 01 00000001 04 00000001 00000000 00000001", (1, 0)),
        (2, closure((), 2), frozenset({0}), split_trace,
         "00000002 01 00000000 04 00000001 00000000 00000001", (0, 1)),
        (2, s2, frozenset(), branch_trace,
         "00000001 04 00000000 00000000", (0, 1))]
    for n, group, obj, trace, body, witness in golden:
        assert p1(n, group, "subset", obj) == (trace, header(n) + bytes.fromhex(body), witness)
        totals["golden_checks"] += 1
    graph_gold_trace = bytes.fromhex(
        "10 00000000 20 00000002 00000001 00000001 21 00000002 00000001 00000001 "
        "20 00000002 00000001 00000001 21 00000002 00000001 00000001 00")
    graph_gold_bytes = header(2) + bytes.fromhex(
        "00000001 09 00000000 00000000 00000001 00000001 00000000 00000000 00000001 01 00000000")
    assert p1(2, s2, "graph", ((b"", b""), ((0, 1, b"", 1),))) == (
        graph_gold_trace, graph_gold_bytes, (1, 0))
    assert group_bytes(closure((), 0)) == bytes.fromhex("01 00000000")
    assert group_bytes(s2) == bytes.fromhex("01 00000001 00000002 00000000 00000001")
    assert group_bytes(closure(((1, 2, 0),), 3)) == bytes.fromhex(
        "00 00000001 00000003 00000000 00000001 00000001 00000002 00000002 00000000")
    assert group_bytes(closure((), 2)) + perm_bytes((1, 0)) == bytes.fromhex(
        "01 00000000 00000002 00000000 00000001 00000001 00000000")
    totals["golden_checks"] += 5
    tuple_gold = header(0) + bytes.fromhex("00000002 02 00000000 03 00000002 "
                                          "00000000 00000000 00000001")
    assert dag_bytes(0, [(2, b""), (3, (0, 0))], 1) == tuple_gold
    assert dag_bytes(0, [(2, b""), (2, b""), (3, (1, 0))], 2) == tuple_gold
    # Same interning under reversed allocation/insertion, with an unreachable literal.
    assert dag_bytes(2, [(1, 1), (1, 0), (2, b"unused"), (4, (0, 1, 0))], 3) == subset_bytes(2, {0, 1})
    deep = [(2, b"x")] + [(3, (i-1, i-1)) for i in range(1, 61)]
    assert len(dag_bytes(0, deep, 60)) == 19 + 6 + 60 * 13
    totals["golden_checks"] += 4
    p, q = (1, 0, 2), (0, 2, 1)
    assert mul(p, q) == (2, 0, 1) and mul(q, p) == (1, 2, 0)
    assert inverse(p) == p and act_list((0, 2), mul(p, q)) == (2, 1)
    rho = (1, 0)
    assert mul(rho, (0, 1)) == rho and mul(inverse(rho), rho) == (0, 1)
    totals["golden_checks"] += 2
    # n=1 loop graph: duplicated unit arcs equal a count-two arc.
    graph = ((b"",), ((0, 0, b"", 1), (0, 0, b"", 1)))
    assert graph_bytes(1, graph) == graph_bytes(1, ((b"",), ((0, 0, b"", 2),)))
    totals["golden_checks"] += 1

    for n in range(4):
        symmetric = tuple(permutations(range(n)))
        identity = tuple(range(n))
        for group in subgroups(symmetric, n):
            # Greedy generators, including the explicitly empty trivial-domain case.
            known, chosen = closure((), n), []
            while known != group:
                previous = len(known)
                chosen.append(min(group - known))
                known = closure(chosen, n)
                assert len(known) >= 2 * previous
            assert 2 ** len(chosen) <= len(group)
            totals["greedy_group_sequences"] += 1
            for mask in range(1 << n):
                obj = frozenset(a for a in range(n) if mask >> a & 1)
                reference = p1(n, group, "subset", obj)
                for h in group:
                    moved = act_object("subset", obj, h)
                    assert p1(n, group, "subset", moved)[:2] == reference[:2]
                    totals["p1_subset_transports"] += 1
                for rho in symmetric:
                    target_group = frozenset(mul(mul(inverse(rho), g), rho) for g in group)
                    transformed = act_object("subset", obj, rho)
                    result = p1(n, target_group, "subset", transformed)
                    lam = mul(rho, result[2])
                    assert lam in {mul(g, rho) for g in group}
                    assert subset_bytes(n, act_object("subset", obj, lam)) == result[1]
                    stabilizer = [g for g in group if act_object("subset", obj, g) == obj]
                    all_labelings = {mul(g, rho) for g in group
                                     if subset_bytes(n, act_object("subset", obj, mul(g, rho))) == result[1]}
                    assert {mul(a, lam) for a in stabilizer} == all_labelings
                    for k in group:
                        other = mul(k, rho)
                        assert p1(n, target_group, "subset", act_object("subset", obj, other))[:2] == result[:2]
                    # Rename source coordinates by a nontrivial mu, if available.
                    mu = symmetric[-1]
                    renamed_group = frozenset(mul(mul(inverse(mu), g), mu) for g in group)
                    new_rho = mul(inverse(mu), rho)
                    assert {mul(g, new_rho) for g in renamed_group} == {
                        mul(inverse(mu), mul(g, rho)) for g in group}
                    assert act_object("subset", act_object("subset", obj, mu), new_rho) == transformed
                    totals["labeling_cosets"] += 1
                for chi in characters(group):
                    stabilizer = [g for g in group if act_object("subset", obj, g) == obj]
                    odd = any(chi[g] == -1 for g in stabilizer)
                    lift = {g + ((n, n+1) if chi[g] == 1 else (n+1, n)) for g in group}
                    assert len(lift) == len(group)
                    assert all(mul(g, h) in lift for g in lift for h in lift)
                    assert sum(g[:n] == identity for g in lift) == 1
                    assert odd == any(g[:n] in stabilizer and g[n] == n+1 for g in lift)
                    signs_by_image = {}
                    for g in group:
                        image = act_object("subset", obj, g)
                        signs_by_image.setdefault(image, set()).add(chi[g])
                    assert odd == any(len(signs) == 2 for signs in signs_by_image.values())
                    if not odd:
                        for h in group:
                            other = p1(n, group, "subset", act_object("subset", obj, h))
                            assert chi[other[2]] == chi[h] * chi[reference[2]]
                    totals["signed_cases"] += 1
            if n <= 2:
                # All n<=2 digraph arc multiplicities 0,1,2, including loops;
                # distinct fixed vertex colours also exercise the initial order.
                for multiplicities, distinct_colours in product(product(range(3), repeat=n*n), (False, True)):
                    graph = (tuple(bytes([a % 2]) if distinct_colours else b"" for a in range(n)),
                             tuple((a, b, b"", multiplicities[a*n+b])
                                   for a in range(n) for b in range(n)
                                   if multiplicities[a*n+b]))
                    reference = p1(n, group, "graph", graph)
                    for h in group:
                        assert p1(n, group, "graph", act_object("graph", graph, h))[:2] == reference[:2]
                        totals["p1_graph_transports"] += 1
    # An identity generator marked odd gives a forbidden projection kernel.
    inconsistent_lift = closure(((0, 2, 1),), 3)
    assert any(g[0] == 0 and g[1] == 2 for g in inconsistent_lift)
    print("PASS v2 finite checks:", totals)


if __name__ == "__main__":
    run()
    run_v2()
