# Ordinary HDR Graphics completed reads /2

Status: implementation in progress; no production engine or WOCC grant.

The selected Graphics content9 budget explicitly requests
`bindlessCompletedReadContractVersion=2`. It selects ReadPlan17 and Pending7,
using the existing original upload control3, canonical inventory, Root2,
resource/content journal and real native completion. Whole RGBA8 or RGBA16F
sample1 color 2D, 2DArray and single Cube views are eligible. Read8 and Read10
retain their existing RGBA8 restriction. Depth, comparison and partial views
remain outside this contract.

Immutable staging /2 adds native RGBA16F uploads using the existing format-aware
packed-copy mapping, original flushed staging lease and upload fence7. Staging
/1 retains its RGBA8 boundary. Metal /2 uses the same format-aware packed
copy mapper and genuine original staging/upload issuer; WebGPU /2 refuses.

Cold Publication5 adds RGBA16F to the original control3 color publication;
the physical descriptor storage must retain and revalidate that version.
Publication1 remains RGBA8. Publication and read-plan validation never authorize
recording, submission or completion by themselves.

| Backend | Mapping |
| --- | --- |
| Null | Validator only; no executable payload |
| Vulkan | Publication5 and Read17 planning; genuine existing Graphics issuer and queried format/allocation support required separately |
| Metal | Explicit staging2, retained Publication5 and Read17/Pending7 on the existing ordinary Graphics Root2/Content9/Lifecycle9/Submission6; implementation validation and Apple acceptance required |
| WebGPU | Explicit unavailable for Publication5 and completed-read /2; previous domains unchanged |

Sampling preserves authored HDR values and independent sampler min/mag/mip
filters. Required numeric coverage includes values above1, distinct faces/mips,
fractional explicit LOD interpolation, Vertex/Fragment use, clean/fault output,
authentic completion and retirement. Actual WOCC HDR cube64/mips7 and LUT128
capacity/whole-scene parity remain separate production acceptance requirements.

The ordinary typed Metal exporter accepts RGBA16F only with retained
Publication5; Publication1 and old indexed completed-image scopes remain
RGBA8. The closure revalidates exact publication Source, whole-view shape and
queried native format/allocation identity. A Graphics command with HDR requires
the original transaction's explicit completed-read2 profile. Before submission
and on completion the same original seal authenticates Pending7 against its
existing journal; no new content state or completion registry is introduced.
Ordinary Compute HDR and indexed HDR images remain refused. Unknown versions,
partial views, depth/comparison, MSAA and unsupported formats remain refused.

The platform test reuses the Vulkan Slang GraphicsHdrGi package, format-aware
original uploads and shared112-lane oracle. It covers values above1, six faces,
two mips/layers, fractional LOD, Vertex/Fragment, exact color/fault, recorded
cancellation and original completion/retirement. Host syntax or generated MSL
alone does not establish Apple compile/execution/capture or a common engine gate.
