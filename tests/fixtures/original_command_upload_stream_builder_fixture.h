#pragma once
#include "rhi/frontend/command_usage.h"

#include <array>

namespace arp::test {
// Shared cold fixture assembly. Arrays retain exact caller-authored ordinals;
// no native execution, observation, initialization or journal is synthesized.
class OriginalCommandUploadStreamBuilderFixture final {
public:
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepareRetained(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        const rhi::CommandUsage* commands,
        std::size_t commandCount,
        const rhi::OriginalCommandUsage::Reference* references,
        std::size_t referenceCount,
        const rhi::CommandVariableArguments::Input* fields,
        const rhi::OriginalCommandInputs::OriginalDescriptor* descriptors,
        std::size_t descriptorCount,
        const rhi::OriginalCommandInputsBudget& inputBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        const char*& diagnostic)
    {
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(
            sources, commands, commandCount, references, referenceCount, graphQuota, diagnostic);
        if (!usage)
            return {};
        const auto& authored = sources->originalBudget();
        auto store = rhi::CommandVariableArguments::prepareOriginal(fields,
            commandCount,
            {1u,
                true,
                static_cast<std::uint32_t>(commandCount),
                authored.maxDynamicOffsets,
                authored.maxLabelBytes,
                authored.maxPushBytes,
                graphQuota->limitBytes()},
            graphQuota,
            diagnostic);
        if (!store)
            return {};
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        if (!scalars)
            return {};
        return rhi::OriginalCommandInputs::prepareOriginal(
            scalars, descriptors, descriptorCount, inputBudget, graphQuota, diagnostic);
    }

    template <std::size_t S, std::size_t C, std::size_t R, std::size_t D>
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepare(
        const std::array<rhi::OriginalCommandResourceSources::Source, S>& retained,
        const std::array<rhi::CommandUsage, C>& commands,
        const std::array<rhi::OriginalCommandUsage::Reference, R>& references,
        const std::array<rhi::OriginalCommandInputs::OriginalDescriptor, D>& descriptors,
        rhi::OriginalCommandAdmissionBudget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        const char*& diagnostic,
        const std::array<std::string_view, C>& labels = {})
    {
        budget.maxCommands = C;
        budget.maxReferences = R;
        budget.maxInputDescriptors = D;
        // Usage + source table + scalar root + variable store + input root.
        budget.maxGraphOwners = 5u;
        for (const auto& source : retained) {
            ++budget.maxSourceOwners[static_cast<std::size_t>(
                rhi::OriginalCommandResourceSources::domain(source))];
            budget.maxGraphOwners += std::visit(
                [](const auto& value) {
                    using Lease =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    return Lease::retainedOwnerCount;
                },
                source);
        }
        using Domain = rhi::OriginalCommandAdmissionBudget::CpuDomain;
        budget.maxCpuBytes[static_cast<std::size_t>(Domain::Source)] = sourceQuota->limitBytes();
        budget.maxCpuBytes[static_cast<std::size_t>(Domain::Graph)] = graphQuota->limitBytes();
        auto sources = rhi::OriginalCommandResourceSources::prepareOriginal(
            retained.data(), S, budget, sourceQuota, diagnostic);
        if (!sources)
            return {};
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(
            sources, commands.data(), C, references.data(), R, graphQuota, diagnostic);
        if (!usage)
            return {};
        auto fields = scalarFields(commands, std::make_index_sequence<C>{});
        std::uint64_t textBytes = 0u;
        for (std::size_t i = 0u; i < C; ++i) {
            fields[i].text = labels[i];
            textBytes += labels[i].size();
        }
        auto store = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
            C,
            {1u, true, C, 0u, textBytes, 0u, graphQuota->limitBytes()},
            graphQuota,
            diagnostic);
        if (!store)
            return {};
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        if (!scalars)
            return {};
        return rhi::OriginalCommandInputs::prepareOriginal(
            scalars, descriptors.data(), D, graphQuota, diagnostic);
    }

private:
    template <std::size_t C, std::size_t... I>
    static auto scalarFields(
        const std::array<rhi::CommandUsage, C>& commands, std::index_sequence<I...>)
    {
        return std::array<rhi::CommandVariableArguments::Input, C>{
            {{*rhi::CommandScalarArguments::capture(
                commands[I].kind, rhi::CommandScalarArguments::None{})}...}};
    }
};
} // namespace arp::test
