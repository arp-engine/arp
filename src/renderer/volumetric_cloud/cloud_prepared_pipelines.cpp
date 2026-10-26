#include "renderer/volumetric_cloud/cloud_prepared_pipelines.h"

namespace arp::renderer::cloud {
namespace {
using namespace rhi;
using D = DescriptorType;

bool bindingsMatch(const ShaderPackageDesc& package,
    ShaderStage stage,
    core::ArrayView<D> types,
    std::uint32_t depthBinding) noexcept
{
    if (package.reflection.pushConstantByteCount
        || package.reflection.descriptorBindings.size() != types.size())
        return false;
    // Match binding identity, not reflection iteration order.
    for (std::size_t binding = 0u; binding < types.size(); ++binding) {
        std::size_t matches = 0u;
        for (const auto& reflected : package.reflection.descriptorBindings)
            if (reflected.set == 0u && reflected.binding == binding
                && reflected.type == types[binding] && reflected.stages == stage
                && reflected.arrayCount == 1u
                && reflected.depthTexture == (binding == depthBinding))
                ++matches;
        if (matches != 1u)
            return false;
    }
    return true;
}

bool computeEntry(const ShaderPackageDesc& p) noexcept
{
    return p.entrypoints.size() == 1u && p.entrypoints[0].stage == ShaderStage::Compute
        && p.entrypoints[0].entryPoint == "computeMain"
        && p.entrypoints[0].computeWorkgroupSizeX == kCloudWorkgroupWidth
        && p.entrypoints[0].computeWorkgroupSizeY == kCloudWorkgroupHeight
        && p.entrypoints[0].computeWorkgroupSizeZ == 1u;
}

std::shared_ptr<PipelineLayout> createLayout(GpuDevice& device, const ShaderPackageDesc& package)
{
    DescriptorLayoutDesc bindings;
    bindings.name = package.label;
    for (const auto& reflected : package.reflection.descriptorBindings) {
        DescriptorBindingDesc binding{reflected.binding, reflected.type, reflected.stages};
        binding.arrayCount = reflected.arrayCount;
        binding.depthTexture = reflected.depthTexture;
        if (binding.type == D::SampledTexture) {
            binding.textureViewDimensionConstrained = true;
            binding.textureViewDimension = TextureViewDimension::Texture2D;
            binding.sampledTextureFilterable = false;
        }
        bindings.bindings.push_back(binding);
    }
    PipelineLayoutDesc layout;
    layout.descriptorSets.push_back({0u, bindings.name});
    layout.descriptorLayouts.push_back(std::move(bindings));
    return core::shareUnique(device.createOriginalPipelineLayout(layout));
}
} // namespace

CloudResult CloudPreparedPipelines::validateSelection(
    const CloudPreparedPipelineSelection& c) noexcept
{
    if (c.contractVersion != 1u)
        return CloudResult::failure("CLOUD-PREPARED-PIPELINE-SELECTION-VERSION");
    switch (c.resolve) {
    case CloudPreparedResolve::Temporal1:
        if (c.reconstruction.contractVersion || c.reconstruction.maxRadianceRange != 0.f
            || c.reconstruction.maxTransmittanceRange != 0.f)
            return CloudResult::failure("CLOUD-PREPARED-PIPELINE-UNUSED-RECONSTRUCTION");
        return {};
    case CloudPreparedResolve::Reconstruction1:
        return CloudReconstruction::validate(c.reconstruction);
    default:
        return CloudResult::failure("CLOUD-PREPARED-PIPELINE-RESOLVE");
    }
}

CloudResult CloudPreparedPipelines::validateInputs(const CloudPreparedPipelineSelection& c,
    const ShaderPackageDesc& raymarch,
    const ShaderPackageDesc& resolve,
    const ShaderPackageDesc& composite) noexcept
{
    if (const auto result = validateSelection(c); !result.success)
        return result;
    constexpr std::array rayTypes{
        D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::StorageBuffer};
    constexpr std::array temporalTypes{
        D::UniformBuffer, D::ReadOnlyStorageBuffer, D::ReadOnlyStorageBuffer, D::StorageBuffer};
    constexpr std::array spatialTypes{D::UniformBuffer,
        D::ReadOnlyStorageBuffer,
        D::ReadOnlyStorageBuffer,
        D::StorageBuffer,
        D::ReadOnlyStorageBuffer,
        D::SampledTexture,
        D::UniformBuffer};
    constexpr std::array compositeTypes{
        D::UniformBuffer, D::ReadOnlyStorageBuffer, D::SampledTexture, D::SampledTexture};
    const bool spatial = c.resolve == CloudPreparedResolve::Reconstruction1;
    if (!computeEntry(raymarch) || !bindingsMatch(raymarch, ShaderStage::Compute, rayTypes, 2u))
        return CloudResult::failure("CLOUD-PREPARED-PIPELINE-RAYMARCH-ABI");
    if (!computeEntry(resolve)
        || !(spatial ? bindingsMatch(resolve, ShaderStage::Compute, spatialTypes, 5u)
                     : bindingsMatch(resolve, ShaderStage::Compute, temporalTypes, UINT32_MAX)))
        return CloudResult::failure("CLOUD-PREPARED-PIPELINE-RESOLVE-ABI");
    if (composite.entrypoints.size() != 2u || composite.entrypoints[0].stage != ShaderStage::Vertex
        || composite.entrypoints[0].entryPoint != "vertexMain"
        || composite.entrypoints[1].stage != ShaderStage::Fragment
        || composite.entrypoints[1].entryPoint != "fragmentMain"
        || !bindingsMatch(composite, ShaderStage::Fragment, compositeTypes, 3u))
        return CloudResult::failure("CLOUD-PREPARED-PIPELINE-COMPOSITE-ABI");
    return {};
}

CloudResult CloudPreparedPipelines::planResources(const CloudPreparedPipelineSelection& selection,
    const CloudPreparedResourceRequest& request,
    const ComputeResourceCapabilities& limits,
    CloudPreparedResourcePlan& output) noexcept
{
    return planResourcesWithAdmission(selection, request, limits, nullptr, output);
}

CloudResult CloudPreparedPipelines::planResources(const CloudPreparedPipelineSelection& selection,
    const CloudPreparedResourceRequest& request,
    const ComputeResourceCapabilities& limits,
    const CloudOriginalDispatchAdmission& admission,
    CloudPreparedResourcePlan& output) noexcept
{
    return planResourcesWithAdmission(selection, request, limits, &admission, output);
}

CloudResult CloudPreparedPipelines::planResourcesWithAdmission(
    const CloudPreparedPipelineSelection& selection,
    const CloudPreparedResourceRequest& request,
    const ComputeResourceCapabilities& limits,
    const CloudOriginalDispatchAdmission* admission,
    CloudPreparedResourcePlan& output) noexcept
{
    output = {};
    if (const auto result = validateSelection(selection); !result.success)
        return result;
    CloudPreparedResourcePlan next;
    const bool spatial = selection.resolve == CloudPreparedResolve::Reconstruction1;
    if (spatial) {
        if (const auto result = admission
                ? CloudReconstruction::describeResources(selection.reconstruction,
                      request.config,
                      request.extent,
                      request.assetBytes,
                      sizeof(CloudGpuConstants),
                      limits,
                      *admission,
                      next.reconstruction)
                : CloudReconstruction::describeResources(selection.reconstruction,
                      request.config,
                      request.extent,
                      request.assetBytes,
                      sizeof(CloudGpuConstants),
                      limits,
                      next.reconstruction);
            !result.success)
            return result;
        next.raw = next.reconstruction.raw;
    } else if (const auto result = admission
            ? CloudExecutionPlanner::describeResources(request.config,
                  request.extent,
                  request.assetBytes,
                  sizeof(CloudGpuConstants),
                  limits,
                  *admission,
                  next.raw)
            : CloudExecutionPlanner::describeResources(request.config,
                  request.extent,
                  request.assetBytes,
                  sizeof(CloudGpuConstants),
                  limits,
                  next.raw);
        !result.success)
        return result;
    // describeResources has already checked the frames-in-flight products.
    next.descriptorSets = request.config.framesInFlight * kCloudPipelineCount;
    next.descriptorElements = request.config.framesInFlight
        * (spatial ? CloudReconstruction::descriptorElementsPerSlot
                   : kCloudDescriptorElementsPerSlot);
    if (next.descriptorSets > request.maxDescriptorSets
        || next.descriptorElements > request.maxDescriptorElements)
        return CloudResult::failure("CLOUD-PREPARED-PIPELINE-DESCRIPTOR-BUDGET");
    output = next;
    return {};
}

CloudResult CloudPreparedPipelines::create(
    const CloudPreparedPipelineCreateContext& c, core::pmr::String& diagnostic)
try {
    diagnostic.clear();
    const auto fail = [&](const char* id) {
        if (diagnostic.empty())
            diagnostic = id;
        return CloudResult::failure(id);
    };
    if (created())
        return fail("CLOUD-PREPARED-PIPELINE-ALREADY-CREATED");
    if (const auto result = validateSelection(c.selection); !result.success)
        return fail(result.diagnostic);
    CloudPreparedResourcePlan resources;
    const CloudOriginalDispatchAdmission admission{c.device, c.dispatchDomain};
    if (const auto result = planResourcesWithAdmission(c.selection,
            c.resources,
            c.device.capabilities().computeResources,
            c.dispatchDomain ? &admission : nullptr,
            resources);
        !result.success)
        return fail(result.diagnostic);
    if (!c.device.capabilities().memory.hostVisible)
        return fail("CLOUD-CAPABILITY-UNAVAILABLE");
    const auto& b = c.budget;
    if (!c.orchestrationQuota || !b.maxCpuOrchestrationBytes
        || c.orchestrationQuota->limitBytes() != b.maxCpuOrchestrationBytes)
        return fail("CLOUD-PREPARED-PIPELINE-ORCHESTRATION-QUOTA");
    // This owner consumes only the already approved synchronous /1 creation
    // and /2 accepted-wrapper profiles. Expanded profiles need separate work.
    if (b.computeInput.contractVersion != 1u || b.graphicsInput.contractVersion != 1u
        || b.computeInput.availableTargetContractVersion || b.computeInput.prototypeOptIn
        || b.graphicsInput.availableTargetContractVersion || b.graphicsInput.prototypeOptIn
        || !b.computeInput.maxCpuMetadataBytes || !b.graphicsInput.maxCpuMetadataBytes
        || b.computeWrapper.contractVersion != 2u || b.graphicsWrapper.contractVersion != 2u)
        return fail("CLOUD-PREPARED-PIPELINE-BUDGET-PROFILE");
    for (const auto* id :
        {OriginalSynchronousComputeCreationContract::validateBudget(b.computeCreation),
            OriginalSynchronousGraphicsCreationContract::validateBudget(b.graphicsCreation),
            OriginalAcceptedComputePipelineFactoryContract::validateBudget(b.computeWrapper),
            OriginalAcceptedGraphicsPipelineFactoryContract::validateBudget(b.graphicsWrapper)})
        if (id)
            return fail(id);
    for (const auto& q : c.quotas)
        if (!q.artifact || !q.input || !q.owner || !q.native || !q.wrapper
            || core::SharedOwnerProjection::sameOwner(q.owner, q.native))
            return fail("CLOUD-PREPARED-PIPELINE-QUOTA");
    const char* id = nullptr;
    auto* extension = OriginalPreparedCommandDeviceExtension::query(c.device, c.preparation, id);
    if (!extension)
        return fail(id);
    const bool spatial = c.selection.resolve == CloudPreparedResolve::Reconstruction1;
    const auto& resolvePath = spatial ? c.publications.spatialResolve : c.publications.temporal;
    const auto& compositePath =
        spatial ? c.publications.spatialComposite : c.publications.composite;
    if (c.publications.raymarch.empty() || resolvePath.empty() || compositePath.empty())
        return fail("CLOUD-PREPARED-PIPELINE-PUBLICATION-PATH");
    core::CpuMemoryQuotaScope orchestration(c.orchestrationQuota);
    std::array<std::shared_ptr<const OriginalSingleEntryPipelineInput>, 2u> inputs;
    const std::array paths{&c.publications.raymarch, &resolvePath};
    for (std::size_t i = 0u; i < inputs.size(); ++i) {
        const auto artifact = shader::SingleEntryShaderArtifact::load(
            paths[i]->c_str(), b.computeLoad, c.quotas[i].artifact, diagnostic);
        if (!artifact)
            return fail("CLOUD-PREPARED-PIPELINE-PUBLICATION");
        inputs[i] = OriginalSingleEntryPipelineInput::prepare(
            artifact, b.computeInput, c.quotas[i].input, diagnostic);
        if (!inputs[i])
            return fail("CLOUD-PREPARED-PIPELINE-INPUT");
    }
    const auto artifact = shader::GraphicsShaderModuleArtifact::load(
        compositePath.c_str(), b.graphicsLoad, c.quotas[2].artifact, diagnostic);
    if (!artifact)
        return fail("CLOUD-PREPARED-PIPELINE-PUBLICATION");
    const auto graphics = OriginalGraphicsPipelineInput::prepare(
        artifact, b.graphicsInput, c.quotas[2].input, diagnostic);
    if (!graphics)
        return fail("CLOUD-PREPARED-PIPELINE-INPUT");
    if (const auto result = validateInputs(
            c.selection, inputs[0]->package(), inputs[1]->package(), graphics->package());
        !result.success)
        return fail(result.diagnostic);
    // Build into local owners. No partially-created pipeline set is published.
    std::array<std::shared_ptr<ComputePipeline>, 2u> compute;
    for (std::size_t i = 0u; i < compute.size(); ++i) {
        const auto& input = inputs[i];
        auto layout = createLayout(c.device, input->package());
        if (!layout)
            return fail("CLOUD-PREPARED-PIPELINE-LAYOUT");
        ComputePipelineDesc desc;
        desc.label = i ? (spatial ? "Cloud.SpatialResolve" : "Cloud.Temporal") : "Cloud.Raymarch";
        desc.layout = layout.get();
        desc.shaderPackage = &input->package();
        desc.computeShader = input->package().entrypoints.front();
        if (c.dispatchDomain) {
            const auto& entry = desc.computeShader;
            const auto groups = i && spatial ? resources.reconstruction.dispatchGroups
                                             : resources.raw.dispatchGroups;
            if (const auto* rejection =
                    c.device.validateOriginalComputeDispatchDomain(c.dispatchDomain,
                        {entry.computeWorkgroupSizeX,
                            entry.computeWorkgroupSizeY,
                            entry.computeWorkgroupSizeZ},
                        groups))
                return fail(rejection);
        }
        const auto creation = c.device.createOriginalSynchronousComputeCreation(
            input, layout, desc, b.computeCreation, c.quotas[i].owner, c.quotas[i].native, id);
        if (!creation)
            return fail(id ? id : "CLOUD-PREPARED-PIPELINE-CREATION");
        compute[i] = OriginalRegisteredPipeline::shareAcceptedOriginal(
            c.device.createOriginalArtifactComputePipeline(
                creation, b.computeWrapper, c.quotas[i].wrapper),
            id);
        if (!compute[i])
            return fail(id ? id : "CLOUD-PREPARED-PIPELINE-WRAPPER");
    }
    auto layout = createLayout(c.device, graphics->package());
    if (!layout)
        return fail("CLOUD-PREPARED-PIPELINE-LAYOUT");
    RenderPipelineDesc desc;
    desc.label = spatial ? "Cloud.SpatialComposite" : "Cloud.Composite";
    desc.layout = layout.get();
    desc.shaderPackage = &graphics->package();
    desc.vertexShader = graphics->package().entrypoints[0];
    desc.fragmentShader = graphics->package().entrypoints[1];
    desc.colorFormat = TextureFormat::Rgba16Float;
    desc.colorTargets = {{TextureFormat::Rgba16Float}};
    desc.cullMode = CullMode::None;
    desc.depthStencil.depthTestEnabled = desc.depthStencil.depthWriteEnabled = false;
    desc.dynamicViewportScissor = true;
    const auto creation = extension->createGraphics(
        graphics, layout, desc, b.graphicsCreation, c.quotas[2].owner, c.quotas[2].native, id);
    if (!creation)
        return fail(id ? id : "CLOUD-PREPARED-PIPELINE-CREATION");
    auto composite = OriginalRegisteredPipeline::shareAcceptedOriginal(
        c.device.createOriginalArtifactGraphicsPipeline(
            creation, b.graphicsWrapper, c.quotas[2].wrapper),
        id);
    if (!composite)
        return fail(id ? id : "CLOUD-PREPARED-PIPELINE-WRAPPER");
    selection_ = c.selection;
    resources_ = resources;
    compute_ = std::move(compute);
    composite_ = std::move(composite);
    dispatchDomain_ = c.dispatchDomain;
    return {};
} catch (const std::bad_alloc&) {
    diagnostic = "CLOUD-PREPARED-PIPELINE-ALLOCATION";
    return CloudResult::failure("CLOUD-PREPARED-PIPELINE-ALLOCATION");
}

void CloudPreparedPipelines::release() noexcept
{
    composite_.reset();
    compute_ = {};
    dispatchDomain_.reset();
    selection_ = {};
    resources_ = {};
}
} // namespace arp::renderer::cloud
