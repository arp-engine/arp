#pragma once
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/indirect_sequence_range.h"

namespace arp::rhi {
// Address/access projection over the original inventory. The GPU argument
// values remain unobserved; this cannot authorize a producer or a native draw.
class OriginalCommandIndirectAccesses final {
public:
    struct Point {
        std::size_t commandIndex = SIZE_MAX, sourceIndex = SIZE_MAX;
        GpuResourceId resource;
        std::uint64_t registrationGeneration = 0u, offsetBytes = 0u, sizeBytes = 0u;
        std::uint32_t records = 0u, strideBytes = 0u;
        semantics::ResourceAccessScope scope{ResourceState::ShaderRead,
            PipelineStage::DrawIndirect,
            ResourceAccess::IndirectCommandRead};
    };

    struct Result {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        std::uint64_t calls = 0u, records = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    template <class Visitor>
    static Result visit(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalBindlessRootStateBudget& budget,
        Visitor&& visitor) noexcept
    {
        if (const auto* id = budget.diagnostic())
            return {id};
        if (budget.contractVersion != 6u)
            return {"RHI-ORIGINAL-INDIRECT-ACCESS-ROOT-PROFILE"};
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return {id};
        if (inputs->originalBudget().contractVersion != 8u)
            return {"RHI-ORIGINAL-INDIRECT-ACCESS-INPUTS-PROFILE"};
        const auto checked = walk(*inputs, budget, [](const Point&) noexcept -> const char* {
            return nullptr;
        });
        return checked.diagnostic ? checked : walk(*inputs, budget, visitor);
    }

    // Pure schema/range helper. Does not read or accept CPU substitutes for
    // GPU-written words. Output remains empty if any address is invalid.
    static const char* inspectRange(const BufferDesc& buffer,
        const CommandScalarArguments::DrawIndirect& draw,
        std::uint64_t& extent) noexcept
    {
        extent = 0u;
        const IndexedIndirectSequenceRange range(buffer, draw.offset, draw.count, draw.stride);
        std::uint64_t last = 0u;
        if (!range.valid() || !range.offset(draw.count - 1u, last))
            return "RHI-ORIGINAL-INDIRECT-ACCESS-RANGE";
        // The checked range proves last+recordBytes <= the real allocation.
        extent = last - draw.offset + sizeof(DrawIndexedIndirectCommand);
        return nullptr;
    }

private:
    template <class Visitor>
    static Result walk(const OriginalCommandInputs& inputs,
        const OriginalBindlessRootStateBudget& budget,
        Visitor&& visitor) noexcept
    {
        Result result;
        const auto fail = [&](const char* id) noexcept {
            auto rejected = result;
            rejected.diagnostic = id;
            return rejected;
        };
        const auto& usage = *inputs.originalScalars()->originalUsage();
        const auto& inventory = usage.originalSources()->sources();
        const auto& arguments = inputs.originalScalars()->originalArguments()->records();
        for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
            result.commandIndex = command;
            const auto& op = usage.commands()[command];
            if (op.kind == CommandUsageKind::DispatchIndirect
                || op.kind == CommandUsageKind::DrawIndexedIndirectCount
                || op.kind == CommandUsageKind::IndirectProcessorDraw)
                return fail("RHI-ORIGINAL-INDIRECT-ACCESS-CONSUMER");
            if (op.kind != CommandUsageKind::DrawIndexedIndirect)
                continue;
            if (result.calls == budget.maxIndirectCalls)
                return fail("RHI-ORIGINAL-INDIRECT-ACCESS-CALL-CAPACITY");
            if (op.referenceCount != 1u)
                return fail("RHI-ORIGINAL-INDIRECT-ACCESS-OPERAND");
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const auto* lease =
                std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&inventory[source]);
            if (!lease)
                return fail("RHI-ORIGINAL-INDIRECT-ACCESS-BUFFER");
            const auto* fields =
                arguments[command].scalar.template get<CommandScalarArguments::DrawIndirect>();
            if (!fields)
                return fail("RHI-ORIGINAL-INDIRECT-ACCESS-SCALAR");
            Point point;
            if (const auto* id =
                    inspectRange((*lease)->originalObject()->desc(), *fields, point.sizeBytes))
                return fail(id);
            if (fields->count > budget.maxIndirectRecords - result.records)
                return fail("RHI-ORIGINAL-INDIRECT-ACCESS-RECORD-CAPACITY");
            point.commandIndex = command;
            point.sourceIndex = source;
            point.resource = (*lease)->resource();
            const auto registration = (*lease)->originalObject()->originalResourceRegistration();
            if (const auto* id = registration.diagnostic())
                return fail(id);
            point.registrationGeneration = registration.generation();
            point.offsetBytes = fields->offset;
            point.strideBytes = fields->stride;
            point.records = fields->count;
            ++result.calls;
            result.records += fields->count;
            if (const auto* id = visitor(point))
                return fail(id);
        }
        result.commandIndex = usage.commands().size();
        return result;
    }
};
} // namespace arp::rhi
