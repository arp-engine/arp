# Original Table actor and native array boundary

Status (2026-10-06): next implementation DefinedOnly / Not Contract Complete /
Not Prototype Complete / Not Backend Validated / Not Platform Validated / Not
Production Ready. The existing TableLowering1 declaration and native array
refusal contracts are complete; their diagnostic evidence does not establish an
accepted Table actor. Current evidence and reuse sites are recorded in
`original-table-native-actor-next-audit.yaml`.

The selected original shaders already declare three sampled-depth textures at
logical set0/binding0. Compute visibility is Compute, with one separate writable
storage output at set1/binding0 and workgroup5x1x1. Graphics visibility is Fragment;
the vertex child has no resource declaration. The runtime compute index is
thread.x modulo3; the fragment index is uint(position.x) modulo3. The native
actor must preserve the independent element identities and runtime selection.
Neither a scalar resource nor a capacity1 table can represent this contract.

The exact original Table slot must retain type, stage union, capacity3, depth and
view-dimension constraints from the genuine artifact/layout. Its elements must
retain genuine registered texture/view sources and complete dependencies,
successful write revision, arena epoch and original allocation owners. Existing
TableSource2 metadata proves custody only. A declaration match still fails with
RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE.

Two independent prerequisite proofs are required before any new availability:

1. The selected compiler accepts the exact immutable Slang-generated target for
   every resource-consuming entry. Record actual compilation diagnostics and
   explicitly queried or specification-guaranteed language eligibility. Vertex
   acceptance alone does not prove Fragment or Compute support.
2. A versioned native resource ABI carries all three ordered resources and the
   exact layout cardinality. Prove payload construction and runtime element
   selection with genuine independently authored texture content. A layout field,
   header declaration, enabled feature name or shader acceptance alone cannot
   prove the C-to-JavaScript/native resource payload.

The currently pinned emdawn bridge has scalar WGPUBindGroupEntry resources,
ignores layout bindingArraySize and asserts it is0 or1. The current genuine
Compute/Fragment shader probes reject sized_binding_array. These are separate
failures. Altering a capability flag, removing the ABI guard or emitting three
duplicate logical bindings cannot resolve them. Keep ABI1 and TableLowering1
closed. A newer SDK or device must be inspected as a distinct versioned contract;
do not infer availability from its release number or backend/OS name.

Current f4c3b continuation: `OriginalDepthTableScalarExpansion/1` now supplies
the separately tested complete-program compiler prerequisite. See
`original-depth-table-scalar-expansion-contract.md` and its validation YAML.
Three Slang DepthTexture2D scalar resources preserve the exact original logical
capacity3 table, program identities and runtime selection. All compiler targets
and real WebGPU compute/fragment ordering/substitution probes pass in both
feature modes. The distinct compiler publication has no ordinary factory reader
and does not establish an accepted original Pipeline/Table actor. This does
not change the pinned native-array refusal or TableLowering1.

The Equivalent scalar expansion requires a separate complete-program lowering
contract before implementation: deterministic Slang-originated publication,
resource cardinality/physical map, runtime selection and out-of-range semantics,
derivatives and shader stage behavior, exact native limits and resource budgets.
Rewriting generated WGSL/MSL or substituting an unrelated scalar shader is
prohibited. Only the selected versioned program transformation above is proved;
generic Table expansion, native actor availability and execution remain open.

| Backend | Current selected original Table result | Requirement for a future available path |
| --- | --- | --- |
| Null | ValidatorOnly / DiagnosticFailure | Exact metadata negatives and hidden-frame; no mock native success |
| Vulkan | DiagnosticFailure | Genuine original accepted artifact/pipeline/Table payload, capability provenance, native array output and lifetime evidence |
| Metal | DiagnosticFailure | Genuine original accepted artifact/pipeline/Table payload, exact argument array ABI, native element selection and lifetime evidence |
| WebGPU | DiagnosticFailure | Both exact target-language eligibility and versioned resource-array bridge payload, plus real original actor results |

A future request must independently opt in to its approved actor/ABI version and
author element, group/layout, native call, retained-owner and CPU capacities.
ExtensionOptIn is distinct from base capability. Freeze the selected lowering
before execution and fail before native work if any prerequisite is absent.
Do not enlarge immutable Source authority, reinterpret unused owner/quota bytes
as element capacity, add another inventory or manufacture a successful receipt.

Reuse DescriptorSlotShapeContract::TableMatch::ExactOriginal, the existing
OriginalCommandDescriptorBindingContract diagnosis, TableSource2 frozen element
and owner projections, accepted artifact/input validation, original native entry
builder and existing candidate transaction. Add shared core/runtime helpers only
if these modules cannot express the required operation. Cold owned storage must
use core allocation under explicitly independent original quotas.

After a real accepted actor exists, define positional table admission before
opening tableLowering: active original pipeline/source/scope and set slot,
generation/revision/epoch, complete ordered element/dependency identity and whole
stream preflight. Then define table shader usage at actual Draw/Dispatch,
including every render table binding in the full render usage scope, before
extending frozen synchronization/native closure/cold counts and the existing
Recording/Submission. The current selected descriptor executor remains unchanged.
No new state graph, journal, completion pool or proof issuer is permitted.

Each implementation stage retains a quick contract test and Null hidden-frame
path. Native acceptance must cover distinct element outputs, wrong ordering and
resource substitution, exact/less/exhausted independent quotas, preallocation
failure, mutation/revision/epoch, caller drop, cancel/error/uncertain submission,
Retired/Shutdown and actual physical loss with foreign drain. Preserve all prior
feature flags, stable message multisets, allocation sizes,180-second timeout and
Asyncify16384. RenderDoc capture is required for visual acceptance; current
RHI-RENDERDOC-TOOL-UNAVAILABLE grants no visual/platform/production status.

Continuation order: retain the completed f4c3b compiler proof; establish the
original immutable logical/physical input boundary specified in
`original-table-expanded-input-next-contract.md`; implement only that approved private payload;
prove original actor creation and native numbers/lifetime; then table positional,
state/frozen synchronization and sole issuer integration. The accepted Table
parent and full bindless plan remain open.
