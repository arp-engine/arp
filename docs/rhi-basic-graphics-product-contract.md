# BasicGraphicsProduct/1 — P4-B

Scope: P4-06 through P4-12 form one acceptance batch. This document and CPU
geometry lowering alone do not close those tasks. Status: Contract Complete /
Prototype Complete for the controlled ordered profile. Isolated Chrome WebGL1 /
ANGLE D3D11 native output is Backend Validated; final root and cross-platform
closure are tracked independently by the implementation plan.

The caller explicitly selects a product profile. Device names cannot select an
alternative material, instance policy, color space, font, atlas, light count or
performance budget. All GPU operations use the original P4-A device/issuer and
existing FrameGraph compiler; original timeline paths retain real completion.

The original ordered issuer exposes a borrowed immutable **L2 pipeline
definition**, exact-resource CPU reuse diagnostics and `ClientTextureUpload/1`.
The definition is read from the original same-domain live pipeline, never a
caller report. The renderer validates the vertex/push/descriptor/blend contract
before accepting it, including the caller's explicit cooked Slang source SHA-256
against original pipeline reflection; entry names alone cannot select the source.
Client upload consumes declared top-left straight-alpha
RGBA8 rows through the existing original domain and upload budget; initialized
byte coverage, retained-read/state-journal gates and native failures are unchanged.
The source is consumed within the call. The neutral alias preserves the original
public upload type and mangled signatures. Null has validator-only failure;
Vulkan/Metal/WebGPU require their existing completion upload paths and have no
separate ordered issuer; WebGL is the explicit original-context Prototype.
These operations authorize neither GPU completion nor external sharing.

`BasicForwardLambert/1` explicitly consumes original `MeshAsset` world-space
positions/normals/UVs and original `MaterialResource`/`ShaderPackageResource`
metadata. The material and shader must both declare
`arp/material/basic-forward-lambert` version1 and the caller's exact shader
package identity. No PBR resource is converted to Lambert. Normal/metallic/
occlusion/emissive maps, nonzero metallic/emissive terms and Mask alpha reject.
Each source binding declares straight linear RGBA8. Untextured material intent
still supplies an actual caller-owned image binding, never a dummy texture.
The explicit light budget is zero or one directional light; no light is silently
dropped. A zero light count requires zero directional intensity. Nonfinite or
negative irradiance, zero direction for an enabled light and output irradiance
above the declared LDR range reject before recording. Output is linear
premultiplied RGBA8; Opaque uses alpha1, Blend uses exact authored/source alpha.
Double-sided intent explicitly disables culling and selects two-sided Lambert
lighting. The caller retains transparent draw order; the pipeline never sorts.

`BasicWorldMesh/1` requires explicit `WorldSpaceStatic` geometry, a versioned
index policy and exactly one instance per source draw. `Uint16Exact` preserves
all original indices and fails if any exceeds65535. `RequireUint32` preserves
original32-bit indices and requires the original native extension admission;
no draw is split. A request for multiple instances fails with an explicit
instance-profile requirement; callers must select the original native instance
profile or supply a separately authored baked world-space mesh. Mesh sections
must cover the original index stream contiguously, preserve material slots and
consist of triangles. Vertex/index/material/draw/metadata budgets are declared
before registration. No backend performs partitioning or synthesizes geometry.

Camera bytes are one row-major `core::Mat4` in a vertex-only uniform buffer.
All consumers declare default Slang `float4x4 viewProjection`, compile with the
explicit row-major option and validate ColMajor decoration plus numeric mapping.
The shared camera owner is held by the frame slot; overwrite requires both CPU
graph retirement and the original buffer reuse gate. Shader fragment material
push data is a separate80-byte block with exact source/hash/layout validation.
Depth is an explicitly provided `Depth24Stencil8` attachment with LessEqual;
opaque draws write depth, transparent draws test without writing. Null validates
only; Vulkan/Metal/WebGPU retain original timeline paths, WebGL uses the original
ordered Prototype issuer. Native creation/lowering remains authoritative on all
five backends; unsupported depth/precision/index requirements fail closed.

Scene2D's explicit ordered vertex path retains graph-owned slot and atlas
snapshots before recording. A CPU slot remains reserved while any graph callback
owns it, even before native recording. Its underlying original buffer must also
have no pending journal or recorded read before overwrite. Atlas unload checks
both CPU graph reservations and original native recorded ownership. Graph
destruction cancels unsubmitted reservations; successful issue followed by graph
retirement releases them; partial issue cannot expose the slot as reusable.
The timeline route remains separate and retains its real semaphore/wait behavior.

GUI uses an explicit `GuiCoordinateMapping/1` (rectangle Y and texture UV Y
transforms), not a fictitious Null backend or a backend-name decision. The
ordered top-left raster profile selects identity CPU transforms with the
published overlay vertex formula; ES100 performs its certified clip transform.
Its vertex entry consumes the shared six-corner core topology, avoiding vertex
ID and instance APIs. Existing GuiDrawPlan retains command order and 64-byte
straight-alpha color push ABI. Exact per-command clip, font token, atlas page and
external texture bindings remain mandatory. Ordered GUI supports only cooked
RGBA8 linear bitmap glyphs; SDF requires a separate explicit derivative profile
and is rejected here. No placeholder atlas is created, including for boxes.
`UiBitmapFontCook/1` is an explicit static glyph profile, separate from dynamic
FreeType/SDF text. A cooked resource binds its exact font token, atlas resource
UUID, page, design size, glyph codepoint-to-slice relation, advance and bearings.
The atlas remains the original `SpriteAtlasResource` loaded by ResourceManager.
The declared SHA-256 covers decoded RGBA8 upload bytes. The profile requires
linear, straight-alpha nearest-filtered RGBA8; stale/missing atlas, different
bytes, unknown glyph, duplicate codepoint, absent slice or unsupported profile
fails. It does not guess a font, synthesize a glyph or lay out strings. Existing
text shaping/layout remains authoritative for string-to-glyph geometry. Original
resource generations guard metadata use; graph snapshots retain GPU owners.
The neutral CPU resource contract applies to all five backends, while native
upload/execution continues through each backend's already declared path.
The bitmap fragment entry explicitly produces premultiplied output. Each source
binding declares Straight/Premultiplied alpha and CPU-top-left/attachment origin;
the prepared push selects exact conversion. Attachment UV inversion is explicit
binding metadata, never inferred from texture IDs. Color/alpha blend are both
One/OneMinusSrcAlpha; no gamma conversion occurs. Box commands bind a real
caller-provided source although their shader path does not sample it.

For imported buffers in `ShaderRead`, the initial conservative read dependency
is the union permitted by the original descriptor usage: vertex/index uses
`VertexInput`, indirect uses `DrawIndirect`, uniform/storage uses shader stages.
This describes possible prior readers, not proof that a read executed. It must
never invent ShaderRead access on a vertex-only buffer. Explicit pass accesses
remain exact and usage-validated. Null/Vulkan/Metal/WebGPU/WebGL consume the same
L2 scope; their existing lowering/diagnostic gates remain authoritative.

| Layer | Reused owner/implementation | Required behavior |
|---|---|---|
| Sprite | `SpriteDrawPlan`, `SpriteAtlasResource`, `ResourceManager` | Preserve sorted A/B/A order, batch boundaries, transforms, pivots, flip, UV and premultiplied output. Explicit `SpriteVertexDrawPlan/1` expands each sprite to six vertices; no runtime instance API or hidden draw splitting. Caller declares sprite/vertex-byte budgets. |
| GUI | `UiDrawPlanLowerer`, `GuiDrawPlan`, atlas metadata and descriptor bindings | Preserve command order, glyph source/page, exact external texture identity, clip and straight-alpha semantics. Unsupported SDF/precision must reject unless explicitly admitted by shader/profile. Missing font/atlas or uninitialized data never generates a white placeholder. |
| Frame ownership | Original ordered issuer; existing native timeline owners | Before overwrite/reset/unload, all original recorded references must be released. Accepted issue is command ordering only. Lost/partial issue closes retries and retains pending owners; no frame-number delay or null-fence completion. |
| Static 3D | Existing mesh/material metadata, `core::math`, Slang compiler | Explicit static mesh, base color/texture, normals, bounded simple lights, depth, alpha and double-sided contract. HDR/PBR/DDGI/storage skinning/GPU culling requirements reject; there is no automatic visual substitution. |
| Mesh cook | Existing indexed mesh source and versioned authoring policy | Uint16 limits, extension requirements or explicit offline partition policy are validated before runtime. Partition must preserve every index/vertex/material relation; backend cannot split draws. |
| Offscreen/UI | Existing FrameGraph attachment/sample dependency | Explicit RGBA8 linear color and alpha conversion; exact source owner and UV origin. No sRGB masquerading, automatic gamma or tonemap. |
| Depth diagnostic | Original depth extension query/opt-in, shared camera bytes | Caster and receiver consume identical default Slang matrix declarations, row-major CPU bytes and explicit row-major compiler target. Numerical nonsymmetric probe plus native depth/output capture are required. |

`SpriteVertexDrawPlan/1` consumes the already sorted/culled original plan. Its
triangle order is (0,0), (1,0), (1,1), (0,0), (1,1), (0,1), exactly the existing
Slang sprite shader. Position is origin + axisX*x + axisY*y. UV uses the existing
flip contract; straight color remains unchanged until the sprite fragment stage
performs premultiplication. CPU allocation is bounded and occurs at preparation;
rebuilding within the configured capacity performs no owned allocation. Failed
builds publish no vertices/batches. Resource generations remain original handles.

| Backend | Product admission |
|---|---|
| Null | CPU/command validator and hidden-frame path only; no native output claim |
| Vulkan | Original timeline/completion path; explicit vertex profile may use native vertex draws after shader/package validation |
| Metal | Original completion path; explicit vertex profile requires target shader/package and platform validation |
| WebGPU | Original completion path; explicit vertex profile requires target shader/package and browser validation |
| WebGL | P4-A controlled Prototype owner only; all original format/extension/limit gates remain; isolated Chrome/ANGLE product output validated |

Acceptance combines 2D atlas A/B/A, glyph and image UI with clipping, basic opaque
and transparent 3D, offscreen sampling and the optional depth branch in a fixed
scene/camera. Quick CPU/contract tests and Null hidden-frame precede original
browser execution and RenderDoc inspection. Each new shader is Slang-only and
must publish SPIR-V, MSL, reflection and the explicit ES100 artifact. Production
factory, NativeSync and target-platform readiness remain independent gates.

`PreparedGraphicsCommands/4` adds explicit heterogeneous immutable shader ABIs
between render passes. Versions1-3 retain their same-ABI catalog restriction.
Each compatible original pipeline group owns its original `WebGlUniformSnapshots`
and exact atomic push block; draw payloads pin the group index. Uniform capacity
is the SUM of all group's fixed maxDraws backing, bounded by maxUniformBytes.
Every source/vertex/descriptor/texture scratch array is reserved at cold prepare;
the first explicit setPipeline of a new pass selects that original group. There
is no implicit pipeline selection from attachment shape. Switching incompatible
ABIs inside one pass fails with `RHI-ORDERED-GRAPHICS-ABI-PASS-BOUNDARY-REQUIRED`.
All groups preserve one logical stream-slot and descriptor-set-index topology;
different topology fails `RHI-ORDERED-GRAPHICS-CATALOG-TOPOLOGY` before recording.
Depth presence may differ between targets; each draw checks its actual pipeline
against that pass's exact attachments. Canvas remains a single Clear/Store pass;
fullscreen color and GUI overlay are ordered draws within that physical pass.
All original owners, full queue preflight, mutation journal, partial-issue/loss
gates, allocation quotas and submission ordering remain authoritative. Null is
validator-only; Vulkan/Metal/WebGPU retain their original completion paths and
reject this ordered entry. WebGL's original issuer is explicit Prototype opt-in.
This is an execution organization change, granting no NativeSync or completion.

FrameGraph whole `Depth24Stencil8` attachment writes apply their declared
load/store to BOTH packed aspects. Clear means depth1/stencil0; Load preserves
both; DontCare discards both. The graph must not synthesize stencil Load for a
new packed attachment. Null validates this whole-resource contract; original
Vulkan/Metal/WebGPU/WebGL attachment lowering consumes the same explicit L2
operations or returns its existing format diagnostic. Separate aspect policies
require a separate API; no backend may guess one here.

`BasicDepthDiagnostic/1` is a caller-selected validation scene, requiring the
original context's explicit WEBGL_depth_texture opt-in AND original native
query/enablement. The production base profile never implicitly enables it.
Caster and receiver upload the same 64 row-major bytes, use identical default
`float4x4 viewProjection` declarations and explicit row-major targets. The
receiver's declared clip-to-depth-UV map is (0.5,-0.5,0.5,0.5) for the certified
ordered attachment sampling profile. This is explicit scene binding metadata,
not a backend-name branch. Nearest Level0 sampling returns packed depth as
linear grayscale RGBA8, alpha1. The source has exactly one mip and the sampler
declares nearest min/mag and no mip filtering, so ordinary Sample can only read
Level0 and requires no shader-texture-LOD extension. No depth comparison sampler, color conversion or
shadow effect is implied. The scene contains depth0.25 at left and depth0.75 at
right, verifies that transparent geometry never writes depth, and displays the
actual sampled result through the original GUI external-texture binding.
Missing consent/native support fails before resources. Null remains validator
only; Vulkan/Metal/WebGPU retain original format/capability gates and completion
paths. Backend/Platform validation is awarded only to the paths actually run.
