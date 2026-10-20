# Original immutable shader artifact owners /1

Status: Contract Complete / Prototype Complete for CPU publication custody below;
verified 2026-10-05 host17/17, Wasm/browser8/8 and inventory184/184. Pipeline artifact consumption and canonical typed execution
remain DiagnosticFailure.

`SingleEntryShaderArtifact` and `GraphicsShaderModuleArtifact` remain the existing
strict cold loaders and immutable byte owners. After all current role, path,
hash, YAML schema, row-major, selected-entry, reflection and explicit target
diagnostic checks succeed, publish the original object through
`core::QuotaBoundSharedObjectIdentity::shareOriginal`. Allocate the object,
metadata, retained bytes and shared custody under the one authored loader quota.
Retain the original validated publication YAML bytes as well as all existing
source/target/reflection bytes; its SHA identity refers to those exact bytes.
Do not reconstruct bytes, add a parallel artifact registry or create substitute
metadata. Remove duplicate stored quota owners when the core base already
retains that exact quota. Construction keys and mutation remain loader-private.

An allocation-free `validateOriginal` requires the exact published object and
control block. Null, borrowed/no-op, forged unrelated owners and non-owning
aliases fail with a stable original-owner diagnostic. A graphics module must
also retain the exact valid vertex and fragment publications on the same quota
object/control block. Loader failures, including shared publication allocation,
return no partial owner and release all quota charges. The caller may retain a
child after releasing the module; child bytes remain owned until final release.
Inspection is memory-only and remains valid after the authored files disappear.
Retain the authored CPU quota ceiling in each immutable object. Changing that
ceiling (including disabling it with zero) invalidates original admission until
the original limit is restored; pointer identity alone cannot prove a budget.

This CPU custody contract applies equally to Null, Vulkan, Metal and WebGPU:
Equivalent immutable ownership; pipeline consumption and typed GPU execution
remain DiagnosticFailure. Explicit target unavailability remains unavailable;
publishing a valid artifact does not create that missing target. Neither public
hydration flags, cache hashes nor this shared-owner proof prove native pipeline
byte consumption, mathematical shader accesses or completion. The next contract
must bind the artifact to the actual issuing factory and its selected targets.

Reuse the existing quota/control-block and shared-owner projection helpers.
Tests must cover genuine owners, forged aliases with the same pointer, no-owner
aliases, exact/one-less/exhausted construction and retry, independently bounded
file/aggregate bytes, unchanged target diagnostics, hostile ambient std PMR,
allocation-free validation under exhausted quotas, memory-only retained bytes,
child retention and final quotas0. Preserve fast contract and Null hidden-frame
paths and both WebGPU browser modes with their prior native map/publication
checks. No shader source or visible output changes; no Backend Validated,
Platform Validated or Production Ready promotion.
