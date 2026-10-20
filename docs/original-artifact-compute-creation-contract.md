# Original artifact compute creation acceptance /1

Status: Contract Complete / Prototype Complete; Backend Validated for selected
WebGPU asynchronous creation only, verified 2026-10-05 host17/17,
Wasm/browser8/8 and API inventory187/187. No typed Platform Validated or
Production Ready claim. The /5 original accepted compute factory now retains this L2 receipt; canonical
Source/typed admission still rejects `RHI-ARTIFACT-CREATION-CANONICAL-JOIN-MISSING`.
See original-artifact-compute-factory-contract.md for the separately verified join.

The exact original memory factory and consumed input are prerequisites, not
creation acceptance. A returned native handle or stage digest does not prove
that shader/module/layout/pipeline creation passed native validation. Query the
actual non-lost WebGPU device's shader/pipeline/layout and push/pop error-scope
APIs before starting. Null stays ValidatorOnly without native acceptance;
Vulkan and Metal reject the missing approved original memory creation payload.
WebGPU may publish a ready receipt only after genuine successful native scopes.

Prepare a quota-bound immutable owner with independently authored owner, native
factory and callback quotas, all distinct from input and artifact quotas. Reuse
the existing original input and exact shared L2 layout; freeze all compute fields
and label before initialization. Callback count is the existing versioned
WebGpuErrorScopeContract count, not an inferred budget. Reserve every userdata
owner before any native call. Cold allocation failure has no partial owner or
native work and preserves caller inputs. Unknown version/opt-in/quota/capacity,
forged layout/input and changed authored ceilings fail with stable diagnostics.

Use WebGpuNativeErrorScopeBatch for one push/pop stack transaction around the
same memory factory, and core::runtime::CompletionEvidenceBatch to join actual
Internal/OutOfMemory/Validation callbacks. Always pop the complete batch even
when native construction fails. A callback owns its original actor until
delivery and releases its cold allocation through core. Caller drop cannot free
userdata while the foreign runtime may call it; loss cannot counterfeit callback
drain. Do not add a second canonical root, graph, journal, State or proof issuer.

Externally serialized states are Prepared, Pending, Ready, Rejected and DeviceLost.
Prepared/Pending reject admission. Every participant must arrive once with
successful pop status and NoError; error/unknown/duplicate receipts reject.
Ready requires all evidence plus current exact original input/layout/native
consumption identities, unchanged budgets and a non-lost exact device. Success
may not be synthesized from CPU completion, global uncaptured-error silence or
another factory's receipt. Keep ordinary observation bridging rejected.

Hot readiness observation has allocation0, no waits, IO, shader compilation or
native construction. This owner grants no dispatch, canonical Source or command
execution authority. A later versioned GpuDevice original factory must join the
ready owner, exact registered layout and complete immutable definition with an
explicit owner budget; no retrospective attachment to ordinary pipelines.

Preserve fast contract/Null hidden-frame checks and both WebGPU browser modes.
Verify actual success with authored files absent, real native validation error,
pending/unknown/error/loss/caller-drop behavior, exact/one-less/exhausted cold
owner/native/callback budgets and retry, forged identities, changed limits,
callback retention/drain, hot allocation0 and final quotas0. Preserve all prior
mandatory native map/publication flags. No shader or visible output changes,
and no typed Backend/Platform/Production promotion from this definition alone.

The real browser smoke runs with the complete authored directory absent. All
three scopes arrive through the native callback ABI, with held delivery released
in order 2/0/1; readiness stays closed until the final successful receipt. Actual
invalid native entrypoint creation and rejected native pop promises never become
Ready. Changed live APIs, forged ownership and changed quota ceilings reject.
Ready validation compares the actual retained JS device, not only a C wrapper.
Caller drop retains the actor until callback drain; actual loss in Prepared,
Pending and Ready refuses admission and drains all owned metadata. Measured cold
peaks owner980/native916/callback36 pass exact/one-less/exhausted/retry. The pure
budget/lowering tests also run without the browser API port. Hot validation and
callbacks allocate0 with ambient PMR disabled. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/artifact-creation-validation.yaml`.
