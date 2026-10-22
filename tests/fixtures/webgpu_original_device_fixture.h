#pragma once
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <cstdio>

namespace arp::test {
// A real queried facade for original-resource tests. The caller owns all
// workload budgets; this helper supplies no fallback limits or execution grant.
class WebGpuOriginalDeviceFixture final {
public:
    static rhi_webgpu::WebGpuRhiDeviceDesc describe(core::StringView label,
        std::uint64_t memoryBytes,
        std::uint32_t pushBytes,
        std::uint64_t uploadBytes)
    {
        using namespace rhi_webgpu;
        const auto limits = webGpuNativeDeviceLimits();
        rhi::WebGpuRuntimeProbe probe;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        probe.browserRuntime = EM_ASM_INT({
            // clang-format off
                return !!globalThis.navigator?.gpu;
            // clang-format on
        });
        probe.canvasSurface = EM_ASM_INT({
            // clang-format off
                    return document.getElementById('canvas') instanceof HTMLCanvasElement;
            // clang-format on
        });
        probe.nativeDevice = EM_ASM_INT({
            // clang-format off
                    return Module.preinitializedWebGPUDevice instanceof GPUDevice;
            // clang-format on
        });
#endif
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        EM_ASM({
            // clang-format off
                Module.tableRegistrationRuntimeProbe = true;
            // clang-format on
        });
#endif
        probe.computeDispatch = webGpuNativeDeviceSupportsComputeDispatch();
        probe.storageTextureBinding = webGpuNativeDeviceSupportsStorageTextureBinding();
        probe.storageBuffer = webGpuNativeDeviceSupportsStorageBuffer();
        probe.rgba16FloatStorage =
            webGpuNativeDeviceSupportsStorageTexture(rhi::TextureFormat::Rgba16Float);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        EM_ASM({
            // clang-format off
                Module.tableRegistrationRuntimeProbe = false;
            // clang-format on
        });
#endif
        probe.maxTexture2DSize = limits.maxTexture2DSize;
        probe.maxColorAttachments = limits.maxColorAttachments;
        probe.maxDescriptorBindings = limits.maxDescriptorBindings;
        probe.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
        probe.maxSamplersPerShaderStage = limits.maxSamplersPerShaderStage;
        probe.maxUniformBuffersPerShaderStage = limits.maxUniformBuffersPerShaderStage;
        probe.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
        probe.maxStorageBuffersPerShaderStage = limits.maxStorageBuffersPerShaderStage;
        probe.maxBindGroups = limits.maxBindGroups;
        probe.uniformBufferOffsetAlignment = limits.uniformBufferOffsetAlignment;
        probe.storageBufferOffsetAlignment = limits.storageBufferOffsetAlignment;
        probe.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
        probe.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
        probe.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
        probe.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
        probe.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
        probe.maxComputeWorkgroupStorageSize = limits.maxComputeWorkgroupStorageSize;
        WebGpuRhiDeviceDesc desc;
        desc.label = label;
        desc.runtimeProfile = rhi::buildWebGpuRuntimeProfile(probe);
        auto bridge = rhi::semantics::buildWebGpuRuntimeCapabilityContract(desc.runtimeProfile);
        if (!bridge.success)
            std::fprintf(stderr, "%s\n", bridge.diagnostics.c_str());
        assert(bridge.success);
        desc.capabilityContract = std::move(bridge.contract);
        desc.memoryBudgetBytes = memoryBytes;
        desc.pushConstantDeclaredWorkloadByteBudget = pushBytes;
        desc.uploadStagingByteCapacity = uploadBytes;
        const auto report = describeWebGpuRhiDevice(desc);
        if (!report.success)
            std::fprintf(stderr, "%s\n", report.diagnostics.c_str());
        assert(report.success);
        return desc;
    }
};
} // namespace arp::test
