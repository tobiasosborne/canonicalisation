# ref-b runner: julia run.jl CASEFILE > out.tsv
# Reads a case file (top-level "cases" or "p1_cases", refs/CASES.md) and prints one
# FORMAT record per case (refs/compare/FORMAT.md). With REFB_TIMING=<file>, appends
# "case_id<TAB>status<TAB>seconds" per case to that file.
include(joinpath(@__DIR__, "src", "refb.jl"))

function main(args)
    length(args) == 1 || (println(stderr, "usage: julia run.jl CASEFILE"); exit(2))
    doc = json_parse_file(args[1])
    doc isa Dict || error("case file is not a JSON object")
    cases = haskey(doc, "cases") ? doc["cases"] : haskey(doc, "p1_cases") ? doc["p1_cases"] : error("no cases array")
    tfile = get(ENV, "REFB_TIMING", "")
    tio = isempty(tfile) ? nothing : open(tfile, "a")
    for c in cases
        t0 = time()
        rec = solve_case(c)
        println(stdout, record_line(rec))
        flush(stdout)
        if tio !== nothing
            println(tio, rec.case_id, "\t", rec.status, "\t", round(time() - t0; digits=3))
            flush(tio)
        end
    end
    tio === nothing || close(tio)
end

main(ARGS)
