#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_model.h"

#include <array>
#include <optional>

namespace arp::rhi {

// Counts declared L2 operations and input cardinality, not native driver bytes,
// CPU instruction counts, immutable argument storage or execution authority.
// Repeated use of the same owner is charged on every operation.
struct CommandRecordingUsage {
    std::array<std::uint64_t, static_cast<std::size_t>(CommandUsageKind::Count)> commands{};
    std::uint64_t resourceReferences = 0u, descriptorElements = 0u;
    std::uint64_t textBytes = 0u, dynamicOffsets = 0u, pushBytes = 0u;

    bool append(CommandUsageKind kind, std::uint64_t count = 1u) noexcept
    {
        const auto index = static_cast<std::size_t>(kind);
        if (index >= commands.size() || !core::canAddUnsigned(commands[index], count))
            return false;
        commands[index] += count;
        return true;
    }

    // Project the original validated variable input; do not infer byte counts
    // from labels, deduplicated owners or a separate command representation.
    // Reference/descriptor cardinalities still come from typed binding checks.
    const char* appendArguments(const CommandVariableArguments::Input& input) noexcept
    {
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        auto projected = *this;
        if (!projected.append(input.scalar.kind()))
            return "RHI-COMMAND-RECORDING-BUDGET-OPCODE-OVERFLOW";
        if (!core::canAddUnsigned(textBytes, std::uint64_t{input.text.size()})
            || !core::canAddUnsigned(dynamicOffsets, std::uint64_t{input.offsetCount})
            || !core::canAddUnsigned(pushBytes, std::uint64_t{input.byteCount}))
            return "RHI-COMMAND-RECORDING-BUDGET-ARGUMENT-OVERFLOW";
        projected.textBytes += input.text.size();
        projected.dynamicOffsets += input.offsetCount;
        projected.pushBytes += input.byteCount;
        *this = projected;
        return nullptr;
    }

    bool matches(const CommandRecordingUsage& other) const noexcept
    {
        return commands == other.commands && resourceReferences == other.resourceReferences
            && descriptorElements == other.descriptorElements && textBytes == other.textBytes
            && dynamicOffsets == other.dynamicOffsets && pushBytes == other.pushBytes;
    }
};

struct CommandRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCommands = 0u;
    // Zero disables that kind/category; limits never derive from owner counts.
    CommandRecordingUsage limits;
};

class CommandRecordingBudgetContract final {
public:
    static const char* validateIntent(const CommandRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-COMMAND-RECORDING-BUDGET-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-COMMAND-RECORDING-BUDGET-OPT-IN";
        if (!budget.maxCommands)
            return "RHI-COMMAND-RECORDING-BUDGET-COMMANDS";
        return nullptr;
    }

    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool privateRecordingAvailable = false;
        const char* diagnostic = "RHI-COMMAND-RECORDING-BUDGET-BACKEND";
    };

    // This separate opt-in is only the bounded private recorder's prerequisite.
    // It never publishes ordinary CommandRetention capabilities.
    static Decision lowering(Backend backend, const CommandRecordingBudget& budget) noexcept
    {
        if (const auto* id = validateIntent(budget))
            return {Disposition::DiagnosticFailure, false, id};
        switch (backend) {
        case Backend::Null:
            return {
                Disposition::ValidatorOnly, false, "RHI-COMMAND-RECORDING-BUDGET-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {Disposition::Prototype, true, nullptr};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-COMMAND-RECORDING-BUDGET-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-COMMAND-RECORDING-BUDGET-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure,
                false,
                "RHI-COMMAND-RECORDING-BUDGET-WEBGL-UNSUPPORTED"};
        }
        return {};
    }
};

// One embedded value ledger on the serialized recording owner. Preflight and
// rejection allocate nothing and change no counter. No second owner/state graph.
class CommandRecordingMeter final {
public:
    static std::optional<CommandRecordingMeter> prepare(
        const CommandRecordingBudget& budget, const char*& diagnostic) noexcept
    {
        diagnostic = CommandRecordingBudgetContract::validateIntent(budget);
        return diagnostic ? std::nullopt
                          : std::optional<CommandRecordingMeter>(CommandRecordingMeter(budget));
    }

    const char* preflight(const CommandRecordingUsage& increment) const noexcept
    {
        if (sealed_)
            return "RHI-COMMAND-RECORDING-BUDGET-CLOSED";
        auto total = commandCount_;
        for (std::size_t i = 0u; i < increment.commands.size(); ++i) {
            if (increment.commands[i] > budget_.limits.commands[i] - used_.commands[i])
                return "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY";
            if (increment.commands[i] > budget_.maxCommands - total)
                return "RHI-COMMAND-RECORDING-BUDGET-COMMAND-CAPACITY";
            total += increment.commands[i];
        }
        for (const auto field : fields())
            if (increment.*field.member > budget_.limits.*field.member - used_.*field.member)
                return field.diagnostic;
        return nullptr;
    }

    const char* consume(const CommandRecordingUsage& increment) noexcept
    {
        if (const auto* id = preflight(increment))
            return id;
        for (std::size_t i = 0u; i < increment.commands.size(); ++i) {
            used_.commands[i] += increment.commands[i];
            commandCount_ += increment.commands[i];
        }
        for (const auto field : fields())
            used_.*field.member += increment.*field.member;
        return nullptr;
    }

    void seal() noexcept { sealed_ = true; }

    const CommandRecordingUsage& used() const noexcept { return used_; }

    std::uint64_t commandCount() const noexcept { return commandCount_; }

private:
    explicit CommandRecordingMeter(const CommandRecordingBudget& budget) noexcept : budget_(budget)
    {}

    struct Field {
        std::uint64_t CommandRecordingUsage::* member;
        const char* diagnostic;
    };

    static constexpr std::array<Field, 5u> fields() noexcept
    {
        return {{{&CommandRecordingUsage::resourceReferences,
                     "RHI-COMMAND-RECORDING-BUDGET-REFERENCE-CAPACITY"},
            {&CommandRecordingUsage::descriptorElements,
                "RHI-COMMAND-RECORDING-BUDGET-DESCRIPTOR-CAPACITY"},
            {&CommandRecordingUsage::textBytes, "RHI-COMMAND-RECORDING-BUDGET-TEXT-CAPACITY"},
            {&CommandRecordingUsage::dynamicOffsets,
                "RHI-COMMAND-RECORDING-BUDGET-OFFSET-CAPACITY"},
            {&CommandRecordingUsage::pushBytes, "RHI-COMMAND-RECORDING-BUDGET-PUSH-CAPACITY"}}};
    }

    CommandRecordingBudget budget_;
    CommandRecordingUsage used_;
    std::uint64_t commandCount_ = 0u;
    bool sealed_ = false;
};

} // namespace arp::rhi
