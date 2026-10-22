#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/frontend/pipeline_validation_report_builder.h"
#include "rhi/gpu_bindless.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace arp::rhi_vulkan {

namespace {

std::uint32_t descriptorBindingCount(const rhi::PipelineLayoutDesc& desc)
{
    std::uint32_t count = 0;
    for (const auto& layout : desc.descriptorLayouts) {
        count += static_cast<std::uint32_t>(layout.bindings.size());
    }
    return count;
}

std::uint32_t countPushConstantBytes(const rhi::PipelineLayoutDesc& desc)
{
    std::uint32_t total = 0;
    for (const auto& range : desc.pushConstantRanges) {
        total += range.sizeBytes;
    }
    return total;
}

rhi::DescriptorType descriptorTypeFromShaderBinding(shader::BindingType type)
{
    switch (type) {
    case shader::BindingType::UniformBuffer:
        return rhi::DescriptorType::UniformBuffer;
    case shader::BindingType::StorageBuffer:
        return rhi::DescriptorType::StorageBuffer;
    case shader::BindingType::ReadOnlyStorageBuffer:
        return rhi::DescriptorType::ReadOnlyStorageBuffer;
    case shader::BindingType::SampledTexture:
        return rhi::DescriptorType::SampledTexture;
    case shader::BindingType::StorageTexture:
        return rhi::DescriptorType::StorageTexture;
    case shader::BindingType::Sampler:
        return rhi::DescriptorType::Sampler;
    case shader::BindingType::InputAttachment:
        return rhi::DescriptorType::InputAttachment;
    }
    return rhi::DescriptorType::UniformBuffer;
}

bool shaderStageMatches(shader::ShaderStage cached, rhi::ShaderStage package)
{
    switch (cached) {
    case shader::ShaderStage::Vertex:
        return package == rhi::ShaderStage::Vertex;
    case shader::ShaderStage::Fragment:
        return package == rhi::ShaderStage::Fragment;
    case shader::ShaderStage::Compute:
        return package == rhi::ShaderStage::Compute;
    case shader::ShaderStage::TessellationControl:
        return package == rhi::ShaderStage::TessellationControl;
    case shader::ShaderStage::TessellationEvaluation:
        return package == rhi::ShaderStage::TessellationEvaluation;
    }
    return false;
}

rhi::ValidationResult validateVulkanReflectionArtifact(const rhi::ShaderPackageDesc& package)
{
    if (package.artifacts.reflectionPath.empty()) {
        return {false, "Vulkan RHI requires a generated reflection artifact path"};
    }
    const auto cached =
        shader::ShaderReflectionCache::loadFromFile(package.artifacts.reflectionPath);
    if (!cached.success) {
        auto diagnostics = std::pmr::string("Vulkan RHI could not load reflection artifact: ");
        diagnostics += cached.diagnostics;
        return {false, std::move(diagnostics)};
    }
    const auto artifactIdentityValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(cached.module,
            package.slangModulePath,
            package.artifacts.spirvPath,
            package.artifacts.mslPath,
            package.artifacts.wgslPath);
    if (!artifactIdentityValidation.success) {
        auto diagnostics =
            std::pmr::string("Vulkan RHI reflection artifact identity validation failed: ");
        diagnostics += artifactIdentityValidation.diagnostics;
        return {false, std::move(diagnostics)};
    }
    const auto memberValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(
            cached.module.reflection);
    if (!memberValidation.success) {
        auto diagnostics = std::pmr::string(
            "Vulkan RHI reflection artifact failed push-constant member validation: ");
        diagnostics += memberValidation.diagnostics;
        return {false, std::move(diagnostics)};
    }
    const auto& cachedReflection = cached.module.reflection;
    const auto& packageReflection = package.reflection;
    for (const auto& entrypoint : package.entrypoints) {
        const auto found = std::find_if(cachedReflection.entryPoints.begin(),
            cachedReflection.entryPoints.end(),
            [&](const auto& candidate) {
                return candidate.name == entrypoint.entryPoint
                    && shaderStageMatches(candidate.stage, entrypoint.stage);
            });
        if (found == cachedReflection.entryPoints.end()) {
            return {false, "Vulkan RHI reflection artifact is missing a package entrypoint"};
        }
    }
    if (cachedReflection.pushConstantSizeBytes != packageReflection.pushConstantByteCount) {
        return {false, "Vulkan RHI reflection push-constant size disagrees with package metadata"};
    }
    if (cachedReflection.pushConstantContractVersion
        != packageReflection.pushConstantContractVersion) {
        return {
            false, "Vulkan RHI reflection push-constant contract disagrees with package metadata"};
    }
    if (cachedReflection.pushConstantStages != packageReflection.pushConstantStages) {
        return {false, "Vulkan RHI reflection push-constant stages disagree with package metadata"};
    }
    if (!rhi::ShaderPushConstantAbiIdentity::matchesMembers(
            cachedReflection.pushConstantMembers, packageReflection.pushConstantMembers)) {
        return {
            false, "Vulkan RHI reflection push-constant members disagree with package metadata"};
    }
    if (cachedReflection.bindings.size() != packageReflection.descriptorBindings.size()) {
        auto diagnostics = std::pmr::string(
            "Vulkan RHI reflection descriptor count disagrees with package metadata: artifact=");
        diagnostics += std::to_string(cachedReflection.bindings.size());
        diagnostics += " package=";
        diagnostics += std::to_string(packageReflection.descriptorBindings.size());
        return {false, std::move(diagnostics)};
    }
    for (const auto& cachedBinding : cachedReflection.bindings) {
        const auto found = std::find_if(packageReflection.descriptorBindings.begin(),
            packageReflection.descriptorBindings.end(),
            [&](const auto& candidate) {
                return candidate.set == cachedBinding.set
                    && candidate.binding == cachedBinding.binding;
            });
        if (found == packageReflection.descriptorBindings.end()
            || descriptorTypeFromShaderBinding(cachedBinding.type) != found->type
            || cachedBinding.arrayCount != found->arrayCount
            || cachedBinding.stageMask != static_cast<std::uint32_t>(found->stages)
            || cachedBinding.subpassAttachmentIndex != found->subpassAttachmentIndex
            || cachedBinding.depthTexture != found->depthTexture) {
            return {
                false, "Vulkan RHI reflection artifact disagrees with logical descriptor metadata"};
        }
    }
    return {true, {}};
}

class VulkanRhiPipelineLayout final : public rhi::PipelineLayout {
public:
    explicit VulkanRhiPipelineLayout(
        rhi::PipelineLayoutDesc desc, rhi::semantics::DirectObjectBudgetLease budgetLease)
        : desc_(std::move(desc)), pushConstantBytes_(countPushConstantBytes(desc_)),
          budgetLease_(std::move(budgetLease))
    {}

    const rhi::PipelineLayoutDesc& desc() const override { return desc_; }

    std::uint32_t descriptorSetCount() const override
    {
        return static_cast<std::uint32_t>(desc_.descriptorSets.size());
    }

    std::uint32_t descriptorTableCount() const override
    {
        return static_cast<std::uint32_t>(desc_.descriptorTables.size());
    }

    std::uint32_t pushConstantByteCount() const override { return pushConstantBytes_; }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

private:
    rhi::PipelineLayoutDesc desc_;
    std::uint32_t pushConstantBytes_ = 0;
    rhi::semantics::DirectObjectBudgetLease budgetLease_;
};

rhi::PipelineValidationReport basePipelineReport(const rhi::PipelineLayout* layout,
    const rhi::ShaderPackageDesc* shaderPackage,
    bool authorizedBindless = false)
{
    rhi::PipelineValidationReport report;
    report.backend = rhi::Backend::Vulkan;
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    if (!layout) {
        report.diagnostics = "Vulkan RHI pipeline requires a pipeline layout";
        return report;
    }
    if (!shaderPackage) {
        report.diagnostics = "Vulkan RHI pipeline requires a Slang shader package";
        return report;
    }

    report.descriptorLayoutCount =
        static_cast<std::uint32_t>(layout->desc().descriptorLayouts.size());
    report.descriptorSetCount = layout->descriptorSetCount();
    report.descriptorTableCount = layout->descriptorTableCount();
    report.pushConstantBytes = layout->pushConstantByteCount();
    report.entrypointCount = static_cast<std::uint32_t>(shaderPackage->entrypoints.size());
    report.descriptorBindingCount = descriptorBindingCount(layout->desc());

    const auto shaderValidation = authorizedBindless
        ? rhi::validateAuthorizedBindlessShaderPackage(*shaderPackage)
        : rhi::validateShaderPackage(*shaderPackage);
    if (!shaderValidation.success) {
        report.diagnostics = shaderValidation.diagnostics;
        return report;
    }
    report.shaderPackageValidated = true;

    if (!authorizedBindless) {
        const auto artifactValidation = validateVulkanReflectionArtifact(*shaderPackage);
        if (!artifactValidation.success) {
            report.diagnostics = artifactValidation.diagnostics;
            return report;
        }
    }

    const auto reflectionValidation = rhi::validatePipelineLayoutAgainstShaderReflection(
        layout->desc(), shaderPackage->reflection);
    if (!reflectionValidation.success) {
        report.diagnostics = reflectionValidation.diagnostics;
        return report;
    }
    report.reflectionMatched = true;
    return report;
}

VulkanRhiRetainedGraphicsPipelineState captureRetainedGraphics(
    const rhi::RenderPipelineDesc& d) noexcept
{
    VulkanRhiRetainedGraphicsPipelineState result;
    result.stages = d.vertexShader.stage
        | (d.fragmentShader.entryPoint.empty() ? rhi::ShaderStage::None : d.fragmentShader.stage);
    const auto pass = rhi::OriginalBindlessGraphicsPassContract::capturePipeline(d);
    result.color = pass.colors[0];
    const auto color = rhi::effectiveColorTargetAt(d, 0u);
    result.fullscreenColor = pass.singleSubpass && pass.colorCount == 1u && pass.samples == 1u
        && pass.depth == rhi::TextureFormat::Unknown && d.vertexBuffers.empty()
        && d.topology == rhi::PrimitiveTopology::TriangleList && d.cullMode == rhi::CullMode::None
        && d.dynamicViewportScissor && !d.hasViewport && !d.hasScissor && !color.blendEnabled
        && color.writeMask == rhi::ColorWriteMask::All && !color.resolveTargetRequired
        && !d.depthStencil.depthTestEnabled && !d.depthStencil.depthWriteEnabled
        && !d.depthStencil.stencilEnabled && !d.depthStencil.depthBiasEnabled
        && !d.multisample.alphaToCoverageEnabled && (d.multisample.sampleMask & 1u)
        && d.patchControlPointCount == 0u && d.tessellationControlShader.entryPoint.empty()
        && d.tessellationEvaluationShader.entryPoint.empty();
    return result;
}

class VulkanRhiRenderPipeline final : public rhi::RenderPipeline {
public:
    VulkanRhiRenderPipeline(const rhi::PipelineLayout* layout,
        rhi::ShaderReflectionDesc reflection,
        rhi::PipelineValidationReport report,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout,
        rhi::ShaderStage activeStages,
        rhi::PrimitiveTopology topology,
        rhi::OriginalBindlessGraphicsPassContract::PipelineState passState,
        const rhi::RenderPipelineDesc& definition,
        rhi::BindlessGraphicsPipelineEntries entries)
        : ownedLayout_(std::move(ownedLayout)), layout_(ownedLayout_ ? ownedLayout_.get() : layout),
          reflection_(std::move(reflection)), report_(std::move(report)),
          nativePayload_(std::move(nativePayload)), authorization_(std::move(authorization)),
          activeStages_(activeStages), topology_(topology), passState_(passState),
          vertexBuffers_(rhi::RenderPipelineStateCopy::copy(definition).vertexBuffers),
          graphicsEntries_(entries), retainedGraphics_(captureRetainedGraphics(definition))
    {}

    const rhi::PipelineLayout* layout() const override { return layout_; }

    rhi::ShaderStage bindlessShaderStages() const noexcept override { return activeStages_; }

    const rhi::BindlessPackageModule* bindlessShaderModule(
        rhi::ShaderStage stage, std::uint32_t version) const noexcept override
    {
        return authorization_ ? graphicsEntries_.module(stage, version) : nullptr;
    }

    const char* bindlessOrdinaryBufferRequirement(rhi::ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        rhi::ShaderBufferMemoryRequirement& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if ((version != 1u && version != 2u) || !authorization_)
            return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
        return graphicsEntries_.bufferRequirement(stage, set, binding, false, output, version);
    }

    const char* bindlessOrdinaryBufferMemberMemory(rhi::ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        rhi::ShaderBufferMemberMemory& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if (!authorization_
            || authorization_->package().artifact().execution != rhi::BindlessExecution::Native
            || !nativePayload_
            || nativePayload_->backendVulkanRenderPipelineHandle() == VK_NULL_HANDLE)
            return "RHI-VULKAN-MEMBER-MEMORY-ORIGINAL-PIPELINE";
        return graphicsEntries_.bufferMemberMemory(stage, set, binding, true, output, version);
    }

    rhi::PrimitiveTopology primitiveTopology() const override { return topology_; }

    const core::pmr::Vector<rhi::VertexBufferLayoutDesc>* bindlessVertexBuffers(
        std::uint32_t version) const noexcept override
    {
        return version == 1u && authorization_ ? &vertexBuffers_ : nullptr;
    }

    const rhi::PipelineValidationReport& validationReport() const override { return report_; }

    const rhi::ShaderReflectionDesc& shaderReflection() const override { return reflection_; }

    rhi::ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return rhi::ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    std::shared_ptr<const rhi::BindlessAuthorization>
    bindlessAuthorization() const noexcept override
    {
        return authorization_;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const { return nativePayload_; }

    const auto* retainedGraphicsState() const noexcept
    {
        return authorization_ ? nullptr : &retainedGraphics_;
    }

    const auto* originalBindlessPassState() const noexcept
    {
        return authorization_ ? &passState_ : nullptr;
    }

private:
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout_;
    const rhi::PipelineLayout* layout_ = nullptr;
    rhi::ShaderReflectionDesc reflection_;
    rhi::PipelineValidationReport report_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
    rhi::ShaderStage activeStages_;
    rhi::PrimitiveTopology topology_;
    const rhi::OriginalBindlessGraphicsPassContract::PipelineState passState_;
    const core::pmr::Vector<rhi::VertexBufferLayoutDesc> vertexBuffers_;
    const rhi::BindlessGraphicsPipelineEntries graphicsEntries_;
    const VulkanRhiRetainedGraphicsPipelineState retainedGraphics_;
};

class VulkanRhiComputePipeline final : public rhi::ComputePipeline {
public:
    VulkanRhiComputePipeline(const rhi::PipelineLayout* layout,
        rhi::ShaderReflectionDesc reflection,
        rhi::PipelineValidationReport report,
        std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout)
        : ownedLayout_(std::move(ownedLayout)), layout_(ownedLayout_ ? ownedLayout_.get() : layout),
          reflection_(std::move(reflection)), report_(std::move(report)),
          nativePayload_(std::move(nativePayload)), authorization_(std::move(authorization))
    {}

    const rhi::PipelineLayout* layout() const override { return layout_; }

    const rhi::PipelineValidationReport& validationReport() const override { return report_; }

    const rhi::ShaderReflectionDesc& shaderReflection() const override { return reflection_; }

    rhi::ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return rhi::ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    std::shared_ptr<const rhi::BindlessAuthorization>
    bindlessAuthorization() const noexcept override
    {
        return authorization_;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload() const { return nativePayload_; }

private:
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout_;
    const rhi::PipelineLayout* layout_ = nullptr;
    rhi::ShaderReflectionDesc reflection_;
    rhi::PipelineValidationReport report_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
};

} // namespace

arp::core::UniquePtr<rhi::PipelineLayout> createVulkanRhiPipelineLayout(
    const rhi::PipelineLayoutDesc& desc, rhi::semantics::DirectObjectBudgetLease budgetLease)
{
    if (!rhi::validatePipelineLayout(desc).success) {
        return nullptr;
    }
    return arp::core::makeUnique<VulkanRhiPipelineLayout>(
        arp::rhi::PipelineLayoutStateCopy::copy(desc), std::move(budgetLease));
}

rhi::PipelineValidationReport validateVulkanRhiRenderPipeline(const rhi::RenderPipelineDesc& desc)
{
    auto report = basePipelineReport(desc.layout, desc.shaderPackage);
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        report.diagnostics = "RHI-VULKAN-PATCH-LIST-TESSELLATION-ABI-NOT-LOWERED";
        return report;
    }
    const auto stateValidation = rhi::validateRenderPipelineState(desc);
    rhi::PipelineValidationReportBuilder::validatedGraphicsState(report, stateValidation);
    if (!report.diagnostics.empty()) {
        return report;
    }
    if (!stateValidation.success) {
        report.diagnostics = stateValidation.diagnostics;
        return report;
    }
    report.success = true;
    return report;
}

rhi::PipelineValidationReport validateVulkanRhiComputePipeline(const rhi::ComputePipelineDesc& desc)
{
    auto report = basePipelineReport(desc.layout, desc.shaderPackage);
    if (!report.diagnostics.empty()) {
        return report;
    }
    if (desc.computeShader.stage != rhi::ShaderStage::Compute) {
        report.diagnostics = "Vulkan RHI compute pipeline requires a compute entrypoint";
        return report;
    }
    report.pipelineCacheKey = rhi::stableShaderPackageHash(*desc.shaderPackage);
    report.pipelineCacheKeyStable = report.pipelineCacheKey != 0;
    report.success = true;
    return report;
}

rhi::PipelineValidationReport validateVulkanRhiAuthorizedBindlessComputePipeline(
    const rhi::ComputePipelineDesc& desc)
{
    auto report = basePipelineReport(desc.layout, desc.shaderPackage, true);
    if (!report.diagnostics.empty())
        return report;
    if (desc.computeShader.stage != rhi::ShaderStage::Compute) {
        report.diagnostics = "Vulkan RHI compute pipeline requires a compute entrypoint";
        return report;
    }
    report.pipelineCacheKey = rhi::stableShaderPackageHash(*desc.shaderPackage);
    report.pipelineCacheKeyStable = report.pipelineCacheKey != 0;
    report.success = true;
    return report;
}

rhi::PipelineValidationReport validateVulkanRhiAuthorizedBindlessRenderPipeline(
    const rhi::RenderPipelineDesc& desc)
{
    auto report = basePipelineReport(desc.layout, desc.shaderPackage, true);
    if (!report.diagnostics.empty())
        return report;
    if (desc.vertexShader.stage != rhi::ShaderStage::Vertex
        || (!desc.fragmentShader.entryPoint.empty()
            && desc.fragmentShader.stage != rhi::ShaderStage::Fragment)) {
        report.diagnostics = "Vulkan RHI bindless render pipeline entry stages are invalid";
        return report;
    }
    const auto stateValidation = rhi::validateRenderPipelineState(desc);
    report.colorTargetCount = stateValidation.colorTargetCount;
    report.sampleCount = stateValidation.sampleCount;
    report.colorFormats = stateValidation.colorFormats;
    report.depthStencilFormat = stateValidation.depthStencilFormat;
    report.blendStateValidated = stateValidation.blendStateValidated;
    report.colorWriteMaskValidated = stateValidation.colorWriteMaskValidated;
    report.stencilStateValidated = stateValidation.stencilStateValidated;
    report.depthBiasStateValidated = stateValidation.depthBiasStateValidated;
    report.rasterStateValidated = stateValidation.rasterStateValidated;
    report.topologyValidated = stateValidation.topologyValidated;
    report.msaaStateValidated = stateValidation.msaaStateValidated;
    report.resolveStateValidated = stateValidation.resolveStateValidated;
    report.viewportScissorStateValidated = stateValidation.viewportScissorStateValidated;
    report.depthOnlyStateValidated = stateValidation.depthOnlyStateValidated;
    report.unsupportedStateDiagnosed = stateValidation.unsupportedStateDiagnosed;
    report.pipelineCacheKey = stateValidation.pipelineCacheKey;
    report.pipelineCacheKeyStable =
        stateValidation.success && stateValidation.pipelineCacheKey != 0;
    if (!stateValidation.success) {
        report.diagnostics = stateValidation.diagnostics;
        return report;
    }
    report.success = true;
    return report;
}

arp::core::UniquePtr<rhi::RenderPipeline> createVulkanRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout)
{
    if ((ownedLayout && ownedLayout.get() != desc.layout)
        || (authorization
            && (!authorization->current(authorization)
                || authorization->plan().execution != rhi::BindlessExecution::Native
                || !authorization->plan().executable)))
        return nullptr;
    auto report = validateVulkanRhiRenderPipeline(desc);
    if (!report.success) {
        return nullptr;
    }
    const auto stages = authorization ? rhi::ShaderStage::Vertex
            | (desc.fragmentShader.entryPoint.empty() ? rhi::ShaderStage::None
                                                      : rhi::ShaderStage::Fragment)
                                      : rhi::ShaderStage::None;
    rhi::BindlessGraphicsPipelineEntries entries;
    if (authorization
        && rhi::BindlessGraphicsPipelineEntries::capture(authorization->package(), desc, entries))
        return nullptr;
    return arp::core::makeUnique<VulkanRhiRenderPipeline>(desc.layout,
        desc.shaderPackage->reflection,
        std::move(report),
        std::move(nativePayload),
        std::move(authorization),
        std::move(ownedLayout),
        stages,
        desc.topology,
        rhi::OriginalBindlessGraphicsPassContract::capturePipeline(desc),
        desc,
        entries);
}

arp::core::UniquePtr<rhi::RenderPipeline> createVulkanRhiAuthorizedBindlessRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout)
{
    if (!authorization || !authorization->current(authorization)
        || authorization->plan().execution != rhi::BindlessExecution::Native
        || !authorization->plan().executable || (ownedLayout && ownedLayout.get() != desc.layout))
        return nullptr;
    auto report = validateVulkanRhiAuthorizedBindlessRenderPipeline(desc);
    if (!report.success)
        return nullptr;
    rhi::BindlessGraphicsPipelineEntries entries;
    if (rhi::BindlessGraphicsPipelineEntries::capture(authorization->package(), desc, entries))
        return nullptr;
    return arp::core::makeUnique<VulkanRhiRenderPipeline>(desc.layout,
        desc.shaderPackage->reflection,
        std::move(report),
        std::move(nativePayload),
        std::move(authorization),
        std::move(ownedLayout),
        rhi::ShaderStage::Vertex
            | (desc.fragmentShader.entryPoint.empty() ? rhi::ShaderStage::None
                                                      : rhi::ShaderStage::Fragment),
        desc.topology,
        rhi::OriginalBindlessGraphicsPassContract::capturePipeline(desc),
        desc,
        entries);
}

const rhi::OriginalBindlessGraphicsPassContract::PipelineState*
vulkanRhiBindlessGraphicsPassPipelineState(const rhi::RenderPipeline& pipeline) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiRenderPipeline*>(&pipeline);
    return original ? original->originalBindlessPassState() : nullptr;
}

const VulkanRhiRetainedGraphicsPipelineState* vulkanRhiRetainedGraphicsPipelineState(
    const rhi::RenderPipeline& pipeline) noexcept
{
    const auto* original = dynamic_cast<const VulkanRhiRenderPipeline*>(&pipeline);
    return original ? original->retainedGraphicsState() : nullptr;
}

arp::core::UniquePtr<rhi::ComputePipeline> createVulkanRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout)
{
    if ((ownedLayout && ownedLayout.get() != desc.layout)
        || (authorization
            && (!authorization->current(authorization)
                || authorization->plan().execution != rhi::BindlessExecution::Native
                || !authorization->plan().executable)))
        return nullptr;
    auto report = validateVulkanRhiComputePipeline(desc);
    if (!report.success) {
        return nullptr;
    }
    return arp::core::makeUnique<VulkanRhiComputePipeline>(desc.layout,
        desc.shaderPackage->reflection,
        std::move(report),
        std::move(nativePayload),
        std::move(authorization),
        std::move(ownedLayout));
}

arp::core::UniquePtr<rhi::ComputePipeline> createVulkanRhiAuthorizedBindlessComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout)
{
    if (!authorization || !authorization->current(authorization)
        || authorization->plan().execution != rhi::BindlessExecution::Native
        || (!authorization->plan().executable
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 6u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                authorization->plan()))
        || (ownedLayout && ownedLayout.get() != desc.layout))
        return nullptr;
    auto report = validateVulkanRhiAuthorizedBindlessComputePipeline(desc);
    if (!report.success)
        return nullptr;
    return arp::core::makeUnique<VulkanRhiComputePipeline>(desc.layout,
        desc.shaderPackage->reflection,
        std::move(report),
        std::move(nativePayload),
        std::move(authorization),
        std::move(ownedLayout));
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::RenderPipeline& pipeline)
{
    const auto* vulkanPipeline = dynamic_cast<const VulkanRhiRenderPipeline*>(&pipeline);
    return vulkanPipeline ? vulkanPipeline->nativePayload() : nullptr;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::ComputePipeline& pipeline)
{
    const auto* vulkanPipeline = dynamic_cast<const VulkanRhiComputePipeline*>(&pipeline);
    return vulkanPipeline ? vulkanPipeline->nativePayload() : nullptr;
}

} // namespace arp::rhi_vulkan
