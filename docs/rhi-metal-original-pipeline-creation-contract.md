# Metal original synchronous pipeline creation / 1

Status: creation-stage Contract Complete (host); focused host regressions passed.
Apple validation deferred by user on 2026-10-20; the shared Metal package remains open.
No Apple Backend Validated or Platform Validated claim. This is an internal
creation stage of the existing P4-14 Metal execution package, not another completed
main task or a shared workload/output acceptance.

## L2 and lowering

`OriginalSynchronousPipelineCreationContract/1` is explicitly Prototype opt-in.
It has independent nonzero target-byte, core CPU metadata and cold-creation time
budgets. Creation is cold and synchronous; its deadline is checked before the
result escapes. It cannot cancel an in-progress driver compile. There is no
per-frame/per-draw shader compilation admission, command execution permission,
accepted generic-wrapper receipt, original-output audit or production promotion.

| Backend | Selected synchronous profile / 1 |
|---|---|
| Null | Equivalent validator only; no pipeline/output evidence |
| Vulkan | Existing original issuer required; this new profile refuses |
| Metal | Creation eligible only with original MSL, queried capability contract and actual native device; success requires the real native shader/pipeline payload |
| WebGPU | Existing asynchronous original issuer required; unchanged |
| WebGL1 | Existing ordered issuer required; unchanged |

The existing generic accepted graphics/compute creation profiles retain their
own gates. This new profile never imports bindless-only authorization to ordinary
pipelines and cannot mint a sealed `OriginalConformanceRun`.

## Original-source chain

`MetalOriginalPipeline` consumes the existing quota-bound
`OriginalGraphicsPipelineInput` or `OriginalSingleEntryPipelineInput`. It checks
exact package definition, stage/entry identity, source-owned reflection, complete
layout ABI and normal capability/state lowering. Combined graphics selects the
single original MSL module; compute selects the original compute MSL. Native
construction reuses the existing in-memory MSL overloads, never a path reload.
Logical-to-physical mappings and vertex-stream reservations come from that same
verified reflection and the existing Metal lowering.

Each wrapper owns a complete cold layout/interface/state copy. Both the wrapper
and its native pipeline retain the exact original input. Retaining that native payload therefore also retains the original artifact and
independent creation quota when the wrapper is destroyed. This stage does not
yet implement an original command journal or authentic submission/callback custody. `diagnostic(native)` checks the private source custody,
source/input quota validity, creation quota fingerprint and actual native owner.
It does not grant command, source-buffer, texture, drawable or output authority.
Future shared execution must still consume this diagnostic at its own finite
record/submit gates and retain accepted owners through authentic completion.

Creation fails closed on missing/foreign input, mutated package or ABI, unsupported
state, wrong target/budget/quota domain, exhausted CPU quota, absent native device,
missing native payload, or elapsed cold deadline. Partial native objects are
released synchronously; there is no dummy or file-backed compatibility success.
Native driver allocation accounting and a device-wide object reservation remain
obligations of the future shared issuer. The declared CPU budget accounts for
core-owned creation metadata, not opaque driver-internal memory.

## Validation paths

The host contract test loads the existing Sampled/PingPong module, Storage/MRT
graphics module and Storage compute entry. It validates RGBA8/BGRA8 graphics
variants, MRT formats, workgroup metadata, exact package/entry/layout negatives,
source/input quota invalidation, host-native refusal, opt-in and budget failures,
independent ownership domains and hostile default PMR. Four-backend push lowering
also runs with the default allocator disabled. A separate isolated copied-fixture
test deletes all artifact files after original owners load and then repeats the
validation. Neither test is Apple native execution evidence.

`arp_rhi_metal_original_pipeline_tests --native <sampled.module.yaml>
<storage.module.yaml> <compute.entry.yaml>` is the required native creation smoke.
It demands actual native pipeline payloads, owning layout copies, no bindless
credential, source retention after wrapper destruction, and quota-pressure
rollback. On Apple, set all three `ARP_METAL_ORIGINAL_*` CMake FILEPATH cache
variables to register `arp_rhi_metal_original_pipeline_native_smoke`; missing
publications do not produce a successful skipped native test. No native smoke has
been run on this Windows host.

Existing Null hidden-frame/shared workload and native Vulkan regression paths
remain mandatory for this stage. Draw/dispatch, issuer-owned output copies,
same-drawable present, callbacks/loss, native visual evidence and target platform
acceptance are still required before accepting the Metal shared execution package.
See `rhi-metal-shared-original-output-decision.md`. P4-14 stays open.

## Host verification checkpoint (2026-10-20)

MinGW Debug and Release each passed 27/27 focused tests, including Null hidden-frame
and actual Vulkan regression paths. WebGL passed 3/3, private WebGPU passed 3/3,
and seven shared WebGPU execution/loss browser paths passed. Source/bundle custody
was unchanged. Source rules passed 7/7 and eight owned C++ files passed locked
clang-format 21.1.8. These results grant no Apple creation/command/output evidence.
The three pre-existing user test edits are excluded from the commit.

Receipts: `build/p4k-{debug,release}-final-custody.json`,
`build/p4h-{webgl,webgpu}-p4k-final-custody.json`,
`build/webgl1-evidence/p4/p4k-shared-final/custody.json`, and
`build/p4k-style.json`. Earlier Vulkan/WebGPU capture baselines remain unchanged.
