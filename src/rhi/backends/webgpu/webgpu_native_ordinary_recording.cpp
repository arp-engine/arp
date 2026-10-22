#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"

#include "core/text/utf8.h"
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_original_texture_upload_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"

namespace arp::rhi_webgpu {
WebGpuOrdinaryRecording::~WebGpuOrdinaryRecording()
{
    if (originalTransaction_)
        originalTransaction_->abandon();
    if (originalTextureTransaction_)
        originalTextureTransaction_->abandon();
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepare(
    std::shared_ptr<const WebGpuOrdinaryScalarComputeRange> range,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOrdinaryComputeRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)range;
    (void)owner;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!range || !range->hasQuotaBoundSharedOwner(range)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER";
        return {};
    }
    if ((diagnostic = range->validate()))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    const auto& groups = *range->validation_->groups_;
    if (groups.groupCount() > budget.maxGroups) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-GROUP-CAPACITY";
        return {};
    }
    if (groups.pipeline_->contract().nativeDevice() != owner->nativeDevice()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate(range, {}, owner, std::move(signal), diagnostic);
        if (!result)
            return {};
        auto& command = result->command_;
        result->pushScopes();
        if (!WebGpuNativeCommandPreparation::open(
                *command, owner->nativeDevice(), "RHI WebGPU cold scalar Compute", diagnostic))
            result->failure_ = diagnostic;
        else {
            auto descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            descriptor.label = webGpuNativeStringView("RHI WebGPU cold ordinary Compute pass");
            command->impl_->computePass =
                wgpuCommandEncoderBeginComputePass(command->impl_->encoder, &descriptor);
            if (!command->impl_->computePass)
                result->failure_ = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-PASS";
        }
        result->popScopes(result, 0u);
        diagnostic = nullptr;
        return result; // Pending until all actual native callbacks succeed.
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepare(
    std::shared_ptr<const WebGpuOrdinaryGraphicsAttachments> attachments,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOrdinaryGraphicsRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)attachments;
    (void)owner;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!attachments || !attachments->hasQuotaBoundSharedOwner(attachments)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ATTACHMENT-OWNER";
        return {};
    }
    if ((diagnostic = attachments->validate()))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    const auto& groups = *attachments->range()->validation_->groups_;
    if (groups.groupCount() > budget.maxGroups) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-GROUP-CAPACITY";
        return {};
    }
    if (groups.pipeline_->contract().nativeDevice() != owner->nativeDevice()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({}, attachments, owner, std::move(signal), diagnostic);
        if (!result)
            return {};
        auto& command = result->command_;
        result->pushScopes();
        if (!WebGpuNativeCommandPreparation::open(
                *command, owner->nativeDevice(), "RHI WebGPU cold triangle Graphics", diagnostic))
            result->failure_ = diagnostic;
        else {
            auto color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = attachments->target().view()->view;
            color.resolveTarget = nullptr;
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Store;
            const auto& clear = attachments->clear();
            color.clearValue = {clear.r, clear.g, clear.b, clear.a};
            WGPURenderPassMaxDrawCount drawLimit{};
            drawLimit.chain.next = nullptr;
            drawLimit.chain.sType = WGPUSType_RenderPassMaxDrawCount;
            drawLimit.maxDrawCount = 1u; // Closed language has exactly one draw.
            auto descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            descriptor.nextInChain = &drawLimit.chain;
            descriptor.label = webGpuNativeStringView("RHI WebGPU cold ordinary Graphics pass");
            descriptor.colorAttachmentCount = 1u;
            descriptor.colorAttachments = &color;
            descriptor.depthStencilAttachment = nullptr;
            descriptor.occlusionQuerySet = nullptr;
            descriptor.timestampWrites = nullptr;
            command->impl_->renderPass =
                wgpuCommandEncoderBeginRenderPass(command->impl_->encoder, &descriptor);
            if (!command->impl_->renderPass)
                result->failure_ = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-PASS";
        }
        result->popScopes(result, 0u);
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalComputeInputs(
    const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
    const std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>& range) noexcept
{
    if (const auto* id = WebGpuOriginalScalarAccessValidation::compute(states, range, 3u))
        return id;
    using K = rhi::CommandUsageKind;
    const auto& usage = *states->inputs()->scalars()->usage();
    const auto& graph = *usage.projection();
    const std::array<K, 6> language{K::BeginComputePass,
        K::BindComputePipeline,
        K::BindComputeDescriptor,
        K::Dispatch,
        K::EndComputePass,
        K::Finish};
    const auto label = states->inputs()->scalars()->arguments()->text(0u);
    if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
        return "RHI-WEBGPU-ORIGINAL-COMPUTE-LABEL-ENCODING";
    if (usage.commands().size() != language.size())
        return "RHI-WEBGPU-ORIGINAL-COMPUTE-LANGUAGE";
    for (std::size_t i = 0u; i < language.size(); ++i)
        if (usage.commands()[i].kind != language[i])
            return "RHI-WEBGPU-ORIGINAL-COMPUTE-LANGUAGE";
    if (graph.transfer() || graph.resources() || graph.pipelines().size() != 1u
        || graph.physicalGroups().size() != 1u || graph.references().size() != 2u
        || states->sourceReferences() != 1u || states->states().size() != 1u
        || range->originalGroups()->observedSets().size() != 1u
        || range->originalGroups()->groupCount() != 1u
        || !core::SharedOwnerProjection::sameOwner(
            graph.physicalGroups()[0], range->originalGroups())
        || !core::SharedOwnerProjection::sameOwner(
            graph.pipelines()[0], range->originalGroups()->pipeline()))
        return "RHI-WEBGPU-ORIGINAL-COMPUTE-SOURCE-COVERAGE";
    return nullptr;
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalCompute(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<const WebGpuOrdinaryScalarComputeRange> range,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalComputeRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.native.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)range;
    (void)owner;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-COMPUTE-NATIVE-UNAVAILABLE";
    return {};
#else
    if ((diagnostic = validateOriginalComputeInputs(states, range)))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    if (range->originalGroups()->groupCount() > budget.native.maxGroups) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-GROUP-CAPACITY";
        return {};
    }
    if (range->originalGroups()->pipeline()->contract().nativeDevice() != owner->nativeDevice()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate(range, {}, owner, std::move(signal), diagnostic);
        if (!result)
            return {};
        result->originalStates_ = std::move(states);
        result->command_->impl_->ordinaryOriginalStates = result->originalStates_;
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        diagnostic = nullptr;
        return result; // No encoder/pass is opened during cold preparation.
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::recordOriginalCompute(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalStates_ || !self->range_)
        return "RHI-WEBGPU-ORIGINAL-COMPUTE-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-COMPUTE-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    const auto& arguments = *self->originalStates_->inputs()->scalars()->arguments();
    const auto* dispatch =
        arguments.records()[3u].scalar.get<rhi::CommandScalarArguments::Dispatch>();
    const auto& groups = *self->range_->validation_->groups_;
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    const char* diagnostic = nullptr;
    if (!WebGpuNativeCommandPreparation::open(*self->command_,
            self->completion_->owner()->nativeDevice(),
            "RHI WebGPU original six-command Compute",
            diagnostic))
        self->failure_ = diagnostic;
    else {
        auto descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        const auto label = arguments.text(0u);
        descriptor.label = {label.data(), label.size()};
        descriptor.nextInChain = nullptr;
        descriptor.timestampWrites = nullptr;
        state.computePass = wgpuCommandEncoderBeginComputePass(state.encoder, &descriptor);
        if (!state.computePass)
            self->failure_ = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-PASS";
        else {
            wgpuComputePassEncoderSetPipeline(
                state.computePass, groups.pipeline_->compute()->pipeline);
            for (std::uint32_t i = 0u; i < groups.groups_.size(); ++i) {
                const auto& group = groups.groups_[i];
                wgpuComputePassEncoderSetBindGroup(state.computePass,
                    i,
                    group.originalGroup,
                    group.offsets.size(),
                    group.offsets.empty() ? nullptr : group.offsets.data());
            }
            wgpuComputePassEncoderDispatchWorkgroups(
                state.computePass, dispatch->x, dispatch->y, dispatch->z);
            wgpuComputePassEncoderEnd(state.computePass);
            wgpuComputePassEncoderRelease(state.computePass);
            state.computePass = nullptr;
            self->finishBuffer();
        }
    }
    self->popScopes(self, scopeCount);
    return nullptr; // Actual scope completion is still mandatory before recorded proof.
#endif
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalGraphics(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> range,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalGraphicsRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.native.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)range;
    (void)owner;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-GRAPHICS-NATIVE-UNAVAILABLE";
    return {};
#else
    if ((diagnostic = WebGpuOriginalGraphicsStreamValidation::validate(states, range)))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    if (range->originalGroups()->groupCount() > budget.native.maxGroups) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-GROUP-CAPACITY";
        return {};
    }
    if (range->originalGroups()->pipeline()->contract().nativeDevice() != owner->nativeDevice()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({}, {}, owner, std::move(signal), diagnostic, range);
        if (!result)
            return {};
        result->originalStates_ = std::move(states);
        result->command_->impl_->ordinaryOriginalStates = result->originalStates_;
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::recordOriginalGraphics(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalStates_ || !self->originalGraphicsRange_)
        return "RHI-WEBGPU-ORIGINAL-GRAPHICS-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-GRAPHICS-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.renderPass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    const auto& inputs = *self->originalStates_->inputs();
    const auto& records = inputs.scalars()->arguments()->records();
    const auto& pass = inputs.passes()->passes()[0].arguments->desc();
    const auto& attachment = pass.colorAttachments[0];
    const auto& target = inputs.scalars()->usage()->projection()->resources()->resources()[0];
    const auto& groups = *self->validation().groups_;
    const auto& viewport = *records[2u].scalar.get<rhi::ViewportState>();
    const auto& scissor = *records[3u].scalar.get<rhi::ScissorState>();
    const auto& draw = *records[5u].scalar.get<rhi::CommandScalarArguments::Draw>();
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    const char* diagnostic = nullptr;
    if (!WebGpuNativeCommandPreparation::open(*self->command_,
            self->completion_->owner()->nativeDevice(),
            "RHI WebGPU original eight-command Graphics",
            diagnostic))
        self->failure_ = diagnostic;
    else {
        auto color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = target.view()->view;
        color.resolveTarget = nullptr;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp = WebGpuAttachmentOperationLowering::load(attachment.loadOp);
        color.storeOp = WebGpuAttachmentOperationLowering::store(attachment.storeOp);
        const auto& clear = attachment.clearColor;
        color.clearValue = {clear.r, clear.g, clear.b, clear.a};
        WGPURenderPassMaxDrawCount drawLimit{};
        drawLimit.chain.next = nullptr;
        drawLimit.chain.sType = WGPUSType_RenderPassMaxDrawCount;
        drawLimit.maxDrawCount = 1u;
        auto descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.nextInChain = &drawLimit.chain;
        descriptor.label = webGpuNativeStringView("RHI WebGPU original Graphics pass");
        descriptor.colorAttachmentCount = 1u;
        descriptor.colorAttachments = &color;
        descriptor.depthStencilAttachment = nullptr;
        descriptor.occlusionQuerySet = nullptr;
        descriptor.timestampWrites = nullptr;
        state.renderPass = wgpuCommandEncoderBeginRenderPass(state.encoder, &descriptor);
        if (!state.renderPass)
            self->failure_ = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-PASS";
        else {
            wgpuRenderPassEncoderSetPipeline(
                state.renderPass, groups.pipeline_->graphics()->pipeline);
            wgpuRenderPassEncoderSetViewport(state.renderPass,
                viewport.x,
                viewport.y,
                viewport.width,
                viewport.height,
                viewport.minDepth,
                viewport.maxDepth);
            wgpuRenderPassEncoderSetScissorRect(state.renderPass,
                static_cast<std::uint32_t>(scissor.x),
                static_cast<std::uint32_t>(scissor.y),
                scissor.width,
                scissor.height);
            for (std::uint32_t i = 0u; i < groups.groups_.size(); ++i) {
                const auto& group = groups.groups_[i];
                wgpuRenderPassEncoderSetBindGroup(state.renderPass,
                    i,
                    group.originalGroup,
                    group.offsets.size(),
                    group.offsets.empty() ? nullptr : group.offsets.data());
            }
            wgpuRenderPassEncoderDraw(state.renderPass,
                draw.vertices,
                draw.instances,
                draw.firstVertex,
                draw.firstInstance);
            wgpuRenderPassEncoderEnd(state.renderPass);
            wgpuRenderPassEncoderRelease(state.renderPass);
            state.renderPass = nullptr;
            self->finishBuffer();
        }
    }
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalTransferInputs(
    const std::shared_ptr<const WebGpuOriginalResourceStates>& states, void* device) noexcept
{
    if (const auto* id = WebGpuOriginalTransferStreamValidation::validate(states))
        return id;
    const auto label = states->inputs()->scalars()->arguments()->text(0u);
    if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
        return "RHI-WEBGPU-ORIGINAL-TRANSFER-LABEL-ENCODING";
    const auto& root = *states->inputs()->scalars()->usage()->projection()->transfer();
    const auto& copy =
        std::get<rhi::TransferCopyArguments>(states->inputs()->descriptors()[0].arguments);
    return WebGpuNativeTransferCopyLowering::validate(root, copy, device);
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalTransfer(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalTransferRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)owner;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-TRANSFER-NATIVE-UNAVAILABLE";
    return {};
#else
    // Reject the entire original language and label before reserving or opening native work.
    if ((diagnostic = WebGpuOriginalTransferStreamValidation::validate(states)))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    if ((diagnostic = validateOriginalTransferInputs(states, owner->nativeDevice())))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({}, {}, owner, std::move(signal), diagnostic, {}, std::move(states));
        if (!result)
            return {};
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::recordOriginalTransfer(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalTransfer() || self->originalTransaction_
        || self->originalTextureTransaction_)
        return "RHI-WEBGPU-ORIGINAL-TRANSFER-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-TRANSFER-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.renderPass || state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    const auto& inputs = *self->originalStates_->inputs();
    const auto& root = *inputs.scalars()->usage()->projection()->transfer();
    const auto& copy = std::get<rhi::TransferCopyArguments>(inputs.descriptors()[0].arguments);
    const auto label = inputs.scalars()->arguments()->text(0u);
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    const char* diagnostic = nullptr;
    if (!WebGpuNativeCommandPreparation::open(*self->command_,
            self->completion_->owner()->nativeDevice(),
            "RHI WebGPU original four-command Transfer",
            diagnostic))
        self->failure_ = diagnostic;
    else {
        wgpuCommandEncoderPushDebugGroup(state.encoder, {label.data(), label.size()});
        self->failure_ = WebGpuNativeTransferCopyLowering::emitOriginal(state.encoder, root, copy);
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        if (!self->failure_)
            self->finishBuffer();
    }
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalTransferDependency(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalTransferDependencyRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<core::CpuMemoryQuota> transactionQuota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.native.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
    if (!transactionQuota
        || transactionQuota->limitBytes() != budget.transaction.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)owner;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-DEPENDENCY-NATIVE-UNAVAILABLE";
    return {};
#else
    if ((diagnostic = WebGpuOriginalTransferDependencyValidation::validate(states)))
        return {};
    const auto mapping = WebGpuOriginalTransferDependencyValidation::initialMapping(states);
    if ((diagnostic = mapping.diagnostic))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    const auto& root = *states->inputs()->scalars()->usage()->projection()->transfer();
    const auto& copy =
        std::get<rhi::TransferCopyArguments>(states->inputs()->descriptors()[2u].arguments);
    if ((diagnostic =
                WebGpuNativeTransferCopyLowering::validate(root, copy, owner->nativeDevice())))
        return {};
    auto transaction = WebGpuOriginalStateTransaction::prepareOriginal(
        states, budget.transaction, transactionQuota, diagnostic);
    if (!transaction)
        return {};
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({},
            {},
            owner,
            std::move(signal),
            diagnostic,
            {},
            std::move(states),
            std::move(transaction));
        if (!result)
            return {};
        result->originalDependencyDecisions_ = mapping.dependencies;
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalDependencyInputs() const noexcept
{
    if (!originalTransfer() || !originalTransaction_
        || !originalTransaction_->hasQuotaBoundSharedOwner(originalTransaction_)
        || !core::SharedOwnerProjection::sameOwner(originalTransaction_->states(), originalStates_))
        return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-TRANSACTION-OWNER";
    const auto mapping =
        WebGpuOriginalTransferDependencyValidation::initialMapping(originalStates_);
    if (mapping.diagnostic)
        return mapping.diagnostic;
    for (std::size_t i = 0u; i < mapping.dependencies.size(); ++i) {
        const auto& original = originalDependencyDecisions_[i];
        const auto& current = mapping.dependencies[i];
        if (original.diagnostic || original.outcome != current.outcome
            || original.operation != current.operation)
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-LOWERING-UNPROVEN";
    }
    using T = WebGpuOriginalStateTransaction;
    const auto expected = status_ == Status::Prepared ? T::Status::Prepared
        : command_->impl_->submitted                  ? T::Status::Committed
                                                      : T::Status::Pending;
    if (originalTransaction_->status() != expected)
        return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-TRANSACTION-STATUS";
    if (const auto* id = originalTransaction_->validate())
        return id;
    const auto& root = *originalStates_->inputs()->scalars()->usage()->projection()->transfer();
    const auto& copy = std::get<rhi::TransferCopyArguments>(
        originalStates_->inputs()->descriptors()[2u].arguments);
    return WebGpuNativeTransferCopyLowering::validate(
        root, copy, completion_ ? completion_->owner()->nativeDevice() : nullptr);
}

const char* WebGpuOrdinaryRecording::recordOriginalTransferDependency(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalTransfer() || !self->originalTransaction_ || self->originalUpload_
        || self->originalReadbackCopies_)
        return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.renderPass || state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    // Consume both frozen decisions before changing state or emitting native work.
    for (const auto& decision : self->originalDependencyDecisions_) {
        if (decision.diagnostic || decision.outcome != rhi::CapabilityMode::Equivalent
            || decision.operation
                != rhi::semantics::TransferDependencyLowering::Operation::ImplicitOrderedUsage)
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-LOWERING-UNPROVEN";
    }
    if (const auto* id = WebGpuOriginalStateTransaction::apply(self->originalTransaction_))
        return id;
    const auto& inputs = *self->originalStates_->inputs();
    const auto& root = *inputs.scalars()->usage()->projection()->transfer();
    const auto& copy = std::get<rhi::TransferCopyArguments>(inputs.descriptors()[2u].arguments);
    const auto label = inputs.scalars()->arguments()->text(2u);
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    const char* diagnostic = nullptr;
    if (!WebGpuNativeCommandPreparation::open(*self->command_,
            self->completion_->owner()->nativeDevice(),
            "RHI WebGPU original dependency Transfer",
            diagnostic))
        self->failure_ = diagnostic;
    else {
        wgpuCommandEncoderPushDebugGroup(state.encoder, {label.data(), label.size()});
        self->failure_ = WebGpuNativeTransferCopyLowering::emitOriginal(state.encoder, root, copy);
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        if (!self->failure_)
            self->finishBuffer();
    }
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalUploadInputs(
    const std::shared_ptr<const WebGpuOriginalResourceStates>& states, void* device) noexcept
{
    if (const auto* id = WebGpuOriginalBufferUploadStreamValidation::validate(states))
        return id;
    const auto& arguments = *states->inputs()->scalars()->arguments();
    for (const auto index : {0u, 1u}) {
        const auto label = arguments.text(index);
        if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-LABEL-ENCODING";
    }
    const auto& root = *states->inputs()->scalars()->usage()->projection()->transfer();
    const auto& upload =
        std::get<rhi::BufferUploadArguments>(states->inputs()->descriptors()[0].arguments);
    return WebGpuNativeTransferCopyLowering::validate(root, upload.copyArguments(), device);
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalBufferUpload(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalBufferUploadRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<core::CpuMemoryQuota> transactionQuota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.native.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)owner;
    (void)transactionQuota;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-UPLOAD-NATIVE-UNAVAILABLE";
    return {};
#else
    if ((diagnostic = WebGpuOriginalBufferUploadStreamValidation::validate(states)))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    if ((diagnostic = validateOriginalUploadInputs(states, owner->nativeDevice())))
        return {};
    auto transaction = WebGpuOriginalStateTransaction::prepareOriginal(
        states, budget.transaction, std::move(transactionQuota), diagnostic);
    if (!transaction)
        return {};
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({},
            {},
            owner,
            std::move(signal),
            diagnostic,
            {},
            std::move(states),
            std::move(transaction));
        if (!result)
            return {};
        result->originalUpload_ = true;
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalUploadInputs() const noexcept
{
    if (!originalTransfer() || !originalUpload_ || !originalTransaction_
        || !originalTransaction_->hasQuotaBoundSharedOwner(originalTransaction_)
        || !core::SharedOwnerProjection::sameOwner(originalTransaction_->states(), originalStates_))
        return "RHI-WEBGPU-ORIGINAL-UPLOAD-TRANSACTION-OWNER";
    const auto expected = command_->impl_->submitted
        ? WebGpuOriginalStateTransaction::Status::Committed
        : status_ == Status::Prepared ? WebGpuOriginalStateTransaction::Status::Prepared
                                      : WebGpuOriginalStateTransaction::Status::Pending;
    if (originalTransaction_->status() != expected)
        return "RHI-WEBGPU-ORIGINAL-UPLOAD-TRANSACTION-STATUS";
    if (const auto* id = originalTransaction_->validate())
        return id;
    return validateOriginalUploadInputs(
        originalStates_, completion_ ? completion_->owner()->nativeDevice() : nullptr);
}

const char* WebGpuOrdinaryRecording::recordOriginalBufferUpload(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalUpload_)
        return "RHI-WEBGPU-ORIGINAL-UPLOAD-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-UPLOAD-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.renderPass || state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    if (const auto* id = WebGpuOriginalStateTransaction::apply(self->originalTransaction_))
        return id;
    const auto& inputs = *self->originalStates_->inputs();
    const auto& root = *inputs.scalars()->usage()->projection()->transfer();
    const auto& upload = std::get<rhi::BufferUploadArguments>(inputs.descriptors()[0].arguments);
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    const char* diagnostic = nullptr;
    if (!WebGpuNativeCommandPreparation::open(*self->command_,
            self->completion_->owner()->nativeDevice(),
            "RHI WebGPU original BufferUpload",
            diagnostic))
        self->failure_ = diagnostic;
    else {
        for (const auto index : {0u, 1u}) {
            const auto label = inputs.scalars()->arguments()->text(index);
            wgpuCommandEncoderPushDebugGroup(state.encoder, {label.data(), label.size()});
        }
        self->failure_ = WebGpuNativeTransferCopyLowering::emitOriginal(
            state.encoder, root, upload.copyArguments());
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        if (!self->failure_)
            self->finishBuffer();
    }
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalTextureUpload(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalTextureUploadRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<core::CpuMemoryQuota> transactionQuota,
    std::shared_ptr<core::CpuMemoryQuota> contentQuota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.native.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)owner;
    (void)transactionQuota;
    (void)contentQuota;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-NATIVE-UNAVAILABLE";
    return {};
#else
    if ((diagnostic = WebGpuOriginalTextureUploadTransaction::validateInitialStream(states)))
        return {};
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    const auto& input = *states->inputs();
    const auto& source = input.scalars()->usage()->projection()->originalTextureUploads()[0];
    const auto copies = source->packing()->mipLayouts().size();
    // One native copy per exact mip, followed by one native finish operation.
    if (copies > budget.maxMipCopies || copies >= budget.native.maxCommands) {
        diagnostic = "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-COPY-CAPACITY";
        return {};
    }
    for (const auto index : {0u, 1u}) {
        const auto label = input.scalars()->arguments()->text(index);
        if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label)) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-LABEL-ENCODING";
            return {};
        }
    }
    auto transaction = WebGpuOriginalTextureUploadTransaction::prepareOriginal(states,
        budget.transaction,
        std::move(transactionQuota),
        std::move(contentQuota),
        diagnostic);
    if (!transaction)
        return {};
    for (std::size_t i = 0u; i < transaction->decisions().size(); ++i)
        if ((diagnostic = WebGpuOriginalTextureUploadCopyValidation::validateCurrent(
                 transaction, i, transaction->decisions()[i], owner->nativeDevice())))
            return {};
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({},
            {},
            owner,
            std::move(signal),
            diagnostic,
            {},
            std::move(states),
            {},
            std::move(transaction));
        if (!result)
            return {};
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalTextureUploadInputs() const noexcept
{
    using Transaction = WebGpuOriginalTextureUploadTransaction;
    if (!originalTransfer() || originalUpload_ || originalTransaction_
        || !originalTextureTransaction_->hasQuotaBoundSharedOwner(originalTextureTransaction_)
        || originalTextureTransaction_->states().get() != originalStates_.get()
        || !core::SharedOwnerProjection::sameOwner(
            originalTextureTransaction_->states(), originalStates_))
        return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-TRANSACTION-OWNER";
    const auto expected = command_->impl_->submitted ? Transaction::Status::Submitted
        : status_ == Status::Prepared                ? Transaction::Status::Prepared
                                                     : Transaction::Status::Pending;
    if (originalTextureTransaction_->status() != expected)
        return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-TRANSACTION-STATUS";
    if (const auto* id = originalTextureTransaction_->validate())
        return id;
    for (std::size_t i = 0u; i < originalTextureTransaction_->decisions().size(); ++i)
        if (const auto* id = WebGpuOriginalTextureUploadCopyValidation::validateCurrent(
                originalTextureTransaction_,
                i,
                originalTextureTransaction_->decisions()[i],
                completion_ ? completion_->owner()->nativeDevice() : nullptr))
            return id;
    return nullptr;
}

const char* WebGpuOrdinaryRecording::recordOriginalTextureUpload(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalTextureTransaction_)
        return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.renderPass || state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    if (const auto* id =
            WebGpuOriginalTextureUploadTransaction::apply(self->originalTextureTransaction_))
        return id;
    const auto& input = *self->originalStates_->inputs();
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    const char* diagnostic = nullptr;
    if (!WebGpuNativeCommandPreparation::open(*self->command_,
            self->completion_->owner()->nativeDevice(),
            "RHI WebGPU original TextureUpload",
            diagnostic))
        self->failure_ = diagnostic;
    else {
        for (const auto index : {0u, 1u}) {
            const auto label = input.scalars()->arguments()->text(index);
            wgpuCommandEncoderPushDebugGroup(state.encoder, {label.data(), label.size()});
        }
        for (std::size_t i = 0u;
            i < self->originalTextureTransaction_->decisions().size() && !self->failure_;
            ++i)
            self->failure_ = WebGpuNativeOriginalTextureUploadLowering::emit(state.encoder,
                self->originalTextureTransaction_,
                i,
                self->completion_->owner()->nativeDevice());
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        if (!self->failure_)
            self->finishBuffer();
    }
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::prepareOriginalTextureReadback(
    std::shared_ptr<const WebGpuOriginalResourceStates> states,
    std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner,
    const WebGpuOriginalTextureReadbackRecordingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<core::CpuMemoryQuota> transactionQuota,
    std::shared_ptr<core::CpuMemoryQuota> copyQuota,
    const char*& diagnostic,
    std::shared_ptr<WebGpuNativeCompletionState> signal)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.native.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)states;
    (void)owner;
    (void)transactionQuota;
    (void)copyQuota;
    (void)signal;
    diagnostic = "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        return {};
    }
    auto transaction = WebGpuOriginalStateTransaction::prepareReadbackOriginal(
        states, budget.transaction, std::move(transactionQuota), diagnostic);
    if (!transaction)
        return {};
    auto copies = WebGpuOriginalTextureReadbackCopies::prepareOriginal(
        transaction, budget.copies, std::move(copyQuota), diagnostic);
    if (!copies)
        return {};
    rhi::semantics::TransferDependencyLowering::Decision dependency;
    if ((diagnostic = WebGpuOriginalTextureReadbackCopies::initialDependencyMapping(
             transaction, dependency)))
        return {};
    const auto& pair = states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
    if (copies->decisions().size() > budget.maxMipCopies
        || copies->decisions().size() >= budget.native.maxCommands) {
        diagnostic = "RHI-WEBGPU-ORIGINAL-READBACK-COPY-CAPACITY";
        return {};
    }
    if (pair->destination()->nativeResource()->ownerDevice != owner->nativeDevice()
        || pair->destination()->source()->resource().texture->ownerDevice
            != owner->nativeDevice()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        return {};
    }
    for (const auto index : {1u, 2u}) {
        const auto label = states->inputs()->scalars()->arguments()->text(index);
        if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label)) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-READBACK-LABEL-ENCODING";
            return {};
        }
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = allocate({},
            {},
            owner,
            std::move(signal),
            diagnostic,
            {},
            std::move(states),
            std::move(transaction),
            {},
            std::move(copies));
        if (!result)
            return {};
        result->originalDependencyDecisions_[0] = dependency;
        result->status_ = Status::Prepared;
        result->remaining_ = 0u;
        if ((diagnostic = result->validateOriginalTextureReadbackInputs()))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryRecording::validateOriginalTextureReadbackInputs() const noexcept
{
    if (webGpuNativeDeviceLost())
        return "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST";
    if (!originalTransfer() || originalUpload_ || originalTextureTransaction_
        || !originalTransaction_ || !originalReadbackCopies_
        || !originalReadbackCopies_->hasQuotaBoundSharedOwner(originalReadbackCopies_)
        || !originalTransaction_->hasQuotaBoundSharedOwner(originalTransaction_)
        || !originalTransaction_->isReadbackProfile()
        || !core::SharedOwnerProjection::sameOwner(
            originalReadbackCopies_->transaction(), originalTransaction_)
        || !core::SharedOwnerProjection::sameOwner(originalTransaction_->states(), originalStates_))
        return "RHI-WEBGPU-ORIGINAL-READBACK-TRANSACTION-OWNER";
    rhi::semantics::TransferDependencyLowering::Decision dependency;
    if (const auto* id = WebGpuOriginalTextureReadbackCopies::initialDependencyMapping(
            originalTransaction_, dependency))
        return id;
    const auto& frozen = originalDependencyDecisions_[0];
    if (frozen.diagnostic || frozen.outcome != dependency.outcome
        || frozen.operation != dependency.operation)
        return "RHI-WEBGPU-ORIGINAL-READBACK-DEPENDENCY-UNPROVEN";
    const auto expected = status_ == Status::Prepared
        ? WebGpuOriginalStateTransaction::Status::Prepared
        : command_->impl_->submitted ? WebGpuOriginalStateTransaction::Status::Committed
                                     : WebGpuOriginalStateTransaction::Status::Pending;
    if (originalTransaction_->status() != expected)
        return "RHI-WEBGPU-ORIGINAL-READBACK-TRANSACTION-STATUS";
    return originalReadbackCopies_->validate();
}

const char* WebGpuOrdinaryRecording::recordOriginalTextureReadback(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (!self->originalReadbackCopies_)
        return "RHI-WEBGPU-ORIGINAL-READBACK-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (state.encoder || state.renderPass || state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    // Explicitly consume the frozen equivalent dependency before journal apply.
    const auto& dependency = self->originalDependencyDecisions_[0];
    if (dependency.diagnostic || dependency.outcome != rhi::CapabilityMode::Equivalent
        || dependency.operation
            != rhi::semantics::TransferDependencyLowering::Operation::ImplicitOrderedUsage)
        return "RHI-WEBGPU-ORIGINAL-READBACK-DEPENDENCY-UNPROVEN";
    if (const auto* id = WebGpuOriginalStateTransaction::apply(self->originalTransaction_))
        return id;
    self->readbackRecordingIssued_ = true;
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->pushScopes();
    const auto& inputs = *self->originalStates_->inputs();
    const auto label = inputs.scalars()->arguments()->text(1u);
    const char* failure = nullptr;
    if (!WebGpuNativeCommandPreparation::open(
            *self->command_, self->completion_->owner()->nativeDevice(), label, failure))
        self->failure_ = failure;
    else {
        for (const auto index : {1u, 2u}) {
            const auto text = inputs.scalars()->arguments()->text(index);
            wgpuCommandEncoderPushDebugGroup(state.encoder, {text.data(), text.size()});
        }
        for (std::size_t i = 0u;
            i < self->originalReadbackCopies_->decisions().size() && !self->failure_;
            ++i)
            self->failure_ = WebGpuNativeOriginalTextureReadbackLowering::emit(state.encoder,
                self->originalReadbackCopies_,
                i,
                self->completion_->owner()->nativeDevice());
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        wgpuCommandEncoderPopDebugGroup(state.encoder);
        if (!self->failure_)
            self->finishBuffer();
    }
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocate(
    std::shared_ptr<const WebGpuOrdinaryScalarComputeRange> range,
    std::shared_ptr<const WebGpuOrdinaryGraphicsAttachments> attachments,
    const std::shared_ptr<WebGpuOrdinaryCompletionOwner>& owner,
    std::shared_ptr<WebGpuNativeCompletionState> signal,
    const char*& diagnostic,
    std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> originalGraphics,
    std::shared_ptr<const WebGpuOriginalResourceStates> originalTransfer,
    std::shared_ptr<WebGpuOriginalStateTransaction> originalTransaction,
    std::shared_ptr<WebGpuOriginalTextureUploadTransaction> originalTextureTransaction,
    std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> originalReadbackCopies)
{
    auto command = WebGpuNativeCommandPreparation::allocate({});
    command->impl_->ordinaryPrepared = true;
    command->impl_->ordinaryScalarRange = range;
    command->impl_->ordinaryGraphicsAttachments = attachments;
    command->impl_->ordinaryOriginalGraphicsRange = originalGraphics;
    command->impl_->ordinaryOriginalStates = originalTransfer;
    command->impl_->ordinaryOriginalStateTransaction = originalTransaction;
    command->impl_->ordinaryOriginalTextureTransaction = originalTextureTransaction;
    command->impl_->ordinaryOriginalReadbackCopies = originalReadbackCopies;
    auto unique = originalTransfer ? core::makeUnique<WebGpuOrdinaryRecording>(
                                         ConstructionKey{}, std::move(originalTransfer), command)
        : range
        ? core::makeUnique<WebGpuOrdinaryRecording>(ConstructionKey{}, std::move(range), command)
        : originalGraphics ? core::makeUnique<WebGpuOrdinaryRecording>(
                                 ConstructionKey{}, std::move(originalGraphics), command)
                           : core::makeUnique<WebGpuOrdinaryRecording>(
                                 ConstructionKey{}, std::move(attachments), command);
    for (auto& callback : unique->callbacks_)
        callback = core::makeUnique<Callback>();
    auto result = shareOriginal(std::move(unique));
    result->originalTransaction_ = std::move(originalTransaction);
    result->originalTextureTransaction_ = std::move(originalTextureTransaction);
    result->originalReadbackCopies_ = std::move(originalReadbackCopies);
    if (!WebGpuNativeCommandPreparation::connect(*command, owner->nativeDevice(), diagnostic))
        return {};
    result->completion_ =
        WebGpuOrdinaryCompletionReservation::prepare(owner, command, signal, diagnostic);
    return result->completion_ ? result : std::shared_ptr<WebGpuOrdinaryRecording>{};
}

const WebGpuOrdinaryShaderSizeValidation& WebGpuOrdinaryRecording::validation() const noexcept
{
    return range_                ? *range_->validation_
        : originalGraphicsRange_ ? *originalGraphicsRange_->validation_
                                 : *attachments_->range()->validation_;
}

const char* WebGpuOrdinaryRecording::validateOriginal() const noexcept
{
    if (canonicalMode_)
        return canonicalDiagnostic();
    if (!command_->impl_ || !command_->impl_->ordinaryPrepared
        || command_->impl_->ordinaryUnsupported
        || !core::SharedOwnerProjection::sameOwner(command_->impl_->ordinaryScalarRange, range_)
        || !core::SharedOwnerProjection::sameOwner(
            command_->impl_->ordinaryGraphicsAttachments, attachments_)
        || !core::SharedOwnerProjection::sameOwner(
            command_->impl_->ordinaryOriginalGraphicsRange, originalGraphicsRange_))
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    if (!core::SharedOwnerProjection::sameOwner(
            command_->impl_->ordinaryOriginalStates, originalStates_))
        return originalTransfer()    ? "RHI-WEBGPU-ORIGINAL-TRANSFER-STATE-OWNER"
            : originalGraphicsRange_ ? "RHI-WEBGPU-ORIGINAL-GRAPHICS-STATE-OWNER"
                                     : "RHI-WEBGPU-ORIGINAL-COMPUTE-STATE-OWNER";
    if (!core::SharedOwnerProjection::sameOwner(
            command_->impl_->ordinaryOriginalStateTransaction, originalTransaction_))
        return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-TRANSACTION-OWNER";
    if (!core::SharedOwnerProjection::sameOwner(
            command_->impl_->ordinaryOriginalTextureTransaction, originalTextureTransaction_))
        return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-TRANSACTION-OWNER";
    if (!core::SharedOwnerProjection::sameOwner(
            command_->impl_->ordinaryOriginalReadbackCopies, originalReadbackCopies_))
        return "RHI-WEBGPU-ORIGINAL-READBACK-COPIES-OWNER";
    if (originalStates_) {
        const auto* id = originalReadbackCopies_ ? validateOriginalTextureReadbackInputs()
            : originalTextureTransaction_        ? validateOriginalTextureUploadInputs()
            : originalUpload_                    ? validateOriginalUploadInputs()
            : originalTransaction_               ? validateOriginalDependencyInputs()
            : originalTransfer()                 ? validateOriginalTransferInputs(originalStates_,
                                       completion_ ? completion_->owner()->nativeDevice() : nullptr)
            : originalGraphicsRange_             ? WebGpuOriginalGraphicsStreamValidation::validate(
                                           originalStates_, originalGraphicsRange_)
                                     : validateOriginalComputeInputs(originalStates_, range_);
        if (id)
            return id;
    }
    if (range_) {
        if (!range_->hasQuotaBoundSharedOwner(range_))
            return "RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER";
        if (const auto* id = range_->validate())
            return id;
    } else if (originalGraphicsRange_) {
        if (!originalGraphicsRange_->hasQuotaBoundSharedOwner(originalGraphicsRange_))
            return "RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER";
        if (const auto* id = originalGraphicsRange_->validate())
            return id;
    } else if (!originalTransfer()) {
        if (!attachments_ || !attachments_->hasQuotaBoundSharedOwner(attachments_))
            return "RHI-WEBGPU-ORDINARY-RECORDING-ATTACHMENT-OWNER";
        if (const auto* id = attachments_->validate())
            return id;
    }
    return validateOriginalCommand();
}

WebGpuOrdinaryRecording::Status WebGpuOrdinaryRecording::status() const noexcept
{
    if (canonicalMode_)
        return canonicalStatus();
    if (completion_ && completion_->owner()->retained.closed())
        return Status::DeviceLost;
    if (!originalTransfer()
        && validation().status() == WebGpuOrdinaryShaderSizeValidation::Status::DeviceLost)
        return Status::DeviceLost;
    return status_;
}

const char* WebGpuOrdinaryRecording::diagnostic() const noexcept
{
    if (canonicalMode_)
        return canonicalDiagnostic();
    if ((originalTransaction_ || originalTextureTransaction_) && status_ == Status::Rejected)
        return failure_;
    if (const auto* id = validateOriginal())
        return id;
    switch (status()) {
    case Status::PreparationPending:
        return "RHI-WEBGPU-ORDINARY-RECORDING-PREPARATION-PENDING";
    case Status::Prepared:
        return "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED";
    case Status::RecordingPending:
        return "RHI-WEBGPU-ORDINARY-RECORDING-FINISH-PENDING";
    case Status::Recorded:
        return nullptr;
    case Status::Rejected:
        return failure_;
    case Status::DeviceLost:
        return "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST";
    }
    return "RHI-WEBGPU-ORDINARY-RECORDING-STATUS";
}

const char* WebGpuOrdinaryRecording::record(const std::shared_ptr<WebGpuOrdinaryRecording>& self,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (self->originalStates_)
        return self->originalTransfer()    ? "RHI-WEBGPU-ORIGINAL-TRANSFER-MODE"
            : self->originalGraphicsRange_ ? "RHI-WEBGPU-ORIGINAL-GRAPHICS-MODE"
                                           : "RHI-WEBGPU-ORIGINAL-COMPUTE-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
    if (!self->range_)
        return "RHI-WEBGPU-ORDINARY-RECORDING-LANGUAGE";
    if (const auto* id = self->range_->validateDispatch(x, y, z))
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (!state.encoder || !state.computePass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    const auto& groups = *self->range_->validation_->groups_;
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    wgpuComputePassEncoderSetPipeline(state.computePass, groups.pipeline_->compute()->pipeline);
    for (std::uint32_t i = 0; i < groups.groups_.size(); ++i) {
        const auto& group = groups.groups_[i];
        wgpuComputePassEncoderSetBindGroup(state.computePass,
            i,
            group.originalGroup,
            group.offsets.size(),
            group.offsets.empty() ? nullptr : group.offsets.data());
    }
    wgpuComputePassEncoderDispatchWorkgroups(state.computePass, x, y, z);
    wgpuComputePassEncoderEnd(state.computePass);
    wgpuComputePassEncoderRelease(state.computePass);
    state.computePass = nullptr;
    self->finishBuffer();
    self->popScopes(self, scopeCount);
    return nullptr; // RecordingPending is observable and blocks execution.
#endif
}

const char* WebGpuOrdinaryRecording::record(const std::shared_ptr<WebGpuOrdinaryRecording>& self,
    std::uint32_t vertices,
    std::uint32_t instances,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER";
    if (self->originalStates_)
        return self->originalTransfer()    ? "RHI-WEBGPU-ORIGINAL-TRANSFER-MODE"
            : self->originalGraphicsRange_ ? "RHI-WEBGPU-ORIGINAL-GRAPHICS-MODE"
                                           : "RHI-WEBGPU-ORIGINAL-COMPUTE-MODE";
    if (const auto* id = self->recordDiagnostic())
        return id;
    if (!self->attachments_)
        return "RHI-WEBGPU-ORDINARY-RECORDING-LANGUAGE";
    if (const auto* id = self->attachments_->range()->validateDraw(
            vertices, instances, firstVertex, firstInstance))
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-UNAVAILABLE";
#else
    auto& state = *self->command_->impl_;
    if (!state.encoder || !state.renderPass || state.finished || state.submitted)
        return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
    const auto& groups = *self->validation().groups_;
    const auto& graphics = groups.pipeline_->contract().graphics();
    self->status_ = Status::RecordingPending;
    self->remaining_ = scopeCount;
    self->callbackSuccess_ = true;
    self->noErrors_ = true;
    self->pushScopes();
    wgpuRenderPassEncoderSetPipeline(state.renderPass, groups.pipeline_->graphics()->pipeline);
    for (std::uint32_t i = 0u; i < groups.groups_.size(); ++i) {
        const auto& group = groups.groups_[i];
        wgpuRenderPassEncoderSetBindGroup(state.renderPass,
            i,
            group.originalGroup,
            group.offsets.size(),
            group.offsets.empty() ? nullptr : group.offsets.data());
    }
    const auto& v = graphics.viewport;
    const auto& s = graphics.scissor;
    wgpuRenderPassEncoderSetViewport(
        state.renderPass, v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth);
    wgpuRenderPassEncoderSetScissorRect(state.renderPass,
        static_cast<std::uint32_t>(s.x),
        static_cast<std::uint32_t>(s.y),
        s.width,
        s.height);
    wgpuRenderPassEncoderDraw(state.renderPass, vertices, instances, firstVertex, firstInstance);
    wgpuRenderPassEncoderEnd(state.renderPass);
    wgpuRenderPassEncoderRelease(state.renderPass);
    state.renderPass = nullptr;
    self->finishBuffer();
    self->popScopes(self, scopeCount);
    return nullptr;
#endif
}

const char* WebGpuOrdinaryRecording::recordDiagnostic() const noexcept
{
    if (const auto* id = validateOriginal())
        return id;
    if (status_ != Status::Prepared)
        return status_ == Status::PreparationPending
            ? "RHI-WEBGPU-ORDINARY-RECORDING-PREPARATION-PENDING"
            : "RHI-WEBGPU-ORDINARY-RECORDING-STATE";
    return nullptr;
}
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void WebGpuOrdinaryRecording::finishBuffer() noexcept
{
    auto& state = *command_->impl_;
    auto descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = webGpuNativeStringView("RHI WebGPU bounded ordinary command buffer");
    state.commandBuffer = wgpuCommandEncoderFinish(state.encoder, &descriptor);
    if (!state.commandBuffer)
        failure_ = "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-FINISH";
    state.finished = true;
}

void WebGpuOrdinaryRecording::pushScopes() noexcept
{
    auto device = static_cast<WGPUDevice>(completion_->owner()->nativeDevice());
    WebGpuNativeErrorScopeBatch::push(device);
}

void WebGpuOrdinaryRecording::popScopes(
    const std::shared_ptr<WebGpuOrdinaryRecording>& self, std::size_t first) noexcept
{
    auto device = static_cast<WGPUDevice>(completion_->owner()->nativeDevice());
    WebGpuNativeErrorScopeBatch::pop(device, self, callbacks_, first, &complete);
}

void WebGpuOrdinaryRecording::complete(
    WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView, void* data, void*) noexcept
{
    auto* original = static_cast<Callback*>(data);
    core::UniquePtr<Callback> callback(original,
        core::CpuAllocatedDeleter<Callback>{original, &core::destroyCpuAllocatedUnique<Callback>});
    if (!callback || !callback->owner)
        return;
    auto& self = *callback->owner;
    if (!self.remaining_)
        return;
    --self.remaining_;
    if (self.status() == Status::DeviceLost) {
        self.status_ = Status::DeviceLost;
        core::runtime::CompletionDrainConsumer::consumeOnce(
            self.readbackRecordingCallbacksDrained(), self.readbackDrainConsumer_);
        return;
    }
    if (self.status_ != Status::PreparationPending && self.status_ != Status::RecordingPending)
        return;
    self.callbackSuccess_ &= status == WGPUPopErrorScopeStatus_Success;
    self.noErrors_ &= type == WGPUErrorType_NoError;
    if (self.remaining_)
        return;
    const auto* diagnostic = WebGpuOrdinaryShaderSizeValidationOwner::callbackDiagnostic(
        self.callbackSuccess_, self.noErrors_, self.failure_);
    if (!diagnostic && self.originalReadbackCopies_ && self.status_ == Status::RecordingPending) {
        const auto& state = *self.command_->impl_;
        diagnostic = !state.finished || !state.commandBuffer
            ? "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-FINISH"
            : WebGpuOriginalTextureReadbackCopies::markNativeRecorded(self.originalReadbackCopies_,
                  self.identity_,
                  state.device,
                  state.queue,
                  self.completion_->owner());
    }
    if (diagnostic) {
        if (self.originalReadbackCopies_)
            self.originalReadbackCopies_->invalidateNativeCompletion();
        if (self.originalTransaction_)
            self.originalTransaction_->abandon();
        if (self.originalTextureTransaction_)
            self.originalTextureTransaction_->abandon();
        self.failure_ = diagnostic;
        self.status_ = Status::Rejected;
    } else
        self.status_ =
            self.status_ == Status::PreparationPending ? Status::Prepared : Status::Recorded;
    core::runtime::CompletionDrainConsumer::consumeOnce(
        self.readbackRecordingCallbacksDrained(), self.readbackDrainConsumer_);
}
#endif
} // namespace arp::rhi_webgpu
