#pragma once
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "webgpu_async_wait_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Backend-private real wrapper probe. No original Source or GPU execution proof.
class WebGpuDescriptorDependencyRetirementFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void prepare()
    {
        using namespace rhi_webgpu;
        assert(!caller_ && !custodian_ && !quota_ && !webGpuNativeDeviceLost());
        quota_ = core::makeShared<core::CpuMemoryQuota>(65536u);
        core::CpuMemoryQuotaScope allocation(quota_);
        EM_ASM({
            // clang-format off
                Module.arpDescriptorRetirementScope = 0;
                Module.preinitializedWebGPUDevice.pushErrorScope("validation");
            // clang-format on
        });
        rhi::BufferDesc bufferDesc;
        bufferDesc.label = "retirement storage";
        bufferDesc.sizeBytes = 16u;
        bufferDesc.usage = rhi::ResourceUsage::StorageBuffer;
        bufferDesc.initialState = rhi::ResourceState::ShaderRead;
        auto buffer = createWebGpuNativeBufferPayload(bufferDesc);
        assert(buffer && buffer->buffer);
        auto sampler = createWebGpuNativeSamplerPayload(rhi::SamplerDesc{});
        assert(sampler && sampler->sampler);
        auto group = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
        auto device = emscripten_webgpu_get_device();
        assert(device);
        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{
            WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        layoutEntries[0].binding = 0u;
        layoutEntries[0].visibility = WGPUShaderStage_Compute;
        layoutEntries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        layoutEntries[0].buffer.hasDynamicOffset = WGPU_FALSE;
        layoutEntries[0].buffer.minBindingSize = bufferDesc.sizeBytes;
        layoutEntries[1].binding = 1u;
        layoutEntries[1].visibility = WGPUShaderStage_Compute;
        layoutEntries[1].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
        auto layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = layoutEntries.size();
        layoutDesc.entries = layoutEntries.data();
        group->layout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
        assert(group->layout);
        std::array<WGPUBindGroupEntry, 2> entries{
            WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        entries[0].binding = 0u;
        entries[0].buffer = buffer->buffer;
        entries[0].offset = 0u;
        entries[0].size = bufferDesc.sizeBytes;
        entries[1].binding = 1u;
        entries[1].sampler = sampler->sampler;
        auto groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        groupDesc.layout = group->layout;
        groupDesc.entryCount = entries.size();
        groupDesc.entries = entries.data();
        group->group = wgpuDeviceCreateBindGroup(device, &groupDesc);
        assert(group->group);
        wgpuDeviceRelease(device);
        WebGpuNativeDescriptorBindingPayload retainedSampler;
        retainedSampler.binding = {1u, rhi::DescriptorType::Sampler, rhi::ShaderStage::Compute, 1u};
        retainedSampler.sampler = sampler;
        group->bindings.push_back(std::move(retainedSampler));
        rhi::RenderBundleDesc bundleDesc;
        bundleDesc.label = "retirement empty native bundle";
        bundleDesc.maxCommandCount = 1u;
        bundleDesc.colorFormats.push_back(rhi::TextureFormat::Rgba8Unorm);
        core::pmr::String diagnostic;
        caller_ = createWebGpuNativeRenderBundlePayload(bundleDesc, diagnostic);
        assert(caller_ && caller_->encoder);
        // A valid empty native bundle is enough to test host-reference release;
        // no wrapper pipelineBound/commandCount/execution proof is fabricated.
        auto finishDesc = WGPU_RENDER_BUNDLE_DESCRIPTOR_INIT;
        caller_->bundle = wgpuRenderBundleEncoderFinish(caller_->encoder, &finishDesc);
        assert(caller_->bundle);
        caller_->retainedBuffers.push_back(buffer);
        caller_->retainedDescriptorGroups.push_back(group);
        wgpuBindGroupAddRef(group->group);
        caller_->retainedNativeDescriptorGroups.push_back(group->group);
        custodian_ = caller_;
        EM_ASM(
            {
                // clang-format off
                    Module.arpDescriptorRetirementObjects =
                        Array.of(WebGPU.getJsObject($0), WebGPU.getJsObject($1), WebGPU.getJsObject($2),
                                 WebGPU.getJsObject($3), WebGPU.getJsObject($4), WebGPU.getJsObject($5));
                    Module.preinitializedWebGPUDevice.popErrorScope().then(
                        error => { Module.arpDescriptorRetirementScope = error ? -1 : 1; },
                        () => { Module.arpDescriptorRetirementScope = -1; });
                // clang-format on
            },
            buffer->buffer,
            sampler->sampler,
            group->layout,
            group->group,
            caller_->encoder,
            caller_->bundle);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpDescriptorRetirementScope;
                // clang-format on
            }) != 0;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpDescriptorRetirementScope;
            // clang-format on
        }) == 1);
        live(true);
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_DEPENDENCY_RETIREMENT_PREPARED_OK native-wrappers6 payload-custodians2 validation-scope-clean execution-unproven");
    }

    static void verifyLoss()
    {
        assert(caller_ && custodian_ && quota_ && rhi_webgpu::webGpuNativeDeviceLost());
        live(true);
        caller_.reset();
        assert(custodian_.use_count() == 1u);
        live(true);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        {
            core::CpuMemoryQuotaLease full(quota_, quota_->limitBytes() - quota_->liveBytes());
            assert(full.charged());
            custodian_.reset();
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount
            && !quota_->liveBytes());
        live(false);
        quota_.reset();
        EM_ASM({
            // clang-format off
                delete Module.arpDescriptorRetirementObjects;
                delete Module.arpDescriptorRetirementScope;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_DEPENDENCY_RETIREMENT_LOSS_OK actual-loss retained-until-last-custodian native-wrappers6-zero quota-zero allocation0");
    }

private:
    static void live(bool expected)
    {
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           const wrappers = Object.values(WebGPU.Internals.jsObjects);
                           const objects = Module.arpDescriptorRetirementObjects;
                           return objects.length === 6 && ($0 ? objects.every(object => wrappers.includes(object))
                                                              : !objects.some(object => wrappers.includes(object)));
                       // clang-format on
                   },
                   expected)
            == 1);
    }

    static inline std::shared_ptr<rhi_webgpu::WebGpuNativeRenderBundlePayload> caller_, custodian_;
    static inline std::shared_ptr<core::CpuMemoryQuota> quota_;
#endif
};
} // namespace arp::test
