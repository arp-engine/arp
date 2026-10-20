# Original buffer declaration custody /1

Status: Contract Complete / Prototype Complete for c2b4f2b immutable binary
declaration custody. Host24/24, additional rehashed-binary loader1/1,
Wasm/browser8/8 and inventory204/204 pass. C++/Python cross-validation covers15
binary tests,32 real publications and255 hostile reflection cases. Evidence:
build/web-bindless-apple-debug/artifacts/bindless-continuation/
buffer-declaration-custody-validation.yaml. Not Backend Validated / Not Platform
Validated / Not Production Ready; no target minimum or native slice execution.

Reuse boundaries: SingleEntryShaderArtifact already retains original publication,
source, SPIR-V, MSL, WGSL and reflection bytes under its original quota-bound
shared identity. GraphicsShaderModuleArtifact already retains exact per-stage
SingleEntry children. SpirvInstructionView already supplies allocation-free
framing/definition/decoration lookup. Extend these boundaries; do not introduce
a separate owner, seal, source union, journal, root or proof issuer. Preserve the
existing Recording and Submission native issuers.

Approved serialization: SingleEntry publication3 has all targets, publication4
has the existing explicit WGSL diagnostic role. Their file-role contracts remain
the existing /1 and /2 respectively. Legacy publication1/2 has no declaration
section and its query fails with SHADER-ENTRY-BUFFER-LAYOUT-UNAVAILABLE. Versions
3/4 require bufferDeclarationLayout (type SpirvBufferDeclarationLayouts,
contractVersion1, status DeclarationOnly, spirvSha256, bindings). Reject extra
section/record fields. Records contain set, binding, spirvId, blockTypeId,
storageClass, hasRuntimeArray and exact binary declaration quantities. All uint64
quantities use canonical 16-digit hexadecimal strings with Hex suffixes, reusing
core::YamlContract::hex64 rather than lossy signed YAML integers. Record lookup
must validate the same artifact's original shared owner and quota limit first.

SPIRV-Reflect block size/padding remains tooling evidence only: it is deliberately
excluded from the runtime section because this phase has no binary-equivalent
proof of its padding policy. The loader verifies every serialized runtime field
against a reusable cold iterative SPIR-V declaration verifier. It must also join
each record with the existing reflection set/binding/type/cardinality, rejecting
missing/extra buffer bindings. Facts are stored in the existing artifact's core
PMR vector. The cold verifier's traversal allocations use that load quota; hot
lookup never runs the verifier. Graphics modules consume their original children
without an independent combined-module footprint claim.

Implementation order:

1. Define a versioned YAML declaration-layout section and explicit publication
   availability. A declaration layout is not a target buffer binding footprint.
   Legacy publications without the section retain whole-buffer semantics and
   cannot supply a successful declaration-layout query. Unknown versions,
   ambiguous keys, missing fields or facts with invented native minima fail.
   Preserve the independent explicit WGSL-unavailable publication contract.
2. Extend the shader module's binary layout validator, reusing
   SpirvInstructionView. Cross-check every accepted buffer descriptor's variable
   id, pointer/storage class, block type, cardinality, occupied extent and
   runtime tail against original SPIR-V bytes. Check serialized SPIR-V identity
   against the exact retained file role. Tool-reported padding is a separate
   declaration fact; it cannot be promoted to a native minimum. Do not trust
   publication hashes alone to certify authored metadata semantics.
3. Store only validated fixed scalar records in the existing immutable artifact
   using core::pmr containers and the original cold metadata quota. Bound cold
   traversal and allocation by explicit load-budget/input relationships. Failure
   must clean up partially acquired metadata under the same quota. All hot
   queries are allocation0, memory-only and return stable diagnostics without
   caller-supplied facts. Returned copies confer no custody or execution authority.
4. Extend the existing real artifact loader fixtures with hostile declaration
   metadata, including rehashed reflection/publication files. Independent field
   changes, absent/wrong SPIR-V identity, wrong type/cardinality/stride/extent,
   duplicates and missing bindings must reject. Delete the publication files
   after successful load and verify memory-only facts under hostile default PMR
   and exhausted quotas. Keep assertion-enabled quick and Null hidden-frame
   paths; preserve both real WebGPU browser modes and earlier flags.

Backend outcomes for declaration facts: Null remains ValidatorOnly. Vulkan may
later consume the validated SPIR-V declaration but has no new native issuer.
Metal requires MSL-equivalent offset/length/extent semantics; WebGPU requires
WGSL-equivalent layout and genuine minimum binding size. Both must default to
DiagnosticFailure while these proofs are absent. A SPIR-V uniform scalar's
occupied4/reflected-padded16 and a runtime array's block-size0/stride4 are
deliberately distinct facts. None individually authorizes a native range.

f2c is a subsequent explicit request2 contract joining frozen original device
limits with validated target layout/minimum/length provenance. Static base and
dynamic offset must each satisfy queried alignment; unchanged authored size
must fit resource bounds and proved minimum/maximum. Access coverage remains
independent. f3 lowering and f4 canonical Source/Inputs/state joins remain open;
the final version2 descriptor write gate stays closed until those phases pass.

No shader source or visual output change is planned for this metadata phase.
RenderDoc unavailability remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no Platform or
Production promotion follows from host/Wasm declaration validation.
