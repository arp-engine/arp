#pragma once

#include "rhi/backends/webgpu/webgpu_descriptor_dynamic_offset_validation.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuWholeBufferBindingFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        const std::shared_ptr<rhi_webgpu::WebGpuNativeDescriptorGroupPayload>& group)
    {
        using namespace rhi_webgpu;
        assert(group && group->sharedOwnerMatches(group));
        const auto limits = webGpuNativeDeviceLimits();
        assert(limits.uniformBufferOffsetAlignment && limits.storageBufferOffsetAlignment
            && limits.maxUniformBufferBindingSize && limits.maxStorageBufferBindingSize);
        auto& binding = group->bindings.front();
        assert(binding.binding.type == rhi::DescriptorType::UniformBuffer
            && binding.binding.dynamicOffset && binding.buffer && binding.buffer->buffer);
        const auto bufferBytes = binding.buffer->sizeBytes;
        const auto bindingNumber = binding.binding.binding;
        using Guard = WebGpuDescriptorDynamicOffsetValidation;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Guard::validateBinding(*group, bindingNumber, 0u, limits));
        for (const auto offset : {1u, limits.uniformBufferOffsetAlignment, UINT32_MAX})
            assert(std::string_view(Guard::validateBinding(*group, bindingNumber, offset, limits))
                == "RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN");
        auto missing = limits;
        missing.uniformBufferOffsetAlignment = 0u;
        assert(std::string_view(Guard::validateBinding(*group, bindingNumber, 0u, missing))
            == "RHI-DESCRIPTOR-WHOLE-BUFFER-LIMITS-UNAVAILABLE");
        missing = limits;
        missing.maxUniformBufferBindingSize = 0u;
        assert(std::string_view(Guard::validateBinding(*group, bindingNumber, 0u, missing))
            == "RHI-DESCRIPTOR-WHOLE-BUFFER-LIMITS-UNAVAILABLE");
        assert(std::string_view(Guard::validateBinding(*group, UINT32_MAX, 0u, limits))
            == "RHI-WEBGPU-DESCRIPTOR-DYNAMIC-BINDING-MISSING");
        // Deliberately hostile metadata must reject; it never reaches the API.
        binding.buffer->sizeBytes = static_cast<std::uint64_t>(limits.maxUniformBufferBindingSize)
            + limits.uniformBufferOffsetAlignment;
        assert(std::string_view(Guard::validateBinding(
                   *group, bindingNumber, limits.uniformBufferOffsetAlignment, limits))
            == "RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN");
        assert(std::string_view(Guard::validateBinding(*group, bindingNumber, 0u, limits))
            == "RHI-DESCRIPTOR-WHOLE-BUFFER-BINDING-SIZE");
        binding.buffer->sizeBytes = bufferBytes;
        assert(!Guard::validateBinding(*group, bindingNumber, 0u, limits));
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_WHOLE_BUFFER_OK actual-native-buffer zero-valid nonzero-rejected hot0");
    }
#endif
};
} // namespace arp::test
