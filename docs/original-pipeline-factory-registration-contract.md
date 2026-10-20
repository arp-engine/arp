# Original pipeline factory registration /2

Status: **Contract Complete / Prototype Complete** CPU factory lifetime and
metadata custody, verified 2026-10-05. This is a cold,
explicit Prototype lifetime prerequisite for d3d2. It does not authorize a
canonical Pipeline Source, typed recording, GPU execution or native loss.

The existing OriginalResourceRegistration registry gains version 2. Version 1
keeps its Buffer/Texture/View policy and rejects nonzero pipeline budgets.
Version 2 adds independently bounded PipelineLayout, GraphicsPipeline and
ComputePipeline records. A pipeline budget requires a layout budget. Other
typed domains remain unissued. Fixed owners remain three; each layout/resource
costs two owners. Each pipeline costs four: wrapper, record, exact retained
layout owner and independently copied shader-package metadata owner.

Objects without a GPU resource ID use exact logical wrapper identity, kind and
the registry's monotonic issued generation. No ID is synthesized from addresses
or generations. Weak tickets cannot adopt an ordinary object. Only GpuDevice's
explicit nonvirtual original factories publish records; all ordinary pipeline
factories keep returning unregistered objects.

Before calling a backend factory, the issuer validates the exact original
device/domain, version, kind/owner capacities, registered layout and its
canonical core shared control block. It reserves the generation and allocates
the quota-charged record and package metadata. Factory failure releases all
reserved capacity/owners, retains no candidate and reports a stable diagnostic.
Consumed generation numbers are never recycled. Caller factory allocations
remain on the caller's quota. Record/package allocations use the existing
registration quota. Operations are cold and externally serialized.

A successful pipeline retains the exact original layout allocation and a deep
copy of shader-package metadata passed to the real backend factory. The
returned pipeline must observe that same layout and have a successful backend
validation report. A foreign/replaced layout or previously registered candidate
fails. Caller metadata mutation and layout-owner release cannot invalidate the
retained definition. Destruction, domain retirement and device shutdown
invalidate receipts without allocation or GPU waits. A layout held by a pipeline
remains alive until its last true owner drops.

| Backend | Logical factory receipt | Canonical typed execution | Native loss join |
| --- | --- | --- | --- |
| Null | Equivalent lifetime registration, validator object only | DiagnosticFailure | DiagnosticFailure |
| Vulkan | Equivalent lifetime registration of successful original wrapper | DiagnosticFailure | DiagnosticFailure |
| Metal | Equivalent lifetime registration of successful original wrapper | DiagnosticFailure | DiagnosticFailure |
| WebGPU | Equivalent lifetime registration of successful original wrapper | DiagnosticFailure | DiagnosticFailure |

An already prepared original native-loss binding rejects these factories before
the backend call with `RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING`. Metadata
paths, reflection hydration flags and successful ordinary creation do not prove
immutable artifact bytes, selected-entry reflection or native payload authority.
No Dummy, borrowed adoption, public record issuer, second graph/journal or change
to WebGpuRecordedCommandProof issuers is permitted.

Required checks: versions/opt-in/dependent budgets; exact object/kind/parent and
shared-control-block identity; ordinary/foreign/forged/missing/retired objects;
kind and owner capacity; allocator exact/one-less/exhausted limits and rollback;
monotonic post-release generation; true parent retention and package deep copy;
native factory failure; hot validation with allocation disabled; four closed
execution outcomes, fast contract test and Null hidden-frame smoke. Browser
smoke must exercise genuine layout registration and keep prior GPU map/publication
checks. Native typed execution, descriptor/query issuance and source adapters
remain future work and cannot be marked Backend Validated by CPU tests.

Verification: host12/12 (baseline11 plus Metal device contract), Wasm/browser8/8,
API inventory179/179. Host metadata peaks are layout144, graphics2616 and
compute2616 bytes. The layout factory runs with the ambient std PMR allocator
disabled. Actual Null graphics/compute factories consume existing generated
reflection; caller layout release preserves the original allocation and quota.
Both browser modes require `originalPipelineFactoryLayoutPassed` and
`originalPipelineFactoryWebGpuPassed`, alongside prior map/publication probes.
Failure rollback, missing package/layout, forged shared owner, ordinary/foreign
objects, exact/one-less/exhausted quotas and pipeline owner one-less all pass.
All final metadata/domain/object quotas return to zero.

No visible rendering or shader behavior changes. RenderDoc commands are absent
on the current macOS PATH; no new visual acceptance, typed Backend Validated,
Platform Validated or Production Ready claim is made. Evidence resides in
`build/web-bindless-apple-debug/artifacts/bindless-continuation/typed-pipeline-factory-validation.yaml`
and the associated final build/test/XML/browser report files. d3d2 remains open:
next complete descriptor factory/content observations, immutable pipeline artifact
and full-definition ownership, original Query joins and positional typed Inputs
over the existing Source/root/seal/journal before d3d3 execution.
