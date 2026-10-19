# Minimal JSON reader for ref-b (BRIEF §4, Answer 5): standard library only.
# Integers are read exactly (Int64 when they fit, otherwise BigInt), never via
# floating point (spec §4.1: "no ... implicit numeric coercion or floating-point
# interpretation"). A number with a fraction or exponent is returned as a
# JSONNonInteger wrapper holding its source text, so that any integer field that
# receives one is rejected as INVALID_INPUT by the validator.

struct JSONNonInteger
    text::String
end

struct JSONError <: Exception
    msg::String
end

mutable struct JSONReader
    s::Vector{UInt8}
    i::Int
end

@inline function skipws!(r::JSONReader)
    s = r.s
    i = r.i
    n = length(s)
    while i <= n
        c = s[i]
        if c == 0x20 || c == 0x0a || c == 0x0d || c == 0x09
            i += 1
        else
            break
        end
    end
    r.i = i
end

@inline peekb(r::JSONReader) = r.i <= length(r.s) ? r.s[r.i] : 0x00

function expect!(r::JSONReader, c::UInt8)
    peekb(r) == c || throw(JSONError("expected '$(Char(c))' at byte $(r.i)"))
    r.i += 1
end

function parse_value!(r::JSONReader)
    skipws!(r)
    c = peekb(r)
    if c == UInt8('{')
        return parse_object!(r)
    elseif c == UInt8('[')
        return parse_array!(r)
    elseif c == UInt8('"')
        return parse_string!(r)
    elseif c == UInt8('-') || (UInt8('0') <= c <= UInt8('9'))
        return parse_number!(r)
    elseif c == UInt8('t')
        lit!(r, "true"); return true
    elseif c == UInt8('f')
        lit!(r, "false"); return false
    elseif c == UInt8('n')
        lit!(r, "null"); return nothing
    else
        throw(JSONError("unexpected byte at $(r.i)"))
    end
end

function lit!(r::JSONReader, w::String)
    for b in codeunits(w)
        expect!(r, b)
    end
end

function parse_object!(r::JSONReader)
    expect!(r, UInt8('{'))
    d = Dict{String,Any}()
    skipws!(r)
    if peekb(r) == UInt8('}')
        r.i += 1
        return d
    end
    while true
        skipws!(r)
        k = parse_string!(r)
        skipws!(r)
        expect!(r, UInt8(':'))
        v = parse_value!(r)
        d[k] = v   # duplicate keys: last wins (no case file relies on duplicates)
        skipws!(r)
        c = peekb(r)
        if c == UInt8(',')
            r.i += 1
        elseif c == UInt8('}')
            r.i += 1
            return d
        else
            throw(JSONError("expected ',' or '}' at byte $(r.i)"))
        end
    end
end

function parse_array!(r::JSONReader)
    expect!(r, UInt8('['))
    a = Any[]
    skipws!(r)
    if peekb(r) == UInt8(']')
        r.i += 1
        return a
    end
    while true
        push!(a, parse_value!(r))
        skipws!(r)
        c = peekb(r)
        if c == UInt8(',')
            r.i += 1
        elseif c == UInt8(']')
            r.i += 1
            return a
        else
            throw(JSONError("expected ',' or ']' at byte $(r.i)"))
        end
    end
end

function hexval(c::UInt8)
    if UInt8('0') <= c <= UInt8('9')
        return Int(c - UInt8('0'))
    elseif UInt8('a') <= c <= UInt8('f')
        return Int(c - UInt8('a')) + 10
    elseif UInt8('A') <= c <= UInt8('F')
        return Int(c - UInt8('A')) + 10
    end
    throw(JSONError("bad hex digit in \\u escape"))
end

function parse_u4!(r::JSONReader)
    r.i + 3 <= length(r.s) || throw(JSONError("short \\u escape"))
    v = 0
    for k in 0:3
        v = 16v + hexval(r.s[r.i+k])
    end
    r.i += 4
    return v
end

function parse_string!(r::JSONReader)
    expect!(r, UInt8('"'))
    s = r.s
    start = r.i
    # fast path: no escapes
    i = start
    n = length(s)
    while i <= n && s[i] != UInt8('"') && s[i] != UInt8('\\')
        i += 1
    end
    i <= n || throw(JSONError("unterminated string"))
    if s[i] == UInt8('"')
        r.i = i + 1
        return String(s[start:i-1])
    end
    io = IOBuffer()
    write(io, view(s, start:i-1))
    r.i = i
    while true
        r.i <= n || throw(JSONError("unterminated string"))
        c = s[r.i]
        if c == UInt8('"')
            r.i += 1
            return String(take!(io))
        elseif c == UInt8('\\')
            r.i += 1
            e = s[r.i]
            r.i += 1
            if e == UInt8('"'); write(io, UInt8('"'))
            elseif e == UInt8('\\'); write(io, UInt8('\\'))
            elseif e == UInt8('/'); write(io, UInt8('/'))
            elseif e == UInt8('b'); write(io, 0x08)
            elseif e == UInt8('f'); write(io, 0x0c)
            elseif e == UInt8('n'); write(io, 0x0a)
            elseif e == UInt8('r'); write(io, 0x0d)
            elseif e == UInt8('t'); write(io, 0x09)
            elseif e == UInt8('u')
                cp = parse_u4!(r)
                if 0xd800 <= cp <= 0xdbff && r.i + 1 <= n && s[r.i] == UInt8('\\') && s[r.i+1] == UInt8('u')
                    r.i += 2
                    lo = parse_u4!(r)
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00)
                end
                write(io, Char(cp))
            else
                throw(JSONError("bad escape"))
            end
        else
            write(io, c)
            r.i += 1
        end
    end
end

function parse_number!(r::JSONReader)
    s = r.s
    start = r.i
    i = r.i
    n = length(s)
    if i <= n && s[i] == UInt8('-')
        i += 1
    end
    while i <= n && UInt8('0') <= s[i] <= UInt8('9')
        i += 1
    end
    isint = true
    if i <= n && (s[i] == UInt8('.') || s[i] == UInt8('e') || s[i] == UInt8('E'))
        isint = false
        while i <= n && (UInt8('0') <= s[i] <= UInt8('9') || s[i] in (UInt8('.'), UInt8('e'), UInt8('E'), UInt8('+'), UInt8('-')))
            i += 1
        end
    end
    r.i = i
    txt = String(s[start:i-1])
    if !isint
        return JSONNonInteger(txt)
    end
    # exact integer: digits only, read without floating point
    neg = startswith(txt, "-")
    digits = neg ? txt[2:end] : txt
    isempty(digits) && throw(JSONError("bad number"))
    if length(digits) <= 18
        v = 0
        for ch in codeunits(digits)
            v = 10v + Int(ch - UInt8('0'))
        end
        return neg ? -v : v
    end
    v = big(0)
    for ch in codeunits(digits)
        v = 10v + (ch - UInt8('0'))
    end
    v = neg ? -v : v
    return typemin(Int64) <= v <= typemax(Int64) ? Int64(v) : v
end

function json_parse(bytes::Vector{UInt8})
    r = JSONReader(bytes, 1)
    v = parse_value!(r)
    skipws!(r)
    r.i == length(bytes) + 1 || throw(JSONError("trailing data"))
    return v
end

json_parse_file(path::AbstractString) = json_parse(read(path))
