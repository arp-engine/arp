#include "core/unsigned_range.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_conformance_artifact_wait.h"
#include "rhi/backends/webgpu/webgpu_conformance_clear.h"
#include "rhi/backends/webgpu/webgpu_conformance_sampled_inputs.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/conformance/sampled_command_contract.h"
#include "rhi/conformance/sampled_pipeline.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/frontend/descriptor_buffer_binding_range.h"
#include "rhi/gpu_device.h"

#include <chrono>
#include <cstring>

namespace arp::rhi_webgpu {
using namespace rhi;

const WebGpuConformanceSampledInputs::Buffer* WebGpuConformanceSampledInputs::buffer(
    const WebGpuNativeBufferPayload& native,
    ConformanceSampledInput role,
    const void* bytes,
    std::size_t size) const noexcept
{
    for (const auto& entry : buffers_)
        if (entry.native.get() == &native && entry.role == role && entry.object->hostWritesSealed()
            && entry.object->gpuWritesSealed() && entry.object->gpuTransferWritesSealed()
            && entry.content.initialized(0, entry.content.size())
            && (!bytes
                || (entry.content.size() >= size
                    && !std::memcmp(entry.content.data(), bytes, size))))
            return &entry;
    return nullptr;
}

const char* WebGpuConformanceSampledInputs::diagnostic() const noexcept
{
    if ((storageMrt_ ? !nativeCompute_ : !nativeSampler_)
        || (!nativePipelines_[0] && !nativePipelines_[1]))
        return "RHI-CONFORMANCE-SAMPLED-ORIGINAL-INPUT-INCOMPLETE";
    return sourceDiagnostic();
}

const char* WebGpuConformanceSampledInputs::sourceDiagnostic() const noexcept
{
    if (artifact_)
        if (const auto* id = shader::GraphicsShaderModuleArtifact::validateOriginal(artifact_))
            return id;
    if (computeArtifact_)
        if (const auto* id = shader::SingleEntryShaderArtifact::validateOriginal(computeArtifact_))
            return id;
    if (computeCreation_) {
        if (const auto* id =
                WebGpuOriginalArtifactComputeCreation::validateReadyOriginal(computeCreation_))
            return id;
        if (const auto* id =
                WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(nativeCompute_))
            return id;
    }
    for (std::size_t i = 0; i < creations_.size(); ++i) {
        if (!creations_[i])
            continue;
        if (const auto* id =
                WebGpuOriginalArtifactGraphicsCreation::validateReadyOriginal(creations_[i]))
            return id;
        if (const auto* id = WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(
                nativePipelines_[i]))
            return id;
    }
    return nullptr;
}

const char* WebGpuConformanceClearExecution::initialize(
    core::UniquePtr<ConformanceSampledWorkload> workload,
    const ConformanceSampledExecutionRequest& request)
{
    if (const auto* id = request.artifacts.diagnostic())
        return id;
    if (!workload || run_ || inputs_)
        return "RHI-CONFORMANCE-SAMPLED-ORIGINAL-INPUT-CONTRACT";
    inputs_ = core::makeShared<WebGpuConformanceSampledInputs>(
        request.maxNativeInputBufferBytes, request.artifacts);
    owner_->inputs = inputs_;
    inputs_->buffers_.reserve(workload->frames().size() + 2);
    return initializeFrames({}, std::move(workload));
}

std::shared_ptr<GpuBuffer> WebGpuConformanceClearExecution::prepareInput(
    ConformanceSampledInput role, std::size_t frame, const char*& id)
{
    id = diagnostic(*device_);
    if (id)
        return {};
    if (!inputs_ || (!run_->sampled_ && !run_->storage_) || prepared_ || run_->next_
        || frame >= run_->frameCount()
        || (run_->storage_ ? role != ConformanceSampledInput::StorageValues
                           : (role != ConformanceSampledInput::Uniforms && frame))) {
        id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-ORDER");
        return {};
    }
    for (const auto& entry : inputs_->buffers_)
        if (entry.role == role && entry.frame == frame) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-DUPLICATE");
            return {};
        }
    const void* bytes = nullptr;
    std::size_t size = 0;
    ResourceUsage usage = ResourceUsage::None;
    switch (role) {
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
        bytes = run_->sampled_->frames()[frame].uniforms.data();
        size = sizeof(run_->sampled_->frames()[frame].uniforms);
        usage = ResourceUsage::UniformBuffer;
        break;
    case ConformanceSampledInput::StorageValues:
        if (!run_->storage_) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-ROLE");
            return {};
        }
        bytes = run_->storage_->frames()[frame].values.data();
        size = sizeof(run_->storage_->frames()[frame].values);
        usage = ResourceUsage::StorageBuffer;
        break;
    default:
        id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-ROLE");
        return {};
    }
    // queue.writeBuffer and native buffer sizes require multiples of four.
    std::uint64_t aligned = 0;
    if (!core::roundUpUnsigned(std::uint64_t(size), std::uint64_t(4), aligned)
        || aligned > inputs_->budget_ - inputs_->nativeBytes_ || aligned > SIZE_MAX) {
        id = fail("RHI-CONFORMANCE-SAMPLED-NATIVE-INPUT-BUDGET");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        core::InitializedByteBuffer content{std::size_t(aligned)};
        core::ByteBuffer padded(std::size_t(aligned), 0, &core::defaultCpuMemoryResource());
        std::memcpy(padded.data(), bytes, size);
        if (!content.write(0, padded.data(), padded.size())) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-INITIALIZATION");
            return {};
        }
        auto object =
            core::shareUnique(device_->createBuffer({"WebGpuSharedConformance/1 immutable input",
                aligned,
                usage | ResourceUsage::CopyDst,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead}));
        const auto native = object ? webGpuNativePayloadFor(*object) : nullptr;
        if (!native || native->sizeBytes != aligned) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-NATIVE-ALLOCATION");
            return {};
        }
        const auto written = device_->writeBuffer(*object, padded.data(), padded.size());
        if (!written.success) {
            id = fail("RHI-CONFORMANCE-SAMPLED-INPUT-WRITE-FLUSH");
            return {};
        }
        object->sealWrites();
        inputs_->buffers_.push_back({role, frame, object, native, std::move(content)});
        inputs_->nativeBytes_ += aligned;
        return object;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

std::shared_ptr<GpuSampler> WebGpuConformanceClearExecution::prepareSampler(const char*& id)
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
            inputs_->sampler_ ? webGpuNativePayloadFor(*inputs_->sampler_) : nullptr;
        if (!inputs_->nativeSampler_) {
            id = fail("RHI-CONFORMANCE-SAMPLED-SAMPLER-NATIVE");
            return {};
        }
        return inputs_->sampler_;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

std::shared_ptr<RenderPipeline> WebGpuConformanceClearExecution::preparePipeline(
    const RenderPipelineDesc& state,
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact,
    const char*& id)
{
    id = diagnostic(*device_);
    if (!id)
        id = shader::GraphicsShaderModuleArtifact::validateOriginal(artifact);
    if (id)
        return {};
    const auto index = state.colorFormat == TextureFormat::Rgba8Unorm ? 0u : 1u;
    if (!inputs_ || (!run_->sampled_ && !run_->storage_) || inputs_->pipelines_[index] || prepared_
        || run_->next_ || !state.layout || !state.shaderPackage
        || (state.colorFormat != TextureFormat::Rgba8Unorm
            && state.colorFormat != TextureFormat::Bgra8Unorm)
        || (inputs_->artifact_ && inputs_->artifact_ != artifact)
        || artifact->module().reflection.sourceSha256
            != (run_->storage_ ? run_->storage_->shaderSourceSha256()
                               : run_->sampled_->shaderSourceSha256())
        || !PipelineDefinitionIdentity::matches(state,
            run_->storage_ ? ConformanceStorageMrtPipeline::pipelineDefinition(
                                 *state.layout, *state.shaderPackage, state.colorFormat)
                           : ConformanceSampledPipeline::definition(
                                 *state.layout, *state.shaderPackage, state.colorFormat))) {
        id = fail("RHI-CONFORMANCE-SAMPLED-ORIGINAL-SHADER");
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
        core::pmr::String diagnosticText{&core::defaultCpuMemoryResource()};
        auto input = OriginalGraphicsPipelineInput::prepare(
            artifact, {1, b.inputMetadataBytes, 0, false}, inputQuota, diagnosticText);
        if (!input) {
            id = fail("RHI-CONFORMANCE-SAMPLED-ORIGINAL-ARTIFACT-INPUT");
            return {};
        }
        if (!PipelineDefinitionIdentity::matches(
                state.shaderPackage->reflection, input->package().reflection)
            || state.vertexShader.entryPoint != input->package().entrypoints[0].entryPoint
            || state.fragmentShader.entryPoint != input->package().entrypoints[1].entryPoint) {
            id = fail("RHI-CONFORMANCE-SAMPLED-ORIGINAL-ENTRY-ABI");
            return {};
        }
        auto layout = core::shareUnique(
            device_->createPipelineLayout(PipelineLayoutStateCopy::copy(state.layout->desc())));
        if (!layout) {
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-LAYOUT");
            return {};
        }
        auto authored = run_->storage_
            ? ConformanceStorageMrtPipeline::pipelineDefinition(
                  *layout, input->package(), state.colorFormat)
            : ConformanceSampledPipeline::definition(*layout, input->package(), state.colorFormat);
        const auto validation = validateOriginalWebGpuRhiRenderPipeline(authored, input);
        const auto lowered = semantics::lowerRenderPipelineState(*capabilityContract_, authored);
        if (!validation.success || !lowered.success) {
            std::printf("shared sampled pipeline lowering: %s\n", validation.diagnostics.c_str());
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-LOWERING");
            return {};
        }
        using Creation = WebGpuOriginalArtifactGraphicsCreation;
        if (!WebGpuOriginalArtifactGraphicsFactory::validateDefinition(
                input, authored, diagnosticText, 2)) {
            std::printf("shared sampled original definition: %s\n", diagnosticText.c_str());
            id = fail("RHI-CONFORMANCE-SAMPLED-PIPELINE-DEFINITION");
            return {};
        }
        auto creation = Creation::prepareOriginal(input,
            layout,
            authored,
            {1,
                true,
                WebGpuErrorScopeContract::count,
                authored.label.size(),
                b.diagnosticBytes,
                b.creationOwnerBytes,
                b.callbackBytes,
                {2, true, b.targetBytes, b.nativeMetadataBytes}},
            ownerQuota,
            nativeQuota,
            callbackQuota,
            id);
        if (!creation) {
            fail(id);
            return {};
        }
        if ((id = WebGpuConformanceArtifactWait::initialize(
                 creation, b.pipelineCreationTimeoutNanoseconds))) {
            fail(id);
            return {};
        }
        auto pipeline = WebGpuAcceptedOriginalGraphicsPipelineFactory::create(
            authored, creation, wrapperQuota, id);
        if (!pipeline) {
            fail(id);
            return {};
        }
        const auto native = webGpuNativePayloadFor(*pipeline);
        if (!native
            || (id = WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(native))) {
            id = fail(id ? id : "RHI-CONFORMANCE-SAMPLED-NATIVE-PIPELINE");
            return {};
        }
        inputs_->artifact_ = artifact;
        owner_->artifact = artifact;
        inputs_->nativePipelines_[index] = native;
        inputs_->creations_[index] = creation;
        inputs_->pipelines_[index] = core::shareUnique(std::move(pipeline));
        return inputs_->pipelines_[index];
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
#else
    id = fail("RHI-CONFORMANCE-SAMPLED-WEBGPU-NATIVE-UNAVAILABLE");
    return {};
#endif
}

bool WebGpuConformanceClearFrame::rejectSampled(core::pmr::String& id)
{
    id = reject("RHI-CONFORMANCE-SAMPLED-COMMAND-LANGUAGE");
    return false;
}

bool WebGpuConformanceClearFrame::sampledTransition(WebGpuNativeCommandPayload& command,
    const WebGpuNativeTexturePayload& image,
    const ResourceBarrierDesc& desc,
    core::pmr::String& id)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& range = desc.textureSubresources;
    if (!issuer_.isCurrent() || failure_ || !canvas_ || !canvas_->surface_.isCurrent()
        || canvas_->discarded_ || !canvas_->issuer_.sameCurrentEpoch(issuer_)
        || state_ == State::Pass || state_ == State::Copied || state_ == State::Finished
        || range.aspect != TextureAspect::Color || range.baseMipLevel || range.baseArrayLayer
        || range.mipLevelCount != 1 || range.arrayLayerCount != 1 || desc.before == desc.after
        || ++imageBarriers_ > 3 + sampled_->draws.size() * 3
        || !ConformanceSampledCommandContract::imageScopes(desc))
        return rejectSampled(id);
    if (&image == image_.get() && desc.after == ResourceState::Present) {
        if (pass_ != sampled_->draws.size() + 2 || state_ != State::Stored
            || desc.resource != textureIds_[4] || desc.before != textureStates_[4]
            || desc.before != ResourceState::ColorAttachment)
            return rejectSampled(id);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!command.impl_ || command.impl_->renderPass || command.impl_->computePass
            || command.impl_->finished || command.impl_->submitted)
            return rejectSampled(id);
        if (const auto* error = WebGpuNativeOriginalTextureReadbackLowering::emitFrozen(
                command.impl_->encoder, image, *readbacks_[0].staging, readbacks_[0].copy)) {
            id = reject(error);
            return false;
        }
        state_ = State::Copied;
        return true;
#else
        return rejectSampled(id);
#endif
    }
    if (pass_ >= sampled_->draws.size() + 2)
        return rejectSampled(id);
    const auto target = pass_ < 2 ? pass_ : std::size_t(sampled_->draws[pass_ - 2].target);
    std::size_t index = textures_.size();
    for (std::size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i].get() == &image)
            index = i;
    if (index == textures_.size()) {
        if (target == 4 || textures_[target] || desc.after != ResourceState::ColorAttachment
            || desc.before != ResourceState::Undefined || image.originalCanvas_
            || image.creationState() != ResourceState::Undefined
            || image.rhiFormat != TextureFormat::Rgba8Unorm)
            return rejectSampled(id);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!image.texture || image.ownerDevice != nativeDevice_ || image.browserSurfaceTexture
            || wgpuTextureGetWidth(image.texture) != sampled_->extent.width
            || wgpuTextureGetHeight(image.texture) != sampled_->extent.height
            || wgpuTextureGetDepthOrArrayLayers(image.texture) != 1
            || wgpuTextureGetMipLevelCount(image.texture) != 1
            || wgpuTextureGetSampleCount(image.texture) != 1
            || wgpuTextureGetFormat(image.texture) != WGPUTextureFormat_RGBA8Unorm
            || wgpuTextureGetUsage(image.texture)
                != (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding))
            return rejectSampled(id);
        for (const auto& retained : command.impl_->retainedTextures)
            if (retained.get() == &image)
                textures_[target] = retained;
#endif
        if (!textures_[target])
            return rejectSampled(id);
        textureIds_[target] = desc.resource;
        index = target;
    }
    if (desc.resource != textureIds_[index] || desc.before != textureStates_[index])
        return rejectSampled(id);
    bool allowed = desc.after == ResourceState::ColorAttachment && index == target;
    if (desc.after == ResourceState::ShaderRead && pass_ >= 2 && index != 4) {
        const auto& draw = sampled_->draws[pass_ - 2];
        allowed = index == std::size_t(draw.sourceA) || index == std::size_t(draw.sourceB);
    }
    if (!allowed)
        return rejectSampled(id);
    textureStates_[index] = desc.after;
    return true;
#else
    (void)command;
    (void)image;
    (void)desc;
    return rejectSampled(id);
#endif
}

bool WebGpuConformanceClearFrame::sampledBegin(const RenderPassDesc& desc,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& targets,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& resolves,
    const WebGpuNativeTexturePayload* depth,
    core::pmr::String& id)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!issuer_.isCurrent() || failure_ || pass_ >= sampled_->draws.size() + 2
        || state_ == State::Pass || state_ == State::Copied || state_ == State::Finished
        || targets.size() != 1 || resolves.size() != 1 || resolves[0] || depth
        || desc.hasDepthStencilAttachment || desc.colorAttachments.size() != 1
        || desc.layeredRendering || !desc.subpasses.empty() || !desc.subpassDependencies.empty())
        return rejectSampled(id);
    const auto index = pass_ < 2 ? pass_ : std::size_t(sampled_->draws[pass_ - 2].target);
    if (!textures_[index] || textures_[index].get() != targets[0]
        || textureStates_[index] != ResourceState::ColorAttachment)
        return rejectSampled(id);
    const auto color = pass_ < 2
        ? ConformanceClearFrame{sampled_->extent, pass_ ? sampled_->sourceB : sampled_->sourceA}
              .clearColor()
        : Color{0, 0, 0, 1};
    const auto& attachment = desc.colorAttachments[0];
    if (attachment.target != textureIds_[index] || attachment.format != targets[0]->rhiFormat
        || attachment.loadOp != LoadOp::Clear || attachment.storeOp != StoreOp::Store
        || attachment.hasResolveTarget || attachment.resolveTarget.isValid()
        || attachment.clearColor.r != color.r || attachment.clearColor.g != color.g
        || attachment.clearColor.b != color.b || attachment.clearColor.a != color.a)
        return rejectSampled(id);
    bindings_ = 0;
    drew_ = false;
    state_ = State::Pass;
    return true;
#else
    (void)desc;
    (void)targets;
    (void)resolves;
    (void)depth;
    return rejectSampled(id);
#endif
}

bool WebGpuConformanceClearFrame::bufferBarrier(
    const WebGpuNativeBufferPayload& buffer, const ResourceBarrierDesc& desc, core::pmr::String& id)
{
    if (storageMrt_) {
        if (!issuer_.isCurrent() || failure_ || state_ == State::Pass || computeActive_ || pass_ > 1
            || desc.before != ResourceState::ShaderRead || desc.after != ResourceState::ShaderRead
            || ++bufferBarriers_ > 2 || desc.sourceAccess != ResourceAccess::ShaderRead
            || desc.destinationAccess != ResourceAccess::ShaderRead
            || desc.destinationStages
                != (pass_ ? PipelineStage::FragmentShader : PipelineStage::ComputeShader)
            || semantics::validateResourceAccessScope(
                {desc.before, desc.sourceStages, desc.sourceAccess}))
            return rejectStorage(id);
        const auto* original = inputs_->buffer(buffer,
            ConformanceSampledInput::StorageValues,
            storageMrt_->values.data(),
            sizeof(storageMrt_->values));
        return original && original->frame == frameIndex_ && original->object->id() == desc.resource
            ? true
            : rejectStorage(id);
    }
    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ == State::Pass || pass_ < 2
        || pass_ >= sampled_->draws.size() + 2 || desc.before != ResourceState::ShaderRead
        || desc.after != ResourceState::ShaderRead
        || ++bufferBarriers_ > sampled_->draws.size() * 3)
        return rejectSampled(id);
    for (const auto& entry : inputs_->buffers_) {
        if (entry.native.get() != &buffer || entry.object->id() != desc.resource
            || !inputs_->buffer(buffer, entry.role))
            continue;
        if (entry.role == ConformanceSampledInput::Uniforms && entry.frame != frameIndex_)
            return rejectSampled(id);
        const auto stages = entry.role == ConformanceSampledInput::Uniforms
            ? PipelineStage::VertexShader | PipelineStage::FragmentShader
            : PipelineStage::VertexInput;
        const auto access = entry.role == ConformanceSampledInput::Uniforms
            ? ResourceAccess::ShaderRead
            : entry.role == ConformanceSampledInput::Vertices ? ResourceAccess::VertexAttributeRead
                                                              : ResourceAccess::IndexRead;
        if (desc.destinationStages != stages || desc.destinationAccess != access)
            return rejectSampled(id);
        if (semantics::validateResourceAccessScope(
                {desc.before, desc.sourceStages, desc.sourceAccess})
            || semantics::validateResourceAccessScope(
                {desc.after, desc.destinationStages, desc.destinationAccess}))
            return rejectSampled(id);
        return true;
    }
    return rejectSampled(id);
}

bool WebGpuConformanceClearFrame::bind(std::uint32_t bit, core::pmr::String& id)
{
    if ((!storageMrt_ && !sampled_) || !issuer_.isCurrent() || failure_ || state_ != State::Pass
        || (storageMrt_ ? pass_ != 1 : pass_ < 2) || drew_ || (bindings_ & bit))
        return rejectSampled(id);
    bindings_ |= bit;
    return true;
}

bool WebGpuConformanceClearFrame::pipeline(
    const WebGpuNativeRenderPipelinePayload& pipeline, core::pmr::String& id)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (storageMrt_)
        return &pipeline
                    == inputs_
                           ->nativePipelines_[image_->rhiFormat == TextureFormat::Rgba8Unorm ? 0
                                                                                             : 1]
                           .get()
                && !inputs_->diagnostic()
            ? bind(1, id)
            : rejectStorage(id);
    if (!sampled_ || pass_ < 2 || pass_ >= sampled_->draws.size() + 2 || inputs_->diagnostic())
        return rejectSampled(id);
    const auto target = std::size_t(sampled_->draws[pass_ - 2].target);
    const auto index = textures_[target]->rhiFormat == TextureFormat::Rgba8Unorm ? 0u : 1u;
    return &pipeline == inputs_->nativePipelines_[index].get() ? bind(1, id) : rejectSampled(id);
#else
    (void)pipeline;
    return rejectSampled(id);
#endif
}

bool WebGpuConformanceClearFrame::viewport(const ViewportState& v, core::pmr::String& id)
{
    return ConformanceSampledCommandContract::viewport(v, expected_.extent) ? bind(2, id)
                                                                            : rejectSampled(id);
}

bool WebGpuConformanceClearFrame::scissor(const ScissorState& s, core::pmr::String& id)
{
    return ConformanceSampledCommandContract::scissor(s, expected_.extent) ? bind(4, id)
                                                                           : rejectSampled(id);
}

bool WebGpuConformanceClearFrame::vertex(std::uint32_t slot,
    const WebGpuNativeBufferPayload& buffer,
    std::uint64_t offset,
    core::pmr::String& id)
{
    return sampled_ && slot == 3 && !offset
            && inputs_->buffer(buffer, ConformanceSampledInput::Vertices)
        ? bind(8, id)
        : rejectSampled(id);
}

bool WebGpuConformanceClearFrame::index(const WebGpuNativeBufferPayload& buffer,
    IndexFormat format,
    std::uint64_t offset,
    core::pmr::String& id)
{
    return sampled_ && pass_ >= 2 && pass_ < sampled_->draws.size() + 2
            && sampled_->draws[pass_ - 2].indexed && format == IndexFormat::Uint16 && !offset
            && inputs_->buffer(buffer, ConformanceSampledInput::Indices)
        ? bind(64, id)
        : rejectSampled(id);
}

bool WebGpuConformanceClearFrame::push(ShaderStage stages,
    std::uint32_t offset,
    std::uint32_t size,
    const void* bytes,
    core::pmr::String& id)
{
    return sampled_ && pass_ >= 2 && pass_ < sampled_->draws.size() + 2
            && ConformanceSampledCommandContract::push(
                sampled_->draws[pass_ - 2], stages, offset, size, bytes)
        ? bind(32, id)
        : rejectSampled(id);
}

bool WebGpuConformanceClearFrame::descriptors(const WebGpuNativeDescriptorGroupPayload* set,
    std::uint32_t index,
    std::uint32_t dynamicCount,
    core::pmr::String& id)
{
    if (storageMrt_)
        return storageDescriptors(set, index, dynamicCount, id);
    if (!sampled_ || pass_ < 2 || pass_ >= sampled_->draws.size() + 2 || !set || index
        || dynamicCount || set->tableBacked || set->bindings.size() != 5 || set->dynamicOffsetCount
        || !set->dynamicOffsetBindings.empty())
        return rejectSampled(id);
    std::array<const WebGpuNativeDescriptorBindingPayload*, 5> entries{};
    for (const auto& entry : set->bindings) {
        if (entry.write.binding >= entries.size() || entries[entry.write.binding]
            || entry.arrayElement)
            return rejectSampled(id);
        entries[entry.write.binding] = &entry;
    }
    const auto* uniform = entries[0];
    const auto* original = uniform && uniform->buffer ? inputs_->buffer(*uniform->buffer,
                                                            ConformanceSampledInput::Uniforms,
                                                            sampled_->uniforms.data(),
                                                            sizeof(sampled_->uniforms))
                                                      : nullptr;
    if (!uniform || !uniform->buffer || uniform->textureView || uniform->sampler
        || uniform->write.type != DescriptorType::UniformBuffer
        || uniform->write.bufferBindingRange.offsetBytes || !original
        || original->frame != frameIndex_)
        return rejectSampled(id);
    DescriptorBufferBindingRangeContract::Resolved range;
    if (DescriptorBufferBindingRangeContract::resolve(
            uniform->write.bufferBindingRange, uniform->buffer->sizeBytes, 0, range)
        || range.offsetBytes || range.sizeBytes != sizeof(sampled_->uniforms))
        return rejectSampled(id);
    const auto& draw = sampled_->draws[pass_ - 2];
    for (std::size_t t = 0; t < 2; ++t) {
        const auto source = std::size_t(t ? draw.sourceB : draw.sourceA);
        const auto* view = entries[1 + t * 2];
        const auto* sampler = entries[2 + t * 2];
        TextureViewDesc shape;
        shape.format = TextureFormat::Rgba8Unorm;
        shape.usage = TextureUsage::Sampled;
        shape.aspect = TextureAspect::Color;
        if (!textures_[source] || textureStates_[source] != ResourceState::ShaderRead || !view
            || !view->textureView || view->buffer || view->sampler
            || view->write.type != DescriptorType::SampledTexture
            || view->textureView->parentTexture != textures_[source]
            || !view->textureView->matchesDescription(shape) || !sampler || sampler->buffer
            || sampler->textureView || sampler->write.type != DescriptorType::Sampler
            || sampler->sampler != inputs_->nativeSampler_)
            return rejectSampled(id);
    }
    return bind(16, id);
}

bool WebGpuConformanceClearFrame::draw(bool indexed,
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::int32_t offset,
    std::uint32_t firstInstance,
    core::pmr::String& id)
{
    if (storageMrt_) {
        if (!issuer_.isCurrent() || failure_ || state_ != State::Pass || drew_ || pass_ != 1
            || indexed || count != 3 || instances != 1 || first || offset || firstInstance
            || bindings_ != 23)
            return rejectStorage(id);
        drew_ = true;
        return true;
    }
    if (!sampled_ || !issuer_.isCurrent() || failure_ || state_ != State::Pass || drew_ || pass_ < 2
        || pass_ >= sampled_->draws.size() + 2
        || !ConformanceSampledCommandContract::draw(sampled_->draws[pass_ - 2],
            indexed,
            count,
            instances,
            first,
            offset,
            firstInstance,
            bindings_))
        return rejectSampled(id);
    drew_ = true;
    return true;
}
} // namespace arp::rhi_webgpu
