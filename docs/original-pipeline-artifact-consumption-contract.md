# Original artifact compute consumption /1

Status: Contract Complete / Prototype Complete for the private cold compute
source factory below; verified 2026-10-05 host17/17, Wasm/browser8/8 and inventory186/186. Genuine L2 factory registration and
canonical typed execution remain DiagnosticFailure.

This receipt proves byte consumption, not asynchronous native creation
acceptance. `RHI-ORIGINAL-ARTIFACT-CREATION-RECEIPT-MISSING` remains the creation
gate; reuse the existing native error-scope batch and completion evidence before
the next L2 factory join. Ordinary pipeline observation rejects artifact-input
receipts with `RHI-WEBGPU-ORIGINAL-ARTIFACT-PIPELINE-ORDINARY-BRIDGE`, without
allocation. A successful source receipt cannot enter old transaction adapters.

Consume a genuine OriginalSingleEntryPipelineInput /1 whose selected stage is
Compute. Require explicit version, Prototype opt-in, nonzero target-byte budget
and independent native CPU metadata quota. Target-byte budget counts exactly
the retained WGSL source passed to shader creation, not a guess at driver GPU
allocation size. The quota must be distinct from input and artifact quotas;
native payload, layout metadata, stage facts and issued receipt use that quota.
Construction is cold only. Zero/one-less/exhausted budget returns no placeholder
and releases all factory charges; retry requires the same valid inputs and an
available device. Nothing runs on a frame/draw hot path.

| Backend | Original compute consumption /1 | Canonical typed execution |
| --- | --- | --- |
| Null | Equivalent ValidatorOnly; no native receipt | DiagnosticFailure |
| Vulkan | DiagnosticFailure, missing approved memory compute payload | DiagnosticFailure |
| Metal | DiagnosticFailure, missing approved memory compute payload | DiagnosticFailure |
| WebGPU | Native only with genuine available non-lost device and successful native module/layout/pipeline | DiagnosticFailure |

This table is a selected Prototype contract, not a promotion of generic bindless
capabilities. Preserve explicit unsupported diagnostics until matching backend
payloads exist. WebGPU reuses its original shader emission and compute pipeline
emission functions: ordinary file creation reads its files and passes resulting
memory to the same native emitter; original creation selects retained WGSL bytes
and immutable reflection lowering, with no file IO, YAML parse or digest-based
path correlation. Native strings have the exact retained byte length. Source
fingerprint facts are computed from the exact view supplied to native creation.
Missing/invalid module, layout or pipeline rejects. No dummy/native-policy bypass.

The existing private issued-pipeline receipt retains the exact original input
owner only when the memory factory passes it during issuance. Before publication
verify stage, entry, workgroup, target-byte size/digest and original input/quota
relationships against actual issued stage facts. Ordinary path factories have
no artifact input and cannot be relabeled by observation. Receipt observation is
allocation-free, rechecks genuine input/quota and existing native issued identity
and loss gates, and cannot mint a command proof. All native handles remain private.

The caller-authored L2 layout is validated against the input's exact canonical
package before native emission. The private factory receives that same package
and entry; selected declarations and compute state use existing validators.
Later L2 integration must retain exact registered layout/complete definition and
the same receipt under a versioned owner budget. This stage alone does not let a
native payload become an original L2 Pipeline or canonical Source.

Tests keep the fast contract and Null hidden-frame paths for explicit four-backend
outcomes and forbidden adoption. Both real WebGPU browser modes must create a
genuine memory pipeline with authored files absent, retain input after caller
drop, reject path-created/forged inputs and changed quota limits, verify cold
exact/one-less/exhausted pressure and hot observation allocation0, check real
issued shader fingerprint and preserve existing device-loss/native-error checks.
No shader or visible output changes; RenderDoc unavailable is recorded as a
diagnostic and no Platform Validated or Production Ready claim is made.
