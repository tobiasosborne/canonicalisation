# Objectives 0x0001-0x0007 (spec §3 table) by the complete reference algorithms of
# §8.1-8.4, typed labelings §3.1, statuses §3.2, the work quota §11.1 and the
# FORMAT record (refs/compare/FORMAT.md).

const OBJ_NAMES = Dict(1 => "CANONICAL_IMAGE", 2 => "LEX_MIN_IMAGE", 3 => "TRANSPORTER_ONE",
    4 => "STABILISER", 5 => "CANONICAL_LABELING_COSET", 6 => "TRANSPORTER_COSET",
    7 => "SIGNED_CANONICAL_IMAGE", 8 => "CONSTRAINT_ONE", 9 => "CONSTRAINT_ENUM")
# refs/CASES.md objective table, spec §4.3: P1 (1) for canonical and signed images,
# NO_TREE (0) for coset-enumeration objectives
const OBJ_PROFILE = Dict(1 => 1, 2 => 0, 3 => 0, 4 => 0, 5 => 1, 6 => 0, 7 => 1)

struct Record
    case_id::String
    objective::String
    status::String
    trace::String
    bytes::String
    witness::String
    group::String
end

record_line(r::Record) = join((r.case_id, r.objective, r.status, r.trace, r.bytes, r.witness, r.group), '\t')

# FORMAT: comma-separated decimal images; "-" for n = 0 (the empty permutation)
wstr(w::Perm) = isempty(w) ? "-" : join(w, ",")

fail(id, obj, st) = Record(id, obj, st, "", "", "-", "-")

# ---------------------------------------------------------------- stabiliser
# spec §8.2 stabiliser consumer: test x^r = x, insert every hit into a verified
# subgroup (rebuilt only when a hit is not yet a member).
mutable struct SubgroupBuilder
    n::Int
    gens::Vector{Perm}
    K::BSGS
end
SubgroupBuilder(n::Int) = SubgroupBuilder(n, Perm[], full_chain(n, Perm[]))
function sg_insert!(sb::SubgroupBuilder, g::Perm)
    contains(sb.K, g) && return
    push!(sb.gens, g)
    sb.K = full_chain(sb.n, sb.gens)
end

# complete stabiliser of x by the exhaustive §8.1 enumeration of E (charged in full)
function complete_stabiliser(E::Enumerator, x::Obj, bud::Budget)
    charge!(bud, full_visits(E))
    sb = SubgroupBuilder(E.B.n)
    enumerate_cosets(E, nothing) do r
        fixes(x, r) && sg_insert!(sb, r)
        false
    end
    return sb.K
end

# ---------------------------------------------------------------- SIMPLE-UPPER-1
# spec §4.4: only for uncoloured simple undirected graphs: empty vertex/arc labels,
# no loops, exactly one arc in each direction for each edge.
function is_simple_undirected(x::Obj)
    x.kind == :graph || return false
    g = x.graph
    all(isempty, g.colours) || return false
    s = Set{Tuple{Int,Int}}()
    for a in g.arcs
        (isempty(a.label) && a.s != a.t && a.m == 1) || return false
        push!(s, (a.s, a.t))
    end
    for (u, v) in s
        (v, u) in s || return false
    end
    return true
end

# U32(n) then upper-triangle bits (0,1),(0,2),(1,2),(0,3),... MSB first, zero padded;
# for n <= 1 exactly U32(n)
function su1_key(x::Obj)
    n = x.n
    io = UInt8[]
    u32!(io, n)
    n <= 1 && return io
    adj = Set{Tuple{Int,Int}}((a.s, a.t) for a in x.graph.arcs)
    nb = n * (n - 1) ÷ 2
    bytes = zeros(UInt8, cld(nb, 8))
    k = 0
    for j in 1:n-1, i in 0:j-1
        if (i, j) in adj
            bytes[k÷8+1] |= UInt8(0x80 >> (k % 8))
        end
        k += 1
    end
    append!(io, bytes)
    return io
end

# ---------------------------------------------------------------- signed helpers
# spec §8.4: lifted generated group on Ω ⊔ {+,-} (points n, n+1); generator g acts as
# given on Ω and swaps the last two points iff its sign is -1.
function lift(g::Perm, s::Int)
    n = length(g)
    return vcat(g, s == 1 ? [n, n + 1] : [n + 1, n])
end

# χ(h) for h in G, given the lifted chain (kernel already proved trivial)
chi(Bl::BSGS, h::Perm) = contains(Bl, lift(h, 1)) ? 1 : -1

# ---------------------------------------------------------------- dispatcher
function solve_case(c)
    id = c isa Dict && haskey(c, "id") && c["id"] isa String ? c["id"] : ""
    tag = c isa Dict ? get(c, "objective_tag", nothing) : nothing
    objhex = isint(tag) && 0 <= tag <= 0xffff ? lpad(string(tag; base=16), 4, '0') : "0000"
    try
        return solve_case_inner(c, id, objhex)
    catch e
        if e isa InvalidInput
            return fail(id, objhex, "INVALID_INPUT")
        elseif e isa Unsupported
            return fail(id, objhex, "UNSUPPORTED_ACTION")
        elseif e isa QuotaExceeded || e isa CapacityError
            return fail(id, objhex, "CAPACITY_LIMIT")
        else
            @warn "internal error on case $id" exception = (e, catch_backtrace())
            return fail(id, objhex, "INTERNAL_ERROR")
        end
    end
end

function solve_case_inner(c, id, objhex)
    c isa Dict || throw(InvalidInput("case is not an object"))
    isempty(id) && throw(InvalidInput("missing id"))
    tag = req(c, "objective_tag")
    isint(tag) || throw(InvalidInput("objective_tag not an integer"))
    haskey(OBJ_NAMES, tag) || throw(Unsupported("unknown objective"))
    req(c, "objective") == OBJ_NAMES[tag] || throw(InvalidInput("objective name and tag disagree"))
    # objectives 8/9 are outside M0: UNSUPPORTED_ACTION, read no further keys (CASES.md)
    tag in (8, 9) && throw(Unsupported("constraint objectives"))
    # signed labeling problem: σ_ρ deferred, M0 returns UNSUPPORTED_ACTION (spec §3.1)
    tag == 7 && haskey(c, "labeling") && throw(Unsupported("signed labeling"))
    enc = req(c, "encoding")
    isint(enc) || throw(InvalidInput("encoding not an integer"))
    enc == 2 || throw(Unsupported("encoding"))           # spec §4.1: unknown versions unsupported
    prof = req(c, "profile")
    isint(prof) || throw(InvalidInput("profile not an integer"))
    prof == OBJ_PROFILE[tag] || throw(Unsupported("profile"))
    n = req(c, "n")
    (isint(n) && n >= 0) || throw(InvalidInput("bad n"))
    n <= 0xffffffff || throw(CapacityError("n exceeds U32"))
    n = Int(n)
    gens = parse_gens(req(c, "group_generators"), n)
    wm = get(c, "witness_mode", "deterministic")
    wm in ("deterministic", "any") || throw(InvalidInput("bad witness_mode"))
    det = wm == "deterministic"
    quota = big(2)^20                                    # spec §11.1 default quota
    if haskey(c, "capacity")
        cap = c["capacity"]
        cap isa Dict || throw(InvalidInput("capacity not an object"))
        if haskey(cap, "max_search_nodes")
            q = cap["max_search_nodes"]
            (isint(q) && q >= 0) || throw(InvalidInput("bad max_search_nodes"))
            quota = big(q)
        end
        if haskey(cap, "work_policy")
            wp = cap["work_policy"]
            (isint(wp) && wp > 0) || throw(InvalidInput("bad work_policy"))
            wp == 1 || throw(Unsupported("work policy"))   # only 0x0001 (unpruned reference)
        end
    end
    x = parse_object(req(c, "object"), n)
    bud = Budget(quota)
    if tag == 1
        return obj_canonical(id, objhex, n, gens, x, det, bud)
    elseif tag == 2
        return obj_lexmin(c, id, objhex, n, gens, x, bud)
    elseif tag == 3 || tag == 6
        y = parse_object(req(c, "target"), n)
        return obj_transporter(id, objhex, n, gens, x, y, det, bud, tag == 6)
    elseif tag == 4
        A = complete_stabiliser(Enumerator(full_chain(n, gens)), x, bud)
        return Record(id, objhex, "COMPLETE", "", "", "-", hexstr(group_bytes!(UInt8[], A)))
    elseif tag == 5
        return obj_labeling(c, id, objhex, n, gens, x, det, bud)
    elseif tag == 7
        return obj_signed(c, id, objhex, n, gens, x, det, bud)
    end
    throw(Unsupported("objective"))
end

# §7.3: "If every generator fixes x, the unsigned orbit is a singleton"; §11.1: the
# root shortcuts are part of P1's policy and visit no tree nodes.
singleton_orbit(x::Obj, gens::Vector{Perm}) = all(g -> fixes(x, g), gens)

function run_p1(x::Obj, gens::Vector{Perm}, bud::Budget)
    G = GNode(x.n, gens)
    if singleton_orbit(x, gens)
        return p1_run(x, G, nothing; single_path=true)
    end
    return p1_run(x, G, bud)
end

# 0x0001 CANONICAL_IMAGE: P1 image; deterministic witness = least g with x^g = c,
# i.e. min A t after the complete stabiliser enumeration (spec §3), run after P1.
function obj_canonical(id, objhex, n, gens, x, det, bud)
    E = Enumerator(full_chain(n, gens))
    det && charge!(bud, full_visits(E))       # the enumeration's count, charged up front
    r = run_p1(x, gens, bud)
    w = r.t
    if det
        bud2 = Budget(typemax(Int64))         # already charged
        A = complete_stabiliser(E, x, bud2)
        w = least_coset(A, r.t)
    end
    return Record(id, objhex, "COMPLETE", hexstr(r.trace), hexstr(r.stream), wstr(w), "-")
end

# 0x0002 LEX_MIN_IMAGE: §8.2 minimum consumer over the whole §8.1 enumeration; the
# witness is the least g attaining the minimum (spec §3).
function obj_lexmin(c, id, objhex, n, gens, x, bud)
    ord = req(c, "order")
    ord isa String || throw(InvalidInput("order not a string"))
    su1 = if ord == "CDAG-BYTE-1"
        false
    elseif ord == "SIMPLE-UPPER-1"
        is_simple_undirected(x) || throw(Unsupported("SIMPLE-UPPER-1 needs an uncoloured simple undirected graph"))
        true
    else
        throw(Unsupported("unknown order"))
    end
    E = Enumerator(full_chain(n, gens))
    charge!(bud, full_visits(E))
    bestkey = nothing; bestg = Int[]; beststream = UInt8[]
    enumerate_cosets(E, nothing) do r
        y = act(x, r)
        key = su1 ? su1_key(y) : y.stream
        if bestkey === nothing || isless(key, bestkey) || (key == bestkey && isless(r, bestg))
            bestkey = key; bestg = r; beststream = y.stream
        end
        false
    end
    return Record(id, objhex, "COMPLETE", "", hexstr(beststream), wstr(bestg), su1 ? hexstr(bestkey) : "-")
end

# 0x0003 TRANSPORTER_ONE / 0x0006 TRANSPORTER_COSET (spec §8.2): enumerate until
# x^r = y; then the complete stabiliser (coset objective always, TRANSPORTER_ONE in the
# deterministic mode, "after the hit"); r0 = min A g. A target of another shape is
# never hit (spec §3).
function obj_transporter(id, objhex, n, gens, x, y, det, bud, coset)
    E = Enumerator(full_chain(n, gens))
    # Shortcut of computation only: with a hit the count is (visits to the hit) + the
    # full stabiliser enumeration; without one it is the full enumeration. Either way
    # it is at least full_visits(E), so a smaller quota is CAPACITY_LIMIT whatever
    # the search would find.
    (coset || det) && full_visits(E) > bud.quota && throw(QuotaExceeded())
    hit = nothing
    enumerate_cosets(E, bud) do r
        if act(x, r).stream == y.stream
            hit = r
            return true
        end
        false
    end
    hit === nothing && return Record(id, objhex, "COMPLETE", "", "", "-", "-")
    if coset || det
        A = complete_stabiliser(E, x, bud)
        r0 = least_coset(A, hit)
        grp = coset ? hexstr(perm_bytes!(group_bytes!(UInt8[], A), r0)) : "-"
        return Record(id, objhex, "COMPLETE", "", "", wstr(det ? r0 : hit), grp)
    end
    return Record(id, objhex, "COMPLETE", "", "", wstr(hit), "-")
end

# 0x0005 CANONICAL_LABELING_COSET (spec §3.1, §8.2): x' = x^ρ, G' = ρ^-1 G ρ on D_n;
# P1 gives t in G'; λ = ρ t; A = Aut_G(x) computed and reported on Ω; λ0 = min Aλ.
function obj_labeling(c, id, objhex, n, gens, x, det, bud)
    lab = req(c, "labeling")
    lab isa Dict || throw(InvalidInput("labeling not an object"))
    rho = parse_perm(req(lab, "rho"), n)
    E = Enumerator(full_chain(n, gens))
    charge!(bud, full_visits(E))
    xp = act(x, rho)
    gp = Perm[pconj(g, rho) for g in gens]
    r = run_p1(xp, gp, bud)
    A = complete_stabiliser(E, x, Budget(typemax(Int64)))
    λ = pmul(rho, r.t)
    λ0 = least_coset(A, λ)
    grp = hexstr(perm_bytes!(group_bytes!(UInt8[], A), λ0))
    return Record(id, objhex, "COMPLETE", hexstr(r.trace), hexstr(r.stream), wstr(det ? λ0 : λ), grp)
end

# 0x0007 SIGNED_CANONICAL_IMAGE (spec §8.4, §3, §7.3)
function obj_signed(c, id, objhex, n, gens, x, det, bud)
    ch = req(c, "character")
    (ch isa Vector && length(ch) == length(gens) && all(s -> isint(s) && (s == 1 || s == -1), ch)) ||
        throw(InvalidInput("character must give ±1 per generator"))
    n + 2 <= 0xffffffff || throw(CapacityError("lifted degree"))
    lifted = Perm[lift(g, Int(s)) for (g, s) in zip(gens, ch)]
    Bl = full_chain(n + 2, lifted)
    BG = full_chain(n, gens)
    # χ exists iff the lifted subgroup fixing Ω pointwise is trivial: |G~| = |G|
    group_order(Bl) == group_order(BG) || throw(InvalidInput("signs define no character"))
    sgn(s) = s == 1 ? "+1" : "-1"
    if singleton_orbit(x, gens)
        # §7.3 signed singleton route: A = G known without traversal; any odd generator
        # proves zero, else all of G = A is even. Nothing is enumerated (§3).
        oddi = findfirst(==(-1), ch)
        if oddi !== nothing
            # least odd element of A = G: least element of the coset G+ a
            Bp = build_bsgs(n + 2, lifted, [n])
            gplus = Perm[g[1:n] for g in level_gens(Bp, 2)]
            a0 = least_coset(full_chain(n, gplus), gens[oddi])
            return Record(id, objhex, "COMPLETE", "", "00", wstr(a0) * ";sign=0", "-")
        end
        r = p1_run(x, GNode(n, gens), nothing; single_path=true)
        w = det ? least_coset(BG, r.t) : r.t
        s = chi(Bl, r.t)
        return Record(id, objhex, "COMPLETE", hexstr(r.trace), hexstr(r.stream),
                      wstr(w) * ";sign=" * sgn(s), hexstr(group_bytes!(UInt8[], BG)))
    end
    # §8.4 reference: enumerate the stabiliser (§8.2) over the lifted group, whose
    # enumeration visits exactly the lifts of the §8.1 enumeration of G (same chain
    # orbits on Ω, trivial kernel), so that χ(r) is read off the marker points.
    El = Enumerator(Bl)
    det && charge!(bud, full_visits(El))
    sb = SubgroupBuilder(n)
    oddmin = nothing
    enumerate_cosets(El, det ? nothing : bud) do rl
        r = rl[1:n]
        if fixes(x, r)
            sg_insert!(sb, r)
            if rl[n+1] != n                      # odd: swaps + and -
                if oddmin === nothing || isless(r, oddmin)
                    oddmin = r
                end
                det || return true               # without the mode: stop at the first odd witness
            end
        end
        false
    end
    if oddmin !== nothing
        return Record(id, objhex, "COMPLETE", "", "00", wstr(oddmin) * ";sign=0", "-")
    end
    A = sb.K
    r = run_p1(x, gens, bud)
    w = det ? least_coset(A, r.t) : r.t
    s = chi(Bl, r.t)
    return Record(id, objhex, "COMPLETE", hexstr(r.trace), hexstr(r.stream),
                  wstr(w) * ";sign=" * sgn(s), hexstr(group_bytes!(UInt8[], A)))
end
