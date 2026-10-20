# Original General finite buffer composition /2

Status (2026-10-11): Contract Complete / Prototype Complete; scoped Backend
Validated for the selected original finite UniformBuffer16 + StorageTexture/
SampledTexture/Sampler creation in Release Edge, with static and nonzero dynamic
displacements. The corrected browser passes in4.60s; affected browsers3/3 in
184.25s retain both439-Boolean profiles and the old General numerical result.
Dedicated fast General contracts, rebuilt command-retention/Null smoke and
descriptor Node pass, as do quick3/3 and source354. Null's four artifacts are
packaged into Wasm and its Slang source preserves the authored LF hash. The first
browser hook used entry.buffer instead of the native JS entry.resource.buffer;
its failed report and corrected final report are retained. Other first test/build
failures are preserved too. Evidence: build/bindless-general-finite2-final-
validation.json and artifacts/bindless-continuation/general-finite-binding2/.
The three buffer types have shape contracts; this does not prove all native
mixed buffer types, finite execution, public execution or platform/production.
Next dependency: original-general-finite-source-contract.md (planned Source/5).

GeneralBinding/2 and GeneralWrite/2 compose explicitly authored finite buffer
bindings with the existing scalar texture-view and sampler bindings in one
accepted original pipeline group. GeneralBinding/1 and GeneralWrite/1 retain
their scalar whole-buffer language. Selecting /2 is independent and explicit;
it does not change ordinary descriptor writes or existing execution profiles.

A buffer element carries DescriptorBufferBindingRange/2, immutable base/size,
and a frozen dynamic displacement. A non-dynamic layout requires displacement0.
Both the static and displaced ranges must fit the original buffer. Static base
and displacement are independently aligned to the accepted pipeline's queried
limits. Binding size satisfies the original target-memory minimum and native
declared minimum and queried maximum. Access coverage is exactly the effective
full bound interval; it cannot manufacture a binding slice. Reuse finite range,
FullBoundView and accepted target-memory lowering rather than new arithmetic.
Whole buffer elements and non-buffer elements keep /1 semantics. Arrays,
table-backed elements and physical substitution require their own contracts.
TargetMemoryInterval/2 adds UniformBuffer to the existing immutable declaration
diagnostic. Its /1 storage-only type gate is preserved. Both versions use the
same original artifact, resource registration, accepted queried limits and range
validator; neither provides successful writes or execution authority itself.

| Backend | /2 preparation | /2 execution |
|---|---|---|
| Null | ValidatorOnly, deterministic native refusal | Unavailable |
| Vulkan | DiagnosticFailure, original adapter missing | Unavailable |
| Metal | DiagnosticFailure, original adapter missing | Unavailable |
| WebGPU | Existing explicit private scoped creation | Unavailable until composition joins are implemented |

The original transaction retains the same individual canonical dependencies,
frozen entries and dynamic offsets, three independent CPU quotas, queried
pipeline provenance, whole allocation accounting, genuine creation scopes and
single atomic revision gate. No additional inventory, queue, journal or proof
issuer is introduced. Revalidation checks the complete frozen entry and each
ordered native binding/displacement. Failed replacement preserves accepted
content and revision; pending/canceled work retains custody until genuine drain.

NativeIdentity/4 GeneralWrite2 observes /2 creation only. GeneralWrite1 identity
must refuse /2 and vice versa, including all-whole /2 groups. Existing Closure8,
Freeze6, Payload6, Cold6 and GeneralSubmission1 cannot infer /2 authorization.
Next connect frozen mixed whole/finite footprints and offsets through Source,
Usage/State/Stream and native closure before recording/submission/numerical
acceptance. Ordinary public WebGPU and the full P3-P6 goal remain open.

Required validation: contract negatives on all buffer types and non-buffer
domains; real accepted mixed group with static and nonzero dynamic ranges;
exact native entry arguments, queried alignment/minimum/extent failures before
native creation; profile mismatch, identity/offset mutation, genuine scoped
replacement failure, original count/byte quotas and last-owner retirement.
Keep existing fast contracts and Null/platform smoke. No new shader is needed
for creation: use the existing Slang General resources artifact and its original
target-memory publication. Status and actual evidence are recorded after checks.
