# Analytic performance bounds for the proposed canonicalisation engine

**Referee appendix · 29 September 2026; consistency update 30 September 2026 · specification v2.0**

This appendix analyses [the architecture specification](specification.md). It assumes the best legal data structures, packing, caching, scheduling and reuse compatible with the operation being analysed. Costs caused only by an avoidable allocation, unnecessary clearing, oversized index, chosen traversal or materialised intermediate are identified separately. No implementation, hardware benchmark or calibration was run. Numerical results are arithmetic consequences of stated assumptions, not measurements.

The principal conclusion is that useful hardware floors can be obtained for **mandatory information movement, mandatory output and a specified computation DAG**. Version 2.0 freezes P1 and its wire representation, but a profile alone does not establish necessary search work or boundary traffic for every solve; there is still no useful single numeric lower bound for all supported inputs. In particular, factorial enumeration is a fallback cost, not a lower bound for canonicalisation as a problem. The v1.0 full-transversal subgroup output, now diagnostic-only in specification §9.4, is an exception: its size gives a substantial, exact lower bound for that selected serialization contract.

## 1. What a lower-bound claim means

Use the following labels throughout implementation and benchmarking.

| Label | Meaning | Examples |
|---|---|---|
| **U: problem or contract bound** | Every correct algorithm in the declared computational/input/output model pays the cost on the indicated inputs or in the worst case. | Reading an untrusted explicit permutation array; writing a required stream of $Z$ bytes; an information bound on an injective output. |
| **A: algorithm bound** | The specified algorithm, profile, representation or computation DAG imposes the cost; a different legal algorithm may avoid it. | Exhaustive coset enumeration without successful pruning; row-mask AND/popcount; full-array transporter materialisation; the specified subgroup transversal output. |
| **H: hardware-model bound** | Given a proved demand and an upper limit on a hardware resource, resource demand divided by that limit is a lower bound. | Bytes that actually must cross fixed-speed DDR5 or PCIe; native-instruction count divided by the instruction's documented maximum throughput, subject to a clock bound. |
| **E: estimate or calibration model** | A predicted time uses assumed or empirically attained throughput/latency. It is useful for engineering but does not certify an absolute floor. | $B/(60\,\mathrm{GB/s})$; a chain of misses modelled at 80 ns each; an assumed 5 μs kernel-launch overhead. |

Bounds are instance-sensitive unless their worst-case quantifier is stated. An $\Omega(f(n))$ worst-case result does not make every instance cost $f(n)$. An asymptotic result does not supply a usable constant in seconds. Output bounds concern a particular output contract: a lazy view, a compact group descriptor and a fully expanded byte stream are different obligations.

Distinguish three times: `group_create`/validation and preprocessing; `solve` with trusted frozen inputs; and `result_encode`/delivery to the specified sink. A claim about their sum cannot be inferred from a warm `solve` timing alone. Output already cached, output returned by reference and output committed to a device require different movement accounting.

## 2. A concrete commodity desktop and its evidence

The example machine is an **AMD Ryzen 9 9950X**, **two 32 GiB Kingston KVR56U46BD8-32 DIMMs at JEDEC DDR5-5600**, and an **NVIDIA GeForce RTX 5080**, in a motherboard providing an unshared PCIe 5.0 ×16 graphics slot. The CPU is the production target in the specification; the GPU is an optional analysis of an additional backend. This is an example configuration, not a recommendation to buy hardware and not a claim that the workspace runs on it.

Primary documents are archived under [review_sources/hardware](../review_sources/hardware/README.md); [manifest.json](../review_sources/hardware/manifest.json) records retrieval URLs, timestamps, response types, byte lengths and SHA-256 values. Numerical hardware claims below rely on the local documents, not search snippets. Vendor portal stubs are marked as such and are not used as documentation of execution ports or cache latencies.

| Item | Sourced specification or selected configuration | Interpretation |
|---|---|---|
| CPU | 16 cores, 32 SMT threads; base 4.3 GHz, advertised boost up to 5.7 GHz; aggregate L1 1280 KB, L2 16 MB, L3 64 MB; two memory channels; DDR5-5600 with two DIMMs; PCIe 5.0. | The aggregate cache figure is not a single cache shared equally by all cores. Advertised boost is not a guaranteed simultaneous all-core clock. [Local AMD specifications](../review_sources/hardware/amd_ryzen_9_9950x.html), “General Specifications”/“Connectivity”; [retrieval URL](https://www.amd.com/en/products/processors/desktops/ryzen/9000-series/amd-ryzen-9-9950x.html). |
| RAM | Selected pair of 32 GiB, 2Rx8, ×64 DDR5-5600 DIMMs, CL46, JEDEC timing 46-45-45; 64 GiB installed. | The module's 4G ×64 organization gives binary capacity. The clock is 2.8 GHz; 46 clocks are 16.43 ns. This CAS interval omits controller, interconnect, row activation, queueing and return time. [Local Kingston datasheet](../review_sources/hardware/kingston_KVR56U46BD8-32.pdf), p. 1; [retrieval URL](https://www.kingston.com/datasheets/KVR56U46BD8-32.pdf). |
| GPU | RTX 5080/GB203: 84 SMs; boost 2617 MHz; 16 GB GDDR7; 256-bit interface at 30 Gb/s; stated bandwidth 960 GB/s; L2 65536 KB; aggregate L1/shared memory 10752 KB. | Capacity calculations below use nominal 16 GiB; available allocation capacity must be queried and is smaller. L1/shared capacity is distributed, and shared-memory use reduces available cache. [Local NVIDIA whitepaper](../review_sources/hardware/nvidia_rtx_blackwell_architecture.pdf), v1.1, printed pp. 49–51, Table 4; [retrieval URL](https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf). |
| GPU link | PCIe 5.0, selected ×16 wiring; 32 GT/s per lane, 128b/130b encoding. | Before packet overhead, ×16 gives $16\times32\times(128/130)/8=63.015$ GB/s **per direction**. Gen4 ×16 or Gen5 ×8 halves this. Full duplex does not double the rate of a one-direction transfer. [Local PCI-SIG presentation](../review_sources/hardware/pcisig_generations_2025.pdf), 15 July 2025, slide 7; [retrieval URL](https://pcisig.com/sites/default/files/2026-01/PCI-SIG%20PCIe%207.0%20Webinar_Rev5_FINAL.pdf); [local GPU product specifications](../review_sources/hardware/nvidia_rtx_5080.html). |

For decimal time arithmetic, 1 GB is $10^9$ bytes; capacities explicitly marked GiB/MiB use powers of two. The ideal DDR5 interface rate is

$$
\beta_{\mathrm{DDR,signal}}=2\cdot 8\cdot5.6\times10^9
=89.6\times10^9\ \mathrm{bytes/s}.
$$

This is an optimistic signaling limit under the selected fixed clock, not sustained application bandwidth. Commands, refresh, turnarounds, chiplet interconnects and conflicting requests reduce useful throughput. The GPU's 960 GB/s likewise concerns its local GDDR interface, not bandwidth accessible to a CPU DFS. No claim about strict real-machine upper limits follows from three-digit marketing clock ratings alone: certify actual maximum clocks/configuration when a strict bound in seconds is required. Disable overclocking, document clock tolerances and use a conservative upper rate.

The following **E parameters** provide realistic sensitivity examples; none was measured on this configuration.

| Parameter | Central scenario | Sensitivity range or qualification |
|---|---:|---|
| Loaded CPU clock | 5.0 GHz | 4.3–5.7 GHz; record actual frequency under each kernel. |
| Aggregate DRAM useful bandwidth | 60 GB/s | 40–80 GB/s for examples; a range to test, not guaranteed machine performance. Random updates can be much slower. |
| GPU useful GDDR bandwidth | 700 GB/s | 500–850 GB/s for coalesced streams; irregular kernels require separate calibration. |
| Pinned host/device useful transfer rate | 50 GB/s per direction | 35–55 GB/s; topology, driver, transfer size and host-memory competition matter. |
| CPU L1/L2/LLC hit latency | 4 / 14 / 45 cycles | Illustrative dependency costs, not vendor guarantees established by the archived product sheet. |
| CPU dependent DRAM miss | 80 ns | 60–150 ns; use loaded latency for the active core set. |
| GPU dependent device-memory miss | 300 ns | 200–800 ns; warp scheduling, cache hits and outstanding requests matter. |
| GPU launch plus short synchronization | 5 μs | 3–15 μs per operation; operating system/runtime dependent. |
| Local CPU cache topology model | 48 KiB L1D/core; 1 MiB L2/core; two 32 MiB shared-cache clusters | Planning model to verify through CPUID/OS topology on the actual machine; do not treat all 64 MiB L3 as one uniform local cache. |

CPU integer port rates are deliberately left as parameters until the target optimization guide and emitted machine code have been checked. It would be less rigorous to invent a “canonicalisation operations per second” rating from CPU frequency or GPU FLOPs.

## 3. Unavoidable information, validation and output

Let $I$ be input bits, $Z$ required output bytes, $w$ the machine-model word width, $n=|\Omega|$, $N$ total working vertices including auxiliaries, and $m$ directed relation incidences. Counts of bits do not themselves prove that all those bits cross DRAM during a warm operation.

### 3.1 Explicit import and trivial-action reductions

For arbitrary untrusted explicit inputs, worst-case validation must inspect every field that can make the input invalid. An unread entry of a purported permutation may create an out-of-range value or a duplicate. Thus importing $s$ dense degree-$n$ generators requires $\Omega(sn)$ entry inspections and their associated input read, even if the generated group has a much shorter description. This bound belongs to untrusted import; a previously verified immutable group can be reused without repeating it.

For an object with the trivial admissible group, canonicalisation returns that object's exact encoding. Arbitrary literal or tuple payloads cannot be reconstructed if some relevant input symbols remain unexamined. This gives a worst-case $\Omega(I/w)$ word-access bound for explicit inputs in the word-RAM model and $\Omega(Z/w)$ materialised-output work. It does **not** say that a trusted handle must be copied, that a highly repetitive literal must be expanded internally, or that every transporter rejection must read the whole input. An early mismatch can prove failure on particular instances.

If $Z$ bytes must cross a specified sink interface of maximum payload rate $\beta_{\rm sink}^{\max}$, then

$$
T_{\rm emit}\ge Z/\beta_{\rm sink}^{\max}. \tag{1}
$$

This **U+H** bound is independent of data layout and internal computation. If the contract returns an immutable view and does not emit bytes, (1) is inapplicable. If output is written into RAM and allowed to remain dirty in cache at timing completion, only bytes forced beyond the cache boundary belong in a DRAM bound. Disk/network delivery uses that sink's limit, which can dominate the RAM limit.

### 3.2 Optimal packing versus C arrays

For a finite family with $F$ possible distinct values, an injective fixed-length representation requires at least $\lceil\log_2 F\rceil$ bits in the worst case. For variable-length binary descriptions, the analogous maximum-length statement changes by at most an additive constant when short lengths are counted. This is an information bound, not a guarantee that entropy-optimal compression is cheap or supports constant-time access.

| Value family | Information floor | Conventional representation and qualification |
|---|---:|---|
| Arbitrary permutation of $n$ points | $\log_2(n!)=n\log_2n-(\log_2e)n+O(\log n)$ bits | $4n$ bytes for uint32 images; $\lceil\log_2n\rceil n$ bits for independently packed images. Tagged identity/small support/structured elements can be smaller on their special families. |
| Arbitrary $k$-subset | $\log_2\binom nk$ bits | Sorted list $4k$ bytes or bitset $\lceil n/8\rceil$ bytes. Neither universally wins. |
| Tuple of length $a$ over $n$ atoms | $a\log_2n$ bits | Repetitions are semantic; a compressed repetitive instance may be shorter. |
| Simple directed graph with $m$ arcs among $P=n^2$ possible arcs | $\log_2\binom Pm$ bits on an indexed domain | CSR destination integers, offsets and labels add access-support overhead. For loopless relations replace $P$ by $n(n-1)$. |
| Arbitrary directed graph canonical images under Sym($n$) | At least $n^2-\log_2(n!)$ bits in worst-case injective orbit descriptions | There are $2^{n^2}$ indexed graphs and at most $n!$ representatives per orbit. A chosen fixed dense wire format still writes $n^2$ bits even on an easy graph. |
| Exact group order | $\lfloor\log_2\lvert G\rvert\rfloor+1$ bits | Because $\lvert G\rvert\le n!$, worst-case order length is $\Theta(n\log n)$, not constant. |

An arbitrary witness need not always contain $\log_2(n!)$ bits: many inputs permit the identity. That worst-case information requirement does hold for a witness forced to relabel an ordered tuple containing each atom once to a fixed canonical tuple, as its input ranges over all $n!$ orders. A dense witness contract imposes $n$ output entries regardless of the particular witness's compressibility.

At $n=100{,}000$, $\log_2(n!)\approx1{,}516{,}704.17$ bits, or about 189.6 kB. A dense uint32 permutation is 400 kB; a 17-bit image array is 212.5 kB. One thousand materialised dense transporters occupy 400 MB, but their information floor is **not automatically** one thousand times that of an independent permutation: transversals of a shared known group may be represented by short correlated words. At $n=1000$, a maximum group order already needs about 8530 bits. At $n=21$, $21!>2^{64}-1$.

There is a useful genuine worst-case subgroup information bound. Put $t=\lfloor n/2\rfloor$ disjoint transpositions on the indexed domain; their products form $C_2^t$. Every $k$-dimensional binary subspace gives a different subgroup. The number is the Gaussian binomial

$$
{t\brack k}_2=\prod_{j=0}^{k-1}\frac{2^t-2^j}{2^k-2^j}
\ge 2^{k(t-k)}.
$$

Taking $k=\lfloor t/2\rfloor$ proves that some indexed subgroups require at least $\lfloor t^2/4\rfloor$ encoding bits. Quotienting by conjugation removes at most a factor $n!$, so the corresponding worst-case floor is $\lfloor t^2/4\rfloor-\log_2(n!)=\Omega(n^2)$ bits as well. This counting argument is original to this appendix and does not establish a cubic serialization requirement. Sym($n$) itself has a short symbolic description.

### 3.3 The historical cubic output, now diagnostic-only

For the v1.0 full-transversal format retained as `TRANSVERSAL-1` in specification §9.4, let $r_i$ be the orbit size at fixed-base level $i$, and let

$$
R=\sum_i(r_i-1)
$$

count the emitted nonidentity canonical transversals. If every emitted permutation uses a full $n$-entry image array, the output has exactly $nR$ point entries, besides framing. For $H=\operatorname{Sym}(n)$, $r_i=n-i$ and

$$
R=\frac{n(n-1)}2,\qquad
Z_{32}=4nR=2n^2(n-1)\ \mathrm{bytes}. \tag{2}
$$

This is a tight **A/selected-output-contract** lower bound: the entries really are specified output. Optimising chains, CPU code or GPU kernels cannot remove the obligation while retaining that full-entry output format. Streaming avoids peak allocation but not writing the stream. A different compact canonical group encoding can avoid it.

| $n$ | uint32 point-entry output from (2) | Ideal 89.6 GB/s movement floor | E scenario at 60 GB/s |
|---:|---:|---:|---:|
| 1,000 | 1.998 GB | 22.30 ms | 33.30 ms |
| 10,000 | 1.9998 TB | 22.32 s | 33.33 s |
| 100,000 | 1.99998 PB | 6.20 hours | 9.26 hours |

These are floors/scenarios for crossing the RAM interface, if that crossing is mandatory; persistence can be far slower. The last two outputs cannot reside in the example's RAM or VRAM. Even independently packed point labels retain $nR\lceil\log_2n\rceil$ bits. Since Sym($n$) is known from $n$ and this fixed convention, a symbolic encoding can be tiny; therefore neither (2) nor the packed-entry count is a universal group-information bound. Version 2.0 makes this an explicitly selected diagnostic format. Its production Group(H) encoding uses exact symbolic-product detection and a canonical greedy generating sequence, with O(n² log n) worst-case point entries rather than cubic entries. That is still an output cost to budget, not a universal information floor.

### 3.4 Nested DAG output can be exponential in stored input

Let $x_0$ be a nonempty literal and $x_{i+1}=(x_i,x_i)$, with the pair a typed ordered tuple. A DAG stores $O(k)$ nodes and references for $x_k$; recursively emitting both tuple children produces at least $2^k$ occurrences of the literal. Hence expanded output gives $\Omega(2^k)$ output work and an instance family whose bit complexity is exponential in the compact stored DAG size. This is independent of any permutation search and can occur with the trivial group.

A canonical shared encoding can avoid expansion if equal subobjects receive canonical content identities and sharing is defined extensionally. Raw allocation/DAG node IDs cannot be wire labels, because the specification makes storage sharing nonsemantic. Version 2.0 §4.2 requires canonical bottom-up DAG references, so this exponential occurrence stream is not its production output contract. The same distinction affects auxiliary incidence expansion, traversal, comparisons and checkpoint size.

## 4. Search is the largest unknown term

Let $V$ be evaluated search nodes, $L$ evaluated leaves, and $d$ the longest executed dependent root-to-leaf/refinement path. These quantities must be tied to a profile, an objective and a legal pruning policy. They are not functions of $n$ alone in the present specification.

With trivial refinement and no successful pruning, canonical individualisation has $n!$ leaves. For $n\ge1$, because the final remaining atom is already a singleton, the full tree has

$$
V_{\rm trivial}=\sum_{j=0}^{n-1}\frac{n!}{(n-j)!}
\sim(e-1)n!.
$$

At $n=0$ the root itself is the single leaf/node. For $n=15$, the displayed formula gives 1,307,674,368,000 leaves and 2,246,953,104,076 nodes. At $n=20$, the leaf count is 2,432,902,008,176,640,000. This is an exact **A bound for that unpruned traversal**, and a fallback tree-size upper example. It is not proof of factorial lower complexity for the intended pruned engine: the specification's generator-fixes-object shortcut, symmetry, normalised group orbits, stronger refiners, identical-state certificates and objective bounds can bypass that traversal. Even complete/empty graphs do not force factorial work on the prescribed symmetry-aware engine.

Coset splitting without successful pruning reaches $\lvert G\rvert$ singleton cosets, not necessarily $n!$. The number of distinct images is $\lvert G\rvert/|\operatorname{Stab}_G(x)|$, and symbolic treatment can avoid enumerating even that number. An oracle model requiring comparison of arbitrary unrelated orbit values can force exhaustive minimum search, but that is a black-box assumption; known object structure invalidates the inference.

There is a proved exponential search-tree lower bound for a restricted IR model. Neuen–Schweitzer, Theorem 3.2, construct rigid graph families requiring $2^{\Omega(n)}$ tree nodes for every fixed $k$-realizable refinement operator, selector and invariant in their model, including perfect knowledge of automorphisms. [Local TeX, theorem and definitions](../review_sources/algorithms/1705.03283v1/ir-analysis.tex), lines around `thm:main` and §3; [local PDF](../review_sources/algorithms/1705.03283v1.pdf); [retrieval URL](https://arxiv.org/abs/1705.03283). To apply it here, prove that the selected profile's operators and pruning are inside that model. The specification's custom refiners, native group methods and exact subsearch do not automatically satisfy that restriction. The theorem supplies neither a factorial bound nor a practical seconds-per-vertex constant.

No strong general canonicalisation search lower bound is established for the architecture's unrestricted operator interface. Nor does this appendix claim an unconditional superpolynomial lower bound for ordinary graph canonicalisation. A canonicaliser solves graph isomorphism by comparing two canonical outputs; a universal exponential claim would therefore require substantially different justification from an IR restriction. Schweitzer–Wiebking also stress that object size matters alongside atom count; their recursive canonicalisation theory does not make this practical backtracker asymptotically optimal. [Local TeX introduction](../review_sources/algorithms/1806.07466v2/articles/intro.tex) and [hereditarily finite objects](../review_sources/algorithms/1806.07466v2/articles/hereditarilyFiniteObjects.tex); [retrieval URL](https://arxiv.org/abs/1806.07466).

The general `LEX_MIN_IMAGE(order)` contract has an additional hardness issue. For a simple undirected graph and Sym($n$), encode upper-triangle bits in increasing larger endpoint order: $(0,1),(0,2),(1,2),(0,3),\ldots$, with $0<1$. Its first $\binom k2$ bits are all zero in some relabeling exactly when the graph has an independent set of size $k$. If such a prefix exists, the global lexicographic minimum has that prefix. Thus an exact minimum-image call followed by inspection of the prefix solves Independent Set; this is an explicit polynomial reduction, not a claim about the trace canonical objective. Assuming $P\ne NP$, no general polynomial-time guarantee is possible for all such allowed minimum orders. NP-hardness by itself gives **no numeric bound and no unconditional exponential lower bound**; an ETH-based claim would need its own hypotheses and parameter-preserving reduction.

## 5. Work, span and data movement

For a fixed required computation DAG, let $U_j$ count operations of resource class $j$; $\mathcal R_j^{\max}(p)$ be an upper throughput on the active processors; $B_h$ be bytes that must cross hierarchy boundary $h$; and $S^{\min}$ be a lower bound on DAG critical-path time. Then

$$
T_p\ge
\max\left\{
\max_j\frac{U_j}{\mathcal R_j^{\max}(p)},
\max_h\frac{B_h}{\beta_h^{\max}(p)},
S^{\min}
\right\}. \tag{3}
$$

This is **A+H** when demands and capacities are justified. Operation counts must be mapped to instructions correctly: one SIMD instruction may process many logical words, while a gather, wide integer, population count or multi-limb operation may require several instructions. Two instruction classes sharing a port need combined capacity constraints; taking separate maxima can be valid but weaker. A throughput table for integer addition cannot be used for scatter, branchy sifting or popcount.

The work/span special case is $T_p\ge\max(W/p,S)$ for unit-speed identical processors and fixed unit work. Its ideal speedup ceiling is $\min(p,W/S)$. SMT does not supply another 16 full cores or double all port capacities. Shared memory interfaces are not multiplied by worker count. Real search changes $V,L,U_j,B_h$ with scheduling and earlier discovery of incumbents or automorphisms; compare DAGs only after fixing or logging that work.

If requests have minimum latency $\ell_{\min}$, $D$ necessarily dependent requests give $S\ge D\ell_{\min}$. If at most $q$ requests can be outstanding, the sum of their residence times is at most $qT$, giving $T\ge Q\ell_{\min}/q$ for $Q$ requests. Substituting an assumed average latency and assumed effective $q$ produces an **E model**, not a proved minimum. Interleaving independent point queries can increase $q$; a precomputed composition can remove a dependency chain at preprocessing cost. A lower bound for the unmaterialised word cannot forbid those legal alternatives.

For a concrete **E illustration**, $10^6$ misses at 80 ns give 80 ms if wholly dependent, versus 2.5 ms with 32 independent outstanding requests. Their 64-byte read traffic is 64 MB, whose ideal DDR5 signaling floor is only 0.714 ms. Counting bytes alone misses the limiting mechanism. This does not prove that the misses or their latency are unavoidable for the problem.

Sequential stages with no permitted overlap satisfy $T\ge\sum_a L_a$ where $L_a$ lower-bounds each stage. Within an overlapping stage, do not add compute time and transfer time merely because both exist; use a resource maximum and dependency DAG. Conversely, a required upload → kernel → download chain cannot be replaced by their maximum for a single operation whose result is needed before the next stage.

**Caches matter to the premises.** A logical read is not a DRAM read. At the start of a cold import, at most the initial resident cache bytes can avoid incoming traffic; at completion, output still retained in caches need not yet be written back. Repeated scans of an LLC-resident immutable relation may have almost no DRAM traffic. Include a boundary-state allowance, or explicitly define an interval that streams data larger than cache and drains output. The original specification's “200 MB of irreducible DRAM traffic at 60 GB/s” and “100,000 misses at 80 ns” examples are conditional diagnostics until their irreducibility/rate premises are proved.

## 6. Kernel bounds with legal representation alternatives

The following bounds concern exact kernels, not the whole canonicalisation problem. An explicit array/scan output fixes a stronger obligation than a compact handle. Sparse supports, symbolic full-symmetric groups, precomputed summaries and reusable frozen contexts must be considered before claiming a demand is irreducible.

### 6.1 Permutation composition, inverse and word application

For the array convention, $r[v]=q[p[v]]$. With two arbitrary, distinct degree-$n$, $d$-byte image arrays and a required fresh dense result, materialised composition has worst-case $\Omega(n)$ entry work. The direct full-array kernel reads every $p$ entry, consults every $q$ entry because $p$ is bijective, and writes every result entry: its useful logical data volume is $3dn$, before cache-line amplification. Another legal algorithm can infer a final entry from bijectivity, among other shortcuts, so $3dn$ is an exact direct-kernel ledger rather than an exact universal byte minimum. A cold streamed interval that forces those direct-kernel source reads and output writeback has a $3dn/\beta^{\max}$ movement floor; with cache retention allowances subtract the relevant initial/terminal resident bytes rather than calling $3dn$ mandatory DRAM traffic unconditionally.

The second load depends on the first **for each point**; different points are independent. Gather latency therefore need not yield $n\ell$ span. Inverse construction also needs $\Omega(n)$ entries for a dense output, but uses scatter. Input bijectivity makes inverse destinations unique, avoiding duplicate-write races. Identity/tagged/small-support cases and a composed handle can avoid full materialisation; then a dense-array lower bound cannot apply.

For applying a generator word of length $a$ to $k$ queried points, the direct kernel performs $ak$ image queries and each point has an $a$-query dependency chain. This gives **A** work $\Omega(ak)$ and a conditional latency span for direct application. Materialising the composed permutation once can replace repeated word application with $k$ queries at the cost of constructing and retaining the array. A balanced composition schedule, cached subwords or structure can change both work and span. Account for these alternatives, especially the specification's leaf tuple minimisation: $\Omega(n^2)$ per leaf cannot be asserted merely because the tuple has $n$ entries and the stabiliser chain has $n$ levels.

As an **E example**, $n=10^7,d=4$ yields 120 MB of useful full composition traffic, with all source/result streams charged to a drained cold interval. The ideal 89.6 GB/s interface floor is 1.34 ms; the 60 GB/s scenario is 2.00 ms. Random gathers can cost more. For $n=100{,}000$, all three arrays total only 1.2 MB, so dividing 1.2 MB by DRAM bandwidth is not a suitable warm-kernel floor.

### 6.2 Orbits and Schreier queries

Expanding an orbit of size $r$ by $s$ active dense generators using direct BFS makes $rs$ point-image queries, plus membership/queue operations. That is **A** work of this expansion algorithm. A spanning traversal may expose fewer useful discoveries, but direct BFS still checks its generator edges unless it has a proved shortcut. Reusing a known orbit partition, reducing generators, a symbolic group provider or an orbit certificate can avoid the $rs$ demand. Materialising an arbitrary orbit list still writes $r$ labels; an explicit orbit-ID array over the full domain writes $n$ entries.

An arbitrary Schreier path of length $a$ reconstructed as successive parent/edge records has a dependent path. Selected jump ancestors or a cached word/transport change it. Dense caching can improve many uses while consuming $dn$ bytes per transporter. No unavoidable $O(nr)$ transversal-memory lower bound follows from an orbit of size $r$: the compact tree described by the specification stores only $O(r)$ records, and algebraic structure can shrink it further.

### 6.3 Verified BSGS and tuple minimisation

For chain length $b$, level orbit sizes $r_i$ and active generator counts $s_i$, a direct Schreier closure pass considers up to $\sum_i r_i s_i$ generator edges. Define $E_{\rm Sch}$ as the edges/candidates actually required by the chosen verifier after legal reuse and symbolic shortcuts; define $Q_{\rm sift}$ as actual point queries and $C_{\rm dense}$ as required dense compositions. Its lower-bound ledger should charge those quantities separately. An upper algorithmic expression for candidates is not a universal lower bound on chain construction.

For $n\ge2$, one may take $b\le n-1$; $\lvert G\rvert=\prod_i r_i$, and $R=\sum_i(r_i-1)\le n(n-1)/2$. A tree record can pack a parent/orbit position and a generator selector into roughly $\lceil\log_2r_i\rceil+\lceil\log_2(s_i+1)\rceil$ bits, plus flags. Orbit lists and indexed lookup support add space. Full-domain uint32 lookups at every level add $4nb$ bytes, but sparse maps, rank-supported bitvectors, shared structure and symbolic levels can avoid that exact allocation.

At $n=100{,}000$, Sym($n$) has $R=4{,}999{,}950{,}000$. Even an 8-byte record per nonroot orbit point is about 40 GB, before dense level lookup arrays or generators. A symbolic symmetric-group provider avoids constructing this generic quadratic chain. Thus a lower bound derived from those generic records is a representation obligation, not a reason every Sym($n$) input needs 40 GB.

Dense sifting that rewrites a residue at every level can incur $\Theta(nb)$ entry work per sift. Sifting using only required base-point images, words and cached actions can be cheaper, so the dense rewrite is not a general BSGS lower bound. Exact verification and provenance remain necessary: faster input-generator membership tests do not replace Schreier closure/completeness. The architecture's choice of deterministic reference construction does not determine one unavoidable numerical chain-build cost for all groups. [Local graph-backtracking paper](../review_sources/algorithms/2209.02534v4/paper.tex), group/orbit and canonical-image constructions; [retrieval URL](https://arxiv.org/abs/2209.02534).

Exact group order has the bit length in §3.2. An implementation producing its full binary/decimal stream must pay output cost proportional to that stream; multi-limb multiplication, comparisons and decimal conversion cannot be assigned constant cost for growing $n$. This observation alone does not prove a particular quadratic multiplication cost: the integer algorithm and operand lengths must be specified.

### 6.4 Bitsets and dense refinement

For two arbitrary bitsets of $a$ bits and a required materialised AND output, $K=\lceil a/64\rceil$ uint64 words give worst-case $\Omega(K)$ word work. The direct full AND kernel moves $24K$ useful logical bytes in the explicit 64-bit representation; shortcuts for known zero words or structured inputs can reduce that kernel ledger. A compare can stop at an early mismatch; worst-case equality examines the full data unless a trusted exact equality certificate has already been built. A popcount reduction need not write an output bitset, so its traffic differs.

For $r$ dense relation rows over $N$ vertices, a row-mask count scans $t$ nonzero mask word positions per row, giving

$$
W_{\rm count}=rt,\qquad B_{\rm rows}=8rt. \tag{4}
$$

Here $t\le\lceil N/64\rceil$; zeros can be skipped if their positions are known without rescanning all rows. The splitter mask can stay cached rather than being charged once per row. Count output adds $dr$ bytes when materialised. This **A bound** assumes these row-word operations are necessary for the selected kernel and there is no reusable exact summary. SIMD changes instruction count, not the number of selected row bytes.

Equation (4) does not make every splitter scan $N^2/8$ bytes. A singleton can test one bit per relevant row; a sparse splitter may use incidence traversal; a cached equitable signature can avoid a count pass. The best legal implementation can choose a cheaper path and must charge preprocessing of any auxiliary index. A row-word floor should therefore be reported alongside which path was used, not advertised as a universal dense-refinement bound.

For a broad mask and $N=r=100{,}000$, $t=1563$, so $W=156{,}300{,}000$ and row traffic is 1.2504 GB including 64-bit row padding. On a mandatory streamed interval the ideal DDR5 floor is 13.96 ms, or 20.84 ms in the 60 GB/s scenario. At 40/80 GB/s that scenario changes to 31.26/15.63 ms. These numbers concern **one full count pass**, not a node whose number of passes has yet to be established.

A full-profile higher-dimensional WL table has $N^k$ tuple positions. If its output explicitly stores one $d$-byte color per tuple, that table costs $dN^k$ bytes and producing it writes those entries. This is an algorithm/representation obligation; it does not establish a universal $N^k$ canonicalisation lower bound. Streaming/weaker summaries and the chosen $k$ need their own exact semantic contract.

### 6.5 Sparse refinement and duplicate scatter

Let $E_C$ be the arcs actually visited for splitter $C$ and $u_C$ the distinct touched destinations. Direct exact accumulation must read the selected arc data and combine their contributions: $\Omega(E_C)$ edge visits, plus the required touched-state/output work. In 32-bit CSR, $4E_C$ destination bytes are the useful adjacency stream before offsets, labels and counts. A precomputed exact count/signature summary can shift this work to preprocessing; an already equitably stable state may not need another identical scan.

A random 4-byte counter update can cause a 64-byte line fill and dirty writeback, but **128 bytes per arc is not a lower bound** under optimal layout. Duplicate destinations reuse lines; tiled output ownership, sorting, sparse accumulators and known-zero line creation can remove much of that traffic. For a specified scatter schedule, use its compulsory misses and capacity/reuse argument, or count actual controller traffic. An unavoidable cold distinct-line read contributes at least a line transaction at that boundary; it need not correspond one-to-one with logical increments.

For $E_C=10^8$, the destination stream is 400 MB, so the ideal DDR5 floor is 4.46 ms when the stream genuinely crosses DRAM. The 60 GB/s scenario is 6.67 ms. At the other extreme, a scatter schedule with an actual 128-byte transfer per update moves 12.8 GB and has a 213 ms bandwidth scenario at that same rate. This 32-fold difference explains why representation/locality can matter as much as arithmetic. It is not evidence that either traffic count is attained on the selected CPU.

A smaller-fragment refinement analysis such as $O((N+m)\log N)$ is an **upper/amortised local-work result** under its hypotheses. It is not a lower bound, and must not be multiplied blindly by node count when inherited partitions and reusable summaries change per-node work.

### 6.6 Sorting, exact comparison, rollback and serialization

Sorting $q$ distinct unconstrained keys using comparisons alone needs at least $\lceil\log_2(q!)\rceil$ comparisons in the worst case. This comparison-model bound cannot forbid fixed-width radix sorting, direct binary splits, bucket counting, exact interned keys or trie reuse. Native integer signatures therefore do not have a universal $\Omega(q\log q)$ time requirement on the specified machine. Their required reads, permutation of records and materialised outputs still impose appropriate movement/work costs.

For variable-length exact encodings, hashing cannot decide equality alone. Worst-case comparison of unindexed equal strings must inspect their relevant full content; shared-prefix indexing or canonical DAG identities can reuse that work. The sum of comparison prefix lengths is a ledger for chosen comparisons, not a proof that a global sorter must repeatedly reread every common prefix.

Rollback must recover a sufficient description of changes or reconstruct from a checkpoint; it need not log every intermediate write. If $a$ arbitrary old $w$-bit values become irrecoverably overwritten while the input remains available only through the current state, exact restoration needs that old information somewhere or a recomputation source. In the general family it is $aw$ bits. But swaps can be logged by indices, region snapshots by bytes, and known deterministic updates by a recipe. The optimal time-space trade-off prevents claiming the specification's exact trail traffic is universally necessary. The one-writer partition commit and sequentially dependent refinements do belong in the chosen DAG span.

Every materialised canonical relation/tuple/string stream has its declared output size cost. Losing leaves can compare transformed views and stop early. There is no universal “rewrite the whole graph at every leaf” lower bound. Subgroup/coset objects and extensional DAG expansion require the separate output accounting in §3; they can dominate even a zero-search instance.

### 6.7 Small-instance batch regime

This regime complements the large dense-row examples. Consider B independent small requests sharing a verified immutable group/registry, with each active request's full live state W fitting its assigned cache budget. Degree ≤128 is a useful workload family, not a proof that W fits L1: include DAG/graph construction, chain/provenance, counts, wrapper buffers, output and verification scratch, plus competition from other workers. Warm group reuse does not erase its cold validation/construction cost.

Let U_j be required instruction-resource work for the chosen per-request computation, d its dependent span, M_br the branch recoveries in a fixed replay, r_br a justified minimum nonoverlapping recovery cost, and R_j^max the corresponding service ceiling. Then T is at least max(max_j U_j/R_j^max, S_min), an **A+H** floor, where S_min lower-bounds the critical path using d and only recoveries established to lie on that path. For a serial replay whose recoveries cannot overlap, M_br r_br contributes a span bound. A measured miss rate times an average penalty is **E**, not a universal minimum; a branchless algorithm or different search can change M_br. No TensorGR instruction counts, penalties, cache-byte model or clock numbers are imported.

For the whole batch, define I_B and Z_B as bytes that must cross a declared input/output boundary after reuse, compression and cache boundary allowances. The **U/A+H**, as applicable, streaming floor is max(I_B/beta_in^max, Z_B/beta_out^max); combine demands if they share one bottleneck interface. Even when per-request working data are cache-resident, a long batch can stream input/output beyond cache. Dividing every logical per-request read by DRAM bandwidth is invalid; saying that batch bandwidth never matters is also invalid.

If the application collects canonical terms in a table, collection is an explicit extra operation. For a fixed insertion algorithm needing Q independent or dependent probes with minimum residence time ell_min and at most q outstanding probes, Q ell_min/q is a conditional **A+H** throughput floor, alongside its longest dependent probe chain and required insertion bytes. Sum numerator/denominator big-integer work separately for exact rational collection. Probe count, hash-table collisions and branch recoveries are not mandatory for all legal collectors: sorting, batching, exact interning and better packing can change the algorithm. Per-key serial updates and independent keys have different spans. An assumed latency/occupancy substitution is an **E** model.

For an overlapping steady-state pipeline use the resource maximum plus startup/drain dependencies; for genuinely sequential import → build → solve → encode → collect stages use justified sums. Report cold total, warm per-request latency and full-batch throughput, together with per-stage times, peak W, active workers, root-discrete fraction, actual automorphism structure and collection-table residency. Caller-owned threads with separate workspaces avoid nested pools. A skeleton cache must verify its exact key and coordinate transport, and its construction/amortisation is reported. These are modelling/measurement requirements; no batch timings were taken in this revision.

## 7. Memory capacity and locality admission

For a simple directed relation with 32-bit destination IDs and 64-bit offsets, the specification's formulas are correct:

$$
M_{\rm CSR}=4m+8(N+1),\quad
M_{\rm CSR+CSC}=8m+16(N+1),\quad
M_{\rm dense}=8N\lceil N/64\rceil.
$$

Labels, multiplicities, transpose copies, reference maps and alignment are additional obligations only where the chosen layout needs them. These are storage costs of useful accessible representations, not entropy floors. For $N=100{,}000,m=10^6$, bidirectional unlabelled sparse storage is about 9.6 MB. A broad directed simple-graph family at this size/cardinality needs about $m\log_2(N^2/m)+(\log_2e)m\approx14.73$ million bits, or 1.84 MB, before access indexes; this is an asymptotic sparse-entropy estimate, not a ready-made constant-time adjacency format. One padded dense matrix is 1.2504 GB and its transpose pair is 2.5008 GB.

With the specification's **planning** 48 bytes/vertex/worker, one $N=100{,}000$ worker has 4.8 MB base state and sixteen have 76.8 MB, before trails/group data. That alone exceeds the CPU's advertised aggregate 64 MiB L3. Under the illustrative two-cluster topology, eight such workers need 38.4 MB per cluster before shared input, while a 32 MiB cluster holds 33.55 MB. Private L2 and precise liveness can retain some state, so this comparison predicts pressure rather than proving every access misses. Shrinking or sharing reconstructible arrays is a legal optimization.

For $N=10^7,m=10^8$, the same sparse pair is about 960 MB and sixteen 48-byte worker states are 7.68 GB. That fits nominal RAM before other terms but gives little cache residency. A dense matrix at $N=10^6$ is 125 GB, exceeding this machine's RAM and VRAM even once; this is a limit on that chosen representation, not a proof the graph cannot be canonised by a sparse/streamed algorithm.

Ignoring all scratch and reserving **no** runtime memory, a dense matrix fits nominal 16 GiB only up to $N\approx370{,}727$, or $262{,}144$ vertices if a transpose is also retained. In 64 GiB, the corresponding optimistic limits are $741{,}455$ and $524{,}288$. Padding lowers nonintegral thresholds slightly. Actual limits are smaller because counters, groups, output, runtime/display allocations and in-flight work also live in memory.

Admission should use simultaneous liveness:

$$
M_{\rm peak}=\max_{\rm phases}
\bigl(M_{\rm immutable}+pM_{\rm private}+M_{\rm tasks}
+M_{\rm group}+M_{\rm publications}+M_{\rm output}\bigr)
\le M_{\rm budget}. \tag{5}
$$

Optional caches should not be called unavoidable memory. Serial execution, output streaming and reconstructible recipes can reduce peak memory at added time. Host RAM and discrete VRAM do not form one uniformly accessible 80 GiB pool. A duplicated input consumes capacity on each side; accessing absent device data entails transfer. Auxiliary expansion changes $N$, and compressed semantic DAG size alone does not bound its expanded incidence/output size.

## 8. Optional GPU: integer kernels, transfers and dependency

The specification rightly excludes a GPU runtime from the production requirement. If one is added, assess bulk exact kernels and independent batches first. SIMD/SIMT throughput cannot shorten a sequential search decision or provide arbitrary-group pruning.

The local [CUDA Best Practices Guide](../review_sources/hardware/nvidia_cuda_best_practices.html), version 13.4, §12.1.1, Table 5, states maximum native arithmetic **results per SM clock** and warns that special instruction sequences/compiler care may be required. The [local compute-capability list](../review_sources/hardware/nvidia_compute_capabilities.html) places RTX 5080 in CC 12.0. Relevant rates in the archived table are 128 for 32-bit add/subtract, 64 for 32-bit AND/OR/XOR and 16 for `popc.b32`. [Retrieval URLs: guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#throughput-of-native-arithmetic-instructions), [capability list](https://developer.nvidia.com/cuda/gpus).

Using 84 SMs and the **rated** 2.617 GHz boost gives these optimistic arithmetic-model capacities:

$$
\mathcal R_{\rm add32}=28.14\times10^{12},\quad
\mathcal R_{\rm logic32}=14.07\times10^{12},\quad
\mathcal R_{\rm popc32}=3.517\times10^{12}
\ \mathrm{results/s}.
$$

They are separate ceilings, not simultaneous capacities. The whitepaper's INT32 TOPS rating is not a replacement for instruction-specific analysis; multiply-add operation counting and shared execution units differ from one AND or popcount. Wide popcount/integers may compile to multiple instructions. Native Tensor FLOPs/AI TOPS are not appropriate denominators for exact graph counters, permutation gathers or BSGS sifts.

For the §6.4 broad dense count example, a 32-bit implementation makes two `popc.b32` operations per 64-bit word. Its popcount-only capacity floor at the rated clock is about 88.9 μs. The obligatory 1.2504 GB GDDR row read has a 960 GB/s ideal interface floor of 1.303 ms, so memory is already the stronger optimistic constraint; reductions, loads, register pressure and scheduling can increase time. Different exact counting algorithms require their own operation ledger, so the popcount-instruction floor is kernel-specific.

If the matrix is freshly uploaded and its count result is needed back before CPU refinement commits, a nonoverlapped operation obeys

$$
T_{\rm offload}\ge
\frac{B_{\rm H2D}}{\beta_{\rm PCIe}^{\max}}
+L_{\rm device}
+\frac{B_{\rm D2H}}{\beta_{\rm PCIe}^{\max}}, \tag{6}
$$

with launch/synchronization dependencies added if their minimum costs are established. Pipelined batches can overlap tiles and use a resource maximum plus startup/drain costs; that different DAG may avoid the simple sum. Page-locked buffers/async streams enable particular overlap regimes but do not prove that overlap occurs for dependent CPU partition updates. [Local guide](../review_sources/hardware/nvidia_cuda_best_practices.html), §§10.1.1–10.1.2.

Uploading 1.2504 GB has an ideal PCIe 5.0 ×16 one-direction floor of **19.84 ms**, already larger than the CPU's 13.96 ms ideal DRAM full-read floor. In the E scenario of 50 GB/s transfers and 700 GB/s device reads, the upload is 25.01 ms and device read is 1.786 ms, plus about 8 μs for 400 kB count download and assumed launch cost. A single fresh offload is therefore unattractive in this simple performance model. Comparing these floors alone does **not** prove the CPU wins: actual CPU time may be far above its floor and the two demands may differ.

If the matrix remains resident for $h$ independent or appropriately scheduled broad-mask passes, the upload is amortised. The same **E rate model** predicts an optimistic crossover near two passes: compare $h(20.84\,\mathrm{ms})$ CPU row reads with $25.01\,\mathrm{ms}+h(1.786\,\mathrm{ms}+0.008\,\mathrm{ms})$, before launch/semantic synchronization. This is a model crossover, not an established speedup. For small splitters or serially selected branch masks, transfer/launch/grain overheads can dominate; keep the matrix resident and send only masks/deltas where correctness permits.

If an arbitrary incompressible matrix of size $B$ must be inspected on each dependent pass and available device storage is $M<B$, at least $B-M$ matrix bytes are absent at pass start. They must enter/access the device during that pass, barring a reusable exact summary or changed computation. Independent pass batching can reuse a tile before eviction and invalidate a per-pass retransmission premise. The capacity/transfer bound must state this dependence and residency explicitly.

GPU random gathers and duplicate scatter need their own models. Coalesced memory throughput is not a rate for 32 unrelated lane addresses; equal destinations also create races unless increments are combined or otherwise synchronised. Hundreds of independent warps can hide latency, but occupancy and useful parallel work constrain that ability. No GPU FLOP figure establishes an advantage for the CPU's dependent Schreier walks.

## 9. Conditional end-to-end numeric envelopes

A useful implementation-facing model records, for each node $v$, splitter edge/word work, orbit/sift queries, unavoidable dense materialisations, comparison bytes, trail/snapshot movement and critical dependencies. Aggregate these over the actual legal traversal, including initialization, preprocessing and final output.

For a single uniform node class with irreducible boundary traffic $B_v$, parallel work $U_v$ and dependent path cost, a fixed-work envelope is

$$
T_p\ge\max\left\{
\frac{VU_v}{\mathcal R^{\max}(p)},
\frac{VB_v}{\beta^{\max}(p)},
S^{\min}
\right\},
$$

with setup/output sequential-stage floors outside or inside the complete DAG as appropriate. **Uniform traffic and node cost are strong conditions**; tiny losing nodes and expensive leaves should not be averaged into invented universal costs.

| Hypothetical demand | Optimistic/interface floor or E envelope | What must be established |
|---|---|---|
| $10^6$ nodes, each forcing 200 MB of DRAM traffic | $2232$ s at the ideal 89.6 GB/s; E: $3333$ s at 60 GB/s | The 200 MB is genuinely irreducible DRAM traffic per node after optimal reuse, and this many nodes are legally necessary. Core count cannot multiply the shared DDR rate. |
| $10^8$ nodes, each 1 μs of one-core equivalent compute | E: at least 6.25 s ideal work division over 16 cores | The 1 μs is a calibrated fixed-work quantity or stated scenario, not a universal node lower bound; include span and memory. At 10 μs the envelope becomes 62.5 s. |
| $10^5$ dependent misses at 80 ns | E: 8 ms path | A mandatory dependent miss path and appropriate loaded latency; materialising transporters can change it. |
| One broad dense count at $N=10^5$ | Ideal CPU DDR read 13.96 ms; ideal resident GPU GDDR read 1.303 ms; fresh upload alone 19.84 ms | Same mandatory operation; stream residency and transfer DAG declared. |
| Sym($10^5$) using diagnostic full-transversal uint32 output | 1.99998 PB must be emitted; ideal RAM-interface floor 6.20 hours if the entire output crosses it | This exact expanded output contract. A symbolic format removes this obligation. |

The first row is not a prediction that a real node will move 200 MB. Its purpose is to show why a proved byte demand, once available, can impose an end-to-end wall-time floor. Without a justified per-instance necessary-work argument, even for the frozen P1, the specification cannot support a credible target such as “within twice the absolute minimum runtime.”

## 10. Tightness and performance gaps

Let $T^*$ be the fastest legal time on the declared input, hardware, output contract and algorithm scope. A valid lower bound $L\le T^*$ and observed time $T\ge T^*$ define:

$$
\Delta_{\rm floor}=T-L,\qquad
\rho_{\rm floor}=T/L,\qquad
\delta_{\rm floor}=(T-L)/L,\qquad
\eta_{\rm floor}=L/T\quad(L>0).
$$

These are the absolute time above the bound, ratio to the bound, fractional gap, and fraction of runtime accounted for by it. For zero or unavailable L, ratios are undefined; investigate a measured T<L rather than clamping the gap. They do not equal the gap to an optimum. In fact

$$
0\le T-T^*\le T-L,\qquad
1\le T/T^*\le T/L.
$$

Thus $T-L$ is an **upper bound on potentially recoverable time**, not proof that all that time is waste. A certified $T\le cL$ does establish $T\le cT^*$ in the same scope/model. A ratio to an E model cannot establish this. Label whether $L$ is universal, profile-constrained, output-format-constrained or DAG-constrained; a DAG bound cannot certify optimality among algorithms that choose a different DAG.

Streaming copy/bitset kernels over data much larger than cache can approach a sustained memory rate; this makes their movement models reasonably tight after calibration. An ideal signaling limit is less tight because it omits mandatory protocol effects. Irregular gathers, conflicting counter updates, incomplete SIMD lanes and search dependencies generally leave a larger gap. A maximum of resource floors assumes favorable overlap; incompatible resource schedules can require a larger time. An entropy floor ignores decoding/index cost. An output-size floor can be tight for emission while saying little about computing the required bytes.

Strong pruning often changes work by orders of magnitude. Fewer nodes at higher per-node cost can be the superior algorithm; a highly efficient kernel cannot certify an efficient canoniser. Conversely, matching a profile's required full output floor may show excellent engineering of a poor production output contract.

## 11. Benchmark gates that can support a performance claim

These are proposed gates, not completed tests. They supplement specification §19 without pretending there is one universally optimal canoniser.

1. **Fix the comparison contract.** Pin hardware, topology, clocks/power conditions, compiler/build, versions, object action, objective, canonical profile, encoding and completion status. Compare CPU-only production against CPU baselines. Report GPU runs with their runtime dependency and complete host/device costs.
2. **Separate cold, warm and amortised work.** Report validation, BSGS/normal-form setup, input conversion, solve and final emission. For $h$ solves reusing a group, report setup once plus all solves and the amortised contribution $T_{\rm setup}/h$; do not omit it from cold time.
3. **Collect a reproducible demand ledger.** Count nodes/leaves, arcs/words examined, distinct materialised permutations, sift/orbit queries, comparison bytes, retained state and task-transfer work. Distinguish logical byte counts from measured cache/DRAM/controller transactions. Fixed-work replay separates hardware scaling from search changes.
4. **Calibrate only representative kernels.** Sweep arrays across private/shared cache and DRAM, core counts and splitter density. Measure read/write/random-update bandwidth, loaded dependent latency and independent gathers. Use an explicit short calibration command and record variance; it is not part of semantic canonicalisation. Document that the resulting rate is empirical, not a certified capacity maximum.
5. **Use broad instance families and censored statistics.** Include easy sparse/dense cases, restricted/native groups, large object/auxiliary growth, weak-refinement rigid families, repeated components and subgroup/coset presentations. For timeouts report solved counts and a declared penalised/censored statistic over the same suite. Preserve per-family distributions rather than only an average over solved instances.
6. **Require correctness before speed.** Independently establish small-instance canonical/minimum/stabiliser answers, witnesses and complete group orders; test identical profiles across schedules/ISAs. A valid witness alone does not prove the incumbent canonical or the stabiliser complete.
7. **Set regime-specific acceptance targets.** As engineering targets, a large regular kernel might aim for at least 70% of the separately measured matching-stream rate, and task transfer might aim below 5% of useful task time. Those percentages are proposed gates, not lower-bound theorems. Require end-to-end improvement with uncertainty/repetition, memory impact, node/work inflation and no material declared-regime regression; numerical threshold choices belong in a versioned benchmark policy.

A performance claim can responsibly say “this full count kernel is within $c$ of the stated interface floor on these sizes” if its demand is proved and the timing boundary matches. It can say “faster than these pinned competitors on this suite” after fair completed measurements. It cannot infer “optimal canonicalisation,” a universal exponential lower bound, GPU acceleration of CPU DFS, or a fixed multicore speedup from those observations.

The revised specification selects P1 and CDAG-2. The next implementation steps are independent reference agreement and instrumentation of exact group/refinement/search demands, as gated by the implementation plan. That converts the presently conditional envelopes into auditable per-instance algorithm bounds. The substantial universal limits remain input validation, bit/output size and any separately proved restricted-model hardness; no current engineering argument closes the gap to a general optimal canonicalisation algorithm.
