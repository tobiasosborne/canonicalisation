# Profile P1 (spec §7.1 scalar rules, §7.2 trace bytes, leaf map, least witness).

# ---------------------------------------------------------------- stabiliser trie
# GNode holds a group H (generators), its orbits (ordered by least point) and a
# Schreier tree rooted at each orbit's least point; children[b] = H_b.
mutable struct GNode
    n::Int
    gens::Vector{Perm}
    ginv::Vector{Perm}
    trivial::Bool
    orbmin::Vector{Int}
    orbpos::Vector{Int}
    parent::Vector{Int}
    pgen::Vector{Int}
    children::Dict{Int,GNode}
end

function GNode(n::Int, gens0::Vector{Perm})
    gens = Perm[g for g in gens0 if !isid(g)]
    orbmin = fill(-1, n)
    orbpos = zeros(Int, n)
    parent = fill(-1, n)
    pgen = zeros(Int, n)
    rank = 0
    for v in 0:n-1
        orbmin[v+1] >= 0 && continue
        rank += 1
        orbmin[v+1] = v
        orbpos[v+1] = rank
        q = [v]
        h = 1
        while h <= length(q)
            γ = q[h]; h += 1
            for (k, g) in enumerate(gens)
                β = g[γ+1]
                if orbmin[β+1] < 0
                    orbmin[β+1] = v
                    orbpos[β+1] = rank
                    parent[β+1] = γ
                    pgen[β+1] = k
                    push!(q, β)
                end
            end
        end
    end
    return GNode(n, gens, Perm[pinv(g) for g in gens], isempty(gens), orbmin, orbpos, parent, pgen, Dict{Int,GNode}())
end

function child(H::GNode, b::Int)
    c = get(H.children, b, nothing)
    c === nothing || return c
    c = GNode(H.n, H.trivial ? Perm[] : stabiliser_gens(H.n, H.gens, b))
    H.children[b] = c
    return c
end

# u in H with a^u = orbit minimum of a
function to_root(H::GNode, a::Int)
    u = pid(H.n)
    x = a
    while H.parent[x+1] >= 0
        u = pmul(u, H.ginv[H.pgen[x+1]])
        x = H.parent[x+1]
    end
    return u
end

# spec §7.2 constructive procedure: start t=id, H=G; at list position i set
# a=t[L[i]], choose b=min(a^H) and u in H with a^u=b, then t <- t u, H <- H_b.
# Returns (M = L^t, t, H = G_M).
function lexmin_tuple(G::GNode, L::Vector{Int})
    t = pid(G.n)
    H = G
    M = Vector{Int}(undef, length(L))
    for (i, f) in enumerate(L)
        a = t[f+1]
        if H.trivial
            M[i] = a
            continue
        end
        b = H.orbmin[a+1]
        if b != a
            t = pmul(t, to_root(H, a))
        end
        M[i] = b
        H = child(H, b)
    end
    return M, t, H
end

# ---------------------------------------------------------------- partitions
const Partition = Vector{Vector{Int}}

# spec §7.1 split(P, sig): each old cell, in its old position, is replaced by its
# nonempty signature classes in increasing lexicographic signature order.
function split_cells(P::Partition, sig::Function)
    out = Partition()
    for C in P
        if length(C) == 1
            push!(out, C)
            continue
        end
        ss = [(sig(v), v) for v in C]
        sort!(ss; by=first)
        cur = Int[ss[1][2]]
        for j in 2:length(ss)
            if ss[j][1] == ss[j-1][1]
                push!(cur, ss[j][2])
            else
                push!(out, sort!(cur))
                cur = Int[ss[j][2]]
            end
        end
        push!(out, sort!(cur))
    end
    return out
end

function token_sizes!(buf::Vector{UInt8}, tag::UInt8, P::Partition)
    push!(buf, tag)
    u32!(buf, length(P))
    for C in P
        u32!(buf, length(C))
    end
end

# ---------------------------------------------------------------- graph O stage data
struct GraphAdj
    nlabels::Int
    out::Vector{Vector{Tuple{Int,Int,Int}}}   # out[v+1] = (label index, target, mult)
    inn::Vector{Vector{Tuple{Int,Int,Int}}}   # inn[v+1] = (label index, source, mult)
end

function GraphAdj(n::Int, g::GraphData)
    labels = sort(unique([a.label for a in g.arcs]); lt=blabel_less)   # sorted by B(label)
    lid = Dict(l => i for (i, l) in enumerate(labels))
    out = [Tuple{Int,Int,Int}[] for _ in 1:n]
    inn = [Tuple{Int,Int,Int}[] for _ in 1:n]
    for a in g.arcs
        a.m <= typemax(Int) ÷ (4 * max(n, 1) * max(length(g.arcs), 1)) || throw(CapacityError("multiplicity too large for this reference"))
        m = Int(a.m)
        push!(out[a.s+1], (lid[a.label], a.t, m))   # a loop counts once in each direction
        push!(inn[a.t+1], (lid[a.label], a.s, m))
    end
    return GraphAdj(length(labels), out, inn)
end

# spec §7.1 O stage for a graph: for arc labels sorted by B(label), then cell index j:
# outgoing multiplicity from v into C[j], incoming multiplicity from C[j] into v.
function o_stage(P::Partition, adj::GraphAdj, n::Int)
    k = length(P)
    cell = zeros(Int, n)
    for (j, C) in enumerate(P), v in C
        cell[v+1] = j
    end
    L = adj.nlabels
    sigf = function (v)
        s = zeros(Int, 2 * L * k)
        for (l, w, m) in adj.out[v+1]
            s[((l-1)*k+(cell[w+1]-1))*2+1] += m
        end
        for (l, w, m) in adj.inn[v+1]
            s[((l-1)*k+(cell[w+1]-1))*2+2] += m
        end
        s
    end
    return split_cells(P, sigf)
end

# spec §7.1 G stage: F = singleton atoms in partition order, M = lexmin F^G,
# u with F^u = M, sig(v) = position of the G_M orbit (orbits ordered by sorted
# label lists) containing u[v].
function g_stage(P::Partition, G::GNode)
    F = Int[C[1] for C in P if length(C) == 1]
    _, u, H = lexmin_tuple(G, F)
    return split_cells(P, v -> H.orbpos[u[v+1]+1])
end

function initial_partition(x::Obj)
    n = x.n
    n == 0 && return Partition()
    if x.kind == :subset
        # membership 0/1; cells ordered by increasing key
        inn = Int[a for a in x.atoms]
        out = Int[v for v in 0:n-1 if !insorted(v, x.atoms)]
        return Partition(filter(!isempty, [out, inn]))
    elseif x.kind == :graph
        # key B(vertex_colour[a]); cells by increasing key (B order: length, then bytes)
        keys = [x.graph.colours[v+1] for v in 0:n-1]
        ks = sort(unique(keys); lt=blabel_less)
        return Partition([sort([v for v in 0:n-1 if keys[v+1] == k]) for k in ks])
    end
    return Partition([collect(0:n-1)])      # every other root: the empty key
end

# ---------------------------------------------------------------- P1 search
mutable struct P1State
    x::Obj
    G::GNode
    adj::Union{Nothing,GraphAdj}
    bud::Union{Budget,Nothing}
    single_path::Bool          # singleton-orbit route: all children equivalent (A = G)
    buf::Vector{UInt8}
    best_trace::Union{Nothing,Vector{UInt8}}
    best_stream::Vector{UInt8}
    best_t::Perm
    nodes::Int
end

function memcmp_less(a::Vector{UInt8}, b::Vector{UInt8})
    la, lb = length(a), length(b)
    c = ccall(:memcmp, Cint, (Ptr{UInt8}, Ptr{UInt8}, Csize_t), a, b, min(la, lb))
    return c < 0 || (c == 0 && la < lb)
end
function memcmp_eq(a::Vector{UInt8}, b::Vector{UInt8})
    length(a) == length(b) || return false
    return ccall(:memcmp, Cint, (Ptr{UInt8}, Ptr{UInt8}, Csize_t), a, b, length(a)) == 0
end

function p1_node!(S::P1State, P::Partition, depth::Int)
    # spec §11.1: one unit per NODE token; the §7.3 root shortcuts (discrete root,
    # singleton orbit) "visit no tree nodes", so the root is charged only once it
    # branches (QUESTIONS.md Q3).
    depth > 0 && S.bud !== nothing && charge!(S.bud)
    S.nodes += 1
    buf = S.buf
    mark = length(buf)
    push!(buf, 0x10); u32!(buf, depth)        # NODE(d)
    n = S.x.n
    while true
        c0 = length(P)
        if S.adj !== nothing
            P = o_stage(P, S.adj, n)
        end
        token_sizes!(buf, 0x20, P)            # STAGE_O
        P = g_stage(P, S.G)
        token_sizes!(buf, 0x21, P)            # STAGE_G
        length(P) == c0 && break
    end
    if all(C -> length(C) == 1, P)
        push!(buf, 0x00)                      # LEAF
        leaf!(S, P)
    else
        # target: cell minimising (size, position) among size > 1
        ti = 0
        for (i, C) in enumerate(P)
            if length(C) > 1 && (ti == 0 || length(C) < length(P[ti]))
                ti = i
            end
        end
        C = P[ti]
        depth == 0 && S.bud !== nothing && charge!(S.bud)   # non-discrete root
        for a in C
            Q = Partition(undef, length(P) + 1)
            for i in 1:ti-1; Q[i] = P[i]; end
            Q[ti] = [a]
            Q[ti+1] = [v for v in C if v != a]
            for i in ti+1:length(P); Q[i+1] = P[i]; end
            p1_node!(S, Q, depth + 1)
            S.single_path && break
        end
    end
    resize!(buf, mark)
end

# spec §7.2 leaf: L from the singleton order; t_L the unique element minimising L^G;
# objective is the pair (complete trace, CDAG-2 bytes of x^t_L), trace first.
function leaf!(S::P1State, P::Partition)
    buf = S.buf
    if S.best_trace !== nothing
        bt = S.best_trace
        if memcmp_less(bt, buf)
            return
        end
        eq = memcmp_eq(bt, buf)
    else
        eq = false
    end
    L = Int[C[1] for C in P]
    _, t, _ = lexmin_tuple(S.G, L)
    img = act(S.x, t)
    if S.best_trace === nothing || !eq || memcmp_less(img.stream, S.best_stream)
        S.best_trace = copy(buf)
        S.best_stream = img.stream
        S.best_t = t
    end
end

struct P1Result
    trace::Vector{UInt8}
    stream::Vector{UInt8}
    t::Perm
    nodes::Int
end

# Run the unpruned P1 tree on x under G (GNode). If single_path, the caller has
# proved every generator fixes x (A = G); then all children of a node are
# G_F-equivalent automorphic images, every leaf has the same trace and image,
# and the first path gives the unpruned result (used only on the §7.3
# singleton-orbit route, which §11.1 counts as visiting no tree nodes).
function p1_run(x::Obj, G::GNode, bud::Union{Budget,Nothing}; single_path::Bool=false)
    adj = x.kind == :graph ? GraphAdj(x.n, x.graph) : nothing
    S = P1State(x, G, adj, bud, single_path, UInt8[], nothing, UInt8[], Int[], 0)
    p1_node!(S, initial_partition(x), 0)
    return P1Result(S.best_trace, S.best_stream, S.best_t, S.nodes)
end
