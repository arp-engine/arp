#include "rhi/backends/vulkan/vulkan_conformance_clear.h"

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_conformance_sampled_inputs.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/gpu_device.h"

#include <chrono>
#include <cstring>

namespace arp::rhi_vulkan {

bool VulkanConformanceClearFrame::reject(core::pmr::String& diagnostic)
{
    failure_ = storageMrt_ ? "RHI-CONFORMANCE-STORAGE-MRT-COMMAND-LANGUAGE"
        : sampled_         ? "RHI-CONFORMANCE-SAMPLED-COMMAND-LANGUAGE"
                           : "RHI-CONFORMANCE-CLEAR-COMMAND-LANGUAGE";
    diagnostic = failure_;
    return false;
}

bool VulkanConformanceClearFrame::begin(const rhi::RenderPassDesc& desc,
    const core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>& targets,
    const VulkanRhiNativeResourcePayload* depth,
    core::pmr::String& diagnostic)
{
    if (storageMrt_)
        return storageBegin(desc, targets, depth, diagnostic);
    if (sampled_)
        return sampledBegin(desc, targets, depth, diagnostic);
    if (!issuer_.isCurrent() || failure_ || state_ != State::Color || targets.size() != 1
        || targets[0] != image_ || depth || desc.hasDepthStencilAttachment
        || desc.colorAttachments.size() != 1 || desc.layeredRendering || !desc.subpasses.empty())
        return reject(diagnostic);
    const auto& color = desc.colorAttachments[0];
    const auto expected = expected_.clearColor();
    const auto format = image_->backendVulkanImageFormat() == VK_FORMAT_R8G8B8A8_UNORM
        ? rhi::TextureFormat::Rgba8Unorm
        : rhi::TextureFormat::Bgra8Unorm;
    if (color.target != imageId_ || color.format != format || color.loadOp != rhi::LoadOp::Clear
        || color.storeOp != rhi::StoreOp::Store || color.clearColor.r != expected.r
        || color.clearColor.g != expected.g || color.clearColor.b != expected.b
        || color.clearColor.a != expected.a || color.hasResolveTarget
        || color.resolveTarget.isValid() || !desc.subpassDependencies.empty())
        return reject(diagnostic);
    state_ = State::Pass;
    return true;
}

bool VulkanConformanceClearFrame::end(core::pmr::String& diagnostic)
{
    if (!issuer_.isCurrent() || failure_ || state_ != State::Pass)
        return reject(diagnostic);
    if (storageMrt_) {
        if (pass_ != 1 || !drew_)
            return reject(diagnostic);
        ++pass_;
    }
    if (sampled_) {
        if ((pass_ < 2 && drew_) || (pass_ >= 2 && !drew_))
            return reject(diagnostic);
        ++pass_;
    }
    state_ = State::Stored;
    return true;
}

bool VulkanConformanceClearFrame::transition(VulkanRhiNativeCommandPayload& command,
    const VulkanRhiNativeResourcePayload& image,
    const rhi::ResourceBarrierDesc& desc,
    core::pmr::String& diagnostic)
{
    if (storageMrt_)
        return storageTransition(command, image, desc, diagnostic);
    if (sampled_)
        return sampledTransition(command, image, desc, diagnostic);
    const auto& range = desc.textureSubresources;
    if (!issuer_.isCurrent() || failure_ || image.backendVulkanSwapchainImageDiagnostic()
        || &image != image_.get() || desc.resource != imageId_
        || range.aspect != rhi::TextureAspect::Color || range.baseMipLevel || range.baseArrayLayer
        || range.mipLevelCount != 1 || range.arrayLayerCount != 1)
        return reject(diagnostic);
    if (state_ == State::Prepared && desc.before == image.backendVulkanSwapchainAcquiredState()
        && desc.after == rhi::ResourceState::ColorAttachment) {
        if (!command.recordValidatedOriginalImageBarrier(image, desc, diagnostic))
            return reject(diagnostic);
        state_ = State::Color;
        return true;
    }
    if (state_ != State::Stored || desc.before != rhi::ResourceState::ColorAttachment
        || desc.after != rhi::ResourceState::Present)
        return reject(diagnostic);
    return copyToPresent(command, image, desc, diagnostic);
}

bool VulkanConformanceClearFrame::copyToPresent(VulkanRhiNativeCommandPayload& command,
    const VulkanRhiNativeResourcePayload& image,
    const rhi::ResourceBarrierDesc& desc,
    core::pmr::String& diagnostic)
{
    const auto& range = desc.textureSubresources;
    if (!issuer_.isCurrent() || failure_ || state_ != State::Stored || &image != image_.get()
        || image.backendVulkanSwapchainImageDiagnostic() || desc.resource != imageId_
        || desc.before != rhi::ResourceState::ColorAttachment
        || desc.after != rhi::ResourceState::Present)
        return reject(diagnostic);
    auto copyBarrier = rhi::makeInferredResourceBarrier(
        imageId_, range, rhi::ResourceState::ColorAttachment, rhi::ResourceState::CopySrc);
    if (!command.recordValidatedOriginalImageBarrier(image, copyBarrier, diagnostic)
        || !command.flushPendingBarriers(diagnostic))
        return reject(diagnostic);
    rhi::TextureToBufferCopyDesc copy;
    copy.extent = {expected_.extent.width, expected_.extent.height, 1};
    copy.bytesPerRow = expected_.extent.width * 4;
    copy.rowsPerImage = expected_.extent.height;
    copy.textureSubresources = range;
    internalCopy_ = true;
    const auto copied = command.recordImageToBufferCopy(image, *staging_, copy, diagnostic);
    internalCopy_ = false;
    if (!copied)
        return reject(diagnostic);
    copyBarrier = rhi::makeInferredResourceBarrier(
        imageId_, range, rhi::ResourceState::CopySrc, rhi::ResourceState::Present);
    if (!command.recordValidatedOriginalImageBarrier(image, copyBarrier, diagnostic))
        return reject(diagnostic);
    state_ = State::Copied;
    return true;
}

const char* VulkanConformanceClearExecution::Owner::diagnostic() const noexcept
{
    if (const auto* id = device.diagnostic())
        return id;
    if (!loss)
        return "RHI-CONFORMANCE-CLEAR-NATIVE-LOSS-UNPREPARED";
    if (const auto* id = loss->diagnostic())
        return id;
    if (artifact)
        if (const auto* id = shader::GraphicsShaderModuleArtifact::validateOriginal(artifact))
            return id;
    if (computeArtifact)
        if (const auto* id = shader::SingleEntryShaderArtifact::validateOriginal(computeArtifact))
            return id;
    const auto native = runtime.lock();
    if (native && native->admissionDiagnostic())
        return native->admissionDiagnostic();
    return native && native->valid() && !native->syntheticRuntime()
        ? nullptr
        : "RHI-CONFORMANCE-CLEAR-NATIVE-OWNER-LOST";
}

VulkanConformanceClearExecution::VulkanConformanceClearExecution(Key,
    rhi::GpuDevice& device,
    VulkanRhiNativeResourceAllocator allocator,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    rhi::ConformanceClearExecutionRequest request,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : device_(&device), allocator_(std::move(allocator)), capabilityContract_(&capabilityContract),
      request_(request), quota_(std::move(quota)), owner_(core::makeShared<Owner>())
{
    owner_->device = device.originalDeviceLifetimeTicket();
    owner_->runtime = allocator_.runtime();
    const char* id = nullptr;
    if (auto loss = device.observeOriginalDeviceNativeLoss(id))
        owner_->loss.emplace(*loss);
}

const char* VulkanConformanceClearExecution::initialize(
    core::UniquePtr<rhi::ConformanceClearWorkload> workload)
{
    return initializeFrames(std::move(workload), {});
}

const char* VulkanConformanceClearExecution::initializeFrames(
    core::UniquePtr<rhi::ConformanceClearWorkload> workload,
    core::UniquePtr<rhi::ConformanceSampledWorkload> sampled,
    core::UniquePtr<rhi::ConformanceStorageMrtWorkload> storage)
{
    if (run_ || !frames_.empty())
        return "RHI-CONFORMANCE-ORIGINAL-ALREADY-STARTED";
    if (const auto* id = owner_->diagnostic())
        return id;
    const auto count = workload ? workload->frames().size()
        : sampled               ? sampled->frames().size()
        : storage               ? storage->frames().size()
                                : 0;
    if (!count || count > request_.observation.maxFrames)
        return "RHI-CONFORMANCE-ORIGINAL-FRAME-BUDGET";
    std::uint64_t bytes = 0, nativeBytes = 0;
    frames_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto expected = workload ? workload->frames()[i]
            : sampled
            ? rhi::ConformanceClearFrame{sampled->frames()[i].extent, sampled->frames()[i].expected}
            : rhi::ConformanceClearFrame{
                  storage->frames()[i].extent, storage->frames()[i].expectedColor0};
        const auto count = std::uint64_t(expected.extent.width) * expected.extent.height;
        const auto frameBytesPerPixel = storage ? 16u : 4u;
        if (expected.extent.width > UINT32_MAX / 8 || count > SIZE_MAX / frameBytesPerPixel
            || count > (request_.observation.maxOutputBytes - bytes) / frameBytesPerPixel)
            return "RHI-CONFORMANCE-ORIGINAL-OUTPUT-BUDGET";
        bytes += count * frameBytesPerPixel;
        auto frame = core::makeShared<VulkanConformanceClearFrame>(
            VulkanConformanceClearFrame::Key{}, expected);
        for (std::size_t role = 0; role < (storage ? 3u : 1u); ++role) {
            rhi::BufferDesc desc;
            desc.label = "OriginalConformanceExecution/1 owned readback";
            desc.sizeBytes = count * (role == 2 ? 8 : 4);
            desc.usage = rhi::ResourceUsage::CopyDst;
            desc.memory = rhi::MemoryUsage::GpuToCpu;
            desc.initialState = rhi::ResourceState::CopyDst;
            const char* id = nullptr;
            const auto allocation = allocator_.queryBufferAllocationBytes(desc, id);
            if (id || !allocation || allocation > request_.maxNativeReadbackBytes - nativeBytes)
                return id ? id : "RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET";
            nativeBytes += allocation;
            const auto native = allocator_.createBuffer(desc);
            if (!native.success || !native.payload || !native.payload->backendMappedData()
                || !native.allocation.success || native.allocation.sizeBytes > allocation)
                return "RHI-CONFORMANCE-CLEAR-NATIVE-ALLOCATION";
            if (role == 0)
                frame->staging_ = native.payload;
            else
                frame->auxiliaryStaging_[role - 1] = native.payload;
        }
        if (storage) {
            frame->storageMrt_ =
                core::makeUnique<rhi::ConformanceStorageMrtFrame>(storage->frames()[i]);
            frame->inputs_ = inputs_;
        }
        frame->issuer_ = owner_->lifetime.ticket();
        if (sampled) {
            frame->sampled_ = core::makeUnique<rhi::ConformanceSampledFrame>();
            *frame->sampled_ = sampled->frames()[i];
            frame->inputs_ = inputs_;
        }
        frames_.push_back(std::move(frame));
    }
    const auto epoch = owner_->lifetime.ticket();
    run_ = core::makeUnique<rhi::OriginalConformanceRun>(
        rhi::OriginalConformanceRun::ConstructionKey{},
        std::move(workload),
        std::move(sampled),
        quota_,
        epoch,
        epoch,
        epoch,
        owner_,
        +[](const void* owner) noexcept {
            return static_cast<const Owner*>(owner)->diagnostic();
        },
        std::move(storage));
    return nullptr;
}

const char* VulkanConformanceClearExecution::fail(const char* id) noexcept
{
    if (run_ && !run_->failure_)
        run_->failure_ = id;
    return id;
}

const char* VulkanConformanceClearExecution::diagnostic(const rhi::GpuDevice& device) const noexcept
{
    if (&device != device_)
        return "RHI-CONFORMANCE-CLEAR-DEVICE-MISMATCH";
    if (const auto* id = owner_->diagnostic())
        return id;
    if (quota_->limitBytes() != request_.observation.maxCpuMetadataBytes)
        return "RHI-CONFORMANCE-ORIGINAL-QUOTA";
    return run_ ? run_->failure_ : "RHI-CONFORMANCE-ORIGINAL-SOURCE-REQUIRED";
}

core::UniquePtr<rhi::CommandEncoder> VulkanConformanceClearExecution::prepare(
    const rhi::CommandEncoderDesc& desc, rhi::GpuTexture& target, const char*& id)
{
    id = owner_->diagnostic();
    if (!id)
        id = diagnostic(*device_);
    if (id)
        return {};
    if (inputs_ && (id = inputs_->diagnostic())) {
        fail(id);
        return {};
    }
    if (prepared_ || run_->next_ >= frames_.size()) {
        id = fail("RHI-CONFORMANCE-CLEAR-FRAME-ORDER");
        return {};
    }
    auto& frame = *frames_[run_->next_];
    auto image = vulkanRhiNativePayload(target);
    const auto& shape = target.desc();
    const auto format = image ? image->backendVulkanImageFormat() : VK_FORMAT_UNDEFINED;
    if (!image || image->backendVulkanSwapchainImageDiagnostic()
        || !image->backendVulkanSwapchainFrameSyncAvailable()
        || (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_UNORM)
        || shape.extent.width != frame.expected_.extent.width
        || shape.extent.height != frame.expected_.extent.height || shape.extent.depth != 1
        || shape.mipLevelCount != 1 || shape.arrayLayerCount != 1 || shape.sampleCount != 1
        || target.state() != image->backendVulkanSwapchainAcquiredState()) {
        id = fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-IMAGE");
        return {};
    }
    auto sync = image->backendVulkanSwapchainFrameSyncState();
    if (!sync || !sync->belongsTo(allocator_.runtime()) || sync->renderSubmitted()
        || sync->nativePresentAccepted() || !image->backendVulkanSwapchainFrameSerial()) {
        id = fail("RHI-CONFORMANCE-CLEAR-ACQUISITION");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        auto encoder = device_->createCommandEncoder(desc);
        auto command = encoder ? vulkanRhiCommandPayload(*encoder) : nullptr;
        if (!command || !command->impl_ || command->impl_->conformanceClear) {
            id = fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-ENCODER");
            return {};
        }
        frame.image_ = std::move(image);
        frame.imageId_ = target.id();
        if (frame.sampled_ || frame.storageMrt_) {
            constexpr auto output = std::size_t(rhi::ConformanceSampledResource::Output);
            frame.textures_[output] = frame.image_;
            frame.textureIds_[output] = frame.imageId_;
            frame.textureStates_[output] = target.state();
        }
        frame.serial_ = frame.image_->backendVulkanSwapchainFrameSerial();
        frame.sync_ = std::move(sync);
        command->impl_->conformanceClear = frames_[run_->next_];
        command->retainResource(frame.image_);
        command->retainResource(frame.staging_);
        for (const auto& auxiliary : frame.auxiliaryStaging_)
            if (auxiliary)
                command->retainResource(auxiliary);
        prepared_ = std::move(command);
        return encoder;
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        return {};
    }
}

rhi::QueueSubmitResult VulkanConformanceClearExecution::submit(
    const rhi::CommandBuffer& buffer, rhi::GpuFence* fence)
{
    if (const auto* id = owner_->diagnostic())
        return {false, core::toPmrString(id)};
    if (const auto* id = diagnostic(*device_))
        return {false, core::toPmrString(id)};
    if (!prepared_ || submitted_ || vulkanRhiCommandPayload(buffer) != prepared_
        || run_->next_ >= frames_.size())
        return {false, core::toPmrString(fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-COMMAND"))};
    auto& frame = *frames_[run_->next_];
    if (frame.failure_ || frame.state_ != VulkanConformanceClearFrame::State::Copied
        || frame.image_->backendVulkanSwapchainImageDiagnostic() || frame.sync_->renderSubmitted()
        || frame.sync_->nativePresentAccepted()
        || prepared_->impl_->swapchainFrameSyncState != frame.sync_
        || prepared_->observedFrameSerial() != frame.serial_)
        return {false,
            core::toPmrString(fail(
                frame.failure_ ? frame.failure_ : "RHI-CONFORMANCE-CLEAR-RECORDING-INCOMPLETE"))};
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(request_.completionTimeoutNanoseconds);
    if (timeout > std::chrono::steady_clock::time_point::max() - now)
        return {false, core::toPmrString(fail("RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET"))};
    const auto deadline = now + timeout;
    frame.submitAuthorized_ = true;
    auto result = device_->queue().submit(buffer, fence);
    frame.submitAuthorized_ = false;
    submitted_ = result.hasAcceptedWork();
    if (!result.success) {
        fail("RHI-CONFORMANCE-ORIGINAL-SUBMIT-FAILED");
        return result;
    }
    core::pmr::String message;

    const auto size = run_->frames_[run_->next_].pixels.size();
    if (!prepared_->waitSubmittedFenceUntil(deadline, message)
        || !frame.staging_->backendInvalidateMappedRead(0, size, message)) {
        fail("RHI-CONFORMANCE-CLEAR-READBACK-FAILED");
        return {false, core::toPmrString(run_->failure_), result.submittedCommandCount, true};
    }
    auto& output = run_->frames_[run_->next_];
    const auto* pixels = static_cast<const std::uint8_t*>(frame.staging_->backendMappedData());
    const auto bgra = frame.image_->backendVulkanImageFormat() == VK_FORMAT_B8G8R8A8_UNORM;
    bool matches = true;
    for (std::size_t i = 0; i < size; i += 4) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const auto source = bgra && channel < 3 ? 2 - channel : channel;
            const auto actual = pixels[i + source];
            output.pixels[i + channel] = actual;
            matches = matches && actual == frame.expected_.rgba8[channel];
        }
    }
    if (frame.storageMrt_)
        if (const auto* id = observeAuxiliary()) {
            fail(id);
            return {false, core::toPmrString(run_->failure_), result.submittedCommandCount, true};
        }
    output.canvas = frame.imageId_;
    output.serial = frame.serial_;
    output.observed = true;
    if (!matches) {
        fail("RHI-CONFORMANCE-ORIGINAL-OUTPUT-MISMATCH");
        return {false, core::toPmrString(run_->failure_), result.submittedCommandCount, true};
    }
    return result;
}

const char* VulkanConformanceClearExecution::finalizePresented() noexcept
{
    if (const auto* id = owner_->diagnostic())
        return id;
    if (const auto* id = diagnostic(*device_))
        return id;
    if (!prepared_ || !submitted_ || run_->next_ >= frames_.size())
        return fail("RHI-CONFORMANCE-CLEAR-PRESENT-ORDER");
    auto& frame = *frames_[run_->next_];
    auto& output = run_->frames_[run_->next_];
    if (!output.observed || !frame.sync_->graphicsCompleted()
        || !frame.sync_->nativePresentAccepted() || frame.sync_->submittedCommand() != prepared_
        || output.canvas != frame.imageId_ || output.serial != frame.serial_)
        return fail("RHI-CONFORMANCE-ORIGINAL-PRESENT-INCOMPLETE");
    output.presented = true;
    // Historical observations retain bytes/identity, not a retired surface.
    // The original command independently retains native owners through its fence.
    frame.image_.reset();
    frame.sync_.reset();
    frame.staging_.reset();
    for (auto& staging : frame.auxiliaryStaging_)
        staging.reset();
    for (auto& texture : frame.textures_)
        texture.reset();
    frame.inputs_.reset();
    if (inputs_)
        output.shaderArtifact = inputs_->artifact_;
    ++run_->next_;
    submitted_ = false;
    prepared_.reset();
    return nullptr;
}
} // namespace arp::rhi_vulkan
