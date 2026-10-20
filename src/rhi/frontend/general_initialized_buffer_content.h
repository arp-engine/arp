#pragma once

#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/descriptor_general_binding_validation.h"
#include "rhi/semantics/general_initialized_content_lowering.h"

namespace arp::rhi {
// Borrowed GeneralFiniteInitializedContent/1 association. Initialization still
// authenticates the complete backing payload through Staging/4; this contract
// adds no initializer, owner storage, command or execution authority.
class OriginalGeneralFiniteInitializedBufferContentContract final {
public:
    using Initialized = OriginalCommandInitializedBufferStagingContract;
    using Resolved = DescriptorBufferBindingRangeContract::Resolved;

    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxRetainedOwners = 0u;
        std::uint64_t maxInitializedBytes = 0u, maxRetainedNativeBytes = 0u;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, associationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-GENERAL-FINITE-CONTENT-BACKEND-UNKNOWN";
    };

    static constexpr const char* validateRequest(const Request& request) noexcept
    {
        if (request.contractVersion != 1u)
            return "RHI-GENERAL-FINITE-CONTENT-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-GENERAL-FINITE-CONTENT-OPT-IN";
        if (request.maxRetainedOwners < Initialized::retainedOwnerBound)
            return "RHI-GENERAL-FINITE-CONTENT-OWNER-CAPACITY";
        return request.maxInitializedBytes && request.maxRetainedNativeBytes
            ? nullptr
            : "RHI-GENERAL-FINITE-CONTENT-BYTE-CAPACITY";
    }

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::OriginalGeneralFiniteInitializedContentLowering::lowering<Decision>(
            backend);
    }

    // Pure L2 declaration checks. The backend must first authenticate the same
    // original Source/5 element, initialized lease, frozen bytes and completion.
    static const char* validateDeclaration(const Request& request,
        std::uint32_t sourceVersion,
        const Initialized::Budget& initialization,
        const DescriptorBindingDesc& binding,
        const OriginalCommandDescriptorSetElement& element,
        std::uint64_t backingBytes,
        std::uint64_t initializedBytes,
        std::uint64_t allocationBytes,
        Resolved& output) noexcept
    {
        output = {};
        if (const auto* id = validateRequest(request))
            return id;
        if (sourceVersion != 5u)
            return "RHI-GENERAL-FINITE-CONTENT-SOURCE-PROFILE";
        if (const auto* id = Initialized::validateShaderPayload(initialization, initializedBytes))
            return id;
        if (!backingBytes || backingBytes != initializedBytes || allocationBytes != backingBytes)
            return "RHI-GENERAL-FINITE-CONTENT-COMPLETE-BACKING";
        if (initializedBytes > request.maxInitializedBytes
            || allocationBytes > request.maxRetainedNativeBytes)
            return "RHI-GENERAL-FINITE-CONTENT-BYTE-CAPACITY";
        if (element.write.type != initialization.shaderContent.type)
            return "RHI-GENERAL-FINITE-CONTENT-TYPE-JOIN";
        if (const auto* id =
                OriginalGeneralDescriptorBindingContract::validateShape(binding, element, 2u))
            return id;
        using Footprint = OriginalCommandTypedSourceContract;
        const auto* footprint = element.write.bufferBindingRange.contractVersion == 2u
            ? Footprint::validateFullBoundView(element, binding, backingBytes, 1u)
            : Footprint::validateWholeBoundView(element, binding, backingBytes, 1u);
        if (footprint)
            return footprint;
        return DescriptorBufferBindingRangeContract::resolve(
            element.write.bufferBindingRange, initializedBytes, element.dynamicOffset, output);
    }
};
} // namespace arp::rhi
