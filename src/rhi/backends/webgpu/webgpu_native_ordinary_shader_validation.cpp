#include "rhi/backends/webgpu/webgpu_native_ordinary_shader_validation.h"

#include "core/ownership/external_object_slot.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"

namespace arp::rhi_webgpu {
const char* WebGpuOrdinaryShaderSizeValidation::diagnostic() const noexcept
{
    if (deviceLost_->load(std::memory_order_acquire))
        return "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST";
    if (const auto* id = groups_->validate())
        return id;
    if (vertices_ && !vertices_->matches())
        return "RHI-WEBGPU-ORDINARY-VALIDATION-VERTEX-MUTATED";
    switch (status_) {
    case Status::Pending:
        return "RHI-WEBGPU-ORDINARY-VALIDATION-PENDING";
    case Status::Validated:
        return nullptr;
    case Status::Rejected:
        return failure_;
    case Status::DeviceLost:
        return "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST";
    }
    return "RHI-WEBGPU-ORDINARY-VALIDATION-STATUS";
}

const char* WebGpuOrdinaryShaderSizeValidationOwner::validateBudget(
    const WebGpuOrdinaryShaderValidationBudget& budget) noexcept
{
    if (budget.contractVersion != 1u)
        return "RHI-WEBGPU-ORDINARY-VALIDATION-VERSION";
    if (!budget.prototypeOptIn)
        return "RHI-WEBGPU-ORDINARY-VALIDATION-OPT-IN";
    if (!budget.maxPendingProbes || budget.maxPendingCallbacks < scopeCount
        || !budget.maxCpuMetadataBytes)
        return "RHI-WEBGPU-ORDINARY-VALIDATION-BUDGET";
    return nullptr;
}

core::UniquePtr<WebGpuOrdinaryShaderSizeValidationOwner>
WebGpuOrdinaryShaderSizeValidationOwner::prepare(
    std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline,
    const WebGpuOrdinaryShaderValidationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)pipeline;
    diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!pipeline || !pipeline->hasQuotaBoundSharedOwner(pipeline)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-PIPELINE-OWNER";
        return {};
    }
    diagnostic = pipeline->validate();
    if (diagnostic)
        return {};
    const auto& issued = pipeline->contract();
    if (issued.semantics().groupCount > budget.maxGroups
        || issued.graphics().vertexBufferCount > budget.maxVertexStreams
        || issued.graphics().colorFormats.size() > budget.maxColorTargets) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-CAPACITY";
        return {};
    }
    if (issued.graphics().graphics ? (!budget.maxBundleEncoders || !budget.maxBundles)
                                   : (!budget.maxCommandEncoders || !budget.maxComputePasses
                                         || !budget.maxCommandBuffers)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CAPACITY";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        return core::makeUnique<WebGpuOrdinaryShaderSizeValidationOwner>(
            ConstructionKey{}, std::move(pipeline), budget, quota);
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-ALLOCATION";
        return {};
    }
#endif
}

WebGpuOrdinaryShaderSizeValidationOwner::~WebGpuOrdinaryShaderSizeValidationOwner() = default;

void WebGpuOrdinaryShaderSizeValidationOwner::lost(void* userdata) noexcept
{
    auto* observer =
        static_cast<std::shared_ptr<WebGpuOrdinaryShaderSizeValidationOwner>*>(userdata);
    if (observer && *observer) {
        auto& owner = **observer;
        owner.deviceLost_->store(true, std::memory_order_release);
        // Invalidate only graphs associated with this original device.
        owner.retained_.invalidate([](Root& root) noexcept {
            root.result->status_ = WebGpuOrdinaryShaderSizeValidation::Status::DeviceLost;
        });
    }
    core::destroyCpuAllocatedUnique<std::shared_ptr<WebGpuOrdinaryShaderSizeValidationOwner>>(
        observer);
}
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void WebGpuOrdinaryShaderSizeValidationOwner::complete(WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView,
    void* userdata,
    void*) noexcept
{
    auto* callback = static_cast<Callback*>(userdata);
    if (!callback)
        return;
    auto& owner = *callback->owner;
    if (auto* root = owner.retained_.get(callback->ticket)) {
        root->callbackStatusSuccess =
            root->callbackStatusSuccess && status == WGPUPopErrorScopeStatus_Success;
        root->noErrors = root->noErrors && type == WGPUErrorType_NoError;
        if (--root->remainingCallbacks) {
            core::destroyCpuAllocatedUnique<Callback>(callback);
            return;
        }
        root->result->failure_ =
            callbackDiagnostic(root->callbackStatusSuccess, root->noErrors, root->result->failure_);
        root->result->status_ = !root->result->failure_
            ? WebGpuOrdinaryShaderSizeValidation::Status::Validated
            : WebGpuOrdinaryShaderSizeValidation::Status::Rejected;
        // Callback delivery retires only already-released, never-submitted
        // probe objects. This true is not queue/GPU completion evidence.
        owner.retained_.resolve(callback->ticket, true);
    }
    core::destroyCpuAllocatedUnique<Callback>(callback);
}
#endif
std::shared_ptr<WebGpuOrdinaryShaderSizeValidation> WebGpuOrdinaryShaderSizeValidationOwner::probe(
    const std::shared_ptr<WebGpuOrdinaryShaderSizeValidationOwner>& owner,
    std::shared_ptr<const WebGpuOrdinaryPhysicalGroups> groups,
    core::UniquePtr<WebGpuOrdinaryResourceProjection> vertices,
    const rhi::GpuResourceId* ids,
    std::size_t count,
    const char*& diagnostic)
{
    diagnostic = nullptr;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)owner;
    (void)groups;
    (void)vertices;
    (void)ids;
    (void)count;
    diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!owner || !owner->hasQuotaBoundSharedOwner(owner) || !owner->retained_.accepting()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-OWNER";
        return {};
    }
    if (!groups || !groups->hasQuotaBoundSharedOwner(groups)
        || groups->pipeline_.get() != owner->pipeline_.get()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-GROUP-OWNER";
        return {};
    }
    if ((diagnostic = groups->validate()))
        return {};
    if (owner->retained_.pending() >= owner->budget_.maxPendingProbes
        || owner->retained_.pending() >= owner->budget_.maxPendingCallbacks / scopeCount) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-CAPACITY";
        return {};
    }
    const auto& issued = owner->pipeline_->contract();
    auto device = static_cast<WGPUDevice>(issued.nativeDevice());
    if (count != issued.graphics().vertexBufferCount || (count && (!ids || !vertices))
        || (vertices && !vertices->matches())) {
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-VERTEX-COVERAGE";
        return {};
    }
    if (vertices)
        for (const auto& resource : vertices->resources()) {
            bool used = false;
            for (std::size_t i = 0u; i < count; ++i)
                if (ids[i] == resource.id())
                    used = true;
            if (!used) {
                diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-VERTEX-COVERAGE";
                return {};
            }
        }
    for (std::size_t i = 0u; i < count; ++i) {
        const auto* resource = vertices->find(ids[i]);
        if (!resource || resource->nativeDevice() != device || !resource->backing().buffer
            || (resource->backing().bufferDesc.usage & rhi::ResourceUsage::VertexBuffer)
                == rhi::ResourceUsage::None
            || !resource->backing().bufferDesc.sizeBytes) {
            diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-VERTEX-RESOURCE";
            return {};
        }
    }
    Pool::Ticket ticket;
    try {
        core::CpuMemoryQuotaScope scope(owner->quota_);
        auto unique = core::makeUnique<WebGpuOrdinaryShaderSizeValidation>(
            WebGpuOrdinaryShaderSizeValidation::ConstructionKey{},
            groups,
            std::move(vertices),
            owner->deviceLost_);
        unique->vertexIds_.reserve(count);
        for (std::size_t i = 0u; i < count; ++i)
            unique->vertexIds_.push_back(ids[i]);
        auto result = WebGpuOrdinaryShaderSizeValidation::shareOriginal(std::move(unique));
        auto root = core::makeUnique<Root>();
        root->result = result;
        std::array<core::UniquePtr<Callback>, scopeCount> callbacks;
        for (auto& callback : callbacks) {
            callback = core::makeUnique<Callback>();
            callback->owner = owner;
        }
        core::pmr::Vector<WGPUTextureFormat> colors;
        colors.reserve(issued.graphics().colorFormats.size());
        for (const auto color : issued.graphics().colorFormats) {
            const auto native = nativeTextureFormat(color);
            if (native == WGPUTextureFormat_Undefined) {
                diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-FORMAT";
                return {};
            }
            colors.push_back(native);
        }
        const auto depth = nativeTextureFormat(issued.graphics().depthStencilFormat);
        if (issued.graphics().depthStencilFormat != rhi::TextureFormat::Unknown
            && depth == WGPUTextureFormat_Undefined) {
            diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-FORMAT";
            return {};
        }
        ticket = owner->retained_.reserve(root);
        if (!ticket) {
            diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-CAPACITY";
            return {};
        }
        for (auto& callback : callbacks)
            callback->ticket = ticket;
        if (!owner->lossObserved_) {
            if (!observeWebGpuNativeOwnerDeviceLoss(owner, device, &lost)) {
                owner->retained_.cancel(ticket);
                diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-LOSS-OBSERVER";
                return {};
            }
            owner->lossObserved_ = true;
        }
        // All CPU metadata exists before any native probe object is created.
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Internal);
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_OutOfMemory);
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        if (issued.graphics().graphics) {
            auto desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = colors.size();
            desc.colorFormats = colors.empty() ? nullptr : colors.data();
            desc.depthStencilFormat = depth;
            desc.sampleCount = issued.graphics().sampleCount;
            desc.depthReadOnly = false;
            desc.stencilReadOnly = false;
            core::ExternalObjectSlot encoder([](void* object) noexcept {
                wgpuRenderBundleEncoderRelease(static_cast<WGPURenderBundleEncoder>(object));
            });
            core::ExternalObjectSlot bundle([](void* object) noexcept {
                wgpuRenderBundleRelease(static_cast<WGPURenderBundle>(object));
            });
            encoder.adopt(wgpuDeviceCreateRenderBundleEncoder(device, &desc));
            if (encoder.get()) {
                auto native = static_cast<WGPURenderBundleEncoder>(encoder.get());
                wgpuRenderBundleEncoderSetPipeline(native, owner->pipeline_->graphics()->pipeline);
                for (std::uint32_t i = 0u; i < groups->groups_.size(); ++i) {
                    const auto& group = groups->groups_[i];
                    wgpuRenderBundleEncoderSetBindGroup(native,
                        i,
                        group.originalGroup,
                        group.offsets.size(),
                        group.offsets.empty() ? nullptr : group.offsets.data());
                }
                for (std::uint32_t i = 0u; i < count; ++i) {
                    const auto& vertex = result->vertices_->find(ids[i])->backing();
                    wgpuRenderBundleEncoderSetVertexBuffer(
                        native, i, vertex.buffer->buffer, 0u, vertex.bufferDesc.sizeBytes);
                }
                wgpuRenderBundleEncoderDraw(native, 0u, 0u, 0u, 0u);
                auto finish = WGPU_RENDER_BUNDLE_DESCRIPTOR_INIT;
                bundle.adopt(wgpuRenderBundleEncoderFinish(native, &finish));
            }
            if (!encoder.get() || !bundle.get())
                result->failure_ = "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CREATE";
        } else {
            core::ExternalObjectSlot encoder([](void* object) noexcept {
                wgpuCommandEncoderRelease(static_cast<WGPUCommandEncoder>(object));
            });
            core::ExternalObjectSlot pass([](void* object) noexcept {
                wgpuComputePassEncoderRelease(static_cast<WGPUComputePassEncoder>(object));
            });
            core::ExternalObjectSlot buffer([](void* object) noexcept {
                wgpuCommandBufferRelease(static_cast<WGPUCommandBuffer>(object));
            });
            auto desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            encoder.adopt(wgpuDeviceCreateCommandEncoder(device, &desc));
            if (encoder.get()) {
                auto native = static_cast<WGPUCommandEncoder>(encoder.get());
                auto passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                pass.adopt(wgpuCommandEncoderBeginComputePass(native, &passDesc));
                if (pass.get()) {
                    auto compute = static_cast<WGPUComputePassEncoder>(pass.get());
                    wgpuComputePassEncoderSetPipeline(
                        compute, owner->pipeline_->compute()->pipeline);
                    for (std::uint32_t i = 0u; i < groups->groups_.size(); ++i) {
                        const auto& group = groups->groups_[i];
                        wgpuComputePassEncoderSetBindGroup(compute,
                            i,
                            group.originalGroup,
                            group.offsets.size(),
                            group.offsets.empty() ? nullptr : group.offsets.data());
                    }
                    wgpuComputePassEncoderDispatchWorkgroups(compute, 0u, 0u, 0u);
                    wgpuComputePassEncoderEnd(compute);
                }
                auto finish = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                buffer.adopt(wgpuCommandEncoderFinish(native, &finish));
            }
            if (!encoder.get() || !pass.get() || !buffer.get())
                result->failure_ = "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CREATE";
        }
        // The scopes above released every probe reference. This callback only
        // owns validation metadata and original resources, never queue work.
        owner->retained_.submit(ticket);
        for (auto& callback : callbacks) {
            auto info = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
            info.mode = WGPUCallbackMode_AllowSpontaneous;
            info.callback = &complete;
            info.userdata1 = callback.release();
            (void)wgpuDevicePopErrorScope(device, info);
        }
        return result;
    } catch (const std::bad_alloc&) {
        owner->retained_.cancel(ticket);
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        owner->retained_.cancel(ticket);
        diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-ALLOCATION";
        return {};
    }
#endif
}
} // namespace arp::rhi_webgpu
