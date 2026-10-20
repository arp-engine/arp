# Canonical ordinary color Graphics reads /3

Status: implementation in progress; no execution or common engine grant.

This explicit profile is the canonical control1 counterpart of native control3
ordinary color/HDR. It uses the existing whole-upload creator/packing/source,
Root2 inventory, Content9 transaction, original journal and completion chain.
It does not add a texture-content control, executor or completion registry.

Publication6 retains its exact version, original completed Source and physical
descriptor identity. It admits whole sample1 RGBA8/RGBA16F 2D, 2DArray and one
square six-face Cube, independently of Publication2's RGBA8 2D/array restriction.
Old native Publication1/5 still require control3. Ordinary writes and legacy
indexed image consumers must reject Publication6. Publication alone cannot
create a physical bind group, execute a shader or change resource state.

Content9 explicitly selects `bindlessCompletedReadContractVersion=3`, choosing
Read18 and Pending9. Read18 requires genuine completed control1 content and the
same whole color-view geometry. Pending9 authenticates the existing original
transaction/journal under canonical Lifecycle12; both CopyDst->ShaderRead and
already-ShaderRead reads preserve the existing generation/revision invariants.
Read8/10/17 and Pending5/6/7/8 retain their original control/format/profile scope.

| Backend | Publication6 | Read profile3 / Read18 |
| --- | --- | --- |
| Null | Validator only, no publication payload | CPU validator only |
| Vulkan | Explicit profile refusal; native Publication1/5 unchanged | Explicit canonical-profile refusal; native profile2 unchanged |
| Metal | Explicit profile refusal; native Publication1/5 unchanged | Explicit canonical-profile refusal; native profile2 unchanged |
| WebGPU | Retained metadata with real original Source/queried native identity | Equivalent CPU planning, execution separately gated |
| WebGL | Explicit unavailable | Explicit unavailable |

Required execution acceptance uses original public Graphics roots and actual
browser callbacks/readback. The same Slang RGBA8/HDR workload must preserve
Vertex/Fragment sampling, independent sampler, all Cube faces/mips/array layers,
fractional LOD, clean/fault output, cancellation and retirement. Queried limits,
reserved bindings and actual allocation owners remain mandatory. Native object
creation or budget failure publishes no partial state. Current planning and
publication tests cannot replace browser execution or whole-scene acceptance.
