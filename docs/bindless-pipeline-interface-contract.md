# Bindless PipelineInterface/1

Metal Root/2 buffer Compute increment (scoped Platform Validated): only scalar ordinary
UniformBuffer/ReadOnlyStorageBuffer/StorageBuffer with exact Compute stages and
no dynamic offsets or table-backed bindings are admitted. The same full
(set,binding) compiler map, canonical inventory and original lifecycle6 are
consumed. The logical group ceiling derives conservatively from the native
function buffer slot ceiling; uniform/storage counts share that ceiling over
the complete pipeline. Standalone prepared binding recording refuses composed
domains. Null validates; Vulkan retains its existing scoped lowering; WebGPU
composition remains refused. Public OriginalExecution/7 uses lifecycle6 and
the same completed handler/journal/result. Twelve Root/2 public cases pass,
with exact indexed49, ordinary49/49 or49/50, fault0/1, cancellation, caller drop,
real device lifetime retirement and quota return0. Native clean/fault capture
and numerical readback are retained; no independent Metal replay is claimed.
Original Graphics/images and scene acceptance remain independent closed gates.
Evidence: D:/arp/build/bindless-metal-domains1/current-report.json.

Current execution increment (2026-10-15): queried Vulkan capabilities explicitly
admit ordinary scalar uniform/read-only/writable buffers by resource class.
Root/2 consumes all original sets in the same canonical inventory, journal,
real fence and retirement chain. Compute three-set output/fault/terminal tests
and public vertex-ID Graphics clean/fault/cancel/budget tests with independent
RenderDoc replay pass. Plain snapshot recording refuses composed
domains with `RHI-BINDLESS-PIPELINE-DOMAINS-ORIGINAL-ROOT-REQUIRED`. Additional
images/samplers, Root/2 geometry, Metal/WebGPU composition and scene support
remain closed. Compute evidence: D:/arp/build/bindless-root-domains2-native1/current-report.json.
Graphics current evidence: D:/arp/build/bindless-root-graphics-domains1/current-report.json;
full existing Vulkan positive/partial/terminal regression passes318.38s.

The following opaque declaration/compiler results are historical scoped evidence.

Ordinary opaque declaration increment (Contract Complete for this subset): optional
`ordinaryOpaqueBindings` requires PipelineInterface/1. Each scalar descriptor
declares exact name/set/binding/type/stages. SampledTexture additionally declares
`textureViewDimension` (Texture2D, Texture2DArray, Texture3D or TextureCube),
strict boolean `depthTexture` and `sampledTextureFilterable`. Sampler declares
`samplerFiltering` (Filtering, NonFiltering or Comparison). Depth3D, storage
images and descriptor arrays remain refused in this increment. The complete
list enters the lowering digest; every item must be active with the exact stage
union, and no ordinary opaque descriptor may be omitted. It never describes an
indexed table or changes the existing indexed image ABI.

SPIR-V independently proves scalar float32 image dimension/array/MS/sample kind
and sampler kind/count. Unknown SPIR-V Depth=2 is not a depth-view proof.
Emulated additionally checks the exact WGSL declaration through the reflected
physical map, including depth and comparison kind. Filterability is explicit
resource intent, not a property inferred from float declarations. SPIR-V sampler
declarations do not prove comparison/filtering use; MSL use, actual view/sampler
state and operation equivalence remain execution gates. All four backends keep
the additional image/sampler execution refusal until the public Root closure and
required use proofs are complete. No sampling operation is rewritten here.

Validation: Windows/Mac compiler39/39, Emscripten/Node focused4/4 with37 real
package invocations, source417/417 and style5/5. Mac debug-layer regression8/8;
Xcode compiles the4 generated GI MSL modules. No new fixture GPU execution is
claimed. Exact source, runners and evidence:
D:/arp/build/bindless-original-buffer-sources1/pipeline-images1-current-report.json.

Prior checkpoint: Contract Complete for the typed-domain/buffer-declaration subset,
including Native host and Emscripten/Node validation. This is an ABI and budget
contract; it does not grant execution, resource content or completion authority.

Current continuation: the shared WGSL declaration reader also exposes exact
scalar opaque target facts, sharing buffer binding budgets/collision detection
and clearing both outputs on failure. Sampler kind/count is checked directly
in SPIR-V; Emulated packages additionally reject regular/comparison sampler
declaration mismatch. This does not prove native comparison/filtering usage or
open external image domains. The WOCC physical-map audit uses this same reader
for all15 variants and caught/fixed the alpha-shadow push-constant annotation.
Metal platform validation now proceeds alongside Vulkan/WebGPU per the latest
user direction; the older Metal-last sequence below is historical.

Additional ordinary Emulated buffers now require the existing bounded
SpirvWgslBufferMemoryLowering relation. The exact reflected physical map selects
the target root; descriptor count/type/address space are still checked first.
A target struct selects BlockStruct, a non-struct uniform selects UniformValue,
and a storage runtime array selects StorageRuntimeArray. Each selects exactly
one approved form, without retry or inferred array wrappers. Graph limits derive
from admitted source/target byte lengths and binding budgets; pair traversal is
bounded by their combined node counts. Scalar identity, vector width, member
offsets, fixed/runtime strides and nesting must agree. Unsupported matrix,
atomic, descriptor-array or array-wrapper relations fail with
RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY. This is declaration representation
validation only: no native range, C++ member identity, initialized content or
resource/completion authority is inferred. Native declarations and additional
domain execution retain their existing gates. Shared bindings also compare their
complete SPIR-V member DAGs across selected entries in Native and Emulated;
disagreement fails with RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-STAGE-MEMORY.
Mac compiler37/37 and8 platform tests, Windows focused3/3, Wasm focused3/3
(62 real package invocations), source417/417 and style5/5 pass. These frame /
instance record fixtures do not prove the complete WOCC production member ABI.
Current evidence and exact source/runner/package hashes:
D:/arp/build/bindless-original-buffer-sources1/pipeline-memory1-current-report.json.

`BindlessShaderArtifact::physicalLayout` remains the indexed snapshot domain.
`additionalDomains` retains the other ordinary descriptor layouts by exact set.
For version1, historical `otherBindGroups` and `reservedPerStage` must be zero.
Counts derive from the actual layouts, including stage unions and array counts.
The plan digest includes every domain set and every binding shape. Binding
numbers may repeat across sets; they must be unique within each set.

`inspectBindlessPipelineInterface` checks the complete interface against queried
group, binding, per-stage and combined image limits. Read-only and writable
storage buffers share one limit. It uses checked bounded counts, performs no
allocation and clears its output on every failure. Its descriptor total covers
all domains; snapshot update/retention budgets still cover only the snapshot
domain. Explicit-domain resource retention must be admitted separately before
recording.

The initial offline/runtime loader increment supports ordinary UniformBuffer,
ReadOnlyStorageBuffer and StorageBuffer in additional domains. Slang compiles
all selected entries to SPIR-V/MSL and, for Emulated, WGSL. The loader merges
the exact active entry stage masks and validates binary descriptor count and
Uniform/StorageBuffer class using the existing declaration verifier. Emulated
also validates the WGSL address space and the supported member representation
relation described above. Native MSL member equivalence, actual WOCC C++ member
identity, native buffer ranges and initialization remain unproved.
The ordinary opaque increment above supersedes the initial blanket refusal for
its explicit declaration subset. Storage format/access, native comparison and
filtering use remain unproved. Emulated ordinary descriptor arrays are rejected; no `binding_array`
fallback is introduced. Existing indexed table contracts remain unchanged.

Four-backend lowering:

| Backend | Interface inspection | Additional-domain execution |
| --- | --- | --- |
| Null | Validator with complete prepared pipeline layout and Root/2 journal | Rejected; no executable payload |
| Vulkan | ABI/budget and shared Root/2 custody | Compute scalar buffers through Root/2; images/samplers and Graphics refused |
| Metal | ABI/budget only | Rejected; argument-buffer ownership/completion not composed |
| WebGPU | ABI/budget and shared Root/2 custody | Rejected; native closure has not consumed ordinary sets |

The shared planner requires each ordinary resource class to be explicitly admitted
in `ordinaryDescriptorDomains`; `executablePayload=true` alone grants nothing.
Existing original execution and cold
preparation profiles cannot inherit this interface by version proximity.

Root Source/2 now composes authentic ordinary Descriptor Sources/2 into the same
canonical inventory and existing Null state/journal; see the original Root
contract for exact lifetime, budget and negative evidence. This is custody and
projection support. Vulkan Compute scalar-buffer execution now has scoped native
proof; other ordinary domains remain refused.
Next: make Metal/WebGPU and Vulkan scene-required Graphics/image domains consume these exact sets through the original
native closure/device budget and genuine completion; prove native GI sampling
use, required storage format/access and actual frame/instance/member ABI.
Only those proofs may open the selected public layouts and execution. Then
move to renderer/WOCC and GPU-driven. Metal advances alongside Vulkan/WebGPU per
the latest user direction. No scene GPU or visual acceptance is claimed here.

Evidence: D:/arp/build/bindless-original-buffer-sources1/pipeline-interface1-current-report.json.
Compiler package tests 36/36, current host tests 4/4, source 417/417 and style 5/5
pass. Emscripten/Node passes the full shared contract, both new package modes
and the too-small package binding budget. The aggregate contract target needs
an explicit bounded test stack; its default 64KiB run failed and is retained.
The test target now has a 1MiB ceiling and stack-overflow checks. Production
GPU/resource budgets are unchanged. The rebuilt Vulkan public depth workload
and independent RenderDoc replay pass with the original numerical outputs,
fault0/1 and zero validation messages; they validate the existing single-domain
path, not additional-domain execution. Initial fixture/provenance mismatches
and the transient unrelated editor configure failure remain in the logs.
