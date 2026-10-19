# Group kernel for ref-b: a deterministic Schreier-Sims of my own writing
# (BRIEF §2: "a simple Schreier-Sims of your own writing"), orbits, point
# stabilisers, least elements of cosets, the §8.1 disjoint coset enumerator
# and the §9.4 canonical Group(H) bytes. Convention: spec §3, perm.jl.

# ---------------------------------------------------------------- byte helpers
u8!(io::Vector{UInt8}, x::Integer) = push!(io, UInt8(x))
function u16!(io::Vector{UInt8}, x::Integer)
    0 <= x <= 0xffff || throw(CapacityError("U16 overflow"))
    push!(io, UInt8((x >> 8) & 0xff), UInt8(x & 0xff))
end
# spec §4.1: U32 exactly 4 bytes big endian; overflow is a capacity error
function u32!(io::Vector{UInt8}, x::Integer)
    0 <= x <= 0xffffffff || throw(CapacityError("U32 overflow"))
    push!(io, UInt8((x >> 24) & 0xff), UInt8((x >> 16) & 0xff), UInt8((x >> 8) & 0xff), UInt8(x & 0xff))
end
# spec §4.1: Nat(k) = U32(b) || big_endian_bytes(k, b), shortest b, b=0 for k=0
function nat!(io::Vector{UInt8}, k::Integer)
    k >= 0 || throw(ArgumentError("negative Nat"))
    bytes = UInt8[]
    kk = big(k)
    while kk > 0
        pushfirst!(bytes, UInt8(kk & 0xff))
        kk >>= 8
    end
    u32!(io, length(bytes))
    append!(io, bytes)
end
# spec §4.1: B(s) = U32(len(s)) || s
function bstr!(io::Vector{UInt8}, s::AbstractVector{UInt8})
    u32!(io, length(s))
    append!(io, s)
end

struct CapacityError <: Exception
    msg::String
end

# ---------------------------------------------------------------- BSGS
mutable struct BSGS
    n::Int
    base::Vector{Int}                       # 0-based base points
    sgs::Vector{Perm}                       # strong generating set (non-identity)
    lgens::Vector{Vector{Int}}              # level i: indices of sgs fixing base[1..i-1]
    orbit::Vector{Vector{Int}}              # level i orbit of base[i], discovery order
    trans::Vector{Vector{Union{Nothing,Perm}}}  # level i: trans[i][β+1] = u with base[i]^u = β
end

function fixes_prefix(g::Perm, base::Vector{Int}, upto::Int)
    @inbounds for j in 1:upto
        g[base[j]+1] == base[j] || return false
    end
    return true
end

function first_moved(g::Perm)
    for i in eachindex(g)
        g[i] != i - 1 && return i - 1
    end
    return -1
end

function compute_level!(B::BSGS, i::Int)
    n = B.n
    B.lgens[i] = [k for k in eachindex(B.sgs) if fixes_prefix(B.sgs[k], B.base, i - 1)]
    b = B.base[i]
    tr = Vector{Union{Nothing,Perm}}(nothing, n)
    tr[b+1] = pid(n)
    orb = [b]
    q = 1
    while q <= length(orb)
        γ = orb[q]
        q += 1
        uγ = tr[γ+1]::Perm
        for k in B.lgens[i]
            s = B.sgs[k]
            β = s[γ+1]
            if tr[β+1] === nothing
                tr[β+1] = pmul(uγ, s)   # base^(uγ s) = γ^s = β
                push!(orb, β)
            end
        end
    end
    B.orbit[i] = orb
    B.trans[i] = tr
end

# sift g from level l: returns (residue, level where it dropped out or k+1)
function strip(B::BSGS, g::Perm, l::Int)
    k = length(B.base)
    h = g
    for i in l:k
        β = h[B.base[i]+1]
        u = B.trans[i][β+1]
        u === nothing && return (h, i)
        h = pmul(h, pinv(u))
    end
    return (h, k + 1)
end

# Build a BSGS for <gens> with the given base prefix (extended as needed).
function build_bsgs(n::Int, gens::Vector{Perm}, prefix::Vector{Int}=Int[])
    S = Perm[]
    for g in gens
        isid(g) && continue
        any(==(g), S) && continue
        push!(S, copy(g))
    end
    base = copy(prefix)
    for s in S
        if fixes_prefix(s, base, length(base))
            push!(base, first_moved(s))
        end
    end
    k = length(base)
    B = BSGS(n, base, S, [Int[] for _ in 1:k], [Int[] for _ in 1:k],
             [Union{Nothing,Perm}[] for _ in 1:k])
    for i in 1:k
        compute_level!(B, i)
    end
    i = k
    while i >= 1
        added = false
        orb = copy(B.orbit[i])
        gl = copy(B.lgens[i])
        for β in orb
            uβ = B.trans[i][β+1]::Perm
            for kk in gl
                s = B.sgs[kk]
                βs = s[β+1]
                g = pmul(pmul(uβ, s), pinv(B.trans[i][βs+1]::Perm))
                isid(g) && continue
                h, j = strip(B, g, i + 1)
                if j <= length(B.base) || !isid(h)
                    if j > length(B.base)
                        push!(B.base, first_moved(h))
                        push!(B.lgens, Int[])
                        push!(B.orbit, Int[])
                        push!(B.trans, Union{Nothing,Perm}[])
                    end
                    push!(B.sgs, h)
                    for l in 1:j
                        compute_level!(B, l)
                    end
                    i = j
                    added = true
                    break
                end
            end
            added && break
        end
        added || (i -= 1)
    end
    return B
end

# full chain on base 0,1,...,n-1 (every point is a base point, some levels trivial)
full_chain(n::Int, gens::Vector{Perm}) = build_bsgs(n, gens, collect(0:n-1))

group_order(B::BSGS) = prod(BigInt[length(o) for o in B.orbit]; init=big(1))

function contains(B::BSGS, g::Perm)
    h, j = strip(B, g, 1)
    return j == length(B.base) + 1 && isid(h)
end

# generators of the subgroup B^(l) (fixing base[1..l-1]) by the strong generating property
level_gens(B::BSGS, l::Int) = l > length(B.base) ? Perm[] : Perm[B.sgs[k] for k in B.lgens[l]]

# point stabiliser generators H_b
function stabiliser_gens(n::Int, gens::Vector{Perm}, b::Int)
    B = build_bsgs(n, gens, [b])
    return level_gens(B, 2)
end

# orbits of <gens> on 0..n-1, each sorted, ordered by least point (spec §7.1, §9.4)
function orbits(n::Int, gens::Vector{Perm})
    lab = fill(-1, n)
    res = Vector{Vector{Int}}()
    for v in 0:n-1
        lab[v+1] >= 0 && continue
        id = length(res) + 1
        orb = [v]
        lab[v+1] = id
        q = 1
        while q <= length(orb)
            γ = orb[q]; q += 1
            for g in gens
                β = g[γ+1]
                if lab[β+1] < 0
                    lab[β+1] = id
                    push!(orb, β)
                end
            end
        end
        sort!(orb)
        push!(res, orb)
    end
    return res, lab
end

# Least image-array element of the coset B^(l) * w = {h w : h in B^(l)}, where B has
# the full base 0..n-1 (level l fixes 0..l-2). Point-by-point minimisation: at level v
# the elements h of B^(v) with v-1 ↦ β are B^(v+1) u_β, and (h w)[v-1] = w[β].
function least_coset_from(B::BSGS, l::Int, w::Perm)
    @assert B.base == collect(0:B.n-1)
    for v in l:length(B.base)
        orb = B.orbit[v]
        length(orb) == 1 && continue
        best = orb[1]
        for β in orb
            if w[β+1] < w[best+1]
                best = β
            end
        end
        if best != B.base[v]
            w = pmul(B.trans[v][best+1]::Perm, w)
        end
    end
    return w
end
least_coset(B::BSGS, w::Perm) = least_coset_from(B, 1, w)
least_element(B::BSGS) = least_coset(B, pid(B.n))

# ---------------------------------------------------------------- work quota
# spec §11.1 (v2.1/v2.2): one unit per P1 NODE token and one per §8.1 enumerator
# visit, one quota per solve. Exceeding the quota yields CAPACITY_LIMIT.
mutable struct Budget
    quota::Int64      # quotas beyond typemax(Int64) are clamped (never reachable)
    used::Int64
end
Budget(q::Integer) = Budget(q > typemax(Int64) ? typemax(Int64) : Int64(q), 0)
struct QuotaExceeded <: Exception end
@inline function charge!(bud::Budget, k::Integer=1)
    if k > bud.quota - bud.used
        bud.used = bud.quota       # exhausted
        throw(QuotaExceeded())
    end
    bud.used += Int64(k)
    return nothing
end

# ---------------------------------------------------------------- §8.1 enumerator
# visit(H, r): if H == {id}: consume(r); return
#   a = smallest atom moved by H; for b in sorted(a^H):
#     t_b = least image-array element of H with a^t_b = b; visit(H_a, t_b r)
# With the full base 0..n-1, the groups met are the chain levels, and the
# "smallest atom moved" by level group B^(l) is the base point of the first
# level l' >= l whose orbit is nontrivial.
struct Enumerator
    B::BSGS
    levels::Vector{Int}                 # nontrivial levels in increasing order
    tb::Vector{Vector{Perm}}            # per nontrivial level: t_b for sorted b
end

function Enumerator(B::BSGS)
    levels = [l for l in 1:length(B.base) if length(B.orbit[l]) > 1]
    tb = Vector{Vector{Perm}}()
    for l in levels
        bs = sort(B.orbit[l])
        # t_b = least element of {h in B^(l) : a^h = b} = B^(l+1) u_b
        push!(tb, [least_coset_from(B, l + 1, B.trans[l][b+1]::Perm) for b in bs])
    end
    return Enumerator(B, levels, tb)
end

# number of visit() calls of a complete enumeration: sum_k prod_{i<=k} r_i
function full_visits(E::Enumerator)
    tot = big(1)
    p = big(1)
    for l in eachindex(E.levels)
        p *= length(E.tb[l])
        tot += p
    end
    return tot
end

# Run the enumeration; consume(r) returns true to stop. Charges each visit to bud
# unless bud === nothing (pre-charged). Returns true if stopped by consume.
function enumerate_cosets(consume::Function, E::Enumerator, bud::Union{Budget,Nothing})
    n = E.B.n
    m = length(E.levels)
    # iterative DFS: depth d in 0..m, r[d+1] the region representative
    rs = Vector{Perm}(undef, m + 1)
    rs[1] = pid(n)
    idx = zeros(Int, m)
    bud === nothing || charge!(bud)          # visit(G, id)
    if m == 0
        return consume(rs[1])
    end
    d = 1
    idx[1] = 0
    while d >= 1
        idx[d] += 1
        if idx[d] > length(E.tb[d])
            d -= 1
            continue
        end
        r = pmul(E.tb[d][idx[d]], rs[d])     # visit(H_a, t_b r)
        bud === nothing || charge!(bud)
        if d == m
            consume(r) && return true
        else
            rs[d+1] = r
            d += 1
            idx[d] = 0
        end
    end
    return false
end

# ---------------------------------------------------------------- §9.4 Group(H)
# Perm(p) = U32(s) then s pairs U32(i),U32(p[i]) in increasing i over the moved support
function perm_bytes!(io::Vector{UInt8}, p::Perm)
    mv = [i - 1 for i in eachindex(p) if p[i] != i - 1]
    u32!(io, length(mv))
    for i in mv
        u32!(io, i)
        u32!(io, p[i+1])
    end
    return io
end

function group_bytes!(io::Vector{UInt8}, n::Int, gens::Vector{Perm})
    B = full_chain(n, gens)
    group_bytes!(io, B)
end

function group_bytes!(io::Vector{UInt8}, B::BSGS)
    n = B.n
    gens = B.sgs
    orbs, _ = orbits(n, gens)
    ordH = group_order(B)
    prodfact = prod(BigInt[factorial(big(length(o))) for o in orbs]; init=big(1))
    if ordH == prodfact
        # rule 1: 01 || U32(k) || non-singleton orbits as U32(size), U32(points...)
        blocks = [o for o in orbs if length(o) > 1]
        u8!(io, 0x01)
        u32!(io, length(blocks))
        for o in blocks
            u32!(io, length(o))
            for p in o
                u32!(io, p)
            end
        end
        return io
    end
    # rule 2: 00 || U32(k) || Perm(g_1)...Perm(g_k); greedy least g in H \ K
    seq = Perm[]
    K = full_chain(n, Perm[])
    while group_order(K) != ordH
        g = least_outside(B, K)
        push!(seq, g)
        K = full_chain(n, seq)
    end
    u8!(io, 0x00)
    u32!(io, length(seq))
    for g in seq
        perm_bytes!(io, g)
    end
    return io
end

function subgroup_of(B::BSGS, l::Int, K::BSGS)
    for g in level_gens(B, l)
        contains(K, g) || return false
    end
    return true
end

# least image-array element of H \ K (spec §9.4: descend in point-image order
# through constrained cosets C = J r, discarding C ⊆ K iff r in K and J <= K)
function least_outside(B::BSGS, K::BSGS)
    n = B.n
    nl = length(B.base)
    # L = least level with B^(L) <= K  (B^(nl+1) is trivial)
    L = nl + 1
    for l in nl:-1:1
        if subgroup_of(B, l, K)
            L = l
        else
            break
        end
    end
    @assert L > 1 "H <= K"
    w = pid(n)
    # levels 1..L-2: every child coset has J ⊄ K, so take the least image
    for v in 1:L-2
        orb = B.orbit[v]
        best = orb[1]
        for β in orb
            w[β+1] < w[best+1] && (best = β)
        end
        best != B.base[v] && (w = pmul(B.trans[v][best+1]::Perm, w))
    end
    # level L-1: children are B^(L) u_β w with B^(L) <= K; need u_β w ∉ K
    v = L - 1
    cands = sort(B.orbit[v]; by=β -> w[β+1])
    chosen = nothing
    for β in cands
        w2 = β == B.base[v] ? w : pmul(B.trans[v][β+1]::Perm, w)
        if !contains(K, w2)
            chosen = w2
            break
        end
    end
    chosen === nothing && error("least_outside: no coset outside K")
    # every element of B^(L) w2 is outside K; take its least element
    return least_coset_from(B, L, chosen)
end
