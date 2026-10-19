# Definitional brute-force check of ref-b's records on a case file (oracle-style, small
# groups only): julia tests/check_corpus.jl CASEFILE OUT.tsv [MAXORDER] [STRIDE]
# For each COMPLETE record whose group has order <= MAXORDER (default 5000), checks
# against the explicit element list of G: c in x^G and invariance C(x^h) = C(x) for a
# random h; witnesses are the least solutions; transporter emptiness; stabiliser and
# coset bytes from the brute-force A; labeling λ0 = min Aλ; signed zero iff A has an odd
# element, the sign and the least odd element. Traces are not oracle-checkable.
include(joinpath(@__DIR__, "..", "src", "refb.jl"))
using Random

function closure(n, gens)
    els = Set{Perm}([pid(n)]); fr = [pid(n)]
    while !isempty(fr)
        nx = Perm[]
        for e in fr, g in gens
            h = pmul(e, g)
            h in els || (push!(els, h); push!(nx, h))
        end
        fr = nx
    end
    return sort(collect(els))
end

function main(casefile, outfile, maxord=5000, stride=1)
    doc = json_parse_file(casefile)
    cases = haskey(doc, "cases") ? doc["cases"] : doc["p1_cases"]
    recs = Dict{String,Vector{String}}()
    for l in eachline(outfile)
        f = split(l, '\t'); recs[f[1]] = f
    end
    rng = MersenneTwister(11)
    checked = 0; bad = 0
    for (ci, c) in enumerate(cases)
        (ci - 1) % stride == 0 || continue
        r = recs[c["id"]]
        r[3] == "COMPLETE" || continue
        n = c["n"]; gens = Perm[Int.(g) for g in c["group_generators"]]
        group_order(full_chain(n, gens)) <= maxord || continue
        els = closure(n, gens)
        x = parse_object(c["object"], n)
        tag = c["objective_tag"]
        img(g) = act(x, g).stream
        W(s) = s == "-" ? Int[] : parse.(Int, split(s, ","))
        ok = true
        A = [g for g in els if img(g) == x.stream]
        if tag == 1 || tag == 5 || (tag == 7 && r[5] != "00")
            cb = hex2bytes(r[5])
            # c in x^G; for the labeling c in x^(Gρ) (spec §3.1: λ is not in G)
            lam = tag == 5 ? Int.(c["labeling"]["rho"]) : pid(n)
            ok &= any(g -> img(pmul(g, lam)) == cb, els)
            # invariance: re-solve on x^h
            h = rand(rng, els)
            c2 = deepcopy(c)
            # transport the input object through its normalised DAG form
            xh = act(x, h)
            c2["object"] = obj_to_input(xh)
            if tag == 7
                c2["character"] = c["character"]
            end
            r2 = solve_case(c2)
            ok &= (r2.trace == r[4] && r2.bytes == r[5])
        end
        if tag == 1
            ok &= W(r[6]) == minimum(g for g in els if img(g) == hex2bytes(r[5]))
        elseif tag == 2
            su1 = c["order"] == "SIMPLE-UPPER-1"
            key(g) = su1 ? su1_key(act(x, g)) : img(g)
            m = minimum(key(g) for g in els)
            ok &= W(r[6]) == minimum(g for g in els if key(g) == m)
        elseif tag == 3 || tag == 6
            y = parse_object(c["target"], n)
            sols = [g for g in els if img(g) == y.stream]
            if isempty(sols)
                ok &= r[6] == "-" && r[7] == "-"
            else
                ok &= W(r[6]) == minimum(sols)
                if tag == 6
                    ok &= hex2bytes(r[7]) == perm_bytes!(group_bytes!(UInt8[], n, A), minimum(sols))
                end
            end
        elseif tag == 4
            ok &= hex2bytes(r[7]) == group_bytes!(UInt8[], n, A)
        elseif tag == 5
            rho = Int.(c["labeling"]["rho"])
            sols = [pmul(g, rho) for g in els if act(x, pmul(g, rho)).stream == hex2bytes(r[5])]
            ok &= W(r[6]) == minimum(sols)
            ok &= hex2bytes(r[7]) == perm_bytes!(group_bytes!(UInt8[], n, A), minimum(sols))
        elseif tag == 7
            Bl = full_chain(n + 2, Perm[lift(g, Int(s)) for (g, s) in zip(gens, c["character"])])
            odd = [a for a in A if chi(Bl, a) == -1]
            w, s = split(r[6], ";sign=")
            if isempty(odd)
                sols = [g for g in els if img(g) == hex2bytes(r[5])]
                ok &= W(w) == minimum(sols) && parse(Int, s) == chi(Bl, minimum(sols))
                ok &= hex2bytes(r[7]) == group_bytes!(UInt8[], n, A)
            else
                ok &= r[5] == "00" && s == "0" && W(w) == minimum(odd)
            end
        end
        checked += 1
        if !ok
            bad += 1
            println("BAD ", c["id"])
        end
    end
    println("checked $checked, bad $bad")
end

# an input description of a normalised object (as a dag of its canonical nodes)
function obj_to_input(o::Obj)
    if o.kind == :subset
        return Dict{String,Any}("kind" => "subset", "atoms" => Any[o.atoms...])
    elseif o.kind == :tuple
        return Dict{String,Any}("kind" => "tuple", "points" => Any[o.atoms...])
    elseif o.kind == :graph
        return Dict{String,Any}("kind" => "graph", "vertex_colours_hex" => Any[bytes2hex(c) for c in o.graph.colours],
            "arcs" => Any[Dict{String,Any}("source" => a.s, "target" => a.t, "label_hex" => bytes2hex(a.label), "multiplicity" => a.m) for a in o.graph.arcs])
    end
    recs = Any[]
    for nd in o.nodes
        t = Int(nd.tag)
        d = Dict{String,Any}("tag" => t)
        t == 1 && (d["atom"] = nd.atom)
        t == 2 && (d["literal_hex"] = bytes2hex(nd.lit))
        (t == 3 || t == 4) && (d["children"] = Any[(nd.kids .- 1)...])
        t == 5 && (d["pairs"] = Any[Dict{String,Any}("child" => k - 1, "multiplicity" => m) for (k, m) in zip(nd.kids, nd.mults)])
        t == 6 && (d["perm"] = Any[nd.perm...])
        t == 7 && (d["group_generators"] = Any[Any[g...] for g in nd.gens])
        t == 8 && (d["group_generators"] = Any[Any[g...] for g in nd.gens]; d["r"] = Any[nd.r...])
        push!(recs, d)
    end
    # reverse the record order (insertion order has no meaning, spec §4.2)
    N = length(recs)
    rev = Any[]
    for i in N:-1:1
        d = copy(recs[i])
        haskey(d, "children") && (d["children"] = Any[N - 1 - k for k in d["children"]])
        haskey(d, "pairs") && (d["pairs"] = Any[Dict{String,Any}("child" => N - 1 - p["child"], "multiplicity" => p["multiplicity"]) for p in d["pairs"]])
        push!(rev, d)
    end
    return Dict{String,Any}("kind" => "dag", "records" => rev, "root" => 0)
end

main(ARGS[1], ARGS[2], length(ARGS) >= 3 ? parse(Int, ARGS[3]) : 5000, length(ARGS) >= 4 ? parse(Int, ARGS[4]) : 1)
