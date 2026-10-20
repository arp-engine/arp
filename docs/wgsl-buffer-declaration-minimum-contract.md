# WGSL buffer declaration minimum /1

Status: Contract Complete / Prototype Complete for the declared /1 grammar.
Backend Validated for the selected WebGPU minimum/error/slice observation only.
Not Platform Validated; Not Production Ready. This is the f2c1 prerequisite; f2c original
custody, stage joins, request2 and all native slice gates remain incomplete.

The authoritative rules are WGSL 14.4.1–14.4.5 and 13.3.4, and WebGPU
“minimum buffer binding size”. The inspected W3C WebGPU revision is
`8c9399eba2a810daeab11f0aebd41078f627cbce`. A buffer's target minimum is
`SizeOf(storeType)` with a runtime array replaced by one element. This includes
target padding and is independent of SPIR-V occupied bytes or tooling padding.
For storage runtime arrays, length is `(effectiveBindingBytes - arrayOffset) /
arrayStride`, using integer division. No minimum proves an execution access
footprint, SPIR-V/target equivalence, static entry-point use or original authority.

`WgslBufferDeclarationLayoutVerifier` consumes exact borrowed immutable WGSL
bytes and an explicit binding budget. Cold parsing uses core PMR storage and
iterative type traversal. It returns only declaration records: physical group,
binding, Uniform/StorageRead/StorageReadWrite, AlignOf, minimum binding size,
runtime array flag/offset/stride. Rejection clears all output. No record confers
pipeline, resource, Source, Inputs, State, native or submission authority.

The /1 grammar supports scalar i32/u32/f32, enabled f16, vector/matrix generic
and shorthand types, atomic i32/u32 in storage, fixed arrays with positive
unsuffixed decimal literal lengths (including exact i32/u32 literal constructors
within their respective ranges), direct runtime storage arrays and final
runtime members of a top-level struct, named aliases/structs, and explicit
member @align/@size with positive decimal literals or literal constructors. Alignments must
be powers of two and at least the natural alignment. Explicit size must be at
least SizeOf and cannot apply to a runtime member. Duplicate names/members/
bindings/attributes, cycles, unresolved names, overflow and nested runtime
arrays fail deterministically. Uniform constraints include 16-byte array
stride, required member alignment and nested-struct separation; scalar uniform
u32 still has minimum4. Matrix layout facts describe WGSL column vectors only;
they cannot certify C++/Slang matrix byte or numerical agreement.

Unknown reachable types, bool in host buffers, descriptor binding arrays,
buffer-view types, override/const-expression lengths, nonliteral layout
attributes, unknown extensions and uniform_buffer_standard_layout explicitly
fail. No support is guessed. IO attributes in unrelated structs do not confer
buffer layout authority; a reachable IO member fails. Functions and nonbuffer
initializers are opaque ASCII regions with checked comment/delimiter framing.
This verifier is not a whole WGSL validator: native shader compilation must
independently succeed before any eventual native execution. All module buffer
declarations are returned; an entry-point interface and every graphics-stage
consumer must be joined separately, never inferred from this module inventory.

The next custody phase must join exact original WGSL role/hash and immutable
physical binding maps to original SPIR-V declaration identities/cardinality.
Graphics must join vertex and fragment independently. Explicit request2 must
then validate authored base and frozen dynamic offset separately against original
queried alignment; unchanged size against the proved target minimum, queried
maximum and original buffer bounds. /1 remains whole-only. Caller copies and
native minBindingSize0 cannot issue those proofs. No new owner/root/seal/native
issuer is permitted.

Four backend conclusions for /1: Null is validator-only; Vulkan and Metal have
DiagnosticFailure for this WGSL proof and require their own original target
lowering/length contract; WebGPU has declaration calculations only and
DiagnosticFailure for execution until original custody, device capability,
physical bindings, Source/Inputs/State and f3 native payload proofs are complete.
The existing version2 write gate continues to reject before mutation on all four.

Validation requires fast contract tests (including hostile output clearing,
overflow and deep types), actual Slang WGSL declaration fixtures, Null hidden
frame, and headless WebGPU checks of minimum-size rejection and bound-region
element reads. Native arrayLength confirmation remains a separate gate: Slang
2026.17 diagnoses StructuredBuffer.GetDimensions as unavailable for Metal.
Browser fixture WGSL is a negative/semantic test input, not production shader
authoring. No production shader sources or visible output are changed.
RenderDoc tool unavailability is recorded as RHI-RENDERDOC-TOOL-UNAVAILABLE;
this does not establish Platform Validated or Production Ready.

Verified 2026-10-05: host27/27, Wasm/browser9/9, inventory204/204. Real
publications: accepted28, descriptor-array diagnostic2, unavailable target3
(including the new Slang probe with SPIR-V/MSL/WGSL/reflection). Quick verifier:
49 negative cases, deep1500 types, hostile PMR/exhausted-quota cleanup. Both
browser modes confirm minima4/48/4, early minimum errors3, deferred size44 versus
required48 error1, and exact authored base plus dynamic offset result47. Earlier
true flags remain true; diagnostics/nativeErrors empty. Runtime native length
and original custody remain unproved. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/wgsl-buffer-minimum-validation.yaml`.
