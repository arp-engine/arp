# MetalPhysicalStageBinding/1

L2 descriptor visibility describes semantic shader access. Target lowering may
emit the same resource as a native formal argument in additional selected stages.
The backend must bind the same validated resource to every required native stage;
it must not reinterpret a logical stage mask as the native ABI footprint.
The L2 layout/reflection visibility validation remains mandatory and unchanged.

Null validates L2 visibility only. Vulkan uses its set/binding and SPIR-V stage
contract; WebGPU uses its generated group/binding contract. Metal derives physical
stage occupancy from MTLRenderPipelineReflection/MTLComputePipelineReflection at
pipeline creation, matching the selected native functions. This is Equivalent
lowering: no extra semantic access, different resource, missing-resource fallback,
or consumer-visible native state is introduced. No capability or public ABI change.

Reuse MetalBindingSpace/1 for logical-to-physical identity and its separate
buffer/texture/sampler namespaces. Record all native reflection arguments into
fixed-capacity stage tables, using the existing 31/128/16 slot domain. Missing
reflection, unsupported argument kinds or out-of-range slots fail native
pipeline creation with a stable diagnostic. An absent physical stage stays absent;
never infer all stages or widen application descriptors to suppress validation.
The lowering records the complete reflected formal signature without filtering
on the native `active` optimization property. This is a conservative native ABI
rule, not a claim that every inactive parameter is read by the shader. Include
only reflected arguments, without manufacturing bindings for absent parameters.

Direct descriptors and argument-buffer/table roots use native physical stages,
intersected with the encoder's graphics/compute stages. Existing logical validation,
array bounds, candidate preparation, resource/device/lifetime checks remain intact.
Vertex stream lowering retains its conservative union reservation for shader
buffer slots, so additional physical VS arguments cannot alias vertex streams.
Push constants and special pipeline ABIs retain their own explicit contracts.

Tables are pipeline-owned fixed arrays, built once from native reflection; no new
per-draw allocation or native query. Tests must cover namespace independence,
stage accumulation/absence, invalid ranges/types/stages, real descriptor graphics/
compute/table paths, and the WOCC PBR frame that previously aborted API Validation
for gPreviousCamera/gWind/gPreviousWind/gSkinningTemporal fragment bindings.
API Validation and full-size output must pass before claiming backend integration.
