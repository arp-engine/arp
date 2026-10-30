#include "rhi/backends/webgpu/webgpu_backend_objects.h"

#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/backends/webgpu/webgpu_original_descriptor_buffer_limits_query.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/buffer_placement.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/frontend/pipeline_validation_report_builder.h"
#include "rhi/gpu_bindless.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

namespace arp::rhi_webgpu {

namespace {

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool textureAspectMatchesFormat(rhi::TextureFormat format, rhi::TextureAspect aspect)
{
    if (format == rhi::TextureFormat::Depth24Stencil8) {
        return aspect == rhi::TextureAspect::Depth || aspect == rhi::TextureAspect::Stencil;
    }
    if (rhi::isDepthTextureFormat(format)) {
        return aspect == rhi::TextureAspect::Depth;
    }
    return aspect == rhi::TextureAspect::Color;
}

bool textureViewRangeWithinTexture(
    const rhi::TextureDesc& texture, const rhi::TextureViewDesc& view)
{
    if (view.mipLevelCount == 0u || view.arrayLayerCount == 0u
        || view.baseMipLevel >= texture.mipLevelCount
        || view.baseArrayLayer >= texture.arrayLayerCount
        || view.mipLevelCount > texture.mipLevelCount - view.baseMipLevel
        || view.arrayLayerCount > texture.arrayLayerCount - view.baseArrayLayer) {
        return false;
    }
    return textureAspectMatchesFormat(texture.format, view.aspect);
}

bool textureViewDimensionMatches(const rhi::TextureDesc& texture, const rhi::TextureViewDesc& view)
{
    if (texture.extent.depth > 1u) {
        return view.dimension == rhi::TextureViewDimension::Texture3D && view.baseArrayLayer == 0u
            && view.arrayLayerCount == 1u;
    }
    if (view.dimension == rhi::TextureViewDimension::Texture3D) {
        return false;
    }
    if (view.dimension == rhi::TextureViewDimension::TextureCube) {
        return texture.cubeCompatible && view.arrayLayerCount == 6u && texture.arrayLayerCount >= 6u
            && view.baseArrayLayer <= texture.arrayLayerCount - 6u;
    }
    if (view.arrayLayerCount > 1u) {
        return view.dimension == rhi::TextureViewDimension::Texture2DArray;
    }
    return true;
}

rhi::ValidationResult validateAndResolveWebGpuTextureViewDesc(
    const rhi::TextureDesc& texture, rhi::TextureViewDesc& view)
{
    if (view.format == rhi::TextureFormat::Unknown) {
        view.format = texture.format;
    }
    if (view.usage == rhi::TextureUsage::None) {
        view.usage = texture.usage;
    }
    if (view.format == rhi::TextureFormat::Unknown || view.format != texture.format
        || view.usage == rhi::TextureUsage::None
        || (static_cast<std::uint32_t>(view.usage) & ~static_cast<std::uint32_t>(texture.usage))
            != 0u
        || !textureViewRangeWithinTexture(texture, view)
        || !textureViewDimensionMatches(texture, view)) {
        return {false, "WebGPU RHI texture view is incompatible with its parent texture"};
    }
    return {true, {}};
}

// WebGPU guarantees only four bind groups. A generated lowering artifact may
// require more, but that path is valid only when the browser-owned device has
// published a queried maxBindGroups limit that covers the whole pipeline.
rhi::ValidationResult validateWebGpuBindGroupBudget(const rhi::PipelineLayoutDesc& layout,
    const WebGpuBindingSpaceLowering* lowering,
    std::uint32_t pushConstantGroup)
{
    const bool usesPushConstants = !layout.pushConstantRanges.empty();
    if (usesPushConstants && pushConstantGroup == UINT32_MAX) {
        return {false, "WebGPU RHI push-constant lowering is missing a physical group"};
    }
    std::uint32_t requiredGroupCount = usesPushConstants ? pushConstantGroup + 1u : 0u;
    for (const auto& slot : layout.descriptorSets) {
        // `slot.set` is the backend-neutral logical descriptor-set identity;
        // it is deliberately not a WebGPU bind-group number.  WebGpuBinding-
        // Space/5 lowers that identity to a physical group, so use the
        // generated artifact whenever it is available.  Checking the logical
        // number here rejects valid layouts such as logical set 3 -> group 0.
        std::uint32_t group = slot.set;
        if (lowering) {
            const auto* descriptorLayout =
                rhi::findPipelineDescriptorLayout(layout, slot.layoutName);
            if (descriptorLayout) {
                for (const auto& descriptor : descriptorLayout->bindings) {
                    for (const auto& entry : lowering->entries) {
                        if (entry.active && entry.set == slot.set
                            && entry.binding == descriptor.binding) {
                            group = entry.group;
                            break;
                        }
                    }
                }
            }
        }
        if (usesPushConstants && group == pushConstantGroup) {
            return {
                false, "WebGPU RHI descriptor group collides with push-constant lowering group"};
        }
        requiredGroupCount = std::max(requiredGroupCount, group + 1u);
    }
    if (lowering) {
        requiredGroupCount = std::max(requiredGroupCount, lowering->physicalGroupCount);
    }
    if (!webGpuNativeBindGroupCountSupported(requiredGroupCount)) {
        return {false,
            "WebGPU RHI pipeline requires more physical bind groups than the queried device limit"};
    }
    return {true, {}};
}

rhi::ResourceMemoryClass memoryClassFor(rhi::MemoryUsage usage)
{
    switch (usage) {
    case rhi::MemoryUsage::GpuOnly:
        return rhi::ResourceMemoryClass::DeviceLocal;
    case rhi::MemoryUsage::CpuToGpu:
        return rhi::ResourceMemoryClass::Upload;
    case rhi::MemoryUsage::GpuToCpu:
        return rhi::ResourceMemoryClass::Readback;
    case rhi::MemoryUsage::Transient:
        return rhi::ResourceMemoryClass::Transient;
    }
    return rhi::ResourceMemoryClass::DeviceLocal;
}

rhi::ResourceAllocation allocationFrom(
    const WebGpuRhiAllocation& allocation, rhi::MemoryUsage memory)
{
    return {
        memoryClassFor(memory),
        allocation.sizeBytes,
        allocation.nativeAllocationExposedToUpperLayer,
        true,
    };
}

rhi::ResourceBarrierResult barrierResultFrom(
    rhi::GpuResourceId id, const rhi::ResourceBarrierDesc& desc)
{
    rhi::ResourceBarrierResult result;
    result.resource = id;
    result.before = desc.before;
    result.after = desc.after;
    result.sourceStages = desc.sourceStages;
    result.destinationStages = desc.destinationStages;
    result.sourceAccess = desc.sourceAccess;
    result.destinationAccess = desc.destinationAccess;
    result.stageAccessAware = desc.sourceStages != rhi::PipelineStage::None
        || desc.destinationStages != rhi::PipelineStage::None;
    return result;
}

class WebGpuRhiBuffer final : public rhi::GpuBuffer {
public:
    WebGpuRhiBuffer(rhi::GpuResourceId id,
        rhi::BufferDesc desc,
        rhi::ResourceAllocation allocation,
        std::shared_ptr<void> releaseToken,
        std::shared_ptr<WebGpuNativeBufferPayload> nativePayload,
        std::weak_ptr<WebGpuRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), allocation_(allocation),
          releaseToken_(std::move(releaseToken)), nativePayload_(std::move(nativePayload)),
          registry_(std::move(registry)), metadataState_(desc_.initialState)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (nativePayload_ && nativePayload_->buffer && nativePayload_->ownerDevice)
            bindOriginalRecordedStateSource(*nativePayload_);
#endif
        if (nativePayload_ && releaseToken_)
            nativePayload_->allocationReleaseToken = releaseToken_;
    }

    ~WebGpuRhiBuffer() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterBufferIfOwner(id_, nativePayload_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::BufferDesc& desc() const override { return desc_; }

    const rhi::ResourceAllocation& allocation() const override { return allocation_; }

    rhi::ResourceState state() const override
    {
        return nativePayload_ ? nativePayload_->recordedState().state() : metadataState_.state();
    }

    std::shared_ptr<WebGpuNativeBufferPayload> nativePayload() const { return nativePayload_; }

    const char* recordState(rhi::ResourceState after, core::MutationJournal* journal) noexcept
    {
        return nativePayload_ ? nativePayload_->recordState(after, journal)
                              : metadataState_.writeBuffer(after, journal);
    }

    const char* observeOriginal(WebGpuTransferResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        if (!registry)
            return "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
        return registry->observeTransferResources(&id_, 1u, &observation);
    }

    const char* validateOriginal(
        const WebGpuTransferResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        if (!registry)
            return "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
        return registry->validateTransferResources(&observation, 1u);
    }

private:
    rhi::GpuResourceId id_;
    rhi::BufferDesc desc_;
    rhi::ResourceAllocation allocation_;
    std::shared_ptr<void> releaseToken_;
    std::shared_ptr<WebGpuNativeBufferPayload> nativePayload_;
    std::weak_ptr<WebGpuRhiResourceRegistry> registry_;
    // Explicit CPU-only metadata profile when native payload is absent. It
    // cannot be captured as original native state by the retained graph.
    rhi::common::RecordedResourceStateControl metadataState_;
};

class WebGpuRhiTexture final : public rhi::GpuTexture {
public:
    WebGpuRhiTexture(rhi::GpuResourceId id,
        rhi::TextureDesc desc,
        rhi::ResourceAllocation allocation,
        std::shared_ptr<void> releaseToken,
        std::shared_ptr<WebGpuNativeTexturePayload> nativePayload,
        std::weak_ptr<WebGpuRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), allocation_(allocation),
          releaseToken_(std::move(releaseToken)), nativePayload_(std::move(nativePayload)),
          registry_(std::move(registry))
    {
        if (!nativePayload_)
            metadataState_.emplace(desc_.initialState);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (nativePayload_ && nativePayload_->texture && nativePayload_->ownerDevice)
            bindOriginalRecordedStateSource(*nativePayload_);
#endif
        if (nativePayload_ && releaseToken_)
            nativePayload_->allocationReleaseToken = releaseToken_;
    }

    ~WebGpuRhiTexture() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterTextureIfOwner(id_, nativePayload_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::TextureDesc& desc() const override { return desc_; }

    const rhi::ResourceAllocation& allocation() const override { return allocation_; }

    rhi::ResourceState state() const override
    {
        return nativePayload_ ? nativePayload_->recordedState().state() : metadataState_->state();
    }

    rhi::ResourceState subresourceState(const rhi::TextureSubresourceRange& range) const override
    {
        if (nativePayload_)
            return nativePayload_->normalizedRecordedState().uniformState(range).value_or(
                static_cast<rhi::ResourceState>(UINT32_MAX));
        return metadataState_->state();
    }

    std::shared_ptr<WebGpuNativeTexturePayload> nativePayload() const { return nativePayload_; }

    const char* recordState(rhi::ResourceState after, core::MutationJournal* journal) noexcept
    {
        return nativePayload_ ? nativePayload_->recordState(after, journal)
                              : metadataState_->write(after, journal);
    }

    const char* recordSubresourceState(const rhi::TextureSubresourceRange& range,
        rhi::ResourceState before,
        rhi::ResourceState after,
        core::MutationJournal* journal) noexcept
    {
        if (nativePayload_)
            return nativePayload_->recordSubresourceState(range, before, after, journal);
        if (range != rhi::defaultTextureSubresourceRange(desc_))
            return "RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN";
        if (metadataState_->state() != before)
            return "WebGPU RHI texture transition state mismatch";
        return metadataState_->write(after, journal);
    }

    const char* observeOriginal(WebGpuTransferResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        if (!registry)
            return "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
        return registry->observeTransferResources(&id_, 1u, &observation);
    }

    const char* validateOriginal(
        const WebGpuTransferResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        if (!registry)
            return "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
        return registry->validateTransferResources(&observation, 1u);
    }

private:
    rhi::GpuResourceId id_;
    rhi::TextureDesc desc_;
    rhi::ResourceAllocation allocation_;
    std::shared_ptr<void> releaseToken_;
    std::shared_ptr<WebGpuNativeTexturePayload> nativePayload_;
    std::weak_ptr<WebGpuRhiResourceRegistry> registry_;
    // Explicit CPU-only metadata profile when native payload is absent. It
    // cannot be captured as original native state by the retained graph.
    std::optional<rhi::common::RecordedResourceStateControl> metadataState_;
};

class WebGpuRhiBufferView final : public rhi::GpuBufferView {
public:
    WebGpuRhiBufferView(
        rhi::GpuResourceId id, rhi::GpuResourceId bufferId, rhi::BufferViewDesc desc)
        : id_(id), bufferId_(bufferId), desc_(std::move(desc))
    {}

    rhi::GpuResourceId id() const override { return id_; }

    rhi::GpuResourceId bufferId() const override { return bufferId_; }

    const rhi::BufferViewDesc& desc() const override { return desc_; }

private:
    rhi::GpuResourceId id_;
    rhi::GpuResourceId bufferId_;
    rhi::BufferViewDesc desc_;
};

class WebGpuRhiTextureView final : public rhi::GpuTextureView {
public:
    WebGpuRhiTextureView(rhi::GpuResourceId id,
        rhi::GpuResourceId textureId,
        rhi::TextureViewDesc desc,
        std::shared_ptr<WebGpuNativeTextureViewPayload> nativePayload,
        std::weak_ptr<WebGpuRhiResourceRegistry> registry)
        : id_(id), textureId_(textureId), desc_(std::move(desc)),
          nativePayload_(std::move(nativePayload)), registry_(std::move(registry))
    {}

    ~WebGpuRhiTextureView() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterResource(id_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    rhi::GpuResourceId textureId() const override { return textureId_; }

    const rhi::TextureViewDesc& desc() const override { return desc_; }

    std::shared_ptr<WebGpuNativeTextureViewPayload> nativePayload() const { return nativePayload_; }

    const char* observeOriginal(WebGpuOrdinaryResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        if (!registry)
            return "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
        return registry->observeOrdinaryResources(&id_, 1u, &observation);
    }

    const char* validateOriginal(
        const WebGpuOrdinaryResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        if (!registry)
            return "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
        return registry->validateOrdinaryResources(&observation, 1u);
    }

private:
    rhi::GpuResourceId id_;
    rhi::GpuResourceId textureId_;
    rhi::TextureViewDesc desc_;
    std::shared_ptr<WebGpuNativeTextureViewPayload> nativePayload_;
    std::weak_ptr<WebGpuRhiResourceRegistry> registry_;
};

class WebGpuRhiSampler final : public rhi::GpuSampler {
public:
    WebGpuRhiSampler(rhi::GpuResourceId id,
        rhi::SamplerDesc desc,
        std::shared_ptr<WebGpuNativeSamplerPayload> nativePayload,
        std::weak_ptr<WebGpuRhiResourceRegistry> registry)
        : id_(id), desc_(std::move(desc)), nativePayload_(std::move(nativePayload)),
          registry_(std::move(registry))
    {}

    ~WebGpuRhiSampler() override
    {
        if (auto registry = registry_.lock()) {
            registry->unregisterResource(id_);
        }
    }

    rhi::GpuResourceId id() const override { return id_; }

    const rhi::SamplerDesc& desc() const override { return desc_; }

    std::shared_ptr<WebGpuNativeSamplerPayload> nativePayload() const { return nativePayload_; }

    const char* observeOriginal(WebGpuOrdinaryResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        return registry ? registry->observeOrdinaryResources(&id_, 1u, &observation)
                        : "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
    }

    const char* validateOriginal(
        const WebGpuOrdinaryResourceObservation& observation) const noexcept
    {
        const auto registry = registry_.lock();
        return registry ? registry->validateOrdinaryResources(&observation, 1u)
                        : "RHI-WEBGPU-ORIGINAL-CLOSURE-REGISTRY-EXPIRED";
    }

private:
    rhi::GpuResourceId id_;
    rhi::SamplerDesc desc_;
    std::shared_ptr<WebGpuNativeSamplerPayload> nativePayload_;
    std::weak_ptr<WebGpuRhiResourceRegistry> registry_;
};

class WebGpuRhiPipelineLayout final : public rhi::PipelineLayout {
public:
    explicit WebGpuRhiPipelineLayout(rhi::PipelineLayoutDesc desc) : desc_(std::move(desc))
    {
        for (const auto& range : desc_.pushConstantRanges) {
            pushConstantBytes_ += range.sizeBytes;
        }
    }

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
};

class WebGpuRhiRenderPipeline final : public rhi::RenderPipeline {
public:
    WebGpuRhiRenderPipeline(const rhi::PipelineLayout* layout,
        rhi::ShaderReflectionDesc reflection,
        rhi::PipelineValidationReport report,
        rhi::PrimitiveTopology primitiveTopology,
        std::shared_ptr<WebGpuNativeRenderPipelinePayload> nativePayload,
        core::SharedPtr<const shader::GeneratedShaderArtifactSet> generatedArtifactSet = {})
        : layout_(layout), reflection_(std::move(reflection)), report_(std::move(report)),
          primitiveTopology_(primitiveTopology),
          generatedArtifactSet_(std::move(generatedArtifactSet)),
          nativePayload_(std::move(nativePayload))
    {}

    WebGpuRhiRenderPipeline(arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout,
        rhi::ShaderReflectionDesc reflection,
        rhi::PipelineValidationReport report,
        rhi::PrimitiveTopology primitiveTopology,
        std::shared_ptr<WebGpuNativeRenderPipelinePayload> nativePayload,
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        rhi::BindlessGraphicsPipelineEntries entries,
        const rhi::RenderPipelineDesc& state)
        : ownedLayout_(std::move(ownedLayout)), layout_(ownedLayout_.get()),
          reflection_(std::move(reflection)), report_(std::move(report)),
          primitiveTopology_(primitiveTopology), nativePayload_(std::move(nativePayload)),
          authorization_(std::move(authorization)), graphicsEntries_(entries),
          graphicsState_(rhi::RenderPipelineStateCopy::copy(state))
    {
        // The composed shader package is temporary; pass/raster observations
        // must never retain its borrowed pointer. Layout ownership is local.
        graphicsState_.layout = layout_;
        graphicsState_.shaderPackage = nullptr;
    }

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

    rhi::PrimitiveTopology primitiveTopology() const override { return primitiveTopology_; }

    const core::pmr::Vector<rhi::VertexBufferLayoutDesc>* bindlessVertexBuffers(
        std::uint32_t version) const noexcept override
    {
        return version == 1u && authorization_ ? &graphicsState_.vertexBuffers : nullptr;
    }

    const rhi::RenderPipelineDesc* originalBindlessGraphicsState() const noexcept
    {
        return authorization_ && nativePayload_ && nativePayload_->issuedContract()
            ? &graphicsState_
            : nullptr;
    }

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
        return graphicsEntries_.bufferRequirement(stage, set, binding, true, output, version);
    }

    const char* bindlessOrdinaryBufferMemberMemory(rhi::ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        rhi::ShaderBufferMemberMemory& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if (!authorization_
            || authorization_->package().artifact().execution != rhi::BindlessExecution::Emulated
            || !nativePayload_ || !nativePayload_->issuedContract())
            return "RHI-WEBGPU-MEMBER-MEMORY-ORIGINAL-PIPELINE";
        if ((bindlessShaderStages() & stage) != stage || stage == rhi::ShaderStage::None)
            return "RHI-BINDLESS-MEMBER-MEMORY-STAGE";
        return graphicsEntries_.bufferMemberMemory(stage, set, binding, false, output, version);
    }

    rhi::ShaderStage bindlessShaderStages() const noexcept override
    {
        rhi::ShaderStage stages = rhi::ShaderStage::None;
        if (authorization_ && nativePayload_ && nativePayload_->issuedContract())
            for (const auto& fact : nativePayload_->issuedContract()->stages())
                stages = stages | fact.stage;
        return stages;
    }

    std::shared_ptr<WebGpuNativeRenderPipelinePayload> nativePayload() const
    {
        return nativePayload_;
    }

private:
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout_;
    const rhi::PipelineLayout* layout_ = nullptr;
    rhi::ShaderReflectionDesc reflection_;
    rhi::PipelineValidationReport report_;
    rhi::PrimitiveTopology primitiveTopology_ = rhi::PrimitiveTopology::TriangleList;
    core::SharedPtr<const shader::GeneratedShaderArtifactSet> generatedArtifactSet_;
    std::shared_ptr<WebGpuNativeRenderPipelinePayload> nativePayload_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
    const rhi::BindlessGraphicsPipelineEntries graphicsEntries_;
    rhi::RenderPipelineDesc graphicsState_;
};

class WebGpuRhiComputePipeline final : public rhi::ComputePipeline {
public:
    WebGpuRhiComputePipeline(const rhi::PipelineLayout* layout,
        rhi::ShaderReflectionDesc reflection,
        rhi::PipelineValidationReport report,
        std::shared_ptr<WebGpuNativeComputePipelinePayload> nativePayload,
        core::SharedPtr<const shader::GeneratedShaderArtifactSet> generatedArtifactSet = {})
        : layout_(layout), reflection_(std::move(reflection)), report_(std::move(report)),
          generatedArtifactSet_(std::move(generatedArtifactSet)),
          nativePayload_(std::move(nativePayload))
    {}

    WebGpuRhiComputePipeline(arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout,
        rhi::ShaderReflectionDesc reflection,
        rhi::PipelineValidationReport report,
        std::shared_ptr<WebGpuNativeComputePipelinePayload> nativePayload,
        std::shared_ptr<const rhi::BindlessAuthorization> authorization)
        : ownedLayout_(std::move(ownedLayout)), layout_(ownedLayout_.get()),
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

    std::shared_ptr<WebGpuNativeComputePipelinePayload> nativePayload() const
    {
        return nativePayload_;
    }

private:
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout_;
    const rhi::PipelineLayout* layout_ = nullptr;
    rhi::ShaderReflectionDesc reflection_;
    rhi::PipelineValidationReport report_;
    core::SharedPtr<const shader::GeneratedShaderArtifactSet> generatedArtifactSet_;
    std::shared_ptr<WebGpuNativeComputePipelinePayload> nativePayload_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
};

} // namespace

const rhi::RenderPipelineDesc* webGpuRhiBindlessGraphicsPipelineState(
    const rhi::RenderPipeline& pipeline) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiRenderPipeline*>(&pipeline);
    return original ? original->originalBindlessGraphicsState() : nullptr;
}

std::uint64_t estimateWebGpuRhiTextureBytes(const rhi::TextureDesc& desc)
{
    // Account for every mip, array/cube layer, and MSAA sample. A first-mip
    // estimate undercounts the allocator ledger and lets a valid WebGPU
    // resource exceed its declared memory budget.
    return rhi::estimateTexturePlacementRequirements(desc).sizeBytes;
}

arp::core::UniquePtr<rhi::GpuBuffer> createWebGpuRhiBuffer(rhi::GpuResourceId id,
    const rhi::BufferDesc& desc,
    const WebGpuRhiAllocation& allocation,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    const WebGpuNativeBufferInitializer& initializer)
{
    if (const auto* id = rhi::HostVisibleDeviceLocalBufferContract::validate(
            desc, rhi::Backend::WebGPU, false)) {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, id);
        return {};
    }
    if (initializer.initialize && !nativePayloadPolicy.enabled)
        return {};
    auto nativePayload =
        nativePayloadPolicy.enabled ? createWebGpuNativeBufferPayload(desc, initializer) : nullptr;
    if ((nativePayloadPolicy.enabled || nativePayloadPolicy.required) && !nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<WebGpuRhiBuffer>(id,
        desc,
        allocationFrom(allocation, desc.memory),
        allocation.releaseToken,
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuTexture> createWebGpuRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const WebGpuRhiAllocation& allocation,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry)
{
    auto nativePayload =
        nativePayloadPolicy.enabled ? createWebGpuNativeTexturePayload(desc) : nullptr;
    if ((nativePayloadPolicy.enabled || nativePayloadPolicy.required) && !nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<WebGpuRhiTexture>(id,
        desc,
        allocationFrom(allocation, desc.memory),
        allocation.releaseToken,
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuTexture> createWebGpuRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const WebGpuRhiAllocation& allocation,
    std::shared_ptr<WebGpuNativeTexturePayload> nativePayload,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry)
{
    if (!nativePayload || !nativePayload->hasNormalizedRecordedState()
        || !nativePayload->normalizedRecordedState().geometry().matches(desc)
        || nativePayload->creationState() != desc.initialState)
        return {};
    return arp::core::makeUnique<WebGpuRhiTexture>(id,
        desc,
        allocationFrom(allocation, desc.memory),
        allocation.releaseToken,
        std::move(nativePayload),
        std::move(registry));
}

arp::core::UniquePtr<rhi::GpuBufferView> createWebGpuRhiBufferView(
    rhi::GpuResourceId id, rhi::GpuResourceId bufferId, const rhi::BufferViewDesc& desc)
{
    return arp::core::makeUnique<WebGpuRhiBufferView>(id, bufferId, desc);
}

arp::core::UniquePtr<rhi::GpuTextureView> createWebGpuRhiTextureView(rhi::GpuResourceId id,
    rhi::GpuResourceId textureId,
    const rhi::GpuTexture& texture,
    const rhi::TextureViewDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry)
{
    rhi::TextureViewDesc resolvedDesc = desc;
    if (!validateAndResolveWebGpuTextureViewDesc(texture.desc(), resolvedDesc).success) {
        return nullptr;
    }
    auto texturePayload = webGpuNativePayloadFor(texture);
    auto nativePayload = nativePayloadPolicy.enabled && texturePayload
        ? getOrCreateWebGpuNativeTextureViewPayload(*texturePayload, resolvedDesc)
        : nullptr;
    if (nativePayloadPolicy.required && !nativePayload) {
        return nullptr;
    }
    if (nativePayload) {
        nativePayload->parentTexture = std::move(texturePayload);
    }
    return arp::core::makeUnique<WebGpuRhiTextureView>(
        id, textureId, resolvedDesc, std::move(nativePayload), std::move(registry));
}

arp::core::UniquePtr<rhi::GpuSampler> createWebGpuRhiSampler(rhi::GpuResourceId id,
    const rhi::SamplerDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry)
{
    auto nativePayload =
        nativePayloadPolicy.enabled ? createWebGpuNativeSamplerPayload(desc) : nullptr;
    if (nativePayloadPolicy.required && !nativePayload) {
        return nullptr;
    }
    return arp::core::makeUnique<WebGpuRhiSampler>(
        id, desc, std::move(nativePayload), std::move(registry));
}

std::shared_ptr<WebGpuNativeBufferPayload> webGpuNativePayloadFor(const rhi::GpuBuffer& buffer)
{
    const auto* webGpuBuffer = dynamic_cast<const WebGpuRhiBuffer*>(&buffer);
    return webGpuBuffer ? webGpuBuffer->nativePayload() : nullptr;
}

std::shared_ptr<WebGpuNativeTexturePayload> webGpuNativePayloadFor(const rhi::GpuTexture& texture)
{
    const auto* webGpuTexture = dynamic_cast<const WebGpuRhiTexture*>(&texture);
    return webGpuTexture ? webGpuTexture->nativePayload() : nullptr;
}

std::shared_ptr<WebGpuNativeTextureViewPayload> webGpuNativePayloadFor(
    const rhi::GpuTextureView& textureView)
{
    const auto* webGpuTextureView = dynamic_cast<const WebGpuRhiTextureView*>(&textureView);
    return webGpuTextureView ? webGpuTextureView->nativePayload() : nullptr;
}

std::shared_ptr<WebGpuNativeSamplerPayload> webGpuNativePayloadFor(const rhi::GpuSampler& sampler)
{
    const auto* webGpuSampler = dynamic_cast<const WebGpuRhiSampler*>(&sampler);
    return webGpuSampler ? webGpuSampler->nativePayload() : nullptr;
}

std::shared_ptr<WebGpuNativeRenderPipelinePayload> webGpuNativePayloadFor(
    const rhi::RenderPipeline& pipeline)
{
    const auto* webGpuPipeline = dynamic_cast<const WebGpuRhiRenderPipeline*>(&pipeline);
    return webGpuPipeline ? webGpuPipeline->nativePayload() : nullptr;
}

std::shared_ptr<WebGpuNativeComputePipelinePayload> webGpuNativePayloadFor(
    const rhi::ComputePipeline& pipeline)
{
    const auto* webGpuPipeline = dynamic_cast<const WebGpuRhiComputePipeline*>(&pipeline);
    return webGpuPipeline ? webGpuPipeline->nativePayload() : nullptr;
}

const char* observeOriginalWebGpuResource(
    const rhi::GpuBuffer& object, WebGpuTransferResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiBuffer*>(&object);
    return original ? original->observeOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* validateOriginalWebGpuResource(
    const rhi::GpuBuffer& object, const WebGpuTransferResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiBuffer*>(&object);
    return original ? original->validateOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* observeOriginalWebGpuResource(
    const rhi::GpuTexture& object, WebGpuTransferResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiTexture*>(&object);
    return original ? original->observeOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* validateOriginalWebGpuResource(
    const rhi::GpuTexture& object, const WebGpuTransferResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiTexture*>(&object);
    return original ? original->validateOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* observeOriginalWebGpuResource(
    const rhi::GpuTextureView& object, WebGpuOrdinaryResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiTextureView*>(&object);
    return original ? original->observeOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* validateOriginalWebGpuResource(const rhi::GpuTextureView& object,
    const WebGpuOrdinaryResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiTextureView*>(&object);
    return original ? original->validateOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* observeOriginalWebGpuResource(
    const rhi::GpuSampler& object, WebGpuOrdinaryResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiSampler*>(&object);
    return original ? original->observeOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

const char* validateOriginalWebGpuResource(
    const rhi::GpuSampler& object, const WebGpuOrdinaryResourceObservation& observation) noexcept
{
    const auto* original = dynamic_cast<const WebGpuRhiSampler*>(&object);
    return original ? original->validateOriginal(observation)
                    : "RHI-WEBGPU-ORIGINAL-CLOSURE-WRAPPER";
}

WebGpuRhiRegisteredResource WebGpuRhiResourceRegistry::bufferRegistration(
    const rhi::GpuBuffer& buffer)
{
    WebGpuRhiRegisteredResource registered;
    registered.kind = rhi::DescriptorResourceKind::Buffer;
    registered.bufferUsage = buffer.desc().usage;
    registered.buffer = webGpuNativePayloadFor(buffer);
    registered.bufferDesc = buffer.desc();
    registered.gpuWriteSeal = buffer.gpuWriteSealToken();
    registered.allocationBytes = buffer.allocation().estimatedSizeBytes;
    if (registered.buffer) {
        registered.allocationOwner = registered.buffer->allocationReleaseToken;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        registered.registeredNativeObject =
            reinterpret_cast<std::uintptr_t>(registered.buffer->buffer);
        registered.registeredNativeDevice =
            reinterpret_cast<std::uintptr_t>(registered.buffer->ownerDevice);
#endif
    }
    return registered;
}

void WebGpuRhiResourceRegistry::registerBuffer(const rhi::GpuBuffer& buffer)
{
    std::scoped_lock lock(mutex_);
    resources_[buffer.id().value] = bufferRegistration(buffer);
}

bool WebGpuRhiResourceRegistry::tryRegisterFreshBuffer(const rhi::GpuBuffer& buffer)
{
    std::scoped_lock lock(mutex_);
    if (resources_.find(buffer.id().value) != resources_.end())
        return false;
    resources_.emplace(buffer.id().value, bufferRegistration(buffer));
    return true;
}

void WebGpuRhiResourceRegistry::unregisterBufferIfOwner(
    rhi::GpuResourceId id, const std::shared_ptr<WebGpuNativeBufferPayload>& payload) noexcept
{
    std::scoped_lock lock(mutex_);
    const auto found = resources_.find(id.value);
    if (found != resources_.end() && found->second.kind == rhi::DescriptorResourceKind::Buffer
        && core::SharedOwnerProjection::sameOwner(found->second.buffer, payload))
        resources_.erase(found);
}

WebGpuRhiDescriptorResourceFacts WebGpuRhiRegisteredResource::descriptorFacts() const noexcept
{
    const auto exclusive = [](const auto& payload) noexcept {
        return payload && payload->contentAccessPolicy().ordinaryAccessDiagnostic();
    };
    return {kind,
        bufferUsage,
        textureUsage,
        textureViewDimension,
        textureDesc.format,
        textureViewAspect,
        parentTextureLive,
        gpuWritesSealed,
        exclusive(texture) || exclusive(parentTextureOwner)
            || (textureView && exclusive(textureView->parentTexture))};
}

void WebGpuRhiResourceRegistry::unregisterTextureIfOwner(
    rhi::GpuResourceId id, const std::shared_ptr<WebGpuNativeTexturePayload>& payload) noexcept
{
    std::scoped_lock lock(mutex_);
    const auto found = resources_.find(id.value);
    if (found != resources_.end() && found->second.textureObject
        && core::SharedOwnerProjection::sameOwner(found->second.texture, payload))
        resources_.erase(found);
}

bool WebGpuRhiResourceRegistry::tryRegisterFreshTexture(const rhi::GpuTexture& texture)
{
    std::scoped_lock lock(mutex_);
    if (resources_.find(texture.id().value) != resources_.end())
        return false;
    resources_.emplace(texture.id().value, textureRegistration(texture));
    return true;
}

WebGpuRhiRegisteredResource WebGpuRhiResourceRegistry::textureRegistration(
    const rhi::GpuTexture& texture)
{
    WebGpuRhiRegisteredResource registered;
    registered.kind = rhi::DescriptorResourceKind::TextureView;
    registered.textureUsage = texture.desc().usage;
    registered.texture = webGpuNativePayloadFor(texture);
    registered.textureDesc = texture.desc();
    registered.textureObject = true;
    registered.allocationBytes = texture.allocation().estimatedSizeBytes;
    if (registered.texture) {
        registered.allocationOwner = registered.texture->allocationReleaseToken;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        registered.registeredNativeObject =
            reinterpret_cast<std::uintptr_t>(registered.texture->texture);
        registered.registeredNativeDevice =
            reinterpret_cast<std::uintptr_t>(registered.texture->ownerDevice);
#endif
    }
    return registered;
}

void WebGpuRhiResourceRegistry::registerTexture(const rhi::GpuTexture& texture)
{
    // Finish fallible descriptor/label copies before publishing a map entry.
    // A failed factory must not retain a half-registered native allocation.
    auto registered = textureRegistration(texture);
    std::scoped_lock lock(mutex_);
    resources_.insert_or_assign(texture.id().value, std::move(registered));
}

void WebGpuRhiResourceRegistry::registerTextureView(const rhi::GpuTextureView& textureView)
{
    std::scoped_lock lock(mutex_);
    resources_[textureView.id().value] = {
        rhi::DescriptorResourceKind::TextureView,
        rhi::ResourceUsage::None,
        textureView.desc().usage,
        nullptr,
        nullptr,
        webGpuNativePayloadFor(textureView),
        nullptr,
    };
    resources_[textureView.id().value].textureViewDimension = textureView.desc().dimension;
    resources_[textureView.id().value].textureDesc.format = textureView.desc().format;
    resources_[textureView.id().value].textureViewAspect = textureView.desc().aspect;
    resources_[textureView.id().value].parentTexture = textureView.textureId();
    auto& registered = resources_[textureView.id().value];
    registered.textureViewDesc = textureView.desc();
    const auto parent = resources_.find(textureView.textureId().value);
    if (parent != resources_.end() && parent->second.textureObject) {
        registered.parentTextureOwner = parent->second.texture;
        registered.allocationOwner = parent->second.allocationOwner;
        registered.allocationBytes = parent->second.allocationBytes;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (registered.textureView) {
        registered.registeredNativeObject =
            reinterpret_cast<std::uintptr_t>(registered.textureView->view);
        registered.registeredNativeDevice = registered.parentTextureOwner
            ? reinterpret_cast<std::uintptr_t>(registered.parentTextureOwner->ownerDevice)
            : 0u;
    }
#endif
}

void WebGpuRhiResourceRegistry::registerSampler(const rhi::GpuSampler& sampler)
{
    std::scoped_lock lock(mutex_);
    resources_[sampler.id().value] = {
        rhi::DescriptorResourceKind::Sampler,
        rhi::ResourceUsage::None,
        rhi::TextureUsage::None,
        nullptr,
        nullptr,
        nullptr,
        webGpuNativePayloadFor(sampler),
    };
    resources_[sampler.id().value].samplerDesc = sampler.desc();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    auto& registered = resources_[sampler.id().value];
    if (registered.sampler) {
        registered.registeredNativeObject =
            reinterpret_cast<std::uintptr_t>(registered.sampler->sampler);
        registered.registeredNativeDevice =
            reinterpret_cast<std::uintptr_t>(registered.sampler->ownerDevice);
    }
#endif
}

void WebGpuRhiResourceRegistry::unregisterResource(rhi::GpuResourceId id)
{
    std::scoped_lock lock(mutex_);
    resources_.erase(id.value);
}

const char* WebGpuRhiResourceRegistry::observeTransferResources(const rhi::GpuResourceId* ids,
    std::size_t count,
    WebGpuTransferResourceObservation* observations) const noexcept
{
    if (count && (!ids || !observations))
        return "RHI-WEBGPU-TRANSFER-MANIFEST-MISSING";
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return "RHI-WEBGPU-TRANSFER-REGISTRY-BUSY";
    for (std::size_t index = 0u; index < count; ++index) {
        for (std::size_t previous = 0u; previous < index; ++previous)
            if (ids[previous] == ids[index])
                return "RHI-WEBGPU-TRANSFER-MANIFEST-DUPLICATE";
        const auto found = resources_.find(ids[index].value);
        if (!ids[index].isValid() || found == resources_.end())
            return "RHI-WEBGPU-TRANSFER-RESOURCE-MISSING";
        if (const auto* diagnostic = observations[index].capture(ids[index], found->second))
            return diagnostic;
        const auto owners = observations[index].owners();
        for (std::size_t previous = 0u; previous < index; ++previous) {
            const auto previousOwners = observations[previous].owners();
            if (owners[0] == previousOwners[0]
                || owners[observations[index].ownerCount() - 1u]
                    == previousOwners[observations[previous].ownerCount() - 1u])
                return "RHI-WEBGPU-TRANSFER-ALIASED-RESOURCE-UNSUPPORTED";
        }
    }
    return nullptr;
}

const char* WebGpuRhiResourceRegistry::validateTransferResources(
    const WebGpuTransferResourceObservation* observations, std::size_t count) const noexcept
{
    if (count && !observations)
        return "RHI-WEBGPU-TRANSFER-MANIFEST-MISSING";
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return "RHI-WEBGPU-TRANSFER-REGISTRY-BUSY";
    for (std::size_t index = 0u; index < count; ++index) {
        const auto& original = observations[index];
        if (!original.matches())
            return "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
        const auto found = resources_.find(original.id.value);
        if (found == resources_.end())
            return "RHI-WEBGPU-TRANSFER-RESOURCE-MISSING";
        WebGpuTransferResourceObservation current;
        if (const auto* id = current.capture(original.id, found->second))
            return id;
        if (!original.sameOwnersAndLayout(current))
            return "RHI-WEBGPU-TRANSFER-ORIGINAL-OWNER-OR-LAYOUT";
    }
    return nullptr;
}

const char* WebGpuRhiResourceRegistry::observeOrdinaryResources(const rhi::GpuResourceId* ids,
    std::size_t count,
    WebGpuOrdinaryResourceObservation* observations) const noexcept
{
    if (count && (!ids || !observations))
        return "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-MISSING";
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTRY-BUSY";
    for (std::size_t index = 0u; index < count; ++index) {
        for (std::size_t previous = 0u; previous < index; ++previous)
            if (ids[previous] == ids[index])
                return "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-DUPLICATE";
        const auto found = resources_.find(ids[index].value);
        if (!ids[index].isValid() || found == resources_.end())
            return "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING";
        const auto parent = resources_.find(found->second.parentTexture.value);
        if (const auto* id = observations[index].capture(
                ids[index], found->second, parent == resources_.end() ? nullptr : &parent->second))
            return id;
    }
    return nullptr;
}

std::optional<WebGpuRhiRegisteredResource> WebGpuRhiResourceRegistry::find(
    rhi::GpuResourceId id) const
{
    std::scoped_lock lock(mutex_);
    const auto found = resources_.find(id.value);
    if (found == resources_.end())
        return std::nullopt;
    auto resource = found->second;
    if (const auto seal = resource.gpuWriteSeal.lock()) {
        resource.gpuWritesSealed = seal->writableDescriptorSealed();
        resource.gpuTransferWritesSealed = seal->transferWritesSealed();
    }
    const auto parent = resources_.find(resource.parentTexture.value);
    resource.parentTextureLive = parent != resources_.end() && parent->second.textureObject;
    return resource;
}

const char* WebGpuRhiResourceRegistry::validateOrdinaryResources(
    const WebGpuOrdinaryResourceObservation* observations, std::size_t count) const noexcept
{
    if (count && !observations)
        return "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-MISSING";
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTRY-BUSY";
    for (std::size_t index = 0u; index < count; ++index) {
        const auto& original = observations[index];
        if (!original.matches())
            return "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED";
        const auto found = resources_.find(original.id().value);
        if (found == resources_.end())
            return "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING";
        const auto parent = resources_.find(found->second.parentTexture.value);
        WebGpuOrdinaryResourceObservation current;
        if (const auto* id = current.capture(original.id(),
                found->second,
                parent == resources_.end() ? nullptr : &parent->second))
            return id;
        if (!original.sameOwners(current))
            return "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-OWNER";
    }
    return nullptr;
}

std::optional<WebGpuRhiDescriptorResourceFacts> WebGpuRhiResourceRegistry::findDescriptorFacts(
    rhi::GpuResourceId id) const
{
    std::scoped_lock lock(mutex_);
    const auto found = resources_.find(id.value);
    if (found == resources_.end())
        return std::nullopt;
    auto facts = found->second.descriptorFacts();
    if (const auto seal = found->second.gpuWriteSeal.lock())
        facts.gpuWritesSealed = seal->writableDescriptorSealed();
    const auto parent = resources_.find(found->second.parentTexture.value);
    facts.parentTextureLive = parent != resources_.end() && parent->second.textureObject;
    return facts;
}

rhi::ResourceBarrierResult transitionWebGpuRhiBuffer(
    rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc, core::MutationJournal* journal)
{
    auto result = barrierResultFrom(buffer.id(), desc);
    auto* webGpuBuffer = dynamic_cast<WebGpuRhiBuffer*>(&buffer);
    if (!webGpuBuffer || desc.resource != buffer.id() || buffer.state() != desc.before) {
        result.diagnostics = "WebGPU RHI buffer transition state mismatch";
        return result;
    }
    if (const auto* id = webGpuBuffer->recordState(desc.after, journal)) {
        result.diagnostics = id;
        return result;
    }
    result.success = true;
    return result;
}

rhi::ResourceBarrierResult transitionWebGpuRhiTexture(
    rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc, core::MutationJournal* journal)
{
    auto result = barrierResultFrom(texture.id(), desc);
    result.subresourceAware = true;
    if (const auto payload = webGpuNativePayloadFor(texture))
        if (const auto* id = payload->contentAccessPolicy().ordinaryAccessDiagnostic()) {
            result.diagnostics = id;
            return result;
        }
    auto* webGpuTexture = dynamic_cast<WebGpuRhiTexture*>(&texture);
    if (!webGpuTexture || desc.resource != texture.id()) {
        result.diagnostics = "WebGPU RHI texture transition state mismatch";
        return result;
    }
    if (!rhi::semantics::isTextureResourceState(texture.desc().format, desc.before)
        || !rhi::semantics::isTextureResourceState(texture.desc().format, desc.after)) {
        result.diagnostics = "RHI-RESOURCE-STATE-INVALID";
        return result;
    }
    if (const auto* id = webGpuTexture->recordSubresourceState(
            desc.textureSubresources, desc.before, desc.after, journal)) {
        result.diagnostics = id;
        return result;
    }
    result.success = true;
    return result;
}

arp::core::UniquePtr<rhi::PipelineLayout> createWebGpuRhiPipelineLayout(
    const rhi::PipelineLayoutDesc& desc)
{
    return rhi::validatePipelineLayout(desc).success
        ? arp::core::makeUnique<WebGpuRhiPipelineLayout>(
              arp::rhi::PipelineLayoutStateCopy::copy(desc))
        : nullptr;
}

static rhi::PipelineValidationReport validateWebGpuRhiRenderPipelineImpl(
    const rhi::RenderPipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& originalInput)
{
    rhi::PipelineValidationReport report;
    report.backend = rhi::Backend::WebGPU;
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    if (!desc.layout || !desc.shaderPackage) {
        report.diagnostics = "WebGPU RHI render pipeline requires layout and shader package";
        return report;
    }
    const auto shaderValidation = originalInput ? ([&]() -> rhi::ValidationResult {
        if (const auto* id = rhi::OriginalGraphicsPipelineInput::packageDiagnostic(
                originalInput, *desc.shaderPackage))
            return {false, id};
        return {true, {}};
    })()
                                                : rhi::validateShaderPackage(*desc.shaderPackage);
    if (!shaderValidation.success) {
        report.diagnostics = shaderValidation.diagnostics;
        return report;
    }
    const auto reflectionValidation = rhi::validatePipelineLayoutAgainstShaderReflection(
        desc.layout->desc(), desc.shaderPackage->reflection);
    if (!reflectionValidation.success) {
        report.diagnostics = reflectionValidation.diagnostics;
        return report;
    }
    const auto lowering = originalInput
        ? loadOriginalWebGpuGraphicsBindingSpaceLowering(originalInput)
        : loadWebGpuBindingSpaceLowering(*desc.shaderPackage);
    if (!lowering.success) {
        report.diagnostics = lowering.diagnostics;
        return report;
    }
    const auto loweringValidation = validateWebGpuBindingSpaceLowering(desc.layout->desc(),
        desc.shaderPackage->reflection,
        lowering.lowering,
        lowering.lowering.pushConstantGroup);
    if (!loweringValidation.success) {
        report.diagnostics = loweringValidation.diagnostics;
        return report;
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        report.diagnostics =
            "WebGPU RHI render pipeline PatchList topology has no executable lowering";
        return report;
    }
    const auto bindGroupBudget = validateWebGpuBindGroupBudget(
        desc.layout->desc(), &lowering.lowering, lowering.lowering.pushConstantGroup);
    if (!bindGroupBudget.success) {
        report.diagnostics = bindGroupBudget.diagnostics;
        return report;
    }
    const auto stateValidation = rhi::validateRenderPipelineState(desc);
    if (!stateValidation.success) {
        report.diagnostics = stateValidation.diagnostics;
        return report;
    }
    rhi::PipelineValidationReportBuilder::validatedInterface(report, desc);
    rhi::PipelineValidationReportBuilder::validatedGraphicsState(report, stateValidation);
    return report;
}

rhi::PipelineValidationReport validateWebGpuRhiRenderPipeline(const rhi::RenderPipelineDesc& desc)
{
    return validateWebGpuRhiRenderPipelineImpl(desc, {});
}

rhi::PipelineValidationReport validateOriginalWebGpuRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input)
{
    if (const auto* id = rhi::OriginalGraphicsPipelineInput::validateOriginal(input)) {
        rhi::PipelineValidationReport result;
        result.diagnostics = id;
        return result;
    }
    if (desc.shaderPackage != &input->package()) {
        rhi::PipelineValidationReport result;
        result.diagnostics = "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PACKAGE";
        return result;
    }
    return validateWebGpuRhiRenderPipelineImpl(desc, input);
}

static rhi::PipelineValidationReport validateWebGpuRhiComputePipelineImpl(
    const rhi::ComputePipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& originalInput)
{
    rhi::PipelineValidationReport report;
    report.backend = rhi::Backend::WebGPU;
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    if (!desc.layout || !desc.shaderPackage
        || desc.computeShader.stage != rhi::ShaderStage::Compute) {
        report.diagnostics =
            "WebGPU RHI compute pipeline requires layout, shader package, and compute entrypoint";
        return report;
    }
    const auto shaderValidation = originalInput ? ([&]() -> rhi::ValidationResult {
        if (const auto* id = rhi::OriginalSingleEntryPipelineInput::packageDiagnostic(
                originalInput, *desc.shaderPackage))
            return {false, id};
        return {true, {}};
    })()
                                                : rhi::validateShaderPackage(*desc.shaderPackage);
    if (!shaderValidation.success) {
        report.diagnostics = shaderValidation.diagnostics;
        return report;
    }
    const auto reflectionValidation = rhi::validatePipelineLayoutAgainstShaderReflection(
        desc.layout->desc(), desc.shaderPackage->reflection);
    if (!reflectionValidation.success) {
        report.diagnostics = reflectionValidation.diagnostics;
        return report;
    }
    const auto lowering = originalInput ? loadOriginalWebGpuBindingSpaceLowering(originalInput)
                                        : loadWebGpuBindingSpaceLowering(*desc.shaderPackage);
    if (!lowering.success) {
        report.diagnostics = lowering.diagnostics;
        return report;
    }
    const auto loweringValidation = validateWebGpuBindingSpaceLowering(desc.layout->desc(),
        desc.shaderPackage->reflection,
        lowering.lowering,
        lowering.lowering.pushConstantGroup);
    if (!loweringValidation.success) {
        report.diagnostics = loweringValidation.diagnostics;
        return report;
    }
    const auto bindGroupBudget = validateWebGpuBindGroupBudget(
        desc.layout->desc(), &lowering.lowering, lowering.lowering.pushConstantGroup);
    if (!bindGroupBudget.success) {
        report.diagnostics = bindGroupBudget.diagnostics;
        return report;
    }
    rhi::PipelineValidationReportBuilder::validatedInterface(report, desc);
    report.pipelineCacheKey = rhi::stableShaderPackageHash(*desc.shaderPackage);
    report.pipelineCacheKeyStable = report.pipelineCacheKey != 0;
    return report;
}

rhi::PipelineValidationReport validateWebGpuRhiComputePipeline(const rhi::ComputePipelineDesc& desc)
{
    return validateWebGpuRhiComputePipelineImpl(desc, {});
}

rhi::PipelineValidationReport validateOriginalWebGpuRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input)
{
    if (const auto* id = rhi::OriginalSingleEntryPipelineInput::validateOriginal(input)) {
        rhi::PipelineValidationReport result;
        result.diagnostics = id;
        return result;
    }
    if (desc.shaderPackage != &input->package()) {
        rhi::PipelineValidationReport result;
        result.diagnostics = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-PACKAGE";
        return result;
    }
    return validateWebGpuRhiComputePipelineImpl(desc, input);
}

template <class Actor, class Desc>
static rhi::PipelineValidationReport validateWebGpuSelectedTableExpansionWrapper(
    const Actor& actor, const Desc& desc)
{
    rhi::PipelineValidationReport report;
    report.backend = rhi::Backend::WebGPU;
    if (const auto* id = actor.validate()) {
        report.diagnostics = id;
        return report;
    }
    // Ready proves the exact original declaration/definition and issued native
    // projection. An ordinary BindingSpace/5 lowering is never substituted.
    if constexpr (std::is_same_v<Desc, rhi::RenderPipelineDesc>) {
        const auto state = rhi::validateRenderPipelineState(desc);
        if (!state.success) {
            report.diagnostics = state.diagnostics;
            return report;
        }
        rhi::PipelineValidationReportBuilder::validatedGraphicsState(report, state);
        report.colorFormats = state.colorFormats;
        report.depthStencilFormat = state.depthStencilFormat;
        report.selectedFragmentWritesRenderTargetArrayIndex =
            desc.fragmentShader.writesRenderTargetArrayIndex;
    } else {
        report.pipelineCacheKey = rhi::stableShaderPackageHash(*desc.shaderPackage);
        report.pipelineCacheKeyStable = report.pipelineCacheKey != 0;
        report.selectedComputeWorkgroupSizeX = desc.computeShader.computeWorkgroupSizeX;
        report.selectedComputeWorkgroupSizeY = desc.computeShader.computeWorkgroupSizeY;
        report.selectedComputeWorkgroupSizeZ = desc.computeShader.computeWorkgroupSizeZ;
    }
    rhi::PipelineValidationReportBuilder::validatedInterface(report, desc);
    report.descriptorBindingCount =
        static_cast<std::uint32_t>(desc.shaderPackage->reflection.descriptorBindings.size());
    return report;
}

core::UniquePtr<rhi::ComputePipeline> WebGpuAcceptedOriginalComputePipelineFactory::create(
    const rhi::ComputePipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalComputePipelineCreation>& creation,
    const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
    const char*& diagnostic)
{
    const auto* actor =
        WebGpuAcceptedOriginalPipelineValidation::validate<WebGpuOriginalArtifactComputeCreation>(
            desc,
            creation,
            wrapperQuota,
            "RHI-ACCEPTED-COMPUTE-FACTORY-ACTOR",
            "RHI-ACCEPTED-COMPUTE-FACTORY-DEFINITION",
            "RHI-ACCEPTED-COMPUTE-FACTORY-QUOTA-DOMAIN",
            diagnostic);
    if (!actor)
        return {};
    try {
        core::CpuMemoryQuotaScope wrapperScope(wrapperQuota);
        auto report = actor->isTableExpansion()
            ? validateWebGpuSelectedTableExpansionWrapper(*actor, desc)
            : validateWebGpuRhiComputePipelineImpl(desc, actor->originalInput());
        if (!report.success) {
            diagnostic = "RHI-ACCEPTED-COMPUTE-FACTORY-INTERFACE";
            return {};
        }
        if ((diagnostic = actor->validate()))
            return {};
        auto wrapper = core::makeUnique<WebGpuRhiComputePipeline>(desc.layout,
            rhi::ShaderReflectionStateCopy::copy(desc.shaderPackage->reflection),
            std::move(report),
            actor->pipeline_);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        wrapper->originalDescriptorBufferLimits_ = WebGpuOriginalDescriptorBufferLimitsQuery::query(
            static_cast<WGPUDevice>(actor->device_.get()));
        if ((diagnostic = actor->validate()))
            return {};
        if (actor->isTableExpansion()) {
            const auto selected = rhi::OriginalDepthTableExpansionCreationContract::wrapperLowering(
                rhi::Backend::WebGPU,
                true,
                !rhi::OriginalDescriptorBufferBindingContract::validateLimits(
                    wrapper->originalDescriptorBufferLimits_));
            if (!selected.acceptanceEligible) {
                diagnostic = selected.diagnostic;
                return {};
            }
        }
#endif
        wrapper->acceptedComputeWrapperCreation_ = creation;
        return wrapper;
    } catch (const std::bad_alloc&) {
        if (actor->isTableExpansion()) {
            diagnostic = "RHI-ORIGINAL-TABLE-WRAPPER-ALLOCATION";
            return {};
        }
        throw;
    }
}

core::UniquePtr<rhi::RenderPipeline> WebGpuAcceptedOriginalGraphicsPipelineFactory::create(
    const rhi::RenderPipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>& creation,
    const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
    const char*& diagnostic)
{
    const auto* actor =
        WebGpuAcceptedOriginalPipelineValidation::validate<WebGpuOriginalArtifactGraphicsCreation>(
            desc,
            creation,
            wrapperQuota,
            "RHI-ACCEPTED-GRAPHICS-FACTORY-ACTOR",
            "RHI-ACCEPTED-GRAPHICS-FACTORY-DEFINITION",
            "RHI-ACCEPTED-GRAPHICS-FACTORY-QUOTA-DOMAIN",
            diagnostic);
    if (!actor)
        return {};
    try {
        core::CpuMemoryQuotaScope wrapperScope(wrapperQuota);
        // Shared admission proved every copied package/definition field against
        // the accepted actor. Use the same private memory validator; the public
        // consumption entry retains its exact original package-pointer requirement.
        auto report = actor->isTableExpansion()
            ? validateWebGpuSelectedTableExpansionWrapper(*actor, desc)
            : validateWebGpuRhiRenderPipelineImpl(desc, actor->originalInput());
        if (!report.success) {
            diagnostic = "RHI-ACCEPTED-GRAPHICS-FACTORY-INTERFACE";
            return {};
        }
        if ((diagnostic = actor->validate()))
            return {};
        auto wrapper = core::makeUnique<WebGpuRhiRenderPipeline>(desc.layout,
            rhi::ShaderReflectionStateCopy::copy(desc.shaderPackage->reflection),
            std::move(report),
            desc.topology,
            actor->pipeline_);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        wrapper->originalDescriptorBufferLimits_ = WebGpuOriginalDescriptorBufferLimitsQuery::query(
            static_cast<WGPUDevice>(actor->device_.get()));
        if ((diagnostic = actor->validate()))
            return {};
        if (actor->isTableExpansion()) {
            const auto selected = rhi::OriginalDepthTableExpansionCreationContract::wrapperLowering(
                rhi::Backend::WebGPU,
                true,
                !rhi::OriginalDescriptorBufferBindingContract::validateLimits(
                    wrapper->originalDescriptorBufferLimits_));
            if (!selected.acceptanceEligible) {
                diagnostic = selected.diagnostic;
                return {};
            }
        }
#endif
        wrapper->acceptedGraphicsWrapperCreation_ = creation;
        return wrapper;
    } catch (const std::bad_alloc&) {
        if (actor->isTableExpansion()) {
            diagnostic = "RHI-ORIGINAL-TABLE-WRAPPER-ALLOCATION";
            return {};
        }
        throw;
    }
}

arp::core::UniquePtr<rhi::RenderPipeline> createWebGpuRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc, WebGpuRhiNativePayloadPolicy nativePayloadPolicy)
{
    auto report = validateWebGpuRhiRenderPipeline(desc);
    if (!report.success) {
        std::fprintf(stderr,
            "[wgpu-validate] FAIL label='%s' diag='%s'\n",
            desc.label.c_str(),
            report.diagnostics.c_str());
    }
    auto nativePayload = report.success && nativePayloadPolicy.enabled
        ? createWebGpuNativeRenderPipelinePayload(desc)
        : nullptr;
    if (nativePayloadPolicy.required && !nativePayload) {
        return nullptr;
    }
    return report.success ? arp::core::makeUnique<WebGpuRhiRenderPipeline>(desc.layout,
                                desc.shaderPackage->reflection,
                                std::move(report),
                                desc.topology,
                                std::move(nativePayload),
                                desc.shaderPackage->generatedArtifactSet)
                          : nullptr;
}

arp::core::UniquePtr<rhi::RenderPipeline> createWebGpuRhiAuthorizedBindlessRenderPipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    core::pmr::String& diagnostic,
    const rhi::BindlessGraphicsPushRequest* push)
{
    diagnostic.clear();
    if (!authorization || !authorization->current(authorization)
        || (!authorization->plan().executable
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 2u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 3u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 5u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 11u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 12u))
        || authorization->plan().execution != rhi::BindlessExecution::Emulated
        || authorization->plan().shaderPackageIdentity
            != authorization->package().artifact().shaderPackageIdentity) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return {};
    }
    const auto& package = authorization->package();
    const auto& artifact = package.artifact();
    if (artifact.execution != rhi::BindlessExecution::Emulated) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-PACKAGE-MISMATCH";
        return {};
    }
    const auto authored = rhi::validateBindlessAuthoredRenderState(state, true);
    if (!authored.success) {
        diagnostic = authored.diagnostics;
        return {};
    }
    rhi::ShaderPackageDesc shaderInterface;
    shaderInterface.label = artifact.physicalLayout.name;
    shaderInterface.generatedReflectionHydrated = true;
    rhi::PipelineLayoutDesc layoutDesc;
    rhi::BindlessGraphicsPipelineEntries entries;
    if (push) {
        if (const auto* id = push->diagnostic()) {
            diagnostic = id;
            return {};
        }
        const auto composed =
            rhi::BindlessGraphicsPipelineEntries::compose(authorization->originalPackageOwner(),
                vertexEntry,
                fragmentEntry,
                shaderInterface,
                layoutDesc,
                entries);
        if (!composed.success) {
            diagnostic = composed.diagnostics;
            return {};
        }
        const auto bytes = shaderInterface.reflection.pushConstantByteCount;
        const auto limits = webGpuNativeDeviceLimits();
        if (!bytes || bytes % 4u || bytes > push->maxBytes || !limits.maxUniformBufferBindingSize
            || bytes > limits.maxUniformBufferBindingSize || !limits.uniformBufferOffsetAlignment) {
            diagnostic = "RHI-ORIGINAL-PUSH-RECORDING-SIZE-LIMIT";
            return {};
        }
    } else if (!rhi::appendBindlessShaderEntry(
                   package, vertexEntry, rhi::ShaderStage::Vertex, shaderInterface, diagnostic)
        || !rhi::appendBindlessShaderEntry(
            package, fragmentEntry, rhi::ShaderStage::Fragment, shaderInterface, diagnostic)) {
        return {};
    }
    if (!push)
        layoutDesc = rhi::bindlessPipelineLayout(artifact);
    auto ownedLayout = createWebGpuRhiPipelineLayout(layoutDesc);
    if (!ownedLayout) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-LAYOUT-INVALID";
        return {};
    }
    const auto reflection =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, shaderInterface.reflection);
    if (!reflection.success) {
        diagnostic = reflection.diagnostics;
        return {};
    }
    auto pipelineState = state;
    pipelineState.label = shaderInterface.label;
    pipelineState.layout = ownedLayout.get();
    pipelineState.shaderPackage = &shaderInterface;
    pipelineState.vertexShader = shaderInterface.entrypoints[0];
    pipelineState.fragmentShader = shaderInterface.entrypoints[1];
    if (const auto* id =
            rhi::BindlessGraphicsPipelineEntries::capture(package, pipelineState, entries)) {
        diagnostic = id;
        return {};
    }
    auto nativePayload = createWebGpuNativeBindlessRenderPipelinePayload(
        package, vertexEntry, fragmentEntry, pipelineState, diagnostic, authorization.get(), push);
    if (!nativePayload)
        return {};

    rhi::PipelineValidationReport report;
    report.success = true;
    report.backend = rhi::Backend::WebGPU;
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    report.shaderPackageValidated = true;
    report.reflectionMatched = true;
    report.entrypointCount = 2u;
    report.descriptorLayoutCount = layoutDesc.descriptorLayouts.size();
    report.descriptorSetCount = layoutDesc.descriptorSets.size();
    report.colorTargetCount = rhi::effectiveColorTargetCount(state);
    for (std::uint32_t i = 0u; i < report.colorTargetCount; ++i)
        report.colorFormats.push_back(rhi::effectiveColorTargetAt(state, i).format);
    report.depthStencilFormat = rhi::effectiveDepthStencilFormat(state);
    report.sampleCount = state.multisample.sampleCount;
    report.pipelineCacheKey = artifact.shaderPackageIdentity;
    report.pipelineCacheKeyStable = true;
    auto pipeline = arp::core::makeUnique<WebGpuRhiRenderPipeline>(std::move(ownedLayout),
        std::move(shaderInterface.reflection),
        std::move(report),
        state.topology,
        std::move(nativePayload),
        std::move(authorization),
        entries,
        state);
    if (!pipeline)
        diagnostic = "RHI-WEBGPU-BINDLESS-RENDER-PIPELINE-CREATION-FAILED";
    return pipeline;
}

arp::core::UniquePtr<rhi::ComputePipeline> createWebGpuRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc, WebGpuRhiNativePayloadPolicy nativePayloadPolicy)
{
    auto report = validateWebGpuRhiComputePipeline(desc);
    auto nativePayload = report.success && nativePayloadPolicy.enabled
        ? createWebGpuNativeComputePipelinePayload(desc)
        : nullptr;
    if (nativePayloadPolicy.required && !nativePayload) {
        return nullptr;
    }
    return report.success ? arp::core::makeUnique<WebGpuRhiComputePipeline>(desc.layout,
                                desc.shaderPackage->reflection,
                                std::move(report),
                                std::move(nativePayload),
                                desc.shaderPackage->generatedArtifactSet)
                          : nullptr;
}

arp::core::UniquePtr<rhi::ComputePipeline> createWebGpuRhiAuthorizedBindlessComputePipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::string_view entry,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!authorization || !authorization->current(authorization)
        || (!authorization->plan().executable
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 1u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 5u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 8u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageResourcePlan(
                authorization->plan()))
        || authorization->plan().execution != rhi::BindlessExecution::Emulated
        || authorization->plan().shaderPackageIdentity
            != authorization->package().artifact().shaderPackageIdentity) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return {};
    }
    const auto& artifact = authorization->package().artifact();
    if (artifact.execution != rhi::BindlessExecution::Emulated) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-PACKAGE-MISMATCH";
        return {};
    }

    rhi::ShaderPackageDesc shaderInterface;
    shaderInterface.label = artifact.physicalLayout.name;
    shaderInterface.generatedReflectionHydrated = true;
    if (!rhi::appendBindlessShaderEntry(authorization->package(),
            entry,
            rhi::ShaderStage::Compute,
            shaderInterface,
            diagnostic)) {
        return {};
    }
    auto layoutDesc = rhi::bindlessPipelineLayout(artifact);
    auto ownedLayout = createWebGpuRhiPipelineLayout(layoutDesc);
    if (!ownedLayout) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-LAYOUT-INVALID";
        return {};
    }
    const auto reflection =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, shaderInterface.reflection);
    if (!reflection.success) {
        diagnostic = reflection.diagnostics;
        return {};
    }
    auto nativePayload = createWebGpuNativeBindlessComputePipelinePayload(
        authorization->package(), entry, diagnostic);
    if (!nativePayload)
        return {};

    rhi::PipelineValidationReport report;
    report.success = true;
    report.backend = rhi::Backend::WebGPU;
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    report.shaderPackageValidated = true;
    report.reflectionMatched = true;
    report.entrypointCount = 1u;
    report.descriptorLayoutCount = static_cast<std::uint32_t>(layoutDesc.descriptorLayouts.size());
    report.descriptorSetCount = static_cast<std::uint32_t>(layoutDesc.descriptorSets.size());
    report.pipelineCacheKey = artifact.shaderPackageIdentity;
    report.pipelineCacheKeyStable = true;
    auto pipeline = arp::core::makeUnique<WebGpuRhiComputePipeline>(std::move(ownedLayout),
        std::move(shaderInterface.reflection),
        std::move(report),
        std::move(nativePayload),
        std::move(authorization));
    if (!pipeline)
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-CREATION-FAILED";
    return pipeline;
}

} // namespace arp::rhi_webgpu
