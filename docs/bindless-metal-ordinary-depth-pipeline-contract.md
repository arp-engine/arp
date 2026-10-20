# Metal ordinary depth pipeline declaration

Current Metal execution continuation: public GraphicsExecution2 passes authored
Apple11/11, clean/fault40 numeric lanes/color16, recorded cancel, real completion
and quota return, plus a native submitted-only capture. See
`bindless-metal-ordinary-depth-execution-contract.md`. Final host/shared-harness
verification is pending. The narrower checkpoints below retain their own input
epochs and grant no independent execution authority. Common engine gate=false.

Status: Platform Validated for authored cold Metal pipeline creation. No new
comparison execution or common engine acceptance is claimed. WOCC remains
Traditional.

`OrdinaryDepthPipelineDeclaration/1` now declares Metal cold pipeline support for
scalar ordinary DepthTexture2D and independent comparison samplers at Fragment.
The existing Slang GraphicsDepthColor package, PipelineInterface1, device-issued
authorization and native selected-entry pipeline construction are reused.
Graphics plus graphics-image opt-ins, queried native texture/sampler limits and
the original sampled-depth probe are mandatory. Missing depth substrate refuses
with `RHI-METAL-BINDLESS-ORDINARY-DEPTH-PIPELINE-CAPABILITY` before publication.
Unowned retained pipeline factories keep their buffer-only admission.

| Backend | Declaration | Native closure / execution authority from this contract |
| --- | --- | --- |
| Null | Validator only | None |
| Vulkan | Existing Fragment/Compute declaration | None |
| Metal | Fragment scalar depth/comparison, queried prerequisites | None |
| WebGPU | Explicit pipeline-unavailable diagnostic | None |
| WebGL | Explicit unsupported diagnostic | None |

Metal indexed depth/comparison, ordinary Vertex/Compute depth, arrays, dynamic
offsets, table-backed declarations and unconstrained/non-2D views remain refused.
This declaration checkpoint alone does not widen original Root validators.
Closure1 still accepts color only. The later independent Closure16/Content18
CPU increment is recorded in `bindless-ordinary-depth-native-closure-contract.md`;
GraphicsExecution2 still refuses Metal. Earlier Compute depth profiles do not
acquire ordinary Graphics authority.
No command, state journal, completion pool or dummy image is introduced.

Required evidence: fast four-backend declaration/profile and Metal artifact/probe
refusal tests; Null hidden-frame smoke; genuine Mac public authorization and
clean/fault pipeline creation with two selected native texture slots and three
sampler slots; affected ordinary GI/PBR/Terrain27 platform regression; Slang
SPIR-V/MSL/WGSL/reflection codegen and Emscripten contract compilation.
Pipeline creation is no draw, numeric, visual or capture evidence. Actual ordinary
Graphics comparison still requires Closure16 native correspondence, Content18
pending journal authentication and the real Lifecycle19/Submission13 chain.

Validation (2026-10-19): current Windows contract/Null3/3, Slang depth package
codegen1/1, rebuilt Emscripten/Node1/1, source421/421, style6/6 and seven-file
pinned formatting pass. Actual Apple public authorization and both selected
native pipelines pass; the final fast/platform cohort is4/4. Ordinary RGBA8/HDR
GI, PBR and Terrain27 regressions pass4/4 in the initial Apple cohort with
unchanged runtime source inputs. The staged9198-input final Apple epoch matches
before and after build/tests; this does not seal the current integrated tree.

Two failed fixture attempts remain preserved. The first omitted original
resource registration and correctly refused snapshot creation. The second
expected image opt-in refusal during authorization, while the actual existing
interface gate correctly refused the snapshot earlier. The fixture now supplies
the exact four-buffer/one-arena/one-set registration and checks that earlier
stable diagnostic. No runtime guard was relaxed to repair either failure.
Report: `build/bindless-metal-ordinary-depth-pipeline1/current-report.json`.
