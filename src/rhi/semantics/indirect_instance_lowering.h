#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/frontend/command_indirect_commands.h"

namespace arp::rhi::semantics {

enum class IndirectInstanceDiagnostic {
    None,
    CapacityMissing,
    RangeInvalid,
    MappingInvalid,
    InstanceInvalid
};

struct IndirectInstanceEncoding {
    DrawIndexedIndirectCommand physical{};
    IndirectInstanceBase mapping;
    IndirectInstanceDiagnostic diagnostic = IndirectInstanceDiagnostic::None;

    constexpr bool valid() const noexcept { return diagnostic == IndirectInstanceDiagnostic::None; }
};

struct IndirectInstanceResolution {
    std::uint32_t logicalInstance = 0;
    IndirectInstanceDiagnostic diagnostic = IndirectInstanceDiagnostic::None;

    constexpr bool valid() const noexcept { return diagnostic == IndirectInstanceDiagnostic::None; }
};

// Value lowering only. It neither uploads arguments nor grants execution.
class IndirectInstanceBaseLowering final {
public:
    static constexpr IndirectInstanceEncoding encode(
        DrawIndexedIndirectCommand logical, std::uint32_t capacity) noexcept
    {
        IndirectInstanceEncoding result;
        result.physical = logical;
        result.physical.firstInstance = 0;
        if (!validRange(logical.firstInstance, logical.instanceCount, capacity)) {
            result.physical.instanceCount = 0;
            result.diagnostic = capacity ? IndirectInstanceDiagnostic::RangeInvalid
                                         : IndirectInstanceDiagnostic::CapacityMissing;
            return result;
        }
        result.mapping = {logical.firstInstance, logical.instanceCount, capacity, 1u};
        return result;
    }

    static constexpr IndirectInstanceResolution resolve(
        IndirectInstanceBase mapping, std::uint32_t physicalInstance) noexcept
    {
        if (mapping.contractVersion != 1u
            || !validRange(mapping.firstInstance, mapping.instanceCount, mapping.instanceCapacity))
            return {0u, IndirectInstanceDiagnostic::MappingInvalid};
        if (physicalInstance >= mapping.instanceCount)
            return {0u, IndirectInstanceDiagnostic::InstanceInvalid};
        return {mapping.firstInstance + physicalInstance, IndirectInstanceDiagnostic::None};
    }

    static constexpr IndirectInstanceResolution resolve(IndirectInstanceBase mapping,
        std::uint32_t physicalInstance,
        std::uint32_t expectedCapacity) noexcept
    {
        if (!expectedCapacity || mapping.instanceCapacity != expectedCapacity)
            return {0u, IndirectInstanceDiagnostic::MappingInvalid};
        return resolve(mapping, physicalInstance);
    }

    static constexpr const char* diagnosticId(IndirectInstanceDiagnostic diagnostic) noexcept
    {
        switch (diagnostic) {
        case IndirectInstanceDiagnostic::None:
            return "RHI-INDIRECT-INSTANCE-BASE-VALID";
        case IndirectInstanceDiagnostic::CapacityMissing:
            return "RHI-INDIRECT-INSTANCE-CAPACITY-MISSING";
        case IndirectInstanceDiagnostic::RangeInvalid:
            return "RHI-INDIRECT-INSTANCE-RANGE-INVALID";
        case IndirectInstanceDiagnostic::MappingInvalid:
            return "RHI-INDIRECT-INSTANCE-MAPPING-INVALID";
        case IndirectInstanceDiagnostic::InstanceInvalid:
            return "RHI-INDIRECT-INSTANCE-ID-INVALID";
        }
        return "RHI-INDIRECT-INSTANCE-DIAGNOSTIC-INVALID";
    }

private:
    static constexpr bool validRange(
        std::uint32_t first, std::uint32_t count, std::uint32_t capacity) noexcept
    {
        return capacity
            && (count ? core::containsUnsignedRange(0u, capacity, first, count)
                      : first <= capacity);
    }
};

} // namespace arp::rhi::semantics
