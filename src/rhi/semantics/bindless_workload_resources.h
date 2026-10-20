#pragma once
#include "rhi/frontend/descriptor_buffer_binding_range.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/semantics/bindless_workload_resource_types.h"

#include <algorithm>
#include <numeric>

namespace arp::rhi::semantics {
// Borrowed cold declarations, not original resource identities or ownership.
// Include non-descriptor buffers and all helper allocations. Repeated binding
// references do not multiply allocations; this inspection derives maxima only.
struct BindlessWorkloadBufferBinding {
    std::uint32_t bufferIndex = 0u;
    DescriptorType type = DescriptorType::UniformBuffer;
    DescriptorBufferBindingRange range;
};

struct BindlessWorkloadResources {
    std::uint32_t contractVersion = 0u;
    const BufferDesc* buffers = nullptr;
    std::size_t bufferCount = 0u;
    const BindlessWorkloadBufferBinding* bufferBindings = nullptr;
    std::size_t bufferBindingCount = 0u;
    const TextureDesc* textures = nullptr;
    std::size_t textureCount = 0u;
};

class BindlessWorkloadResourceContract final {
public:
    struct DevicePreflightDecision {
        bool available = false, globalAlignmentRequired = false;
        const char* diagnostic = "RHI-BINDLESS-WORKLOAD-DEVICE-BACKEND";
    };

    // Preflight is a prerequisite only, never execution authority.
    static constexpr DevicePreflightDecision devicePreflightLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {true, true, nullptr};
        case Backend::Metal:
            return {true, false, nullptr};
        case Backend::Null:
            return {false, false, "RHI-BINDLESS-WORKLOAD-DEVICE-VALIDATOR-ONLY"};
        case Backend::WebGL:
            return {};
        }
        return {};
    }

    static BindlessWorkloadResourceDemand merge(
        const BindlessWorkloadResourceDemand& a, const BindlessWorkloadResourceDemand& b) noexcept
    {
        return {std::max(a.maxBufferBytes, b.maxBufferBytes),
            std::max(a.maxUniformBindingBytes, b.maxUniformBindingBytes),
            std::max(a.maxStorageBindingBytes, b.maxStorageBindingBytes),
            std::gcd(a.uniformOffsetDivisor, b.uniformOffsetDivisor),
            std::gcd(a.storageOffsetDivisor, b.storageOffsetDivisor),
            std::max(a.texture2DDimension, b.texture2DDimension),
            std::max(a.texture3DDimension, b.texture3DDimension),
            std::max(a.textureArrayLayers, b.textureArrayLayers)};
    }

    struct Decision {
        bool planningAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-WORKLOAD-RESOURCES-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {};
        }
        return {};
    }

    static const char* inspect(
        const BindlessWorkloadResources& resources, BindlessWorkloadResourceDemand& output) noexcept
    {
        output = {};
        if (resources.contractVersion != 1u)
            return "RHI-BINDLESS-WORKLOAD-RESOURCES-VERSION";
        if ((resources.bufferCount && !resources.buffers)
            || (resources.bufferBindingCount && !resources.bufferBindings)
            || (resources.textureCount && !resources.textures))
            return "RHI-BINDLESS-WORKLOAD-RESOURCES-INPUT";
        BindlessWorkloadResourceDemand result;
        for (std::size_t i = 0; i < resources.bufferCount; ++i) {
            const auto& buffer = resources.buffers[i];
            if (!buffer.sizeBytes || buffer.usage == ResourceUsage::None)
                return "RHI-BINDLESS-WORKLOAD-BUFFER-SHAPE";
            result.maxBufferBytes = std::max(result.maxBufferBytes, buffer.sizeBytes);
        }
        for (std::size_t i = 0; i < resources.bufferBindingCount; ++i) {
            const auto& binding = resources.bufferBindings[i];
            if (binding.bufferIndex >= resources.bufferCount)
                return "RHI-BINDLESS-WORKLOAD-BUFFER-INDEX";
            const bool uniform = binding.type == DescriptorType::UniformBuffer;
            if (!uniform && binding.type != DescriptorType::ReadOnlyStorageBuffer
                && binding.type != DescriptorType::StorageBuffer)
                return "RHI-BINDLESS-WORKLOAD-BUFFER-TYPE";
            const auto& buffer = resources.buffers[binding.bufferIndex];
            const auto usage =
                uniform ? ResourceUsage::UniformBuffer : ResourceUsage::StorageBuffer;
            if ((buffer.usage & usage) == ResourceUsage::None)
                return "RHI-BINDLESS-WORKLOAD-BUFFER-USAGE";
            DescriptorBufferBindingRangeContract::Resolved range;
            if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                    binding.range, buffer.sizeBytes, 0u, range))
                return id;
            auto& maximum = uniform ? result.maxUniformBindingBytes : result.maxStorageBindingBytes;
            auto& divisor = uniform ? result.uniformOffsetDivisor : result.storageOffsetDivisor;
            maximum = std::max(maximum, range.sizeBytes);
            divisor = std::gcd(divisor, range.offsetBytes);
        }
        for (std::size_t i = 0; i < resources.textureCount; ++i) {
            const auto& texture = resources.textures[i];
            const bool volume = texture.extent.depth > 1u;
            if (!texture.sampleCount || texture.usage == TextureUsage::None
                || (volume
                    && (texture.arrayLayerCount != 1u || texture.cubeCompatible
                        || texture.sampleCount != 1u))
                || (texture.cubeCompatible
                    && (texture.extent.width != texture.extent.height
                        || texture.arrayLayerCount % 6u || texture.sampleCount != 1u))
                || (texture.sampleCount != 1u
                    && (texture.mipLevelCount != 1u || texture.arrayLayerCount != 1u)))
                return "RHI-BINDLESS-WORKLOAD-TEXTURE-SHAPE";
            std::uint64_t texelBytes = 0u;
            if (const auto* id = TextureUploadDestinationValidation::footprint(texture, texelBytes))
                return id;
            if (!core::canMultiplyUnsigned(texelBytes, std::uint64_t(texture.sampleCount)))
                return "RHI-TEXTURE-DESTINATION-BYTE-OVERFLOW";
            auto& dimension = volume ? result.texture3DDimension : result.texture2DDimension;
            dimension = std::max(dimension, std::max(texture.extent.width, texture.extent.height));
            if (volume)
                dimension = std::max(dimension, texture.extent.depth);
            else
                result.textureArrayLayers =
                    std::max(result.textureArrayLayers, texture.arrayLayerCount);
        }
        output = result;
        return nullptr;
    }

    static const char* validateSizeLimits(const BindlessWorkloadResourceDemand& demand,
        const BindlessWorkloadResourceLimits& limits) noexcept
    {
        if (limits.contractVersion != 1u)
            return "RHI-BINDLESS-WORKLOAD-LIMITS-VERSION";
        const auto unavailable = [](auto required, auto limit) {
            return required && !limit;
        };
        if (unavailable(demand.maxBufferBytes, limits.maxBufferBytes)
            || unavailable(demand.maxUniformBindingBytes, limits.maxUniformBindingBytes)
            || unavailable(demand.maxStorageBindingBytes, limits.maxStorageBindingBytes)
            || unavailable(demand.texture2DDimension, limits.texture2DDimension)
            || unavailable(demand.texture3DDimension, limits.texture3DDimension)
            || unavailable(demand.textureArrayLayers, limits.textureArrayLayers))
            return "RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE";
        if (demand.maxBufferBytes > limits.maxBufferBytes
            || demand.maxUniformBindingBytes > limits.maxUniformBindingBytes
            || demand.maxStorageBindingBytes > limits.maxStorageBindingBytes
            || demand.texture2DDimension > limits.texture2DDimension
            || demand.texture3DDimension > limits.texture3DDimension
            || demand.textureArrayLayers > limits.textureArrayLayers)
            return "RHI-BINDLESS-WORKLOAD-RESOURCE-LIMIT";
        return nullptr;
    }

    static const char* validateLimits(const BindlessWorkloadResourceDemand& demand,
        const BindlessWorkloadResourceLimits& limits) noexcept
    {
        if (limits.contractVersion != 1u)
            return "RHI-BINDLESS-WORKLOAD-LIMITS-VERSION";
        if ((demand.maxUniformBindingBytes && !limits.uniformOffsetAlignment)
            || (demand.maxStorageBindingBytes && !limits.storageOffsetAlignment))
            return "RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE";
        if (const auto* id = validateSizeLimits(demand, limits))
            return id;
        const auto badAlignment = [](std::uint64_t divisor, std::uint32_t alignment) {
            return (alignment && (alignment & (alignment - 1u)))
                || (divisor && (!alignment || divisor % alignment));
        };
        if (badAlignment(demand.uniformOffsetDivisor, limits.uniformOffsetAlignment)
            || badAlignment(demand.storageOffsetDivisor, limits.storageOffsetAlignment))
            return "RHI-BINDLESS-WORKLOAD-BUFFER-ALIGNMENT";
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
