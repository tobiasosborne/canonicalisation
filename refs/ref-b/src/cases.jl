# Case-record parsing and validation (refs/CASES.md Parts A and B).

isint(x) = x isa Integer && !(x isa Bool)

function req(d::Dict, k::String)
    haskey(d, k) || throw(InvalidInput("missing key $k"))
    return d[k]
end

function parse_hex(s)
    s isa String || throw(InvalidInput("hex field is not a string"))
    iseven(length(s)) || throw(InvalidInput("odd-length hex"))
    out = Vector{UInt8}(undef, length(s) ÷ 2)
    for i in 1:length(out)
        hi = hexdigit(codeunit(s, 2i - 1))
        lo = hexdigit(codeunit(s, 2i))
        out[i] = UInt8(16hi + lo)
    end
    return out
end
function hexdigit(c::UInt8)
    UInt8('0') <= c <= UInt8('9') && return Int(c - UInt8('0'))
    UInt8('a') <= c <= UInt8('f') && return Int(c - UInt8('a')) + 10
    UInt8('A') <= c <= UInt8('F') && return Int(c - UInt8('A')) + 10
    throw(InvalidInput("bad hex digit"))
end

function parse_atom(x, n)
    (isint(x) && 0 <= x < n) || throw(InvalidInput("atom out of domain"))
    return Int(x)
end

function parse_perm(p, n)
    (p isa Vector && isperm0(p, n)) || throw(InvalidInput("not a bijection of degree n"))
    return Int[Int(v) for v in p]
end

function parse_gens(a, n)
    a isa Vector || throw(InvalidInput("generators not an array"))
    return Perm[parse_perm(p, n) for p in a]
end

function parse_mult(m)
    (isint(m) && m > 0) || throw(InvalidInput("multiplicity must be a positive integer"))
    return big(m)
end

function parse_graph(o::Dict, n::Int)
    cols = req(o, "vertex_colours_hex")
    (cols isa Vector && length(cols) == n) || throw(InvalidInput("need exactly n vertex colours"))
    colours = Vector{UInt8}[parse_hex(c) for c in cols]
    arcs_in = req(o, "arcs")
    arcs_in isa Vector || throw(InvalidInput("arcs not an array"))
    arcs = Arc[]
    for a in arcs_in
        a isa Dict || throw(InvalidInput("arc not an object"))
        push!(arcs, Arc(parse_atom(req(a, "source"), n), parse_atom(req(a, "target"), n),
                        parse_hex(req(a, "label_hex")), parse_mult(req(a, "multiplicity"))))
    end
    return GraphData(colours, normalise_arcs(arcs))
end

function parse_relations(rs, n)
    rs isa Vector || throw(InvalidInput("relations not an array"))
    rels = Relation[]
    for r in rs
        r isa Dict || throw(InvalidInput("relation not an object"))
        name = parse_hex(req(r, "name_hex"))
        ar = req(r, "arity")
        (isint(ar) && ar >= 0) || throw(InvalidInput("bad arity"))
        ts = req(r, "tuples")
        ts isa Vector || throw(InvalidInput("tuples not an array"))
        tuples = Tuple{Vector{Int},BigInt}[]
        for t in ts
            t isa Dict || throw(InvalidInput("tuple not an object"))
            at = req(t, "atoms")
            (at isa Vector && length(at) == ar) || throw(InvalidInput("tuple arity mismatch"))
            push!(tuples, (Int[parse_atom(v, n) for v in at], parse_mult(req(t, "multiplicity"))))
        end
        push!(rels, normalise_relation(name, Int(ar), tuples))
    end
    return sort_relations(rels)
end

function parse_dag(o::Dict, n::Int)
    recs = req(o, "records")
    recs isa Vector || throw(InvalidInput("records not an array"))
    N = length(recs)
    N >= 1 || throw(InvalidInput("no records"))
    ref(c) = (isint(c) && 0 <= c < N) ? Int(c) + 1 : throw(InvalidInput("child reference out of range"))
    nodes = Node[]
    for r in recs
        r isa Dict || throw(InvalidInput("record not an object"))
        tag = req(r, "tag")
        isint(tag) || throw(InvalidInput("tag not an integer"))
        if tag == 1
            push!(nodes, Node(0x01; atom=parse_atom(req(r, "atom"), n)))
        elseif tag == 2
            push!(nodes, Node(0x02; lit=parse_hex(req(r, "literal_hex"))))
        elseif tag == 3 || tag == 4
            ch = req(r, "children")
            ch isa Vector || throw(InvalidInput("children not an array"))
            push!(nodes, Node(UInt8(tag); kids=Int[ref(c) for c in ch]))
        elseif tag == 5
            ps = req(r, "pairs")
            ps isa Vector || throw(InvalidInput("pairs not an array"))
            kids = Int[]; ms = BigInt[]
            for p in ps
                p isa Dict || throw(InvalidInput("pair not an object"))
                push!(kids, ref(req(p, "child")))
                push!(ms, parse_mult(req(p, "multiplicity")))
            end
            push!(nodes, Node(0x05; kids=kids, mults=ms))
        elseif tag == 6
            push!(nodes, Node(0x06; perm=parse_perm(req(r, "perm"), n)))
        elseif tag == 7
            push!(nodes, Node(0x07; gens=parse_gens(req(r, "group_generators"), n)))
        elseif tag == 8
            push!(nodes, Node(0x08; gens=parse_gens(req(r, "group_generators"), n), r=parse_perm(req(r, "r"), n)))
        elseif tag == 9
            push!(nodes, Node(0x09; graph=parse_graph(r, n)))
        elseif tag == 10
            push!(nodes, Node(0x0a; rels=parse_relations(req(r, "relations"), n)))
        else
            throw(InvalidInput("unknown record tag"))   # spec §4.1: unknown type tags are invalid
        end
    end
    root = ref(req(o, "root"))
    check_acyclic_all(nodes)
    return make_dag(n, nodes, root)
end

# spec §4.2 acyclicity, checked over every supplied record (see QUESTIONS.md Q4)
function check_acyclic_all(nodes::Vector{Node})
    N = length(nodes)
    state = zeros(Int8, N)
    for s in 1:N
        state[s] != 0 && continue
        stack = [(s, 1)]
        state[s] = 1
        while !isempty(stack)
            i, k = stack[end]
            kv = nodes[i].tag in (0x03, 0x04, 0x05) ? nodes[i].kids : Int[]
            if k <= length(kv)
                stack[end] = (i, k + 1)
                c = kv[k]
                state[c] == 1 && throw(InvalidInput("cyclic DAG"))
                if state[c] == 0
                    state[c] = 1
                    push!(stack, (c, 1))
                end
            else
                state[i] = 2
                pop!(stack)
            end
        end
    end
end

function parse_object(o, n::Int)
    o isa Dict || throw(InvalidInput("object is not a JSON object"))
    kind = req(o, "kind")
    if kind == "subset"
        at = req(o, "atoms")
        at isa Vector || throw(InvalidInput("atoms not an array"))
        return make_subset(n, Int[parse_atom(a, n) for a in at])
    elseif kind == "tuple"
        pts = req(o, "points")
        pts isa Vector || throw(InvalidInput("points not an array"))
        return make_tuple(n, Int[parse_atom(a, n) for a in pts])
    elseif kind == "graph"
        return make_graph(n, parse_graph(o, n))
    elseif kind == "dag"
        return parse_dag(o, n)
    end
    throw(InvalidInput("unknown object kind"))
end
