# WebGlExtensionExecution/1

L2 retains its existing index format, vertex/instance input rate, instance count,
depth/stencil attachment, sampled depth view and shader derivative semantics.
This is a versioned backend lowering of those intents, not an extension of L2.
No renderer/game may branch on GL objects, extension names or native enums.

| Backend | Outcome for this WebGL-specific execution request |
|---|---|
| Null | Validator only; no native extension payload |
| Vulkan | DiagnosticFailure / not applicable; existing L2 native lowering unchanged |
| Metal | DiagnosticFailure / not applicable; existing L2 native lowering unchanged |
| WebGPU | DiagnosticFailure / not applicable; existing L2 native lowering unchanged |
| WebGL | Explicit Prototype opt-in, native queried/enabled extensions and original-domain payload; otherwise deterministic diagnostic failure |

The original context creation intent is the only extension opt-in authority.
A resource ownership request declares a separate execution workload subset through
`WebGlExtensionExecutionRequest`. Version zero with an empty subset is the closed
baseline. Version one requires Prototype opt-in and a nonempty subset. The cold
resource factory fixes the lowering before any resource/pipeline/command issue.
Presence, native enablement and caller opt-in must all cover the workload subset;
no default descriptor or capability application enables a branch. Restoration
requeries native availability and reconstructs this same original workload intent.

Native mappings:

- Uint32 uses OES_element_index_uint, original Uint32 index bytes and
  GL_UNSIGNED_INT; the shared index parser validates exact source ranges and
  extrema. No index truncation or CPU geometry rewrite.
- Instancing uses ANGLE_instanced_arrays, divisor zero for Vertex and one for
  Instance streams, and original instance count. FirstInstance/baseVertex and
  primitive restart retain their existing deterministic rejection. Instance
  ranges/initialization use the existing shared vertex consumption contract.
  Every enabled instance path restores all attribute divisors explicitly.
- VAO uses OES_vertex_array_object. A cold original pipeline owns one native VAO
  with original domain/epoch/quota/registry custody. Each issue binds that exact
  owner and writes the complete captured draw layout/buffers/index binding;
  no hot VAO creation or reliance on persistent external state. Deletion occurs
  only in the original live context; loss retires the native owner without
  deleting equal numeric handles in a restored context.
- Depth textures use WEBGL_depth_texture and the exact L2 Depth24Stencil8
  Sampled|RenderAttachment role. Native storage is DEPTH_STENCIL with
  UNSIGNED_INT_24_8_WEBGL and a texture framebuffer attachment. Cold native
  allocation/FBO completeness is queried, separately from packed renderbuffer
  facts. Client upload, comparison sampling, mip chains and unproved formats
  remain rejected. Depth initialization follows actual load/store and render
  execution; attachment-to-sampled transitions and original reader custody are
  retained. No CPU shadow fabricates rendered depth bytes.
- Derivatives consume only the original Slang-derived program publication.
  SPIR-V operations and required extension metadata must agree; the original
  domain workload, context enablement and shader limits must all allow
  OES_standard_derivatives. GLSL ES100 is generated offline. Program identity
  and pipeline keys retain the exact shader publication and lowering selection.

All storage/owners are charged to existing original quotas. Recording, successful
issue and update remain allocation-free; creation/query/compile/link are cold.
Telemetry counts actual core and extension calls; native extension calls never
stand in for GPU completion. Capability/factory/NativeSync production gates remain
closed until their independent original plan requirements are satisfied.

Acceptance is one complete P3-08 batch: fast contract/Null smoke, enabled native
five-extension workload with distinct instance outputs, Uint32 indices, native
VAO, depth attachment-to-sampled output and derivative-dependent pixels; disabled
opt-in and unavailable native extension negatives for every branch; stale owners,
restore/full state, quota and hot-path checks; Slang SPIR-V/MSL/reflection outputs;
RenderDoc replay and AI image inspection. Definitions alone do not close P3-08.

Status: Contract Complete / Prototype Complete. Bounded Chrome WebGL1 / ANGLE
D3D11 Backend Validated; no mini-program or Apple/mobile Platform Validated and
no Production Ready. Acceptance: 14 host CTests, 5 isolated browser CTests,
10 inventory checks, frozen root browser suites and four RenderDoc captures with
24 validated instanced draws. AI inspected the final red/green output. The capture
driver profile-cleanup failure is retained separately; it does not negate native
runtime/replay evidence or become a clean-teardown claim. Evidence:
`build/webgl1-evidence/p3/extensions-native-final-2/checkpoint.yaml`.
