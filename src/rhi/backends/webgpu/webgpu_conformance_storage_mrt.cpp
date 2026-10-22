#include "core/math/half_float.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_conformance_artifact_wait.h"
#include "rhi/backends/webgpu/webgpu_conformance_clear.h"
#include "rhi/backends/webgpu/webgpu_conformance_sampled_inputs.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/conformance/sampled_command_contract.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/frontend/descriptor_buffer_binding_range.h"
#include "rhi/gpu_device.h"

namespace arp::rhi_webgpu {
using namespace rhi;

const char* WebGpuConformanceClearExecution::initialize(
    core::UniquePtr<ConformanceStorageMrtWorkload> workload,
    const ConformanceStorageMrtExecutionRequest& request)
{
    if (const auto* id = ConformanceStorageMrtExecutionContract::validateWebGpu(request))
        return id;
    if (!workload || run_ || inputs_)
        return "RHI-CONFORMANCE-STORAGE-MRT-INPUT-CONTRACT";
    if (const auto* id = workload->executionDiagnostic(device_->capabilities()))
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    auto native = emscripten_webgpu_get_device();
    auto limits = WGPU_LIMITS_INIT;
    const auto status = native ? wgpuDeviceGetLimits(native, &limits) : WGPUStatus_Error;
    if (native)
        wgpuDeviceRelease(native);
    if (status != WGPUStatus_Success || limits.maxColorAttachments < 2
        || limits.maxStorageBufferBindingSize < sizeof(std::array<float, 12>)
        || !limits.maxComputeWorkgroupsPerDimension)
        return "RHI-CONFORMANCE-STORAGE-MRT-NATIVE-LIMITS";
    for (const auto& frame : workload->frames())
        if (frame.extent.width > limits.maxComputeWorkgroupsPerDimension
            || frame.extent.height > limits.maxComputeWorkgroupsPerDimension)
            return "RHI-CONFORMANCE-STORAGE-MRT-DISPATCH-LIMIT";
    inputs_ = core::makeShared<WebGpuConformanceSampledInputs>(
        request.maxNativeInputBufferBytes, request.artifacts);
    inputs_->dispatchLimit_ = limits.maxComputeWorkgroupsPerDimension;
    inputs_->storageMrt_ = true;
    inputs_->textureBudget_ = request.maxNativeTextureBytes;
    inputs_->buffers_.reserve(workload->frames().size());
    inputs_->textures_.reserve(workload->frames().size() * 2);
    owner_->inputs = inputs_;
    return initializeFrames({}, {}, std::move(workload));
#else
    return "RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE";
#endif
}

std::shared_ptr<GpuBuffer> WebGpuConformanceClearExecution::prepareStorageInput(
    std::size_t frame, const char*& id)
{
    return prepareInput(ConformanceSampledInput::StorageValues, frame, id);
}

std::shared_ptr<GpuTexture> WebGpuConformanceClearExecution::prepareStorageTexture(
    ConformanceOutputRole role, std::size_t frame, const char*& id)
{
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
        // WebGPU exposes no physical allocation query. Use exactly the same
        // format/extent/mip/sample ledger contract as the device allocator.
        const auto bytes = estimateWebGpuRhiTextureBytes(desc);
        if (!bytes || bytes > inputs_->textureBudget_ - inputs_->nativeTextureBytes_) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-NATIVE-TEXTURE-BUDGET");
            return {};
        }
        auto object = core::shareUnique(device_->createTexture(desc));
        auto native = object ? webGpuNativePayloadFor(*object) : nullptr;
        if (!native || object->allocation().estimatedSizeBytes != bytes) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-TEXTURE-NATIVE-ALLOCATION");
            return {};
        }
        inputs_->textures_.push_back(object);
        inputs_->nativeTextureBytes_ += bytes;
        frames_[frame]->textures_[slot] = native;
        frames_[frame]->textureIds_[slot] = object->id();
        return object;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

std::shared_ptr<ComputePipeline> WebGpuConformanceClearExecution::prepareComputePipeline(
    const ComputePipelineDesc& state,
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact,
    const char*& id)
{
    id = diagnostic(*device_);
    if (!id)
        id = shader::SingleEntryShaderArtifact::validateOriginal(artifact);
    if (id)
        return {};
    if (!inputs_ || !run_->storage_ || inputs_->compute_ || prepared_ || run_->next_
        || !state.layout || !state.shaderPackage
        || artifact->module().reflection.sourceSha256 != run_->storage_->computeSourceSha256()
        || !PipelineDefinitionIdentity::matches(state,
            ConformanceStorageMrtPipeline::computeDefinition(
                *state.layout, *state.shaderPackage))) {
        id = fail("RHI-CONFORMANCE-STORAGE-MRT-ORIGINAL-COMPUTE");
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        const auto& b = inputs_->artifacts_;
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(b.inputMetadataBytes);
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(b.creationOwnerBytes);
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(b.nativeMetadataBytes);
        auto callbackQuota = core::makeShared<core::CpuMemoryQuota>(b.callbackBytes);
        auto wrapperQuota = core::makeShared<core::CpuMemoryQuota>(b.wrapperBytes);
        core::pmr::String message{&core::defaultCpuMemoryResource()};
        auto input = OriginalSingleEntryPipelineInput::prepare(
            artifact, {1, b.inputMetadataBytes}, inputQuota, message);
        if (!input || input->package().entrypoints.size() != 1
            || !PipelineDefinitionIdentity::matches(
                state.shaderPackage->reflection, input->package().reflection)
            || state.computeShader.entryPoint != input->package().entrypoints[0].entryPoint
            || input->package().reflection.computeWorkgroupSizeX != 1
            || input->package().reflection.computeWorkgroupSizeY != 1
            || input->package().reflection.computeWorkgroupSizeZ != 1) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-ENTRY-ABI");
            return {};
        }
        auto layout = core::shareUnique(
            device_->createPipelineLayout(PipelineLayoutStateCopy::copy(state.layout->desc())));
        if (!layout) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-LAYOUT");
            return {};
        }
        auto authored = ConformanceStorageMrtPipeline::computeDefinition(*layout, input->package());
        if (!validateOriginalWebGpuRhiComputePipeline(authored, input).success
            || !semantics::lowerComputePipelineState(*capabilityContract_, authored).success) {
            id = fail("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-LOWERING");
            return {};
        }
        using Creation = WebGpuOriginalArtifactComputeCreation;
        auto creation = Creation::prepareOriginal(input,
            layout,
            authored.label,
            {1,
                true,
                WebGpuErrorScopeContract::count,
                authored.label.size(),
                b.diagnosticBytes,
                b.creationOwnerBytes,
                b.callbackBytes,
                {1, true, b.targetBytes, b.nativeMetadataBytes}},
            ownerQuota,
            nativeQuota,
            callbackQuota,
            id);
        if (!creation
            || (id = WebGpuConformanceArtifactWait::initialize(
                    creation, b.pipelineCreationTimeoutNanoseconds))) {
            fail(id);
            return {};
        }
        auto pipeline = WebGpuAcceptedOriginalComputePipelineFactory::create(
            authored, creation, wrapperQuota, id);
        auto native = pipeline ? webGpuNativePayloadFor(*pipeline) : nullptr;
        if (!native
            || (id = WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(native))) {
            id = fail(id ? id : "RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-NATIVE");
            return {};
        }
        inputs_->computeArtifact_ = artifact;
        inputs_->computeCreation_ = creation;
        inputs_->nativeCompute_ = native;
        inputs_->compute_ = core::shareUnique(std::move(pipeline));
        return inputs_->compute_;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
#else
    id = fail("RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE");
    return {};
#endif
}

bool WebGpuConformanceClearFrame::rejectStorage(core::pmr::String& id)
{
    id = reject("RHI-CONFORMANCE-STORAGE-MRT-COMMAND-LANGUAGE");
    return false;
}

bool WebGpuConformanceClearFrame::computePass(bool begin, core::pmr::String& id)
{
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || pass_ || computeActive_ == begin)
        return rejectStorage(id);
    if (begin) {
        if (state_ != State::Prepared || textureStates_[1] != ResourceState::ShaderWrite)
            return rejectStorage(id);
        bindings_ = 0;
        computeActive_ = true;
    } else {
        if (!dispatched_ || bindings_ != 3)
            return rejectStorage(id);
        computeActive_ = false;
        pass_ = 1;
        state_ = State::Stored;
    }
    return true;
}

bool WebGpuConformanceClearFrame::computePipeline(
    const WebGpuNativeComputePipelinePayload& pipeline, core::pmr::String& id)
{
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || !computeActive_ || dispatched_
        || (bindings_ & 1) || inputs_->diagnostic() || &pipeline != inputs_->nativeCompute_.get())
        return rejectStorage(id);
    bindings_ |= 1;
    return true;
}

bool WebGpuConformanceClearFrame::dispatch(
    std::uint32_t x, std::uint32_t y, std::uint32_t z, core::pmr::String& id)
{
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || !computeActive_ || dispatched_
        || bindings_ != 3 || x != storageMrt_->extent.width || y != storageMrt_->extent.height
        || x > inputs_->dispatchLimit_ || y > inputs_->dispatchLimit_ || z != 1)
        return rejectStorage(id);
    dispatched_ = true;
    return true;
}

bool WebGpuConformanceClearFrame::storageDescriptors(const WebGpuNativeDescriptorGroupPayload* set,
    std::uint32_t index,
    std::uint32_t dynamicCount,
    core::pmr::String& id)
{
    const auto bit = computeActive_ ? 2u : 16u;
    if (!storageMrt_ || !issuer_.isCurrent() || failure_ || index || dynamicCount || !set
        || set->tableBacked || set->bindings.size() != 2 || set->dynamicOffsetCount
        || !set->dynamicOffsetBindings.empty() || (bindings_ & bit)
        || (computeActive_ ? dispatched_ : state_ != State::Pass || drew_))
        return rejectStorage(id);
    std::array<const WebGpuNativeDescriptorBindingPayload*, 2> entries{};
    for (const auto& entry : set->bindings) {
        if (entry.write.binding >= entries.size() || entries[entry.write.binding]
            || entry.arrayElement)
            return rejectStorage(id);
        entries[entry.write.binding] = &entry;
    }
    const auto* input = entries[0];
    const auto* original = input && input->buffer ? inputs_->buffer(*input->buffer,
                                                        ConformanceSampledInput::StorageValues,
                                                        storageMrt_->values.data(),
                                                        sizeof(storageMrt_->values))
                                                  : nullptr;
    if (!original || original->frame != frameIndex_ || input->textureView || input->sampler
        || input->write.type != DescriptorType::ReadOnlyStorageBuffer)
        return rejectStorage(id);
    DescriptorBufferBindingRangeContract::Resolved range;
    if (DescriptorBufferBindingRangeContract::resolve(
            input->write.bufferBindingRange, input->buffer->sizeBytes, 0, range)
        || range.offsetBytes || range.sizeBytes != sizeof(storageMrt_->values))
        return rejectStorage(id);
    TextureViewDesc shape;
    shape.format = TextureFormat::Rgba16Float;
    shape.aspect = TextureAspect::Color;
    shape.usage = computeActive_ ? TextureUsage::Storage : TextureUsage::Sampled;
    const auto* view = entries[1];
    if (!view || !view->textureView || view->buffer || view->sampler
        || view->write.type
            != (computeActive_ ? DescriptorType::StorageTexture : DescriptorType::SampledTexture)
        || view->textureView->parentTexture != textures_[1]
        || !view->textureView->matchesDescription(shape)
        || textureStates_[1]
            != (computeActive_ ? ResourceState::ShaderWrite : ResourceState::ShaderRead))
        return rejectStorage(id);
    bindings_ |= bit;
    return true;
}

bool WebGpuConformanceClearFrame::storageBegin(const RenderPassDesc& desc,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& targets,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& resolves,
    const WebGpuNativeTexturePayload* depth,
    core::pmr::String& id)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!issuer_.isCurrent() || failure_ || computeActive_ || pass_ != 1 || state_ != State::Stored
        || targets.size() != 2 || resolves.size() != 2 || resolves[0] || resolves[1]
        || targets[0] != image_.get() || targets[1] != textures_[0].get()
        || targets[0] == targets[1] || textureStates_[0] != ResourceState::ColorAttachment
        || textureStates_[4] != ResourceState::ColorAttachment
        || textureStates_[1] != ResourceState::ShaderRead || desc.colorAttachments.size() != 2
        || depth || desc.hasDepthStencilAttachment || desc.layeredRendering
        || !desc.subpasses.empty() || !desc.subpassDependencies.empty())
        return rejectStorage(id);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& color = desc.colorAttachments[i];
        if (color.target != textureIds_[i ? 0u : 4u] || color.format != targets[i]->rhiFormat
            || color.loadOp != LoadOp::Clear || color.storeOp != StoreOp::Store
            || color.hasResolveTarget || color.resolveTarget.isValid() || color.clearColor.r
            || color.clearColor.g || color.clearColor.b || color.clearColor.a != 1)
            return rejectStorage(id);
    }
    bindings_ = 0;
    drew_ = false;
    state_ = State::Pass;
    return true;
#else
    (void)desc;
    (void)targets;
    (void)resolves;
    (void)depth;
    return rejectStorage(id);
#endif
}

bool WebGpuConformanceClearFrame::storageTransition(WebGpuNativeCommandPayload& command,
    const WebGpuNativeTexturePayload& image,
    const ResourceBarrierDesc& desc,
    core::pmr::String& id)
{
    const auto& range = desc.textureSubresources;
    if (!issuer_.isCurrent() || failure_ || !canvas_ || !canvas_->surface_.isCurrent()
        || canvas_->discarded_ || !canvas_->issuer_.sameCurrentEpoch(issuer_) || computeActive_
        || state_ == State::Pass || state_ == State::Copied || state_ == State::Finished
        || range.aspect != TextureAspect::Color || range.baseMipLevel || range.baseArrayLayer
        || range.mipLevelCount != 1 || range.arrayLayerCount != 1 || ++imageBarriers_ > 5)
        return rejectStorage(id);
    std::size_t slot = textures_.size();
    for (const auto candidate : {0u, 1u, 4u})
        if (textures_[candidate].get() == &image)
            slot = candidate;
    if (slot == textures_.size() || desc.resource != textureIds_[slot]
        || desc.before != textureStates_[slot])
        return rejectStorage(id);
    for (const auto& scope :
        {semantics::ResourceAccessScope{desc.before, desc.sourceStages, desc.sourceAccess},
            semantics::ResourceAccessScope{
                desc.after, desc.destinationStages, desc.destinationAccess}}) {
        if (scope.state == ResourceState::Undefined) {
            if (scope.stages != PipelineStage::TopOfPipe || scope.access != ResourceAccess::None)
                return rejectStorage(id);
        } else {
            if (semantics::validateResourceAccessScope(scope))
                return rejectStorage(id);
            if (scope.state == ResourceState::ShaderWrite
                && ((scope.stages & PipelineStage::ComputeShader) == PipelineStage::None
                    || scope.access != ResourceAccess::ShaderWrite))
                return rejectStorage(id);
            if (scope.state == ResourceState::ShaderRead
                && ((scope.stages & PipelineStage::FragmentShader) == PipelineStage::None
                    || scope.access != ResourceAccess::ShaderRead))
                return rejectStorage(id);
            if (scope.state == ResourceState::ColorAttachment
                && (scope.access & ResourceAccess::ColorAttachmentWrite) == ResourceAccess::None)
                return rejectStorage(id);
        }
    }
    if (slot == 4 && desc.after == ResourceState::Present) {
        if (pass_ != 2 || state_ != State::Stored || desc.before != ResourceState::ColorAttachment)
            return rejectStorage(id);
        return copyOutputs(command, id);
    }
    const bool fill = slot == 1 && !pass_ && desc.before == ResourceState::Undefined
        && desc.after == ResourceState::ShaderWrite;
    const bool publish = slot == 1 && pass_ == 1 && desc.before == ResourceState::ShaderWrite
        && desc.after == ResourceState::ShaderRead;
    const bool color = slot != 1 && pass_ == 1 && desc.before == ResourceState::Undefined
        && desc.after == ResourceState::ColorAttachment;
    if (!fill && !publish && !color)
        return rejectStorage(id);
    // WebGPU pass boundaries and usage scopes provide the approved equivalent
    // dependency; this journal authenticates the exact L2 producer/consumer.
    textureStates_[slot] = desc.after;
    return true;
}

bool WebGpuConformanceClearFrame::copyOutputs(
    WebGpuNativeCommandPayload& command, core::pmr::String& id)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!command.impl_ || command.impl_->renderPass || command.impl_->computePass
        || command.impl_->finished || command.impl_->submitted)
        return rejectStorage(id);
    for (std::size_t role = 0; role < readbackCount(); ++role) {
        const auto slot = role == 0 ? 4u : role - 1;
        if (!textures_[slot] || !readbacks_[role].staging
            || textureStates_[slot]
                != (role == 2 ? ResourceState::ShaderRead : ResourceState::ColorAttachment))
            return rejectStorage(id);
        if (const auto* error =
                WebGpuNativeOriginalTextureReadbackLowering::emitFrozen(command.impl_->encoder,
                    *textures_[slot],
                    *readbacks_[role].staging,
                    readbacks_[role].copy)) {
            id = reject(error);
            return false;
        }
    }
    state_ = State::Copied;
    return true;
#else
    (void)command;
    return rejectStorage(id);
#endif
}

const char* WebGpuConformanceClearExecution::observeAuxiliary()
{
    auto& frame = *frames_[run_->next_];
    auto& output = run_->frames_[run_->next_];
    bool matches = true;
    for (std::size_t role = 1; role < 3; ++role) {
        auto& bytes = role == 1 ? output.color1 : output.storage;
        const auto& readback = frame.readbacks_[role];
        const auto row = std::size_t(output.extent.width) * (role == 2 ? 8 : 4);
        if (!readback.ready || !readback.map.nativeUnmapProven())
            return "RHI-CONFORMANCE-STORAGE-MRT-READBACK-MAPPING";
        for (std::size_t y = 0; y < output.extent.height; ++y)
            std::copy_n(readback.padded.data() + y * readback.copy.original.bytesPerRow,
                row,
                bytes.data() + y * row);
        if (role == 1) {
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
} // namespace arp::rhi_webgpu
