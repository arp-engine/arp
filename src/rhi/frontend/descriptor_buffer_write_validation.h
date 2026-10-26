#pragma once
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_whole_buffer_binding.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/semantics/descriptor_lowering.h"

#include <array>

// region original_descriptor_buffer_binding
namespace arp::rhi {
// Independently opted-in range prerequisite. It does not join Descriptor
// dependencies or issue native command/state authority.
class OriginalDescriptorBufferBindingContract final {
public:
    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    using Limits = semantics::DescriptorWholeBufferBinding::Limits;

    struct IntervalDiagnosticRequest {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct IntervalDiagnosticObservation {
        DescriptorBufferBindingRangeContract::Resolved range;
        std::uint64_t runtimeLength = 0u, minimumBindingBytes = 0u, runtimeStrideBytes = 0u;
        std::uint32_t group = 0u, binding = 0u, stageMask = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    struct FiniteAccessDiagnosticRequest {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct FiniteAccessDiagnosticObservation {
        DescriptorBufferBindingRangeContract::Resolved binding, physicalAccess;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    // Maps explicitly authored relative access; does not infer a shader
    // footprint, original content, limits, Source or command authorization.
    static constexpr const char* diagnoseFiniteAccess(const FiniteAccessDiagnosticRequest& request,
        const DescriptorBufferBindingRange& range,
        std::uint64_t bufferBytes,
        std::uint32_t dynamicOffset,
        std::uint64_t shaderOffset,
        std::uint64_t shaderBytes,
        std::uint64_t coverageOffset,
        std::uint64_t coverageBytes,
        FiniteAccessDiagnosticObservation& output) noexcept
    {
        output = {};
        if (request.contractVersion != 1u)
            return "RHI-ORIGINAL-FINITE-ACCESS-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-FINITE-ACCESS-OPT-IN";
        if (const auto* id = DescriptorBufferBindingRangeContract::validateSchema(range, true))
            return id;
        if (range.contractVersion != 2u)
            return "RHI-ORIGINAL-FINITE-ACCESS-SCHEMA";
        FiniteAccessDiagnosticObservation observed;
        if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                range, bufferBytes, dynamicOffset, observed.binding))
            return id;
        if (!core::containsUnsignedRange(
                std::uint64_t{0u}, observed.binding.sizeBytes, shaderOffset, shaderBytes))
            return "RHI-ORIGINAL-FINITE-ACCESS-SHADER-RANGE";
        // Binding containment and shader-relative containment prove this sum
        // cannot overflow. Dynamic displacement is applied exactly once.
        observed.physicalAccess = {observed.binding.offsetBytes + shaderOffset, shaderBytes};
        if (!core::containsUnsignedRange(
                std::uint64_t{0u}, bufferBytes, coverageOffset, coverageBytes)
            || !core::containsUnsignedRange(coverageOffset,
                coverageBytes,
                observed.physicalAccess.offsetBytes,
                observed.physicalAccess.sizeBytes))
            return "RHI-ORIGINAL-FINITE-ACCESS-COVERAGE";
        output = observed;
        return nullptr;
    }

    static constexpr const char* validateIntervalDiagnosticRequest(
        const IntervalDiagnosticRequest& request) noexcept
    {
        if (request.contractVersion != 1u)
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-VERSION";
        return request.prototypeOptIn ? nullptr : "RHI-ORIGINAL-RUNTIME-INTERVAL-OPT-IN";
    }

    static constexpr const char* validateRequest(const Request& request) noexcept
    {
        if (request.contractVersion != 1u)
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-VERSION";
        return request.prototypeOptIn ? nullptr : "RHI-ORIGINAL-DESCRIPTOR-BUFFER-OPT-IN";
    }

    static constexpr const char* validateLimits(const Limits& limits) noexcept
    {
        return semantics::DescriptorWholeBufferBinding::validLimits(limits)
            ? nullptr
            : "RHI-ORIGINAL-DESCRIPTOR-BUFFER-LIMITS-UNAVAILABLE";
    }

    static constexpr const char* validateBinding(DescriptorType type,
        std::uint64_t bytes,
        std::uint32_t offset,
        const Limits& limits) noexcept
    {
        if (const auto* id = validateLimits(limits))
            return id;
        using Policy = semantics::DescriptorWholeBufferBinding;
        const auto failure = Policy::validate(type, bytes, offset, limits);
        return failure == Policy::Failure::DynamicRangeUnproven
            ? "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-RANGE-UNPROVEN"
            : Policy::diagnostic(failure);
    }

    // Scalar finite-interval prerequisite only. Caller facts cannot issue
    // original lineage, a descriptor write or native execution authority.
    static constexpr const char* validateFiniteInterval(DescriptorType type,
        const DescriptorBufferBindingRange& range,
        std::uint64_t bufferBytes,
        std::uint32_t dynamicOffset,
        std::uint64_t targetMinimumBytes,
        const Limits& limits,
        DescriptorBufferBindingRangeContract::Resolved& output) noexcept
    {
        output = {};
        if (const auto* id = DescriptorBufferBindingRangeContract::validateSchema(range, true))
            return id;
        if (range.contractVersion != 2u)
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-SCHEMA";
        if (const auto* id = validateLimits(limits))
            return id;
        const bool uniform = type == DescriptorType::UniformBuffer;
        if (!uniform && type != DescriptorType::StorageBuffer
            && type != DescriptorType::ReadOnlyStorageBuffer)
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-TYPE";
        if (!semantics::DescriptorWholeBufferBinding::kindAvailable(type, limits))
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-KIND-ABSENT";
        const auto alignment =
            uniform ? limits.uniformOffsetAlignment : limits.storageOffsetAlignment;
        const auto maximum =
            uniform ? limits.maxUniformBindingBytes : limits.maxStorageBindingBytes;
        if (!targetMinimumBytes)
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-MINIMUM-UNAVAILABLE";
        // An aligned sum never repairs separately unaligned authored arguments.
        if (range.offsetBytes % alignment || dynamicOffset % alignment)
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-ALIGNMENT";
        if (range.sizeBytes < targetMinimumBytes || range.sizeBytes > maximum)
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-SIZE";
        return DescriptorBufferBindingRangeContract::resolve(
            range, bufferBytes, dynamicOffset, output);
    }

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, captureAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-DESCRIPTOR-BUFFER-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::OriginalDescriptorBufferBinding_lowering<Decision>(
            backend);
    }

    static constexpr Decision intervalDiagnosticLowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::
            OriginalDescriptorBufferBinding_intervalDiagnosticLowering<Decision>(backend);
    }
};

// endregion original_descriptor_buffer_binding

// region original_finite_descriptor_write

struct OriginalFiniteDescriptorWriteRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBindings = 0u, maxRetainedOwnerSlots = 0u, maxCallbacks = 0u;
    std::array<std::uint64_t, 3u> maxCpuBytes{};
};

struct OriginalFiniteDescriptorBufferWrite {
    DescriptorBindingWrite write;
    std::shared_ptr<const OriginalCommandBufferLease> buffer;
    std::uint32_t dynamicOffset = 0u;
};

class OriginalFiniteDescriptorWriteContract final {
public:
    static constexpr const char* validateSourceDiagnostic(
        const OriginalFiniteDescriptorSourceDiagnosticRequest& r) noexcept
    {
        return r.diagnostic();
    }

    static constexpr std::uint32_t callbackCount = 3u, quotaCount = 3u;
    // Private state/group ownership, two typed pipeline slots, original arena, three quota
    // slots, and each native callback's group-retention slot. Each binding
    // retains one original lease and one native buffer payload.
    static constexpr std::uint32_t fixedOwnerSlots = 2u + 2u + 1u + quotaCount + callbackCount;

    static constexpr std::uint64_t ownerSlots(std::uint32_t bindings) noexcept
    {
        return std::uint64_t(fixedOwnerSlots) + std::uint64_t(bindings) * 2u;
    }

    static constexpr const char* validate(const OriginalFiniteDescriptorWriteRequest& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-ORIGINAL-FINITE-WRITE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-FINITE-WRITE-OPT-IN";
        return validateBudget(
            r, "RHI-ORIGINAL-FINITE-WRITE-BUDGET", "RHI-ORIGINAL-FINITE-WRITE-CPU-BUDGET");
    }

    template <class Request>
    static constexpr const char* validateBudget(
        const Request& r, const char* budget, const char* cpu) noexcept
    {
        if (!r.maxBindings || ownerSlots(r.maxBindings) > r.maxRetainedOwnerSlots
            || r.maxCallbacks != callbackCount)
            return budget;
        for (const auto bytes : r.maxCpuBytes)
            if (!bytes)
                return cpu;
        return nullptr;
    }

    struct Facts {
        bool device = false, group = false, layoutQuery = false, pushScope = false,
             popScope = false;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, preparationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-FINITE-WRITE-BACKEND";
    };

    static constexpr Decision decide(Backend backend, Facts f) noexcept
    {
        return semantics::DescriptorLowering::OriginalFiniteDescriptorWrite_decide<Decision>(
            backend, f);
    }

    static constexpr Decision decide(Backend backend) noexcept { return decide(backend, Facts{}); }

    // Metadata diagnostic only. Actual committed state is revalidated by the
    // existing descriptor payload; no caller fact grants Source/native custody.
    static constexpr Decision sourceDiagnosticDecision(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::
            OriginalFiniteDescriptorWrite_sourceDiagnosticDecision<Decision>(backend);
    }
};

// endregion original_finite_descriptor_write

// region original_whole_descriptor_write

// WholeWrite1 is an independently admitted original creation profile. Neither
// ordinary write receipts nor FiniteWrite1 interval observations admit it.
struct OriginalWholeDescriptorWriteRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBindings = 0u, maxRetainedOwnerSlots = 0u, maxCallbacks = 0u;
    std::array<std::uint64_t, 3u> maxCpuBytes{};
};

struct OriginalWholeDescriptorBufferWrite {
    DescriptorBindingWrite write;
    std::shared_ptr<const OriginalCommandBufferLease> buffer;
    std::uint32_t dynamicOffset = 0u;
};

class OriginalWholeDescriptorWriteContract final {
public:
    // Same candidate custody and callback participants, independently budgeted.
    static constexpr auto callbackCount = OriginalFiniteDescriptorWriteContract::callbackCount;
    static constexpr auto quotaCount = OriginalFiniteDescriptorWriteContract::quotaCount;
    static constexpr auto fixedOwnerSlots = OriginalFiniteDescriptorWriteContract::fixedOwnerSlots;

    static constexpr std::uint64_t ownerSlots(std::uint32_t bindings) noexcept
    {
        return OriginalFiniteDescriptorWriteContract::ownerSlots(bindings);
    }

    static constexpr const char* validate(const OriginalWholeDescriptorWriteRequest& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-ORIGINAL-WHOLE-WRITE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-WHOLE-WRITE-OPT-IN";
        if (!r.maxBindings || ownerSlots(r.maxBindings) > r.maxRetainedOwnerSlots
            || r.maxCallbacks != callbackCount)
            return "RHI-ORIGINAL-WHOLE-WRITE-BUDGET";
        for (const auto bytes : r.maxCpuBytes)
            if (!bytes)
                return "RHI-ORIGINAL-WHOLE-WRITE-CPU-BUDGET";
        return nullptr;
    }

    static const char* validateBinding(const DescriptorBindingDesc& binding,
        const DescriptorBindingWrite& write,
        std::uint64_t actualBufferBytes,
        std::uint32_t dynamicOffset,
        DescriptorBufferBindingRangeContract::Resolved& output) noexcept
    {
        output = {};
        if (binding.arrayCount != 1u || binding.tableBacked || binding.stages == ShaderStage::None
            || (binding.type != DescriptorType::StorageBuffer
                && binding.type != DescriptorType::ReadOnlyStorageBuffer)
            || write.binding != binding.binding || write.type != binding.type || write.arrayElement
            || write.resourceKind != DescriptorResourceKind::Buffer || !write.resource.value
            || write.physicalLowering.enabled)
            return "RHI-ORIGINAL-WHOLE-WRITE-BINDING";
        if (const auto* id = DescriptorBufferBindingRangeContract::validateSchema(
                write.bufferBindingRange, true))
            return id;
        if (write.bufferBindingRange.contractVersion != 1u)
            return "RHI-ORIGINAL-WHOLE-WRITE-RANGE-PROFILE";
        if (dynamicOffset)
            return "RHI-ORIGINAL-WHOLE-WRITE-DYNAMIC-PROFILE";
        return DescriptorBufferBindingRangeContract::resolve(
            write.bufferBindingRange, actualBufferBytes, 0u, output);
    }

    using Facts = OriginalFiniteDescriptorWriteContract::Facts;
    using Decision = OriginalFiniteDescriptorWriteContract::Decision;

    static constexpr Decision decide(Backend backend, Facts f = {}) noexcept
    {
        return semantics::DescriptorLowering::OriginalWholeDescriptorWrite_decide<Decision>(
            backend, f);
    }
};

// endregion original_whole_descriptor_write

// region original_finite_target_memory_write

// Independent opt-in declaration profile; never a runtime-length fallback.
struct OriginalFiniteTargetMemoryWriteRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBindings = 0u, maxRetainedOwnerSlots = 0u, maxCallbacks = 0u;
    std::array<std::uint64_t, 3u> maxCpuBytes{};
};

class OriginalFiniteTargetMemoryWriteContract final {
public:
    using Shared = OriginalFiniteDescriptorWriteContract;
    using Facts = Shared::Facts;
    using Decision = Shared::Decision;
    static constexpr auto callbackCount = Shared::callbackCount;
    static constexpr auto quotaCount = Shared::quotaCount;

    static constexpr std::uint64_t ownerSlots(std::uint32_t n) noexcept
    {
        return Shared::ownerSlots(n);
    }

    static constexpr const char* validate(const OriginalFiniteTargetMemoryWriteRequest& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-ORIGINAL-TARGET-FINITE-WRITE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-TARGET-FINITE-WRITE-OPT-IN";
        return Shared::validateBudget(r,
            "RHI-ORIGINAL-TARGET-FINITE-WRITE-BUDGET",
            "RHI-ORIGINAL-TARGET-FINITE-WRITE-CPU-BUDGET");
    }

    static constexpr Decision decide(Backend backend, Facts f) noexcept
    {
        return semantics::DescriptorLowering::OriginalFiniteTargetMemoryWrite_decide<Decision>(
            backend, f);
    }

    static constexpr Decision decide(Backend backend) noexcept { return decide(backend, Facts{}); }

    static constexpr Decision sourceDiagnosticDecision(Backend backend) noexcept
    {
        // Versioned diagnostic availability only; every original instance joins
        // its committed independent profile and three native scope participants.
        return decide(backend, Facts{true, true, true, true, true});
    }
};

static_assert(
    sizeof(OriginalFiniteTargetMemoryWriteRequest) == sizeof(OriginalFiniteDescriptorWriteRequest));
static_assert(alignof(OriginalFiniteTargetMemoryWriteRequest)
    == alignof(OriginalFiniteDescriptorWriteRequest));
} // namespace arp::rhi

// endregion original_finite_target_memory_write
