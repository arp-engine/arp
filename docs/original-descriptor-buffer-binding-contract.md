# Original Descriptor buffer binding and dynamic offsets — next boundary

Status: Contract Complete / Prototype Complete for the selected current-schema
whole-buffer range and exact accepted-wrapper limits prerequisites (c2b4c1/c2b4c2).
Backend Validated: actual WebGPU native-buffer metadata guard and exact-device
queried limits/CPU gate only. Not Platform Validated or Production Ready. Selected
Pipeline Inputs /2 is verified; Descriptor/Table Inputs /3 and State/native binds
remain closed. Explicit authored subranges/nonzero offsets remain a separate
versioned four-backend task; current-schema completion does not certify them.

Current DescriptorBindingWrite identifies a buffer without an authored binding
offset or size. Its original Source element bufferAccessOffset/bufferAccessBytes
describe access coverage, not a native descriptor subrange. For this schema, the
binding is the complete resource at offset0. It must never become the remaining
buffer tail merely because a nonzero dynamic offset was supplied. WebGPU currently
lowers offset0/WGPU_WHOLE_SIZE; Vulkan lowers offset0/VK_WHOLE_SIZE. A future
explicit subrange needs a versioned L2 write/reflection/lowering contract across
all four backends before any backend uses the access declaration as its range.

Selected original positional /3 must either prove that the dynamic argument
preserves this complete-buffer binding or reject with a stable diagnostic. Zero
offset satisfies that range relationship; any nonzero offset for the current
whole-resource write must fail with
RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-RANGE-UNPROVEN. Reuse/extract the existing
WebGpuOrdinaryDescriptorObservation nonzero rejection instead of copying a new
backend-specific heuristic. Keep exact count and ascending binding-number order
from existing descriptor validators and frozen CommandVariableArguments.

For buffer limits, capture both uniform/storage alignment and maximum binding
size from actual native queries or a reviewed versioned backend contract, on the
cold path. Existing webGpuNativeDeviceLimits already queries GPUDevice.limits and
returns unavailable values for missing/malformed facts. Caller runtime profiles,
backend names, OS names and arbitrary DeviceCapabilities overrides cannot issue
an original native limits receipt. Bind captured facts to the exact accepted
original pipeline/layout/device lineage through a private issuer; validate exact
owner/control block and live Source custody. Do not expose native handles, add a
second root/inventory, or query native limits during hot replay. Default/future
versions, unavailable alignment/size, expired/foreign lineage and physical loss
reject. Use core::containsUnsignedRange for overflow-safe ranges. Keep binding
range separate from declared access coverage until shader/state contracts join.

| Backend | Selected original limits/whole-buffer positional prerequisite |
| --- | --- |
| Null | ValidatorOnly / DiagnosticFailure for accepted native original inputs |
| Vulkan | DiagnosticFailure until approved accepted original payload/limits issuer exists |
| Metal | DiagnosticFailure until approved accepted original payload/limits issuer exists |
| WebGPU | Equivalent cold queried facts inside exact accepted wrapper and deterministic whole-binding proof; /3 execution closed |

No NativeSync, shader footprint, state or GPU work is authorized by successful
range validation. Selected WebGPU fact capture is verified. Inputs /3 consumption and typed native
execution are separate gates and remain unavailable.
The render-bundle remaining-size validator is a separate consumer to audit against
the same binding semantics; it cannot define original L2 behavior.

Required tests: quick contract/unit plus Null hidden-frame; both actual browser
modes; exact/missing/malformed queried facts, full buffer size, offset0 versus
aligned/nonaligned nonzero, count/order/type/overflow, fake capabilities and
foreign/expired receipt, cold quota exact/one-less/retry, frozen descriptor
revision/epoch/dependencies, caller drop and physical loss, hot allocation0 and
no hot native query. Subsequent Descriptor/Table /3 joins retain the same Inputs
sequence, active pipeline scope, Source indices/seal/journal and private command
proof issuers. Keep remaining post-loss Buffer/Sampler/DescriptorGroup/RenderBundle
reference retirement as an explicit validation obligation before native promotion.

## c2b4c1: shared whole-buffer range prerequisite

Defined before implementation, 2026-10-05. The reusable L2 semantic policy
`DescriptorWholeBufferBinding/1` consumes scalar limit facts but cannot issue
original device authority. Default/future fact versions, non-buffer types,
missing alignment/maximum, empty buffers and full sizes above the maximum fail.
Every nonzero dynamic offset fails, including aligned offsets and offsets which
would previously make an oversized buffer tail fit. Count/pointer/ascending
binding order continue to use existing descriptor layout/command metadata.
No access-coverage field participates in this calculation.

WebGPU ordinary observation reuses its existing stable range diagnostic. Native
render, compute and render-bundle consumers use the same complete-binding policy;
no `bufferBytes - dynamicOffset` inference remains. Their existing queried native
limits are validation inputs only; this stage does not claim cold original
receipts or hot-query removal. Internal push-constant upload rings author explicit
slice sizes through their separate contract and do not pass through this policy.

Null uses the policy as a validator only. Vulkan/Metal retain their existing
lowering and have no accepted-original receipt; original positional execution
fails closed. WebGPU gains a shared deterministic native descriptor guard;
original Inputs /3 still fails closed until exact-lineage receipt and slot joins.
Selected policy unit and Null hidden-frame plus actual WebGPU descriptor payload
negative probes are required. No shader or visible-output changes are intended.
Enclosing c2b4c and all native/State gates remain open pending the receipt stage.

Selected c2b4c1 verified: Contract Complete / Prototype Complete shared guard,
Backend Validated actual WebGPU native-buffer metadata guard only. Host quick/Null
5/5 and Wasm/browser8/8; inventory198/198; all prior true flags preserved and
native diagnostics empty. The logical Descriptor payload deliberately has no
physical group until active-pipeline compact lowering, so the native-buffer guard
probe does not certify group creation or emission. Initial incorrect fixture
assumption and reports retained. RenderDoc unavailable, no shader/visible change.
Enclosing c2b4c remains incomplete. Continue c2b4c2 cold exact-lineage capture.

## c2b4c2: limits captured inside the accepted wrapper

Defined before implementation. Store fixed scalar limits privately in the exact
accepted wrapper, using its existing weak typed creation seal and eventual strong
creation/layout/registration custody. The two existing approved accepted factories
query the actor's private native device on their cold path, validate that exact
device against Module's physical device before/after capture, and stamp the
snapshot together with wrapper issuance. No separate receipt owner, public setter,
detachable authority, root, inventory or command proof issuer is added.

The buffer binding request is independently versioned /1 with explicit Prototype
opt-in. Default/future request versions reject. Validation checks the accepted
wrapper's original full definition, sealed exact creation control block, exact
layout and current original registration/lifetime before consuming frozen facts.
Missing or malformed facts fail only this new prerequisite; accepted pipeline
creation evidence retains its existing scope. Both alignments and maxima must be
available. Native queries require positive safe integers with declared integer
representability; exceptions or absent/malformed properties capture unavailable
facts. Repeated validation never reads native limits. Caller capability objects
cannot provide or replace this snapshot. Buffer bytes remain validation arguments
until /3 joins the actual frozen Descriptor dependency; this method authorizes no
state or command work. Current whole-buffer schema still rejects nonzero offsets;
future subrange support remains a separate versioned four-backend obligation.

Null is ValidatorOnly/DiagnosticFailure; Vulkan and Metal DiagnosticFailure
because accepted-original capture payloads are absent; WebGPU Equivalent selected
cold facts only, execution unavailable. Bounded storage is charged inside the
existing wrapper quota and uses the existing exact/one-less/exhausted/retry probes.
Actual compute/graphics fixtures must verify valid/oversized/offset/version,
malformed/missing capture, caller capability independence, frozen facts, no hot
limit getter reads/allocations, foreign physical device and physical/L2 loss.
All old Source owner unions, record quotas and /1-/2-/3 State gates remain intact.

Selected c2b4c2 verified 2026-10-05: host19/19 and final quick/Null5/5;
Wasm/browser8/8, inventory199/199. Actual compute/graphics wrappers capture four
scalar native-device facts cold. Ten unavailable/malformed/throwing query cases
produce unusable snapshots, never original authority; genuine retry succeeds.
Frozen facts retain original wrapper/actor/layout identity after caller drop,
reject changed physical lineage, L2 retirement and physical loss, and validate
under all existing quotas/hostile PMR with allocation0 and native limit getter
reads0. Existing exact/one-less/exhausted/retry tests measure wrapper unique peaks
780/788 bytes, without new owned receipt storage. All prior Source owner unions,
record quotas and true browser flags remain; native diagnostics are empty.
Logical Descriptor and typed GPU emission are not certified. The existing
ordinary native command query path is still separate; no general hot-query
removal is claimed. Initial capture fixture overlapped two wrappers under exact
owner capacity and was reordered rather than increasing the budget. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/descriptor-limits-validation.yaml`.

Continue c2b4d with the independently versioned Inputs /3 slot audit. Reuse exact
frozen layout equality and original Source validation, which already guard
revision/epoch/dependencies; legacy ValidationResult error strings may allocate
and cannot be copied into hot replay unchanged. Actual buffer size comes from the
retained original buffer lease, never bufferAccessBytes. Frozen offsets already
live in CommandVariableArguments. Table fixtures have capacity3/binding0 and need
existing YAML entry/module publication modes before original actors can consume
them. No additional source ownership graph or native command proof issuer is
permitted. Audit: canonical-original-descriptor-inputs3-next-audit.yaml.
