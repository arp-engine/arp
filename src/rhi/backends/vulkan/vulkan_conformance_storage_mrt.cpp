#include "core/math/half_float.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_conformance_clear.h"
#include "rhi/backends/vulkan/vulkan_conformance_sampled_inputs.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <cstring>

namespace arp::rhi_vulkan {
using namespace rhi;

const char* VulkanConformanceClearExecution::initialize(
    core::UniquePtr<ConformanceStorageMrtWorkload> workload,
    std::uint64_t inputBudget,
    std::uint64_t textureBudget)
{
    if (run_ || inputs_ || !workload || !inputBudget || !textureBudget)
        return "RHI-CONFORMANCE-STORAGE-MRT-INPUT-CONTRACT";
    if (const auto* id = workload->executionDiagnostic(device_->capabilities()))
        return id;
    inputs_ = core::makeShared<VulkanConformanceSampledInputs>(
        VulkanConformanceSampledInputs::Key{}, inputBudget);
    inputs_->storageMrt_ = true;
    inputs_->textureBudget_ = textureBudget;
    inputs_->buffers_.reserve(workload->frames().size());
    inputs_->textures_.reserve(workload->frames().size() * 2);
    return initializeFrames({}, {}, std::move(workload));
}

std::shared_ptr<GpuBuffer> VulkanConformanceClearExecution::prepareStorageInput(
    std::size_t frame, const char*& id)
{
    return prepareInput(ConformanceSampledInput::StorageValues, frame, id);
}

std::shared_ptr<GpuTexture> VulkanConformanceClearExecution::prepareStorageTexture(
    ConformanceOutputRole role, std::size_t frame, const char*& id)
{
    id = owner_->diagnostic();
    if (!id)
        id = diagnostic(*device_);
    if (id)
        return {};
    if (!run_->storage_ || prepared_ || run_->next_ || frame >= frames_.size()
        || (role != ConformanceOutputRole::Color1 && role != ConformanceOutputRole::Storage)) {
        id = fail("RHI-CONFORMANCE-STORAGE-MRT-TEXTURE-ORDER");
        return {};
    }
    const auto slot = role == ConformanceOutputRole::Color1 ? 0u : 1u;
    if (frames_[frame]->textures_[slot]) {
        id = fail("RHI-CONFORMANCE-STORAGE-MRT-TEXTURE-DUPLICATE");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        const auto extent = frames_[frame]->storageMrt_->extent;
        TextureDesc desc{slot ? "Conformance.StorageOutput" : "Conformance.MrtColor1",
            {extent.width, extent.height, 1},
            slot ? TextureFormat::Rgba16Float : TextureFormat::Rgba8Unorm,
            (slot ? TextureUsage::Storage | TextureUsage::Sampled : TextureUsage::RenderAttachment)
                | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined};
        const auto allocation = allocator_.queryTextureAllocationBytes(desc, id);
        if (id || !allocation
            || allocation > inputs_->textureBudget_ - inputs_->nativeTextureBytes_) {
            id = fail(id ? id : "RHI-CONFORMANCE-STORAGE-MRT-NATIVE-TEXTURE-BUDGET");
            return {};
        }
        auto object = core::shareUnique(device_->createTexture(desc));
        const auto native = object ? vulkanRhiNativePayload(*object) : nullptr;
        if (!native || !native->backendVulkanImageHandle()
            || native->backendVulkanDeviceHandle() != allocator_.runtime()->logicalDevice()
            || !object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > allocation) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-TEXTURE-NATIVE-ALLOCATION");
            return {};
        }
        inputs_->textures_.push_back({frame, slot, object});
        inputs_->nativeTextureBytes_ += allocation;
        frames_[frame]->textures_[slot] = native;
        frames_[frame]->textureIds_[slot] = object->id();
        return object;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

std::shared_ptr<ComputePipeline> VulkanConformanceClearExecution::prepareComputePipeline(
    const ComputePipelineDesc& state,
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact,
    const char*& id)
{
    id = owner_->diagnostic();
    if (!id)
        id = diagnostic(*device_);
    if (!id)
        id = shader::SingleEntryShaderArtifact::validateOriginal(artifact);
    if (id)
        return {};
    if (!run_->storage_ || prepared_ || run_->next_ || inputs_->compute_ || !state.layout
        || !state.shaderPackage
        || artifact->module().reflection.sourceSha256 != run_->storage_->computeSourceSha256()) {
        id = fail("RHI-CONFORMANCE-STORAGE-MRT-ORIGINAL-COMPUTE");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        ShaderPackageDesc package;
        const auto populated = populateSingleEntryShaderInterface(*artifact, package);
        if (!populated.success || package.entrypoints.size() != 1
            || state.shaderPackage->entrypoints.size() != 1
            || !PipelineDefinitionIdentity::matches(
                package.reflection, state.shaderPackage->reflection)
            || package.reflection.computeWorkgroupSizeX != 1
            || package.reflection.computeWorkgroupSizeY != 1
            || package.reflection.computeWorkgroupSizeZ != 1
            || !PipelineDefinitionIdentity::matches(state,
                ConformanceStorageMrtPipeline::computeDefinition(
                    *state.layout, *state.shaderPackage))) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-DEFINITION");
            return {};
        }
        package.entrypoints[0].modulePath = state.computeShader.modulePath;
        if (!PipelineDefinitionIdentity::matches(package.entrypoints[0], state.computeShader)
            || !PipelineDefinitionIdentity::matches(
                state.computeShader, state.shaderPackage->entrypoints[0])) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-ENTRY-ABI");
            return {};
        }
        const auto lowered = rhi::semantics::lowerComputePipelineState(*capabilityContract_, state);
        if (!lowered.success || !device_->validateComputePipeline(state).success) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-LOWERING");
            return {};
        }
        const auto& bytes = artifact->bytes(shader::ShaderArtifactRole::Spirv);
        if (bytes.empty() || bytes.size() % sizeof(std::uint32_t)) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-SPIRV");
            return {};
        }
        core::pmr::Vector<std::uint32_t> words(
            bytes.size() / sizeof(std::uint32_t), &core::defaultCpuMemoryResource());
        std::memcpy(words.data(), bytes.data(), bytes.size());
        auto layout =
            device_->createPipelineLayout(PipelineLayoutStateCopy::copy(state.layout->desc()));
        if (!layout) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-LAYOUT");
            return {};
        }
        auto definition = ComputePipelineStateCopy::copy(state);
        definition.layout = layout.get();
        const auto native =
            allocator_.createComputePipelineFromBytes(definition, words.data(), words.size());
        if (!native.success || !native.payload
            || !native.payload->backendVulkanComputePipelineHandle()) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-NATIVE");
            return {};
        }
        auto pipeline =
            createVulkanRhiComputePipeline(definition, native.payload, {}, std::move(layout));
        if (!pipeline) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-WRAPPER");
            return {};
        }
        owner_->computeArtifact = artifact;
        inputs_->computeArtifact_ = artifact;
        inputs_->nativeCompute_ = native.payload;
        inputs_->compute_ = core::shareUnique(std::move(pipeline));
        return inputs_->compute_;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

bool VulkanConformanceClearFrame::computePass(bool begin, core::pmr::String& id)
{
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || pass_ || computeActive_ == begin)
        return reject(id);
    if (begin) {
        if (state_ != State::Prepared || textureStates_[1] != ResourceState::ShaderWrite)
            return reject(id);
        bindings_ = 0;
        computeActive_ = true;
    } else {
        if (!dispatched_ || bindings_ != 3)
            return reject(id);
        computeActive_ = false;
        pass_ = 1;
        state_ = State::Stored;
    }
    return true;
}

bool VulkanConformanceClearFrame::computePipeline(
    const VulkanRhiNativeResourcePayload& pipeline, core::pmr::String& id)
{
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || !computeActive_ || dispatched_
        || (bindings_ & 1) || inputs_->diagnostic() || &pipeline != inputs_->nativeCompute_.get())
        return reject(id);
    bindings_ |= 1;
    return true;
}

bool VulkanConformanceClearFrame::dispatch(
    std::uint32_t x, std::uint32_t y, std::uint32_t z, core::pmr::String& id)
{
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || !computeActive_ || dispatched_
        || bindings_ != 3 || x != storageMrt_->extent.width || y != storageMrt_->extent.height
        || z != 1)
        return reject(id);
    dispatched_ = true;
    return true;
}

bool VulkanConformanceClearFrame::storageDescriptors(
    const VulkanRhiNativeResourcePayload& set, core::pmr::String& id)
{
    const auto bit = computeActive_ ? 2u : 16u;
    if (!issuer_.isCurrent() || failure_ || (bindings_ & bit)
        || (computeActive_ ? dispatched_ : state_ != State::Pass))
        return reject(id);
    const auto buffer = set.backendVulkanDescriptorElementResource(0);
    const auto interval = set.backendVulkanDescriptorElementBufferRange(0);
    if (!buffer || interval.first
        || (interval.second != VK_WHOLE_SIZE && interval.second != sizeof(storageMrt_->values))
        || !inputs_->buffer(*buffer,
            ConformanceSampledInput::StorageValues,
            storageMrt_->values.data(),
            sizeof(storageMrt_->values)))
        return reject(id);
    const auto view = set.backendVulkanDescriptorElementResource(1);
    const auto* shape = view ? view->backendVulkanOriginalTextureViewDesc() : nullptr;
    if (!shape || view->backendVulkanTextureViewParentPayload() != textures_[1]
        || shape->format != TextureFormat::Rgba16Float || shape->aspect != TextureAspect::Color
        || shape->baseMipLevel || shape->baseArrayLayer || shape->mipLevelCount != 1
        || shape->arrayLayerCount != 1
        || shape->usage != (computeActive_ ? TextureUsage::Storage : TextureUsage::Sampled)
        || textureStates_[1]
            != (computeActive_ ? ResourceState::ShaderWrite : ResourceState::ShaderRead))
        return reject(id);
    bindings_ |= bit;
    return true;
}

bool VulkanConformanceClearFrame::storageBegin(const RenderPassDesc& desc,
    const core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>& targets,
    const VulkanRhiNativeResourcePayload* depth,
    core::pmr::String& id)
{
    if (!issuer_.isCurrent() || failure_ || computeActive_ || pass_ != 1 || state_ != State::Stored
        || targets.size() != 2 || targets[0] != image_ || targets[1] != textures_[0]
        || targets[0] == targets[1] || textureStates_[0] != ResourceState::ColorAttachment
        || textureStates_[4] != ResourceState::ColorAttachment
        || textureStates_[1] != ResourceState::ShaderRead || desc.colorAttachments.size() != 2
        || depth || desc.hasDepthStencilAttachment || desc.layeredRendering
        || !desc.subpasses.empty() || !desc.subpassDependencies.empty())
        return reject(id);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& color = desc.colorAttachments[i];
        const auto slot = i ? 0u : 4u;
        const auto format = targets[i]->backendVulkanImageFormat() == VK_FORMAT_R8G8B8A8_UNORM
            ? TextureFormat::Rgba8Unorm
            : TextureFormat::Bgra8Unorm;
        if (color.target != textureIds_[slot] || color.format != format
            || color.loadOp != LoadOp::Clear || color.storeOp != StoreOp::Store
            || color.hasResolveTarget || color.resolveTarget.isValid() || color.clearColor.r
            || color.clearColor.g || color.clearColor.b || color.clearColor.a != 1)
            return reject(id);
    }
    bindings_ = 0;
    state_ = State::Pass;
    return true;
}

bool VulkanConformanceClearFrame::storageTransition(VulkanRhiNativeCommandPayload& command,
    const VulkanRhiNativeResourcePayload& image,
    const ResourceBarrierDesc& desc,
    core::pmr::String& id)
{
    const auto& range = desc.textureSubresources;
    if (!issuer_.isCurrent() || failure_ || computeActive_ || state_ == State::Pass
        || state_ == State::Copied || range.aspect != TextureAspect::Color || range.baseMipLevel
        || range.baseArrayLayer || range.mipLevelCount != 1 || range.arrayLayerCount != 1
        || ++imageBarriers_ > 5)
        return reject(id);
    std::size_t slot = 5;
    for (const auto candidate : {0u, 1u, 4u})
        if (textures_[candidate].get() == &image)
            slot = candidate;
    if (slot == 5 || desc.resource != textureIds_[slot] || desc.before != textureStates_[slot])
        return reject(id);
    const auto expected =
        makeInferredResourceBarrier(desc.resource, range, desc.before, desc.after);
    // ShaderWrite is exclusively compute; ShaderRead is exclusively fragment
    // in this authored dependency. Broader equivalent shader stages are allowed.
    const auto covers = [](PipelineStage actual, PipelineStage required) {
        return (actual & required) == required;
    };
    const auto sourceStage = desc.before == ResourceState::ShaderRead
        ? PipelineStage::FragmentShader
        : expected.sourceStages;
    const auto targetStage = desc.after == ResourceState::ShaderRead ? PipelineStage::FragmentShader
                                                                     : expected.destinationStages;
    const auto accessMatches =
        [](ResourceState state, ResourceAccess actual, ResourceAccess inferred) {
            if (state == ResourceState::ColorAttachment)
                return (actual & ResourceAccess::ColorAttachmentWrite) != ResourceAccess::None
                    && (static_cast<std::uint32_t>(actual)
                           & ~static_cast<std::uint32_t>(ResourceAccess::ColorAttachmentRead
                               | ResourceAccess::ColorAttachmentWrite))
                    == 0;
            return actual == inferred;
        };
    if (!covers(desc.sourceStages, sourceStage) || !covers(desc.destinationStages, targetStage)
        || !accessMatches(desc.before, desc.sourceAccess, expected.sourceAccess)
        || !accessMatches(desc.after, desc.destinationAccess, expected.destinationAccess))
        return reject(id);
    if (slot == 4 && desc.after == ResourceState::Present) {
        if (pass_ != 2 || !copyAuxiliary(command, id))
            return reject(id);
        return copyToPresent(command, image, desc, id);
    }
    const bool fill = slot == 1 && !pass_ && desc.before == ResourceState::Undefined
        && desc.after == ResourceState::ShaderWrite;
    const bool publish = slot == 1 && pass_ == 1 && desc.before == ResourceState::ShaderWrite
        && desc.after == ResourceState::ShaderRead;
    const bool color = slot != 1 && pass_ == 1 && desc.after == ResourceState::ColorAttachment
        && desc.before
            == (slot == 4 ? image.backendVulkanSwapchainAcquiredState() : ResourceState::Undefined);
    if ((!fill && !publish && !color)
        || !command.recordValidatedOriginalImageBarrier(image, desc, id))
        return reject(id);
    textureStates_[slot] = desc.after;
    return true;
}

bool VulkanConformanceClearFrame::copyAuxiliary(
    VulkanRhiNativeCommandPayload& command, core::pmr::String& id)
{
    for (std::size_t role = 0; role < 2; ++role) {
        const auto state = role ? ResourceState::ShaderRead : ResourceState::ColorAttachment;
        if (!textures_[role] || !auxiliaryStaging_[role] || textureStates_[role] != state)
            return reject(id);
        const auto range = TextureSubresourceRange{0, 1, 0, 1, TextureAspect::Color};
        auto barrier =
            makeInferredResourceBarrier(textureIds_[role], range, state, ResourceState::CopySrc);
        if (!command.recordValidatedOriginalImageBarrier(*textures_[role], barrier, id)
            || !command.flushPendingBarriers(id))
            return reject(id);
        TextureToBufferCopyDesc copy;
        copy.extent = {storageMrt_->extent.width, storageMrt_->extent.height, 1};
        copy.bytesPerRow = storageMrt_->extent.width * (role ? 8 : 4);
        copy.rowsPerImage = storageMrt_->extent.height;
        copy.textureSubresources = range;
        internalCopy_ = true;
        const auto copied =
            command.recordImageToBufferCopy(*textures_[role], *auxiliaryStaging_[role], copy, id);
        internalCopy_ = false;
        if (!copied)
            return reject(id);
        barrier =
            makeInferredResourceBarrier(textureIds_[role], range, ResourceState::CopySrc, state);
        if (!command.recordValidatedOriginalImageBarrier(*textures_[role], barrier, id))
            return reject(id);
    }
    return true;
}

const char* VulkanConformanceClearExecution::observeAuxiliary()
{
    auto& frame = *frames_[run_->next_];
    auto& output = run_->frames_[run_->next_];
    bool matches = true;
    for (std::size_t role = 0; role < 2; ++role) {
        auto& bytes = role ? output.storage : output.color1;
        core::pmr::String message;
        if (!frame.auxiliaryStaging_[role]->backendInvalidateMappedRead(0, bytes.size(), message))
            return "RHI-CONFORMANCE-STORAGE-MRT-READBACK-INVALIDATE";
        const auto* actual = frame.auxiliaryStaging_[role]->backendMappedData();
        if (!actual)
            return "RHI-CONFORMANCE-STORAGE-MRT-READBACK-MAPPING";
        std::memcpy(bytes.data(), actual, bytes.size());
        if (!role) {
            for (std::size_t i = 0; i < bytes.size(); ++i)
                matches = matches && bytes[i] == frame.storageMrt_->expectedColor1[i % 4];
        } else {
            for (std::size_t i = 0; i < bytes.size(); i += 2) {
                const auto bits = std::uint16_t(bytes[i]) | (std::uint16_t(bytes[i + 1]) << 8);
                matches = matches
                    && bits == core::floatToHalf(frame.storageMrt_->expectedStorage[(i / 2) % 4]);
            }
        }
    }
    return matches ? nullptr : "RHI-CONFORMANCE-STORAGE-MRT-OUTPUT-MISMATCH";
}
} // namespace arp::rhi_vulkan
