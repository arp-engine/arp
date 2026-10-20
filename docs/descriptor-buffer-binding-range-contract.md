# Descriptor buffer binding range /2

Status: Contract Complete / Prototype Complete for c2b4f1 schema, scalar interval
resolution, frozen identity and closed write gates. Backend Validated only for
actual WebGPU pre-native rejection; native subranges are unproved. Not Platform
Validated / Not Production Ready. Host20/20, Wasm/browser8/8 and inventory204/204
passed; evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
buffer-binding-range-validation.yaml. Explicit native subranges remain open.

L2 semantics: each DescriptorBindingWrite carries a fixed scalar
DescriptorBufferBindingRange. Version1 is the canonical whole-resource binding:
offsetBytes=0 and sizeBytes=0, where version1 explicitly defines the symbolic
whole-resource size. It never means a remaining tail. Version2 authors a finite,
nonempty byte interval [offsetBytes, offsetBytes+sizeBytes) of a Buffer resource
with UniformBuffer/StorageBuffer/ReadOnlyStorageBuffer type. Unknown versions,
noncanonical version1 fields, zero version2 size or version2 non-buffer domains
are deterministic failures. Version2 is an explicit schema opt-in; it cannot
implicitly change a version1 write. Non-buffer writes carry canonical version1.

At consumption, version2 uses the same authored size and adds the frozen dynamic
offset to its authored base offset. Both the static and shifted intervals must
fit the actual retained buffer size without unsigned overflow. Version1 permits
only dynamic offset0. Use core::containsUnsignedRange; validate the dynamic
offset within the remaining static-base extent before forming the effective
offset. Resolve produces fixed offset/size values only on success and clears
output on failure. Resolving scalar facts grants no resource/lifetime/owner,
shader-footprint, queried alignment/limit, state or native execution authority.

All successful-write receipts, write comparisons and frozen original Source
elements must include version, offset and size verbatim. Reuse existing
descriptorBindingWriteMatches; remove Vulkan's duplicated write equality loop.
No new owner, root, journal, seal, source union or native proof issuer is needed.

Implementation order: f1 adds schema, deterministic range arithmetic and the
shared non-overridable write gate, preserving every old whole-buffer contract.
For this prerequisite all four backends reject version2 writes with
RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING before backend mutation. Null
scalar-range calculation is ValidatorOnly; it is not a successful descriptor
write or native proof. Vulkan/Metal/WebGPU have no approved subrange payload at
this stage. Version1 delegates to each existing backend and its existing gates.

f2 must establish immutable original limit and shader footprint provenance and
validate exact static/dynamic alignment and maximum/minimum binding bytes. f3
must define and implement four-backend lowering before removing any write gate:
Vulkan offset/range must be explicit; WebGPU buffer offset/size and dynamic
commands must consume the same frozen range; Metal must prove exact size
semantics (direct encoder offset alone is insufficient); Null must validate the
actual resource size. Lack of a proved mapping remains DiagnosticFailure, with
stable labels and capability false, never a successful placeholder or guessed
remaining tail. f4 joins these fields to original Source/Inputs/state contracts,
retaining /1-/2 and incomplete /3 execution gates until their proofs are complete.

Quick tests: schema versions/domains, whole/dynamic behavior, exact-end intervals,
zero/out-of-bounds/overflow cases, independently altered identity fields, cleared
failure output, allocation0 under hostile default PMR/exhausted core quota, and
real Null write rejection preserving revision/old successful-write observation.
Both browser modes must verify actual WebGPU writes reject before native creation
and preserve every prior flag. Keep Null hidden-frame and source inventory; no
shader or visual output changes. RenderDoc availability remains a recorded
diagnostic, with no Platform Validated/Production Ready promotion.
