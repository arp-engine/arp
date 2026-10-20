# Accepted original Pipeline wrapper seal /1

Status: Contract Complete / Prototype Complete / Backend Validated for selected
WebGPU accepted-wrapper adoption and CPU custody. Not Platform Validated or
Production Ready. Verified 2026-10-05: host19/19, Wasm/browser8/8, inventory195/195.

Prerequisite for canonical positional Inputs /2. A sealed creation receipt and a
successful L2 validation report cannot certify the candidate returned by an
overridable GpuDevice factory. The final wrapper must carry a backend-private
issuance relationship to the exact creation owner whose accepted native payload
it adopts. A substitute facade, including one forwarding a genuine wrapper's
interface, must fail before registry object binding or original metadata
publication. No wrapper seal may be inferred from backend name or layout equality.

Only WebGpuAcceptedOriginalComputePipelineFactory and
WebGpuAcceptedOriginalGraphicsPipelineFactory issue their respective weak typed
creation owner into the final, noncopyable wrapper after adopting actor->pipeline_.
The gate compares pointer and owning control block against the accepted creation.
An expired owner, unissued wrapper or different receipt fails with
RHI-ACCEPTED-COMPUTE-FACTORY-WRAPPER-SEAL or
RHI-ACCEPTED-GRAPHICS-FACTORY-WRAPPER-SEAL. Weak ownership prevents recycled
addresses from satisfying the identity and adds no strong owner or native getter.
The existing explicit factory /1 opt-in and compute registry /5 or graphics /6
remain required. Weak fields are included in the authored wrapper CPU quota and
exact/one-less allocation tests; canonical retained-owner union is unchanged.

Live originalArtifact diagnostics repeat the seal check. Failure releases the
candidate and provisional registry record; the genuine actor remains accepted and
can be retried. No fallback, placeholder publication, command proof, state or
execution authority is issued. Existing Recording/Submission proof friends stay
unchanged. Hot checks allocate zero and do not consult the allocator ledger.

| Backend | Accepted original wrapper /1 |
| --- | --- |
| Null | DiagnosticFailure / ValidatorOnly; no accepted native receipt issuer |
| Vulkan | DiagnosticFailure; accepted original payload not implemented |
| Metal | DiagnosticFailure; accepted original payload not implemented |
| WebGPU | Equivalent sealed L2 wrapper adopting exact accepted native payload |

Required tests: fast contract plus Null hidden-frame, both real browser modes,
compute and graphics genuine actor plus substitute facade rejection, different
accepted receipt rejection, genuine private-factory retry, quota rollback and
caller drop/physical loss/final retirement. Preserve earlier mandatory browser
flags, files-absent and zero recreation evidence. Record RenderDoc availability;
no visible output changes or broader readiness promotion.

Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
wrapper-seal-validation.yaml and wrapper-seal-final-{pipeline,indirect-disabled}-
report.json. Both actual modes reject substitute facades and different genuine
receipts for compute/graphics, restore metadata/wrapper live-byte baselines and
retry through genuine private factories under hostile PMR. Unregistered wrappers
retain no actor. Existing exact/one-less/exhausted/retry, hot0, caller drop,
physical loss, native-wrapper and seven-quota retirement probes pass. Prior true
flags preserved; diagnostics/nativeErrors empty. Wrapper/shared Wasm peaks now
760 compute / 768 graphics; ownership unions unchanged. Initial build naming
collision was corrected; initial and final logs retained. RenderDoc unavailable
on this macOS host (RHI-RENDERDOC-TOOL-UNAVAILABLE); no shader/visible changes.
