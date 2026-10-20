# OriginalConformanceSampledExecution/1

Status: Contract Complete / Prototype Complete / bounded Backend Validated (Windows Vulkan, Sampled/PingPong only). No Platform Validated or Production Ready promotion. Final evidence: build/webgl1-evidence/p4/p4f-sampled-original-checkpoint.yaml.

This cold, explicit Prototype extends the original Clear execution with the complete immutable `ConformanceSampledWorkload/1,2` intent. Expected pixels are an independent post-execution oracle; only authored vertices, indices, uniforms, push values, clear colors and resource edges drive native execution.

## L2 and input ownership

The original device alone constructs an execution. Its native-loss observation and original lifetime must already be prepared. The existing observation, CPU, native readback and absolute completion deadline budgets remain mandatory. A separate explicit cumulative native input buffer budget is mandatory. Input count is bounded by two geometry buffers plus one uniform buffer per authored frame, one sampler and at most two pipelines (one per explicit RGBA8/BGRA8 attachment format). All input owners and initialization coverage use the execution's core allocator quota.

The execution creates complete initialized input buffers from immutable authored intent, performs the original mapped upload and flush, then monotonically seals host and GPU writes. A successful ordinary buffer creation, zero allocation, caller bytes or matching mapped contents grant no provenance. Initialization coverage is recorded only after the actual write/flush succeeds. Shared recording consumes these original input objects without native access.

The pipeline consumes the original `GraphicsShaderModuleArtifact` selected SPIR-V bytes directly. Caller filenames/reflection are never artifact authority. The complete reflected ABI and selected entry names must match the original artifact. Pipeline state consumes the original device-owned capability lowering contract and is explicitly validated before native creation and retained privately together with the artifact. Native creation does not reread a mutable SPIR-V path. Sampler creation explicitly fixes nearest, clamp, no mip, no comparison and no anisotropy.

## Original recording language

Only the authored two Clear/Store passes followed by the exact authored draws are accepted. The private journal checks pass count/order, target identity and no aliasing, producer/consumer texture identities, extent/format/subresources, original immutable geometry/uniform buffers, offsets, Uint16 index mode, exact push bytes, viewport/scissor and actual bound original pipeline/sampler/descriptor payloads. Image barriers must preserve the actual prior writer and next fragment consumer stage/access scopes; a same-state enum or compute-only read scope does not prove fragment visibility. Every draw requires all bindings in that pass. Extra commands, copies, compute, indirect work, partial commands and direct queue bypass poison the journal and fail closed before queue acceptance. Immutable buffer ownership remains retained through original completion.

The existing owned staging copy/fence/canonical RGBA8/present serial join is reused for final output. Before acceptance, failure discards preparation; after acceptance, failure retains accepted-work and native owners until genuine completion, never retries or claims rollback. Finalized history releases surface images and keeps actual output bytes and artifact identity. Audit consumes this result and never authorizes execution.

## Versioned lowering

| Backend | Decision |
|---|---|
| Null | Validator only; deterministic no-original-output diagnostic |
| Vulkan | Explicit cold Prototype native input, commands, owned copy and real completion/present; requires original runtime and loss observer |
| Metal | DiagnosticFailure: original shared output join unavailable; no Apple validation claimed |
| WebGPU | DiagnosticFailure for this shared entry; existing private output issuer remains independently tested |
| WebGL1 | Existing ordered original execution required; this native-fence entry fails closed |
| Unknown | Deterministic backend diagnostic |

## Validation and limitations

Keep fast L2/lowering/negative contract tests and Null hidden-frame paths. Native validation must exercise Sampled and four-draw PingPong through the shared recorder with recorder destruction before final execution. RenderDoc must confirm actual native shader bytes, original buffer inputs, intermediate dependencies, owned final copy and actual presented pixels. Device retirement, quota mutation and unsupported backends must refuse audit/execution. Synchronous diagnostic work does not establish production hot-path budgets, NativeSync, platform support or Production Ready.
