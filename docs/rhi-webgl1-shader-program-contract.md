# WebGlShaderProgram/1 implementation contract

L2 intent: one traditional VS/FS program, original generated stage artifacts,
explicit vertex locations, scalar Texture2D/sampler pairs, CPU-authored uniform
and full-block push bytes, immutable per-draw snapshots. No native handle/location
is serialized. Compiler success is compile-ready artifact status, not execution
or completion authority. Null validates; Vulkan/Metal/WebGPU keep their original
artifacts and execution contracts. WebGL must independently compile/link and
verify the active interface against this ABI at cold program creation in P2.

Add a versioned target artifact composed from existing SingleEntryShaderArtifact
publications. Preserve existing enum/class layouts and old artifact schemas.
The new YAML wrapper hashes source, stage artifacts, generated ES100, reflection,
tool versions/DLL hashes, converter/generator/options/precision/coordinates and
required extensions. Original wrappers hydrate through existing strict loaders;
the wrapper's identity is the cache key, with actual profile identity included
by native pipeline creation. Unsupported existing shader target declarations
are DiagnosticFailure, with no empty successful GLSL file.

Use the fixed SPIRV-Cross C API for production compilation: set ES100, explicit
highp float/int, ordinary uniform blocks, explicit depth fixup and vertex-Y
lowering. Query combined image-sampler IDs directly; never infer pairs from
generated names. Assign interface/root/member names through compiler APIs before
compilation, then derive leaf paths from the exact binary layout. No source
patching, flatten-ubo, dummy sampler, unsigned/bitwise emulation or LOD0 fallback.

Baseline leaf subset: 32-bit float scalar/vector, signed integer scalar/vector,
default Slang float4x4 with explicit row-major compilation, bounded fixed arrays
and structs. Reject runtime arrays, descriptor arrays, unknown layout/padding,
GPU-written uniform sources, dynamic access chains, loops not eliminated by the
compiler, unsupported builtins/stages/storage/image operations and unknown binary
operations. Explicit required/optional leaf declarations control optimized-out
uniform admission. Per-stage vector/unit/attribute limits are computed from the
ABI then checked against queried facts. Missing highp or required extension
rejects before creation; precision never silently decreases.

CPU bytes stay core::Mat4 row-major. SPIR-V default matrices carry the actual
ColMajor transposed logical representation; ordinary GL uniformMatrix4fv(false)
uploads those bytes without a second mathematical transpose. Verify decoration,
default source, push-size and non-symmetric numerical output. Clip Z is [0,w] in
L2, converted once by the compiler to [-w,w]; canonical Vulkan-style negative Y
projection is consumed unchanged and compiler flips clip Y once for GL. Viewport
and scissor convert explicit top-left rectangles at the backend boundary; CPU
raster row order and attachment/readback row order are distinct metadata. Verify
asymmetric attachment round trip and winding in the actual browser fixture.

Snapshot storage uses caller-declared bounded draw/byte limits and core allocators;
recording copies complete uniform/push values, uses explicit initialized coverage,
rejects partial pushes under AtomicFullBlockPush/1, and preserves earlier draws.
Unknown/uninitialized/gpu-written/out-of-range source and dynamic offset reject
before snapshot mutation. Native locations are cold-created, per-program only.

Validation: fast original artifact/hash/schema/layout/limit negative tests and
immutable snapshots; unchanged Null hidden smoke; all new Slang fixtures retain
SPIR-V/MSL/WGSL/reflection; real browser link/interface/two-draw/two-texture/matrix/
clip/blend numerical tests; RenderDoc capture/replay and AI inspection. Completion
of P1-01..11 requires the complete batch to pass. Status and evidence are recorded
in the P1 validation checkpoint, not inferred from this contract.

Implementation uses new composed `WebGlShaderProgramArtifact/1` and
`WebGlShaderTargetArtifact/1` schemas. Existing SingleEntryShaderArtifact,
GraphicsShaderModuleArtifact, ShaderArtifactRole and TargetBackend layouts stay
unchanged. The startup loader hydrates both original stages under the same CPU
quota and aggregate input ledger, re-derives uniform leaves from the existing
binary declaration DAG, and verifies exact variable/load/OpSampledImage
relationships. The admitted sampler dataflow is direct variable loads only;
other dataflow must receive a new proven contract. Interface locations/types are
re-derived from the same original binary. Wrapper hashes provide cache/integrity
identity, not cryptographic authorization of arbitrary hostile shader contents.

The declared optional `arpUbo_0_0.m3` field deliberately remains unused. Native
drivers may keep all members of a uniform struct active: optional permits absence
but never requires optimization. Active members still require exact type and
location. Required renamed fields are rejected by actual program linking checks.

Uniform-vector consumption is an intentionally conservative upper bound: one
vector per scalar/vector/array element, four per mat4, plus sampler uniforms.
It never undercounts by packing unrelated leaves. Native compile/link still has
the final queried implementation limit check. Fragment/vertex highp are required;
the backend queries precision/ranges and never selects mediump. A second Slang
fixture exercises real derivative and explicit LOD operations, their two queried
extensions, numeric output and disabled-extension failure.

HighpFloatAndInt/1 uses the ES 2.0 highp minimums (float precision 16 bits and
exponent range at least 62, integer range at least 16); these are specification
requirements, not device guesses. Fragment coordinate builtins are refused
until their origin lowering is defined. Signed ddy is refused unless every
computational use is the exact sign-invariant FAbs operation imported from
GLSL.std.450. ExtInst arguments, store values, return values and branch conditions
are checked independently of their opcode/result-prefix positions; ddx/fwidth
retain their defined mappings. Explicit image sampling admits scalar Lod only,
and refuses Grad/offset/unknown operands. This bounds the coordinate-sensitive
language subset without source rewriting or a hidden derivative-sign change.

| Backend | P1 conclusion |
| --- | --- |
| Null | Equivalent metadata/ABI/snapshot validation; no GL execution |
| Vulkan | Native original SPIR-V publication preserved; existing hidden smoke regression |
| Metal | Native original MSL publication preserved; Apple execution unvalidated on Windows |
| WebGPU | Native original WGSL publication preserved; existing schema/loader preserved, execution unchanged |
| WebGL | Equivalent ES100 shader lowering; real Chrome/ANGLE program component validation; generic device/queue remains DiagnosticFailure before P2 |

The browser component is backend-private JavaScript. It consumes only metadata
and snapshots from the original C++ loader in this validation path. P2 must bind
it to the actual Emscripten context and native C++ resource/command owner; browser
test transport alone cannot authorize production RHI execution. Cold creation
owns all locations and snapshot capacity. Applying an already prepared snapshot
only selects the program and uploads fixed typed views; no compiler, filesystem,
location query, readback or new view allocation appears in that operation.
Linked shader attachments are explicitly detached and deleted at cold creation;
the native browser test queries the resulting empty attachment list.
