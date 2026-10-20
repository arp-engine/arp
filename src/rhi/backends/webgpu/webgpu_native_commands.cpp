#include "core/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_conformance_clear.h"
#include "rhi/backends/webgpu/webgpu_descriptor_dynamic_offset_validation.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"
#include "rhi/backends/webgpu/webgpu_native_indirect_processor.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/backends/webgpu/webgpu_ordinary_completion_retention.h"
#include "rhi/frontend/bindless_snapshot_metadata.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/command_state_lowering.h"
#include "rhi/semantics/push_constant_upload.h"
#include "rhi/semantics/push_constant_validation.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {

namespace {

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
bool browserWebGpuEnvironmentAvailable()
{
    return emscripten_run_script_int("typeof navigator !== 'undefined' && !!navigator.gpu"
                                     "&& typeof Module !== 'undefined'"
                                     "&& !!Module.preinitializedWebGPUDevice"
                                     "&& !Module.webGpuDeviceLostInfo")
        != 0;
}

void releaseBuffer(WGPUBuffer& buffer)
{
    if (buffer) {
        if (!webGpuNativeDeviceLost()) {
            wgpuBufferDestroy(buffer);
        }
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }
}

void releaseBindGroupLayout(WGPUBindGroupLayout& layout)
{
    if (layout) {
        wgpuBindGroupLayoutRelease(layout);
        layout = nullptr;
    }
}

void releaseBindGroup(WGPUBindGroup& group)
{
    if (group) {
        wgpuBindGroupRelease(group);
        group = nullptr;
    }
}

void releaseTextureView(WGPUTextureView& view)
{
    if (view) {
        if (!webGpuNativeDeviceLost())
            wgpuTextureViewRelease(view);
        view = nullptr;
    }
}

void releaseRenderPass(WGPURenderPassEncoder& pass)
{
    if (pass) {
        if (!webGpuNativeDeviceLost())
            wgpuRenderPassEncoderRelease(pass);
        pass = nullptr;
    }
}

void releaseComputePass(WGPUComputePassEncoder& pass)
{
    if (pass) {
        if (!webGpuNativeDeviceLost())
            wgpuComputePassEncoderRelease(pass);
        pass = nullptr;
    }
}

void releaseCommandBuffer(WGPUCommandBuffer& commandBuffer)
{
    if (commandBuffer) {
        if (!webGpuNativeDeviceLost())
            wgpuCommandBufferRelease(commandBuffer);
        commandBuffer = nullptr;
    }
}

void releaseCommandEncoder(WGPUCommandEncoder& encoder)
{
    if (encoder) {
        if (!webGpuNativeDeviceLost())
            wgpuCommandEncoderRelease(encoder);
        encoder = nullptr;
    }
}

void releaseQueue(WGPUQueue& queue)
{
    if (queue) {
        if (!webGpuNativeDeviceLost())
            wgpuQueueRelease(queue);
        queue = nullptr;
    }
}

void releaseQuerySet(WGPUQuerySet& querySet)
{
    if (querySet) {
        if (!webGpuNativeDeviceLost()) {
            wgpuQuerySetDestroy(querySet);
            wgpuQuerySetRelease(querySet);
        }
        querySet = nullptr;
    }
}

void releaseDevice(WGPUDevice& device)
{
    if (device) {
        wgpuDeviceRelease(device);
        device = nullptr;
    }
}

void recordCommand(const std::shared_ptr<WebGpuNativeCommandStats>& stats)
{
    if (stats) {
        ++stats->nativeCommandRecordCount;
    }
}

void releaseRenderBundleEncoder(WGPURenderBundleEncoder& encoder)
{
    if (encoder) {
        wgpuRenderBundleEncoderRelease(encoder);
        encoder = nullptr;
    }
}

void releaseRenderBundle(WGPURenderBundle& bundle)
{
    if (bundle) {
        wgpuRenderBundleRelease(bundle);
        bundle = nullptr;
    }
}

struct WebGpuTimestampReadbackCallbackState {
    WGPUBuffer resolveBuffer = nullptr;
    WGPUBuffer readbackBuffer = nullptr;
    std::shared_ptr<WebGpuRhiQueryPoolPayload> pool;
    std::uint32_t queryIndex = 0u;
};

void completeWebGpuZeroBaseProducerRetention(
    WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*)
{
    auto* callback = static_cast<WebGpuProducerCompletionCallback*>(userdata);
    if (!callback)
        return;
    if (callback->owner) {
        callback->owner->retained.resolve(
            callback->ticket, status == WGPUQueueWorkDoneStatus_Success);
        if (status != WGPUQueueWorkDoneStatus_Success)
            EM_ASM({
                // clang-format off
                Module.webGpuPreparedProducerDiagnostic = "RHI-WEBGPU-PRODUCER-COMPLETION-UNPROVEN";
                // clang-format on
            });
    }
    core::destroyCpuAllocatedUnique<WebGpuProducerCompletionCallback>(callback);
}

void completeWebGpuProducerDeviceLoss(void* userdata)
{
    auto* observer = static_cast<std::shared_ptr<WebGpuProducerCompletionOwner>*>(userdata);
    if (observer && *observer) {
        (*observer)->retained.invalidate();
        (*observer)->ordinaryTransfers.invalidate();
    }
    core::destroyCpuAllocatedUnique<std::shared_ptr<WebGpuProducerCompletionOwner>>(observer);
}

void completeWebGpuOrdinaryTransferRetention(
    WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*)
{
    auto* callback = static_cast<WebGpuOrdinaryTransferCompletionCallback*>(userdata);
    if (!callback)
        return;
    if (callback->owner) {
        auto& pool = callback->owner->ordinaryTransfers;
        bool completed = status == WGPUQueueWorkDoneStatus_Success;
        if (auto* root = pool.get(callback->ticket)) {
            completed = completed && root->storage().retireAfterCompletion(true);
            pool.resolve(callback->ticket, completed);
            if (!completed)
                EM_ASM({
                    // clang-format off
                    Module.webGpuOrdinaryTransferDiagnostic = "RHI-WEBGPU-TRANSFER-COMPLETION-UNPROVEN";
                    // clang-format on
                });
        }
    }
    core::destroyCpuAllocatedUnique<WebGpuOrdinaryTransferCompletionCallback>(callback);
}

void completeWebGpuOrdinaryCompletionDeviceLoss(void* userdata)
{
    auto* observer = static_cast<std::shared_ptr<WebGpuOrdinaryCompletionOwner>*>(userdata);
    if (observer && *observer)
        (*observer)->retained.invalidate([](auto& graph) noexcept {
            graph.quarantineState();
        });
    core::destroyCpuAllocatedUnique<std::shared_ptr<WebGpuOrdinaryCompletionOwner>>(observer);
}

void completeWebGpuBindlessFaultReadback(
    WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*)
{
    auto* state = static_cast<WebGpuBindlessFaultReadbackCallbackState*>(userdata1);
    std::uint32_t bits = 0u;
    const bool read = state && state->destination && status == WGPUMapAsyncStatus_Success
        && wgpuBufferReadMappedRange(state->destination, 0u, &bits, sizeof(bits))
            == WGPUStatus_Success;
    if (state) {
        if (status == WGPUMapAsyncStatus_Success && state->destination)
            wgpuBufferUnmap(state->destination);
        state->lease.release();
        state->latch.reset();
        releaseBuffer(state->destination);
        if (state->result) {
            if (read)
                state->result->complete(bits);
            else
                state->result->fail();
        }
        core::destroyCpuAllocatedUnique<WebGpuBindlessFaultReadbackCallbackState>(state);
    }
}

void completeWebGpuBindlessFaultQueue(
    WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata1, void*)
{
    auto* state = static_cast<WebGpuBindlessFaultReadbackCallbackState*>(userdata1);
    if (!state)
        return;
    if (status != WGPUQueueWorkDoneStatus_Success || !state->destination) {
        state->lease.release();
        state->latch.reset();
        releaseBuffer(state->destination);
        if (state->result)
            state->result->fail();
        core::destroyCpuAllocatedUnique<WebGpuBindlessFaultReadbackCallbackState>(state);
        return;
    }
    state->lease.markFaultReadbackCompleted();
    auto callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = &completeWebGpuBindlessFaultReadback;
    callbackInfo.userdata1 = state;
    // Pinned Emdawn validates this explicit mode and transfers MapAsyncEvent
    // ownership before returning. A callback can be synchronous; only that
    // callback may free state. A missing callback leaves validation Pending.
    (void)wgpuBufferMapAsync(
        state->destination, WGPUMapMode_Read, 0u, sizeof(std::uint32_t), callbackInfo);
}

void completeWebGpuTimestampReadback(
    WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*)
{
    auto* state = static_cast<WebGpuTimestampReadbackCallbackState*>(userdata1);
    bool available = false;
    if (state && state->readbackBuffer && status == WGPUMapAsyncStatus_Success) {
        std::uint64_t timestamp = 0u;
        available =
            wgpuBufferReadMappedRange(state->readbackBuffer, 0u, &timestamp, sizeof(timestamp))
            == WGPUStatus_Success;
        wgpuBufferUnmap(state->readbackBuffer);
    }
    if (state && state->pool) {
        if (available) {
            state->pool->markAvailable(state->queryIndex);
        } else {
            state->pool->markDiagnosticFailure(state->queryIndex);
        }
    }
    if (state) {
        releaseBuffer(state->resolveBuffer);
        releaseBuffer(state->readbackBuffer);
        core::destroyCpuAllocatedUnique<WebGpuTimestampReadbackCallbackState>(state);
    }
}

WGPUBindGroup createPersistentPushConstantBindGroup(WebGpuNativeCommandPayload::Impl& impl,
    WGPUBindGroupLayout layout,
    std::uint32_t binding,
    std::pmr::string& diagnostics)
{
    if (!layout || !impl.device || !impl.pushConstantUploadRing || binding == UINT32_MAX
        || impl.pushConstantDynamicBindingSize == 0u
        || impl.pushConstantDynamicBindingSize > impl.pushConstantUploadRingByteCapacity) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeBindGroup;
        diagnostics += "WebGPU B2 push-constant group requires a bounded dynamic binding";
        return nullptr;
    }
    auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.buffer = impl.pushConstantUploadRing;
    entry.offset = 0u;
    entry.size = impl.pushConstantDynamicBindingSize;
    auto groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDesc.label = webGpuNativeStringView("RHI WebGPU B2 push constant bind group");
    groupDesc.layout = layout;
    groupDesc.entryCount = 1u;
    groupDesc.entries = &entry;
    auto group = wgpuDeviceCreateBindGroup(impl.device, &groupDesc);
    if (!group) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeBindGroup;
        diagnostics += "WebGPU B2 persistent push-constant bind-group creation failed";
    }
    return group;
}

bool physicalGroupForCurrentPipeline(
    const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& groups,
    std::uint32_t logicalSet,
    std::uint32_t& physicalGroup)
{
    for (const auto& group : groups) {
        if (group.logicalSet == logicalSet) {
            physicalGroup = group.webGpuGroup;
            return true;
        }
    }
    return false;
}

const WebGpuNativeDescriptorBindingLowering* findDescriptorBindingLowering(
    const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
    std::uint32_t logicalSet,
    std::uint32_t logicalBinding)
{
    for (const auto& binding : bindings) {
        if (binding.logicalSet == logicalSet && binding.logicalBinding == logicalBinding) {
            return &binding;
        }
    }
    return nullptr;
}

const WebGpuNativeDescriptorTableLowering* findDescriptorTableLowering(
    const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables, std::uint32_t logicalSet)
{
    for (const auto& table : tables) {
        if (table.logicalSet == logicalSet) {
            return &table;
        }
    }
    return nullptr;
}

const WebGpuNativeCommandPayload::Impl::BoundDescriptorSet* findBoundDescriptorSet(
    const core::pmr::Vector<WebGpuNativeCommandPayload::Impl::BoundDescriptorSet>& sets,
    std::uint32_t logicalSet)
{
    for (const auto& set : sets) {
        if (set.logicalSet == logicalSet) {
            return &set;
        }
    }
    return nullptr;
}

struct NativeBindGroupResourceArray {
    core::pmr::Vector<WGPUBuffer> buffers{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WGPUTextureView> textureViews{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WGPUSampler> samplers{&core::defaultCpuMemoryResource()};
};

bool appendNativeBindGroupEntry(
    const core::pmr::Vector<const WebGpuNativeDescriptorBindingPayload*>& payloads,
    std::uint32_t physicalBinding,
    WGPUBindGroupEntry& entry,
    NativeBindGroupResourceArray& resources,
    std::pmr::string& diagnostics)
{
    if (payloads.empty()) {
        diagnostics = "RHI-WEBGPU-DESCRIPTOR-ARRAY-PAYLOAD-MISSING";
        return false;
    }
    const auto* payload = payloads.front();
    const auto expectedCapacity = payload->binding.arrayCount;
    // The emdawnwebgpu header available to the Emscripten build exposes the
    // stable single-resource WGPUBindGroupEntry ABI. Binding arrays require
    // a newer optional descriptor-array extension, so reject them explicitly
    // instead of compiling against non-existent count/pointer fields.
    if (expectedCapacity > 1u) {
        diagnostics = "RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNAVAILABLE";
        return false;
    }
    if (expectedCapacity != payloads.size()) {
        diagnostics = "RHI-WEBGPU-DESCRIPTOR-ARRAY-INCOMPLETE";
        return false;
    }
    for (std::uint32_t element = 0u; element < expectedCapacity; ++element) {
        const auto found =
            std::find_if(payloads.begin(), payloads.end(), [element](const auto* candidate) {
                return candidate->arrayElement == element;
            });
        if (found == payloads.end()) {
            diagnostics = "RHI-WEBGPU-DESCRIPTOR-ARRAY-ELEMENT-MISSING";
            return false;
        }
    }
    entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = physicalBinding;
    const auto physicalType = payload->binding.type == rhi::DescriptorType::InputAttachment
        ? payload->write.physicalLowering.enabled ? payload->write.physicalLowering.physicalType
                                                  : rhi::DescriptorType::InputAttachment
        : payload->binding.type;
    if (payload->binding.type == rhi::DescriptorType::InputAttachment
        && (!payload->write.physicalLowering.enabled
            || physicalType != rhi::DescriptorType::SampledTexture
            || payload->write.physicalLowering.shaderPackageHash == 0)) {
        diagnostics = "RHI-WEBGPU-LOWERING-INPUT-ATTACHMENT-TICKET-MISSING";
        return false;
    }
    switch (physicalType) {
    case rhi::DescriptorType::UniformBuffer:
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        for (const auto* element : payloads)
            if (const auto* id = rhi::DescriptorBufferBindingRangeContract::writeDiagnostic(
                    element->write.bufferBindingRange, true)) {
                diagnostics = id;
                return false;
            }
        resources.buffers.reserve(payloads.size());
        for (const auto* element : payloads) {
            if (!element->buffer || !element->buffer->buffer) {
                diagnostics = "RHI-WEBGPU-DESCRIPTOR-BUFFER-PAYLOAD-MISSING";
                return false;
            }
            resources.buffers.push_back(element->buffer->buffer);
        }
        entry.buffer = resources.buffers.front();
        entry.offset = 0u;
        entry.size = WGPU_WHOLE_SIZE;
        return true;
    case rhi::DescriptorType::SampledTexture:
    case rhi::DescriptorType::StorageTexture:
        resources.textureViews.reserve(payloads.size());
        for (const auto* element : payloads) {
            if (!element->textureView || !element->textureView->view) {
                diagnostics = "RHI-WEBGPU-DESCRIPTOR-TEXTURE-PAYLOAD-MISSING";
                return false;
            }
            if (!element->textureView->parentTexture) {
                diagnostics = "RHI-WEBGPU-DESCRIPTOR-TEXTURE-PARENT-MISSING";
                return false;
            }
            if (const auto* id = element->textureView->parentTexture->contentAccessPolicy()
                    .ordinaryAccessDiagnostic()) {
                diagnostics = id;
                return false;
            }
            resources.textureViews.push_back(element->textureView->view);
        }
        entry.textureView = resources.textureViews.front();
        return true;
    case rhi::DescriptorType::Sampler:
        resources.samplers.reserve(payloads.size());
        for (const auto* element : payloads) {
            if (!element->sampler || !element->sampler->sampler) {
                diagnostics = "RHI-WEBGPU-DESCRIPTOR-SAMPLER-PAYLOAD-MISSING";
                return false;
            }
            resources.samplers.push_back(element->sampler->sampler);
        }
        entry.sampler = resources.samplers.front();
        return true;
    case rhi::DescriptorType::InputAttachment:
        break;
    }
    diagnostics = "RHI-WEBGPU-DESCRIPTOR-PHYSICAL-TYPE-UNSUPPORTED";
    return false;
}

bool validateDynamicOffsetValue(const WebGpuNativeDescriptorGroupPayload& descriptorGroup,
    std::uint32_t binding,
    std::uint32_t dynamicOffset,
    std::string_view diagnosticPrefix,
    std::pmr::string& diagnostics);

// Build a bind group for one compact physical group.  A missing logical set
// is a normal intermediate state while the caller records several L2 binds;
// in that case the function returns true with a null candidate group and the
// draw/dispatch gate will reject an incomplete physical group later.
bool buildCompactDescriptorGroup(WebGpuNativeCommandPayload::Impl& impl,
    std::uint32_t logicalSet,
    const WebGpuNativeDescriptorGroupPayload& replacement,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::uint32_t physicalGroup,
    WGPUBindGroup& candidateGroup,
    core::pmr::Vector<std::uint32_t>& candidateDynamicOffsets,
    std::pmr::string& diagnostics)
{
    candidateGroup = nullptr;
    candidateDynamicOffsets.clear();
    if (physicalGroup >= impl.activeDescriptorGroupLayouts.size()
        || !impl.activeDescriptorGroupLayouts[physicalGroup]) {
        diagnostics = "RHI-WEBGPU-COMPACT-REMAP-GROUP-LAYOUT-MISSING";
        return false;
    }

    struct SelectedSet {
        std::uint32_t logicalSet = 0u;
        const WebGpuNativeDescriptorGroupPayload* payload = nullptr;
        const std::uint32_t* dynamicOffsets = nullptr;
        std::uint32_t dynamicOffsetCount = 0u;
    };

    core::pmr::Vector<SelectedSet> selected;
    for (const auto& mapping : impl.activeDescriptorBindings) {
        if (mapping.webGpuGroup != physicalGroup) {
            continue;
        }
        bool alreadySelected = false;
        for (const auto& set : selected) {
            alreadySelected = alreadySelected || set.logicalSet == mapping.logicalSet;
        }
        if (alreadySelected) {
            continue;
        }
        if (mapping.logicalSet == logicalSet) {
            selected.push_back({logicalSet, &replacement, dynamicOffsets, dynamicOffsetCount});
        } else {
            const auto* bound =
                findBoundDescriptorSet(impl.boundDescriptorSets, mapping.logicalSet);
            if (!bound || !bound->payload) {
                diagnostics = "RHI-WEBGPU-COMPACT-REMAP-MISSING-BOUND-SET group=";
                diagnostics += std::to_string(physicalGroup).c_str();
                diagnostics += " logicalSet=";
                diagnostics += std::to_string(mapping.logicalSet).c_str();
                return true;
            }
            selected.push_back({
                mapping.logicalSet,
                bound->payload.get(),
                bound->dynamicOffsets.data(),
                static_cast<std::uint32_t>(bound->dynamicOffsets.size()),
            });
        }
    }

    core::pmr::Vector<WGPUBindGroupEntry> entries;
    core::pmr::Vector<NativeBindGroupResourceArray> resourceArrays;
    entries.reserve(impl.activeDescriptorBindings.size());
    resourceArrays.reserve(impl.activeDescriptorBindings.size());
    core::pmr::Vector<std::pair<std::uint32_t, std::uint32_t>> offsetPairs;
    for (const auto& set : selected) {
        if (set.dynamicOffsetCount != set.payload->dynamicOffsetCount
            || (set.dynamicOffsetCount > 0u && !set.dynamicOffsets)) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DYNAMIC-OFFSET-COUNT-MISMATCH";
            return false;
        }
        for (const auto& binding : set.payload->bindings) {
            const auto* tableMapping = set.payload->tableBacked
                ? findDescriptorTableLowering(impl.activeDescriptorTables, set.logicalSet)
                : nullptr;
            if (set.payload->tableBacked && !tableMapping) {
                diagnostics = "RHI-WEBGPU-COMPACT-REMAP-TABLE-MISSING";
                return false;
            }
            const auto* lowering = tableMapping
                ? nullptr
                : findDescriptorBindingLowering(
                      impl.activeDescriptorBindings, set.logicalSet, binding.binding.binding);
            // A sealed bindless snapshot contains bindings for every package
            // entry. The active pipeline layout contains only this entry's
            // stage, so other stages must not enter its native bind group.
            if ((!tableMapping && !lowering)
                || (tableMapping && tableMapping->webGpuGroup != physicalGroup)
                || (lowering && lowering->webGpuGroup != physicalGroup)) {
                continue;
            }
            const auto physicalBinding =
                tableMapping ? tableMapping->webGpuBinding : lowering->webGpuBinding;
            bool alreadyEmitted = false;
            for (const auto& emitted : entries) {
                if (emitted.binding == physicalBinding) {
                    alreadyEmitted = true;
                    break;
                }
            }
            if (alreadyEmitted) {
                continue;
            }
            core::pmr::Vector<const WebGpuNativeDescriptorBindingPayload*> elements;
            for (const auto& candidate : set.payload->bindings) {
                if (candidate.binding.binding == binding.binding.binding) {
                    elements.push_back(&candidate);
                }
            }
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            resourceArrays.emplace_back();
            if (!appendNativeBindGroupEntry(
                    elements, physicalBinding, entry, resourceArrays.back(), diagnostics)) {
                return false;
            }
            entries.push_back(entry);
            if (binding.binding.dynamicOffset) {
                if (set.payload->tableBacked) {
                    diagnostics = "RHI-WEBGPU-COMPACT-REMAP-TABLE-DYNAMIC-OFFSET-UNSUPPORTED";
                    return false;
                }
                auto offsetIt = std::find(set.payload->dynamicOffsetBindings.begin(),
                    set.payload->dynamicOffsetBindings.end(),
                    binding.binding.binding);
                if (offsetIt == set.payload->dynamicOffsetBindings.end()) {
                    diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DYNAMIC-OFFSET-METADATA-MISSING";
                    return false;
                }
                const auto offsetIndex =
                    static_cast<std::size_t>(offsetIt - set.payload->dynamicOffsetBindings.begin());
                if (!validateDynamicOffsetValue(*set.payload,
                        binding.binding.binding,
                        set.dynamicOffsets[offsetIndex],
                        "RHI-WEBGPU-COMPACT-REMAP-DYNAMIC-OFFSET",
                        diagnostics)) {
                    return false;
                }
                offsetPairs.push_back({
                    lowering->webGpuBinding,
                    set.dynamicOffsets[offsetIndex],
                });
            }
        }
    }
    std::sort(offsetPairs.begin(), offsetPairs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    for (std::size_t index = 1u; index < offsetPairs.size(); ++index) {
        if (offsetPairs[index - 1u].first >= offsetPairs[index].first) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DYNAMIC-OFFSET-ORDER-INVALID";
            return false;
        }
    }
    for (const auto& pair : offsetPairs) {
        candidateDynamicOffsets.push_back(pair.second);
    }
    for (const auto& mapping : impl.activeDescriptorBindings) {
        if (mapping.webGpuGroup != physicalGroup)
            continue;
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.binding == mapping.webGpuBinding;
        });
        if (found == entries.end()) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-ACTIVE-BINDING-MISSING";
            return false;
        }
    }
    if (entries.empty()) {
        diagnostics = "RHI-WEBGPU-COMPACT-REMAP-GROUP-EMPTY";
        return false;
    }
    auto groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDesc.label = webGpuNativeStringView("RHI WebGPU compact remapped descriptor group");
    groupDesc.layout = impl.activeDescriptorGroupLayouts[physicalGroup];
    groupDesc.entryCount = static_cast<std::uint32_t>(entries.size());
    groupDesc.entries = entries.data();
    candidateGroup = wgpuDeviceCreateBindGroup(impl.device, &groupDesc);
    if (!candidateGroup) {
        diagnostics = "RHI-WEBGPU-COMPACT-REMAP-BIND-GROUP-CREATE-FAILED";
        return false;
    }
    return true;
}

void retainBoundDescriptorSet(WebGpuNativeCommandPayload::Impl& impl,
    std::uint32_t logicalSet,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& payload,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount)
{
    for (auto& bound : impl.boundDescriptorSets) {
        if (bound.logicalSet != logicalSet) {
            continue;
        }
        bound.payload = payload;
        bound.dynamicOffsets.clear();
        if (dynamicOffsetCount > 0u) {
            bound.dynamicOffsets.insert(
                bound.dynamicOffsets.end(), dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
        }
        return;
    }
    WebGpuNativeCommandPayload::Impl::BoundDescriptorSet bound;
    bound.logicalSet = logicalSet;
    bound.payload = payload;
    if (dynamicOffsetCount > 0u) {
        bound.dynamicOffsets.insert(
            bound.dynamicOffsets.end(), dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
    }
    impl.boundDescriptorSets.push_back(std::move(bound));
}

void clearActiveDescriptorState(WebGpuNativeCommandPayload::Impl& impl)
{
    for (auto& group : impl.activeDescriptorGroups) {
        releaseBindGroup(group);
    }
    for (auto& layout : impl.activeDescriptorGroupLayouts) {
        releaseBindGroupLayout(layout);
    }
    impl.activeDescriptorGroups.clear();
    impl.activeDescriptorGroupLayouts.clear();
    impl.activeDescriptorBindings.clear();
    impl.activeDescriptorTables.clear();
    impl.boundDescriptorSets.clear();
}

bool descriptorGroupsComplete(
    const WebGpuNativeCommandPayload::Impl& impl, std::pmr::string& diagnostics)
{
    for (const auto& binding : impl.activeDescriptorBindings) {
        if (binding.webGpuGroup >= impl.activeDescriptorGroups.size()
            || !impl.activeDescriptorGroups[binding.webGpuGroup]) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DESCRIPTOR-GROUP-INCOMPLETE group=";
            diagnostics += std::to_string(binding.webGpuGroup).c_str();
            diagnostics += " logicalSet=";
            diagnostics += std::to_string(binding.logicalSet).c_str();
            diagnostics += " logicalBinding=";
            diagnostics += std::to_string(binding.logicalBinding).c_str();
            return false;
        }
    }
    return true;
}

#endif

} // namespace

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
WebGpuBindlessFaultReadbackCallbackState::~WebGpuBindlessFaultReadbackCallbackState()
{
    releaseBuffer(destination);
    lease.release();
    if (result)
        result->abandon();
}
#endif

WebGpuProducerCompletionOwner::~WebGpuProducerCompletionOwner()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    releaseDevice(device);
#endif
}

std::shared_ptr<WebGpuProducerCompletionOwner> prepareWebGpuProducerCompletionOwner(
    std::uint32_t maxPendingSubmissions, core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (!maxPendingSubmissions) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-BUDGET";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-NATIVE-UNAVAILABLE";
        return {};
    }
    auto owner = core::makeShared<WebGpuProducerCompletionOwner>(maxPendingSubmissions);
    owner->device = emscripten_webgpu_get_device();
    if (!owner->device) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-DEVICE-MISSING";
        return {};
    }
    if (!observeWebGpuNativeOwnerDeviceLoss(
            owner, owner->device, &completeWebGpuProducerDeviceLoss)) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-LOSS-OBSERVER-MISSING";
        return {};
    }
    return owner;
#else
    diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-NATIVE-UNAVAILABLE";
    return {};
#endif
}

std::shared_ptr<WebGpuProducerCompletionOwner> prepareWebGpuOrdinaryTransferCompletionOwner(
    const WebGpuOrdinaryTransferCompletionBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (budget.contractVersion != 1u || !budget.prototypeOptIn || !budget.maxPendingSubmissions
        || !budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-TRANSFER-COMPLETION-BUDGET";
        return {};
    }
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto owner =
            core::makeShared<WebGpuProducerCompletionOwner>(0u, budget.maxPendingSubmissions);
        owner->transferPreparationQuota = std::move(quota);
        owner->device = emscripten_webgpu_get_device();
        if (!owner->device
            || !observeWebGpuNativeOwnerDeviceLoss(
                owner, owner->device, &completeWebGpuProducerDeviceLoss)) {
            diagnostic = "RHI-WEBGPU-TRANSFER-COMPLETION-LOSS-OBSERVER-MISSING";
            return {};
        }
        return owner;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION";
        return {};
    }
#else
    diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return {};
#endif
}

WebGpuOrdinaryCompletionOwner::~WebGpuOrdinaryCompletionOwner()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    releaseDevice(device_);
#endif
}

void WebGpuOrdinaryCompletionOwner::consumeOriginalDeviceLoss(void* userdata) noexcept
{
    auto* registration = static_cast<OriginalLossRegistration*>(userdata);
    if (!registration || !registration->gate.beginDelivery())
        return;
    // Keep the exact owner alive while severing its deliberate subscription
    // cycle. The native bridge has already cleared captured userdata/function.
    auto owner = registration->owner;
    if (owner && owner->originalLoss_.get() == registration)
        owner->retained.invalidate([](auto& graph) noexcept {
            graph.quarantineState();
        });
    registration->gate.finishDelivery();
    releaseWebGpuNativeDeviceLossRegistration(registration->nativeState);
    registration->nativeState = 0u;
    registration->registrationCapacity.release();
    registration->callbackCapacity.release();
    registration->owner.reset();
}

const char* WebGpuOrdinaryCompletionOwner::detachOriginalDeviceLoss(
    const std::shared_ptr<WebGpuOrdinaryCompletionOwner>& original) noexcept
{
    // Copy first: callers may pass a reference aliased to retained ownership.
    auto owner = original;
    if (!owner || !owner->sharedOwnerMatches(owner) || !owner->originalLoss_
        || !owner->originalPlan_)
        return "RHI-DEVICE-LOSS-REGISTRATION-OWNER";
    auto& registration = *owner->originalLoss_;
    if (registration.gate.phase() != core::runtime::ExternalCallbackGate::Phase::Armed)
        return "RHI-DEVICE-LOSS-REGISTRATION-RETIRED";
    if (const auto* id = rhi::DeviceLossRegistrationContract::validateDetachment(
            owner->retained.pending(), owner->retained.failed(), owner->retained.closed()))
        return id;
    if (!detachWebGpuNativeDeviceLossRegistration(registration.nativeState))
        return "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-DETACH";
    // Native suppression precedes gate mutation, closed admission and retirement.
    registration.gate.detach();
    owner->retained.invalidate();
    releaseWebGpuNativeDeviceLossRegistration(registration.nativeState);
    registration.nativeState = 0u;
    registration.registrationCapacity.release();
    registration.callbackCapacity.release();
    registration.owner.reset();
    return nullptr;
}

std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOriginalReadbackCompletionOwner(
    const std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>& plan,
    const WebGpuOrdinaryCompletionBudget& budget,
    const rhi::DeviceLossRegistrationBudget& lossBudget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = rhi::TextureReadbackColdBudgetPlan::validateOriginal(plan);
    if (diagnostic)
        return {};
    diagnostic = WebGpuOrdinaryCompletionOwner::validateBudget(budget);
    if (diagnostic)
        return {};
    const auto& b = plan->budget();
    if (budget.maxPendingRecords != b.maxPendingRequests
        || budget.maxCpuMetadataBytes != b.maxCpuDeviceBytes
        || !rhi::DeviceLossRegistrationContract::sameBudget(lossBudget, b.deviceLoss)) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-PLAN-BUDGET";
        return {};
    }
    if (!core::SharedOwnerProjection::sameOwner(quota,
            plan->quotas()[static_cast<std::size_t>(
                rhi::TextureReadbackColdBudgetPlan::CpuDomain::Device)])) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-DEVICE-QUOTA";
        return {};
    }
    const auto registrationCapacity =
        rhi::TextureReadbackColdBudgetPlan::originalLossRegistrationCapacity(plan);
    const auto callbackCapacity =
        rhi::TextureReadbackColdBudgetPlan::originalLossCallbackCapacity(plan);
    if (registrationCapacity->used() >= registrationCapacity->capacity()
        || callbackCapacity->used() >= callbackCapacity->capacity()) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-CAPACITY";
        return {};
    }
    if (!rhi::TextureReadbackColdBudgetPlan::originalLossBindingAvailable(plan)) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-REUSED-PLAN";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto owner = core::makeShared<WebGpuOrdinaryCompletionOwner>(
            WebGpuOrdinaryCompletionOwner::ConstructionKey{},
            budget.maxPendingRecords,
            std::move(quota));
        owner->originalPlan_ = plan;
        owner->originalLoss_ =
            core::makeUnique<WebGpuOrdinaryCompletionOwner::OriginalLossRegistration>(plan);
        if (!owner->originalLoss_->registrationCapacity.charged()
            || !owner->originalLoss_->callbackCapacity.charged()) {
            diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-CAPACITY";
            return {};
        }
        owner->device_ = emscripten_webgpu_get_device();
        auto& registration = *owner->originalLoss_;
        if (!rhi::TextureReadbackColdBudgetPlan::commitOriginalLossBinding(plan)) {
            diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-REUSED-PLAN";
            return {};
        }
        registration.gate.arm();
        registration.nativeState = prepareWebGpuNativeDeviceLossRegistration(owner->device_,
            &registration,
            &WebGpuOrdinaryCompletionOwner::consumeOriginalDeviceLoss);
        if (!registration.nativeState) {
            registration.gate.detach();
            diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-BINDING";
            return {};
        }
        // Every fallible C++ allocation precedes native binding. This final
        // shared-owner assignment is allocation-free on the serialized thread.
        registration.owner = owner;
        return owner;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-ALLOCATION";
        return {};
    }
#else
    diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-UNAVAILABLE";
    return {};
#endif
}

std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOrdinaryCompletionOwner(
    const WebGpuOrdinaryCompletionBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = WebGpuOrdinaryCompletionOwner::validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-QUOTA";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-NATIVE-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto owner = core::makeShared<WebGpuOrdinaryCompletionOwner>(
            WebGpuOrdinaryCompletionOwner::ConstructionKey{},
            budget.maxPendingRecords,
            std::move(quota));
        owner->device_ = emscripten_webgpu_get_device();
        if (!owner->device_
            || !observeWebGpuNativeOwnerDeviceLoss(
                owner, owner->device_, &completeWebGpuOrdinaryCompletionDeviceLoss)) {
            diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-LOSS-OBSERVER";
            return {};
        }
        return owner;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION";
        return {};
    }
#else
    diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-NATIVE-UNAVAILABLE";
    return {};
#endif
}

WebGpuNativeZeroBaseIndirectProducer::~WebGpuNativeZeroBaseIndirectProducer()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    releaseBindGroup(group_);
    releaseBindGroupLayout(layout_);
    releaseDevice(device_);
#endif
}

std::shared_ptr<WebGpuNativeZeroBaseIndirectProducer> prepareWebGpuNativeZeroBaseIndirectProducer(
    const rhi::BindlessShaderPackage& package,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptors,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto& artifact = package.artifact();
    if (artifact.execution != rhi::BindlessExecution::Emulated
        || artifact.zeroBaseIndirectProducerContractVersion != 1u) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-PACKAGE-INVALID";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!descriptors || descriptors->tableBacked || descriptors->dynamicOffsetCount
        || descriptors->bindings.size() != artifact.physicalLayout.bindings.size()) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DESCRIPTORS-INVALID";
        return {};
    }
    auto result = core::makeShared<WebGpuNativeZeroBaseIndirectProducer>(
        WebGpuNativeZeroBaseIndirectProducer::ConstructionKey{});
    result->device_ = emscripten_webgpu_get_device();
    if (!result->device_ || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DEVICE-UNAVAILABLE";
        return {};
    }
    const auto limits = webGpuNativeDeviceLimits();
    if (!limits.maxBindGroups || !limits.maxDescriptorBindings
        || !limits.maxUniformBufferBindingSize || !limits.maxStorageBufferBindingSize
        || !limits.maxUniformBuffersPerShaderStage || !limits.maxStorageBuffersPerShaderStage) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-LIMITS-MISSING";
        return {};
    }
    if (artifact.physicalLayout.bindings.size() != result->resources_.size()) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DESCRIPTORS-INVALID";
        return {};
    }
    std::uint32_t storageCount = 0u, uniformCount = 0u;
    for (const auto& binding : artifact.physicalLayout.bindings) {
        if (binding.type == rhi::DescriptorType::UniformBuffer)
            ++uniformCount;
        else
            ++storageCount;
    }
    if (artifact.physicalLayout.bindings.size() > limits.maxDescriptorBindings
        || uniformCount > limits.maxUniformBuffersPerShaderStage
        || storageCount > limits.maxStorageBuffersPerShaderStage) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-BINDING-LIMIT";
        return {};
    }
    for (std::size_t i = 0u; i < artifact.physicalLayout.bindings.size(); ++i) {
        const auto& expected = artifact.physicalLayout.bindings[i];
        const auto found = std::find_if(
            descriptors->bindings.begin(), descriptors->bindings.end(), [&](const auto& entry) {
                return entry.binding.binding == expected.binding;
            });
        if (found == descriptors->bindings.end() || found->binding.type != expected.type
            || found->binding.stages != expected.stages || found->binding.arrayCount != 1u
            || found->binding.dynamicOffset || found->arrayElement || found->write.arrayElement
            || found->write.binding != expected.binding || found->write.type != expected.type
            || found->write.resourceKind != rhi::DescriptorResourceKind::Buffer
            || found->write.physicalLowering.enabled || !found->buffer || !found->buffer->buffer) {
            diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DESCRIPTORS-INVALID";
            return {};
        }
        const auto& buffer = *found->buffer;
        auto requiredUsage = expected.type == rhi::DescriptorType::UniformBuffer
            ? WGPUBufferUsage_Uniform
            : WGPUBufferUsage_Storage;
        std::uint64_t requiredBytes = 0u;
        if (expected.binding == artifact.snapshotMetadata.binding)
            requiredBytes =
                std::uint64_t{artifact.snapshotMetadata.wordCount} * sizeof(std::uint32_t);
        else if (expected.binding == artifact.faultLatch.binding) {
            requiredBytes = sizeof(std::uint32_t);
            requiredUsage |= WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
            result->faultLatch_ = found->buffer;
        } else {
            const auto table = std::find_if(
                artifact.tables.begin(), artifact.tables.end(), [&](const auto& value) {
                    return value.firstBinding == expected.binding;
                });
            if (table == artifact.tables.end()) {
                diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DESCRIPTORS-INVALID";
                return {};
            }
            if (table->tableId == 1u) {
                if (!buffer.sizeBytes || buffer.sizeBytes % sizeof(std::uint32_t)
                    || buffer.sizeBytes / sizeof(std::uint32_t) > UINT32_MAX) {
                    diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-BUFFER-INVALID";
                    return {};
                }
            } else if (table->tableId == 2u) {
                requiredBytes = sizeof(rhi::DrawIndexedIndirectCommand);
                requiredUsage |= WGPUBufferUsage_CopySrc;
            } else if (table->tableId == 3u) {
                requiredBytes = sizeof(rhi::IndirectInstanceBase);
                requiredUsage |= WGPUBufferUsage_CopySrc;
            } else if (table->tableId == 4u)
                requiredBytes = sizeof(rhi::ZeroBaseIndirectProducerParams);
            else {
                diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DESCRIPTORS-INVALID";
                return {};
            }
        }
        if (buffer.ownerDevice != result->device_) {
            diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DEVICE-MISMATCH";
            return {};
        }
        for (std::size_t previous = 0u; previous < i; ++previous)
            if (result->resources_[previous]->buffer == buffer.buffer) {
                diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-ALIAS-UNSUPPORTED";
                return {};
            }
        if (buffer.sizeBytes != wgpuBufferGetSize(buffer.buffer)
            || (requiredBytes && buffer.sizeBytes != requiredBytes)
            || buffer.sizeBytes > (expected.type == rhi::DescriptorType::UniformBuffer
                       ? limits.maxUniformBufferBindingSize
                       : limits.maxStorageBufferBindingSize)
            || (wgpuBufferGetUsage(buffer.buffer) & requiredUsage) != requiredUsage) {
            diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-BUFFER-INVALID";
            return {};
        }
        result->resources_[i] = found->buffer;
    }
    result->pipeline_ = createWebGpuNativeBindlessComputePipelinePayload(
        package, "arpZeroBaseIndirectMain", diagnostic);
    if (!result->pipeline_)
        return {};
    const auto& pipeline = *result->pipeline_;
    if (pipeline.usesPushConstantGroup || pipeline.bindGroupCount != 1u
        || !pipeline.descriptorTables.empty()
        || std::any_of(pipeline.descriptorBindings.begin(),
            pipeline.descriptorBindings.end(),
            [&](const auto& binding) {
                return binding.logicalSet != artifact.set || binding.webGpuGroup != 0u;
            })) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-LOWERING-INVALID";
        return {};
    }
    result->layout_ = wgpuComputePipelineGetBindGroupLayout(pipeline.pipeline, 0u);
    if (!result->layout_) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-LAYOUT-MISSING";
        return {};
    }
    WebGpuNativeCommandPayload::Impl preparation;
    preparation.device = result->device_;
    preparation.activeDescriptorBindings = pipeline.descriptorBindings;
    preparation.activeDescriptorGroupLayouts.push_back(result->layout_);
    core::pmr::Vector<std::uint32_t> dynamicOffsets;
    if (!buildCompactDescriptorGroup(preparation,
            artifact.set,
            *descriptors,
            nullptr,
            0u,
            0u,
            result->group_,
            dynamicOffsets,
            diagnostic)
        || !result->group_ || !dynamicOffsets.empty()) {
        if (diagnostic.empty())
            diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-GROUP-MISSING";
        return {};
    }
    return result;
#else
    (void)descriptors;
    diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-NATIVE-UNAVAILABLE";
    return {};
#endif
}

WebGpuNativeIndirectProcessorPool::WebGpuNativeIndirectProcessorPool(
    rhi::GpuDevice& device, WebGpuIndirectProcessorBudget budget)
    : device_(device), budget_(budget),
      counts_(core::makeShared<core::AtomicCapacityBudget>(budget.maxProcessors)),
      bytes_(core::makeShared<core::AtomicCapacityBudget>(budget.maxResidentBytes))
{}

rhi::ValidationResult WebGpuNativeIndirectProcessor::validate() const
{
    if (!intent_)
        return {false, "RHI-WEBGPU-INDIRECT-PROCESSOR-INTENT-MISSING"};
    const auto live = intent_->validate();
    if (!live.success)
        return live;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (webGpuNativeDeviceLost() || !producer_ || !countLease_ || !countLease_->charged()
        || !byteLease_ || !byteLease_->charged())
        return {false, "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-UNAVAILABLE"};
    const auto& artifact = intent_->desc().producer->artifact();
    const auto& desc = intent_->desc();
    const auto source = webGpuNativePayloadFor(*desc.logicalSource);
    if (!source || !source->buffer || source->sizeBytes != desc.logicalSource->desc().sizeBytes
        || source->sizeBytes != wgpuBufferGetSize(source->buffer)
        || (wgpuBufferGetUsage(source->buffer) & WGPUBufferUsage_Storage) == 0u)
        return {false, "RHI-WEBGPU-INDIRECT-PROCESSOR-SOURCE-INVALID"};
    const std::array<std::uint64_t, 4> sizes{
        std::uint64_t{artifact.snapshotMetadata.wordCount} * 4u,
        sizeof(std::uint32_t),
        sizeof(rhi::DrawIndexedIndirectCommand),
        sizeof(rhi::IndirectInstanceBase)};
    for (std::size_t i = 0u; i < buffers_.size(); ++i) {
        const auto& native = nativeBuffers_[i];
        if (!buffers_[i] || !native || !native->buffer || buffers_[i]->desc().sizeBytes != sizes[i]
            || native->sizeBytes != sizes[i] || wgpuBufferGetSize(native->buffer) != sizes[i]
            || native->ownerDevice != source->ownerDevice || !buffers_[i]->hostWritesSealed()
            || !buffers_[i]->gpuWritesSealed() || !buffers_[i]->gpuTransferWritesSealed())
            return {false, "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-INVALID"};
    }
    const std::array<const rhi::GpuBuffer*, 2> outputs{
        desc.packet->physicalArgs(), desc.packet->instanceMappings()};
    for (std::size_t i = 0u; i < outputs.size(); ++i) {
        const auto& native = destinations_[i];
        if (!native || !native->buffer || native != webGpuNativePayloadFor(*outputs[i])
            || native->ownerDevice != source->ownerDevice || native->sizeBytes != sizes[i + 2u]
            || wgpuBufferGetSize(native->buffer) != sizes[i + 2u]
            || (wgpuBufferGetUsage(native->buffer) & WGPUBufferUsage_CopyDst) == 0u
            || !outputs[i]->hostWritesSealed() || !outputs[i]->gpuWritesSealed()
            || !outputs[i]->gpuTransferWritesSealed())
            return {false, "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-INVALID"};
    }
    return {true, {}};
#else
    return {false, "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-UNAVAILABLE"};
#endif
}

std::shared_ptr<const WebGpuNativeIndirectProcessor> WebGpuNativeIndirectProcessorPool::prepare(
    std::shared_ptr<const rhi::BindlessIndirectProcessorIntent> intent,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!budget_.maxProcessors || !budget_.maxBytesPerProcessor || !budget_.maxResidentBytes
        || budget_.maxBytesPerProcessor > budget_.maxResidentBytes) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-BUDGET";
        return {};
    }
    if (!intent) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-INTENT-MISSING";
        return {};
    }
    const auto valid = intent->validate();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    const auto& desc = intent->desc();
    const auto* extension = static_cast<const rhi::BindlessCrossExtension*>(
        device_.queryInterface(rhi::kExtensionBindlessCross));
    if (!extension || !desc.consumerAuthorization->issuedBy(extension, desc.consumerAuthorization)
        || extension->capabilities().deviceIdentity != desc.consumerPlan.deviceIdentity) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH";
        return {};
    }
    if (desc.producer->artifact().execution != rhi::BindlessExecution::Emulated) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PACKAGE-INVALID";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-UNAVAILABLE";
        return {};
    }
    const auto& artifact = desc.producer->artifact();
    const auto source = webGpuNativePayloadFor(*desc.logicalSource);
    const auto parameters = webGpuNativePayloadFor(*desc.packet->producerParameters());
    if (!source || !source->buffer || !parameters || !parameters->buffer) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-INVALID";
        return {};
    }
    if (source->sizeBytes != desc.logicalSource->desc().sizeBytes
        || source->sizeBytes != wgpuBufferGetSize(source->buffer) || !source->sizeBytes
        || source->sizeBytes % sizeof(std::uint32_t)
        || source->sizeBytes / sizeof(std::uint32_t) > UINT32_MAX) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-SOURCE-INVALID";
        return {};
    }
    const std::array<std::uint64_t, 4> sizes{
        std::uint64_t{artifact.snapshotMetadata.wordCount} * 4u,
        sizeof(std::uint32_t),
        sizeof(rhi::DrawIndexedIndirectCommand),
        sizeof(rhi::IndirectInstanceBase)};
    std::uint64_t minimumBytes = intent->retainedBytes();
    for (const auto size : sizes) {
        if (minimumBytes > budget_.maxBytesPerProcessor
            || size > budget_.maxBytesPerProcessor - minimumBytes) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-FOOTPRINT-EXCEEDED";
            return {};
        }
        minimumBytes += size;
    }
    auto result = core::makeShared<WebGpuNativeIndirectProcessor>(
        WebGpuNativeIndirectProcessor::ConstructionKey{});
    result->countLease_ = core::makeUnique<core::AtomicCapacityLease>(counts_, 1u);
    if (!result->countLease_->charged()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-POOL-FULL";
        return {};
    }
    // Reserve the explicit ceiling before allocating any GPU payload. Actual
    // reported bytes must fit it; the reservation is intentionally conservative.
    result->byteLease_ =
        core::makeUnique<core::AtomicCapacityLease>(bytes_, budget_.maxBytesPerProcessor);
    if (!result->byteLease_->charged()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-RESIDENT-BUDGET";
        return {};
    }
    result->retainedBytes_ = intent->retainedBytes();
    result->intent_ = std::move(intent);
    constexpr std::array<const char*, 4> labels{"Indirect processor metadata",
        "Indirect processor fault",
        "Indirect processor arguments",
        "Indirect processor mapping"};
    for (std::size_t i = 0u; i < sizes.size(); ++i) {
        result->buffers_[i] = device_.createBuffer({labels[i],
            sizes[i],
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst
                | rhi::ResourceUsage::CopySrc,
            rhi::MemoryUsage::CpuToGpu,
            i < 1u ? rhi::ResourceState::ShaderRead : rhi::ResourceState::ShaderWrite});
        auto& buffer = result->buffers_[i];
        if (!buffer) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-BUFFER-CREATION-FAILED";
            return {};
        }
        const auto reported = buffer->allocation().estimatedSizeBytes;
        if (reported < sizes[i]
            || reported > budget_.maxBytesPerProcessor - result->retainedBytes_) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-FOOTPRINT-EXCEEDED";
            return {};
        }
        result->retainedBytes_ += reported;
        result->ownedBufferBytes_ += reported;
        result->nativeBuffers_[i] = webGpuNativePayloadFor(*buffer);
    }
    result->destinations_[0] = webGpuNativePayloadFor(*desc.packet->physicalArgs());
    result->destinations_[1] = webGpuNativePayloadFor(*desc.packet->instanceMappings());
    for (const auto& destination : result->destinations_)
        if (!destination || !destination->buffer
            || destination->ownerDevice != source->ownerDevice) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-INVALID";
            return {};
        }
    const std::array<rhi::BindlessMetadataBufferResource, 4u> metadataResources{
        {{1u, desc.logicalSource->id(), source->sizeBytes},
            {2u, result->buffers_[2]->id(), result->buffers_[2]->desc().sizeBytes},
            {3u, result->buffers_[3]->id(), result->buffers_[3]->desc().sizeBytes},
            {4u, desc.packet->producerParameters()->id(), parameters->sizeBytes}}};
    rhi::BindlessSingleBufferMetadataSource metadataSource(artifact.tables.data(),
        artifact.tables.size(),
        metadataResources.data(),
        metadataResources.size());
    core::pmr::Vector<std::uint32_t> words;
    words.resize(artifact.snapshotMetadata.wordCount);
    const auto metadata = rhi::buildBindlessSnapshotMetadata(artifact.snapshotMetadata,
        desc.consumerGeneration,
        metadataSource,
        words.data(),
        words.size());
    if (!metadata.success) {
        diagnostic = metadata.diagnostics;
        return {};
    }
    if (!device_.writeBuffer(*result->buffers_[0], words.data(), sizes[0]).success) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-INITIALIZATION-FAILED";
        return {};
    }
    constexpr std::array<std::uint32_t, 5> zero{};
    for (std::size_t i = 1u; i < sizes.size(); ++i)
        if (!device_.writeBuffer(*result->buffers_[i], zero.data(), sizes[i]).success) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-INITIALIZATION-FAILED";
            return {};
        }
    auto descriptors = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
    for (const auto& binding : artifact.physicalLayout.bindings) {
        WebGpuNativeDescriptorBindingPayload entry;
        entry.binding = binding;
        entry.write.binding = binding.binding;
        entry.write.type = binding.type;
        entry.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
        if (binding.binding == artifact.snapshotMetadata.binding)
            entry.buffer = result->nativeBuffers_[0];
        else if (binding.binding == artifact.faultLatch.binding)
            entry.buffer = result->nativeBuffers_[1];
        else {
            const auto table = std::find_if(
                artifact.tables.begin(), artifact.tables.end(), [&](const auto& value) {
                    return value.firstBinding == binding.binding;
                });
            if (table == artifact.tables.end()) {
                diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PACKAGE-INVALID";
                return {};
            }
            switch (table->tableId) {
            case 1u:
                entry.buffer = source;
                break;
            case 2u:
                entry.buffer = result->nativeBuffers_[2];
                break;
            case 3u:
                entry.buffer = result->nativeBuffers_[3];
                break;
            case 4u:
                entry.buffer = parameters;
                break;
            default:
                diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PACKAGE-INVALID";
                return {};
            }
        }
        descriptors->bindings.push_back(std::move(entry));
    }
    result->producer_ =
        prepareWebGpuNativeZeroBaseIndirectProducer(*desc.producer, descriptors, diagnostic);
    if (!result->producer_)
        return {};
    for (auto& buffer : result->buffers_)
        buffer->sealWrites();
    const auto prepared = result->validate();
    if (!prepared.success) {
        diagnostic = prepared.diagnostics;
        return {};
    }
    return result;
#else
    diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-UNAVAILABLE";
    return {};
#endif
}

std::shared_ptr<WebGpuNativeRenderBundlePayload> createWebGpuNativeRenderBundlePayload(
    const rhi::RenderBundleDesc& desc, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (desc.maxCommandCount == 0u || desc.sampleCount == 0u
        || (desc.colorFormats.empty() && desc.depthStencilFormat == rhi::TextureFormat::Unknown)) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DESC-INVALID";
        return nullptr;
    }
    core::pmr::Vector<WGPUTextureFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (const auto format : desc.colorFormats) {
        const auto nativeFormat = nativeTextureFormat(format);
        if (nativeFormat == WGPUTextureFormat_Undefined) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-COLOR-FORMAT-INVALID";
            return nullptr;
        }
        colorFormats.push_back(nativeFormat);
    }
    const auto nativeDepthFormat = desc.depthStencilFormat == rhi::TextureFormat::Unknown
        ? WGPUTextureFormat_Undefined
        : nativeTextureFormat(desc.depthStencilFormat);
    if (desc.depthStencilFormat != rhi::TextureFormat::Unknown
        && nativeDepthFormat == WGPUTextureFormat_Undefined) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DEPTH-FORMAT-INVALID";
        return nullptr;
    }
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
        return nullptr;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-DEVICE-MISSING";
        return nullptr;
    }
    auto encoderDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    encoderDesc.label = webGpuNativeStringView(desc.label);
    encoderDesc.colorFormatCount = static_cast<std::uint32_t>(colorFormats.size());
    encoderDesc.colorFormats = colorFormats.empty() ? nullptr : colorFormats.data();
    encoderDesc.depthStencilFormat = nativeDepthFormat;
    encoderDesc.sampleCount = desc.sampleCount;
    auto payload = arp::core::makeShared<WebGpuNativeRenderBundlePayload>();
    payload->primitiveTopology = desc.primitiveTopology;
    payload->colorFormats = desc.colorFormats;
    payload->depthStencilFormat = desc.depthStencilFormat;
    payload->sampleCount = desc.sampleCount;
    payload->encoder = wgpuDeviceCreateRenderBundleEncoder(device, &encoderDesc);
    wgpuDeviceRelease(device);
    if (!payload->encoder) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-ENCODER-CREATE-FAILED";
        return nullptr;
    }
    payload->nativeRuntimeBacked = true;
    return payload;
#else
    (void)desc;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return nullptr;
#endif
}

namespace {

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
bool validRenderBundlePayload(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle, std::pmr::string& diagnostics)
{
    if (!bundle || !bundle->nativeRuntimeBacked || !bundle->encoder || bundle->finished) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-PAYLOAD-INVALID";
        return false;
    }
    return true;
}

bool validateDynamicOffsetValue(const WebGpuNativeDescriptorGroupPayload& descriptorGroup,
    std::uint32_t binding,
    std::uint32_t dynamicOffset,
    std::string_view diagnosticPrefix,
    std::pmr::string& diagnostics)
{
    if (const auto* id = WebGpuDescriptorDynamicOffsetValidation::validateBinding(
            descriptorGroup, binding, dynamicOffset, webGpuNativeDeviceLimits())) {
        diagnostics.assign(diagnosticPrefix.data(), diagnosticPrefix.size());
        diagnostics += " ";
        diagnostics += id;
        return false;
    }
    return true;
}

bool descriptorDynamicOffsetsValid(const WebGpuNativeDescriptorGroupPayload& descriptorGroup,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics)
{
    if (dynamicOffsetCount != descriptorGroup.dynamicOffsetCount
        || (dynamicOffsetCount > 0u && !dynamicOffsets)
        || descriptorGroup.dynamicOffsetBindings.size() != dynamicOffsetCount) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DYNAMIC-OFFSET-COUNT-INVALID";
        return false;
    }
    for (std::size_t index = 1u; index < descriptorGroup.dynamicOffsetBindings.size(); ++index) {
        if (descriptorGroup.dynamicOffsetBindings[index - 1u]
            >= descriptorGroup.dynamicOffsetBindings[index]) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DYNAMIC-OFFSET-ORDER-INVALID";
            return false;
        }
    }
    for (std::uint32_t index = 0u; index < dynamicOffsetCount; ++index) {
        if (!validateDynamicOffsetValue(descriptorGroup,
                descriptorGroup.dynamicOffsetBindings[index],
                dynamicOffsets[index],
                "RHI-WEBGPU-RENDER-BUNDLE-DYNAMIC-OFFSET",
                diagnostics)) {
            return false;
        }
    }
    return true;
}

bool renderBundleDescriptorStateComplete(
    const WebGpuNativeRenderBundlePayload& bundle, std::pmr::string& diagnostics)
{
    for (const auto& required : bundle.requiredDescriptorSets) {
        bool found = false;
        for (const auto& bound : bundle.boundDescriptorSets) {
            if (bound.logicalSet == required.logicalSet && bound.payload != nullptr) {
                found = true;
                break;
            }
        }
        if (!found) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-SET-INCOMPLETE";
            return false;
        }
    }
    return true;
}
#endif

} // namespace

bool recordWebGpuNativeRenderBundlePipeline(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    const WebGpuNativeRenderPipelinePayload& pipeline,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !pipeline.pipeline) {
        if (diagnostics.empty()) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-PIPELINE-PAYLOAD-MISSING";
        }
        return false;
    }
    if (pipeline.usesPushConstantGroup) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-PUSH-CONSTANTS-NOT-LOWERED";
        return false;
    }
    if (pipeline.primitiveTopology != bundle->primitiveTopology
        || pipeline.sampleCount != bundle->sampleCount
        || pipeline.depthStencilFormat != bundle->depthStencilFormat
        || pipeline.colorFormats.size() != bundle->colorFormats.size()) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-PIPELINE-TOPOLOGY-MISMATCH";
        return false;
    }
    for (std::size_t index = 0; index < pipeline.colorFormats.size(); ++index) {
        if (pipeline.colorFormats[index] != bundle->colorFormats[index]) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-ATTACHMENT-FORMAT-MISMATCH";
            return false;
        }
    }
    wgpuRenderBundleEncoderSetPipeline(bundle->encoder, pipeline.pipeline);
    bundle->requiredDescriptorSets = pipeline.descriptorSetGroups;
    bundle->pipelineBound = true;
    return true;
#else
    (void)bundle;
    (void)pipeline;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool recordWebGpuNativeRenderBundleVertexBuffer(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t slot,
    const std::shared_ptr<WebGpuNativeBufferPayload>& buffer,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !bundle->pipelineBound || !buffer
        || !buffer->buffer) {
        if (diagnostics.empty()) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-VERTEX-PAYLOAD-MISSING";
        }
        return false;
    }
    if (bundle->retainedPipeline && slot >= bundle->retainedPipeline->vertexBufferCount) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-VERTEX-SLOT-OUT-OF-RANGE";
        return false;
    }
    wgpuRenderBundleEncoderSetVertexBuffer(
        bundle->encoder, slot, buffer->buffer, offsetBytes, WGPU_WHOLE_SIZE);
    bundle->retainedBuffers.push_back(buffer);
    return true;
#else
    (void)bundle;
    (void)slot;
    (void)buffer;
    (void)offsetBytes;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool recordWebGpuNativeRenderBundleIndexBuffer(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    const std::shared_ptr<WebGpuNativeBufferPayload>& buffer,
    rhi::IndexFormat format,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !bundle->pipelineBound || !buffer
        || !buffer->buffer) {
        if (diagnostics.empty()) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-INDEX-PAYLOAD-MISSING";
        }
        return false;
    }
    wgpuRenderBundleEncoderSetIndexBuffer(bundle->encoder,
        buffer->buffer,
        format == rhi::IndexFormat::Uint16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32,
        offsetBytes,
        WGPU_WHOLE_SIZE);
    bundle->indexBufferBound = true;
    bundle->retainedBuffers.push_back(buffer);
    return true;
#else
    (void)bundle;
    (void)buffer;
    (void)format;
    (void)offsetBytes;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool recordWebGpuNativeRenderBundleDescriptorSet(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t set,
    const WebGpuNativeRenderPipelinePayload& pipeline,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptorGroup,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics)
{
    if (descriptorGroup)
        if (const auto* id = descriptorGroup->ordinaryTextureAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !bundle->pipelineBound || !descriptorGroup
        || !descriptorGroup->group) {
        if (diagnostics.empty()) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-PAYLOAD-MISSING";
        }
        return false;
    }
    if (!descriptorDynamicOffsetsValid(
            *descriptorGroup, dynamicOffsets, dynamicOffsetCount, diagnostics)) {
        return false;
    }
    std::uint32_t physicalGroup = UINT32_MAX;
    for (const auto& mapping : pipeline.descriptorSetGroups) {
        if (mapping.logicalSet == set) {
            physicalGroup = mapping.webGpuGroup;
            break;
        }
    }
    if (physicalGroup == UINT32_MAX || pipeline.bindGroupCount == 0u
        || physicalGroup >= pipeline.bindGroupCount) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-GROUP-LOWERING-MISSING";
        return false;
    }
    bool descriptorSetWasRetained = false;
    for (auto& bound : bundle->boundDescriptorSets) {
        if (bound.logicalSet != set) {
            continue;
        }
        bound.payload = descriptorGroup;
        bound.dynamicOffsets.clear();
        if (dynamicOffsetCount > 0u) {
            bound.dynamicOffsets.insert(
                bound.dynamicOffsets.end(), dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
        }
        descriptorSetWasRetained = true;
        break;
    }
    if (!descriptorSetWasRetained) {
        WebGpuNativeRenderBundlePayload::BoundDescriptorSet bound;
        bound.logicalSet = set;
        bound.payload = descriptorGroup;
        if (dynamicOffsetCount > 0u) {
            bound.dynamicOffsets.insert(
                bound.dynamicOffsets.end(), dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
        }
        bundle->boundDescriptorSets.push_back(std::move(bound));
    }

    // A compact physical group is emitted only after every logical set mapped
    // to it has been supplied. This mirrors the primary render-pass lowering;
    // binding one logical set must not silently discard its siblings.
    for (const auto& mapping : pipeline.descriptorSetGroups) {
        if (mapping.webGpuGroup != physicalGroup) {
            continue;
        }
        bool found = false;
        for (const auto& bound : bundle->boundDescriptorSets) {
            if (bound.logicalSet == mapping.logicalSet && bound.payload) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }

    struct SelectedSet {
        std::uint32_t logicalSet = 0u;
        const WebGpuNativeDescriptorGroupPayload* payload = nullptr;
        const std::uint32_t* dynamicOffsets = nullptr;
        std::uint32_t dynamicOffsetCount = 0u;
    };

    core::pmr::Vector<SelectedSet> selected;
    for (const auto& mapping : pipeline.descriptorSetGroups) {
        if (mapping.webGpuGroup != physicalGroup) {
            continue;
        }
        for (const auto& bound : bundle->boundDescriptorSets) {
            if (bound.logicalSet == mapping.logicalSet) {
                selected.push_back({
                    bound.logicalSet,
                    bound.payload.get(),
                    bound.dynamicOffsets.empty() ? nullptr : bound.dynamicOffsets.data(),
                    static_cast<std::uint32_t>(bound.dynamicOffsets.size()),
                });
                break;
            }
        }
    }
    core::pmr::Vector<WGPUBindGroupEntry> entries;
    core::pmr::Vector<NativeBindGroupResourceArray> resourceArrays;
    entries.reserve(pipeline.descriptorBindings.size());
    resourceArrays.reserve(pipeline.descriptorBindings.size());
    core::pmr::Vector<std::pair<std::uint32_t, std::uint32_t>> offsetPairs;
    for (const auto& selectedSet : selected) {
        if (!descriptorDynamicOffsetsValid(*selectedSet.payload,
                selectedSet.dynamicOffsets,
                selectedSet.dynamicOffsetCount,
                diagnostics)) {
            return false;
        }
        if (selectedSet.payload->containsLoweredInputAttachment
            && (selectedSet.payload->shaderPackageHash == 0 || pipeline.shaderPackageHash == 0
                || selectedSet.payload->shaderPackageHash != pipeline.shaderPackageHash)) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-INPUT-ATTACHMENT-IDENTITY-MISMATCH";
            return false;
        }
        for (const auto& binding : selectedSet.payload->bindings) {
            const auto* tableMapping = selectedSet.payload->tableBacked
                ? findDescriptorTableLowering(pipeline.descriptorTables, selectedSet.logicalSet)
                : nullptr;
            if (selectedSet.payload->tableBacked && !tableMapping) {
                diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-COMPACT-REMAP-TABLE-MISSING";
                return false;
            }
            const auto* lowering = selectedSet.payload->tableBacked
                ? nullptr
                : findDescriptorBindingLowering(
                      pipeline.descriptorBindings, selectedSet.logicalSet, binding.binding.binding);
            const auto physicalBinding = tableMapping ? tableMapping->webGpuBinding
                : lowering                            ? lowering->webGpuBinding
                                                      : UINT32_MAX;
            if (physicalBinding == UINT32_MAX
                || (lowering && lowering->webGpuGroup != physicalGroup)
                || (tableMapping && tableMapping->webGpuGroup != physicalGroup)) {
                diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-COMPACT-REMAP-BINDING-MISSING";
                return false;
            }
            bool alreadyEmitted = false;
            for (const auto& existing : entries) {
                if (existing.binding == physicalBinding) {
                    alreadyEmitted = true;
                    break;
                }
            }
            if (alreadyEmitted) {
                continue;
            }
            core::pmr::Vector<const WebGpuNativeDescriptorBindingPayload*> elements;
            for (const auto& candidate : selectedSet.payload->bindings) {
                if (candidate.binding.binding == binding.binding.binding) {
                    elements.push_back(&candidate);
                }
            }
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            resourceArrays.emplace_back();
            if (!appendNativeBindGroupEntry(
                    elements, physicalBinding, entry, resourceArrays.back(), diagnostics)) {
                return false;
            }
            entries.push_back(entry);
            if (binding.binding.dynamicOffset && selectedSet.payload->tableBacked) {
                diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-TABLE-DYNAMIC-OFFSET-UNSUPPORTED";
                return false;
            }
            if (binding.binding.dynamicOffset) {
                const auto offsetIt = std::find(selectedSet.payload->dynamicOffsetBindings.begin(),
                    selectedSet.payload->dynamicOffsetBindings.end(),
                    binding.binding.binding);
                if (offsetIt == selectedSet.payload->dynamicOffsetBindings.end()) {
                    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DYNAMIC-OFFSET-METADATA-MISSING";
                    return false;
                }
                const auto offsetIndex = static_cast<std::size_t>(
                    offsetIt - selectedSet.payload->dynamicOffsetBindings.begin());
                if (!validateDynamicOffsetValue(*selectedSet.payload,
                        binding.binding.binding,
                        selectedSet.dynamicOffsets[offsetIndex],
                        "RHI-WEBGPU-RENDER-BUNDLE-DYNAMIC-OFFSET",
                        diagnostics)) {
                    return false;
                }
                offsetPairs.push_back({
                    physicalBinding,
                    selectedSet.dynamicOffsets[offsetIndex],
                });
            }
        }
    }
    std::sort(offsetPairs.begin(), offsetPairs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    core::pmr::Vector<std::uint32_t> physicalDynamicOffsets;
    for (std::size_t index = 1u; index < offsetPairs.size(); ++index) {
        if (offsetPairs[index - 1u].first >= offsetPairs[index].first) {
            diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DYNAMIC-OFFSET-ORDER-INVALID";
            return false;
        }
    }
    for (const auto& pair : offsetPairs) {
        physicalDynamicOffsets.push_back(pair.second);
    }
    if (entries.empty()) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-COMPACT-REMAP-GROUP-EMPTY";
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device || !pipeline.pipeline) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-DEVICE-MISSING";
        return false;
    }
    auto layout = wgpuRenderPipelineGetBindGroupLayout(pipeline.pipeline, physicalGroup);
    if (!layout) {
        if (!webGpuNativeDeviceLost())
            wgpuDeviceRelease(device);
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DESCRIPTOR-GROUP-LAYOUT-MISSING";
        return false;
    }
    auto groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDesc.layout = layout;
    groupDesc.entryCount = static_cast<std::uint32_t>(entries.size());
    groupDesc.entries = entries.data();
    auto mergedGroup = wgpuDeviceCreateBindGroup(device, &groupDesc);
    wgpuBindGroupLayoutRelease(layout);
    if (!webGpuNativeDeviceLost()) {
        wgpuDeviceRelease(device);
    }
    if (!mergedGroup) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-COMPACT-REMAP-BIND-GROUP-CREATE-FAILED";
        return false;
    }
    wgpuRenderBundleEncoderSetBindGroup(bundle->encoder,
        physicalGroup,
        mergedGroup,
        static_cast<std::uint32_t>(physicalDynamicOffsets.size()),
        physicalDynamicOffsets.empty() ? nullptr : physicalDynamicOffsets.data());
    bundle->retainedNativeDescriptorGroups.push_back(mergedGroup);
    bundle->retainedDescriptorGroups.push_back(descriptorGroup);
    return true;
#else
    (void)bundle;
    (void)set;
    (void)pipeline;
    (void)descriptorGroup;
    (void)dynamicOffsets;
    (void)dynamicOffsetCount;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool recordWebGpuNativeRenderBundleDrawIndexed(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t indexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstIndex,
    std::int32_t vertexOffset,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !bundle->pipelineBound
        || !bundle->indexBufferBound || indexCount == 0u || instanceCount == 0u) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-INDEXED-DRAW-STATE-INVALID";
        return false;
    }
    if (!renderBundleDescriptorStateComplete(*bundle, diagnostics)) {
        return false;
    }
    wgpuRenderBundleEncoderDrawIndexed(
        bundle->encoder, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    ++bundle->commandCount;
    return true;
#else
    (void)bundle;
    (void)indexCount;
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool recordWebGpuNativeRenderBundleDraw(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !bundle->pipelineBound
        || vertexCount == 0u || instanceCount == 0u) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-DRAW-STATE-INVALID";
        return false;
    }
    if (!renderBundleDescriptorStateComplete(*bundle, diagnostics)) {
        return false;
    }
    wgpuRenderBundleEncoderDraw(
        bundle->encoder, vertexCount, instanceCount, firstVertex, firstInstance);
    ++bundle->commandCount;
    return true;
#else
    (void)bundle;
    (void)vertexCount;
    (void)instanceCount;
    (void)firstVertex;
    (void)firstInstance;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool finishWebGpuNativeRenderBundle(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!validRenderBundlePayload(bundle, diagnostics) || !bundle->pipelineBound
        || bundle->commandCount == 0u) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-FINISH-STATE-INVALID";
        return false;
    }
    bundle->bundle = wgpuRenderBundleEncoderFinish(bundle->encoder, nullptr);
    releaseRenderBundleEncoder(bundle->encoder);
    if (!bundle->bundle) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-FINISH-FAILED";
        return false;
    }
    bundle->finished = true;
    return true;
#else
    (void)bundle;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool executeWebGpuNativeRenderBundle(
    const std::shared_ptr<WebGpuNativeCommandPayload>& commandPayload,
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::pmr::string& diagnostics)
{
    return commandPayload && bundle
        ? commandPayload->recordRenderBundleExecute(bundle, diagnostics)
        : (diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-PAYLOAD-INVALID", false);
}

WebGpuNativeCommandPayload::WebGpuNativeCommandPayload(core::UniquePtr<Impl> impl)
    : impl_(std::move(impl))
{}

WebGpuNativeCommandPayload::~WebGpuNativeCommandPayload()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        return;
    }
    if (!impl_->submitted) {
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (query.pool) {
                query.pool->rollbackRecorded(query.queryIndex);
            }
        }
    }
    for (auto& pending : impl_->pendingTimestampReadbacks) {
        releaseBuffer(pending.resolveBuffer);
        releaseBuffer(pending.readbackBuffer);
    }
    for (auto& pending : impl_->pendingBindlessFaultReadbacks) {
        pending.lease.release();
        if (pending.result)
            pending.result->abandon();
        releaseBuffer(pending.destination);
    }
    releaseComputePass(impl_->computePass);
    releaseRenderPass(impl_->renderPass);
    releaseBuffer(impl_->timingResolveBuffer);
    releaseBuffer(impl_->timingReadbackBuffer);
    releaseQuerySet(impl_->timingQuerySet);
    for (auto& group : impl_->activeDescriptorGroups) {
        releaseBindGroup(group);
    }
    for (auto& layout : impl_->activeDescriptorGroupLayouts) {
        releaseBindGroupLayout(layout);
    }
    releaseCommandBuffer(impl_->commandBuffer);
    releaseCommandEncoder(impl_->encoder);
    releaseBindGroup(impl_->pushConstantBindGroup);
    releaseBindGroupLayout(impl_->pushConstantBindGroupLayout);
    releaseBuffer(impl_->pushConstantUploadRing);
    releaseQueue(impl_->queue);
    releaseDevice(impl_->device);
#endif
}

void* WebGpuNativeCommandPayload::nativeQueue() const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return impl_ ? static_cast<void*>(impl_->queue) : nullptr;
#else
    return nullptr;
#endif
}

bool WebGpuNativeCommandPayload::ordinaryTransferOnly() const noexcept
{
    return impl_ && impl_->ordinaryTransfer;
}

const char* WebGpuNativeCommandPayload::restrictedRecordingDiagnostic() const noexcept
{
    if (const auto* id = preparedRecordingDiagnostic())
        return id;
    return ordinaryTransferOnly() ? "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND" : nullptr;
}

const char* WebGpuNativeCommandPayload::preparedRecordingDiagnostic() const noexcept
{
    if (impl_ && impl_->ordinaryPrepared)
        return "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED";
    return ordinaryTransferOnly() ? nullptr : webGpuGenericRecordingUsageDiagnostic;
}

const char* WebGpuNativeCommandPayload::nonTransferRecordingDiagnostic() const noexcept
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->failure_;
    return restrictedRecordingDiagnostic();
}

const char* WebGpuNativeCommandPayload::transitionRecordingDiagnostic() const noexcept
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->failure_;
    if (const auto* id = restrictedRecordingDiagnostic())
        return id;
    return "RHI-WEBGPU-BARRIER-LOWERING-UNPROVEN";
}

const char* WebGpuNativeCommandPayload::sampledGeometryBarrierDiagnostic(
    const rhi::ResourceBarrierDesc& desc) const noexcept
{
    if (impl_ && impl_->conformanceClear
        && (impl_->conformanceClear->sampled_ || impl_->conformanceClear->storageMrt_))
        return impl_->conformanceClear->failure_;
    return rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
        rhi::Backend::WebGPU, desc);
}

const char* WebGpuNativeCommandPayload::memoryBarrierRecordingDiagnostic() const noexcept
{
    if (const auto* id = restrictedRecordingDiagnostic())
        return id;
    return "RHI-WEBGPU-MEMORY-BARRIER-LOWERING-UNPROVEN";
}

const char* WebGpuNativeCommandPayload::sampledGeometryBarrierDiagnostic(
    const rhi::ResourceMemoryBarrierDesc& desc) const noexcept
{
    if (impl_ && impl_->conformanceClear
        && (impl_->conformanceClear->sampled_ || impl_->conformanceClear->storageMrt_))
        return impl_->conformanceClear->failure_;
    return rhi::semantics::GeometryInputAccessLowering::geometryInputBarrierDiagnostic(
        rhi::Backend::WebGPU, desc);
}

bool WebGpuNativeCommandPayload::completionDeviceMatches(void* nativeDevice) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return impl_ && nativeDevice && impl_->device == nativeDevice && impl_->queue
        && (!impl_->ordinaryOriginalTextureTransaction
            || (!impl_->ordinaryOriginalStateTransaction
                && !impl_->ordinaryOriginalTextureTransaction->validate()));
#else
    (void)nativeDevice;
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordBindlessFaultLatchClear(
    std::shared_ptr<WebGpuNativeBufferPayload> latch, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder || impl_->finished || impl_->submitted) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-NOT-RECORDING";
        return false;
    }
    const auto required =
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    if (!latch || !latch->buffer || latch->sizeBytes != sizeof(std::uint32_t)
        || wgpuBufferGetSize(latch->buffer) != sizeof(std::uint32_t)
        || (wgpuBufferGetUsage(latch->buffer) & required) != required) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-BUFFER-INVALID";
        return false;
    }
    if (latch->ownerDevice != impl_->device) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-DEVICE-MISMATCH";
        return false;
    }
    if (impl_->retainedBindlessFaultLatches.size()
        == impl_->retainedBindlessFaultLatches.capacity()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-CAPACITY";
        return false;
    }
    for (const auto& retained : impl_->retainedBindlessFaultLatches) {
        if (retained->buffer == latch->buffer) {
            diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-DUPLICATE";
            return false;
        }
    }
    impl_->retainedBindlessFaultLatches.push_back(std::move(latch));
    recordCommand(impl_->stats);
    diagnostics.clear();
    return true;
#else
    (void)latch;
    diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordBindlessFaultLatchClear(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_
        || impl_->leasedBindlessFaultLatches.size()
            == impl_->leasedBindlessFaultLatches.capacity()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-LEASE-CAPACITY";
        return false;
    }
    auto lease = rhi::BindlessFaultSubmissionLease::acquire(snapshot, diagnostics);
    if (!lease.valid())
        return false;
    const auto* buffer = lease.faultLatchBuffer();
    auto native = buffer ? webGpuNativePayloadFor(*buffer) : nullptr;
    if (!native || !native->buffer) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-LEASE-PAYLOAD-MISSING";
        return false;
    }
    if (!recordBindlessFaultLatchClear(native, diagnostics))
        return false;
    impl_->leasedBindlessFaultLatches.push_back({std::move(snapshot), std::move(lease)});
    return true;
#else
    (void)snapshot;
    diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::retainIndexedPipeline(const std::shared_ptr<const void>& object,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || impl_->finished || impl_->submitted) {
        diagnostics = "RHI-WEBGPU-BINDLESS-RETENTION-NOT-RECORDING";
        return false;
    }
    if (!object) {
        diagnostics = "RHI-WEBGPU-BINDLESS-RETENTION-PAYLOAD-MISSING";
        return false;
    }
    if (!authorization || !authorization->current(authorization)) {
        diagnostics = "RHI-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return false;
    }
    for (const auto& retained : impl_->retainedBindlessPipelines) {
        if (retained.pipeline.get() == object.get()) {
            if (retained.authorization.get() != authorization.get()) {
                diagnostics = "RHI-BINDLESS-PIPELINE-AUTHORIZATION-MISMATCH";
                return false;
            }
            diagnostics.clear();
            return true;
        }
    }
    if (impl_->retainedBindlessPipelines.size() == impl_->retainedBindlessPipelines.capacity()) {
        diagnostics = "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET";
        return false;
    }
    impl_->retainedBindlessPipelines.push_back({object, std::move(authorization)});
    diagnostics.clear();
    return true;
#else
    (void)object;
    (void)authorization;
    diagnostics = "RHI-WEBGPU-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::retainIndexedSnapshot(
    const std::shared_ptr<const void>& object, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || impl_->finished || impl_->submitted) {
        diagnostics = "RHI-WEBGPU-BINDLESS-RETENTION-NOT-RECORDING";
        return false;
    }
    if (!object) {
        diagnostics = "RHI-WEBGPU-BINDLESS-RETENTION-PAYLOAD-MISSING";
        return false;
    }
    for (const auto& retained : impl_->retainedBindlessSnapshots) {
        if (retained.get() == object.get()) {
            diagnostics.clear();
            return true;
        }
    }
    if (impl_->retainedBindlessSnapshots.size() == impl_->retainedBindlessSnapshots.capacity()) {
        diagnostics = "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET";
        return false;
    }
    impl_->retainedBindlessSnapshots.push_back(object);
    diagnostics.clear();
    return true;
#else
    (void)object;
    diagnostics = "RHI-WEBGPU-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordPreparedZeroBaseIndirectProducer(
    std::shared_ptr<const WebGpuNativeZeroBaseIndirectProducer> producer,
    std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->computePass || impl_->renderPass || impl_->finished || impl_->submitted) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-NOT-RECORDING";
        return false;
    }
    if (!producer || !producer->pipeline_ || !producer->pipeline_->pipeline || !producer->group_) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-PAYLOAD-MISSING";
        return false;
    }
    if (producer->device_ != impl_->device) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-DEVICE-MISMATCH";
        return false;
    }
    auto* retention = impl_->producerCompletion && impl_->producerCompletion->owner
        ? impl_->producerCompletion->owner->retained.get(impl_->producerCompletion->ticket)
        : nullptr;
    if (impl_->producerCompletion && !impl_->producerCompletion->owner->retained.accepting()) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-CLOSED";
        return false;
    }
    if (!retention || retention->producers.size() == retention->producers.capacity()) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-COMMAND-BUDGET";
        return false;
    }
    const auto cleared = std::find_if(impl_->retainedBindlessFaultLatches.begin(),
        impl_->retainedBindlessFaultLatches.end(),
        [&](const auto& latch) {
            return producer->faultLatch_ && latch->buffer == producer->faultLatch_->buffer;
        });
    if (cleared == impl_->retainedBindlessFaultLatches.end()) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-FAULT-NOT-CLEARED";
        return false;
    }
    for (const auto& previous : retention->producers) {
        if (previous.producer->faultLatch_->buffer == producer->faultLatch_->buffer) {
            diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-FAULT-ALIAS";
            return false;
        }
    }
    if (!producer->recordings_.tryAcquire(1u)) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-IN-FLIGHT";
        return false;
    }
    auto& retained = retention->producers;
    retained.emplace_back(std::move(producer));
    const auto& prepared = *retained.back().producer;
    clearActiveDescriptorState(*impl_);
    impl_->pushConstantGroup = UINT32_MAX;
    impl_->pushConstantBinding = UINT32_MAX;
    impl_->activeShaderPackageHash = prepared.pipeline_->shaderPackageHash;
    wgpuComputePassEncoderSetPipeline(impl_->computePass, prepared.pipeline_->pipeline);
    wgpuComputePassEncoderSetBindGroup(impl_->computePass, 0u, prepared.group_, 0u, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(impl_->computePass, 1u, 1u, 1u);
    recordCommand(impl_->stats);
    diagnostics.clear();
    return true;
#else
    (void)producer;
    diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordPreparedIndirectProcessor(
    std::shared_ptr<const WebGpuNativeIndirectProcessor> processor,
    const rhi::BindlessAccessDeclarations& producerAccess,
    const rhi::BindlessAccessDeclarations& consumerAccess,
    core::pmr::String& diagnostic,
    bool consumerRequired)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostic = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder || impl_->computePass || impl_->renderPass || impl_->finished
        || impl_->submitted) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NOT-RECORDING";
        return false;
    }
    if (!processor) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-MISSING";
        return false;
    }
    const auto prepared = processor->validate();
    if (!prepared.success) {
        diagnostic = prepared.diagnostics;
        return false;
    }
    const auto& producer = processor->producer_;
    if (producer->device_ != impl_->device) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH";
        return false;
    }
    auto* retention = impl_->producerCompletion && impl_->producerCompletion->owner
        ? impl_->producerCompletion->owner->retained.get(impl_->producerCompletion->ticket)
        : nullptr;
    if (!retention || !impl_->producerCompletion->owner->retained.accepting()
        || retention->processors.size() == retention->processors.capacity()
        || retention->producers.size() == retention->producers.capacity()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-COMMAND-BUDGET";
        return false;
    }
    if (impl_->retainedBindlessFaultLatches.size()
        == impl_->retainedBindlessFaultLatches.capacity()) {
        diagnostic = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-CAPACITY";
        return false;
    }
    for (const auto& latch : impl_->retainedBindlessFaultLatches)
        if (latch->buffer == processor->nativeBuffers_[1]->buffer) {
            diagnostic = "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-DUPLICATE";
            return false;
        }
    // All fallible L2 validation and both exclusive reservations precede native
    // mutation. The identity belongs to this encoder, never to the caller.
    auto lease = rhi::BindlessIndirectProcessorRecordingLease::acquire(
        processor->intent_, impl_->recordingIdentity, producerAccess, consumerAccess, diagnostic);
    if (!lease.valid())
        return false;
    if (!producer->recordings_.tryAcquire(1u)) {
        diagnostic = "RHI-WEBGPU-ZERO-BASE-PRODUCER-IN-FLIGHT";
        return false;
    }
    WebGpuRetainedZeroBaseProducer retainedProducer(producer);
    auto desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    constexpr char label[] = "Closed indirect processor";
    desc.label = {label, sizeof(label) - 1u};
    auto pass = wgpuCommandEncoderBeginComputePass(impl_->encoder, &desc);
    if (!pass) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-PASS-CREATION-FAILED";
        return false;
    }
    impl_->retainedBindlessFaultLatches.push_back(processor->nativeBuffers_[1]);
    wgpuComputePassEncoderSetPipeline(pass, producer->pipeline_->pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0u, producer->group_, 0u, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    // This is the only protected-write bypass: destinations and sources are
    // immutable processor-owned payloads, with exact contract-owned extents.
    wgpuCommandEncoderCopyBufferToBuffer(impl_->encoder,
        processor->nativeBuffers_[2]->buffer,
        0u,
        processor->destinations_[0]->buffer,
        0u,
        sizeof(rhi::DrawIndexedIndirectCommand));
    wgpuCommandEncoderCopyBufferToBuffer(impl_->encoder,
        processor->nativeBuffers_[3]->buffer,
        0u,
        processor->destinations_[1]->buffer,
        0u,
        sizeof(rhi::IndirectInstanceBase));
    retention->producers.push_back(std::move(retainedProducer));
    retention->processors.push_back(
        {std::move(processor), std::move(lease), consumerRequired, false});
    for (unsigned i = 0u; i < 5u; ++i)
        recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeComputePassBeginRecordCount;
        ++impl_->stats->nativeComputePassEndRecordCount;
        impl_->stats->nativeBufferCopyRecordCount += 2u;
    }
    diagnostic.clear();
    return true;
#else
    (void)processor;
    (void)producerAccess;
    (void)consumerAccess;
    (void)consumerRequired;
    diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-UNAVAILABLE";
    return false;
#endif
}

std::shared_ptr<WebGpuRhiBindlessFaultReadback>
WebGpuNativeCommandPayload::scheduleIndirectProcessorFaultReadback(
    const WebGpuNativeIndirectProcessor& processor, core::pmr::String& diagnostic)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostic = id;
        return {};
    }
    const auto live = processor.validate();
    if (!live.success) {
        diagnostic = live.diagnostics;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_
        || impl_->processorFaultOutcomes.size() == impl_->processorFaultOutcomes.capacity()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-FAULT-BUDGET";
        return {};
    }
    auto result = scheduleBindlessFaultReadback(processor.nativeBuffers_[1], diagnostic);
    if (result)
        impl_->processorFaultOutcomes.push_back(
            {processor.identity(), processor.intent().desc().consumerGeneration, result});
    return result;
#else
    diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-UNAVAILABLE";
    return {};
#endif
}

std::uint64_t WebGpuNativeCommandPayload::recordingIdentity() const noexcept
{
    return impl_ ? impl_->recordingIdentity : 0u;
}

std::shared_ptr<WebGpuOriginalStateTransaction>
WebGpuNativeCommandPayload::originalStateTransaction() const noexcept
{
    return impl_ ? impl_->ordinaryOriginalStateTransaction
                 : std::shared_ptr<WebGpuOriginalStateTransaction>{};
}

std::shared_ptr<WebGpuOriginalTextureUploadTransaction>
WebGpuNativeCommandPayload::originalTextureTransaction() const noexcept
{
    return impl_ ? impl_->ordinaryOriginalTextureTransaction
                 : std::shared_ptr<WebGpuOriginalTextureUploadTransaction>{};
}

std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>
WebGpuNativeCommandPayload::originalReadbackCopies() const noexcept
{
    return impl_ ? impl_->ordinaryOriginalReadbackCopies
                 : std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>{};
}

rhi::BindlessFaultReport WebGpuNativeCommandPayload::processorFaultReport(
    std::uint64_t identity) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (impl_)
        for (const auto& outcome : impl_->processorFaultOutcomes)
            if (outcome.identity == identity && outcome.result)
                return {outcome.snapshotGeneration,
                    outcome.result->status(),
                    outcome.result->faultBits()};
#else
    (void)identity;
#endif
    return {0u, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
}

std::shared_ptr<WebGpuRhiBindlessFaultReadback>
WebGpuNativeCommandPayload::scheduleBindlessFaultReadback(
    std::shared_ptr<WebGpuNativeBufferPayload> latch, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder || impl_->finished || impl_->submitted || impl_->computePass
        || impl_->renderPass) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-NOT-RECORDING";
        return {};
    }
    if (!latch || !latch->buffer || latch->sizeBytes != sizeof(std::uint32_t)
        || wgpuBufferGetSize(latch->buffer) != sizeof(std::uint32_t)
        || (wgpuBufferGetUsage(latch->buffer) & WGPUBufferUsage_CopySrc) == 0u) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-BUFFER-INVALID";
        return {};
    }
    if (latch->ownerDevice != impl_->device) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-DEVICE-MISMATCH";
        return {};
    }
    if (impl_->pendingBindlessFaultReadbacks.size()
        == impl_->pendingBindlessFaultReadbacks.capacity()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-CAPACITY";
        return {};
    }
    const auto cleared = std::find_if(impl_->retainedBindlessFaultLatches.begin(),
        impl_->retainedBindlessFaultLatches.end(),
        [&](const auto& retained) {
            return retained->buffer == latch->buffer;
        });
    if (cleared == impl_->retainedBindlessFaultLatches.end()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-NOT-CLEARED";
        return {};
    }
    for (const auto& pending : impl_->pendingBindlessFaultReadbacks) {
        if (pending.latch->buffer == latch->buffer) {
            diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-DUPLICATE";
            return {};
        }
    }
    const auto slot = impl_->pendingBindlessFaultReadbacks.size();
    if (slot >= impl_->preparedBindlessFaultReadbacks.size()
        || !impl_->preparedBindlessFaultReadbacks[slot]
        || !impl_->preparedBindlessFaultReadbacks[slot]->destination
        || !impl_->preparedBindlessFaultReadbacks[slot]->result) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-CAPACITY";
        return {};
    }
    auto callback = std::move(impl_->preparedBindlessFaultReadbacks[slot]);
    auto destination = std::exchange(callback->destination, nullptr);
    auto result = callback->result;
    wgpuCommandEncoderCopyBufferToBuffer(
        impl_->encoder, latch->buffer, 0u, destination, 0u, sizeof(std::uint32_t));
    impl_->pendingBindlessFaultReadbacks.push_back(
        {std::move(latch), destination, result, {}, 0u, std::move(callback)});
    recordCommand(impl_->stats);
    diagnostics.clear();
    return result;
#else
    (void)latch;
    diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
#endif
}

std::shared_ptr<WebGpuRhiBindlessFaultReadback>
WebGpuNativeCommandPayload::scheduleBindlessFaultReadback(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !snapshot) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-LEASE-SNAPSHOT-MISSING";
        return {};
    }
    auto lease = std::find_if(impl_->leasedBindlessFaultLatches.begin(),
        impl_->leasedBindlessFaultLatches.end(),
        [&](const auto& pending) {
            return pending.snapshot.get() == snapshot.get();
        });
    if (lease == impl_->leasedBindlessFaultLatches.end()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-LEASE-NOT-CLEARED";
        return {};
    }
    const auto* buffer = lease->lease.faultLatchBuffer();
    auto native = buffer ? webGpuNativePayloadFor(*buffer) : nullptr;
    if (!native || !native->buffer) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-LEASE-PAYLOAD-MISSING";
        return {};
    }
    if (impl_->bindlessFaultOutcomes.size() == impl_->bindlessFaultOutcomes.capacity()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-OUTCOME-CAPACITY";
        return {};
    }
    const auto generation = snapshot->generation();
    for (const auto& outcome : impl_->bindlessFaultOutcomes) {
        if (outcome.snapshotGeneration == generation) {
            diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-GENERATION-DUPLICATE";
            return {};
        }
    }
    auto result = scheduleBindlessFaultReadback(std::move(native), diagnostics);
    if (!result)
        return {};
    impl_->pendingBindlessFaultReadbacks.back().lease = std::move(lease->lease);
    impl_->pendingBindlessFaultReadbacks.back().snapshotGeneration = generation;
    impl_->bindlessFaultOutcomes.push_back({generation, result});
    impl_->leasedBindlessFaultLatches.erase(lease);
    return result;
#else
    (void)snapshot;
    diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
#endif
}

rhi::BindlessFaultReport WebGpuNativeCommandPayload::bindlessFaultReport(
    std::uint64_t snapshotGeneration) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (impl_) {
        for (const auto& outcome : impl_->bindlessFaultOutcomes) {
            if (outcome.snapshotGeneration == snapshotGeneration && outcome.result) {
                rhi::BindlessFaultReport report{
                    snapshotGeneration, outcome.result->status(), outcome.result->faultBits()};
                for (const auto& producer : impl_->processorFaultOutcomes)
                    if (producer.snapshotGeneration == snapshotGeneration && producer.result)
                        report = rhi::mergeBindlessFaultReports(report,
                            {snapshotGeneration,
                                producer.result->status(),
                                producer.result->faultBits()});
                return report;
            }
        }
    }
#else
    (void)snapshotGeneration;
#endif
    return {snapshotGeneration, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
}

bool WebGpuNativeCommandPayload::recordBufferBarrier(const WebGpuNativeBufferPayload& buffer,
    const rhi::ResourceBarrierDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->bufferBarrier(buffer, desc, diagnostics);
    (void)buffer;
    (void)desc;
    diagnostics = transitionRecordingDiagnostic();
    return false;
}

bool WebGpuNativeCommandPayload::recordTextureBarrier(const WebGpuNativeTexturePayload& texture,
    const rhi::ResourceBarrierDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->transition(*this, texture, desc, diagnostics);
    if (const auto* id = texture.contentAccessPolicy().ordinaryAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    (void)texture;
    (void)desc;
    diagnostics = transitionRecordingDiagnostic();
    return false;
}

bool WebGpuNativeCommandPayload::recordBufferMemoryBarrier(const WebGpuNativeBufferPayload& buffer,
    const rhi::ResourceMemoryBarrierDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && (impl_->conformanceClear->sampled_ || impl_->conformanceClear->storageMrt_)) {
        rhi::ResourceBarrierDesc dependency;
        dependency.resource = desc.resource;
        dependency.before = dependency.after = rhi::ResourceState::ShaderRead;
        dependency.sourceStages = desc.sourceStages;
        dependency.destinationStages = desc.destinationStages;
        dependency.sourceAccess = desc.sourceAccess;
        dependency.destinationAccess = desc.destinationAccess;
        return impl_->conformanceClear->bufferBarrier(buffer, dependency, diagnostics);
    }
    (void)buffer;
    (void)desc;
    diagnostics = memoryBarrierRecordingDiagnostic();
    return false;
}

bool WebGpuNativeCommandPayload::recordTextureMemoryBarrier(
    const WebGpuNativeTexturePayload& texture,
    const rhi::ResourceMemoryBarrierDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id = texture.contentAccessPolicy().ordinaryAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    (void)texture;
    (void)desc;
    diagnostics = memoryBarrierRecordingDiagnostic();
    return false;
}

bool WebGpuNativeCommandPayload::recordRenderPassBegin(const rhi::RenderPassDesc& desc,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& colorTargets,
    const core::pmr::Vector<const WebGpuNativeTexturePayload*>& resolveTargets,
    const WebGpuNativeTexturePayload* depthTarget,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->begin(
            desc, colorTargets, resolveTargets, depthTarget, diagnostics))
        return false;
    for (const auto* target : colorTargets)
        if (target)
            if (const auto* id = target->contentAccessPolicy().ordinaryAccessDiagnostic()) {
                diagnostics = id;
                return false;
            }
    for (const auto* target : resolveTargets)
        if (target)
            if (const auto* id = target->contentAccessPolicy().ordinaryAccessDiagnostic()) {
                diagnostics = id;
                return false;
            }
    if (depthTarget)
        if (const auto* id = depthTarget->contentAccessPolicy().ordinaryAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }

    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
    const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
    if (!operations.success) {
        diagnostics = operations.diagnostics;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const bool depthOnlyRenderPass =
        desc.colorAttachments.empty() && desc.hasDepthStencilAttachment;
    if (!impl_ || !impl_->encoder || impl_->renderPass
        || (desc.colorAttachments.empty() && !depthOnlyRenderPass)) {
        diagnostics =
            "WebGPU RHI native render pass requires a live encoder and color or depth target";
        return false;
    }
    if (!depthOnlyRenderPass && colorTargets.size() != desc.colorAttachments.size()) {
        diagnostics = "WebGPU RHI native render pass color target count does not match attachments";
        return false;
    }
    if (!depthOnlyRenderPass && resolveTargets.size() != colorTargets.size()) {
        diagnostics =
            "WebGPU RHI native render pass resolve target count does not match color targets";
        return false;
    }
    for (const auto* colorTarget : colorTargets) {
        if (!colorTarget || !colorTarget->texture
            || colorTarget->format == WGPUTextureFormat_Undefined) {
            diagnostics = "WebGPU RHI native render pass requires a compatible color target";
            return false;
        }
    }
    if (depthOnlyRenderPass
        && (!depthTarget || !depthTarget->texture
            || depthTarget->format == WGPUTextureFormat_Undefined)) {
        diagnostics = "WebGPU RHI native depth-only render pass requires a compatible depth target";
        return false;
    }
    if (desc.hasDepthStencilAttachment
        && (!depthTarget || !depthTarget->texture
            || depthTarget->format == WGPUTextureFormat_Undefined
            || (!depthOnlyRenderPass
                && (depthTarget->extent.width != colorTargets.front()->extent.width
                    || depthTarget->extent.height != colorTargets.front()->extent.height)))) {
        diagnostics = "WebGPU RHI native render pass requires a compatible depth target";
        return false;
    }

    // Render bundles are compatible with a render pass only when the actual
    // attachment formats and sample count match the encoder descriptor. Keep
    // this metadata from the validated native resources instead of deriving it
    // from the pipeline or from the logical attachment labels.
    impl_->activeRenderPassColorFormats.clear();
    impl_->activeRenderPipeline = nullptr;
    impl_->activeRenderPassDepthStencilFormat = rhi::TextureFormat::Unknown;
    impl_->activeRenderPassSampleCount = 0u;
    for (std::size_t index = 0; index < colorTargets.size(); ++index) {
        const auto* colorTarget = colorTargets[index];
        if (colorTarget->rhiFormat == rhi::TextureFormat::Unknown || colorTarget->sampleCount == 0u
            || (desc.colorAttachments[index].format != rhi::TextureFormat::Unknown
                && desc.colorAttachments[index].format != colorTarget->rhiFormat)) {
            diagnostics = "RHI-WEBGPU-RENDER-PASS-COLOR-METADATA-INVALID";
            return false;
        }
        if (impl_->activeRenderPassSampleCount == 0u) {
            impl_->activeRenderPassSampleCount = colorTarget->sampleCount;
        } else if (impl_->activeRenderPassSampleCount != colorTarget->sampleCount) {
            diagnostics = "RHI-WEBGPU-RENDER-PASS-SAMPLE-COUNT-MISMATCH";
            return false;
        }
        impl_->activeRenderPassColorFormats.push_back(colorTarget->rhiFormat);
        const auto* resolveTarget = resolveTargets[index];
        if (desc.colorAttachments[index].hasResolveTarget
            && (!resolveTarget || resolveTarget->rhiFormat != colorTarget->rhiFormat
                || resolveTarget->sampleCount != 1u
                || resolveTarget->extent.width != colorTarget->extent.width
                || resolveTarget->extent.height != colorTarget->extent.height)) {
            diagnostics = "RHI-WEBGPU-RENDER-PASS-RESOLVE-METADATA-INVALID";
            return false;
        }
    }
    if (desc.hasDepthStencilAttachment) {
        if (depthTarget->rhiFormat == rhi::TextureFormat::Unknown || depthTarget->sampleCount == 0u
            || (desc.depthStencilAttachment.format != rhi::TextureFormat::Unknown
                && desc.depthStencilAttachment.format != depthTarget->rhiFormat)) {
            diagnostics = "RHI-WEBGPU-RENDER-PASS-DEPTH-METADATA-INVALID";
            return false;
        }
        if (impl_->activeRenderPassSampleCount == 0u) {
            impl_->activeRenderPassSampleCount = depthTarget->sampleCount;
        } else if (impl_->activeRenderPassSampleCount != depthTarget->sampleCount) {
            diagnostics = "RHI-WEBGPU-RENDER-PASS-SAMPLE-COUNT-MISMATCH";
            return false;
        }
        impl_->activeRenderPassDepthStencilFormat = depthTarget->rhiFormat;
    }
    if (impl_->activeRenderPassSampleCount == 0u) {
        diagnostics = "RHI-WEBGPU-RENDER-PASS-SAMPLE-COUNT-MISSING";
        return false;
    }

    core::pmr::Vector<WGPURenderPassColorAttachment> colorAttachments(
        &core::defaultCpuMemoryResource());
    colorAttachments.resize(desc.colorAttachments.size(), WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT);
    if (!depthOnlyRenderPass) {
        for (std::size_t index = 0; index < colorTargets.size(); ++index) {
            const auto* colorTarget = colorTargets[index];
            rhi::TextureViewDesc viewDesc;
            viewDesc.label = desc.colorAttachments[index].label;
            viewDesc.format = colorTarget->rhiFormat;
            viewDesc.usage = rhi::TextureUsage::RenderAttachment;
            viewDesc.dimension = rhi::TextureViewDimension::Texture2D;
            viewDesc.aspect = rhi::TextureAspect::Color;
            auto colorViewPayload =
                getOrCreateWebGpuNativeTextureViewPayload(*colorTarget, viewDesc);
            auto colorView = colorViewPayload ? colorViewPayload->view : nullptr;
            if (!colorView) {
                diagnostics = "WebGPU RHI native render pass failed to create color view";
                return false;
            }
            impl_->retainedTextureViews.push_back(std::move(colorViewPayload));
            auto& colorAttachment = colorAttachments[index];
            colorAttachment.view = colorView;
            colorAttachment.loadOp =
                WebGpuAttachmentOperationLowering::load(desc.colorAttachments[index].loadOp);
            colorAttachment.storeOp =
                WebGpuAttachmentOperationLowering::store(desc.colorAttachments[index].storeOp);
            const auto clearColor = desc.colorAttachments[index].clearColor;
            colorAttachment.clearValue =
                WGPUColor{clearColor.r, clearColor.g, clearColor.b, clearColor.a};
            const auto* resolveTarget = resolveTargets[index];
            if (desc.colorAttachments[index].hasResolveTarget) {
                if (!resolveTarget || !resolveTarget->texture
                    || resolveTarget->format == WGPUTextureFormat_Undefined
                    || resolveTarget->extent.width != colorTarget->extent.width
                    || resolveTarget->extent.height != colorTarget->extent.height) {
                    diagnostics = "WebGPU RHI native render pass resolve target is incompatible";
                    return false;
                }
                rhi::TextureViewDesc resolveViewDesc;
                resolveViewDesc.label = desc.colorAttachments[index].label;
                resolveViewDesc.format = resolveTarget->rhiFormat;
                resolveViewDesc.usage = rhi::TextureUsage::RenderAttachment;
                resolveViewDesc.dimension = rhi::TextureViewDimension::Texture2D;
                resolveViewDesc.aspect = rhi::TextureAspect::Color;
                auto resolveViewPayload =
                    getOrCreateWebGpuNativeTextureViewPayload(*resolveTarget, resolveViewDesc);
                auto resolveView = resolveViewPayload ? resolveViewPayload->view : nullptr;
                if (!resolveView) {
                    diagnostics = "WebGPU RHI native render pass failed to create resolve view";
                    return false;
                }
                impl_->retainedTextureViews.push_back(std::move(resolveViewPayload));
                colorAttachment.resolveTarget = resolveView;
            } else if (resolveTarget) {
                diagnostics = "WebGPU RHI native render pass received an unexpected resolve target";
                return false;
            }
        }
    }

    WGPURenderPassDepthStencilAttachment depthStencilAttachment =
        WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPUTextureView depthView = nullptr;
    if (desc.hasDepthStencilAttachment) {
        rhi::TextureViewDesc depthViewDesc;
        depthViewDesc.label = desc.depthStencilAttachment.label;
        depthViewDesc.format = depthTarget->rhiFormat;
        depthViewDesc.usage = rhi::TextureUsage::RenderAttachment;
        depthViewDesc.dimension = rhi::TextureViewDimension::Texture2D;
        depthViewDesc.aspect = rhi::TextureAspect::Depth;
        auto depthViewPayload =
            getOrCreateWebGpuNativeTextureViewPayload(*depthTarget, depthViewDesc);
        depthView = depthViewPayload ? depthViewPayload->view : nullptr;
        if (!depthView) {
            diagnostics = "WebGPU RHI native render pass failed to create depth view";
            return false;
        }
        impl_->retainedTextureViews.push_back(std::move(depthViewPayload));
        if (const auto* id =
                WebGpuAttachmentOperationLowering::depthStencil(desc.depthStencilAttachment,
                    depthTarget->rhiFormat,
                    depthView,
                    depthStencilAttachment)) {
            diagnostics = id;
            return false;
        }
    }

    auto renderDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    renderDesc.label = webGpuNativeStringView(desc.label);
    renderDesc.colorAttachmentCount =
        depthOnlyRenderPass ? 0u : static_cast<std::uint32_t>(colorAttachments.size());
    renderDesc.colorAttachments = depthOnlyRenderPass ? nullptr : colorAttachments.data();
    if (desc.hasDepthStencilAttachment) {
        renderDesc.depthStencilAttachment = &depthStencilAttachment;
    }
    impl_->boundIndexBuffer = nullptr;
    impl_->boundIndexFormat = WGPUIndexFormat_Undefined;
    impl_->boundIndexOffset = 0u;
    impl_->renderPass = wgpuCommandEncoderBeginRenderPass(impl_->encoder, &renderDesc);
    if (!impl_->renderPass) {
        diagnostics = "WebGPU RHI native render pass creation failed";
        return false;
    }
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeRenderPassBeginRecordCount;
    }
    return true;
#else
    (void)desc;
    (void)colorTargets;
    (void)resolveTargets;
    (void)depthTarget;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordGraphicsPipelineBind(
    const WebGpuNativeRenderPipelinePayload& pipeline, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->pipeline(pipeline, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !pipeline.pipeline) {
        diagnostics = "WebGPU RHI native graphics bind requires render pass and pipeline";
        return false;
    }
    std::uint32_t highestGroup = 0u;
    bool hasGroup = false;
    if (pipeline.usesPushConstantGroup) {
        if (pipeline.pushConstantGroup == UINT32_MAX || pipeline.pushConstantBinding == UINT32_MAX
            || pipeline.bindGroupCount == 0u
            || pipeline.pushConstantGroup >= pipeline.bindGroupCount) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-PUSH-CONSTANT-GROUP-INVALID";
            return false;
        }
        highestGroup = pipeline.pushConstantGroup;
        hasGroup = true;
    }
    for (const auto& binding : pipeline.descriptorBindings) {
        if (binding.webGpuGroup == UINT32_MAX || pipeline.bindGroupCount == 0u
            || binding.webGpuGroup >= pipeline.bindGroupCount) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DESCRIPTOR-GROUP-INVALID";
            return false;
        }
        highestGroup = std::max(highestGroup, binding.webGpuGroup);
        hasGroup = true;
    }
    core::pmr::Vector<WGPUBindGroupLayout> newDescriptorLayouts;
    newDescriptorLayouts.resize(hasGroup ? highestGroup + 1u : 0u, nullptr);
    for (const auto& binding : pipeline.descriptorBindings) {
        if (binding.webGpuGroup == pipeline.pushConstantGroup
            || newDescriptorLayouts[binding.webGpuGroup]) {
            continue;
        }
        newDescriptorLayouts[binding.webGpuGroup] =
            wgpuRenderPipelineGetBindGroupLayout(pipeline.pipeline, binding.webGpuGroup);
        if (!newDescriptorLayouts[binding.webGpuGroup]) {
            for (auto& layout : newDescriptorLayouts) {
                releaseBindGroupLayout(layout);
            }
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DESCRIPTOR-GROUP-LAYOUT-MISSING";
            return false;
        }
    }
    WGPUBindGroupLayout newPushConstantLayout = nullptr;
    WGPUBindGroup newPushConstantBindGroup = nullptr;
    if (pipeline.usesPushConstantGroup) {
        newPushConstantLayout =
            wgpuRenderPipelineGetBindGroupLayout(pipeline.pipeline, pipeline.pushConstantGroup);
        if (!newPushConstantLayout) {
            for (auto& layout : newDescriptorLayouts) {
                releaseBindGroupLayout(layout);
            }
            diagnostics =
                "WebGPU RHI native graphics pipeline has no push-constant bind-group layout";
            return false;
        }
        newPushConstantBindGroup = createPersistentPushConstantBindGroup(
            *impl_, newPushConstantLayout, pipeline.pushConstantBinding, diagnostics);
        if (!newPushConstantBindGroup) {
            releaseBindGroupLayout(newPushConstantLayout);
            for (auto& layout : newDescriptorLayouts) {
                releaseBindGroupLayout(layout);
            }
            return false;
        }
    }
    wgpuRenderPassEncoderSetPipeline(impl_->renderPass, pipeline.pipeline);
    // StaticViewportScissor/1 uses encoder state for declared L2 rectangles.
    if (pipeline.hasViewport) {
        const auto& viewport = pipeline.viewport;
        wgpuRenderPassEncoderSetViewport(impl_->renderPass,
            viewport.x,
            viewport.y,
            viewport.width,
            viewport.height,
            viewport.minDepth,
            viewport.maxDepth);
    }
    if (pipeline.hasScissor) {
        const auto& scissor = pipeline.scissor;
        wgpuRenderPassEncoderSetScissorRect(impl_->renderPass,
            static_cast<std::uint32_t>(scissor.x),
            static_cast<std::uint32_t>(scissor.y),
            scissor.width,
            scissor.height);
    }
    clearActiveDescriptorState(*impl_);
    releaseBindGroup(impl_->pushConstantBindGroup);
    releaseBindGroupLayout(impl_->pushConstantBindGroupLayout);
    impl_->pushConstantGroup = UINT32_MAX;
    impl_->pushConstantBinding = UINT32_MAX;
    impl_->activeDescriptorGroupLayouts = std::move(newDescriptorLayouts);
    impl_->activeDescriptorGroups.resize(impl_->activeDescriptorGroupLayouts.size(), nullptr);
    impl_->activeDescriptorBindings = pipeline.descriptorBindings;
    impl_->activeDescriptorTables = pipeline.descriptorTables;
    if (pipeline.usesPushConstantGroup) {
        impl_->pushConstantGroup = pipeline.pushConstantGroup;
        impl_->pushConstantBinding = pipeline.pushConstantBinding;
        impl_->pushConstantBindGroupLayout = newPushConstantLayout;
        impl_->pushConstantBindGroup = newPushConstantBindGroup;
    }
    impl_->activeDescriptorSetGroups = pipeline.descriptorSetGroups;
    impl_->activeInputAttachmentLoweringValidated = pipeline.hasValidatedInputAttachmentLowering;
    impl_->activeShaderPackageHash = pipeline.shaderPackageHash;
    impl_->activeRenderPipeline = &pipeline;
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeGraphicsPipelineBindRecordCount;
    }
    return true;
#else
    (void)pipeline;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordRenderBundleExecute(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !bundle || !bundle->finished || !bundle->bundle) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-EXECUTE-STATE-INVALID";
        return false;
    }
    if (bundle->colorFormats != impl_->activeRenderPassColorFormats
        || bundle->depthStencilFormat != impl_->activeRenderPassDepthStencilFormat
        || bundle->sampleCount != impl_->activeRenderPassSampleCount) {
        diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-ATTACHMENT-COMPATIBILITY-MISMATCH";
        return false;
    }
    auto bundleHandle = bundle->bundle;
    wgpuRenderPassEncoderExecuteBundles(impl_->renderPass, 1u, &bundleHandle);
    recordCommand(impl_->stats);
    impl_->retainedRenderBundles.push_back(bundle);
    return true;
#else
    (void)bundle;
    diagnostics = "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordSetViewport(
    const rhi::ViewportState& viewport, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->viewport(viewport, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass) {
        diagnostics = "WebGPU RHI native setViewport requires an active render pass";
        return false;
    }
    // wgpuRenderPassEncoderSetViewport: x, y, width, height, minDepth, maxDepth.
    // WebGPU core - no extension gate. The viewport values are L1-validated
    // by the encoder before this call.
    wgpuRenderPassEncoderSetViewport(impl_->renderPass,
        viewport.x,
        viewport.y,
        viewport.width,
        viewport.height,
        viewport.minDepth,
        viewport.maxDepth);
    recordCommand(impl_->stats);
    return true;
#else
    (void)viewport;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordSetScissor(
    const rhi::ScissorState& scissor, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->scissor(scissor, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass) {
        diagnostics = "WebGPU RHI native setScissor requires an active render pass";
        return false;
    }
    // wgpuRenderPassEncoderSetScissorRect: x, y, width, height (uint32).
    wgpuRenderPassEncoderSetScissorRect(impl_->renderPass,
        static_cast<std::uint32_t>(scissor.x),
        static_cast<std::uint32_t>(scissor.y),
        scissor.width,
        scissor.height);
    recordCommand(impl_->stats);
    return true;
#else
    (void)scissor;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordRenderDescriptorSetBind(std::uint32_t set,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptorGroup,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics)
{
    if (descriptorGroup)
        if (const auto* id = descriptorGroup->ordinaryTextureAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->descriptors(
            descriptorGroup.get(), set, dynamicOffsetCount, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !descriptorGroup) {
        diagnostics = "WebGPU RHI native render descriptor bind requires render pass and group";
        return false;
    }
    if (dynamicOffsetCount != descriptorGroup->dynamicOffsetCount
        || (dynamicOffsetCount > 0u && !dynamicOffsets)) {
        diagnostics = "WebGPU RHI render descriptor bind has invalid dynamic offset count";
        return false;
    }
    if (descriptorGroup->dynamicOffsetBindings.size() != dynamicOffsetCount) {
        diagnostics =
            "WebGPU RHI render descriptor bind has invalid dynamic offset binding metadata";
        return false;
    }
    for (std::size_t index = 1u; index < descriptorGroup->dynamicOffsetBindings.size(); ++index) {
        if (descriptorGroup->dynamicOffsetBindings[index - 1u]
            >= descriptorGroup->dynamicOffsetBindings[index]) {
            diagnostics =
                "WebGPU RHI render descriptor bind dynamic offset metadata is not ordered";
            return false;
        }
    }
    for (std::uint32_t index = 0u; index < dynamicOffsetCount; ++index) {
        if (!validateDynamicOffsetValue(*descriptorGroup,
                descriptorGroup->dynamicOffsetBindings[index],
                dynamicOffsets[index],
                "WebGPU RHI render descriptor bind",
                diagnostics)) {
            return false;
        }
    }
    if (descriptorGroup->containsLoweredInputAttachment
        && !impl_->activeInputAttachmentLoweringValidated) {
        diagnostics =
            "WebGPU RHI input attachment bind requires a pipeline-validated lowering artifact";
        return false;
    }
    if (descriptorGroup->containsLoweredInputAttachment
        && (descriptorGroup->shaderPackageHash == 0 || impl_->activeShaderPackageHash == 0
            || descriptorGroup->shaderPackageHash != impl_->activeShaderPackageHash)) {
        diagnostics = "WebGPU RHI input attachment bind has a mismatched shader-package identity";
        return false;
    }
    std::uint32_t physicalGroup = 0u;
    if (!physicalGroupForCurrentPipeline(impl_->activeDescriptorSetGroups, set, physicalGroup)) {
        diagnostics = "WebGPU RHI descriptor bind has no lowering for the current pipeline set";
        return false;
    }
    WGPUBindGroup candidateGroup = nullptr;
    core::pmr::Vector<std::uint32_t> candidateDynamicOffsets;
    if (!buildCompactDescriptorGroup(*impl_,
            set,
            *descriptorGroup,
            dynamicOffsets,
            dynamicOffsetCount,
            physicalGroup,
            candidateGroup,
            candidateDynamicOffsets,
            diagnostics)) {
        return false;
    }
    if (candidateGroup) {
        wgpuRenderPassEncoderSetBindGroup(impl_->renderPass,
            physicalGroup,
            candidateGroup,
            static_cast<std::uint32_t>(candidateDynamicOffsets.size()),
            candidateDynamicOffsets.empty() ? nullptr : candidateDynamicOffsets.data());
        releaseBindGroup(impl_->activeDescriptorGroups[physicalGroup]);
        impl_->activeDescriptorGroups[physicalGroup] = candidateGroup;
        recordCommand(impl_->stats);
        if (impl_->stats) {
            ++impl_->stats->nativeDescriptorBindRecordCount;
        }
    } else {
        releaseBindGroup(impl_->activeDescriptorGroups[physicalGroup]);
        impl_->activeDescriptorGroups[physicalGroup] = nullptr;
    }
    retainBoundDescriptorSet(*impl_, set, descriptorGroup, dynamicOffsets, dynamicOffsetCount);
    return true;
#else
    (void)set;
    (void)descriptorGroup;
    (void)dynamicOffsets;
    (void)dynamicOffsetCount;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordIndexBufferBind(const WebGpuNativeBufferPayload& buffer,
    rhi::IndexFormat format,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->index(buffer, format, offsetBytes, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !buffer.buffer) {
        diagnostics = "WebGPU RHI index bind requires an active render pass and native buffer";
        return false;
    }
    const auto nativeFormat =
        format == rhi::IndexFormat::Uint16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
    wgpuRenderPassEncoderSetIndexBuffer(
        impl_->renderPass, buffer.buffer, nativeFormat, offsetBytes, WGPU_WHOLE_SIZE);
    impl_->boundIndexBuffer = buffer.buffer;
    impl_->boundIndexFormat = nativeFormat;
    impl_->boundIndexOffset = offsetBytes;
    recordCommand(impl_->stats);
    return true;
#else
    (void)buffer;
    (void)format;
    (void)offsetBytes;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordVertexBufferBind(std::uint32_t slot,
    const WebGpuNativeBufferPayload& buffer,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->vertex(slot, buffer, offsetBytes, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !buffer.buffer) {
        diagnostics = "WebGPU RHI vertex bind requires an active render pass and native buffer";
        return false;
    }
    wgpuRenderPassEncoderSetVertexBuffer(
        impl_->renderPass, slot, buffer.buffer, offsetBytes, WGPU_WHOLE_SIZE);
    recordCommand(impl_->stats);
    return true;
#else
    (void)slot;
    (void)buffer;
    (void)offsetBytes;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDrawIndexed(std::uint32_t indexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstIndex,
    std::int32_t vertexOffset,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->draw(
            true, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !impl_->boundIndexBuffer || indexCount == 0) {
        diagnostics = "WebGPU RHI native indexed draw requires a bound index buffer";
        return false;
    }
    if (!descriptorGroupsComplete(*impl_, diagnostics)) {
        return false;
    }
    wgpuRenderPassEncoderDrawIndexed(
        impl_->renderPass, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeIndexedDrawRecordCount;
    }
    return true;
#else
    (void)indexCount;
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordIndirectProcessorDraw(
    const WebGpuNativeIndirectProcessor& processor,
    const rhi::BindlessSnapshot& snapshot,
    const rhi::BindlessAuthorization& authorization,
    const rhi::BindlessAccessDeclarations& indirectAccess,
    core::pmr::String& diagnostic)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostic = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !impl_->activeRenderPipeline || impl_->finished
        || impl_->submitted) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-CONSUMER-NOT-RECORDING";
        return false;
    }
    const auto valid =
        rhi::validateBindlessIndirectProcessorConsumer(processor, snapshot, authorization);
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return false;
    }
    auto* retention = impl_->producerCompletion && impl_->producerCompletion->owner
        ? impl_->producerCompletion->owner->retained.get(impl_->producerCompletion->ticket)
        : nullptr;
    if (!retention) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NOT-PRODUCED";
        return false;
    }
    const auto produced = std::find_if(
        retention->processors.begin(), retention->processors.end(), [&](const auto& entry) {
            return entry.payload.get() == &processor && entry.lease.valid();
        });
    if (produced == retention->processors.end()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NOT-PRODUCED";
        return false;
    }
    if (produced->consumerDrawRecorded) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-ALREADY-CONSUMED";
        return false;
    }
    const auto pipeline = std::find_if(impl_->retainedBindlessPipelines.begin(),
        impl_->retainedBindlessPipelines.end(),
        [&](const auto& entry) {
            return entry.pipeline.get() == impl_->activeRenderPipeline
                && entry.authorization.get() == &authorization;
        });
    const auto nativeDescriptors =
        snapshot.descriptorSet() ? webGpuNativePayloadFor(*snapshot.descriptorSet()) : nullptr;
    const auto descriptors = std::find_if(impl_->boundDescriptorSets.begin(),
        impl_->boundDescriptorSets.end(),
        [&](const auto& entry) {
            return entry.logicalSet == snapshot.setIndex() && entry.payload == nativeDescriptors;
        });
    if (pipeline == impl_->retainedBindlessPipelines.end() || !nativeDescriptors
        || descriptors == impl_->boundDescriptorSets.end()) {
        diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-CONSUMER-MISMATCH";
        return false;
    }
    const auto access =
        rhi::validateBindlessIndirectDrawAccess(*processor.intent().desc().packet->physicalArgs(),
            0u,
            1u,
            sizeof(rhi::DrawIndexedIndirectCommand),
            indirectAccess);
    if (!access.success) {
        diagnostic = access.diagnostics;
        return false;
    }
    if (!recordDrawIndexedIndirect(*processor.destinations_[0],
            0u,
            1u,
            sizeof(rhi::DrawIndexedIndirectCommand),
            diagnostic))
        return false;
    produced->consumerDrawRecorded = true;
    return true;
#else
    (void)processor;
    (void)snapshot;
    (void)authorization;
    (void)indirectAccess;
    diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDrawIndexedIndirect(
    const WebGpuNativeBufferPayload& indirectBuffer,
    std::uint64_t indirectOffset,
    std::uint32_t drawCount,
    std::uint32_t stride,
    std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || !indirectBuffer.buffer || drawCount == 0u
        || stride < sizeof(rhi::DrawIndexedIndirectCommand) || (stride & 3u) != 0u) {
        diagnostics =
            "WebGPU RHI native indirect draw requires a pass, args buffer, and valid stride";
        return false;
    }
    if (!impl_->boundIndexBuffer) {
        diagnostics = "RHI-WEBGPU-INDIRECT-INDEX-BUFFER-UNBOUND";
        return false;
    }
    if (!descriptorGroupsComplete(*impl_, diagnostics)) {
        return false;
    }
    // WebGPU exposes one indexed indirect record per call. The L2 command
    // permits a count/stride pair, so lower it to repeated native calls while
    // retaining GPU-side argument reads and avoiding CPU readback.
    for (std::uint32_t draw = 0; draw < drawCount; ++draw) {
        const auto offset = indirectOffset + static_cast<std::uint64_t>(draw) * stride;
        if (offset < indirectOffset) {
            diagnostics = "WebGPU RHI native indirect draw offset overflow";
            return false;
        }
        wgpuRenderPassEncoderDrawIndexedIndirect(impl_->renderPass, indirectBuffer.buffer, offset);
    }
    recordCommand(impl_->stats);
    return true;
#else
    (void)indirectBuffer;
    (void)indirectOffset;
    (void)drawCount;
    (void)stride;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDrawIndexedIndirectCount(
    const WebGpuNativeBufferPayload& indirectBuffer,
    std::uint64_t indirectOffset,
    const WebGpuNativeBufferPayload& countBuffer,
    std::uint64_t countOffset,
    std::uint32_t maxDrawCount,
    std::uint32_t stride,
    std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)indirectBuffer;
    (void)indirectOffset;
    (void)countBuffer;
    (void)countOffset;
    (void)maxDrawCount;
    (void)stride;
    diagnostics = "WebGPU RHI does not provide GPU-counted indexed-indirect draw lowering";
    return false;
#else
    (void)indirectBuffer;
    (void)indirectOffset;
    (void)countBuffer;
    (void)countOffset;
    (void)maxDrawCount;
    (void)stride;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDraw(std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->draw(
            false, vertexCount, instanceCount, firstVertex, 0, firstInstance, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass || vertexCount == 0) {
        diagnostics = "WebGPU RHI native draw requires render pass and vertices";
        return false;
    }
    if (!descriptorGroupsComplete(*impl_, diagnostics)) {
        return false;
    }
    wgpuRenderPassEncoderDraw(
        impl_->renderPass, vertexCount, instanceCount, firstVertex, firstInstance);
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeDrawRecordCount;
    }
    return true;
#else
    (void)vertexCount;
    (void)instanceCount;
    (void)firstVertex;
    (void)firstInstance;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordTimestamp(
    const rhi::QueryPool& queryPool, const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || (!impl_->renderPass && !impl_->computePass)) {
        diagnostics = "WebGPU timestamp write requires an active render or compute pass";
        return false;
    }
    const auto* nativePool = asWebGpuRhiQueryPool(queryPool);
    if (!nativePool || queryPool.type() != rhi::QueryType::Timestamp
        || queryIndex >= nativePool->count() || !nativePool->nativeQuerySet()) {
        diagnostics = "WebGPU timestamp write received an incompatible query pool or index";
        return false;
    }
    const auto& payload = nativePool->payload();
    if (!payload) {
        diagnostics = "WebGPU timestamp write query pool payload is unavailable";
        return false;
    }
    bool retained = false;
    for (const auto& retainedPool : impl_->retainedTimingPools) {
        if (retainedPool == payload) {
            retained = true;
            break;
        }
    }
    if (!retained) {
        impl_->retainedTimingPools.push_back(payload);
    }
    if (!payload->markRecorded(queryIndex, diagnostics)) {
        return false;
    }
    impl_->recordedTimestampQueries.push_back({payload, queryIndex});
    if (impl_->renderPass) {
        wgpuRenderPassEncoderWriteTimestamp(
            impl_->renderPass, nativePool->nativeQuerySet(), queryIndex);
    } else {
        wgpuComputePassEncoderWriteTimestamp(
            impl_->computePass, nativePool->nativeQuerySet(), queryIndex);
    }
    recordCommand(impl_->stats);
    if (impl_->stats)
        ++impl_->stats->nativeTimestampRecordCount;
    return true;
#else
    (void)queryPool;
    (void)queryIndex;
    diagnostics = "WebGPU RHI timestamp lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordRenderPassEnd(std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->end(diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->renderPass) {
        diagnostics = "WebGPU RHI native render pass end requires an active pass";
        return false;
    }
    wgpuRenderPassEncoderEnd(impl_->renderPass);
    releaseRenderPass(impl_->renderPass);
    impl_->activeRenderPassColorFormats.clear();
    impl_->activeRenderPassDepthStencilFormat = rhi::TextureFormat::Unknown;
    impl_->activeRenderPassSampleCount = 1u;
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeRenderPassEndRecordCount;
    }
    return true;
#else
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordComputePassBegin(
    const rhi::ComputePassDesc& desc, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->computePass(true, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->encoder || impl_->computePass) {
        diagnostics = "WebGPU RHI native compute pass requires a live encoder";
        return false;
    }
    auto computeDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    computeDesc.label = webGpuNativeStringView(desc.label);
    impl_->computePass = wgpuCommandEncoderBeginComputePass(impl_->encoder, &computeDesc);
    if (!impl_->computePass) {
        diagnostics = "WebGPU RHI native compute pass creation failed";
        return false;
    }
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeComputePassBeginRecordCount;
    }
    return true;
#else
    (void)desc;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordComputePipelineBind(
    const WebGpuNativeComputePipelinePayload& pipeline, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->computePipeline(pipeline, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->computePass || !pipeline.pipeline) {
        diagnostics = "WebGPU RHI native compute bind requires compute pass and pipeline";
        return false;
    }
    std::uint32_t highestGroup = 0u;
    bool hasGroup = false;
    if (pipeline.usesPushConstantGroup) {
        if (pipeline.pushConstantGroup == UINT32_MAX || pipeline.pushConstantBinding == UINT32_MAX
            || pipeline.bindGroupCount == 0u
            || pipeline.pushConstantGroup >= pipeline.bindGroupCount) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-PUSH-CONSTANT-GROUP-INVALID";
            return false;
        }
        highestGroup = pipeline.pushConstantGroup;
        hasGroup = true;
    }
    for (const auto& binding : pipeline.descriptorBindings) {
        if (binding.webGpuGroup == UINT32_MAX || pipeline.bindGroupCount == 0u
            || binding.webGpuGroup >= pipeline.bindGroupCount) {
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DESCRIPTOR-GROUP-INVALID";
            return false;
        }
        highestGroup = std::max(highestGroup, binding.webGpuGroup);
        hasGroup = true;
    }
    core::pmr::Vector<WGPUBindGroupLayout> newDescriptorLayouts;
    newDescriptorLayouts.resize(hasGroup ? highestGroup + 1u : 0u, nullptr);
    for (const auto& binding : pipeline.descriptorBindings) {
        if (binding.webGpuGroup == pipeline.pushConstantGroup
            || newDescriptorLayouts[binding.webGpuGroup]) {
            continue;
        }
        newDescriptorLayouts[binding.webGpuGroup] =
            wgpuComputePipelineGetBindGroupLayout(pipeline.pipeline, binding.webGpuGroup);
        if (!newDescriptorLayouts[binding.webGpuGroup]) {
            for (auto& layout : newDescriptorLayouts) {
                releaseBindGroupLayout(layout);
            }
            diagnostics = "RHI-WEBGPU-COMPACT-REMAP-DESCRIPTOR-GROUP-LAYOUT-MISSING";
            return false;
        }
    }
    WGPUBindGroupLayout newPushConstantLayout = nullptr;
    WGPUBindGroup newPushConstantBindGroup = nullptr;
    if (pipeline.usesPushConstantGroup) {
        newPushConstantLayout =
            wgpuComputePipelineGetBindGroupLayout(pipeline.pipeline, pipeline.pushConstantGroup);
        if (!newPushConstantLayout) {
            for (auto& layout : newDescriptorLayouts) {
                releaseBindGroupLayout(layout);
            }
            diagnostics =
                "WebGPU RHI native compute pipeline has no push-constant bind-group layout";
            return false;
        }
        newPushConstantBindGroup = createPersistentPushConstantBindGroup(
            *impl_, newPushConstantLayout, pipeline.pushConstantBinding, diagnostics);
        if (!newPushConstantBindGroup) {
            releaseBindGroupLayout(newPushConstantLayout);
            for (auto& layout : newDescriptorLayouts) {
                releaseBindGroupLayout(layout);
            }
            return false;
        }
    }
    wgpuComputePassEncoderSetPipeline(impl_->computePass, pipeline.pipeline);
    clearActiveDescriptorState(*impl_);
    releaseBindGroup(impl_->pushConstantBindGroup);
    releaseBindGroupLayout(impl_->pushConstantBindGroupLayout);
    impl_->pushConstantGroup = UINT32_MAX;
    impl_->pushConstantBinding = UINT32_MAX;
    impl_->activeDescriptorGroupLayouts = std::move(newDescriptorLayouts);
    impl_->activeDescriptorGroups.resize(impl_->activeDescriptorGroupLayouts.size(), nullptr);
    impl_->activeDescriptorBindings = pipeline.descriptorBindings;
    impl_->activeDescriptorTables = pipeline.descriptorTables;
    if (pipeline.usesPushConstantGroup) {
        impl_->pushConstantGroup = pipeline.pushConstantGroup;
        impl_->pushConstantBinding = pipeline.pushConstantBinding;
        impl_->pushConstantBindGroupLayout = newPushConstantLayout;
        impl_->pushConstantBindGroup = newPushConstantBindGroup;
    }
    impl_->activeDescriptorSetGroups = pipeline.descriptorSetGroups;
    impl_->activeInputAttachmentLoweringValidated = pipeline.hasValidatedInputAttachmentLowering;
    impl_->activeShaderPackageHash = pipeline.shaderPackageHash;
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeComputePipelineBindRecordCount;
    }
    return true;
#else
    (void)pipeline;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDescriptorSetBind(std::uint32_t set,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptorGroup,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics)
{
    if (descriptorGroup)
        if (const auto* id = descriptorGroup->ordinaryTextureAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->storageDescriptors(
            descriptorGroup.get(), set, dynamicOffsetCount, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->computePass || !descriptorGroup) {
        diagnostics = "WebGPU RHI native descriptor bind requires compute pass and group";
        return false;
    }
    if (dynamicOffsetCount != descriptorGroup->dynamicOffsetCount
        || (dynamicOffsetCount > 0u && !dynamicOffsets)) {
        diagnostics = "WebGPU RHI compute descriptor bind has invalid dynamic offset count";
        return false;
    }
    if (descriptorGroup->dynamicOffsetBindings.size() != dynamicOffsetCount) {
        diagnostics =
            "WebGPU RHI compute descriptor bind has invalid dynamic offset binding metadata";
        return false;
    }
    for (std::size_t index = 1u; index < descriptorGroup->dynamicOffsetBindings.size(); ++index) {
        if (descriptorGroup->dynamicOffsetBindings[index - 1u]
            >= descriptorGroup->dynamicOffsetBindings[index]) {
            diagnostics =
                "WebGPU RHI compute descriptor bind dynamic offset metadata is not ordered";
            return false;
        }
    }
    for (std::uint32_t index = 0u; index < dynamicOffsetCount; ++index) {
        if (!validateDynamicOffsetValue(*descriptorGroup,
                descriptorGroup->dynamicOffsetBindings[index],
                dynamicOffsets[index],
                "WebGPU RHI compute descriptor bind",
                diagnostics)) {
            return false;
        }
    }
    if (descriptorGroup->containsLoweredInputAttachment
        && !impl_->activeInputAttachmentLoweringValidated) {
        diagnostics =
            "WebGPU RHI input attachment bind requires a pipeline-validated lowering artifact";
        return false;
    }
    if (descriptorGroup->containsLoweredInputAttachment
        && (descriptorGroup->shaderPackageHash == 0 || impl_->activeShaderPackageHash == 0
            || descriptorGroup->shaderPackageHash != impl_->activeShaderPackageHash)) {
        diagnostics = "WebGPU RHI input attachment bind has a mismatched shader-package identity";
        return false;
    }
    std::uint32_t physicalGroup = 0u;
    if (!physicalGroupForCurrentPipeline(impl_->activeDescriptorSetGroups, set, physicalGroup)) {
        diagnostics = "WebGPU RHI descriptor bind has no lowering for the current pipeline set";
        return false;
    }
    WGPUBindGroup candidateGroup = nullptr;
    core::pmr::Vector<std::uint32_t> candidateDynamicOffsets;
    if (!buildCompactDescriptorGroup(*impl_,
            set,
            *descriptorGroup,
            dynamicOffsets,
            dynamicOffsetCount,
            physicalGroup,
            candidateGroup,
            candidateDynamicOffsets,
            diagnostics)) {
        return false;
    }
    if (candidateGroup) {
        wgpuComputePassEncoderSetBindGroup(impl_->computePass,
            physicalGroup,
            candidateGroup,
            static_cast<std::uint32_t>(candidateDynamicOffsets.size()),
            candidateDynamicOffsets.empty() ? nullptr : candidateDynamicOffsets.data());
        releaseBindGroup(impl_->activeDescriptorGroups[physicalGroup]);
        impl_->activeDescriptorGroups[physicalGroup] = candidateGroup;
        recordCommand(impl_->stats);
        if (impl_->stats) {
            ++impl_->stats->nativeDescriptorBindRecordCount;
        }
    } else {
        releaseBindGroup(impl_->activeDescriptorGroups[physicalGroup]);
        impl_->activeDescriptorGroups[physicalGroup] = nullptr;
    }
    retainBoundDescriptorSet(*impl_, set, descriptorGroup, dynamicOffsets, dynamicOffsetCount);
    return true;
#else
    (void)set;
    (void)descriptorGroup;
    (void)dynamicOffsets;
    (void)dynamicOffsetCount;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordPushConstants(rhi::ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const void* data,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->push(stages, offsetBytes, sizeBytes, data, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)stages;
    if (impl_ && impl_->stats) {
        impl_->stats->pushConstantRequestedByteCount += sizeBytes;
    }
    if (offsetBytes > std::numeric_limits<std::uint32_t>::max() - sizeBytes) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeRange;
        diagnostics += "WebGPU RHI native push constants have an overflowing range";
        return false;
    }
    if (offsetBytes != 0u) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeRange;
        diagnostics += "WebGPU B2 push constants require an atomic full-block offset of zero";
        return false;
    }
    const auto bindingSize = offsetBytes + sizeBytes;
    if (!impl_ || !impl_->queue || !impl_->pushConstantUploadRing
        || !impl_->pushConstantBindGroupLayout || !impl_->pushConstantBindGroup || !data
        || sizeBytes == 0 || impl_->pushConstantGroup == UINT32_MAX
        || impl_->pushConstantBinding == UINT32_MAX || impl_->pushConstantMaxBindingSize == 0
        || impl_->pushConstantDynamicBindingSize == 0 || impl_->pushConstantOffsetAlignment == 0
        || bindingSize > impl_->pushConstantMaxBindingSize
        || bindingSize > impl_->pushConstantDynamicBindingSize
        || bindingSize > impl_->pushConstantUploadRingByteCapacity) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeUploadRing;
        diagnostics += "WebGPU RHI native push constants require bounded upload-ring storage";
        return false;
    }
    // B2 binds one persistent group created at pipeline bind. The upload-ring
    // slice is selected by WebGPU's dynamic-offset argument, so an atomic
    // update never creates a native bind group or changes pipeline state.
    // These values are copied from the browser-owned GPUDevice.limits
    // snapshot during device construction. Missing limits fail closed before
    // a native command payload can be created.
    rhi::semantics::PushConstantUploadSlice slice;
    if (const auto* id =
            rhi::semantics::planPushConstantUploadSlice(impl_->pushConstantUploadRingOffset,
                sizeBytes,
                impl_->pushConstantDynamicBindingSize,
                impl_->pushConstantUploadRingByteCapacity,
                {impl_->pushConstantUploadRingByteCapacity,
                    impl_->pushConstantMaxBindingSize,
                    impl_->pushConstantOffsetAlignment},
                slice)) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeUploadRing;
        diagnostics += id;
        diagnostics += " requested=";
        diagnostics += std::to_string(sizeBytes);
        diagnostics += " used=";
        diagnostics += std::to_string(impl_->pushConstantUploadRingOffset);
        diagnostics += " budget=";
        diagnostics += std::to_string(impl_->pushConstantUploadRingByteCapacity);
        if (impl_->stats) {
            ++impl_->stats->pushConstantRingExhaustionCount;
        }
        return false;
    }
    const auto dynamicOffset = slice.dynamicOffset;
    if (!impl_->renderPass && !impl_->computePass) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeEncoder;
        diagnostics += "WebGPU RHI native push constants require an active pass";
        return false;
    }
    wgpuQueueWriteBuffer(
        impl_->queue, impl_->pushConstantUploadRing, dynamicOffset, data, sizeBytes);
    if (impl_->renderPass) {
        wgpuRenderPassEncoderSetBindGroup(impl_->renderPass,
            impl_->pushConstantGroup,
            impl_->pushConstantBindGroup,
            1u,
            &dynamicOffset);
    } else if (impl_->computePass) {
        wgpuComputePassEncoderSetBindGroup(impl_->computePass,
            impl_->pushConstantGroup,
            impl_->pushConstantBindGroup,
            1u,
            &dynamicOffset);
    }
    impl_->pushConstantUploadRingOffset = static_cast<std::uint32_t>(slice.writeEnd);
    if (impl_->stats) {
        impl_->stats->pushConstantUsedByteCount = impl_->pushConstantUploadRingOffset;
    }
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativePushConstantUploadRingWriteCount;
        impl_->stats->nativePushConstantUploadRingByteCount += sizeBytes;
    }
    return true;
#else
    (void)stages;
    (void)offsetBytes;
    (void)sizeBytes;
    (void)data;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDispatch(std::uint32_t groupCountX,
    std::uint32_t groupCountY,
    std::uint32_t groupCountZ,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->dispatch(groupCountX, groupCountY, groupCountZ, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->computePass || groupCountX == 0 || groupCountY == 0 || groupCountZ == 0) {
        diagnostics = "WebGPU RHI native dispatch requires compute pass and non-zero groups";
        return false;
    }
    if (!descriptorGroupsComplete(*impl_, diagnostics)) {
        return false;
    }
    wgpuComputePassEncoderDispatchWorkgroups(
        impl_->computePass, groupCountX, groupCountY, groupCountZ);
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeDispatchRecordCount;
    }
    return true;
#else
    (void)groupCountX;
    (void)groupCountY;
    (void)groupCountZ;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordDispatchIndirect(
    const WebGpuNativeBufferPayload& indirectBuffer,
    std::uint64_t indirectOffset,
    std::pmr::string& diagnostics)
{
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->computePass || !indirectBuffer.buffer || (indirectOffset & 3u) != 0u) {
        diagnostics =
            "WebGPU RHI native indirect dispatch requires a pass, args buffer, and aligned offset";
        return false;
    }
    if (!descriptorGroupsComplete(*impl_, diagnostics)) {
        return false;
    }
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(
        impl_->computePass, indirectBuffer.buffer, indirectOffset);
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeDispatchIndirectRecordCount;
    }
    return true;
#else
    (void)indirectBuffer;
    (void)indirectOffset;
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordComputePassEnd(std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->computePass(false, diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !impl_->computePass) {
        diagnostics = "WebGPU RHI native compute pass end requires an active pass";
        return false;
    }
    wgpuComputePassEncoderEnd(impl_->computePass);
    releaseComputePass(impl_->computePass);
    recordCommand(impl_->stats);
    if (impl_->stats) {
        ++impl_->stats->nativeComputePassEndRecordCount;
    }
    return true;
#else
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::finish(std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->finish(diagnostics))
        return false;
    if (const auto* id =
            impl_ && impl_->conformanceClear ? nullptr : preparedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (impl_ && impl_->ordinaryTransfer) {
        if (impl_->ordinaryUnsupported) {
            diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
            return false;
        }
        if (!impl_->transferCompletion
            || !impl_->transferCompletion->owner->ordinaryTransfers.accepting()) {
            diagnostics = "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED";
            return false;
        }
        auto* root = impl_->transferCompletion->owner->ordinaryTransfers.get(
            impl_->transferCompletion->ticket);
        if (!root || !root->matches()) {
            impl_->ordinaryUnsupported = true;
            diagnostics = "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
            return false;
        }
        if (impl_->finished)
            return true;
        core::OrderedSpanCursor::Ticket finishTicket;
        if (impl_->frozenTransferUsage) {
            const char* id = nullptr;
            finishTicket = impl_->frozenTransferUsage->preflightFinish(id);
            if (!finishTicket) {
                diagnostics = id;
                return false;
            }
        }
        if (!impl_->encoder || !root->storage().sealForSubmission()) {
            diagnostics = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
            return false;
        }
        auto descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        descriptor.label = webGpuNativeStringView("RHI WebGPU bounded ordinary transfer");
        impl_->commandBuffer = wgpuCommandEncoderFinish(impl_->encoder, &descriptor);
        if (!impl_->commandBuffer) {
            if (impl_->frozenTransferUsage)
                impl_->frozenTransferUsage->abort();
            impl_->ordinaryUnsupported = true;
            diagnostics = "RHI-WEBGPU-TRANSFER-NATIVE-FINISH-FAILED";
            return false;
        }
        if (impl_->frozenTransferUsage) {
            const char* id = nullptr;
            if (!impl_->frozenTransferUsage->commit(finishTicket, id)
                || !impl_->frozenTransferUsage->complete()) {
                impl_->frozenTransferUsage->abort();
                impl_->ordinaryUnsupported = true;
                diagnostics = id ? id : "RHI-WEBGPU-TRANSFER-ARGUMENTS-INCOMPLETE";
                return false;
            }
        }
        impl_->finished = true;
        if (impl_->stats)
            ++impl_->stats->nativeCommandBufferCreateCount;
        return true;
    }
    if (!impl_ || !impl_->encoder || impl_->renderPass || impl_->computePass) {
        diagnostics = "WebGPU RHI native command finish requires a closed encoder";
        return false;
    }
    if (impl_->finished) {
        return true;
    }
    if (impl_->producerCompletion) {
        const auto* retention =
            impl_->producerCompletion->owner->retained.get(impl_->producerCompletion->ticket);
        if (!retention) {
            diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-CLOSED";
            return false;
        }
        for (const auto& processor : retention->processors)
            if (processor.consumerRequired) {
                if (!processor.consumerDrawRecorded) {
                    diagnostics = "RHI-WEBGPU-INDIRECT-PROCESSOR-CONSUMER-DRAW-MISSING";
                    return false;
                }
                const auto& desc = processor.payload->intent().desc();
                const auto latch =
                    webGpuNativePayloadFor(*desc.consumerSnapshot->faultLatchBuffer());
                const auto readback = std::find_if(impl_->pendingBindlessFaultReadbacks.begin(),
                    impl_->pendingBindlessFaultReadbacks.end(),
                    [&](const auto& pending) {
                        return pending.latch == latch && pending.result
                            && pending.snapshotGeneration == desc.consumerGeneration;
                    });
                if (readback == impl_->pendingBindlessFaultReadbacks.end()) {
                    diagnostics = "RHI-WEBGPU-INDIRECT-PROCESSOR-CONSUMER-FAULT-MISSING";
                    return false;
                }
            }
    }
    if (!impl_->leasedBindlessFaultLatches.empty()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-LEASE-READBACK-REQUIRED";
        return false;
    }
    if (impl_->retainedBindlessFaultLatches.size() != impl_->pendingBindlessFaultReadbacks.size()) {
        diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-REQUIRED";
        return false;
    }
    if (impl_->timingQuerySet) {
        if (!impl_->timingResolveBuffer || !impl_->timingReadbackBuffer
            || impl_->timingFrameSerial == 0u) {
            diagnostics = "WebGPU timestamp-query payload is incomplete";
            return false;
        }
        wgpuCommandEncoderWriteTimestamp(impl_->encoder, impl_->timingQuerySet, 1u);
        wgpuCommandEncoderResolveQuerySet(
            impl_->encoder, impl_->timingQuerySet, 0u, 2u, impl_->timingResolveBuffer, 0u);
        wgpuCommandEncoderCopyBufferToBuffer(impl_->encoder,
            impl_->timingResolveBuffer,
            0u,
            impl_->timingReadbackBuffer,
            0u,
            sizeof(std::uint64_t) * 2u);
        impl_->timingResolved = true;
        if (impl_->stats)
            ++impl_->stats->nativeTimestampResolveCount;
    }
    for (const auto& query : impl_->recordedTimestampQueries) {
        if (!query.pool || !query.pool->nativeQuerySet()) {
            diagnostics = "WebGPU timestamp query resolve payload is unavailable";
            return false;
        }
        auto resolveDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        resolveDesc.label = webGpuNativeStringView("RHI WebGPU query resolve buffer");
        resolveDesc.size = sizeof(std::uint64_t);
        resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        auto resolveBuffer = wgpuDeviceCreateBuffer(impl_->device, &resolveDesc);
        auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.label = webGpuNativeStringView("RHI WebGPU query readback buffer");
        readbackDesc.size = sizeof(std::uint64_t);
        readbackDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        auto readbackBuffer = wgpuDeviceCreateBuffer(impl_->device, &readbackDesc);
        if (!resolveBuffer || !readbackBuffer) {
            releaseBuffer(resolveBuffer);
            releaseBuffer(readbackBuffer);
            diagnostics = "WebGPU timestamp query readback buffer creation failed";
            return false;
        }
        wgpuCommandEncoderResolveQuerySet(
            impl_->encoder, query.pool->nativeQuerySet(), query.queryIndex, 1u, resolveBuffer, 0u);
        wgpuCommandEncoderCopyBufferToBuffer(
            impl_->encoder, resolveBuffer, 0u, readbackBuffer, 0u, sizeof(std::uint64_t));
        impl_->pendingTimestampReadbacks.push_back({
            resolveBuffer,
            readbackBuffer,
            query.pool,
            query.queryIndex,
        });
    }
    auto commandBufferDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    commandBufferDesc.label = webGpuNativeStringView("RHI WebGPU command buffer");
    impl_->commandBuffer = wgpuCommandEncoderFinish(impl_->encoder, &commandBufferDesc);
    if (!impl_->commandBuffer) {
        diagnostics = "WebGPU RHI native command buffer creation failed";
        return false;
    }
    impl_->finished = true;
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferCreateCount;
    }
    return true;
#else
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::submit(std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (impl_ && impl_->ordinaryPrepared) {
        diagnostics = "RHI-WEBGPU-ORDINARY-COMPLETION-UNPREPARED";
        return false;
    }
    if (impl_ && impl_->ordinaryTransfer) {
        if (impl_->ordinaryUnsupported) {
            diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
            return false;
        }
        if (!impl_->finished || !impl_->queue || !impl_->commandBuffer || impl_->submitted) {
            diagnostics = "RHI-WEBGPU-TRANSFER-SUBMISSION-STATE";
            return false;
        }
        auto& callback = impl_->transferCompletion;
        if (!callback || webGpuNativeDeviceLost()
            || !callback->owner->ordinaryTransfers.accepting()) {
            diagnostics = "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED";
            return false;
        }
        auto& pool = callback->owner->ordinaryTransfers;
        auto* root = pool.get(callback->ticket);
        if (!root || !root->matches()) {
            impl_->ordinaryUnsupported = true;
            diagnostics = "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
            return false;
        }
        if (root->storage().state() != rhi::common::CommandRetentionStorage::State::Sealed
            || !pool.submit(callback->ticket) || !root->storage().markSubmitted()) {
            diagnostics = "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED";
            return false;
        }
        // The original root is already owned by its cold pool reservation.
        auto info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous;
        info.callback = &completeWebGpuOrdinaryTransferRetention;
        info.userdata1 = callback.release();
        wgpuQueueSubmit(impl_->queue, 1u, &impl_->commandBuffer);
        impl_->submitted = true;
        (void)wgpuQueueOnSubmittedWorkDone(impl_->queue, info);
        if (impl_->stats)
            ++impl_->stats->nativeQueueSubmitCount;
        return true;
    }
    // A producer/fault/readback owner is only one part of a command graph.
    // Never accept general work until its complete original graph and cold
    // completion envelope cover every raw reference and publication domain.
    diagnostics = webGpuGenericCompletionResourceGraphDiagnostic;
    return false;
#else
    diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::finished() const
{
    return impl_ && impl_->finished;
}

void WebGpuNativeCommandPayload::retainTextureForSubmission(
    std::shared_ptr<WebGpuNativeTexturePayload> texture)
{
    if (impl_ && impl_->conformanceClear ? impl_->conformanceClear->failure_
                                         : restrictedRecordingDiagnostic()) {
        if (impl_)
            impl_->ordinaryUnsupported = true;
        return;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (impl_ && texture) {
        impl_->retainedTextures.push_back(std::move(texture));
    }
#else
    (void)texture;
#endif
}

bool WebGpuNativeCommandPayload::scheduleTextureReadback(
    std::shared_ptr<WebGpuNativeTexturePayload> source,
    rhi::TextureFormat format,
    const rhi::Extent3D& extent,
    std::string_view label,
    std::pmr::string& diagnostics)
{
    if (source)
        if (const auto* id = source->contentAccessPolicy().ordinaryAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
    if (const auto* id = restrictedRecordingDiagnostic()) {
        diagnostics = id;
        return {};
    }
    (void)format;
    (void)extent;
    (void)label;
    diagnostics =
        rhi::BorrowedTextureReadbackRetirementContract::lowering(rhi::Backend::WebGPU).diagnostic;
    return false;
}

std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeOrdinaryTransferCommandPayload(
    const WebGpuRhiResourceRegistry& registry,
    const rhi::GpuResourceId* ids,
    std::size_t count,
    const WebGpuOrdinaryTransferBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<WebGpuProducerCompletionOwner> completionOwner,
    std::shared_ptr<WebGpuNativeCommandStats> stats,
    core::pmr::String& diagnostic,
    std::shared_ptr<const WebGpuFrozenTransferArguments> frozenArguments)
{
    diagnostic.clear();
    if (const auto* id = WebGpuOrdinaryTransferRetention::validateBudget(budget)) {
        diagnostic = id;
        return {};
    }
    if (!quota || quota->limitBytes() != budget.retention.maxCpuMetadataBytes) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
        return {};
    }
    if (!completionOwner || !completionOwner->transferPreparationQuota || !completionOwner->device
        || !completionOwner->ordinaryTransfers.accepting()) {
        diagnostic = "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const char* admission = nullptr;
        auto root = WebGpuOrdinaryTransferRetention::prepare(
            registry, ids, count, budget, quota, admission);
        if (!root) {
            diagnostic = admission;
            return {};
        }
        if (frozenArguments) {
            if (!frozenArguments->hasQuotaBoundSharedOwner(frozenArguments)) {
                diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN";
                return {};
            }
            if (const auto* id = frozenArguments->validateAgainst(*root)) {
                diagnostic = id;
                return {};
            }
        }
        for (const auto& resource : root->resources()) {
            const auto device =
                resource.buffer ? resource.buffer->ownerDevice : resource.texture->ownerDevice;
            if (device != completionOwner->device) {
                diagnostic = "RHI-WEBGPU-TRANSFER-DEVICE-MISMATCH";
                return {};
            }
            if (resource.texture
                && (resource.textureDesc.format != rhi::TextureFormat::Rgba8Unorm
                    || resource.textureDesc.extent.depth != 1u
                    || resource.textureDesc.arrayLayerCount != 1u
                    || resource.textureDesc.sampleCount != 1u)) {
                diagnostic = "RHI-WEBGPU-TRANSFER-TEXTURE-DOMAIN-UNSUPPORTED";
                return {};
            }
        }
        auto payload = WebGpuNativeCommandPreparation::allocate(std::move(stats));
        auto& state = *payload->impl_;
        state.ordinaryTransfer = true;
        if (frozenArguments)
            state.frozenTransferUsage =
                core::makeUnique<WebGpuFrozenTransferArguments::Consumption>(
                    std::move(frozenArguments));
        state.transferCompletion = core::makeUnique<WebGpuOrdinaryTransferCompletionCallback>();
        state.transferCompletion->owner = std::move(completionOwner);
        state.transferCompletion->ticket =
            state.transferCompletion->owner->ordinaryTransfers.reserve(root);
        if (!state.transferCompletion->ticket) {
            diagnostic = "RHI-WEBGPU-TRANSFER-COMPLETION-CAPACITY";
            return {};
        }
        const char* nativeDiagnostic = nullptr;
        if (!WebGpuNativeCommandPreparation::open(*payload,
                state.transferCompletion->owner->device,
                "RHI WebGPU bounded ordinary transfer",
                nativeDiagnostic)) {
            diagnostic = std::string_view(nativeDiagnostic) == "RHI-WEBGPU-COMMAND-DEVICE-MISMATCH"
                ? "RHI-WEBGPU-TRANSFER-DEVICE-MISMATCH"
                : "RHI-WEBGPU-TRANSFER-NATIVE-PREPARATION-FAILED";
            return {};
        }
        return payload;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION";
        return {};
    }
#else
    (void)registry;
    (void)ids;
    (void)count;
    (void)completionOwner;
    (void)stats;
    (void)frozenArguments;
    diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return {};
#endif
}

std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeFrozenTransferCommandPayload(
    const WebGpuRhiResourceRegistry& registry,
    const rhi::GpuResourceId* ids,
    std::size_t count,
    const WebGpuOrdinaryTransferBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::shared_ptr<WebGpuProducerCompletionOwner> owner,
    std::shared_ptr<WebGpuNativeCommandStats> stats,
    core::pmr::String& diagnostic,
    std::shared_ptr<const WebGpuFrozenTransferArguments> arguments)
{
    if (!arguments || !arguments->hasQuotaBoundSharedOwner(arguments)) {
        diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN";
        return {};
    }
    return createWebGpuNativeOrdinaryTransferCommandPayload(registry,
        ids,
        count,
        budget,
        std::move(quota),
        std::move(owner),
        std::move(stats),
        diagnostic,
        std::move(arguments));
}

std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeCommandPayload(
    const std::pmr::string& label,
    std::uint32_t pushConstantUploadRingByteCapacity,
    std::uint32_t pushConstantMaxBindingSize,
    std::uint32_t pushConstantOffsetAlignment,
    std::uint64_t pushConstantSubmissionSerial,
    std::shared_ptr<WebGpuNativeCommandStats> stats,
    bool enableTimestampQueries,
    std::pmr::string& diagnostics,
    std::uint32_t maxBindlessFaultReadbacks,
    std::uint32_t maxBindlessPipelines,
    std::uint32_t maxPreparedZeroBaseProducers,
    std::shared_ptr<WebGpuProducerCompletionOwner> producerCompletionOwner,
    std::uint32_t maxRetainedBindlessSnapshots)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (maxPreparedZeroBaseProducers > maxBindlessFaultReadbacks) {
        diagnostics = "RHI-WEBGPU-ZERO-BASE-PRODUCER-FAULT-BUDGET";
        return {};
    }
    if (maxPreparedZeroBaseProducers
        && (!producerCompletionOwner || !producerCompletionOwner->retained.accepting())) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-OWNER-MISSING-OR-CLOSED";
        return {};
    }
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostics = "WebGPU RHI native command payload requires browser WebGPU";
        return nullptr;
    }
    if (pushConstantUploadRingByteCapacity == 0) {
        diagnostics = "WebGPU RHI native command payload requires device and upload ring";
        return nullptr;
    }
    if (pushConstantMaxBindingSize == 0 || pushConstantOffsetAlignment == 0
        || (pushConstantOffsetAlignment & (pushConstantOffsetAlignment - 1u)) != 0u) {
        diagnostics = "WebGPU RHI native command payload requires queried uniform-buffer limits";
        return nullptr;
    }
    std::shared_ptr<WebGpuNativeCommandPayload> payload;
    WebGpuNativeCommandPayload::Impl* impl = nullptr;
    try {
        auto storage = core::makeUnique<WebGpuNativeCommandPayload::Impl>();
        impl = storage.get();
        payload = core::makeShared<WebGpuNativeCommandPayload>(std::move(storage));
    } catch (const std::bad_alloc&) {
        diagnostics = "RHI-WEBGPU-COMMAND-PREPARATION-ALLOCATION";
        return {};
    }
    auto* device = emscripten_webgpu_get_device();
    impl->device = device;
    if (!device) {
        diagnostics = "WebGPU RHI native command payload requires device and upload ring";
        return nullptr;
    }
    if (maxPreparedZeroBaseProducers && producerCompletionOwner->device != device) {
        diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-DEVICE-MISMATCH";
        return {};
    }
    auto* queue = wgpuDeviceGetQueue(device);
    impl->queue = queue;
    if (!queue) {
        diagnostics = "WebGPU RHI native command payload requires queue";
        return nullptr;
    }

    auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoderDesc.label = webGpuNativeStringView(label);
    auto* encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    impl->encoder = encoder;
    if (!encoder) {
        diagnostics = "WebGPU RHI native command encoder creation failed";
        return nullptr;
    }

    auto ringDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    ringDesc.label = webGpuNativeStringView("RHI WebGPU push constant upload ring");
    ringDesc.size = pushConstantUploadRingByteCapacity;
    ringDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    auto* uploadRing = wgpuDeviceCreateBuffer(device, &ringDesc);
    impl->pushConstantUploadRing = uploadRing;
    if (!uploadRing) {
        diagnostics = "WebGPU RHI native push-constant upload ring creation failed";
        return nullptr;
    }

    try {
        impl->stats = std::move(stats);
        impl->pushConstantUploadRingByteCapacity = pushConstantUploadRingByteCapacity;
        impl->pushConstantMaxBindingSize = pushConstantMaxBindingSize;
        impl->pushConstantDynamicBindingSize =
            std::min(pushConstantMaxBindingSize, rhi::kL2AtomicPushConstantLimitBytes);
        impl->pushConstantOffsetAlignment = pushConstantOffsetAlignment;
        impl->pushConstantSubmissionSerial = pushConstantSubmissionSerial;
        impl->retainedBindlessFaultLatches.reserve(maxBindlessFaultReadbacks);
        impl->leasedBindlessFaultLatches.reserve(maxBindlessFaultReadbacks);
        impl->pendingBindlessFaultReadbacks.reserve(maxBindlessFaultReadbacks);
        impl->preparedBindlessFaultReadbacks.reserve(maxBindlessFaultReadbacks);
        for (std::uint32_t slot = 0u; slot < maxBindlessFaultReadbacks; ++slot) {
            auto callback = core::makeUnique<WebGpuBindlessFaultReadbackCallbackState>();
            callback->result = core::makeShared<WebGpuRhiBindlessFaultReadback>();
            auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            readbackDesc.label = webGpuNativeStringView("RHI WebGPU cold bindless fault readback");
            readbackDesc.size = sizeof(std::uint32_t);
            readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            callback->destination = wgpuDeviceCreateBuffer(device, &readbackDesc);
            if (!callback->destination) {
                diagnostics = "RHI-WEBGPU-BINDLESS-FAULT-READBACK-CREATION-FAILED";
                return {};
            }
            impl->preparedBindlessFaultReadbacks.push_back(std::move(callback));
        }
        impl->bindlessFaultOutcomes.reserve(maxBindlessFaultReadbacks);
        impl->retainedBindlessSnapshots.reserve(maxRetainedBindlessSnapshots);
        impl->retainedBindlessPipelines.reserve(maxBindlessPipelines);
        impl->processorFaultOutcomes.reserve(maxPreparedZeroBaseProducers);
        if (maxPreparedZeroBaseProducers) {
            auto retention = core::makeUnique<WebGpuZeroBaseProducerRetention>();
            retention->producers.reserve(maxPreparedZeroBaseProducers);
            retention->processors.reserve(maxPreparedZeroBaseProducers);
            auto completion = core::makeUnique<WebGpuProducerCompletionCallback>();
            completion->owner = std::move(producerCompletionOwner);
            completion->ticket = completion->owner->retained.reserve(retention);
            if (!completion->ticket) {
                diagnostics = "RHI-WEBGPU-PRODUCER-COMPLETION-CAPACITY";
                return {};
            }
            impl->producerCompletion = std::move(completion);
        }
        if (impl->stats) {
            impl->stats->pushConstantBudgetByteCapacity = pushConstantUploadRingByteCapacity;
            impl->stats->pushConstantSubmissionSerial = pushConstantSubmissionSerial;
        }
        if (enableTimestampQueries && webGpuNativeTimestampQuerySupported()) {
            auto queryDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
            queryDesc.label = webGpuNativeStringView("RHI WebGPU frame timestamp query set");
            queryDesc.type = WGPUQueryType_Timestamp;
            queryDesc.count = 2u;
            impl->timingQuerySet = wgpuDeviceCreateQuerySet(device, &queryDesc);

            auto resolveDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            resolveDesc.label = webGpuNativeStringView("RHI WebGPU timestamp resolve buffer");
            resolveDesc.size = sizeof(std::uint64_t) * 2u;
            resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
            impl->timingResolveBuffer = wgpuDeviceCreateBuffer(device, &resolveDesc);

            auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            readbackDesc.label = webGpuNativeStringView("RHI WebGPU timestamp readback buffer");
            readbackDesc.size = sizeof(std::uint64_t) * 2u;
            readbackDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            impl->timingReadbackBuffer = wgpuDeviceCreateBuffer(device, &readbackDesc);

            if (!impl->timingQuerySet || !impl->timingResolveBuffer
                || !impl->timingReadbackBuffer) {
                diagnostics = "WebGPU timestamp-query payload creation failed";
                return nullptr;
            }
            impl->timingFrameSerial = webGpuNativeAllocateTimingFrameSerial();
            wgpuCommandEncoderWriteTimestamp(impl->encoder, impl->timingQuerySet, 0u);
        }
        if (impl->stats) {
            ++impl->stats->nativeCommandEncoderCreateCount;
        }
        return payload;
    } catch (const std::bad_alloc&) {
        diagnostics = "RHI-WEBGPU-COMMAND-PREPARATION-ALLOCATION";
        return {};
    }
#else
    (void)label;
    (void)pushConstantUploadRingByteCapacity;
    (void)pushConstantMaxBindingSize;
    (void)pushConstantOffsetAlignment;
    (void)pushConstantSubmissionSerial;
    (void)stats;
    (void)maxBindlessFaultReadbacks;
    (void)maxBindlessPipelines;
    (void)maxPreparedZeroBaseProducers;
    (void)producerCompletionOwner;
    (void)maxRetainedBindlessSnapshots;
    diagnostics = "WebGPU RHI native commands require an Emscripten WebGPU build";
    return nullptr;
#endif
}

bool webGpuOrdinaryCompletionOwnerClosed(
    const std::weak_ptr<WebGpuOrdinaryCompletionOwner>& original) noexcept
{
    const auto owner = original.lock();
    return !owner || owner->retained.closed();
}

} // namespace arp::rhi_webgpu
