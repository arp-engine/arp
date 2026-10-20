# Original runtime length native interval lineage

Status: Contract Complete / Prototype Complete for the original diagnostic and
native observation contract; Backend Validated for WebGPU compute and both-stage
graphics numeric finite intervals only. Host59/59 with Null hidden-frame,
Wasm/both-browser11/11 and inventory204/204 passed. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/runtime-length-original-interval-validation.yaml`.
Not Platform Validated / Not Production Ready. Descriptor writes,
Source/Inputs/state and production command execution are not proved by this
observation. request2, NativeSync and public/general bindless remain closed.
This contract adds no owner, root, seal, journal, Source family or native issuer.
Recording and Submission remain the two full native execution issuers.

## Reuse and exact ownership boundary

The existing GpuDevice is the joint logical registry validator and the existing
OriginalRegisteredPipeline retains private queried alignment/maxima. An original
buffer extent must come from OriginalCommandBufferLease.originalObject().desc()
after exact lease owner/quota and object-layout validation, followed by this
same GpuDevice's validateOriginalResourceRegistration. A buffer id, copied
BufferDesc, ordinary shared wrapper or native allocation size is insufficient.
The accepted pipeline must pass its exact creation seal, complete definition,
original artifact input and this device's registry validation. Foreign logical
devices sharing one physical WebGPU device must reject. Registered identity and
resource generation stay in their existing tickets; no extra receipt is created.

Add finite scalar validation to OriginalDescriptorBufferBindingContract by
reusing DescriptorBufferBindingRangeContract.resolve. The authored range must be
version2, nonempty and finite. Validate authored base and frozen dynamic
displacement separately against the captured alignment. Validate unchanged size
against target declaration minimum and captured maximum. Both static and shifted
intervals must fit the exact retained extent; overflow or zero limits reject.
Scalar validation consumes facts only and cannot issue original/native authority.

The joint original diagnostic belongs to GpuDevice, which already has private
access to the pipeline's frozen limits and owns the registration authority.
Do not expose detachable limits or add a public setter/test friend. Its independent
version1 Prototype opt-in authorizes a diagnostic observation only, distinct from
OriginalDescriptorBufferBinding request2. Outputs are fixed scalar metadata,
cleared on every failure and explicitly execution-unproved. A successful result
cannot be fed into a descriptor write, canonical command, capability promotion
or accepted native proof. No borrowed request is retained.

Select the logical set/binding from the exact original pipeline definition and
layout. Validate descriptor type, scalar cardinality, active selected stages and
whether a dynamic argument is authored for that binding. Missing, inactive,
duplicate, array/legacy or wrong-type bindings reject. Buffer usage must match the
existing descriptor resource contract. Access coverage cannot author the interval.

Retrieve complete memory and the chosen runtime operation from the genuine
entry/module artifact. Logical set/binding and target physical group/binding must
match, as must runtime member offset/stride. Require the exact diagnosed profile;
copies of operation/declaration facts never choose it. Graphics uses combined
memory with all selected child shapes/minima already validated, and independently
checks every participating selected-stage operation. An operation inventory union
does not prove stage reachability or native invocation count. Compute the expected
length using WgslBufferDeclarationLayout.runtimeArrayLength on unchanged size.

Implementation API: `OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest/1`
requires explicit Prototype opt-in, independently of its still-closed Request/2.
`GpuDevice::diagnoseOriginalRuntimeLengthInterval` consumes a genuine quota-bound
shared accepted ComputePipeline/RenderPipeline and genuine OriginalCommandBufferLease.
It validates this device's exact registry identity for both before selecting the
original input and its artifact. A reusable stateless
OriginalRuntimeLengthIntervalContract joins frozen declaration/operation facts;
bounded original operation-count queries extend the existing artifact lookup API.
Every matching operation must agree with the target runtime member, and every
participating selected graphics stage must have an independently verified matching
operation. Fixed numeric observations carry no owner or native execution receipt.

## Native observation and lowering

| Backend | Diagnostic observation | Production interval/execution |
| --- | --- | --- |
| Null | Scalar/owner validator negatives only | DiagnosticFailure, no native payload |
| Vulkan | DiagnosticFailure: accepted original interval issuer absent | Capability false; request2/write gate closed |
| Metal | SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED | Capability false; no MSL/length ABI placeholder |
| WebGPU | Original joint diagnostic, then backend-private Prototype native test | Capability false until f3/f4 contract and payload joins |

The native test must consume the exact accepted issued pipeline and exact original
buffer payloads, checking their native owner device against the issued pipeline
device. It authors actual bind-group offset and finite size, uses the pipeline's
exact native layout with the declared dynamic binding, and observes the frozen
dynamic displacement. It must reject handle/metadata/pipeline copies and foreign,
expired or lost lineage before native calls. No generated WGSL edits or alternate
shader/pipeline can stand in for the accepted original bytes.

A test-only direct native API observation can measure actual arrayLength outputs
and descriptor semantics under this lineage. It remains a fixture, cannot issue
production authority, and cannot bypass the final version2 write gate. Reuse the
existing error-scope/readback test orchestration where possible. Native buffers,
readback, attachments and test commands need explicitly authored bounded budgets,
deterministic cleanup and completed scopes. Every allocation must use the existing
core and backend ownership path; no dummy, replay or native-query default.

For graphics, measure each participating stage with an authored Slang probe whose
outputs expose both stage lengths. The existing graphics custody shader positions
vertices outside the viewport for these test lengths; issuing a draw alone is
insufficient output evidence. A new probe, if needed, must use canonical compiler
entry/module publication and produce SPIR-V/WGSL/reflection plus the exact MSL
diagnostic, with shader contract tests. Preserve prior fixtures and evidence.

## Tests and remaining gates

Keep request1 whole-buffer/nonzero-dynamic rejection, generic missing-MSL
rejection and all request2/final-write negatives. Quick tests cover independently
unaligned base/dynamic values whose sum is aligned, exact-end/overflow intervals,
zero/oversized/undersized binding bytes, prefix/stride/tails and cleared output.
Original tests cover forged lease/owner, wrong binding/stage/type, original quota
mutation, foreign device/physical payload, retirement/loss and caller drop.
Hot diagnostics require allocation0, IO0 and native limits getter reads0.

Native tests require padded float3 and byte-address read/write variants, one and
multiple elements, partial stride tails, nonzero static plus dynamic offsets and
unchanged finite size. Both graphics stage outputs need actual observations.
Retain Null hidden-frame, both real browser modes and every old success flag.
Record all failing attempts and exact native diagnostics. RenderDoc remains
RHI-RENDERDOC-TOOL-UNAVAILABLE; no Platform/Production promotion is permitted.

Only completed original native interval evidence can precede request2's separate
opt-in extension. f3 must still implement explicit four-backend lowering outcomes;
f4 must still join frozen Descriptor Source/Inputs/state revision/generation,
dynamic arguments and complete dependencies through the existing ownership graph.

Finite scalar, original join and native observations are verified. All previous
browser flags315/314 remain true; the four new original interval flags are
required. Diagnostics/nativeErrors are empty in both actual feature modes.
Four compute variants measure five finite sizes each. Graphics preserves the
prior custody fixture and uses the independently published probe below to
observe both stages, with all four pixels checked in each of five cases.
Independent artifact/input/resource/context quotas are reclaimed, including the
retained actual physical-loss case. Hot diagnostics allocate0 and query0.
RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no visual/Platform/Production
acceptance is claimed. f3/f4 remain mandatory before production consumption.

Graphics numeric probe contract: an independent canonical Slang module preserves
the two binding declarations and three runtime operations of the custody fixture.
Three vertex-ID positions (-1,-1), (3,-1), (-1,3) cover a 2x2 target. Flat uint2
varyings preserve vertex count/stride; fragment output channels contain vertex
count, vertex stride, fragment Shared count and FragmentOnly count, divided by
255 exactly according to the Rgba8Unorm format conversion. Each authored result
must fit uint8; blend is disabled, all channels written, single sample, explicit
viewport/scissor and no culling. An original registered texture/view and original
readback buffer retain bounded ownership. Readback pitch uses the shared WebGPU
copy alignment and core alignment utility, never a guessed padding constant.
Every pixel must equal all four expected integer bytes derived from independent
original per-binding diagnostics; clear sentinel bytes differ from those outputs.
Binding0 has static base plus one frozen dynamic offset; FragmentOnly has a
distinct finite size/static base and rejects nonzero dynamic arguments. This
readback proves both stages numerically, without visual/RenderDoc promotion.
