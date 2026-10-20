# OrdinaryColorDomain/1

Status: Backend Validated for the authored Vulkan RGBA8 resource domain. This increment preserves the
full production goal; it does not close HDR, depth/comparison, scene equivalence
or the common three-backend engine gate.

The L2 declaration domain is a scalar, non-dynamic, non-table-backed ordinary
sampled color image (explicit 2D, 2DArray or one Cube), or an independent
non-comparison sampler. Vertex, Fragment and Compute visibility are explicit;
missing or foreign stages refuse. Declaration acceptance does not authenticate
resource ownership, prove filterability or grant execution.

The original Vulkan Root2 closure additionally requires the existing immutable
package/selected pipeline, canonical view/sampler and parent identity/generation,
actual native descriptor correspondence and completed whole color upload/control3
through ReadPlan8. Its actual RGBA8/sample1/whole-view restrictions remain. State,
content and fault use the same graph, journal and real fence completion. No new
resource graph, generated owner, replacement image, or caller-issued proof exists.

| Backend | Lowering |
| --- | --- |
| Null | Inspect declarations and genuine Sources; validator only |
| Vulkan | Existing native Root2 descriptors, completed-upload custody and Graphics lifecycle; actual numeric/capture acceptance required |
| Metal | Ordinary Graphics RGBA8 implementation uses a typed Root2 exporter, exact completed whole-upload Sources and the existing selected-stage native bindings; Apple compilation/execution/capture pending |
| WebGPU | Ordinary color closure unavailable; existing scalar-buffer admission rejects images/samplers |

Unknown contract versions and unsupported dimensions, arrays of descriptors,
dynamic/table-backed declarations, depth images and comparison samplers refuse.
Whole-workload feature/format/slot/memory budgets remain independent. Native
format support is not inferred from declaration flags.

The Metal ordinary Graphics implementation is distinct from the indexed
`SampledGraphics1` image domain. Indexed textures remain restricted to that
domain's RGBA8 2D/2DArray Fragment profile. Ordinary scalar RGBA8 whole views
add 2D, 2DArray and a single Cube in explicitly declared Vertex/Fragment stages.
Depth/comparison, descriptor arrays and ordinary Compute images still refuse.
HDR has the separate explicit completed-read2/publication5 profile described in
`bindless-ordinary-hdr-graphics-contract.md`; old default reads remain RGBA8.
The declaration lowering reports native closure implementation, not
platform validation or production readiness.

The typed exporter consumes an authentic Root2 and a matching ordinary set;
untyped `nativePayload()` continues to reject completed images. It compares the
exact descriptor publication Source/control block, whole-view dimensions,
format, control3 and existing native resource identity. Native closure retains
and revalidates every ordinary publication and stage mapping. The original
Content9/Lifecycle9/Submission6 path supplies recording, real completion,
fault publication and retirement; no second executor or journal is introduced.

`arp_rhi_metal_bindless_graphics_color_gi_tests` is the platform acceptance
entry point. It uses the same Slang package, uploads and shared 76-lane oracle
as Vulkan, plus exact 2x2 color readback, fault0/1, cancellation, forged Root
refusal and the existing hot-allocation/retirement checks. The optional second
argument records a Metal capture of submitted work before cancellation tests.
Availability of this test target is not evidence that Apple execution passed.

The authored GraphicsColorGi workload shares the existing Slang GI evaluator
with Compute ColorGi. It uploads actual six-face Cube, 2D LUT and two-layer array,
each with two mips, and an independent sampler. Vertex samples Cube and Fragment
reads all faces/mips/layers plus frame values. Four pixels own disjoint nineteen
word ranges (76 numeric lanes); clean/fault use the same public preparation,
submission, readback and retirement path. RenderDoc independently compares exact
uploads, 76 numerical lanes, frame bytes, color and fault. This is an engine
resource-domain workload, not WOCC lighting/IBL or scene migration acceptance.

Windows registered compiler/GPU/Compute-GI/composed-Graphics/Null tests pass5/5.
Independent RenderDoc replay authenticates the original shader binaries and
checks all three uploaded mip/layer chains, frame16,76 lanes per clean/fault draw,
color and fault0/1; validation messages0. Selected-entry Graphics metadata tests
pass Native/Emulated2/2 on Windows and2/2 on Wasm/Node. Compiler contracts3/3 and
source421/style6/pinned formatting pass. Actual Apple compilation and ordinary
Metal/WebGPU color closures are not claimed. First staging-budget refusal and
subsequent corrected execution are retained. Current evidence:
`build/bindless-ordinary-color-graphics1/current-report.json`.
