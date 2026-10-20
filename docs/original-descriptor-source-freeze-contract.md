# Original descriptor Source freeze /1

Status: Contract Complete and Prototype Complete for the cold ownership/content
boundary below. Verified host15/15, Wasm/browser8/8 and inventory181/181.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/original-descriptor-source-freeze-validation.yaml`. Canonical positional integration and native execution
remain DiagnosticFailure on Null, Vulkan, Metal and WebGPU.

An explicit Prototype Source adopts the exact original core-owned DescriptorSet
or DescriptorTable only after all bounded cold allocations and validation succeed.
Reuse `ColdOwnedObjectSlot`, quota-bound shared identity, `BoundedSequence`, the
existing original registry and the actual arena epoch. Failure preserves the
caller's unique object and releases every new Source allocation. The Source
retains the exact core shared original arena and the existing canonical resource
inventory; it creates no inventory, root, seal, command graph or journal.

The Source quota must be the same owning quota as the retained resource inventory.
Its explicit version/opt-in budget bounds logical capacity, written elements,
exclusive retained owner contributions and bytes. The six exclusive contributions
are Source, adopted wrapper, its registration, arena, arena registration and the
existing resource inventory. Resource owners and the shared registry/device
closure retain their existing independently bounded contracts; six is not a
complete graph owner count and cannot authorize graph admission.

Freeze the complete layout/table definition, allocation success/index/capacity,
registration generation and final content revision. Every currently written
logical element must occur exactly once in the caller's explicit declarations;
omitted writes reject, while genuinely unwritten slots remain optional. Capture
each write from the private successful-write observation and compare every field
to the declaration before adopting ownership. Keep exact canonical resource
indices, registration generations and explicit set access/ranges; reuse
`OriginalCommandTypedSourceContract` for usage/view-parent/layout/state/access
validation. Sampled tables retain the existing declared read contract. Sampler,
storage tables and enabled physical lowering remain deterministic failures.

Hot validation checks the exact Source control block, quota, registered facade,
arena parent/epoch, frozen allocation/definition/revision, all weak write receipts
and the original registered resource inventory. It allocates nothing, performs
no IO and never reconstructs content. A successful identical write, reset,
retirement or foreign facade rejects. External serialization remains mandatory.
The adopted object is exposed only as const; this API creates no native proof.

| Backend | Cold original ownership/content closure | Positional/native consumption |
| --- | --- | --- |
| Null | Equivalent CPU closure, validator only | DiagnosticFailure |
| Vulkan | Equivalent CPU closure over genuine original wrappers | DiagnosticFailure |
| Metal | Equivalent CPU closure over genuine original wrappers | DiagnosticFailure |
| WebGPU | Equivalent CPU closure over genuine original wrappers | DiagnosticFailure |

Tests cover genuine original set/table owners and resource dependencies, missing
or duplicate written elements, changed complete writes, wrong indices/generations,
ordinary/foreign/forged ownership, same-valued foreign arena, immutable metadata,
cold allocation failure with caller ownership preserved, exact/one-less/exhausted
quota, successful/failed writes, reset and retirement. Preserve quick contract,
Null hidden-frame and both browser smoke paths. No shader or visual behavior
change; do not infer Backend Validated, Platform Validated or Production Ready.

The quick fixture reuses the genuine registered resource inventory already built
by `OriginalCommandTypedSourceContractFixture`. Null freezes a complete array;
the real WebGPU browser probe explicitly authors two scalar storage bindings and
an optional partially written sampled table. An independent required-array test
checks actual `RHI-DESCRIPTOR-PAYLOAD-CREATION-FAILED`, preserving prior revision
and leaving the rejected array element unwritten. The C binding lacks native
array payload fields; no array capability or canonical execution is promoted.
Cold Source peaks are host set772/table448; Wasm Null array616, WebGPU scalar652,
and table316. Exact/one-less/exhausted attempts retain caller unique ownership
on failure. Both browser modes require Null and WebGPU freeze markers, the array
failure marker, previous real map/publication checks and empty diagnostics/errors.

The frozen owner is a prerequisite and is not yet an alternative canonical
Source graph. `OriginalCommandResourceSources::Source`, Usage, Inputs, state
validation and owner-union projection still need one coordinated typed identity
and positional contract. Pipeline immutable artifact/full-definition and Query
provenance remain missing. Never return execution authority from this cold object.

A fresh current owned Source also rejects actual original-device retirement with
`RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED` under exhausted Source quota and disabled
std PMR. The updated quick/hidden pair passes2/2 and both repeated browser
modes pass8/8 with this negative included. The prior current Source is validated
before retirement; this assertion does not rely on an already stale epoch.
