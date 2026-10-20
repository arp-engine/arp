# Buffer declaration type layouts /1 (f2c2a)

Status: Contract Complete / Prototype Complete for the declared DAG schema and
verifier grammar. Not Backend Validated; Not Platform Validated; Not Production
Ready. This extends the existing SPIR-V and WGSL declaration
verifiers, before original target custody and cross-language lowering. Neither
type graph nor comparison grants original, minimum, access or native authority.

Use a reusable shader-domain DAG with core PMR nodes, ordered struct-member
edges and binding roots. Callers explicitly budget nodes, member edges and roots;
there are no guessed capacities. Child nodes precede parents, aliases are
resolved, and repeated types may share nodes. Fixed array elements are not
expanded, even for enormous lengths. Existing iterative parsers and checked
range arithmetic remain the only sources of graph facts. No duplicate parser.
Graph binding roots are declaration indices, not canonical ownership or sealed
execution roots. The arena is move-only so implicit PMR copy construction cannot
select a default STL memory resource. It adds no source family or native issuer.

Nodes retain scalar category/width, vector components, matrix rows/columns/
stride and source encoding, fixed-array count/stride, runtime-array stride and
element shape, ordered struct offsets and children, and declaration occupied
extent. WGSL additionally records known natural/explicit member alignment,
reserved size and the target SizeOf with a runtime array replaced by one
element. SPIR-V absence of target padding/alignment remains explicit unknown;
occupied bytes must never be relabelled padded size or target minimum.
Root records retain physical/logical declaration address as emitted, storage
class and complete descriptor cardinality; no physical/logical join is inferred.
Descriptor-array dimensions are separate non-byte-array nodes; rank and ordered
dimensions remain available even when their total cardinality matches.

Matrix encodings remain distinct: SPIR-V ColMajor decoration versus WGSL native
matrix column vectors. WGSL generated row-array wrappers remain real structs/
arrays in the graph; names cannot authorize unwrapping. Neither equal bytes nor
equal dimensions proves numeric equivalence, and no matrix execution is added.
WGSL atomic and ordinary integer types also remain distinct. SPIR-V integer
declarations do not prove atomic execution semantics. No implicit conversion.

The graph supports cold exact representation comparison by iterative pairs,
including complete member metadata and source encoding. A successful comparison
proves structural equality under this contract only; it does not prove
cross-target semantics. Reused nodes with different allocation/index/sharing
patterns must compare structurally, never by numeric node IDs or names.
Comparison has an explicit pair budget; malformed roots or exhausted budgets
fail with output false. No implicit unbounded work.
Exact comparison and the approved SPIR-V/WGSL declaration relation now share
visitPairs. Its default pair resolver is identity; only a separately approved
relation can supply an explicit structural transformation plan. Exact compare
still preserves all wrappers and metadata. The relation's consumed array rules
are specified in spirv-wgsl-buffer-memory-lowering-contract.md; they confer no
original or native authority.

verifyWithTypes rejects and clears both facts and graph on any parse, framing,
type, overflow, quota or graph-budget error. Ordinary verify preserves its
existing behavior and does not construct an unnecessary graph. Type-graph work
is cold only; future original lookup must read immutable facts allocation0.

Tests require same-sized type/member/array/stride/encoding disagreements,
nested/fixed/runtime/matrix/vector3 shapes, distinct occupied/padded quantities,
deep iterative traversal, repeated/shared node shapes, authored budgets and
hostile PMR/exhausted quota cleanup. Real Slang target bytes, original binary
facts, Null hidden frame and both browser feature modes are required. All four
native slice gates stay DiagnosticFailure; Null validation is not native proof.
RenderDoc tool unavailability stays explicit, with no Platform/Production claim.

Verified 2026-10-05: host29/29, Wasm/browser10/10, inventory204/204. The
cross-language suite checks binary17, real SPIR-V publications32 and hostile
reflection255. WGSL target graphs check accepted28, descriptor-array diagnostics2
and unavailable targets3. Full type/member/root evidence retains33 publications
including the existing Slang minimum probe. Both browser modes require
bufferDeclarationTypeLayoutsPassed and preserve every earlier true flag with
diagnostics/nativeErrors empty. Same-size scalar signedness mutation, member
order, array rank, per-member matrix stride, known padding versus unknown,
alias/node sharing, deep1500 traversal, explicit node/member/root/pair budgets
and hostile allocator/quota cleanup pass. No shader source or visible output
changed. Original target custody, stage joins and request2 remain incomplete.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
buffer-type-layout-validation.yaml. Continue original physical binding joins.
