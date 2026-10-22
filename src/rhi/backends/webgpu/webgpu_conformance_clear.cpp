#include "rhi/backends/webgpu/webgpu_conformance_clear.h"

#include "core/math/unsigned_range.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_conformance_sampled_inputs.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/gpu_device.h"

#include <chrono>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {
namespace {
// WebGPU spec: copyTextureToBuffer bytesPerRow is a multiple of 256 bytes.
constexpr std::uint64_t textureCopyRowAlignment = 256;
} // namespace

const char* WebGpuConformanceClearFrame::reject(const char* id) noexcept
{
    if (!failure_)
        failure_ = id;
    return failure_;
}

WebGpuConformanceClearFrame::~WebGpuConformanceClearFrame()
{
    for (auto& listener : listeners_)
        Receipt::release(listener.receipt);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (nativeQueue_)
        wgpuQueueRelease(static_cast<WGPUQueue>(nativeQueue_));
    if (nativeDevice_)
        wgpuDeviceRelease(static_cast<WGPUDevice>(nativeDevice_));
#endif
}

bool WebGpuConformanceClearFrame::transition(WebGpuNativeCommandPayload& command,
    const WebGpuNativeTexturePayload& image,
    const rhi::ResourceBarrierDesc& desc,
    core::pmr::String& diagnostic)
{
    if (storageMrt_)
        return storageTransition(command, image, desc, diagnostic);
    if (sampled_)
        return sampledTransition(command, image, desc, diagnostic);
    const auto fail = [&] {
        diagnostic = reject("RHI-CONFORMANCE-CLEAR-COMMAND-LANGUAGE");
        return false;
    };
    const auto& range = desc.textureSubresources;
    if (failure_ || !issuer_.isCurrent() || !canvas_ || !canvas_->surface_.isCurrent()
        || canvas_->discarded_ || !canvas_->issuer_.sameCurrentEpoch(issuer_)
        || &image != image_.get() || desc.resource != canvas_->image_
        || range.aspect != rhi::TextureAspect::Color || range.baseMipLevel || range.baseArrayLayer
        || range.mipLevelCount != 1 || range.arrayLayerCount != 1)
        return fail();
    const auto expected =
        rhi::makeInferredResourceBarrier(desc.resource, range, desc.before, desc.after);
    const auto accessMatches =
        [](rhi::ResourceState state, rhi::ResourceAccess actual, rhi::ResourceAccess expected) {
            if (state == rhi::ResourceState::ColorAttachment)
                return actual == rhi::ResourceAccess::ColorAttachmentWrite
                    || actual
                    == (rhi::ResourceAccess::ColorAttachmentRead
                        | rhi::ResourceAccess::ColorAttachmentWrite);
            return actual == expected;
        };
    if (desc.sourceStages != expected.sourceStages
        || desc.destinationStages != expected.destinationStages
        || !accessMatches(desc.before, desc.sourceAccess, expected.sourceAccess)
        || !accessMatches(desc.after, desc.destinationAccess, expected.destinationAccess))
        return fail();
    if (state_ == State::Prepared && desc.before == rhi::ResourceState::Undefined
        && desc.after == rhi::ResourceState::ColorAttachment) {
        state_ = State::Color;
        return true;
    }
    if (state_ != State::Stored || desc.before != rhi::ResourceState::ColorAttachment
        || desc.after != rhi::ResourceState::Present)
        return fail();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!command.impl_ || command.impl_->renderPass || command.impl_->computePass
        || command.impl_->finished || command.impl_->submitted)
        return fail();
    if (const auto* id = WebGpuNativeOriginalTextureReadbackLowering::emitFrozen(
            command.impl_->encoder, image, *readbacks_[0].staging, readbacks_[0].copy)) {
        diagnostic = reject(id);
        return false;
    }
    state_ = State::Copied;
    return true;
#else
    (void)command;
    return fail();
#endif
}

bool WebGpuConformanceClearFrame::begin(const rhi::RenderPassDesc& desc,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& colors,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& resolves,
    const WebGpuNativeTexturePayload* depth,
    core::pmr::String& diagnostic)
{
    if (storageMrt_)
        return storageBegin(desc, colors, resolves, depth, diagnostic);
    if (sampled_)
        return sampledBegin(desc, colors, resolves, depth, diagnostic);
    const auto fail = [&] {
        diagnostic = reject("RHI-CONFORMANCE-CLEAR-COMMAND-LANGUAGE");
        return false;
    };
    if (failure_ || !issuer_.isCurrent() || !canvas_ || !canvas_->surface_.isCurrent()
        || canvas_->discarded_ || state_ != State::Color || colors.size() != 1
        || colors[0] != image_.get() || resolves.size() != 1 || resolves[0] || depth
        || desc.colorAttachments.size() != 1 || desc.hasDepthStencilAttachment
        || desc.layeredRendering || !desc.subpasses.empty() || !desc.subpassDependencies.empty())
        return fail();
    const auto& color = desc.colorAttachments[0];
    const auto expected = expected_.clearColor();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (color.target != canvas_->image_ || color.format != image_->rhiFormat
        || color.loadOp != rhi::LoadOp::Clear || color.storeOp != rhi::StoreOp::Store
        || color.hasResolveTarget || color.resolveTarget.isValid()
        || color.clearColor.r != expected.r || color.clearColor.g != expected.g
        || color.clearColor.b != expected.b || color.clearColor.a != expected.a)
        return fail();
    state_ = State::Pass;
    return true;
#else
    (void)color;
    (void)expected;
    return fail();
#endif
}

bool WebGpuConformanceClearFrame::end(core::pmr::String& diagnostic)
{
    if ((storageMrt_ || (sampled_ && pass_ >= 2)) && !drew_)
        return rejectSampled(diagnostic);
    if (failure_ || !issuer_.isCurrent() || state_ != State::Pass) {
        diagnostic = reject("RHI-CONFORMANCE-CLEAR-COMMAND-LANGUAGE");
        return false;
    }
    state_ = State::Stored;
    if (sampled_ || storageMrt_)
        ++pass_;
    return true;
}

bool WebGpuConformanceClearFrame::finish(core::pmr::String& diagnostic)
{
    if (failure_ || !issuer_.isCurrent() || state_ != State::Copied) {
        diagnostic = reject("RHI-CONFORMANCE-CLEAR-RECORDING-INCOMPLETE");
        return false;
    }
    state_ = State::Finished;
    return true;
}

const char* WebGpuConformanceClearFrame::arm(
    const std::shared_ptr<WebGpuConformanceClearFrame>& self,
    const std::shared_ptr<WebGpuNativeCommandPayload>& command)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    nativeDevice_ = emscripten_webgpu_get_device();
    if (!nativeDevice_)
        return reject("RHI-CONFORMANCE-CLEAR-ORIGINAL-DEVICE");
    nativeQueue_ = wgpuDeviceGetQueue(static_cast<WGPUDevice>(nativeDevice_));
    if (const auto* id = Receipt::inspect(nativeDevice_, nativeQueue_))
        return reject(id);
    for (std::size_t i = 0; i < listeners_.size(); ++i) {
        auto& listener = listeners_[i];
        listener.owner = this;
        listener.index = i;
        auto prepared = Receipt::prepare(nativeDevice_,
            nativeQueue_,
            i ? Receipt::Kind::Scope : Receipt::Kind::Queue,
            &listener,
            &completed);
        if (!prepared)
            return reject("RHI-CONFORMANCE-CLEAR-NATIVE-CALLBACK-PREPARE");
        listener.receipt = prepared.release();
    }
    // All callback owners exist before any native subscription/invocation.
    callbackLease_ = self;
    retainedCommand_ = command;
    lossRegistration_ = prepareWebGpuNativeDeviceLossRegistration(nativeDevice_, this, &lost);
    if (!lossRegistration_)
        return reject("RHI-CONFORMANCE-CLEAR-NATIVE-LOSS-BINDING");
    if (!Receipt::claimScopeTransaction(nativeDevice_, this))
        return reject("RHI-CONFORMANCE-CLEAR-SCOPE-TRANSACTION-BUSY");
    scopeClaimed_ = true;
    if (!Receipt::pushScopes(nativeDevice_))
        return reject("RHI-CONFORMANCE-CLEAR-NATIVE-SCOPE-PUSH");
    scopesOpen_ = true;
    return nullptr;
#else
    (void)self;
    (void)command;
    return reject("RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE");
#endif
}

void WebGpuConformanceClearFrame::closeScopes() noexcept
{
    if (!scopesOpen_)
        return;
    scopesOpen_ = false;
    for (std::size_t i = 1; i < listeners_.size(); ++i) {
        auto& listener = listeners_[i];
        if (!Receipt::bind(listener.receipt)) {
            // The adapter suppressed userdata; this is not GPU completion.
            listener.drained = true;
            reject("RHI-CONFORMANCE-CLEAR-NATIVE-SCOPE-BIND");
        }
    }
    if (!Receipt::releaseScopeTransaction(nativeDevice_, this))
        reject("RHI-CONFORMANCE-CLEAR-SCOPE-TRANSACTION-IDENTITY");
    else
        scopeClaimed_ = false;
}

void WebGpuConformanceClearFrame::cancel() noexcept
{
    const auto lease = callbackLease_;
    terminal_ = true;
    closeScopes();
    if (scopeClaimed_ && (Receipt::releaseScopeTransaction(nativeDevice_, this) || physicalLoss_))
        scopeClaimed_ = false;
    for (auto& listener : listeners_)
        if (listener.receipt && Receipt::prepared(listener.receipt)) {
            Receipt::release(listener.receipt);
            listener.receipt = 0;
            listener.drained = true;
        }
    if (!submitted_) {
        Receipt::release(listeners_[0].receipt);
        listeners_[0].receipt = 0;
        listeners_[0].drained = true;
    }
    for (auto& readback : readbacks_)
        if (readback.map.phase() == WebGpuNativeBufferMapReceipt::Phase::Pending) {
            if (physicalLoss_)
                (void)readback.map.destroyAndDrainAfterNativeLoss();
            else
                (void)readback.map.abortAndDrain();
        }
    releaseIfDrained();
}

void WebGpuConformanceClearFrame::releaseIfDrained() noexcept
{
    if (!callbackLease_ || scopesOpen_ || scopeClaimed_
        || (submitted_ && !queueCompleted_ && !physicalLoss_))
        return;
    for (const auto& readback : readbacks_)
        if (readback.map.phase() == WebGpuNativeBufferMapReceipt::Phase::Pending
            || readback.map.invocationUncertain())
            return;
    if (submitted_ && !terminal_ && !failure_ && !outputReady_)
        return;
    for (const auto& listener : listeners_)
        if (!listener.drained)
            return;
    if (lossRegistration_) {
        if (!detachWebGpuNativeDeviceLossRegistration(lossRegistration_))
            return;
        releaseWebGpuNativeDeviceLossRegistration(lossRegistration_);
        lossRegistration_ = 0;
    }
    // All foreign userdata has genuinely drained or been suppressed. Historical
    // bytes and acquisition identity remain; accepted native owners can retire.
    retainedCommand_.reset();
    callbackLease_.reset();
}

void WebGpuConformanceClearFrame::completed(void* userdata, Receipt::Outcome outcome) noexcept
{
    auto& listener = *static_cast<Listener*>(userdata);
    auto& frame = *listener.owner;
    const auto lease = frame.callbackLease_;
    if (!lease || listener.drained
        || !Receipt::matchesDelivery(listener.receipt,
            frame.nativeDevice_,
            frame.nativeQueue_,
            listener.index ? Receipt::Kind::Scope : Receipt::Kind::Queue,
            &listener,
            &completed,
            outcome)) {
        frame.reject("RHI-CONFORMANCE-CLEAR-NATIVE-CALLBACK-IDENTITY");
        return;
    }
    listener.drained = true;
    if (!listener.index && outcome == Receipt::Outcome::Success) {
        frame.queueCompleted_ = true;
        if (frame.canvas_)
            frame.canvas_->retirementSafe_ = true;
    }
    if (outcome != Receipt::Outcome::Success)
        frame.reject("RHI-CONFORMANCE-CLEAR-NATIVE-CALLBACK-ERROR");
    bool all = true;
    for (const auto& item : frame.listeners_)
        all = all && item.drained;
    if (all && frame.submitted_ && frame.queueCompleted_ && !frame.failure_ && !frame.terminal_)
        for (std::size_t i = 0; i < frame.readbackCount(); ++i)
            if (const auto* id = frame.readbacks_[i].map.start()) {
                frame.reject(id);
                break;
            }
    if (frame.failure_ || frame.terminal_)
        frame.releaseIfDrained();
}

void WebGpuConformanceClearFrame::mapped(
    void* userdata, WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept
{
    auto& readback = *static_cast<Readback*>(userdata);
    auto& frame = *readback.owner;
    const auto lease = frame.callbackLease_;
    if (!lease)
        return;
    if (outcome != WebGpuNativeBufferMapReceipt::Outcome::Success)
        frame.reject("RHI-CONFORMANCE-CLEAR-NATIVE-MAP-FAILED");
    else if (const auto* id =
                 readback.map.readAndUnmap(readback.padded.data(), readback.padded.size()))
        frame.reject(id);
    else if (!frame.terminal_ && !frame.failure_ && frame.queueCompleted_) {
        readback.ready = true;
        frame.outputReady_ = true;
        for (std::size_t i = 0; i < frame.readbackCount(); ++i)
            frame.outputReady_ = frame.outputReady_ && frame.readbacks_[i].ready;
    }
    frame.releaseIfDrained();
}

void WebGpuConformanceClearFrame::lost(void* userdata) noexcept
{
    auto& frame = *static_cast<WebGpuConformanceClearFrame*>(userdata);
    const auto lease = frame.callbackLease_;
    frame.physicalLoss_ = true;
    if (frame.canvas_)
        frame.canvas_->retirementSafe_ = true;
    frame.reject("RHI-CONFORMANCE-CLEAR-NATIVE-DEVICE-LOST");
    releaseWebGpuNativeDeviceLossRegistration(frame.lossRegistration_);
    frame.lossRegistration_ = 0;
    for (auto& listener : frame.listeners_) {
        Receipt::release(listener.receipt);
        listener.receipt = 0;
        listener.drained = true;
    }
    frame.scopesOpen_ = false;
    frame.cancel();
}

const char* WebGpuConformanceClearExecution::Owner::diagnostic() const noexcept
{
    if (const auto* id = device.diagnostic())
        return id;
    if (!loss)
        return "RHI-CONFORMANCE-CLEAR-NATIVE-LOSS-UNPREPARED";
    if (const auto* id = loss->diagnostic())
        return id;
    if (inputs)
        if (const auto* id = inputs->sourceDiagnostic())
            return id;
    return artifact ? shader::GraphicsShaderModuleArtifact::validateOriginal(artifact) : nullptr;
}

WebGpuConformanceClearExecution::WebGpuConformanceClearExecution(Key,
    rhi::GpuDevice& device,
    const rhi::semantics::NativeBackendCapabilityContract& capabilities,
    rhi::ConformanceClearExecutionRequest request,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : device_(&device), capabilityContract_(&capabilities), request_(request),
      quota_(std::move(quota)), owner_(core::makeShared<Owner>())
{
    owner_->device = device.originalDeviceLifetimeTicket();
    const char* id = nullptr;
    if (auto loss = device.observeOriginalDeviceNativeLoss(id))
        owner_->loss.emplace(*loss);
}

WebGpuConformanceClearExecution::~WebGpuConformanceClearExecution()
{
    owner_->epoch.advance();
    for (auto& frame : frames_)
        frame->cancel();
}

const char* WebGpuConformanceClearExecution::initialize(
    core::UniquePtr<rhi::ConformanceClearWorkload> workload)
{
    return initializeFrames(std::move(workload), {});
}

const char* WebGpuConformanceClearExecution::initializeFrames(
    core::UniquePtr<rhi::ConformanceClearWorkload> workload,
    core::UniquePtr<rhi::ConformanceSampledWorkload> sampled,
    core::UniquePtr<rhi::ConformanceStorageMrtWorkload> storage)
{
    if (const auto* id = owner_->diagnostic())
        return id;
    const auto count = workload ? workload->frames().size()
        : sampled               ? sampled->frames().size()
        : storage               ? storage->frames().size()
                                : 0;
    if (!count || count > request_.observation.maxFrames)
        return "RHI-CONFORMANCE-ORIGINAL-FRAME-BUDGET";
    std::uint64_t outputBytes = 0, nativeBytes = 0;
    frames_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto expected = workload ? workload->frames()[i]
            : sampled
            ? rhi::ConformanceClearFrame{sampled->frames()[i].extent, sampled->frames()[i].expected}
            : rhi::ConformanceClearFrame{
                  storage->frames()[i].extent, storage->frames()[i].expectedColor0};
        auto frame = core::makeShared<WebGpuConformanceClearFrame>(
            WebGpuConformanceClearFrame::Key{}, expected);
        if (storage)
            frame->storageMrt_ = storage->frames()[i];
        for (std::size_t role = 0; role < frame->readbackCount(); ++role) {
            const auto row = std::uint64_t(expected.extent.width) * (role == 2 ? 8 : 4);
            std::uint64_t pitch = 0;
            if (!core::roundUpUnsigned(row, textureCopyRowAlignment, pitch) || pitch > UINT32_MAX
                || !core::canMultiplyUnsigned(pitch, std::uint64_t(expected.extent.height)))
                return "RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET";
            const auto size = pitch * expected.extent.height;
            const auto bytes = row * expected.extent.height;
            if (size > SIZE_MAX || size > request_.maxNativeReadbackBytes - nativeBytes
                || bytes > request_.observation.maxOutputBytes - outputBytes)
                return "RHI-CONFORMANCE-ORIGINAL-OUTPUT-BUDGET";
            nativeBytes += size;
            outputBytes += bytes;
            frame->readbacks_[role].padded.resize(std::size_t(size));
            frame->readbacks_[role].owner = frame.get();
        }
        frame->issuer_ = owner_->epoch.ticket();
        if (sampled) {
            frame->frameIndex_ = i;
            // Construct nested PMR owners with core's resource before assignment.
            // Copy construction would select the ambient std default resource.
            frame->sampled_.emplace();
            *frame->sampled_ = sampled->frames()[i];
            frame->inputs_ = inputs_;
            frame->textureStates_.fill(rhi::ResourceState::Undefined);
        }
        if (storage) {
            frame->frameIndex_ = i;
            frame->inputs_ = inputs_;
            frame->textureStates_.fill(rhi::ResourceState::Undefined);
        }
        frames_.push_back(std::move(frame));
    }
    const auto epoch = owner_->epoch.ticket();
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

const char* WebGpuConformanceClearExecution::fail(const char* id) noexcept
{
    if (run_ && !run_->failure_)
        run_->failure_ = id;
    return id;
}

const char* WebGpuConformanceClearExecution::diagnostic(const rhi::GpuDevice& device) const noexcept
{
    if (&device != device_)
        return "RHI-CONFORMANCE-CLEAR-DEVICE-MISMATCH";
    if (const auto* id = owner_->diagnostic())
        return id;
    if (!quota_ || quota_->limitBytes() != request_.observation.maxCpuMetadataBytes)
        return "RHI-CONFORMANCE-ORIGINAL-QUOTA";
    return run_ ? run_->failure_ : "RHI-CONFORMANCE-ORIGINAL-SOURCE-REQUIRED";
}

const char* WebGpuConformanceClearExecution::prepareReadback(WebGpuConformanceClearFrame& frame,
    WebGpuConformanceClearFrame::Readback& readback,
    const rhi::TextureDesc& shape,
    rhi::GpuResourceId resource)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const char* id = nullptr;
    rhi::ReadbackTextureDesc read;
    read.sourceResource = resource;
    read.destinationReadbackBuffer = rhi::allocateGpuResourceId();
    read.sizeBytes = readback.padded.size();
    read.extent = shape.extent;
    read.bytesPerRow = std::uint32_t(read.sizeBytes / read.extent.height);
    read.rowsPerImage = read.extent.height;
    read.sourceSubresources.aspect = rhi::TextureAspect::Color;
    read.sourceSubresources.baseMipLevel = 0;
    read.sourceSubresources.mipLevelCount = 1;
    read.sourceSubresources.baseArrayLayer = 0;
    read.sourceSubresources.arrayLayerCount = 1;
    const auto metadata = request_.observation.maxCpuMetadataBytes;
    auto snapshot = rhi::TextureReadbackInputSnapshot::prepareOriginal(
        read, {1, true, 1, 1, metadata, metadata}, quota_, quota_, id);
    if (!snapshot) {
        return id;
    }
    auto layout = rhi::TextureReadbackLayout::prepareOriginal(
        snapshot, shape, {1, true, 1, read.sizeBytes, metadata}, quota_, id);
    if (!layout
        || (id = rhi::semantics::TextureReadbackCopyLowering::lowerOriginal(
                rhi::Backend::WebGPU, 1, true, layout, 0, layout->mipCopies()[0], readback.copy))) {
        return id;
    }
    auto staging = core::makeShared<WebGpuNativeBufferPayload>(rhi::ResourceState::CopyDst);
    auto buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer.label = webGpuNativeStringView("WebGpuSharedConformance/1 owned readback");
    buffer.size = read.sizeBytes;
    buffer.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    staging->buffer = wgpuDeviceCreateBuffer(static_cast<WGPUDevice>(frame.nativeDevice_), &buffer);
    staging->ownerDevice = static_cast<WGPUDevice>(frame.nativeDevice_);
    staging->sizeBytes = read.sizeBytes;
    readback.staging = std::move(staging);
    return readback.map.prepareCold(readback.staging->buffer,
        readback.padded.size(),
        &readback,
        &WebGpuConformanceClearFrame::mapped);
#else
    (void)frame;
    (void)readback;
    (void)shape;
    (void)resource;
    return "RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE";
#endif
}

core::UniquePtr<rhi::CommandEncoder> WebGpuConformanceClearExecution::prepare(
    const rhi::CommandEncoderDesc& desc, rhi::GpuTexture& target, const char*& id)
{
    if ((id = diagnostic(*device_)))
        return {};
    if (prepared_ || run_->next_ >= frames_.size()) {
        id = fail("RHI-CONFORMANCE-CLEAR-FRAME-ORDER");
        return {};
    }
    auto& frame = *frames_[run_->next_];
    if (inputs_ && (id = inputs_->diagnostic())) {
        fail(id);
        return {};
    }
    const auto image = webGpuNativePayloadFor(target);
    const auto canvas = image ? image->originalCanvas_ : nullptr;
    const auto& shape = target.desc();
    if (!canvas || !canvas->surface_.isCurrent() || canvas->claimed_ || canvas->discarded_
        || canvas->presented_ || canvas->image_ != target.id() || !canvas->serial_
        || shape.extent.width != frame.expected_.extent.width
        || shape.extent.height != frame.expected_.extent.height || shape.extent.depth != 1
        || shape.mipLevelCount != 1 || shape.arrayLayerCount != 1 || shape.sampleCount != 1
        || (shape.format != rhi::TextureFormat::Rgba8Unorm
            && shape.format != rhi::TextureFormat::Bgra8Unorm)
        || target.state() != rhi::ResourceState::Undefined) {
        id = fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-IMAGE");
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        frame.image_ = image;
        frame.canvas_ = canvas;
        if (inputs_) {
            frame.textures_[4] = image;
            frame.textureIds_[4] = target.id();
        }
        if ((id = frame.arm(frames_[run_->next_], {}))) {
            frame.cancel();
            fail(id);
            return {};
        }
        auto encoder = device_->createCommandEncoder(desc);
        auto native = encoder ? webGpuNativePayloadFor(*encoder) : nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!native || !native->impl_ || native->impl_->conformanceClear || !native->impl_->encoder
            || native->impl_->device != frame.nativeDevice_
            || image->ownerDevice != frame.nativeDevice_ || !image->browserSurfaceTexture
            || wgpuTextureGetWidth(image->texture) != shape.extent.width
            || wgpuTextureGetHeight(image->texture) != shape.extent.height
            || wgpuTextureGetDepthOrArrayLayers(image->texture) != 1
            || wgpuTextureGetMipLevelCount(image->texture) != 1
            || wgpuTextureGetSampleCount(image->texture) != 1
            || wgpuTextureGetFormat(image->texture) != nativeTextureFormat(shape.format)
            || (wgpuTextureGetUsage(image->texture)
                   & (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc))
                != (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc)) {
            id = fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-ENCODER");
            frame.cancel();
            return {};
        }
        for (std::size_t role = 0; role < frame.readbackCount(); ++role) {
            const auto slot = role == 0 ? 4u : role - 1;
            if (!frame.textures_[slot] && role) {
                id = fail("RHI-CONFORMANCE-STORAGE-MRT-ORIGINAL-TEXTURE-INCOMPLETE");
                frame.cancel();
                return {};
            }
            auto readShape = shape;
            if (role) {
                readShape.format =
                    role == 2 ? rhi::TextureFormat::Rgba16Float : rhi::TextureFormat::Rgba8Unorm;
                readShape.usage = role == 2
                    ? rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled
                        | rhi::TextureUsage::CopySrc
                    : rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
            }
            id = prepareReadback(frame,
                frame.readbacks_[role],
                readShape,
                role ? frame.textureIds_[slot] : target.id());
            if (id) {
                fail(id);
                frame.cancel();
                return {};
            }
        }
        canvas->claimed_ = true;
        canvas->issuer_ = owner_->epoch.ticket();
        native->impl_->conformanceClear = frames_[run_->next_];
        frame.retainedCommand_ = native;
        prepared_ = native;
        return encoder;
#else
        (void)native;
        id = fail("RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE");
        frame.cancel();
        return {};
#endif
    } catch (const std::bad_alloc&) {
        id = fail("RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION");
        frame.cancel();
        return {};
    }
}

rhi::QueueSubmitResult WebGpuConformanceClearExecution::submit(
    const rhi::CommandBuffer& buffer, rhi::GpuFence* fence)
{
    using Receipt = WebGpuNativeCompletionReceipt;
    if (const auto* id = diagnostic(*device_))
        return {false, core::toPmrString(id)};
    if (!prepared_ || run_->next_ >= frames_.size() || webGpuNativePayloadFor(buffer) != prepared_
        || fence)
        return {false, core::toPmrString(fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-COMMAND"))};
    auto& frame = *frames_[run_->next_];
    if (frame.failure_ || frame.submitted_
        || frame.state_ != WebGpuConformanceClearFrame::State::Finished
        || !frame.canvas_->surface_.isCurrent() || frame.canvas_->discarded_)
        return {false,
            core::toPmrString(fail(
                frame.failure_ ? frame.failure_ : "RHI-CONFORMANCE-CLEAR-RECORDING-INCOMPLETE"))};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!prepared_->impl_ || !prepared_->impl_->finished || !prepared_->impl_->commandBuffer
        || prepared_->impl_->submitted || frame.retainedCommand_ != prepared_)
        return {false, core::toPmrString(fail("RHI-CONFORMANCE-CLEAR-ORIGINAL-COMMAND"))};
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(request_.completionTimeoutNanoseconds);
    if (timeout > std::chrono::steady_clock::time_point::max() - now)
        return {false, core::toPmrString(fail("RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET"))};
    const auto deadline = now + timeout;
    // Publish accepted-work retention before entering a potentially throwing
    // foreign queue call. No retry or rollback is possible after this point.
    frame.submitted_ = true;
    frame.canvas_->submitted_ = true;
    prepared_->impl_->submitted = true;
    markWebGpuRhiCommandBufferSubmitted(buffer);
    const auto invoked = Receipt::invoke(frame.nativeQueue_, prepared_->impl_->commandBuffer);
    if (!rhi::acceptRecordedCommandOwnership(buffer))
        frame.reject("RHI-CONFORMANCE-CLEAR-RECORDED-OWNERSHIP");
    if (!invoked)
        frame.reject("RHI-CONFORMANCE-CLEAR-NATIVE-INVOKE-UNCERTAIN");
    if (!Receipt::bind(frame.listeners_[0].receipt)) {
        frame.listeners_[0].drained = true;
        frame.reject("RHI-CONFORMANCE-CLEAR-NATIVE-QUEUE-BIND");
    }
    frame.closeScopes();
    while (!frame.outputReady_ && !frame.failure_ && std::chrono::steady_clock::now() < deadline)
        emscripten_sleep(0);
    if (!frame.outputReady_) {
        fail(frame.failure_ ? frame.failure_ : "RHI-CONFORMANCE-CLEAR-NATIVE-DEADLINE");
        frame.cancel();
        return {false, core::toPmrString(run_->failure_), 1, true};
    }
    auto& output = run_->frames_[run_->next_];
    const auto pitch = frame.readbacks_[0].copy.original.bytesPerRow;
    const bool bgra = frame.image_->rhiFormat == rhi::TextureFormat::Bgra8Unorm;
    bool matches = true;
    for (std::size_t y = 0; y < output.extent.height; ++y)
        for (std::size_t x = 0; x < output.extent.width; ++x)
            for (std::size_t c = 0; c < 4; ++c) {
                const auto channel = bgra && c < 3 ? 2 - c : c;
                const auto value = frame.readbacks_[0].padded[y * pitch + x * 4 + channel];
                output.pixels[(y * output.extent.width + x) * 4 + c] = value;
                matches = matches && value == frame.expected_.rgba8[c];
            }
    if (frame.storageMrt_)
        if (const auto* error = observeAuxiliary())
            return {false, core::toPmrString(fail(error)), 1, true};
    output.canvas = frame.canvas_->image_;
    output.serial = frame.canvas_->serial_;
    output.observed = true;
    if (inputs_)
        output.shaderArtifact = inputs_->artifact_;
    if (!matches)
        return {
            false, core::toPmrString(fail("RHI-CONFORMANCE-ORIGINAL-OUTPUT-MISMATCH")), 1, true};
    frame.canvas_->observed_ = true;
    return {true, {}, 1, true};
#else
    return {false, core::toPmrString(fail("RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE"))};
#endif
}

const char* WebGpuConformanceClearExecution::finalizePresented() noexcept
{
    if (const auto* id = diagnostic(*device_))
        return id;
    if (!prepared_ || run_->next_ >= frames_.size())
        return fail("RHI-CONFORMANCE-CLEAR-PRESENT-ORDER");
    auto& frame = *frames_[run_->next_];
    auto& output = run_->frames_[run_->next_];
    if (!frame.outputReady_ || frame.callbackLease_ || !frame.readbacks_[0].map.nativeUnmapProven()
        || !frame.canvas_->presented_ || !frame.canvas_->observed_ || !output.observed
        || output.canvas != frame.canvas_->image_ || output.serial != frame.canvas_->serial_)
        return fail("RHI-CONFORMANCE-ORIGINAL-PRESENT-INCOMPLETE");
    for (std::size_t i = 0; i < frame.readbackCount(); ++i)
        if (!frame.readbacks_[i].ready || !frame.readbacks_[i].map.nativeUnmapProven())
            return fail("RHI-CONFORMANCE-ORIGINAL-PRESENT-INCOMPLETE");
    output.presented = true;
    for (auto& readback : frame.readbacks_) {
        readback.staging.reset();
        readback.copy = {};
    }
    frame.image_.reset();
    frame.canvas_.reset();
    prepared_.reset();
    ++run_->next_;
    return nullptr;
}
} // namespace arp::rhi_webgpu
