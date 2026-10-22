#include "rhi/backends/webgpu/webgpu_prototype_batch.h"

#include "core/diagnostics/diagnostic_error.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/prototype_command_batch_validation.h"

#include <chrono>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {

void WebGpuPrototypeBatch::Fence::signal()
{
    throw core::DiagnosticError("RHI-PROTOTYPE-BATCH-HOST-SIGNAL");
}

void WebGpuPrototypeBatch::Fence::reset()
{
    throw core::DiagnosticError("RHI-PROTOTYPE-BATCH-ONE-SHOT-FENCE");
}

void WebGpuPrototypeBatch::Fence::wait()
{
    if (!state_->accepted && !state_->terminal)
        throw core::DiagnosticError("RHI-PROTOTYPE-BATCH-NOT-SUBMITTED");
    while (!state_->terminal) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        emscripten_sleep(0);
#else
        throw core::DiagnosticError("RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE");
#endif
    }
    if (!isSignaled())
        throw core::DiagnosticError(state_->diagnostic);
}

WebGpuPrototypeBatch::WebGpuPrototypeBatch(rhi::PrototypeCommandBatchBudget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry)
    : budget_(budget), quota_(std::move(quota)), registry_(std::move(registry)),
      records_(budget.maxCommands), completion_(core::makeShared<Completion>()),
      fence_(core::makeShared<Fence>(completion_))
{
    for (auto& listener : listeners_)
        listener.owner = this;
}

WebGpuPrototypeBatch::~WebGpuPrototypeBatch()
{
    if (!completion_->terminal) {
        completion_->terminal = true;
        completion_->diagnostic = "RHI-PROTOTYPE-BATCH-DISCARDED";
    }
    for (auto& listener : listeners_)
        Receipt::release(listener.receipt);
    if (lossRegistration_) {
        detachWebGpuNativeDeviceLossRegistration(lossRegistration_);
        releaseWebGpuNativeDeviceLossRegistration(lossRegistration_);
    }
    if (scopeClaimed_)
        Receipt::releaseScopeTransaction(device_, this);
}

bool WebGpuPrototypeBatch::reject(const char* id, core::pmr::String& message)
{
    message = latch(id);
    completion_->terminal = true;
    completion_->diagnostic = failure_;
    return false;
}

bool WebGpuPrototypeBatch::textureLeaseValid(
    const std::shared_ptr<WebGpuNativeTexturePayload>& texture) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return !texture || !texture->browserSurfaceTexture
        || (texture->acquiredImageValid
            && webGpuNativeExecutionTurnMatches(texture->acquiredExecutionTurn));
#else
    (void)texture;
    return true; // no native image exists on this host; execution remains closed
#endif
}

bool WebGpuPrototypeBatch::validateLeases(core::pmr::String& message)
{
    for (const auto& r : records_.values()) {
        if (!textureLeaseValid(r.texture))
            return reject("RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED", message);
        for (const auto& texture : r.colors)
            if (!textureLeaseValid(texture))
                return reject("RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED", message);
        for (const auto& texture : r.resolves)
            if (!textureLeaseValid(texture))
                return reject("RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED", message);
        if (r.descriptors)
            for (const auto& binding : r.descriptors->bindings)
                if (binding.textureView && !textureLeaseValid(binding.textureView->parentTexture))
                    return reject("RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED", message);
    }
    return true;
}

bool WebGpuPrototypeBatch::charge(std::uint64_t bytes, core::pmr::String& message)
{
    if (bytes > budget_.maxRetainedGpuBytes - gpuBytes_)
        return reject("RHI-PROTOTYPE-BATCH-GPU-BUDGET", message);
    gpuBytes_ += bytes;
    return true;
}

bool WebGpuPrototypeBatch::resource(Record& r,
    rhi::GpuResourceId id,
    const void* original,
    bool texture,
    core::pmr::String& message)
{
    auto found = registry_ ? registry_->find(id) : std::nullopt;
    if (!found
        || (texture ? found->texture.get() : static_cast<const void*>(found->buffer.get()))
            != original)
        return reject("RHI-PROTOTYPE-BATCH-ORIGINAL-RESOURCE", message);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!found->registeredNativeDevice
        || found->registeredNativeDevice != reinterpret_cast<std::uintptr_t>(device_))
        return reject("RHI-PROTOTYPE-BATCH-RESOURCE-DEVICE", message);
#endif
    if (!charge(found->allocationBytes, message))
        return false;
    if (texture) {
        r.texture = found->texture;
        if (!r.texture || !r.texture->sharedOwnerMatches(r.texture))
            return reject("RHI-PROTOTYPE-BATCH-ORIGINAL-RESOURCE", message);
        if (!textureLeaseValid(r.texture))
            return reject("RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED", message);
        if (const auto* error = r.texture->contentAccessPolicy().ordinaryAccessDiagnostic())
            return reject(error, message);
    } else {
        r.buffer = found->buffer;
        if (!r.buffer || !r.buffer->sharedOwnerMatches(r.buffer))
            return reject("RHI-PROTOTYPE-BATCH-ORIGINAL-RESOURCE", message);
    }
    return true;
}

bool WebGpuPrototypeBatch::append(Record&& r, core::pmr::String& message)
{
    if (failure_ || !capturing() || !records_.canAppend())
        return reject("RHI-PROTOTYPE-BATCH-COMMAND-CAPACITY-OR-STATE", message);
    switch (r.op) {
    case Op::RenderBegin:
        if (const auto* id = rhi::semantics::PrototypeCommandBatchPassContract::validate(r.render))
            return reject(id, message);
        [[fallthrough]];
    case Op::ComputeBegin:
        if (pass_ != Pass::None)
            return reject("RHI-PROTOTYPE-BATCH-PASS-ORDER", message);
        pass_ = r.op == Op::RenderBegin ? Pass::Render : Pass::Compute;
        pipelineBound_ = false;
        break;
    case Op::RenderEnd:
    case Op::ComputeEnd:
        if (pass_ != (r.op == Op::RenderEnd ? Pass::Render : Pass::Compute))
            return reject("RHI-PROTOTYPE-BATCH-PASS-ORDER", message);
        pass_ = Pass::None;
        pipelineBound_ = false;
        break;
    case Op::GraphicsPipeline:
    case Op::ComputePipeline:
        if (pass_ != (r.op == Op::GraphicsPipeline ? Pass::Render : Pass::Compute)
            || (r.op == Op::GraphicsPipeline && !r.graphics)
            || (r.op == Op::ComputePipeline && !r.pipeline)
            || (r.graphics && !r.graphics->sharedOwnerMatches(r.graphics))
            || (r.pipeline && !r.pipeline->sharedOwnerMatches(r.pipeline)))
            return reject("RHI-PROTOTYPE-BATCH-PIPELINE-OWNER-OR-ORDER", message);
        pipelineBound_ = true;
        break;
    case Op::RenderDescriptors:
    case Op::ComputeDescriptors:
        if (!pipelineBound_ || !r.descriptors || r.descriptors->tableBacked
            || !r.descriptors->sharedOwnerMatches(r.descriptors)
            || pass_ != (r.op == Op::RenderDescriptors ? Pass::Render : Pass::Compute))
            return reject("RHI-PROTOTYPE-BATCH-DESCRIPTOR-OWNER-OR-ORDER", message);
        if (const auto* error = r.descriptors->ordinaryTextureAccessDiagnostic())
            return reject(error, message);
        for (const auto& binding : r.descriptors->bindings) {
            if (binding.textureView && !textureLeaseValid(binding.textureView->parentTexture))
                return reject("RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED", message);
            auto found = registry_->find(binding.write.resource);
            if (!found || (binding.buffer && found->buffer != binding.buffer)
                || (binding.textureView && found->textureView != binding.textureView)
                || (binding.sampler && found->sampler != binding.sampler))
                return reject("RHI-PROTOTYPE-BATCH-DESCRIPTOR-RESOURCE", message);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!found->registeredNativeDevice
                || found->registeredNativeDevice != reinterpret_cast<std::uintptr_t>(device_))
                return reject("RHI-PROTOTYPE-BATCH-RESOURCE-DEVICE", message);
#endif
            if (!charge(found->allocationBytes, message))
                return false;
        }
        break;
    case Op::BufferBarrier:
    case Op::TextureBarrier:
        if (const auto* id = rhi::semantics::PrototypeCommandBatchBarrierLowering::transition(
                r.barrier, r.op == Op::TextureBarrier))
            return reject(id, message);
        if (pass_ != Pass::None)
            return reject("RHI-PROTOTYPE-BATCH-BARRIER-IN-PASS", message);
        break;
    case Op::BufferMemory:
    case Op::TextureMemory:
        if (const auto* id = rhi::semantics::PrototypeCommandBatchBarrierLowering::memory(
                r.memory, r.memoryState, r.op == Op::TextureMemory))
            return reject(id, message);
        if (pass_ != Pass::None)
            return reject("RHI-PROTOTYPE-BATCH-BARRIER-IN-PASS", message);
        break;
    case Op::Draw:
    case Op::Dispatch:
        if (!pipelineBound_ || pass_ != (r.op == Op::Draw ? Pass::Render : Pass::Compute))
            return reject("RHI-PROTOTYPE-BATCH-WORK-ORDER", message);
        break;
    case Op::Viewport:
    case Op::Scissor:
        if (pass_ != Pass::Render)
            return reject("RHI-PROTOTYPE-BATCH-RASTER-ORDER", message);
        break;
    case Op::Push:
        if (!pipelineBound_ || pass_ == Pass::None)
            return reject("RHI-PROTOTYPE-BATCH-PUSH-ORDER", message);
        break;
    case Op::PushDebug:
        ++debugDepth_;
        break;
    case Op::PopDebug:
        if (!debugDepth_)
            return reject("RHI-PROTOTYPE-BATCH-DEBUG-ORDER", message);
        --debugDepth_;
        break;
    case Op::Marker:
        break;
    }
    return records_.append(std::move(r));
}

bool WebGpuPrototypeBatch::openScopes(
    WebGpuNativeCommandPayload& command, std::size_t first, core::pmr::String& message)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    device_ = command.impl_->device;
    queue_ = command.impl_->queue;
    if (failure_ || !device_ || !queue_ || webGpuNativeDeviceLost()
        || !Receipt::claimScopeTransaction(device_, this))
        return reject("RHI-PROTOTYPE-BATCH-NATIVE-SCOPE-UNAVAILABLE", message);
    scopeClaimed_ = true;
    for (std::size_t i = first; i < first + WebGpuErrorScopeContract::count; ++i) {
        auto& l = listeners_[i];
        auto prepared = Receipt::prepare(device_, queue_, Receipt::Kind::Scope, &l, &completed);
        l.receipt = prepared.release();
        if (!l.receipt)
            return reject("RHI-PROTOTYPE-BATCH-SCOPE-PREPARATION", message);
    }
    if (!Receipt::pushScopes(device_)) {
        nativeUncertain_ = true;
        return reject("RHI-PROTOTYPE-BATCH-SCOPE-INVOKE-UNCERTAIN", message);
    }
    scopesOpen_ = true;
    return true;
#else
    (void)command;
    (void)first;
    return reject("RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE", message);
#endif
}

bool WebGpuPrototypeBatch::closeScopes(std::size_t first, core::pmr::String& message)
{
    bool success = true;
    if (scopesOpen_) {
        for (std::size_t i = first; i < first + WebGpuErrorScopeContract::count; ++i) {
            if (!Receipt::bind(listeners_[i].receipt)) {
                listeners_[i].drained = true;
                nativeUncertain_ = true;
                success = reject("RHI-PROTOTYPE-BATCH-SCOPE-BIND", message);
            }
        }
        scopesOpen_ = false;
    }
    if (scopeClaimed_) {
        Receipt::releaseScopeTransaction(device_, this);
        scopeClaimed_ = false;
    }
    return success;
}

bool WebGpuPrototypeBatch::wait(std::size_t first, std::size_t count, core::pmr::String& message)
{
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        bool ready = true;
        for (std::size_t i = first; i < first + count; ++i)
            ready = ready && listeners_[i].drained;
        if (ready)
            return !failure_ || reject(failure_, message);
        if (webGpuNativeDeviceLost())
            return reject("RHI-PROTOTYPE-BATCH-DEVICE-LOST", message);
        if (std::chrono::steady_clock::now() - start
            >= std::chrono::nanoseconds(budget_.timeoutNanoseconds))
            return reject("RHI-PROTOTYPE-BATCH-DEADLINE", message);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        emscripten_sleep(0);
#else
        return reject("RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE", message);
#endif
    }
}

void WebGpuPrototypeBatch::completed(void* userdata, Receipt::Outcome outcome) noexcept
{
    auto& l = *static_cast<Listener*>(userdata);
    auto& self = *l.owner;
    const auto lease = self.callbackLease_;
    const auto kind = &l == &self.listeners_[0] ? Receipt::Kind::Queue : Receipt::Kind::Scope;
    if (!Receipt::matchesDelivery(
            l.receipt, self.device_, self.queue_, kind, &l, &completed, outcome)) {
        self.failure_ = "RHI-PROTOTYPE-BATCH-CALLBACK-IDENTITY";
        return;
    }
    l.drained = true;
    if (outcome != Receipt::Outcome::Success)
        self.failure_ = "RHI-PROTOTYPE-BATCH-NATIVE-VALIDATION";
    if (self.failure_) {
        self.completion_->terminal = true;
        self.completion_->diagnostic = self.failure_;
    }
    self.releaseIfDrained();
}

void WebGpuPrototypeBatch::releaseIfDrained() noexcept
{
    bool drained = true;
    for (const auto& l : listeners_)
        drained = drained && (!l.receipt || l.drained);
    if ((nativeUncertain_ && !physicalLoss_) || !drained
        || (submitted_ && !listeners_[0].drained && !physicalLoss_))
        return;
    if (submitted_ && !completion_->terminal) {
        completion_->nativeSuccess = !failure_ && !physicalLoss_;
        completion_->diagnostic = failure_;
        completion_->terminal = true;
    }
    if (lossRegistration_) {
        detachWebGpuNativeDeviceLossRegistration(lossRegistration_);
        releaseWebGpuNativeDeviceLossRegistration(lossRegistration_);
        lossRegistration_ = 0;
    }
    callbackLease_.reset();
}

void WebGpuPrototypeBatch::lost(void* userdata) noexcept
{
    auto& self = *static_cast<WebGpuPrototypeBatch*>(userdata);
    const auto lease = self.callbackLease_;
    self.physicalLoss_ = true;
    if (self.scopeClaimed_) {
        Receipt::releaseScopeTransaction(self.device_, &self);
        self.scopeClaimed_ = false;
    }
    self.scopesOpen_ = false;
    self.failure_ = "RHI-PROTOTYPE-BATCH-DEVICE-LOST";
    self.completion_->terminal = true;
    self.completion_->diagnostic = self.failure_;
    for (auto& l : self.listeners_) {
        Receipt::release(l.receipt);
        l.receipt = 0;
        l.drained = true;
    }
    self.releaseIfDrained();
}

bool WebGpuPrototypeBatch::replay(WebGpuNativeCommandPayload& command, core::pmr::String& message)
{
    if (failure_ || sealed_ || pass_ != Pass::None || debugDepth_ || records_.values().empty())
        return reject("RHI-PROTOTYPE-BATCH-INCOMPLETE", message);
    if (!validateLeases(message))
        return false;
    core::CpuMemoryQuotaScope quota(quota_);
    sealed_ = true;
    callbackLease_ = command_.lock();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    device_ = command.nativeQueue() ? command.impl_->device : nullptr;
    queue_ = command.nativeQueue();
    lossRegistration_ = prepareWebGpuNativeDeviceLossRegistration(device_, this, &lost);
    if (!lossRegistration_) {
        cancelPrepared();
        return reject("RHI-PROTOTYPE-BATCH-LOSS-PREPARATION", message);
    }
    auto& l = listeners_[0];
    auto prepared = Receipt::prepare(device_, queue_, Receipt::Kind::Queue, &l, &completed);
    l.receipt = prepared.release();
    if (!l.receipt || !openScopes(command, 1, message)) {
        cancelPrepared();
        return reject("RHI-PROTOTYPE-BATCH-QUEUE-PREPARATION", message);
    }
#else
    callbackLease_.reset();
    return reject("RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE", message);
#endif
    replaying_ = true;
    bool success = true;
    try {
        for (const auto& r : records_.values()) {
            replayRecord_ = &r;
            switch (r.op) {
            case Op::BufferBarrier:
                success = command.recordBufferBarrier(*r.buffer, r.barrier, message);
                break;
            case Op::TextureBarrier:
                success = command.recordTextureBarrier(*r.texture, r.barrier, message);
                break;
            case Op::BufferMemory:
                success = command.recordBufferMemoryBarrier(*r.buffer, r.memory, message);
                break;
            case Op::TextureMemory:
                success = command.recordTextureMemoryBarrier(*r.texture, r.memory, message);
                break;
            case Op::RenderBegin: {
                core::pmr::Vector<const WebGpuNativeTexturePayload*> colors, resolves;
                for (const auto& v : r.colors)
                    colors.push_back(v.get());
                for (const auto& v : r.resolves)
                    resolves.push_back(v.get());
                success = command.recordRenderPassBegin(
                    r.render, colors, resolves, r.texture.get(), message);
                break;
            }
            case Op::RenderEnd:
                success = command.recordRenderPassEnd(message);
                break;
            case Op::ComputeBegin:
                success = command.recordComputePassBegin(r.compute, message);
                break;
            case Op::ComputeEnd:
                success = command.recordComputePassEnd(message);
                break;
            case Op::GraphicsPipeline:
                success = command.recordGraphicsPipelineBind(*r.graphics, message);
                break;
            case Op::ComputePipeline:
                success = command.recordComputePipelineBind(*r.pipeline, message);
                break;
            case Op::RenderDescriptors:
                success = command.recordRenderDescriptorSetBind(
                    r.args[0], r.descriptors, r.offsets.data(), r.offsets.size(), message);
                break;
            case Op::ComputeDescriptors:
                success = command.recordDescriptorSetBind(
                    r.args[0], r.descriptors, r.offsets.data(), r.offsets.size(), message);
                break;
            case Op::Viewport:
                success = command.recordSetViewport(r.viewport, message);
                break;
            case Op::Scissor:
                success = command.recordSetScissor(r.scissor, message);
                break;
            case Op::Push:
                success = command.recordPushConstants(
                    r.stages, r.args[0], r.bytes.size(), r.bytes.data(), message);
                break;
            case Op::Draw:
                success = command.recordDraw(r.args[0], r.args[1], r.args[2], r.args[3], message);
                break;
            case Op::Dispatch:
                success = command.recordDispatch(r.args[0], r.args[1], r.args[2], message);
                break;
            case Op::PushDebug:
                success = command.recordPushDebugGroup(r.label, message);
                break;
            case Op::PopDebug:
                success = command.recordPopDebugGroup(message);
                break;
            case Op::Marker:
                success = command.recordInsertDebugMarker(r.label, message);
                break;
            }
            if (!success)
                break;
        }
        if (success)
            success = command.finish(message);
    } catch (const std::bad_alloc&) {
        success = reject("RHI-PROTOTYPE-BATCH-CPU-BUDGET", message);
    }
    replaying_ = false;
    replayRecord_ = nullptr;
    if (!success && !failure_)
        failure_ = "RHI-PROTOTYPE-BATCH-REPLAY-FAILED";
    if (!success) {
        Receipt::release(listeners_[0].receipt);
        listeners_[0].receipt = 0;
        closeScopes(1, message);
        wait(1, WebGpuErrorScopeContract::count, message);
        releaseIfDrained();
    }
    // No yield here: a browser canvas texture expires at the task boundary.
    // Submit closes the same scope transaction before the first Asyncify wait.
    return success;
}

bool WebGpuPrototypeBatch::submit(
    const std::shared_ptr<WebGpuNativeCommandPayload>& command, core::pmr::String& message)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (webGpuNativeDeviceLost()) {
        reject("RHI-PROTOTYPE-BATCH-DEVICE-LOST", message);
        discard();
        return false;
    }
    const auto admission = rhi::PrototypeCommandBatchContract::lowering(
        rhi::Backend::WebGPU, budget_, webGpuNativePrototypeBatchCapabilities());
    if (!admission.executable) {
        reject(admission.diagnostic, message);
        discard();
        return false;
    }
    if (failure_ || !sealed_ || !command || !command->impl_ || !command->impl_->finished
        || command->impl_->submitted || submitted_ || !command->impl_->commandBuffer)
        return reject("RHI-PROTOTYPE-BATCH-SUBMISSION-STATE", message);
    core::CpuMemoryQuotaScope quota(quota_);
    auto& l = listeners_[0];
    if (!lossRegistration_ || !scopesOpen_ || !l.receipt)
        return reject("RHI-PROTOTYPE-BATCH-SUBMISSION-PREPARATION", message);
    if (!validateLeases(message)) {
        discard();
        return false;
    }
    callbackLease_ = command;
    submitted_ = command->impl_->submitted = true;
    completion_->accepted = true;
    if (!Receipt::invoke(queue_, command->impl_->commandBuffer)) {
        nativeUncertain_ = true;
        reject("RHI-PROTOTYPE-BATCH-QUEUE-INVOKE-UNCERTAIN", message);
    }
    if (!Receipt::bind(l.receipt)) {
        nativeUncertain_ = true;
        reject("RHI-PROTOTYPE-BATCH-QUEUE-BIND", message);
    }
    closeScopes(1, message);
    const bool success = wait(0, listeners_.size(), message);
    releaseIfDrained();
    return success;
#else
    (void)command;
    return reject("RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE", message);
#endif
}

void WebGpuPrototypeBatch::cancelPrepared() noexcept
{
    // No submit or callback binding has occurred. A thrown scope invocation
    // quarantines the exact owners and transaction until physical device loss.
    if (nativeUncertain_)
        return;
    for (auto& l : listeners_) {
        Receipt::release(l.receipt);
        l.receipt = 0;
        l.drained = true;
    }
    if (scopeClaimed_) {
        Receipt::releaseScopeTransaction(device_, this);
        scopeClaimed_ = false;
    }
    releaseIfDrained();
}

void WebGpuPrototypeBatch::discard() noexcept
{
    if (!submitted_ && !completion_->terminal) {
        completion_->terminal = true;
        completion_->diagnostic = "RHI-PROTOTYPE-BATCH-DISCARDED";
    }
    if (!sealed_ || submitted_ || !scopesOpen_)
        return;
    core::pmr::String message;
    Receipt::release(listeners_[0].receipt);
    listeners_[0].receipt = 0;
    closeScopes(1, message);
    // Native scope listeners keep their original ownership until genuine drain.
    releaseIfDrained();
}

void WebGpuNativeCommandPayload::discardPrototypeBatch() noexcept
{
    if (prototypeBatchPrepared())
        impl_->prototypeBatch->discard();
}

bool WebGpuNativeCommandPayload::preparePrototypeBatch(
    const rhi::PrototypeCommandBatchContract::Decision& decision,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    core::pmr::String& message,
    const std::shared_ptr<WebGpuNativeCommandPayload>& self)
{
    const auto& b = decision.budget;
    const auto actual = rhi::PrototypeCommandBatchContract::lowering(
        rhi::Backend::WebGPU, b, webGpuNativePrototypeBatchCapabilities());
    if (!decision.executable || decision.mode != rhi::PrototypeCommandBatchMode::Prototype
        || decision.diagnostic || !actual.executable) {
        message = actual.diagnostic ? actual.diagnostic : "RHI-PROTOTYPE-BATCH-DECISION-REQUIRED";
        return false;
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    message = "RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE";
    return false;
#endif
    if (!impl_ || impl_->prototypeBatch || !registry || self.get() != this
        || !sharedOwnerMatches(self)) {
        message = "RHI-PROTOTYPE-BATCH-PREPARATION-STATE";
        return false;
    }
    try {
        auto quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuBytes);
        core::CpuMemoryQuotaScope scope(quota);
        impl_->prototypeBatch =
            core::makeUnique<WebGpuPrototypeBatch>(b, quota, std::move(registry));
        impl_->prototypeBatch->command_ = self;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        impl_->prototypeBatch->device_ = impl_->device;
        impl_->prototypeBatch->queue_ = impl_->queue;
#endif
        return true;
    } catch (const std::bad_alloc&) {
        message = "RHI-PROTOTYPE-BATCH-CPU-BUDGET";
        return false;
    }
}

bool WebGpuNativeCommandPayload::prototypeBatchPrepared() const noexcept
{
    return impl_ && impl_->prototypeBatch;
}

bool WebGpuNativeCommandPayload::prototypeBatchSubmitted() const noexcept
{
    return prototypeBatchPrepared() && impl_->submitted;
}

std::shared_ptr<rhi::GpuFence> WebGpuNativeCommandPayload::prototypeBatchFence() const
{
    return prototypeBatchPrepared() ? impl_->prototypeBatch->fence() : nullptr;
}

bool WebGpuNativeCommandPayload::submitPrototypeBatch(
    const std::shared_ptr<WebGpuNativeCommandPayload>& self,
    rhi::GpuFence* fence,
    const std::shared_ptr<WebGpuNativeCommandStats>& stats,
    core::pmr::String& message)
{
    if (!prototypeBatchPrepared() || self.get() != this || !sharedOwnerMatches(self)
        || impl_->stats != stats || fence != prototypeBatchFence().get()) {
        message = "RHI-PROTOTYPE-BATCH-SUBMISSION-OWNER";
        return false;
    }
    return impl_->prototypeBatch->submit(self, message);
}

bool WebGpuNativeCommandPayload::recordGraphicsPipelineBind(
    const std::shared_ptr<WebGpuNativeRenderPipelinePayload>& pipeline, core::pmr::String& message)
{
    if (prototypeBatchPrepared() && impl_->prototypeBatch->capturing()) {
        auto& b = *impl_->prototypeBatch;
        return b.capture(
            WebGpuPrototypeBatch::Op::GraphicsPipeline,
            [&](auto& r) {
                r.graphics = pipeline;
                return true;
            },
            message);
    }
    return pipeline && recordGraphicsPipelineBind(*pipeline, message);
}

bool WebGpuNativeCommandPayload::recordComputePipelineBind(
    const std::shared_ptr<WebGpuNativeComputePipelinePayload>& pipeline, core::pmr::String& message)
{
    if (prototypeBatchPrepared() && impl_->prototypeBatch->capturing()) {
        auto& b = *impl_->prototypeBatch;
        return b.capture(
            WebGpuPrototypeBatch::Op::ComputePipeline,
            [&](auto& r) {
                r.pipeline = pipeline;
                return true;
            },
            message);
    }
    return pipeline && recordComputePipelineBind(*pipeline, message);
}

bool WebGpuNativeCommandPayload::recordPushDebugGroup(
    std::string_view label, core::pmr::String& diagnostic)
{
    if (impl_ && impl_->prototypeBatch && impl_->prototypeBatch->capturing()) {
        auto& b = *impl_->prototypeBatch;
        return b.capture(
            WebGpuPrototypeBatch::Op::PushDebug,
            [&](auto& r) {
                r.label = label;
                return true;
            },
            diagnostic);
    }
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder) {
        diagnostic = "RHI-PROTOTYPE-BATCH-MARKER-ENCODER";
        return false;
    }
    if (impl_->renderPass)
        wgpuRenderPassEncoderPushDebugGroup(impl_->renderPass, webGpuNativeStringView(label));
    else if (impl_->computePass)
        wgpuComputePassEncoderPushDebugGroup(impl_->computePass, webGpuNativeStringView(label));
    else
        wgpuCommandEncoderPushDebugGroup(impl_->encoder, webGpuNativeStringView(label));
#endif
    return true;
}

bool WebGpuNativeCommandPayload::recordPopDebugGroup(core::pmr::String& diagnostic)
{
    if (impl_ && impl_->prototypeBatch && impl_->prototypeBatch->capturing()) {
        auto& b = *impl_->prototypeBatch;
        return b.capture(
            WebGpuPrototypeBatch::Op::PopDebug,
            [&](auto& r) {
                return true;
            },
            diagnostic);
    }
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder) {
        diagnostic = "RHI-PROTOTYPE-BATCH-MARKER-ENCODER";
        return false;
    }
    if (impl_->renderPass)
        wgpuRenderPassEncoderPopDebugGroup(impl_->renderPass);
    else if (impl_->computePass)
        wgpuComputePassEncoderPopDebugGroup(impl_->computePass);
    else
        wgpuCommandEncoderPopDebugGroup(impl_->encoder);
#endif
    return true;
}

bool WebGpuNativeCommandPayload::recordInsertDebugMarker(
    std::string_view label, core::pmr::String& diagnostic)
{
    if (impl_ && impl_->prototypeBatch && impl_->prototypeBatch->capturing()) {
        auto& b = *impl_->prototypeBatch;
        return b.capture(
            WebGpuPrototypeBatch::Op::Marker,
            [&](auto& r) {
                r.label = label;
                return true;
            },
            diagnostic);
    }
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder) {
        diagnostic = "RHI-PROTOTYPE-BATCH-MARKER-ENCODER";
        return false;
    }
    if (impl_->renderPass)
        wgpuRenderPassEncoderInsertDebugMarker(impl_->renderPass, webGpuNativeStringView(label));
    else if (impl_->computePass)
        wgpuComputePassEncoderInsertDebugMarker(impl_->computePass, webGpuNativeStringView(label));
    else
        wgpuCommandEncoderInsertDebugMarker(impl_->encoder, webGpuNativeStringView(label));
#endif
    return true;
}
} // namespace arp::rhi_webgpu
