# ref-b's own tests: julia tests/runtests.jl
# Convention checks derived from the rules of spec §3 (no golden constants), kernel
# checks against brute force, and invariance of the outputs under the group,
# generator reordering, redundant generators, inverses and DAG sharing (spec §20).
include(joinpath(@__DIR__, "..", "src", "refb.jl"))
using Test, Random

# brute-force closure of <gens> (small groups only)
function closure(n, gens)
    els = Set{Perm}([pid(n)])
    frontier = [pid(n)]
    while !isempty(frontier)
        nxt = Perm[]
        for e in frontier, g in gens
            h = pmul(e, g)
            if !(h in els)
                push!(els, h); push!(nxt, h)
            end
        end
        frontier = nxt
    end
    return sort(collect(els))
end

randperm0(rng, n) = randperm(rng, n) .- 1
# sign of a permutation: (-1)^(n - number of cycles)
psign(p) = iseven(length(p) - length(orbits(length(p), Perm[p])[1])) ? 1 : -1

function randgroup(rng, n)
    k = rand(rng, 0:3)
    return Perm[randperm0(rng, n) for _ in 1:k]
end

rec(c) = solve_case(c)

function mkcase(id, tag, n, gens, obj; extra...)
    d = Dict{String,Any}("id" => id, "objective_tag" => tag, "objective" => OBJ_NAMES[tag],
        "profile" => OBJ_PROFILE[tag], "encoding" => 2, "n" => n,
        "group_generators" => Any[Any[v for v in g] for g in gens], "object" => obj)
    for (k, v) in extra
        d[string(k)] = v
    end
    return d
end

subsetobj(a) = Dict{String,Any}("kind" => "subset", "atoms" => Any[a...])
tupleobj(a) = Dict{String,Any}("kind" => "tuple", "points" => Any[a...])
function graphobj(rng, n)
    arcs = Any[]
    for _ in 1:rand(rng, 0:2n)
        push!(arcs, Dict{String,Any}("source" => rand(rng, 0:n-1), "target" => rand(rng, 0:n-1),
            "label_hex" => rand(rng, ["", "", "01", "0a0b"]), "multiplicity" => rand(rng, 1:2)))
    end
    Dict{String,Any}("kind" => "graph", "vertex_colours_hex" => Any[rand(rng, ["", "", "ff"]) for _ in 1:n], "arcs" => arcs)
end
function act_input(o, g)   # transport an input object description by g (independent of act)
    if o["kind"] == "subset"
        return subsetobj([g[a+1] for a in o["atoms"]])
    elseif o["kind"] == "tuple"
        return tupleobj([g[a+1] for a in o["points"]])
    else
        n = length(g)
        cols = Vector{Any}(undef, n)
        for v in 0:n-1; cols[g[v+1]+1] = o["vertex_colours_hex"][v+1]; end
        arcs = Any[Dict{String,Any}("source" => g[a["source"]+1], "target" => g[a["target"]+1],
            "label_hex" => a["label_hex"], "multiplicity" => a["multiplicity"]) for a in o["arcs"]]
        return Dict{String,Any}("kind" => "graph", "vertex_colours_hex" => cols, "arcs" => reverse(arcs))
    end
end

@testset "ref-b" begin

@testset "conventions (spec §3)" begin
    rng = MersenneTwister(1)
    for _ in 1:200
        n = rand(rng, 1:7)
        p = randperm0(rng, n); q = randperm0(rng, n)
        # (pq)[v] = q[p[v]] read as functions: first apply p, then q
        fp(v) = p[v+1]; fq(v) = q[v+1]
        @test [fq(fp(v)) for v in 0:n-1] == pmul(p, q)
        # (x^p)^q = x^(pq) on objects; array direction: atom a goes to p[a]
        a = rand(rng, 0:n-1)
        x = make_subset(n, [a])
        @test act(x, p).atoms == [p[a+1]]
        @test act(act(x, p), q).stream == act(x, pmul(p, q)).stream
        # inverses, and inversion of a product reverses its factors
        @test isid(pmul(p, pinv(p))) && isid(pmul(pinv(p), p))
        @test pinv(pmul(p, q)) == pmul(pinv(q), pinv(p))
        # if x^p = y^q then p q^-1 transports x to y
        y = act(act(x, p), pinv(q))
        @test act(x, pmul(p, pinv(q))).stream == y.stream
        # permutation objects: g^-1 c g is cycle conjugation (a b) -> (a^g b^g)
        if n >= 2
            i, j = 0, n - 1
            c = pid(n); c[i+1] = j; c[j+1] = i
            d = pid(n); d[p[i+1]+1] = p[j+1]; d[p[j+1]+1] = p[i+1]
            @test pconj(c, p) == d
        end
    end
    # the spec §7.4 convention inputs, checked by the derivations above
    p = [1, 0, 2]; q = [0, 2, 1]
    @test pmul(p, q) == [q[p[v+1]+1] for v in 0:2]
    @test pinv(p) == p      # p is an involution: p[p[v]] = v for all v
    @test all(p[p[v+1]+1] == v for v in 0:2)
end

@testset "group kernel vs brute force" begin
    rng = MersenneTwister(2)
    for _ in 1:150
        n = rand(rng, 1:6)
        gens = randgroup(rng, n)
        els = closure(n, gens)
        B = full_chain(n, gens)
        @test group_order(B) == length(els)
        @test all(contains(B, g) for g in els)
        h = randperm0(rng, n)
        @test contains(B, h) == (h in els)
        # least element of the coset A w
        w = randperm0(rng, n)
        @test least_coset(B, w) == minimum(pmul(g, w) for g in els)
        # §8.1 enumerator: leaves partition G, visits = sum of products of orbit sizes
        E = Enumerator(B)
        leaves = Perm[]
        bud = Budget(10^9)
        enumerate_cosets(r -> (push!(leaves, r); false), E, bud)
        @test sort(leaves) == els
        @test bud.used == full_visits(E)
        # point stabiliser
        b = rand(rng, 0:n-1)
        S = stabiliser_gens(n, gens, b)
        @test sort(closure(n, S)) == [g for g in els if g[b+1] == b]
        # Group(H) bytes depend only on H: reordered, redundant and inverted generators
        g1 = group_bytes!(UInt8[], n, gens)
        alt = vcat(reverse(gens), Perm[pinv(g) for g in gens], Perm[pmul(g, g) for g in gens], [pid(n)])
        @test group_bytes!(UInt8[], n, alt) == g1
        # rule 2 greedy sequence: each element is the least of H \ <previous>
        if g1[1] == 0x00
            seq = Perm[]
            kk = 0
            while length(closure(n, seq)) < length(els)
                K = Set(closure(n, seq))
                push!(seq, minimum(g for g in els if !(g in K)))
            end
            io = UInt8[0x00]; u32!(io, length(seq))
            for g in seq; perm_bytes!(io, g); end
            @test io == g1
        end
    end
end

@testset "CDAG-2 normalisation (spec §4.2)" begin
    # shared versus duplicated storage, unreachable records, insertion order
    n = 3
    a = Node(0x01; atom=2); b = Node(0x02; lit=UInt8[0x01])
    shared = [a, b, Node(0x03; kids=[1, 2, 1]), Node(0x04; kids=[3, 2, 2])]
    dup = [Node(0x02; lit=UInt8[0x09]), Node(0x04; kids=[5, 4, 6]), Node(0x01; atom=2), Node(0x02; lit=UInt8[0x01]),
           Node(0x03; kids=[3, 4, 7]), Node(0x02; lit=UInt8[0x01]), Node(0x01; atom=2)]
    @test make_dag(n, shared, 4).stream == make_dag(n, dup, 2).stream
    # multiset counts combine
    m1 = [a, Node(0x05; kids=[1, 1], mults=BigInt[1, 2])]
    m2 = [a, Node(0x05; kids=[1], mults=BigInt[3])]
    @test make_dag(n, m1, 2).stream == make_dag(n, m2, 2).stream
    # a set of atoms built as a DAG is the subset; a tuple of atoms is the atom tuple
    @test make_dag(n, [Node(0x01; atom=2), Node(0x01; atom=0), Node(0x04; kids=[1, 2, 1])], 3).stream == make_subset(n, [0, 2]).stream
    @test make_dag(n, [Node(0x01; atom=2), Node(0x01; atom=0), Node(0x03; kids=[1, 2, 1])], 3).stream == make_tuple(n, [2, 0, 2]).stream
    # deep sharing: x_{i+1} = (x_i, x_i) has i+1 records
    nodes = [Node(0x02)]
    for i in 1:20
        push!(nodes, Node(0x03; kids=[i, i]))
    end
    o = make_dag(0, nodes, 21)
    @test length(o.nodes) == 21
    # cyclic input is invalid
    @test_throws InvalidInput make_dag(n, [Node(0x03; kids=[2]), Node(0x03; kids=[1])], 1)
    # action on nested leaves: a coset leaf H r becomes g^-1 H r; check via elements
    rng = MersenneTwister(3)
    for _ in 1:30
        g = randperm0(rng, 3); r = randperm0(rng, 3); h = randperm0(rng, 3)
        x = make_dag(3, [Node(0x08; gens=[h], r=r)], 1)
        y = act(x, g)
        H = closure(3, [h])
        expect = minimum(pmul(pinv(g), pmul(e, r)) for e in H)
        @test y.nodes[1].r == expect
    end
end

@testset "outputs: invariance and witnesses" begin
    rng = MersenneTwister(4)
    for it in 1:120
        n = rand(rng, 1:5)
        gens = randgroup(rng, n)
        els = closure(n, gens)
        kind = rand(rng, 1:3)
        obj = kind == 1 ? subsetobj(unique(rand(rng, 0:n-1, rand(rng, 0:n)))) :
              kind == 2 ? tupleobj(rand(rng, 0:n-1, rand(rng, 0:3))) : graphobj(rng, n)
        x = parse_object(obj, n)
        h = rand(rng, els)
        objh = act_input(obj, h)
        variants = [gens, reverse(gens), vcat(gens, [pid(n)], Perm[pinv(g) for g in gens])]
        for tag in (1, 2, 4, 5, 7)
            extra = Dict{Symbol,Any}()
            tag == 2 && (extra[:order] = "CDAG-BYTE-1")
            if tag == 5
                extra[:labeling] = Dict{String,Any}("rho" => Any[randperm0(rng, n)...])
            end
            if tag == 7
                # a genuine character: sign of the permutation restricted to G
                sgn(p) = psign(p)
                extra[:character] = Any[sgn(g) for g in gens]
            end
            base = rec(mkcase("a", tag, n, gens, obj; extra...))
            @test base.status == "COMPLETE"
            # generator reordering / redundancy: identical records
            for (vi, gv) in enumerate(variants)
                ex2 = copy(extra)
                if tag == 7
                    sg(p) = psign(p)
                    ex2[:character] = Any[sg(g) for g in gv]
                end
                r2 = rec(mkcase("a", tag, n, gv, obj; ex2...))
                @test (r2.trace, r2.bytes, r2.witness, r2.group) == (base.trace, base.bytes, base.witness, base.group)
            end
            # invariance under x -> x^h: same canonical trace and bytes
            rh = rec(mkcase("a", tag, n, gens, objh; extra...))
            if tag in (1, 5)
                @test (rh.trace, rh.bytes) == (base.trace, base.bytes)
            elseif tag == 2
                @test rh.bytes == base.bytes
            elseif tag == 7
                @test (rh.trace, rh.bytes) == (base.trace, base.bytes)
                if base.bytes != "00"
                    # covariance of the sign: s(x^h) = χ(h) s(x)
                    sgnh = psign(h)
                    s1 = parse(Int, split(base.witness, "=")[2]); s2 = parse(Int, split(rh.witness, "=")[2])
                    @test s2 == sgnh * s1
                end
            end
            # witnesses: brute-force least solution
            if tag == 1 && n > 0
                c = parse_object(obj, n)
                sols = [g for g in els if act(x, g).stream == hex2bytes(base.bytes)]
                @test !isempty(sols)
                @test base.witness == join(minimum(sols), ",")
            elseif tag == 2 && n > 0
                m = minimum(act(x, g).stream for g in els)
                @test hex2bytes(base.bytes) == m
                @test base.witness == join(minimum(g for g in els if act(x, g).stream == m), ",")
            elseif tag == 7 && n > 0
                A = [g for g in els if act(x, g).stream == x.stream]
                odd = [g for g in A if psign(g) == -1]
                if isempty(odd)
                    sols = [g for g in els if act(x, g).stream == hex2bytes(base.bytes)]
                    w = minimum(sols)
                    @test base.witness == join(w, ",") * ";sign=" * (psign(w) == 1 ? "+1" : "-1")
                    @test hex2bytes(base.group) == group_bytes!(UInt8[], n, A)
                else
                    @test base.bytes == "00" && base.trace == "" && base.group == "-"
                    @test base.witness == join(minimum(odd), ",") * ";sign=0"
                end
            elseif tag == 4
                A = [g for g in els if act(x, g).stream == x.stream]
                @test hex2bytes(base.group) == group_bytes!(UInt8[], n, A)
            elseif tag == 5 && n > 0
                rho = Int[v for v in extra[:labeling]["rho"]]
                # complete labeling coset A λ: all labelings κ in Gρ with x^κ = c
                cands = [pmul(g, rho) for g in els]
                sols = [k for k in cands if act(x, k).stream == hex2bytes(base.bytes)]
                @test base.witness == join(minimum(sols), ",")
            end
        end
        # transporters: y in the orbit and y outside it
        y = act_input(obj, rand(rng, els))
        r3 = rec(mkcase("t", 3, n, gens, obj; target=y))
        r6 = rec(mkcase("t", 6, n, gens, obj; target=y))
        yy = parse_object(y, n)
        sols = [g for g in els if act(x, g).stream == yy.stream]
        @test !isempty(sols)
        @test r3.witness == (n == 0 ? "-" : join(minimum(sols), ","))
        @test r6.witness == r3.witness
        r7 = rec(mkcase("t", 3, n, gens, obj; target=Dict{String,Any}("kind" => "tuple", "points" => Any[0, 0, 0, 0, 0])))
        @test r7.status == "COMPLETE" && r7.witness == "-"
    end
end

@testset "quota (spec §11.1)" begin
    n = 4
    gens = Perm[[1, 2, 3, 0], [1, 0, 2, 3]]     # Sym(4)
    E = Enumerator(full_chain(n, gens))
    v = full_visits(E)          # 1 + 4 + 12 + 24
    @test v == 41
    obj = subsetobj([0])
    ok = rec(mkcase("q", 4, n, gens, obj; capacity=Dict{String,Any}("max_search_nodes" => 41)))
    no = rec(mkcase("q", 4, n, gens, obj; capacity=Dict{String,Any}("max_search_nodes" => 40)))
    @test ok.status == "COMPLETE" && no.status == "CAPACITY_LIMIT"
    @test (no.trace, no.bytes, no.witness, no.group) == ("", "", "-", "-")
    # signed singleton-orbit route visits nothing: quota 0 suffices
    s0 = rec(mkcase("q", 7, n, gens, subsetobj([]); character=Any[-1, -1], capacity=Dict{String,Any}("max_search_nodes" => 0)))
    @test s0.status == "COMPLETE" && s0.bytes == "00"
end

end
