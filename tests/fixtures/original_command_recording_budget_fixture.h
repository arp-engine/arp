#pragma once

#include "rhi/frontend/command_stream_program.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
// Exercises the same original fixture graph. Creates no resource/source actor.
class OriginalCommandRecordingBudgetFixture final {
    using Program = rhi::OriginalCommandStreamProgram;
    using Budget = rhi::CommandRecordingBudget;
    using Usage = rhi::CommandRecordingUsage;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "recording admission expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    template <class Factory>
    static std::shared_ptr<const Program> verify(const std::shared_ptr<const Program>& original,
        std::uint64_t expectedDescriptorElements,
        Factory&& make)
    {
        const auto& fields = *original->originalInputs()->originalScalars()->originalArguments();
        Budget budget{1u, true, original->counts().commands};
        budget.limits.resourceReferences = original->counts().references;
        budget.limits.descriptorElements = expectedDescriptorElements;
        budget.limits.textBytes = fields.storedTextBytes();
        for (const auto& field : fields.records()) {
            assert(budget.limits.append(field.scalar.kind()));
            budget.limits.dynamicOffsets += field.offsetCount;
            budget.limits.pushBytes += field.byteCount;
        }
        const auto quota = original->originalMetadataQuota();
        const char* id = nullptr;
        const auto rejected = [&](const Budget& b, const char* expected) {
            const auto live = quota->liveBytes();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!make(b, id));
            is(id, expected);
            assert(quota->liveBytes() == live
                && core::defaultCpuAllocator().stats().allocationCount == allocations
                && !original->validate());
        };
        auto bad = budget;
        bad.contractVersion = 0u;
        rejected(bad, "RHI-COMMAND-RECORDING-BUDGET-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        rejected(bad, "RHI-COMMAND-RECORDING-BUDGET-OPT-IN");
        bad = budget;
        --bad.maxCommands;
        rejected(bad,
            bad.maxCommands ? "RHI-COMMAND-RECORDING-BUDGET-COMMAND-CAPACITY"
                            : "RHI-COMMAND-RECORDING-BUDGET-COMMANDS");
        for (std::size_t kind = 0u; kind < budget.limits.commands.size(); ++kind)
            if (budget.limits.commands[kind]) {
                bad = budget;
                --bad.limits.commands[kind];
                rejected(bad, "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY");
            }

        struct Category {
            std::uint64_t Usage::* member;
            const char* diagnostic;
        };

        for (const auto category :
            std::array<Category, 5u>{{{&Usage::resourceReferences,
                                          "RHI-COMMAND-RECORDING-BUDGET-REFERENCE-CAPACITY"},
                {&Usage::descriptorElements, "RHI-COMMAND-RECORDING-BUDGET-DESCRIPTOR-CAPACITY"},
                {&Usage::textBytes, "RHI-COMMAND-RECORDING-BUDGET-TEXT-CAPACITY"},
                {&Usage::dynamicOffsets, "RHI-COMMAND-RECORDING-BUDGET-OFFSET-CAPACITY"},
                {&Usage::pushBytes, "RHI-COMMAND-RECORDING-BUDGET-PUSH-CAPACITY"}}})
            if (budget.limits.*category.member) {
                bad = budget;
                --(bad.limits.*category.member);
                rejected(bad, category.diagnostic);
            }
        {
            const auto live = quota->liveBytes();
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - live);
            assert(pressure.charged() && !make(budget, id));
            is(id, "RHI-ORIGINAL-STREAM-ALLOCATION");
        }
        auto bounded = make(budget, id);
        assert(bounded && !id && bounded->recordingBudget()
            && bounded->recordingBudget()->limits.matches(budget.limits)
            && bounded->counts().matches(original->counts())
            && bounded->retainedOwnerCount() == original->retainedOwnerCount()
            && !bounded->compareOriginalInputs(original->originalInputs()));
        budget.limits = {};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!bounded->validate()
            && bounded->recordingBudget()->limits.commands != budget.limits.commands);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(bounded->executionDiagnostic(backend) && !bounded->nativeExecutionProven());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf("ORIGINAL_RECORDING_BUDGET_OK immutable-input exact-short repetition "
                    "preallocation-rejection quota-exhaustion allocation0 gates4-closed "
                    "descriptor-elements%llu\n",
            static_cast<unsigned long long>(expectedDescriptorElements));
        return bounded;
    }
};
} // namespace arp::test
