#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_conformance_clear.h"
#include "rhi/backends/vulkan/vulkan_conformance_sampled_inputs.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/conformance/sampled_command_contract.h"
#include "rhi/conformance/sampled_pipeline.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/frontend/resource_access_scope.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <cstring>

namespace arp::rhi_vulkan {
using namespace rhi;

const VulkanConformanceSampledInputs::Buffer* VulkanConformanceSampledInputs::buffer(
    const VulkanRhiNativeResourcePayload& payload,
    ConformanceSampledInput role,
    const void* expected,
    std::size_t size) const noexcept
{
    for (const auto& record : buffers_)
        if (record.native.get() == &payload && record.role == role
            && record.object->hostWritesSealed() && record.object->gpuWritesSealed()
            && record.object->gpuTransferWritesSealed() && record.content.size() == size
            && record.content.initialized(0, size)
            && !std::memcmp(record.content.data(), expected, size))
            return &record;
    return nullptr;
}

const char* VulkanConformanceSampledInputs::diagnostic() const noexcept
{
    if (!artifact_ || (!nativePipelines_[0] && !nativePipelines_[1])
        || (storageMrt_ ? !nativeCompute_ : !nativeSampler_))
        return "RHI-CONFORMANCE-SAMPLED-ORIGINAL-INPUT-INCOMPLETE";
    if (const auto* id = shader::GraphicsShaderModuleArtifact::validateOriginal(artifact_))
        return id;
    return storageMrt_ ? shader::SingleEntryShaderArtifact::validateOriginal(computeArtifact_)
                       : nullptr;
}

const char* VulkanConformanceClearExecution::initialize(
    core::UniquePtr<ConformanceSampledWorkload> workload, std::uint64_t nativeBudget)
{
    if (run_ || inputs_ || !workload || !nativeBudget
        || workload->frames().size() > request_.observation.maxFrames)
        return "RHI-CONFORMANCE-SAMPLED-ORIGINAL-INPUT-CONTRACT";
    inputs_ = core::makeShared<VulkanConformanceSampledInputs>(
        VulkanConformanceSampledInputs::Key{}, nativeBudget);
    inputs_->buffers_.reserve(workload->frames().size() + 2);
    return initializeFrames({}, std::move(workload));
}

std::shared_ptr<GpuBuffer> VulkanConformanceClearExecution::prepareInput(
    ConformanceSampledInput role, std::size_t frameIndex, const char*& id)
{
    id = diagnostic(*device_);
    if (id)
        return {};
    if (!inputs_ || (!run_->sampled_ && !run_->storage_) || prepared_ || run_->next_
        || (role != ConformanceSampledInput::Uniforms
            && role != ConformanceSampledInput::StorageValues && frameIndex)
        || frameIndex >= run_->frameCount()
        || (bool(run_->storage_) != (role == ConformanceSampledInput::StorageValues))) {
        id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-ORDER");
        return {};
    }
    for (const auto& old : inputs_->buffers_)
        if (old.role == role && old.frame == frameIndex) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-DUPLICATE");
            return {};
        }
    const void* bytes = nullptr;
    std::size_t size = 0;
    ResourceUsage usage = ResourceUsage::None;
    switch (role) {
    case ConformanceSampledInput::StorageValues:
        bytes = run_->storage_->frames()[frameIndex].values.data();
        size = sizeof(run_->storage_->frames()[frameIndex].values);
        usage = ResourceUsage::StorageBuffer;
        break;
    case ConformanceSampledInput::Vertices:
        bytes = run_->sampled_->vertices().data();
        size = sizeof(run_->sampled_->vertices());
        usage = ResourceUsage::VertexBuffer;
        break;
    case ConformanceSampledInput::Indices:
        bytes = run_->sampled_->indices().data();
        size = sizeof(run_->sampled_->indices());
        usage = ResourceUsage::IndexBuffer;
        break;
    case ConformanceSampledInput::Uniforms:
        bytes = run_->sampled_->frames()[frameIndex].uniforms.data();
        size = sizeof(run_->sampled_->frames()[frameIndex].uniforms);
        usage = ResourceUsage::UniformBuffer;
        break;
    default:
        id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-ROLE");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        const BufferDesc desc{"OriginalConformanceSampledExecution/1 immutable input",
            size,
            usage,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead};
        const auto allocation = allocator_.queryBufferAllocationBytes(desc, id);
        if (id || !allocation || allocation > inputs_->budget_ - inputs_->nativeBytes_) {
            id = fail(id ? id : "RHI-CONFORMANCE-SAMPLED-NATIVE-INPUT-BUDGET");
            return {};
        }
        auto object = core::shareUnique(device_->createBuffer(desc));
        const auto native = object ? vulkanRhiNativePayload(*object) : nullptr;
        if (!native || !native->backendMappedData()
            || native->backendVulkanDeviceHandle() != allocator_.runtime()->logicalDevice()
            || !object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > allocation) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-NATIVE-ALLOCATION");
            return {};
        }
        const auto written = device_->writeBuffer(*object, bytes, size);
        if (!written.success) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-WRITE-FLUSH");
            return {};
        }
        core::InitializedByteBuffer content(size);
        if (!content.write(0, bytes, size)) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-INITIALIZATION");
            return {};
        }
        object->sealWrites();
        inputs_->buffers_.push_back({role, frameIndex, object, native, std::move(content)});
        inputs_->nativeBytes_ += allocation;
        return object;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

std::shared_ptr<GpuSampler> VulkanConformanceClearExecution::prepareSampler(const char*& id)
{
    id = diagnostic(*device_);
    if (id)
        return {};
    if (!inputs_ || inputs_->sampler_ || prepared_ || run_->next_) {
        id = fail("RHI-CONFORMANCE-SAMPLED-SAMPLER-ORDER");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        inputs_->sampler_ = core::shareUnique(device_->createSampler({"Conformance.NearestClamp",
            SamplerFilter::Nearest,
            SamplerFilter::Nearest,
            SamplerAddressMode::ClampToEdge,
            SamplerAddressMode::ClampToEdge,
            SamplerAddressMode::ClampToEdge,
            CompareOp::Never,
            SamplerMipFilter::None,
            0}));
        inputs_->nativeSampler_ =
            inputs_->sampler_ ? vulkanRhiNativePayload(*inputs_->sampler_) : nullptr;
        if (!inputs_->nativeSampler_ || !inputs_->nativeSampler_->backendVulkanSamplerHandle()) {
            id = fail("RHI-CONFORMANCE-SAMPLED-SAMPLER-NATIVE");
            return {};
        }
        return inputs_->sampler_;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

std::shared_ptr<RenderPipeline> VulkanConformanceClearExecution::preparePipeline(
    const RenderPipelineDesc& state,
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact,
    const char*& id)
{
    id = diagnostic(*device_);
    if (!id)
        id = shader::GraphicsShaderModuleArtifact::validateOriginal(artifact);
    if (id)
        return {};
    const auto pipelineIndex = state.colorFormat == TextureFormat::Rgba8Unorm ? 0u : 1u;
    if (!inputs_ || inputs_->pipelines_[pipelineIndex] || prepared_ || run_->next_ || !state.layout
        || (state.colorFormat != TextureFormat::Rgba8Unorm
            && state.colorFormat != TextureFormat::Bgra8Unorm)
        || (inputs_->artifact_ && inputs_->artifact_ != artifact) || !state.shaderPackage
        || artifact->module().reflection.sourceSha256
            != (run_->storage_ ? run_->storage_->shaderSourceSha256()
                               : run_->sampled_->shaderSourceSha256())) {
        id = fail("RHI-CONFORMANCE-SAMPLED-ORIGINAL-SHADER");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        ShaderPackageDesc package;
        const auto populated = populateGraphicsModuleShaderInterface(*artifact, package);
        if (!populated.success || package.entrypoints.size() != 2
            || state.shaderPackage->entrypoints.size() != 2
            || !PipelineDefinitionIdentity::matches(
                package.reflection, state.shaderPackage->reflection)
            || state.vertexShader.entryPoint != package.entrypoints[0].entryPoint
            || state.fragmentShader.entryPoint != package.entrypoints[1].entryPoint
            || !PipelineDefinitionIdentity::matches(state,
                (run_->storage_ ? ConformanceStorageMrtPipeline::pipelineDefinition(
                                      *state.layout, *state.shaderPackage, state.colorFormat)
                                : ConformanceSampledPipeline::definition(
                                      *state.layout, *state.shaderPackage, state.colorFormat)))) {
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-DEFINITION");
            return {};
        }
        for (std::size_t i = 0; i < package.entrypoints.size(); ++i) {
            package.entrypoints[i].modulePath = state.shaderPackage->entrypoints[i].modulePath;
            if (!PipelineDefinitionIdentity::matches(
                    package.entrypoints[i], state.shaderPackage->entrypoints[i])) {
                id = fail("RHI-CONFORMANCE-SAMPLED-ORIGINAL-ENTRY-ABI");
                return {};
            }
        }
        // The original lifetime check above guards this device-owned contract.
        // Use the same actual capability lowering gate as ordinary creation.
        const auto lowered = rhi::semantics::lowerRenderPipelineState(*capabilityContract_, state);
        const auto validation = device_->validateRenderPipeline(state);
        if (!lowered.success || !validation.success) {
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-LOWERING");
            return {};
        }
        const auto& vertex = artifact->vertex()->bytes(shader::ShaderArtifactRole::Spirv);
        const auto& fragment = artifact->fragment()->bytes(shader::ShaderArtifactRole::Spirv);
        if (vertex.empty() || fragment.empty() || vertex.size() % sizeof(std::uint32_t)
            || fragment.size() % sizeof(std::uint32_t)) {
            id = fail("RHI-CONFORMANCE-SAMPLED-ORIGINAL-SPIRV");
            return {};
        }
        core::pmr::Vector<std::uint32_t> v(vertex.size() / 4, &core::defaultCpuMemoryResource());
        core::pmr::Vector<std::uint32_t> f(fragment.size() / 4, &core::defaultCpuMemoryResource());
        std::memcpy(v.data(), vertex.data(), vertex.size());
        std::memcpy(f.data(), fragment.data(), fragment.size());
        auto layout =
            device_->createPipelineLayout(PipelineLayoutStateCopy::copy(state.layout->desc()));
        if (!layout) {
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-LAYOUT");
            return {};
        }
        auto nativeState = RenderPipelineStateCopy::copy(state);
        nativeState.layout = layout.get();
        // SPIR-V bytes come directly from the immutable original artifact.
        const auto native = allocator_.createRenderPipelineFromBytes(
            nativeState, v.data(), v.size(), f.data(), f.size());
        if (!native.success || !native.payload
            || !native.payload->backendVulkanRenderPipelineHandle()) {
            id = fail("RHI-CONFORMANCE-SAMPLED-NATIVE-PIPELINE");
            return {};
        }
        auto pipeline =
            createVulkanRhiRenderPipeline(nativeState, native.payload, {}, std::move(layout));
        if (!pipeline) {
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-WRAPPER");
            return {};
        }
        inputs_->artifact_ = artifact;
        owner_->artifact = artifact;
        inputs_->nativePipelines_[pipelineIndex] = native.payload;
        inputs_->pipelines_[pipelineIndex] = core::shareUnique(std::move(pipeline));
        return inputs_->pipelines_[pipelineIndex];
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

bool VulkanConformanceClearFrame::sampledBegin(const RenderPassDesc& desc,
    const core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>& targets,
    const VulkanRhiNativeResourcePayload* depth,
    core::pmr::String& id)
{
    if (!issuer_.isCurrent() || failure_ || pass_ >= sampled_->draws.size() + 2
        || state_ == State::Pass || targets.size() != 1 || depth || desc.hasDepthStencilAttachment
        || desc.colorAttachments.size() != 1 || desc.layeredRendering || !desc.subpasses.empty()
        || !desc.subpassDependencies.empty())
        return reject(id);
    const auto index = pass_ < 2 ? pass_ : std::size_t(sampled_->draws[pass_ - 2].target);
    if (!textures_[index] || textures_[index] != targets[0]
        || textureStates_[index] != ResourceState::ColorAttachment)
        return reject(id);
    const auto color = pass_ < 2
        ? ConformanceClearFrame{sampled_->extent, pass_ ? sampled_->sourceB : sampled_->sourceA}
              .clearColor()
        : Color{0, 0, 0, 1};
    const auto& attachment = desc.colorAttachments[0];
    const auto format = targets[0]->backendVulkanImageFormat() == VK_FORMAT_R8G8B8A8_UNORM
        ? TextureFormat::Rgba8Unorm
        : TextureFormat::Bgra8Unorm;
    if (attachment.target != textureIds_[index] || attachment.format != format
        || attachment.loadOp != LoadOp::Clear || attachment.storeOp != StoreOp::Store
        || attachment.hasResolveTarget || attachment.resolveTarget.isValid()
        || attachment.clearColor.r != color.r || attachment.clearColor.g != color.g
        || attachment.clearColor.b != color.b || attachment.clearColor.a != color.a)
        return reject(id);
    bindings_ = 0;
    drew_ = false;
    state_ = State::Pass;
    return true;
}

bool VulkanConformanceClearFrame::sampledTransition(VulkanRhiNativeCommandPayload& command,
    const VulkanRhiNativeResourcePayload& image,
    const ResourceBarrierDesc& desc,
    core::pmr::String& id)
{
    const auto& range = desc.textureSubresources;
    if (!issuer_.isCurrent() || failure_ || state_ == State::Pass
        || range.aspect != TextureAspect::Color || range.baseMipLevel || range.baseArrayLayer
        || range.mipLevelCount != 1 || range.arrayLayerCount != 1 || state_ == State::Copied
        || desc.before == desc.after || ++imageBarriers_ > 3 + sampled_->draws.size() * 3)
        return reject(id);
    // Authored image edges publish color writes to a fragment consumer. A
    // state enum alone cannot authenticate the actual native visibility scope.
    if (!ConformanceSampledCommandContract::imageScopes(desc))
        return reject(id);
    constexpr auto output = std::size_t(ConformanceSampledResource::Output);
    if (&image == image_.get() && desc.after == ResourceState::Present) {
        if (pass_ != sampled_->draws.size() + 2 || state_ != State::Stored)
            return reject(id);
        return copyToPresent(command, image, desc, id);
    }
    if (pass_ >= sampled_->draws.size() + 2)
        return reject(id);
    const auto target = pass_ < 2 ? pass_ : std::size_t(sampled_->draws[pass_ - 2].target);
    std::size_t index = output + 1;
    for (std::size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i].get() == &image)
            index = i;
    if (index > output) {
        if (target == output || desc.after != ResourceState::ColorAttachment || textures_[target]
            || image.backendVulkanSwapchainFrameSyncAvailable()
            || image.backendVulkanImageFormat() != VK_FORMAT_R8G8B8A8_UNORM
            || desc.before != ResourceState::Undefined)
            return reject(id);
        const auto extent = image.backendVulkanImageExtent();
        if (extent.width != sampled_->extent.width || extent.height != sampled_->extent.height
            || extent.depth != 1)
            return reject(id);
        // The original command already retains this payload. Its shared owner
        // is recovered from the actual resource barrier, never a caller claim.
        for (const auto& retained : command.impl_->retainedResources)
            if (retained.get() == &image)
                textures_[target] = retained;
        if (!textures_[target])
            return reject(id);
        textureIds_[target] = desc.resource;
        index = target;
    }
    if (desc.resource != textureIds_[index] || desc.before != textureStates_[index])
        return reject(id);
    bool allowed = desc.after == ResourceState::ColorAttachment && index == target;
    if (desc.after == ResourceState::ShaderRead && pass_ >= 2 && index != output) {
        const auto& draw = sampled_->draws[pass_ - 2];
        allowed = index == std::size_t(draw.sourceA) || index == std::size_t(draw.sourceB);
    }
    if (!allowed || !command.recordValidatedOriginalImageBarrier(image, desc, id))
        return reject(id);
    textureStates_[index] = desc.after;
    return true;
}

bool VulkanConformanceClearFrame::bufferBarrier(const VulkanRhiNativeResourcePayload& buffer,
    const ResourceBarrierDesc& desc,
    core::pmr::String& id)
{
    if (storageMrt_) {
        if (!issuer_.isCurrent() || failure_ || computeActive_ || state_ == State::Pass
            || desc.before != ResourceState::ShaderRead || desc.after != ResourceState::ShaderRead
            || ++bufferBarriers_ > 2
            || !inputs_->buffer(buffer,
                ConformanceSampledInput::StorageValues,
                storageMrt_->values.data(),
                sizeof(storageMrt_->values)))
            return reject(id);
        for (const auto& record : inputs_->buffers_)
            if (record.native.get() == &buffer && record.object->id() == desc.resource)
                return true;
        return reject(id);
    }

    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ == State::Pass
        || desc.before != ResourceState::ShaderRead || desc.after != ResourceState::ShaderRead
        || ++bufferBarriers_ > sampled_->draws.size() * 3)
        return reject(id);
    for (const auto& record : inputs_->buffers_)
        if (record.native.get() == &buffer && record.object->id() == desc.resource
            && record.object->hostWritesSealed() && record.object->gpuWritesSealed()
            && record.object->gpuTransferWritesSealed())
            return true;
    return reject(id);
}

bool VulkanConformanceClearFrame::pipeline(
    const VulkanRhiNativeResourcePayload& pipeline, core::pmr::String& id)
{
    if ((!sampled_ && !storageMrt_) || !issuer_.isCurrent() || failure_ || state_ != State::Pass
        || (!storageMrt_ && pass_ < 2) || (bindings_ & 1) || inputs_->diagnostic())
        return reject(id);
    const auto target = storageMrt_ ? 4u : std::size_t(sampled_->draws[pass_ - 2].target);
    const auto index =
        textures_[target]->backendVulkanImageFormat() == VK_FORMAT_R8G8B8A8_UNORM ? 0u : 1u;
    if (&pipeline != inputs_->nativePipelines_[index].get())
        return reject(id);
    bindings_ |= 1;
    return true;
}

bool VulkanConformanceClearFrame::viewport(const ViewportState& v, core::pmr::String& id)
{
    if ((!sampled_ && !storageMrt_) || !issuer_.isCurrent() || failure_ || state_ != State::Pass
        || (!storageMrt_ && pass_ < 2) || (bindings_ & 2)
        || !ConformanceSampledCommandContract::viewport(v, expected_.extent))
        return reject(id);
    bindings_ |= 2;
    return true;
}

bool VulkanConformanceClearFrame::scissor(const ScissorState& s, core::pmr::String& id)
{
    if ((!sampled_ && !storageMrt_) || !issuer_.isCurrent() || failure_ || state_ != State::Pass
        || (!storageMrt_ && pass_ < 2) || (bindings_ & 4)
        || !ConformanceSampledCommandContract::scissor(s, expected_.extent))
        return reject(id);
    bindings_ |= 4;
    return true;
}

bool VulkanConformanceClearFrame::vertex(std::uint32_t slot,
    const VulkanRhiNativeResourcePayload& buffer,
    std::uint64_t offset,
    core::pmr::String& id)
{
    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ != State::Pass || pass_ < 2
        || (bindings_ & 8) || slot != 3 || offset)
        return reject(id);
    bool matched = false;
    for (const auto& record : inputs_->buffers_)
        if (record.native.get() == &buffer && record.role == ConformanceSampledInput::Vertices
            && record.content.initialized(0, record.content.size())
            && record.object->hostWritesSealed() && record.object->gpuWritesSealed())
            matched = true;
    if (!matched)
        return reject(id);
    bindings_ |= 8;
    return true;
}

bool VulkanConformanceClearFrame::index(const VulkanRhiNativeResourcePayload& buffer,
    IndexFormat format,
    std::uint64_t offset,
    core::pmr::String& id)
{
    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ != State::Pass || pass_ < 2
        || (bindings_ & 64) || !sampled_->draws[pass_ - 2].indexed || format != IndexFormat::Uint16
        || offset)
        return reject(id);
    bool matched = false;
    for (const auto& record : inputs_->buffers_)
        if (record.native.get() == &buffer && record.role == ConformanceSampledInput::Indices
            && record.content.initialized(0, record.content.size())
            && record.object->hostWritesSealed() && record.object->gpuWritesSealed())
            matched = true;
    if (!matched)
        return reject(id);
    bindings_ |= 64;
    return true;
}

bool VulkanConformanceClearFrame::push(ShaderStage stages,
    std::uint32_t offset,
    std::uint32_t size,
    const void* bytes,
    core::pmr::String& id)
{
    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ != State::Pass || pass_ < 2
        || (bindings_ & 32)
        || !ConformanceSampledCommandContract::push(
            sampled_->draws[pass_ - 2], stages, offset, size, bytes))
        return reject(id);
    bindings_ |= 32;
    return true;
}

bool VulkanConformanceClearFrame::descriptors(const VulkanRhiNativeResourcePayload& set,
    std::uint32_t index,
    std::uint32_t dynamicCount,
    core::pmr::String& id)
{
    if (storageMrt_) {
        if (index || dynamicCount)
            return reject(id);
        return storageDescriptors(set, id);
    }

    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ != State::Pass || pass_ < 2
        || (bindings_ & 16) || index || dynamicCount)
        return reject(id);
    const auto uniform = set.backendVulkanDescriptorElementResource(0);
    if (!uniform
        || !inputs_->buffer(*uniform,
            ConformanceSampledInput::Uniforms,
            sampled_->uniforms.data(),
            sizeof(sampled_->uniforms)))
        return reject(id);
    const auto range = set.backendVulkanDescriptorElementBufferRange(0);
    if (range.first
        || (range.second != VK_WHOLE_SIZE && range.second != sizeof(sampled_->uniforms)))
        return reject(id);
    const auto& draw = sampled_->draws[pass_ - 2];
    for (std::uint32_t t = 0; t < 2; ++t) {
        const auto source = std::size_t(t ? draw.sourceB : draw.sourceA);
        const auto view = set.backendVulkanDescriptorElementResource(1 + t * 2);
        const auto sampler = set.backendVulkanDescriptorElementResource(2 + t * 2);
        const auto* shape = view ? view->backendVulkanOriginalTextureViewDesc() : nullptr;
        if (!textures_[source] || textureStates_[source] != ResourceState::ShaderRead || !view
            || view->backendVulkanTextureViewParentPayload() != textures_[source] || !shape
            || shape->format != TextureFormat::Rgba8Unorm || shape->aspect != TextureAspect::Color
            || shape->baseMipLevel || shape->mipLevelCount != 1 || shape->baseArrayLayer
            || shape->arrayLayerCount != 1 || sampler != inputs_->nativeSampler_)
            return reject(id);
    }
    bindings_ |= 16;
    return true;
}

bool VulkanConformanceClearFrame::draw(bool indexed,
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::int32_t offset,
    std::uint32_t firstInstance,
    core::pmr::String& id)
{
    if (storageMrt_) {
        if (!issuer_.isCurrent() || failure_ || state_ != State::Pass || pass_ != 1 || drew_
            || indexed || count != 3 || instances != 1 || first || offset || firstInstance
            || bindings_ != 23)
            return reject(id);
        drew_ = true;
        return true;
    }

    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ != State::Pass || pass_ < 2 || drew_
        || !ConformanceSampledCommandContract::draw(sampled_->draws[pass_ - 2],
            indexed,
            count,
            instances,
            first,
            offset,
            firstInstance,
            bindings_))
        return reject(id);
    drew_ = true;
    return true;
}
} // namespace arp::rhi_vulkan
