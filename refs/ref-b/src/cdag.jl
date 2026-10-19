# Objects, action ATOM-TRANSPORT-1 (spec §2.1), CDAG-2 stream (spec §4.1) and
# extensional normalisation (spec §4.2).

struct InvalidInput <: Exception
    msg::String
end
struct Unsupported <: Exception
    msg::String
end

# ---------------------------------------------------------------- graphs (tag 09)
struct Arc
    s::Int
    t::Int
    label::Vector{UInt8}
    m::BigInt
end

struct GraphData
    colours::Vector{Vector{UInt8}}   # colours[v+1] = vertex colour of v
    arcs::Vector{Arc}                # normalised: combined and sorted
end

# spec §4.1: labels compare by (byte length, unsigned bytes), i.e. by B(label)
blabel_less(a::Vector{UInt8}, b::Vector{UInt8}) = length(a) != length(b) ? length(a) < length(b) : isless(a, b)
arc_key_less(a::Arc, b::Arc) = a.s != b.s ? a.s < b.s : a.t != b.t ? a.t < b.t : blabel_less(a.label, b.label)

# spec §4.1: combine duplicate (source,target,label) arcs by exact addition, sort by
# (source, target, B(label)); input zero multiplicities are invalid (checked on import).
function normalise_arcs(arcs::Vector{Arc})
    a = sort(arcs; lt=arc_key_less)
    out = Arc[]
    for x in a
        if !isempty(out) && out[end].s == x.s && out[end].t == x.t && out[end].label == x.label
            out[end] = Arc(x.s, x.t, x.label, out[end].m + x.m)
        else
            push!(out, x)
        end
    end
    return out
end

function graph_record!(io::Vector{UInt8}, g::GraphData)
    u8!(io, 0x09)
    for c in g.colours
        bstr!(io, c)
    end
    u32!(io, length(g.arcs))
    for a in g.arcs
        u32!(io, a.s)
        u32!(io, a.t)
        bstr!(io, a.label)
        nat!(io, a.m)
    end
    return io
end

# action: vertex v goes to g[v] with its colour; arc (s,t) -> (g[s], g[t])
function act_graph(gd::GraphData, g::Perm)
    n = length(gd.colours)
    cols = Vector{Vector{UInt8}}(undef, n)
    for v in 0:n-1
        cols[g[v+1]+1] = gd.colours[v+1]
    end
    arcs = Arc[Arc(g[a.s+1], g[a.t+1], a.label, a.m) for a in gd.arcs]
    return GraphData(cols, normalise_arcs(arcs))
end

# ---------------------------------------------------------------- relations (tag 0a)
struct Relation
    name::Vector{UInt8}
    arity::Int
    tuples::Vector{Tuple{Vector{Int},BigInt}}   # normalised: combined, sorted
end

function normalise_relation(name, arity, tuples)
    t = sort(tuples; by=first)
    out = Tuple{Vector{Int},BigInt}[]
    for (a, m) in t
        if !isempty(out) && out[end][1] == a
            out[end] = (a, out[end][2] + m)
        else
            push!(out, (a, m))
        end
    end
    return Relation(name, arity, out)
end

function relations_record!(io::Vector{UInt8}, rels::Vector{Relation})
    u8!(io, 0x0a)
    u32!(io, length(rels))
    for r in rels
        bstr!(io, r.name)
        u32!(io, r.arity)
        u32!(io, length(r.tuples))
        for (a, m) in r.tuples
            for x in a
                u32!(io, x)
            end
            nat!(io, m)
        end
    end
    return io
end

function sort_relations(rels::Vector{Relation})
    r = sort(rels; lt=(a, b) -> blabel_less(a.name, b.name))
    for i in 2:length(r)
        r[i].name == r[i-1].name && throw(InvalidInput("duplicate relation name"))
    end
    return r
end

act_relations(rels::Vector{Relation}, g::Perm) =
    sort_relations([normalise_relation(r.name, r.arity, [(Int[g[x+1] for x in a], m) for (a, m) in r.tuples]) for r in rels])

# ---------------------------------------------------------------- DAG nodes
struct Node
    tag::UInt8
    atom::Int
    lit::Vector{UInt8}
    kids::Vector{Int}          # 1-based indices into the node list
    mults::Vector{BigInt}      # multiset counts, parallel to kids
    perm::Perm                 # tag 06
    gens::Vector{Perm}         # tags 07, 08
    r::Perm                    # tag 08 (any coset element; canonical nodes hold r0)
    graph::Union{Nothing,GraphData}
    rels::Vector{Relation}
end

const NOPERM = Int[]
Node(tag::UInt8; atom=-1, lit=UInt8[], kids=Int[], mults=BigInt[], perm=NOPERM,
     gens=Perm[], r=NOPERM, graph=nothing, rels=Relation[]) =
    Node(tag, atom, lit, kids, mults, perm, gens, r, graph, rels)

haskids(nd::Node) = nd.tag in (0x03, 0x04, 0x05) && !isempty(nd.kids)

# record bytes of an input node whose children have canonical 0-based indices cidx
function node_record(n::Int, nd::Node, cidx::Vector{Int})
    io = UInt8[]
    t = nd.tag
    if t == 0x01
        u8!(io, 0x01); u32!(io, nd.atom)
        return io, nd
    elseif t == 0x02
        u8!(io, 0x02); bstr!(io, nd.lit)
        return io, nd
    elseif t == 0x03
        ks = Int[cidx[k] for k in nd.kids]
        u8!(io, 0x03); u32!(io, length(ks))
        for k in ks; u32!(io, k); end
        return io, Node(0x03; kids=ks)
    elseif t == 0x04
        # spec §4.2: sets deduplicate equal children; §4.1 increasing distinct refs
        ks = sort(unique(Int[cidx[k] for k in nd.kids]))
        u8!(io, 0x04); u32!(io, length(ks))
        for k in ks; u32!(io, k); end
        return io, Node(0x04; kids=ks)
    elseif t == 0x05
        # spec §4.2: multisets combine equal children and add positive counts
        d = Dict{Int,BigInt}()
        for (k, m) in zip(nd.kids, nd.mults)
            c = cidx[k]
            d[c] = get(d, c, big(0)) + m
        end
        ks = sort(collect(keys(d)))
        ms = BigInt[d[k] for k in ks]
        u8!(io, 0x05); u32!(io, length(ks))
        for (k, m) in zip(ks, ms); u32!(io, k); nat!(io, m); end
        return io, Node(0x05; kids=ks, mults=ms)
    elseif t == 0x06
        u8!(io, 0x06); perm_bytes!(io, nd.perm)
        return io, nd
    elseif t == 0x07
        u8!(io, 0x07); group_bytes!(io, n, nd.gens)
        return io, nd
    elseif t == 0x08
        # spec §4.1 / §9.4: Group(H), Perm(r0), r0 the least element of H r
        B = full_chain(n, nd.gens)
        r0 = least_coset(B, nd.r)
        u8!(io, 0x08); group_bytes!(io, B); perm_bytes!(io, r0)
        return io, Node(0x08; gens=nd.gens, r=r0)
    elseif t == 0x09
        graph_record!(io, nd.graph)
        return io, nd
    elseif t == 0x0a
        relations_record!(io, nd.rels)
        return io, nd
    end
    throw(InvalidInput("unknown tag"))
end

# spec §4.2: validate acyclicity, discard unreachable nodes, number by increasing
# height, within a height sort exact record bytes; equal records are one node.
# Returns canonical nodes (children are 1-based canonical indices) and their records.
function normalise(n::Int, nodes::Vector{Node}, root::Int)
    N = length(nodes)
    (1 <= root <= N) || throw(InvalidInput("root out of range"))
    state = zeros(Int8, N)
    height = zeros(Int, N)
    post = Int[]
    stack = [(root, 1)]
    state[root] = 1
    while !isempty(stack)
        i, k = stack[end]
        nd = nodes[i]
        kidsv = nd.tag in (0x03, 0x04, 0x05) ? nd.kids : Int[]
        if k <= length(kidsv)
            stack[end] = (i, k + 1)
            c = kidsv[k]
            (1 <= c <= N) || throw(InvalidInput("child reference out of range"))
            if state[c] == 1
                throw(InvalidInput("cyclic DAG"))
            elseif state[c] == 0
                state[c] = 1
                push!(stack, (c, 1))
            end
        else
            pop!(stack)
            state[i] = 2
            height[i] = isempty(kidsv) ? 0 : 1 + maximum(height[c] for c in kidsv)
            push!(post, i)
        end
    end
    maxh = height[root]
    byh = [Int[] for _ in 0:maxh]
    for i in post
        push!(byh[height[i]+1], i)
    end
    cidx = zeros(Int, N)            # 0-based canonical index per input node
    cnodes = Node[]
    recs = Vector{UInt8}[]
    for h in 0:maxh
        keyof = Dict{Int,Vector{UInt8}}()
        uniq = Dict{Vector{UInt8},Node}()
        for i in byh[h+1]
            b, cn = node_record(n, nodes[i], cidx)
            keyof[i] = b
            uniq[b] = cn
        end
        ks = sort(collect(keys(uniq)))
        base = length(recs)
        pos = Dict{Vector{UInt8},Int}()
        for (j, k) in enumerate(ks)
            push!(recs, k)
            cn = uniq[k]
            # convert canonical children to 1-based
            if cn.tag in (0x03, 0x04, 0x05)
                cn = Node(cn.tag; kids=cn.kids .+ 1, mults=cn.mults)
            end
            push!(cnodes, cn)
            pos[k] = base + j - 1
        end
        for i in byh[h+1]
            cidx[i] = pos[keyof[i]]
        end
    end
    @assert cidx[root] == length(recs) - 1
    return cnodes, recs
end

# ---------------------------------------------------------------- objects
# kind :subset (atoms sorted distinct), :tuple (points), :graph, :dag (general).
# The kind is derived from the normalised value, not from the input syntax, so
# that every stage factors through decode (spec §5).
struct Obj
    n::Int
    kind::Symbol
    atoms::Vector{Int}
    graph::Union{Nothing,GraphData}
    nodes::Vector{Node}
    stream::Vector{UInt8}
end

function header!(io::Vector{UInt8}, n::Int)
    append!(io, (0x43, 0x4e, 0x02))
    u16!(io, 1)     # schema EXT-DAG-1
    u16!(io, 1)     # action ATOM-TRANSPORT-1
    u32!(io, n)
end

function subset_stream(n::Int, atoms::Vector{Int})
    io = UInt8[]
    header!(io, n)
    k = length(atoms)
    u32!(io, k + 1)
    for a in atoms
        u8!(io, 0x01); u32!(io, a)
    end
    u8!(io, 0x04); u32!(io, k)
    for j in 0:k-1; u32!(io, j); end
    u32!(io, k)
    return io
end

function tuple_stream(n::Int, pts::Vector{Int})
    io = UInt8[]
    header!(io, n)
    d = sort(unique(pts))
    pos = Dict{Int,Int}(a => j - 1 for (j, a) in enumerate(d))
    u32!(io, length(d) + 1)
    for a in d
        u8!(io, 0x01); u32!(io, a)
    end
    u8!(io, 0x03); u32!(io, length(pts))
    for p in pts; u32!(io, pos[p]); end
    u32!(io, length(d))
    return io
end

function graph_stream(n::Int, g::GraphData)
    io = UInt8[]
    header!(io, n)
    u32!(io, 1)
    graph_record!(io, g)
    u32!(io, 0)
    return io
end

function dag_stream(n::Int, recs::Vector{Vector{UInt8}})
    io = UInt8[]
    header!(io, n)
    u32!(io, length(recs))
    for r in recs; append!(io, r); end
    u32!(io, length(recs) - 1)
    return io
end

make_subset(n::Int, atoms::Vector{Int}) = (a = sort(unique(atoms)); Obj(n, :subset, a, nothing, Node[], subset_stream(n, a)))
make_tuple(n::Int, pts::Vector{Int}) = Obj(n, :tuple, pts, nothing, Node[], tuple_stream(n, pts))
make_graph(n::Int, g::GraphData) = Obj(n, :graph, Int[], g, Node[], graph_stream(n, g))

# Build an object from an arbitrary (unnormalised) node list; classify the result.
function make_dag(n::Int, nodes::Vector{Node}, root::Int)
    cnodes, recs = normalise(n, nodes, root)
    q = length(cnodes)
    rt = cnodes[q]
    allatoms = all(cnodes[i].tag == 0x01 for i in 1:q-1)
    if rt.tag == 0x04 && allatoms
        # a set consisting only of atom nodes: a subset (spec §2.1, §7.1)
        return make_subset(n, Int[cnodes[k].atom for k in rt.kids])
    elseif rt.tag == 0x03 && allatoms
        return make_tuple(n, Int[cnodes[k].atom for k in rt.kids])
    elseif rt.tag == 0x09 && q == 1
        return make_graph(n, rt.graph)
    end
    return Obj(n, :dag, Int[], nothing, cnodes, dag_stream(n, recs))
end

# ---------------------------------------------------------------- action (spec §2.1)
# ATOM-TRANSPORT-1: atom a -> g[a]; literals fixed; tuple positions and
# multiplicities preserved; permutation p -> g^-1 p g; subgroup H -> g^-1 H g;
# labeling coset H r -> g^-1 H r = (g^-1 H g)(g^-1 r); graph vertices transported,
# colours and labels fixed.
function act_node(nd::Node, g::Perm)
    t = nd.tag
    if t == 0x01
        return Node(0x01; atom=g[nd.atom+1])
    elseif t == 0x06
        return Node(0x06; perm=pconj(nd.perm, g))
    elseif t == 0x07
        return Node(0x07; gens=Perm[pconj(h, g) for h in nd.gens])
    elseif t == 0x08
        return Node(0x08; gens=Perm[pconj(h, g) for h in nd.gens], r=pmul(pinv(g), nd.r))
    elseif t == 0x09
        return Node(0x09; graph=act_graph(nd.graph, g))
    elseif t == 0x0a
        return Node(0x0a; rels=act_relations(nd.rels, g))
    end
    return nd   # literal, tuple, set, multiset: structure kept, children transported
end

function act(x::Obj, g::Perm)
    n = x.n
    if x.kind == :subset
        return make_subset(n, Int[g[a+1] for a in x.atoms])
    elseif x.kind == :tuple
        return make_tuple(n, Int[g[a+1] for a in x.atoms])
    elseif x.kind == :graph
        return make_graph(n, act_graph(x.graph, g))
    end
    nodes = Node[act_node(nd, g) for nd in x.nodes]
    return make_dag(n, nodes, length(nodes))
end

# fast check x^g == x for the common kinds
function fixes(x::Obj, g::Perm)
    if x.kind == :subset
        for a in x.atoms
            insorted(g[a+1], x.atoms) || return false
        end
        return true
    elseif x.kind == :tuple
        for a in x.atoms
            g[a+1] == a || return false
        end
        return true
    end
    return act(x, g).stream == x.stream
end

hexstr(b::Vector{UInt8}) = bytes2hex(b)
