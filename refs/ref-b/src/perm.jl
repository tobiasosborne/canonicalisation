# Permutation kernel (spec §3).
# A permutation on {0..n-1} is a Vector{Int} p with p[v+1] = v^p (0-based
# images stored at 1-based Julia positions): "Arrays store p[v] = v^p".
# Products act left to right: (pq)[v] = q[p[v]], so (x^p)^q = x^(pq).

const Perm = Vector{Int}

pid(n::Integer) = collect(0:n-1)

@inline pimg(p::Perm, v::Integer) = @inbounds p[v+1]

# spec §3: (pq)[v] = q[p[v]]
function pmul(p::Perm, q::Perm)
    r = similar(p)
    @inbounds for i in eachindex(p)
        r[i] = q[p[i]+1]
    end
    return r
end

# inverse: p^-1[p[v]] = v
function pinv(p::Perm)
    r = similar(p)
    @inbounds for i in eachindex(p)
        r[p[i]+1] = i - 1
    end
    return r
end

function isid(p::Perm)
    @inbounds for i in eachindex(p)
        p[i] == i - 1 || return false
    end
    return true
end

# Is p (any vector) a bijection of {0..n-1}?
function isperm0(p::AbstractVector, n::Integer)
    length(p) == n || return false
    seen = falses(n)
    for x in p
        (x isa Integer && 0 <= x < n) || return false
        seen[x+1] && return false
        seen[x+1] = true
    end
    return true
end

# g^-1 p g (spec §2.1: a permutation object p becomes g^-1 p g);
# (g^-1 p g)[v] = g[p[g^-1[v]]], i.e. cycle (a b) of p maps to (a^g b^g).
function pconj(p::Perm, g::Perm)
    return pmul(pmul(pinv(g), p), g)
end

# lexicographic comparison of image arrays (numerical order on entries)
plt(p::Perm, q::Perm) = isless(p, q)
