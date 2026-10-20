#pragma once
#include "rhi/gpu_bindless.h"
#include "rhi/semantics/bindless_workload_resources.h"

#include <algorithm>
#include <array>

namespace arp::rhi_webgpu {
// Pre-device adapter facts only. This type cannot be used as runtime capability.
struct WebGpuBindlessAdapterCapacity {
    std::uint32_t contractVersion = 0u;
    bool limitsQueried = false;
    std::uint32_t bindGroups = 0u, bindingsPerGroup = 0u;
    rhi::BindlessStageLimits perStage;
};

struct WebGpuBindlessRequiredLimit {
    const char* name = nullptr;
    std::uint32_t value = 0u;
};

// Partial requestDevice dictionary: descriptor capacities only, no features,
// buffer bytes/alignment, texture extents/layers or live/in-flight byte quotas.
struct WebGpuBindlessDeviceLimitPlan {
    std::uint32_t contractVersion = 0u;
    rhi::BindlessPipelineInterfaceDemand demand;
    std::array<WebGpuBindlessRequiredLimit, 7u> requiredLimits{};
    std::uint32_t count = 0u;
};

struct WebGpuBindlessWorkloadAdapterCapacity {
    std::uint32_t contractVersion = 0u;
    WebGpuBindlessAdapterCapacity bindings;
    rhi::semantics::BindlessWorkloadResourceLimits resources;
};

struct WebGpuBindlessWorkloadRequiredLimit {
    const char* name = nullptr;
    std::uint64_t value = 0u;
};

// /2 is a complete binding/resource request dictionary only. Original resource
// closure, format operations and enforced in-flight quotas remain separate.
struct WebGpuBindlessWorkloadDeviceLimitPlan {
    std::uint32_t contractVersion = 0u;
    rhi::BindlessPipelineInterfaceDemand bindings;
    rhi::semantics::BindlessWorkloadResourceDemand resources;
    std::array<WebGpuBindlessWorkloadRequiredLimit, 15u> requiredLimits{};
    std::uint32_t count = 0u;
};

class WebGpuBindlessDeviceLimitPlanner final {
public:
    static const char* planWorkload(const rhi::BindlessShaderArtifact& artifact,
        const rhi::semantics::BindlessWorkloadResources& resources,
        const WebGpuBindlessWorkloadAdapterCapacity& adapter,
        WebGpuBindlessWorkloadDeviceLimitPlan& output) noexcept
    {
        output = {};
        if (adapter.contractVersion != 2u)
            return "RHI-WEBGPU-BINDLESS-WORKLOAD-DEVICE-LIMIT-PLAN-VERSION";
        WebGpuBindlessDeviceLimitPlan bindings;
        if (const auto* id = plan(artifact, adapter.bindings, bindings))
            return id;
        WebGpuBindlessWorkloadDeviceLimitPlan result;
        if (const auto* id = rhi::semantics::BindlessWorkloadResourceContract::inspect(
                resources, result.resources))
            return id;
        if (const auto* id = rhi::semantics::BindlessWorkloadResourceContract::validateLimits(
                result.resources, adapter.resources))
            return id;
        const auto append = [&](const char* name, std::uint64_t value) -> bool {
            // WebGPU requiredLimits uses JS Number. Never truncate a uint64.
            constexpr std::uint64_t maxExactInteger = (std::uint64_t{1} << 53u) - 1u;
            if (value > maxExactInteger)
                return false;
            if (value)
                result.requiredLimits[result.count++] = {name, value};
            return true;
        };
        for (std::uint32_t i = 0; i < bindings.count; ++i)
            if (!append(bindings.requiredLimits[i].name, bindings.requiredLimits[i].value))
                return "RHI-WEBGPU-BINDLESS-WORKLOAD-LIMIT-INTEGER";
        const auto& demand = result.resources;
        if (!append("maxBufferSize", demand.maxBufferBytes)
            || !append("maxUniformBufferBindingSize", demand.maxUniformBindingBytes)
            || !append("maxStorageBufferBindingSize", demand.maxStorageBindingBytes)
            || !append("maxTextureDimension2D", demand.texture2DDimension)
            || !append("maxTextureDimension3D", demand.texture3DDimension)
            || !append("maxTextureArrayLayers", demand.textureArrayLayers)
            || !append("minUniformBufferOffsetAlignment",
                demand.uniformOffsetDivisor ? adapter.resources.uniformOffsetAlignment : 0u)
            || !append("minStorageBufferOffsetAlignment",
                demand.storageOffsetDivisor ? adapter.resources.storageOffsetAlignment : 0u))
            return "RHI-WEBGPU-BINDLESS-WORKLOAD-LIMIT-INTEGER";
        result.contractVersion = 2u;
        result.bindings = bindings.demand;
        output = result;
        return nullptr;
    }

    static const char* plan(const rhi::BindlessShaderArtifact& artifact,
        const WebGpuBindlessAdapterCapacity& adapter,
        WebGpuBindlessDeviceLimitPlan& output) noexcept
    {
        output = {};
        if (adapter.contractVersion != 1u)
            return "RHI-WEBGPU-BINDLESS-DEVICE-LIMIT-PLAN-VERSION";
        if (!adapter.limitsQueried)
            return "RHI-WEBGPU-BINDLESS-ADAPTER-LIMITS-UNAVAILABLE";
        if (artifact.execution != rhi::BindlessExecution::Emulated)
            return "RHI-WEBGPU-BINDLESS-DEVICE-LIMIT-PLAN-EXECUTION";
        WebGpuBindlessDeviceLimitPlan result;
        if (const auto* id = rhi::inspectBindlessPipelineInterfaceDemand(artifact, result.demand))
            return id;
        if (result.demand.bindGroupSlots > adapter.bindGroups
            || result.demand.bindingsPerGroup > adapter.bindingsPerGroup)
            return "RHI-WEBGPU-BINDLESS-ADAPTER-LIMIT";
        const auto append = [&](const char* name, std::uint32_t value) {
            if (value)
                result.requiredLimits[result.count++] = {name, value};
        };
        append("maxBindGroups", result.demand.bindGroupSlots);
        append("maxBindingsPerBindGroup", result.demand.bindingsPerGroup);
        constexpr std::array<std::uint32_t rhi::BindlessStageLimits::*, 5u> members{
            &rhi::BindlessStageLimits::sampledTextures,
            &rhi::BindlessStageLimits::samplers,
            &rhi::BindlessStageLimits::storageTextures,
            &rhi::BindlessStageLimits::uniformBuffers,
            &rhi::BindlessStageLimits::storageBuffers};
        constexpr std::array<const char*, 5u> names{"maxSampledTexturesPerShaderStage",
            "maxSamplersPerShaderStage",
            "maxStorageTexturesPerShaderStage",
            "maxUniformBuffersPerShaderStage",
            "maxStorageBuffersPerShaderStage"};
        for (std::size_t i = 0u; i < members.size(); ++i) {
            std::uint32_t maximum = 0u;
            for (const auto& stage : result.demand.perStage)
                maximum = std::max(maximum, stage.*members[i]);
            if (maximum > adapter.perStage.*members[i])
                return "RHI-WEBGPU-BINDLESS-ADAPTER-LIMIT";
            append(names[i], maximum);
        }
        result.contractVersion = 1u;
        output = result;
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu
