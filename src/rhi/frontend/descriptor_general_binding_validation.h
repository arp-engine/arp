#pragma once

#include "rhi/frontend/command_usage.h"
#include "rhi/semantics/descriptor_lowering.h"

namespace arp::rhi {
// region original_general_descriptor_binding
// GeneralBinding1 (whole) and GeneralBinding2 (whole/finite) are independently
// opted-in borrowed-entry prerequisites.
// It never grants a descriptor receipt, recording, submission or completion.
class OriginalGeneralDescriptorBindingContract final {
public:
    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, preparationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-GENERAL-BINDING-BACKEND";
    };

    static constexpr const char* validateRequest(const Request& request) noexcept
    {
        if (request.contractVersion != 1u && request.contractVersion != 2u)
            return "RHI-ORIGINAL-GENERAL-BINDING-VERSION";
        return request.prototypeOptIn ? nullptr : "RHI-ORIGINAL-GENERAL-BINDING-OPT-IN";
    }

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::OriginalGeneralDescriptorBinding_lowering<Decision>(
            backend);
    }

    static const char* validateShape(const DescriptorBindingDesc& binding,
        const OriginalCommandDescriptorSetElement& element,
        std::uint32_t version = 1u) noexcept
    {
        const auto& write = element.write;
        if (version != 1u && version != 2u)
            return "RHI-ORIGINAL-GENERAL-BINDING-VERSION";
        const bool finite = version == 2u && write.bufferBindingRange.contractVersion == 2u;
        if (binding.arrayCount != 1u || binding.tableBacked || (!finite && binding.dynamicOffset)
            || binding.stages == ShaderStage::None || write.binding != binding.binding
            || write.type != binding.type || write.arrayElement || !write.resource.value
            || write.physicalLowering.enabled || (!binding.dynamicOffset && element.dynamicOffset)
            || (!finite
                && (element.dynamicOffset
                    || !write.bufferBindingRange.matches(DescriptorBufferBindingRange{}))))
            return "RHI-ORIGINAL-GENERAL-BINDING-SCALAR-PROFILE";
        const bool buffer = binding.type == DescriptorType::UniformBuffer
            || binding.type == DescriptorType::ReadOnlyStorageBuffer
            || binding.type == DescriptorType::StorageBuffer;
        if (finite)
            if (const auto* id = DescriptorBufferBindingRangeContract::validateSchema(
                    write.bufferBindingRange, buffer))
                return id;
        switch (binding.type) {
        case DescriptorType::UniformBuffer:
        case DescriptorType::ReadOnlyStorageBuffer:
        case DescriptorType::StorageBuffer:
            return write.resourceKind == DescriptorResourceKind::Buffer
                ? nullptr
                : "RHI-ORIGINAL-GENERAL-BINDING-RESOURCE-KIND";
        case DescriptorType::SampledTexture:
        case DescriptorType::StorageTexture:
            return write.resourceKind == DescriptorResourceKind::TextureView
                ? nullptr
                : "RHI-ORIGINAL-GENERAL-BINDING-RESOURCE-KIND";
        case DescriptorType::Sampler:
            return write.resourceKind == DescriptorResourceKind::Sampler
                ? nullptr
                : "RHI-ORIGINAL-GENERAL-BINDING-RESOURCE-KIND";
        default:
            return "RHI-ORIGINAL-GENERAL-BINDING-TYPE";
        }
    }

    static const char* validateFootprint(const DescriptorBindingDesc& binding,
        const OriginalCommandDescriptorSetElement& element,
        const OriginalCommandDescriptorDependencies::Owner& dependency,
        std::uint32_t version = 1u) noexcept
    {
        if (const auto* id = validateShape(binding, element, version))
            return id;
        return std::visit(
            [&](const auto& source) -> const char* {
                using T =
                    std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                if (const auto* id = T::validateOriginal(source))
                    return id;
                using Footprint = OriginalCommandTypedSourceContract;
                if constexpr (std::is_same_v<T, OriginalCommandBufferLease>)
                    return element.write.bufferBindingRange.contractVersion == 2u
                        ? Footprint::validateFullBoundView(
                              element, binding, source->layout().sizeBytes, 1u)
                        : Footprint::validateWholeBoundView(
                              element, binding, source->layout().sizeBytes, 1u);
                else if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>)
                    return Footprint::validateGeneralTextureFootprint(
                        element, binding, source->originalViewLease()->layout());
                else
                    return Footprint::validateGeneralSamplerFootprint(
                        element, binding, source->layout());
            },
            dependency);
    }
};

// endregion original_general_descriptor_binding
} // namespace arp::rhi
