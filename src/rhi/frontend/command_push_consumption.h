#pragma once
#include "rhi/frontend/command_arguments.h"
#include "rhi/semantics/push_constant_validation.h"

namespace arp::rhi {
// Ephemeral walk state only. Bytes stay in the original frozen argument store;
// no new store, owner, command stream or native execution right is introduced.
class OriginalCommandPushConsumption final {
public:
    void onBind() noexcept
    {
        pending_.onPassEnd();
        ready_ = false;
    }

    const char* update(const std::shared_ptr<const CommandVariableArguments>& arguments,
        std::size_t command,
        const PipelineLayoutDesc& layout,
        const ShaderReflectionDesc& reflection,
        ShaderStage pass,
        const OriginalCommandPushConsumptionBudget& budget,
        PushConstantsDesc& output) noexcept
    {
        output = {};
        if (const auto* id = budget.diagnostic())
            return id;
        if (!arguments || !arguments->hasQuotaBoundSharedOwner(arguments))
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-OWNER";
        if (command >= arguments->records().size())
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-COMMAND";
        const auto& record = arguments->records()[command];
        const auto* push = record.scalar.get<CommandScalarArguments::Push>();
        if (record.scalar.kind() != CommandUsageKind::PushConstants || !push
            || push->size != record.byteCount)
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-PAYLOAD";
        const auto* bytes = arguments->bytes(command);
        if (const auto* id = semantics::pushConstantCommandDiagnostic(
                push->stages, push->offset, push->size, bytes))
            return id;
        if (push->size % 4u)
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-ALIGNMENT";
        if (const auto* id = semantics::pushConstantBindingDiagnostic(layout.pushConstantRanges,
                pass,
                push->stages,
                push->offset,
                push->size,
                &reflection))
            return id;
        // Every selected consumer must receive this full block. A stage subset
        // could otherwise leave the other selected stage's bytes uninitialized.
        if (parseShaderReflectionPushConstantStages(reflection.pushConstantStages) != push->stages)
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-STAGE-COVERAGE";
        if (pending_.hasUnconsumedUpdate())
            return "[PC-E013] push constants already set without intervening draw/dispatch";
        if (updates_ == budget.maxUpdates || bytes_ > budget.maxBytes
            || push->size > budget.maxBytes - bytes_)
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-CAPACITY";
        ++updates_;
        bytes_ += push->size;
        pending_.onPush();
        ready_ = true;
        output = {push->stages, push->offset, push->size, bytes};
        return nullptr;
    }

    const char* consume(const ShaderReflectionDesc& reflection,
        const OriginalCommandPushConsumptionBudget& budget) noexcept
    {
        if (!reflection.pushConstantByteCount)
            return nullptr;
        if (budget.empty())
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-PROFILE";
        if (const auto* id = budget.diagnostic())
            return id;
        if (!ready_)
            return "[PC-E011] RHI-ORIGINAL-PUSH-CONSUMPTION-FRESH-BLOCK-REQUIRED";
        pending_.onConsume();
        return nullptr;
    }

    std::uint64_t updates() const noexcept { return updates_; }

    std::uint64_t bytes() const noexcept { return bytes_; }

private:
    semantics::PushConstantConsumptionState pending_;
    bool ready_ = false;
    std::uint64_t updates_ = 0u, bytes_ = 0u;
};
} // namespace arp::rhi
